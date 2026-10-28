/* llm.c - minimal HTTP/1.1 client for Ollama's native API (/api/version, /api/tags, /api/chat).
 *
 * Written against the real server's behaviour:
 *   GET  /api/version -> {"version":"0.40.10"}
 *   GET  /api/tags    -> {"models":[{"name":"qwen2.5-coder:3b",...}]}
 *   POST /api/chat    -> {"message":{"role":"assistant","content":"..."},"done":true,"done_reason":"stop",
 *                         "prompt_eval_count":N,"eval_count":N,...}
 *   unknown model     -> HTTP 404 {"error":"model 'x' not found"}
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "llm.h"
#include "json.h"

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* ---------------------------------------------------------------- configuration */

void llm_defaults(LlmConfig *c)
{
    const char *h = getenv("OLLAMA_HOST"), *m = getenv("HTC_LLM_MODEL");
    char err[100];
    memset(c, 0, sizeof *c);
    snprintf(c->host, sizeof c->host, "127.0.0.1");
    c->port = 11434;
    c->timeout = 180;
    c->max_body = 1024 * 1024;
    c->max_content = 64 * 1024;
    c->autostart = 1;
    c->num_ctx = 8192;
    c->num_predict = 1536;
    if (getenv("HTC_LLM_NO_AUTOSTART")) c->autostart = 0;         /* used by the tests */
    if (m && *m) snprintf(c->model, sizeof c->model, "%.95s", m);
    if (h && *h) (void)llm_set_endpoint(c, h, err, sizeof err);      /* silently ignored if not loopback */
}

int llm_set_endpoint(LlmConfig *c, const char *spec, char *err, size_t en)
{
    char host[64] = "", *colon;
    const char *p = spec;
    int port = 11434;
    if (!strncmp(p, "http://", 7)) p += 7;
    else if (!strncmp(p, "https://", 8)) { snprintf(err, en, "https is not supported (the local server speaks plain HTTP)"); return 0; }
    snprintf(host, sizeof host, "%.63s", p);
    { char *slash = strchr(host, '/'); if (slash) *slash = 0; }
    colon = strrchr(host, ':');
    if (colon) { port = atoi(colon + 1); *colon = 0; }
    if (!*host) snprintf(host, sizeof host, "127.0.0.1");
    if (!strcmp(host, "localhost")) snprintf(host, sizeof host, "127.0.0.1");
    if (strncmp(host, "127.", 4) != 0) {
        snprintf(err, en, "the language model must run on this machine: '%s' is not a loopback address", host);
        return 0;
    }
    if (port < 1 || port > 65535) { snprintf(err, en, "bad port in '%s'", spec); return 0; }
    snprintf(c->host, sizeof c->host, "%s", host);
    c->port = port;
    return 1;
}

/* ---------------------------------------------------------------- HTTP */

typedef struct {
    char *body;
    size_t len;
    int status;
    int timed_out, too_large;
    char err[300];
} Http;

static void http_free(Http *h) { free(h->body); h->body = NULL; }

static int connect_loopback(const LlmConfig *c, double deadline, char *err, size_t en)
{
    struct sockaddr_in sa;
    int fd = socket(AF_INET, SOCK_STREAM, 0), fl;
    if (fd < 0) { snprintf(err, en, "socket: %s", strerror(errno)); return -1; }
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)c->port);
    if (inet_pton(AF_INET, c->host, &sa.sin_addr) != 1) { close(fd); snprintf(err, en, "bad address %s", c->host); return -1; }
    fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0 && errno != EINPROGRESS) {
        snprintf(err, en, "cannot connect to %s:%d (%s)", c->host, c->port, strerror(errno));
        close(fd);
        return -1;
    }
    {
        struct pollfd pf = { fd, POLLOUT, 0 };
        int left = (int)((deadline - now_s()) * 1000);
        int soerr = 0;
        socklen_t sl = sizeof soerr;
        if (left < 1) left = 1;
        if (poll(&pf, 1, left) <= 0) { snprintf(err, en, "connection to %s:%d timed out", c->host, c->port); close(fd); return -1; }
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr) { snprintf(err, en, "cannot connect to %s:%d (%s)", c->host, c->port, strerror(soerr)); close(fd); return -1; }
    }
    return fd;
}

static int send_all(int fd, const char *p, size_t n, double deadline)
{
    while (n) {
        struct pollfd pf = { fd, POLLOUT, 0 };
        int left = (int)((deadline - now_s()) * 1000);
        ssize_t w;
        if (left < 1) return 0;
        if (poll(&pf, 1, left) <= 0) return 0;
        w = send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0) { if (errno == EAGAIN || errno == EINTR) continue; return 0; }
        p += w; n -= (size_t)w;
    }
    return 1;
}

/* decode a chunked body in place; returns decoded length or (size_t)-1 */
static size_t dechunk(char *buf, size_t len, int *complete)
{
    size_t in = 0, out = 0;
    *complete = 0;
    while (in < len) {
        char *eol = memchr(buf + in, '\n', len - in);
        unsigned long sz;
        char *end;
        if (!eol) return out;                              /* incomplete size line */
        sz = strtoul(buf + in, &end, 16);
        if (end == buf + in) return (size_t)-1;
        in = (size_t)(eol - buf) + 1;
        if (sz == 0) { *complete = 1; return out; }
        if (in + sz > len) { memmove(buf + out, buf + in, len - in); return out + (len - in); }
        memmove(buf + out, buf + in, sz);
        out += sz;
        in += sz;
        if (in < len && buf[in] == '\r') in++;
        if (in < len && buf[in] == '\n') in++;
    }
    return out;
}

/* One request.  `body` may be NULL (GET).  Result in *h. */
static int http_do(const LlmConfig *c, const char *method, const char *path, const char *body,
                   double timeout, Http *h)
{
    double deadline = now_s() + timeout;
    int fd;
    char *req;
    size_t rl, cap = 0, len = 0, hdr_end = 0;
    char *buf = NULL;
    int have_hdr = 0, chunked = 0;
    long clen = -1;

    memset(h, 0, sizeof *h);
    fd = connect_loopback(c, deadline, h->err, sizeof h->err);
    if (fd < 0) return 0;

    rl = strlen(path) + (body ? strlen(body) : 0) + 256;
    req = malloc(rl);
    if (!req) { close(fd); snprintf(h->err, sizeof h->err, "out of memory"); return 0; }
    if (body)
        snprintf(req, rl, "%s %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\nAccept: application/json\r\n"
                          "Content-Length: %zu\r\nConnection: close\r\n\r\n%s", method, path, c->host, c->port, strlen(body), body);
    else
        snprintf(req, rl, "%s %s HTTP/1.1\r\nHost: %s:%d\r\nAccept: application/json\r\nConnection: close\r\n\r\n",
                 method, path, c->host, c->port);
    if (!send_all(fd, req, strlen(req), deadline)) {
        free(req); close(fd);
        h->timed_out = now_s() >= deadline;
        snprintf(h->err, sizeof h->err, "could not send the request to the local server");
        return 0;
    }
    free(req);

    for (;;) {
        struct pollfd pf = { fd, POLLIN, 0 };
        int left = (int)((deadline - now_s()) * 1000);
        ssize_t r;
        if (left < 1) {
            h->timed_out = 1;
            snprintf(h->err, sizeof h->err, "no answer from the local server within %.0f s", timeout);
            free(buf); close(fd);
            return 0;
        }
        if (poll(&pf, 1, left > 500 ? 500 : left) <= 0) continue;
        if (len + 4096 + 1 > cap) {
            cap = cap ? cap * 2 : 8192;
            buf = realloc(buf, cap);
            if (!buf) { close(fd); snprintf(h->err, sizeof h->err, "out of memory"); return 0; }
        }
        r = recv(fd, buf + len, cap - len - 1, 0);
        if (r < 0) { if (errno == EAGAIN || errno == EINTR) continue; break; }
        if (r == 0) break;                                   /* peer closed: whole response read */
        len += (size_t)r;
        buf[len] = 0;
        if (!have_hdr) {
            char *e = strstr(buf, "\r\n\r\n");
            if (e) {
                char *p;
                have_hdr = 1;
                hdr_end = (size_t)(e - buf) + 4;
                h->status = atoi(buf + 9);
                for (p = buf; p < e + 2; ) {                       /* every header line, including the last */
                    char *nl = memchr(p, '\n', (size_t)((e + 4) - p));
                    char saved;
                    if (!nl) break;
                    saved = *nl;
                    *nl = 0;                                        /* look at this line only */
                    if (!strncasecmp(p, "Content-Length:", 15)) clen = atol(p + 15);
                    if (!strncasecmp(p, "Transfer-Encoding:", 18) && strcasestr(p, "chunked")) chunked = 1;
                    *nl = saved;
                    p = nl + 1;
                }
                if (clen > 0 && (size_t)clen > c->max_body) {
                    h->too_large = 1;
                    snprintf(h->err, sizeof h->err, "the reply is %ld bytes; the limit is %zu", clen, c->max_body);
                    free(buf); close(fd);
                    return 0;
                }
            }
        }
        if (have_hdr && len - hdr_end > c->max_body) {
            h->too_large = 1;
            snprintf(h->err, sizeof h->err, "the reply exceeded the %zu byte limit and was discarded", c->max_body);
            free(buf); close(fd);
            return 0;
        }
        if (have_hdr && !chunked && clen >= 0 && len - hdr_end >= (size_t)clen) break;
        if (have_hdr && chunked) {
            /* stop as soon as the terminating zero-length chunk has arrived */
            if (len >= 5 && !memcmp(buf + len - 5, "0\r\n\r\n", 5)) break;
        }
    }
    close(fd);
    if (!have_hdr) { free(buf); snprintf(h->err, sizeof h->err, "the local server sent no valid HTTP reply"); return 0; }
    h->len = len - hdr_end;
    memmove(buf, buf + hdr_end, h->len);
    buf[h->len] = 0;
    if (chunked) {
        int complete;
        size_t n = dechunk(buf, h->len, &complete);
        if (n == (size_t)-1) { free(buf); snprintf(h->err, sizeof h->err, "bad chunked encoding from the server"); return 0; }
        h->len = n;
        buf[n] = 0;
    }
    h->body = buf;
    return 1;
}

/* ---------------------------------------------------------------- status / service */

int llm_model_installed(const LlmStatus *st, const char *want)
{
    int i;
    size_t wl = strlen(want);
    int has_tag = strchr(want, ':') != NULL;
    for (i = 0; i < st->nmodels; i++) {
        if (!strcmp(st->models[i], want)) return 1;
        if (!has_tag && !strncmp(st->models[i], want, wl) && !strcmp(st->models[i] + wl, ":latest")) return 1;
    }
    return 0;
}

int llm_status(const LlmConfig *c, LlmStatus *st)
{
    Http h;
    char err[200];
    JVal *j;
    int i;
    memset(st, 0, sizeof *st);
    if (!http_do(c, "GET", "/api/version", NULL, 5, &h)) {
        snprintf(st->error, sizeof st->error, "%s", h.err);
        return 0;
    }
    if (h.status != 200) {
        snprintf(st->error, sizeof st->error, "the server answered HTTP %d (is this Ollama?)", h.status);
        http_free(&h);
        return 0;
    }
    j = json_parse(h.body, h.len, err, sizeof err);
    http_free(&h);
    if (!j || !jget(j, "version")) {
        snprintf(st->error, sizeof st->error, "the server's /api/version reply was not Ollama's");
        json_free(j);
        return 0;
    }
    st->reachable = 1;
    snprintf(st->version, sizeof st->version, "%.31s", jstr(jget(j, "version"), "?"));
    json_free(j);

    if (!http_do(c, "GET", "/api/tags", NULL, 10, &h)) {
        snprintf(st->error, sizeof st->error, "could not list models: %.250s", h.err);
        return 1;
    }
    j = json_parse(h.body, h.len, err, sizeof err);
    http_free(&h);
    if (j) {
        const JVal *m = jget(j, "models");
        for (i = 0; i < jlen(m) && st->nmodels < 24; i++) {
            const char *nm = jstr(jget(jat(m, i), "name"), NULL);
            if (nm) snprintf(st->models[st->nmodels++], sizeof st->models[0], "%.95s", nm);
        }
    }
    json_free(j);
    return 1;
}

static int find_in_path(const char *prog, char *out, size_t n)
{
    const char *path = getenv("PATH");
    char *copy, *save = NULL, *dir;
    if (!path) return 0;
    copy = strdup(path);
    for (dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        snprintf(out, n, "%s/%s", dir, prog);
        if (access(out, X_OK) == 0) { free(copy); return 1; }
    }
    free(copy);
    return 0;
}

int llm_ollama_installed(void)
{
    char exe[600];
    return find_in_path("ollama", exe, sizeof exe);
}

int llm_start_service(const LlmConfig *c, char *msg, size_t n)
{
    char exe[600], logp[300];
    const char *tmp = getenv("TMPDIR");
    pid_t pid;
    LlmStatus st;
    double until;

    if (!find_in_path("ollama", exe, sizeof exe)) {
        snprintf(msg, n, "Ollama is not installed (no 'ollama' program on PATH). Install it from https://ollama.com/download");
        return 0;
    }
    snprintf(logp, sizeof logp, "%s/htcompass_ollama.log", tmp && *tmp ? tmp : "/tmp");
    fflush(NULL);
    pid = fork();
    if (pid < 0) { snprintf(msg, n, "could not start Ollama: %s", strerror(errno)); return 0; }
    if (pid == 0) {
        int fd;
        setsid();
        fd = open(logp, O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
        fd = open("/dev/null", O_RDONLY);
        if (fd >= 0) { dup2(fd, 0); close(fd); }
        execl(exe, exe, "serve", (char *)NULL);
        _exit(127);
    }
    until = now_s() + 30;
    while (now_s() < until) {
        int status;
        struct timespec ts = { 0, 300 * 1000000L };
        if (waitpid(pid, &status, WNOHANG) == pid) {
            snprintf(msg, n, "'ollama serve' exited immediately (see %s)", logp);
            return 0;
        }
        if (llm_status(c, &st)) {
            snprintf(msg, n, "started 'ollama serve' (pid %d, log: %s); it keeps running until you stop it", (int)pid, logp);
            return 1;
        }
        nanosleep(&ts, NULL);
    }
    snprintf(msg, n, "started 'ollama serve' but it did not answer within 30 s (see %s)", logp);
    return 0;
}

int llm_ensure(const LlmConfig *c, LlmStatus *st, char *note, size_t nn)
{
    note[0] = 0;
    if (llm_status(c, st)) return 1;
    if (!c->autostart) return 0;
    if (strcmp(c->host, "127.0.0.1") != 0) return 0;
    if (llm_start_service(c, note, nn)) return llm_status(c, st);
    snprintf(st->error, sizeof st->error, "%s", note);
    return 0;
}

/* Prefer instruction-tuned code models; fall back to anything installed. */
static const char *PREFERRED[] = {
    "qwen2.5-coder:7b", "qwen2.5-coder:3b", "qwen2.5-coder:1.5b", "qwen2.5-coder:0.5b",
    "qwen2.5-coder", "deepseek-coder", "codellama", "codegemma", "starcoder", "qwen3", "llama3", "qwen2.5", NULL
};

int llm_pick_model(const LlmConfig *c, const LlmStatus *st, char *out, size_t n, char *err, size_t en)
{
    int i, k;
    if (c->model[0]) {
        if (llm_model_installed(st, c->model)) { snprintf(out, n, "%s", c->model); return 1; }
        snprintf(err, en, "the model '%s' is not installed in Ollama. Install it with:  ollama pull %s", c->model, c->model);
        return 0;
    }
    if (st->nmodels == 0) {
        snprintf(err, en, "no model is installed in Ollama. Install one with:  ./run.sh llm-setup   (or: ollama pull qwen2.5-coder:3b)");
        return 0;
    }
    for (k = 0; PREFERRED[k]; k++)
        for (i = 0; i < st->nmodels; i++)
            if (!strncmp(st->models[i], PREFERRED[k], strlen(PREFERRED[k]))) { snprintf(out, n, "%s", st->models[i]); return 1; }
    snprintf(out, n, "%s", st->models[0]);
    return 1;
}

/* ---------------------------------------------------------------- chat */

void llm_reply_free(LlmReply *r) { free(r->content); r->content = NULL; }

int llm_chat(const LlmConfig *c, const char *model, const char *system, const char *user, LlmReply *r)
{
    JBuf b;
    Http h;
    JVal *j;
    char jerr[200];
    const JVal *msg;
    double t0 = now_s();

    memset(r, 0, sizeof *r);

    if (c->reply_file) {                                  /* TEST HOOK: never used for real inference */
        FILE *f = fopen(c->reply_file, "r");
        long n;
        if (!f) { snprintf(r->error, sizeof r->error, "cannot open the reply file %s", c->reply_file); return 0; }
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        if (n < 0 || (size_t)n > c->max_content) { fclose(f); r->too_large = 1; snprintf(r->error, sizeof r->error, "the reply file is larger than the %zu byte limit", c->max_content); return 0; }
        r->content = malloc((size_t)n + 1);
        if (r->content) { n = (long)fread(r->content, 1, (size_t)n, f); r->content[n] = 0; }
        fclose(f);
        snprintf(r->done_reason, sizeof r->done_reason, "file");
        r->ok = r->content != NULL;
        return r->ok;
    }

    jb_init(&b);
    jb_puts(&b, "{\"model\":"); jb_str(&b, model);
    jb_puts(&b, ",\"stream\":false,\"format\":\"json\",\"messages\":[");
    jb_puts(&b, "{\"role\":\"system\",\"content\":"); jb_str(&b, system);
    jb_puts(&b, "},{\"role\":\"user\",\"content\":"); jb_str(&b, user);
    jb_printf(&b, "}],\"options\":{\"temperature\":0.1,\"num_ctx\":%d,\"num_predict\":%d}}", c->num_ctx, c->num_predict);
    if (b.oom) { jb_free(&b); snprintf(r->error, sizeof r->error, "out of memory"); return 0; }

    if (!http_do(c, "POST", "/api/chat", b.p, c->timeout, &h)) {
        jb_free(&b);
        r->timed_out = h.timed_out;
        r->too_large = h.too_large;
        snprintf(r->error, sizeof r->error, "%s", h.err);
        return 0;
    }
    jb_free(&b);
    r->http_status = h.status;
    r->seconds = now_s() - t0;

    j = json_parse(h.body, h.len, jerr, sizeof jerr);
    if (h.status != 200) {
        const char *e = j ? jstr(jget(j, "error"), NULL) : NULL;
        if (h.status == 404 && e && strstr(e, "not found"))
            snprintf(r->error, sizeof r->error, "Ollama says: %.150s. Install it with:  ollama pull %s", e, model);
        else
            snprintf(r->error, sizeof r->error, "Ollama returned HTTP %d%s%.200s", h.status, e ? ": " : "", e ? e : "");
        json_free(j);
        http_free(&h);
        return 0;
    }
    http_free(&h);
    if (!j) {
        if (strstr(jerr, "too long") || strstr(jerr, "too large") || strstr(jerr, "too many")) {
            r->too_large = 1;
            snprintf(r->error, sizeof r->error, "the model's reply is larger than the size limit (%s) and was discarded", jerr);
        } else {
            snprintf(r->error, sizeof r->error, "Ollama's reply was not valid JSON (%s)", jerr);
        }
        return 0;
    }
    msg = jget(j, "message");
    if (!msg || !jstr(jget(msg, "content"), NULL)) {
        snprintf(r->error, sizeof r->error, "Ollama's reply has no message content");
        json_free(j);
        return 0;
    }
    {
        const char *content = jstr(jget(msg, "content"), "");
        size_t cl = strlen(content);
        r->prompt_tokens = (int)jnum(jget(j, "prompt_eval_count"), 0);
        r->eval_tokens = (int)jnum(jget(j, "eval_count"), 0);
        snprintf(r->done_reason, sizeof r->done_reason, "%.23s", jstr(jget(j, "done_reason"), ""));
        r->truncated = !strcmp(r->done_reason, "length");
        if (cl > c->max_content) {
            r->too_large = 1;
            snprintf(r->error, sizeof r->error, "the model's reply is %zu bytes; the limit is %zu, so it was discarded", cl, c->max_content);
            json_free(j);
            return 0;
        }
        r->content = strdup(content);
    }
    json_free(j);
    r->ok = r->content != NULL;
    return r->ok;
}
