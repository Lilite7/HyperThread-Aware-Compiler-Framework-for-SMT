# HT-Compass

**A HyperThread-Aware Compiler Framework for SMT Resource Contention Prediction and Optimization in Parallel C Programs**

BCSE307P – Compiler Design Lab project · VIT Vellore

HT-Compass reads a restricted pthread C program, predicts how much its threads would
interfere with each other if they shared a physical core (SMT / Hyper-Threading), picks the
lowest-contention thread-to-core pairing, and emits

| output | content |
|---|---|
| `program_gen.c` | the original program plus CPU-affinity code that pins every thread according to the chosen mapping |
| `program.htmeta.json` | thread profiles, pairwise contention scores, detected conflicts, and the recommended mapping |

Everything is implemented in C with **Flex** (lexer) and **GNU Bison** (parser). No Clang, LLVM,
pycparser, Python, or external parser libraries are used; the compiler needs only `gcc`, `flex`,
`bison`, `make`.

```
input.c
  │  Flex lexer ──────── tokens (+ #include/#define handling, unsupported-construct errors)
  │  Bison parser ────── Abstract Syntax Tree
  ▼
Semantic analysis ─────── symbol tables, expression types, pthread-call validation
  ▼
Thread discovery ──────── pthread_create sites, worker functions, loop-expanded instances
  ▼
Static resource profiler  int/fp/div ops, loads/stores, access patterns, cache footprint, DRAM lines
  ▼
Concurrency analyzer ──── create/join event timeline -> which threads can overlap
  ▼
SMT contention graph ──── pair scores 0-100 from four resource components
  ▼
Mapping optimizer ─────── minimum-weight matching of threads onto SMT sibling slots
  ▼
Code generator + JSON ─── program_gen.c  +  program.htmeta.json
```

> **Quickest start:** `./run.sh` builds everything and opens the dashboard. See `QUICKSTART.md`.

## Build and run

```bash
make                       # builds ./htcompiler   (needs gcc, flex, bison)
make test                  # 115 checks: compiler, optimizer, fuzzing, mocked AI (no model needed)
make serve                 # dashboard at http://127.0.0.1:8765 (optional, needs python3)

./htcompiler tests/mixed.c -o program_gen.c --json program.htmeta.json
gcc -O2 -pthread -o program_gen program_gen.c -lm      # -lm only if you use sqrt() & co.
./program_gen
```

Other commands:

```bash
./htcompiler --help
./htcompiler --version
./htcompiler input.c --dump-ast                        # print the AST and stop
```

### Options

| option | meaning |
|---|---|
| `-o FILE` | generated C program (default `program_gen.c`) |
| `--json FILE` | analysis report (default `program.htmeta.json`) |
| `--topology "0,4;1,5;2,6;3,7"` | SMT layout of the **target** machine: cores separated by `;`, sibling logical CPUs by `,` |
| `--policy pack` *(default)* | pair up all threads, two per physical core (maximum SMT usage) |
| `--policy spread` | share a core only when there are more threads than physical cores |
| `--cores K` | pretend only the first K physical cores are available |
| `--strategy optimized` *(default)* / `naive` / `worst` | which mapping to emit. `naive` = creation-order pairing, `worst` = maximum contention. Use them as **baselines** when evaluating the optimizer |
| `--l1-kb N`, `--l2-kb N` | override cache sizes (default: sysfs, else 32 KiB / 1 MiB) |
| `--quiet` | no console summary |

Topology comes from `--topology`, else Linux sysfs
(`/sys/devices/system/cpu/cpuN/topology/thread_siblings_list`), else an assumed 4-core × 2-thread
layout. If the build machine has no SMT, adjacent CPUs are paired as *virtual* cores and a warning
is printed. **Pass `--topology` when you compile on one machine for another.**

## Local AI optimization (Ollama)

`./htcompiler prog.c --optimize` consults a **language model running on your own machine** to find optimization
opportunities specific to your program. No paid API, no API key, no account, no copying prompts into a website.

```
parse + analyse ─► prompt (source, stable region ids, strides, thread profiles) ─► local model (Ollama)
                                                                                        │ JSON proposals
   generate program_gen.c + program.htmeta.json ◄─ select ◄─ benchmark ◄─ differential test ◄─ dependence proof
                                                                                        ▲
                                              the compiler validates, maps and applies each proposal itself
```

**Who does what.** The model *discovers, explains and prioritises*. The compiler *applies and checks*. A proposal is a
suggestion, never proof: it can only choose where the compiler looks, in what order, and with which parameter. Every
change still needs a dependence proof, a passing differential test and a measured speedup. The model's text is never
executed or pasted into your program; a hostile reply containing C code is tested and shown to be ignored.

**Proposals use stable regions, not line numbers.** The prompt lists every loop as `function/L<n>` (the n-th loop of that
function) with its iteration count and how far each array reference moves per iteration, e.g. `b[i][j]: stride 3000 elements`.
The model answers with JSON such as

```json
{"proposals":[{"transformation":"interchange","region":"main/L3","parameters":{},
  "explanation":"...", "expected_benefit":"...", "preconditions":["..."], "risks":["..."], "priority":1}]}
```

Each reply is validated strictly: unknown regions, duplicate proposals (even re-spelled), JSON of the wrong shape, cut-off
or oversized replies are rejected and reported. A proposal for something the compiler cannot do (tiling, strength reduction,
redundant-computation elimination, parallelization, or anything invented) is reported as **proposed but unsupported** and
never claimed as applied. The compiler's own finders still run afterwards, so a weak model can only cost time, never
correctness; use `--llm-only` to test only what the model proposed.

### Commands

| command | meaning |
|---|---|
| `./htcompiler prog.c --optimize` | consult the local model automatically (starts `ollama serve` if it is not running) |
| `./htcompiler prog.c --offline-optimize` | no model: the compiler's own rules; works with nothing installed |
| `./htcompiler prog.c --opt-plan` | list opportunities and their safety; builds and runs nothing, no model |
| `./htcompiler prog.c --opt-plan --ai` | the same, but also ask the model (still builds and runs nothing) |
| `./htcompiler --llm-status` | is Ollama reachable, which models are installed, which would be used (JSON) |
| `--llm-model NAME`, `--llm-timeout S`, `--llm-host H:P`, `--llm-no-autostart`, `--llm-only`, `--llm-max-reply BYTES` | model, time limit (180 s), address (loopback only), and so on |
| `--llm-dump-prompt FILE` | write the exact text sent to the model |
| `./run.sh llm-setup` | choose a model for this machine and install it (`ollama pull`) |
| `./run.sh llm-status`, `./run.sh llm-test` | status; the REAL-inference integration test |

**If the model cannot be used, `--optimize` stops** with exit code 3, writes no output files, and prints what is wrong and how
to fix it. It never pretends that AI analysis happened and never silently falls back. `--offline-optimize` is the explicit
no-AI mode. The model is consulted once per run; the dashboard shows its status, its proposals and what happened to each.

### Setting up the model

Free, but it uses your hardware: a CPU-only laptop spends from tens of seconds to a few minutes of full CPU load per
consultation, and a model needs RAM. Downloading the model needs the internet **once**; afterwards inference works offline.
`./run.sh llm-setup` inspects RAM and GPU memory and chooses:

| your machine | model | download | memory while running |
|---|---|---|---|
| 8 GB+ GPU memory, or 16 GB+ RAM | `qwen2.5-coder:7b` | about 4.7 GB | 6 to 8 GB |
| 8 GB+ RAM | `qwen2.5-coder:3b` | about 1.9 GB | about 4 GB |
| 4 GB+ RAM | `qwen2.5-coder:1.5b` | about 1 GB | about 2 GB |
| less | `qwen2.5-coder:0.5b` | about 0.4 GB | poor proposals; the compiler still checks everything |

The sizes are Ollama's published approximations; the development environment could not reach the Ollama registry, so
**these tags were not verified there**. If a pull says the name was not found, look at https://ollama.com/library/qwen2.5-coder
and re-run with `MODEL=<tag> ./run.sh llm-setup`. Any model Ollama can run works (`--llm-model`); instruction-tuned code
models give by far the best proposals.

```bash
# 1. install Ollama once (Linux; macOS: brew install ollama; Windows: use WSL, see QUICKSTART.md)
curl -fsSL https://ollama.com/install.sh | sh
# 2. choose and download a model for this machine
./run.sh llm-setup               # same as: ollama pull qwen2.5-coder:3b
# 3. optimize
./htcompiler tests/opt/transpose.c --optimize
```

### What was tested, and what was not

- **Mocked** (`tests/run_llm_tests.sh`, part of `make test`): 54 checks with canned replies from `tests/llm/` and
  `tests/mock_ollama.py` (a stand-in that replays Ollama's real response formats). They cover valid, malformed, truncated, oversized,
  duplicate, unsafe, unsupported and hostile replies, missing models, an unreachable service, timeouts, chunked transfer,
  HTTP errors, non-loopback addresses, and an incorrect transformation caught by differential testing. These are **not**
  real inference, and the report labels a fixture reply as `reply-file (test hook, not real inference)`.
- **Real** (`./run.sh llm-test`, never part of `make test`): talks to the model actually installed, and prints SKIPPED (exit
  code 77), never PASSED, when there is none. It was run during development against a real Ollama 0.40.10 with a **tiny
  270M base model** (the only model reachable from the sandbox): the pipeline behaved correctly, but that model could not follow the
  instructions, so **nothing is known about proposal quality**. Proposal quality with a proper instruction-tuned model is untested.
- The JSON parser, HTTP client and reply validation also ran clean under AddressSanitizer and UBSan against every hostile fixture.

## Optimization pipeline

`htcompiler --optimize` first improves the *code* (guided by a local language model, see the next section), then maps the (improved) threads onto cores.
It works on **any program in the supported mini-C subset**: it looks for opportunities everywhere,
proves which are safe, and refuses (with the reason) whatever it cannot prove.

```bash
./htcompiler prog.c --optimize                 # the LOCAL AI proposes; the compiler proves, tests, times, applies
./htcompiler prog.c --offline-optimize         # the same pipeline driven by the compiler's own rules (no AI)
./htcompiler prog.c --opt-plan                 # only list opportunities and whether each is safe
./run.sh optimize                              # a worked example, then drop out/program.htmeta.json on the dashboard
```

| transformation | what it does | when it is allowed |
|---|---|---|
| **loop interchange** | swaps two perfectly nested loops so the inner loop walks memory sequentially | constant bounds; every dependence stays in order (distance-vector test); never reorders a floating-point sum unless you pass `--fp-reassoc` |
| **loop fusion** | merges two adjacent loops over the same range that share arrays | identical iteration space; no element is reached by the second loop before the first has finished with it |
| **loop unrolling** | runs several iterations per trip, with an epilogue loop for the remainder | constant trip count; loop variable not modified; iterations keep their exact original order, so no dependence can change |
| **loop-invariant code motion** | computes pure expressions that never change inside a loop once, before it | operands never modified in the loop; only `+ - *`, floating-point `/`, division by a positive constant, and math calls; globals only if no call or thread could change them |
| **scalar promotion** | keeps an array element that is the same on every iteration in a register | straight-line innermost loop, no other element of that array touched, no calls, and no other thread uses the array |

New transformations are added by writing a finder and an applier in `src/transform.c` (the registry is
`g_transforms[]`).

### What each stage establishes

| stage | what it shows | strength |
|---|---|---|
| dependence analysis (`src/depend.c`) | the transformation preserves every ordering that matters | **proof**, for the class it handles (constant bounds, affine subscripts, no calls). Anything else is refused |
| cost model | which candidates are worth testing; predicted memory traffic and contention | a heuristic, used to prioritise |
| differential test (`src/verify.c`) | original and changed program leave identical memory and output, on the program's own data and on random data | **evidence** on those inputs, not a proof for all inputs. Catches mistakes in the proof or the rewriting |
| benchmark | the change is faster on this machine | measurement: best-of-N with alternating order, the winner is re-measured, and a final end-to-end timing reverts everything if the sum is not an overall gain |

A candidate must pass **every** stage. `--no-verify` skips the differential test and `--no-bench` replaces
timing by the cost model; both are reported in the output so the strength of the decision is always visible.

### Manual alternative: any chat AI, by hand (optional)

The automatic local model above replaces this. The older manual workflow still works if you prefer a hosted chat AI:

```bash
./htcompiler prog.c --export-prompt prompt.txt     # a prompt with your program and its profile
# paste prompt.txt into any chat AI; save its reply as advice.txt, one suggestion per line:
#     interchange col_walk 46
./htcompiler prog.c --optimize --suggest advice.txt
```

Advice only changes the *order* in which opportunities are examined. Every suggestion is proved, tested and
timed like any other, and a suggestion that names nothing real is reported as rejected. The dashboard has the
same flow in its *Code changes* section.

### Options

| option | meaning |
|---|---|
| `--optimize` | run the pipeline, consulting the local model |
| `--offline-optimize` | run the pipeline with the compiler's own rules only |
| `--opt-plan` | discover opportunities and judge their safety only; builds and runs nothing |
| `--fp-reassoc` | also allow changes that reorder floating-point sums (results then differ in the last bits; the test compares with a tolerance) |
| `--no-verify` / `--no-bench` | skip the differential test / use the cost model instead of timing |
| `--bench-runs N`, `--min-speedup X` | timing runs (default 5, more for short programs) and the speedup needed (default 1.03) |
| `--opt-rounds N`, `--opt-budget N`, `--opt-seeds N` | greedy rounds (4), candidates tested per round (12), random-data runs per test (2) |
| `--run-timeout S`, `--run-mem-mb N`, `--cc PATH` | limits for each test run (60 s, 4096 MB) and the C compiler used for test builds |
| `--emit-opt F`, `--emit-test F` | write the optimized program (no pinning) / the instrumented build used by the differential test |

### Measured results

From one machine (a 1-CPU, no-SMT sandbox, timings of a few tenths of a second, so treat them as
illustrations, not benchmarks). Each number is what the pipeline measured and then confirmed.

| program | applied | measured |
|---|---|---|
| `tests/opt/transpose.c` | interchange of the column nest, then fusion | 1.88× and 1.06×; 0.138 s to 0.069 s end to end |
| `tests/opt/threads_walk.c` | interchange in both thread workers | about 1.4× and 1.7×; 0.077 s to 0.033 s end to end |
| `tests/opt/promotion.c` (matrix multiply) | interchange of the `j` and `k` loops | 1.25×; scalar promotion and fusion were proven safe but **rejected**, since they measured no gain (gcc -O2 already promotes) |
| `tests/opt/illegal_interchange.c` | nothing | the only candidate is refused: `a[i][j]` depends on `a[i-1][j+1]` |

### How the safety claim was tested

- `tests/fuzz_opt.py` generates random loop nests with random affine subscripts and compares the analysis against a
  brute-force interpreter that runs both loop orders on random data. In 6,000 random programs (about 2,300 "legal"
  verdicts) there were **no soundness violations**; the analysis refused roughly a fifth of the nests that are in fact
  legal (the safe direction to be wrong).
- The fuzzer was itself tested by sabotaging the analysis: with the interchange check disabled it reported 41
  violations in 600 programs, and with the fusion direction check disabled it reported 8 in 300.
- A test hook (`HT_TEST_FORCE_UNSAFE=1`) pushes an illegal change past the proof, and the differential test rejects it.
- The optimizer runs clean under AddressSanitizer and UBSan over every test program and the fuzzer.

**This is evidence, not a verified compiler.** The proof covers a restricted class; random testing cannot show the
absence of bugs.

## Dashboard

A browser dashboard turns `program.htmeta.json` into pictures: the placement of threads on cores,
a contention heat map, and a plain-language breakdown of every score.

**Without a server (works offline).** Open `frontend/index.html` in a browser and drop
`program.htmeta.json` on it (add `program_gen.c` to see the generated code). Two sample reports
are built in.

**With the analysis server.** The *Analyze code* button pastes or edits C source, runs `htcompiler`,
and shows the result straight away, with compiler errors marked on the offending line:

```bash
make serve                 # same as: python3 frontend/server.py
# then open http://127.0.0.1:8765
```

| part of the page | what it shows |
|---|---|
| Code changes | every optimization the compiler considered: what it did, why it is safe, how it was tested and timed, a code diff for applied ones, and the reason for every rejection. Also the "ask an AI" panel |
| core board | each physical core with its two SMT sibling slots; the link between two threads is coloured by their contention score. Switch between *Optimized*, *Creation order* and *As emitted* and the thread tiles slide to their new slots |
| Threads | behaviour class, operation count, a five-bar demand profile, data touched, and where the thread is placed |
| Who clashes with whom | heat map of all pair scores; pairs placed on the same core in the current view are outlined, pairs that never run together are hatched |
| detail panel | the four weighted components behind a pair's score, the overlap and lock factors, and what the compiler noticed |
| Machine and model | topology, cache sizes, score weights, compiler warnings |
| Output files | generated C, report JSON, and your source, with copy and download |

The server is the only Python in the project and is optional; the compiler stays pure C. It uses just
the standard library, listens on `127.0.0.1` only, rejects foreign `Host`/`Origin` headers, validates
every option before it reaches the compiler (no shell is involved), and limits request size and run
time. The compiler only parses and analyses a program, it never executes it. `make test-frontend`
runs the server's 10 tests.

## Example

```
$ ./htcompiler tests/mixed.c --topology "0,4;1,5;2,6;3,7"

  id  thread        class                           ops     bw footprint
  T0  stream_a      memory-bound                1.6e+08   0.75  45.8 MiB
  T1  stream_b      memory-bound                1.6e+08   0.75  45.8 MiB
  T2  fp_compute_1  compute-bound (floating point) 1.5e+08 0.00       0 B
  T3  fp_compute_2  compute-bound (floating point) 1.5e+08 0.00       0 B

Contention scores (0-100, higher = worse when sharing a core):
  T0-T1   62.1  high    memory bandwidth: both threads stream data from beyond the L2 cache
  T0-T2    8.7  low     execution units: both threads demand floating-point units
  ...
Mapping (optimized, exact-dp):
  core 0 (CPU 0 4): T1 stream_b + T3 fp_compute_2
  core 1 (CPU 1 5): T0 stream_a + T2 fp_compute_1

Predicted total contention: naive 76.5 -> optimized 17.4  (77% lower)
```

Full example outputs are in `examples/`.

In the generated program, thread creation becomes

```c
__ht_create(&t[0], stream_a, NULL, 0);       /* was: pthread_create(&t[0], NULL, stream_a, NULL); */
```

where `__ht_create` pins the new thread through `pthread_attr_setaffinity_np` using the
compile-time table `__ht_cpu_map[]`. If the CPU does not exist on the machine that runs the
program, it falls back to an unpinned `pthread_create`, so the program always still runs.

## Supported mini-C

| category | supported |
|---|---|
| types | `int`, `float`, `double`, `void`, `pthread_t`, `pthread_mutex_t` |
| variables | global and local; 1-D and 2-D arrays with literal (or `#define`d) sizes |
| operators | `+ - * / %`, `< > <= >= == !=`, `&& \|\| !`, `= += -= *= /=`, `++`/`--` |
| statements | `if`/`else`, `for` (init may declare a variable), `while`, `return` |
| functions | definitions and calls (no prototypes needed – the generator emits them) |
| library | `printf`, `puts`, `sqrt sin cos tan exp log pow fabs floor ceil`, `rand`, `srand` |
| pthreads | `pthread_create(&t, NULL, worker, arg)`, `pthread_join(t, NULL)`, optional `pthread_mutex_lock/unlock(&m)` |
| preprocessor | `#include …` (passed through) and `#define NAME number` (expanded) |

Thread functions must look like `void *worker(void *arg) { … return NULL; }`.
`pthread_create` may be called directly, or inside **one** `for` loop with constant bounds
(`for (i = 0; i < 8; i++)`), which creates 8 thread instances.

Anything else is rejected with a precise message, e.g.

```
Unsupported construct at line 23: switch statement
Unsupported construct at line 5: pointer dereference
Semantic error at line 5: undeclared identifier 'q'
Syntax error at line 6: unexpected 'return'
Thread analysis error at line 11: pthread_create inside nested loops is not supported
```

## Project layout

```
run.sh            one-command launcher (dashboard / demo / test / build)
QUICKSTART.md     three-step guide, Windows (WSL) notes, troubleshooting
generated/        pre-generated scanner/parser so flex+bison are optional (make regen refreshes)
src/lexer.l       Flex scanner (tokens, #include/#define, unsupported-construct errors)
src/parser.y      Bison grammar -> AST (no shift/reduce conflicts)
src/ast.[ch]      generic AST node, constructors, --dump-ast
src/sema.[ch]     scoped symbol table, expression typing, pthread call checks
src/astutil.c      AST clone, canonical expression keys, renaming
src/depend.c       dependence analysis (distance vectors) for interchange and fusion
src/transform.c    transformation registry (finder / precondition checker / applier): interchange, fusion, promotion, unrolling, licm
src/json.c         bounded JSON parser for untrusted model output
src/llm.c          Ollama client: loopback-only HTTP, timeouts, size caps, status, autostart
src/advisor.c      prompt construction, region ids, strict validation of the model's proposals
src/optimize.c     optimization driver: rounds, evaluation, selection, advice
src/verify.c       differential testing and benchmarking
src/runner.c       runs the C compiler / programs under test (no shell, time and memory limits)
src/analysis.c    constant folding, canonical-loop recognition, thread discovery, create/join timeline
src/profiler.c    static resource profile of every thread function
src/contention.c  pair contention scores + mapping optimizer (exact DP / greedy)
src/topology.c    SMT sibling pairs and cache sizes (sysfs / --topology)
src/codegen.c     AST unparser + affinity injection  -> program_gen.c
src/report.c      JSON writer -> program.htmeta.json, console summary
src/main.c        CLI and pipeline driver
frontend/         dashboard: index.html (self-contained), server.py (optional analysis server), test_server.py
tools/llm_setup.sh pick and install a model for this machine
tests/llm/         fixtures for the mocked AI tests;  tests/mock_ollama.py  mock Ollama;  tests/llm_integration.sh  REAL inference test
tests/            valid programs, tests/errors/ (must be rejected), tests/opt/ (optimizer cases), fuzz_opt.py, run_tests.sh
bench/            run_bench.sh - baseline / naive / optimized / worst timing
examples/         sample generated C, JSON and console output
docs/DESIGN.md    design notes, cost model, report mapping, viva Q&A
```

## Evaluating on real hardware

`make test` proves the compiler and the generated code are correct. Whether the mapping makes the
program *faster* has to be measured on a machine with SMT:

```bash
bench/run_bench.sh tests/mixed.c 7                 # 7 interleaved runs per variant
HT_FLAGS="--cores 2" bench/run_bench.sh tests/mixed.c 7
```

It builds `baseline` (no pinning), `naive`, `optimized`, and `worst` variants from the same source
and prints median wall times next to the compiler's predicted contention. Use `perf stat` for cache
and bandwidth counters. Interpreting results honestly matters for the report: if `optimized` does
not beat `naive`, say so and discuss which model assumption failed.

## Limitations

These are deliberate simplifications and belong in the report's limitations section.

- **The contention model is a static heuristic.** Weights and thresholds (`src/ht.h`) were chosen by
  reasoning, not fitted to hardware measurements.
- Loop trip counts must be compile-time constants; otherwise `DEFAULT_TRIPS = 64` is assumed and
  counted in the report (`assumed_trip_counts`).
- Scalars are assumed to live in registers. An array index computed into a scalar temporary
  (`int k = i*N + j; a[k]`) is treated as loop-invariant, which under-estimates memory traffic.
- Both branches of an `if` are weighted 50/50.
- Thread arguments are not supported (`void *arg` is ignored), so all instances of a worker are identical.
- Thread overlap is derived from source order of `pthread_create`/`pthread_join`; thread creation inside
  called functions or nested loops is not followed.
- The only code transformation is **thread pinning**; loops are not restructured.
- Pinning uses `pthread_attr_setaffinity_np` (Linux/glibc). Mappings are computed for the topology
  known at compile time.
- The mapper is exact (bitmask DP) for up to 20 matching nodes and greedy beyond that.

### Optimizer limitations

- **Scope of "any code".** Any program in the supported subset is accepted, but a transformation is only applied
  where its preconditions hold: loops with compile-time constant bounds, subscripts that are affine in the loop
  variables, no calls other than math functions, no `return` inside the region. Everything else is refused, with
  the reason shown, never guessed.
- The differential test shows agreement on the inputs it tried (the program's own data plus a few random data
  sets). It is not a proof for every input; the proof is the dependence analysis, and it covers only the class above.
- A scalar temporary used as a subscript (`int k = i*N + j; a[k]`) is not understood, so such nests are refused.
- Floating-point sums are not reordered unless you pass `--fp-reassoc`; with it, results differ in the last bits.
- Timing is only as good as the machine: runs of a few hundredths of a second are decided by the cost model with a
  stricter threshold, and one-CPU sandboxes show nothing about SMT. Re-measure on the target machine.
- gcc already performs some of these transformations at `-O2` (scalar promotion in particular), so a "safe but
  rejected: no measurable gain" verdict is expected and correct.
- Promotion and the other rewrites assume the program is data-race-free apart from what the compiler can see;
  global arrays shared with other threads are never promoted.
- `--optimize` builds and **runs** your program (with time and memory limits) to test and time it. Only optimize
  code you trust to run.
