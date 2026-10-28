/* main.c - htcompiler driver.
 *
 *   input.c -> Flex -> Bison -> AST -> semantic analysis
 *           -> [optional] optimization pipeline (see optimize.c)
 *           -> thread discovery -> resource profiler -> concurrency analysis
 *           -> contention graph -> mapping optimizer
 *           -> program_gen.c + program.htmeta.json
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ht.h"
#include "opt.h"
#include "llm.h"
#include "json.h"
#include "common.h"

extern FILE *yyin;
int yyparse(void);

static void usage(FILE *f)
{
    fputs(
"HT-Compass " HT_VERSION " - HyperThread-aware compiler framework for restricted pthread C\n"
"\n"
"Usage: htcompiler input.c [-o program_gen.c] [--json program.htmeta.json] [options]\n"
"\n"
"  -o FILE            generated C program            (default: program_gen.c)\n"
"  --json FILE        analysis report                (default: program.htmeta.json)\n"
"  --strategy S       mapping to emit: optimized (default) | naive | worst\n"
"                     ('naive' = creation-order pairing, 'worst' = max contention;\n"
"                      used as baselines when evaluating the optimizer)\n"
"  --policy P         pack (default): pair up all threads, two per physical core\n"
"                     spread: share a core only when there are more threads than\n"
"                             physical cores (as few SMT pairs as possible)\n"
"  --cores K          use only the first K physical cores of the topology\n"
"  --topology SPEC    SMT layout of the target, e.g. \"0,4;1,5;2,6;3,7\"\n"
"  --l1-kb N          L1 data cache size in KiB      (default: sysfs / 32)\n"
"  --l2-kb N          L2 cache size in KiB           (default: sysfs / 1024)\n"
"  --dump-ast         print the abstract syntax tree and exit\n"
"\n"
"Optimization pipeline:\n"
"  --optimize         ask the LOCAL language model (Ollama) where to optimize, then prove each\n"
"                     proposal safe, test it, time it and keep only the winners; the generated\n"
"                     program is built from the optimized code.  Needs Ollama and an installed\n"
"                     model (./run.sh llm-setup); if it cannot be used this stops with an error\n"
"                     and does NOT pretend any AI analysis happened\n"
"  --offline-optimize the same pipeline driven by the compiler's own rules; no model needed\n"
"  --llm-only         test only what the model proposed (default: also the compiler's own finds)\n"
"  --llm-model NAME   model to use                           (default: best installed code model)\n"
"  --llm-host H:P     Ollama address, loopback only          (default 127.0.0.1:11434, or $OLLAMA_HOST)\n"
"  --llm-timeout S    seconds to wait for the model          (default 180)\n"
"  --llm-no-autostart do not start 'ollama serve' automatically\n"
"  --llm-status       print the state of the local model service as JSON and exit\n"
"  --ai               with --opt-plan: also consult the model (planning only)\n"
"  --llm-dump-prompt F  write the exact prompt sent to the model\n"
"  --fp-reassoc       also allow changes that reorder floating-point sums (rounding differs)\n"
"  --no-verify        skip the differential test (relies on the static proof alone)\n"
"  --no-bench         select by the cost model instead of by timing\n"
"  --bench-runs N     timing runs per program               (default 5)\n"
"  --min-speedup X    measured speedup needed to accept     (default 1.03)\n"
"  --opt-rounds N     greedy rounds                         (default 4)\n"
"  --opt-budget N     candidates tested per round           (default 12)\n"
"  --opt-seeds N      random-data runs in the differential test (default 2)\n"
"  --run-timeout S    seconds allowed for one test run      (default 60)\n"
"  --run-mem-mb N     address-space limit for test runs     (default 4096, 0 = none)\n"
"  --cc PATH          C compiler for test builds            (default gcc)\n"
"  --opt-plan         only list opportunities and their safety; build and run nothing\n"
"  --export-prompt F  write a prompt you can give to any chat AI (no API needed)\n"
"  --suggest F        read that AI's reply (\"transform function line\" per line); advice only\n"
"                     changes the order of testing, the compiler still checks everything\n"
"  --emit-opt F       also write the optimized program without thread pinning\n"
"  --emit-test F      write the instrumented build used by the differential test\n"
"\n"
"  --quiet            no console summary\n"
"  --help             show this help\n"
"  --version          show version\n", f);
}


static int print_llm_status(const LlmConfig *cfg)
{
    LlmConfig c = *cfg;
    LlmStatus st;
    JBuf b;
    char model[96] = "", perr[300] = "";
    int i, up, picked = 0;
    const char *msg;

    c.autostart = 0;                                    /* a status query never starts anything */
    up = llm_status(&c, &st);
    if (up) picked = llm_pick_model(&c, &st, model, sizeof model, perr, sizeof perr);
    if (up && picked) msg = "ready";
    else if (up) msg = perr[0] ? perr : "no model is installed";
    else if (llm_ollama_installed()) msg = "Ollama is installed but not running; --optimize starts it automatically, or run: ollama serve";
    else msg = "Ollama is not installed; see QUICKSTART.md (Local AI setup)";

    jb_init(&b);
    jb_puts(&b, "{\"service\":{\"endpoint\":"); jb_printf(&b, "\"%s:%d\"", c.host, c.port);
    jb_printf(&b, ",\"reachable\":%s,\"ollama_installed\":%s,\"version\":", up ? "true" : "false", llm_ollama_installed() ? "true" : "false");
    jb_str(&b, up ? st.version : "");
    jb_puts(&b, ",\"error\":"); jb_str(&b, up ? "" : st.error);
    jb_puts(&b, "},\"models\":[");
    for (i = 0; up && i < st.nmodels; i++) { if (i) jb_putc(&b, ','); jb_str(&b, st.models[i]); }
    jb_puts(&b, "],\"selected_model\":"); jb_str(&b, picked ? model : "");
    jb_printf(&b, ",\"ready\":%s,\"message\":", (up && picked) ? "true" : "false");
    jb_str(&b, msg);
    jb_puts(&b, "}\n");
    fputs(b.p ? b.p : "{}\n", stdout);
    jb_free(&b);
    return (up && picked) ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *input = NULL, *out_c = "program_gen.c", *out_json = "program.htmeta.json";
    const char *strategy = "optimized", *topo_spec = NULL, *policy = "pack";
    long l1 = 0, l2 = 0, cores_cap = 0;
    int dump = 0, quiet = 0, i;
    HTContext *ctx;
    const Mapping *chosen;
    OptOptions opt;
    OptResult *ores = NULL;
    Node *prog;
    Topology topo;
    const char *emit_opt = NULL, *emit_test = NULL;

    int llm_status_only = 0, offline = 0, want_ai = 0;
    memset(&opt, 0, sizeof opt);
    llm_defaults(&opt.llm);
    opt.verify = 1; opt.bench = 1; opt.bench_runs = 5; opt.rounds = 4; opt.max_cands = 12;
    opt.seeds = 2; opt.min_speedup = 1.03; opt.timeout = 60; opt.mem_mb = 4096; opt.cc = "gcc";

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(stdout); return 0; }
        else if (!strcmp(a, "--version")) { printf("htcompiler %s\n", HT_VERSION); return 0; }
        else if (!strcmp(a, "-o") && i + 1 < argc) out_c = argv[++i];
        else if (!strcmp(a, "--json") && i + 1 < argc) out_json = argv[++i];
        else if (!strcmp(a, "--strategy") && i + 1 < argc) strategy = argv[++i];
        else if (!strcmp(a, "--policy") && i + 1 < argc) policy = argv[++i];
        else if (!strcmp(a, "--topology") && i + 1 < argc) topo_spec = argv[++i];
        else if (!strcmp(a, "--cores") && i + 1 < argc) cores_cap = atol(argv[++i]);
        else if (!strcmp(a, "--l1-kb") && i + 1 < argc) l1 = atol(argv[++i]);
        else if (!strcmp(a, "--l2-kb") && i + 1 < argc) l2 = atol(argv[++i]);
        else if (!strcmp(a, "--optimize")) { opt.enabled = 1; opt.use_llm = 1; }
        else if (!strcmp(a, "--offline-optimize")) { opt.enabled = 1; offline = 1; }
        else if (!strcmp(a, "--ai") || !strcmp(a, "--llm")) want_ai = 1;
        else if (!strcmp(a, "--llm-only")) opt.llm_only = 1;
        else if (!strcmp(a, "--llm-status")) llm_status_only = 1;
        else if (!strcmp(a, "--llm-no-autostart")) opt.llm.autostart = 0;
        else if (!strcmp(a, "--llm-model") && i + 1 < argc) snprintf(opt.llm.model, sizeof opt.llm.model, "%.95s", argv[++i]);
        else if (!strcmp(a, "--llm-timeout") && i + 1 < argc) opt.llm.timeout = atof(argv[++i]);
        else if (!strcmp(a, "--llm-max-reply") && i + 1 < argc) opt.llm.max_content = (size_t)atol(argv[++i]);
        else if (!strcmp(a, "--llm-host") && i + 1 < argc) {
            char e[200];
            if (!llm_set_endpoint(&opt.llm, argv[++i], e, sizeof e)) { fprintf(stderr, "htcompiler: %s\n", e); return 1; }
        }
        else if (!strcmp(a, "--llm-reply-file") && i + 1 < argc) opt.llm.reply_file = argv[++i];   /* TEST HOOK */
        else if (!strcmp(a, "--llm-dump-prompt") && i + 1 < argc) opt.llm_prompt_file = argv[++i];
        else if (!strcmp(a, "--fp-reassoc")) opt.fp_ok = 1;
        else if (!strcmp(a, "--no-verify")) opt.verify = 0;
        else if (!strcmp(a, "--no-bench")) opt.bench = 0;
        else if (!strcmp(a, "--opt-plan")) { opt.enabled = 1; opt.plan = 1; }
        else if (!strcmp(a, "--bench-runs") && i + 1 < argc) opt.bench_runs = atoi(argv[++i]);
        else if (!strcmp(a, "--min-speedup") && i + 1 < argc) opt.min_speedup = atof(argv[++i]);
        else if (!strcmp(a, "--opt-rounds") && i + 1 < argc) opt.rounds = atoi(argv[++i]);
        else if (!strcmp(a, "--opt-budget") && i + 1 < argc) opt.max_cands = atoi(argv[++i]);
        else if (!strcmp(a, "--opt-seeds") && i + 1 < argc) opt.seeds = atoi(argv[++i]);
        else if (!strcmp(a, "--run-timeout") && i + 1 < argc) opt.timeout = atof(argv[++i]);
        else if (!strcmp(a, "--run-mem-mb") && i + 1 < argc) opt.mem_mb = atol(argv[++i]);
        else if (!strcmp(a, "--cc") && i + 1 < argc) opt.cc = argv[++i];
        else if (!strcmp(a, "--suggest") && i + 1 < argc) { opt.suggest_file = argv[++i]; opt.enabled = 1; }
        else if (!strcmp(a, "--export-prompt") && i + 1 < argc) { opt.prompt_file = argv[++i]; if (!opt.enabled) { opt.enabled = 1; opt.plan = 1; } }
        else if (!strcmp(a, "--emit-opt") && i + 1 < argc) emit_opt = argv[++i];
        else if (!strcmp(a, "--emit-test") && i + 1 < argc) emit_test = argv[++i];
        else if (!strcmp(a, "--dump-ast")) dump = 1;
        else if (!strcmp(a, "--quiet")) quiet = 1;
        else if (a[0] == '-' && a[1]) { fprintf(stderr, "htcompiler: unknown option '%s'\n\n", a); usage(stderr); return 1; }
        else if (!input) input = a;
        else { fprintf(stderr, "htcompiler: more than one input file\n"); return 1; }
    }
    if (llm_status_only) return print_llm_status(&opt.llm);
    if (!input) { usage(stderr); return 1; }
    if (offline && opt.use_llm) { fprintf(stderr, "htcompiler: --optimize and --offline-optimize contradict each other\n"); return 1; }
    if (opt.plan && want_ai) opt.use_llm = 1;
    if (opt.llm.reply_file && !opt.use_llm) { fprintf(stderr, "htcompiler: --llm-reply-file needs --optimize\n"); return 1; }
    if (strcmp(strategy, "optimized") && strcmp(strategy, "naive") && strcmp(strategy, "worst")) {
        fprintf(stderr, "htcompiler: --strategy must be optimized, naive or worst\n");
        return 1;
    }
    if (strcmp(policy, "pack") && strcmp(policy, "spread")) {
        fprintf(stderr, "htcompiler: --policy must be pack or spread\n");
        return 1;
    }

    yyin = fopen(input, "r");
    if (!yyin) { perror(input); return 1; }
    g_filename = input;

    /* --- front end --- */
    if (yyparse() != 0 || g_errors) return 1;
    fclose(yyin);
    if (dump) { ast_dump(g_program, 0); return 0; }

    sema_run(g_program);
    if (g_errors) { fprintf(stderr, "%d error(s); compilation stopped.\n", g_errors); return 1; }

    /* --- topology (needed by the optimizer's cost model as well) --- */
    topo_detect(&topo, topo_spec, l1, l2);
    if (cores_cap > 0 && cores_cap < topo.ncores) {
        int c;
        topo.ncores = (int)cores_cap;
        topo.ncpus = 0;
        for (c = 0; c < topo.ncores; c++) topo.ncpus += topo.nsib[c];
    }

    /* --- optimization pipeline --- */
    prog = g_program;
    if (opt.enabled) {
        opt.topo = topo;
        opt.spread = !strcmp(policy, "spread");
        ores = calloc(1, sizeof *ores);
        prog = optimize_program(g_program, &opt, ores);
        if (ores->ai.failed) {
            fprintf(stderr, "htcompiler: AI optimization could not run, so no AI analysis was performed.\n"
                            "  %s\n", ores->ai.error);
            if (ores->ai.note[0] && !strstr(ores->ai.error, ores->ai.note)) fprintf(stderr, "  (%s)\n", ores->ai.note);
            fprintf(stderr, "  Set up the local model:    ./run.sh llm-setup\n"
                            "  Check the service:         ./htcompiler --llm-status\n"
                            "  Optimize without the AI:   ./htcompiler %s --offline-optimize\n", input);
            return 3;
        }
        if (ores->note[0]) ht_warn("%s", ores->note);
        g_errors = 0;
        sema_run(prog);
        if (g_errors) { fprintf(stderr, "internal error: optimized program failed semantic analysis\n"); return 1; }
        if (emit_opt && !codegen_emit_plain(emit_opt, prog, 0)) { perror(emit_opt); return 1; }
    } else if (emit_opt && !codegen_emit_plain(emit_opt, prog, 0)) { perror(emit_opt); return 1; }

    if (emit_test && !codegen_emit_plain(emit_test, prog, 1)) { perror(emit_test); return 1; }

    /* --- analysis --- */
    ctx = calloc(1, sizeof *ctx);
    if (!ctx) { fprintf(stderr, "out of memory\n"); return 2; }
    ctx->topo = topo;
    ctx->opt = ores;
    discover_threads(ctx, prog);
    if (g_errors) { fprintf(stderr, "%d error(s); compilation stopped.\n", g_errors); return 1; }
    if (ctx->nthreads == 0)
        ht_warn("no pthread_create call found: nothing to analyse");
    profile_threads(ctx);
    build_contention(ctx);
    build_mappings(ctx, !strcmp(policy, "spread"));
    if (ctx->nthreads > 2 * ctx->topo.ncores)
        ht_warn("%d threads but only %d SMT slots: some threads must share a CPU (oversubscribed)",
                ctx->nthreads, 2 * ctx->topo.ncores);

    chosen = !strcmp(strategy, "naive") ? &ctx->m_naive
           : !strcmp(strategy, "worst") ? &ctx->m_worst : &ctx->m_opt;

    /* --- back end --- */
    if (!codegen_emit(out_c, ctx, prog, chosen, strategy)) return 1;
    if (!write_json(out_json, ctx, chosen, strategy)) return 1;

    if (!quiet) {
        print_summary(ctx, chosen, strategy);
        printf("\nWrote %s and %s\n", out_c, out_json);
    }
    return 0;
}
