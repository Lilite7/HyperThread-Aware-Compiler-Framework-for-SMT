/* runner.c - run an external program (the C compiler, or a program under test).
 *
 * No shell is involved: arguments are passed to execvp as an array, so file names
 * and options can never be interpreted as commands.  Each run gets its own process
 * group so a timeout kills everything it started, plus an address-space limit.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "opt.h"

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

RunInfo run_proc(char *const argv[], const char *out_path, const char *err_path,
                 double timeout, long mem_mb, const char *const *env)
{
    RunInfo r;
    pid_t pid;
    double t0, limit = timeout > 0 ? timeout : 60.0;
    int status = 0;

    memset(&r, 0, sizeof r);
    r.exit_code = -1;
    fflush(NULL);
    t0 = now_s();
    pid = fork();
    if (pid < 0) return r;
    if (pid == 0) {
        int fd;
        setpgid(0, 0);
        fd = open(out_path ? out_path : "/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) { dup2(fd, 1); close(fd); }
        fd = open(err_path ? err_path : "/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) { dup2(fd, 2); close(fd); }
        fd = open("/dev/null", O_RDONLY);
        if (fd >= 0) { dup2(fd, 0); close(fd); }
        if (mem_mb > 0) {
            struct rlimit rl;
            rl.rlim_cur = rl.rlim_max = (rlim_t)mem_mb * 1024 * 1024;
            setrlimit(RLIMIT_AS, &rl);
        }
        {
            struct rlimit rl;                        /* CPU-time backstop */
            rl.rlim_cur = rl.rlim_max = (rlim_t)(limit * 8 + 10);
            setrlimit(RLIMIT_CPU, &rl);
        }
        if (env)
            for (; *env; env++) putenv((char *)*env);
        execvp(argv[0], argv);
        _exit(127);
    }
    setpgid(pid, pid);
    r.started = 1;
    for (;;) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0 && errno != EINTR) break;
        if (now_s() - t0 > limit) {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            r.timed_out = 1;
            break;
        }
        {
            struct timespec ts = { 0, 2000000 };     /* 2 ms */
            nanosleep(&ts, NULL);
        }
    }
    r.secs = now_s() - t0;
    if (!r.timed_out) {
        if (WIFEXITED(status)) r.exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) r.signaled = WTERMSIG(status);
    }
    return r;
}

int make_tempdir(char *path, size_t n)
{
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    snprintf(path, n, "%s/htopt_XXXXXX", base);
    return mkdtemp(path) != NULL;
}

void remove_tree(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;
    char p[1024];
    if (!d) { unlink(path); return; }
    while ((e = readdir(d)) != NULL) {
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(p, sizeof p, "%s/%s", path, e->d_name);
        if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) remove_tree(p);
        else unlink(p);
    }
    closedir(d);
    rmdir(path);
}
