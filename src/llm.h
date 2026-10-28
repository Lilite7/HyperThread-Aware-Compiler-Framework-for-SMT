/* llm.h - client for a LOCAL Ollama server (http://127.0.0.1:11434 by default).
 *
 * No API key, no paid service, no remote host: the client refuses to talk to anything
 * but a loopback address.  Everything the model says is treated as untrusted data.
 */
#ifndef HT_LLM_H
#define HT_LLM_H

#include <stddef.h>

typedef struct {
    char   host[64];            /* "127.0.0.1" (loopback only)                         */
    int    port;
    char   model[96];           /* "" = choose among the installed models              */
    double timeout;             /* seconds for one chat request                        */
    size_t max_body;            /* largest HTTP response body accepted (bytes)         */
    size_t max_content;         /* largest model reply accepted (bytes)                */
    int    autostart;           /* start `ollama serve` if nothing is listening        */
    int    num_ctx, num_predict;
    const char *reply_file;     /* TEST HOOK: read the "reply" from a file, no network */
} LlmConfig;

typedef struct {
    int  reachable;
    char version[32];
    int  nmodels;
    char models[24][96];
    char error[300];
} LlmStatus;

typedef struct {
    int    ok;
    char  *content;             /* malloc'd, NUL-terminated model text                 */
    double seconds;
    int    prompt_tokens, eval_tokens;
    int    http_status;
    int    timed_out, too_large, truncated;
    char   done_reason[24];
    char   error[400];
} LlmReply;

void llm_defaults(LlmConfig *c);                                   /* env: OLLAMA_HOST, HTC_LLM_MODEL */
int  llm_set_endpoint(LlmConfig *c, const char *spec, char *err, size_t en);   /* loopback only */

int  llm_status(const LlmConfig *c, LlmStatus *st);                /* 1 if the service answers */
int  llm_ollama_installed(void);
int  llm_start_service(const LlmConfig *c, char *msg, size_t n);   /* spawn `ollama serve`     */
int  llm_ensure(const LlmConfig *c, LlmStatus *st, char *note, size_t nn);   /* status, autostart once */
int  llm_pick_model(const LlmConfig *c, const LlmStatus *st, char *out, size_t n, char *err, size_t en);

int  llm_chat(const LlmConfig *c, const char *model, const char *system, const char *user, LlmReply *r);
void llm_reply_free(LlmReply *r);

/* shared by the CLI and tests */
int  llm_model_installed(const LlmStatus *st, const char *want);

#endif
