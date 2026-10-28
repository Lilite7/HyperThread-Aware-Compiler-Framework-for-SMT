/* topology.c - SMT sibling pairs and cache sizes of the target machine.
 *
 * Sources, in order of precedence:
 *   1. --topology "0,4;1,5;2,6;3,7"   (cores separated by ';', siblings by ',')
 *   2. Linux sysfs  /sys/devices/system/cpu/cpuN/topology/thread_siblings_list
 *   3. an assumed 4-core / 8-CPU layout with adjacent siblings
 * If the host has no SMT, consecutive logical CPUs are grouped into *virtual*
 * cores so that the pairing analysis can still be demonstrated.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ht.h"
#include "common.h"

static int read_file(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    size_t len;
    if (!f) return 0;
    if (!fgets(buf, (int)n, f)) { fclose(f); return 0; }
    fclose(f);
    len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = 0;
    return 1;
}

/* Parse "0,4" / "0-1" / "0-1,4-5" into ids; returns count. */
static int parse_list(const char *s, int *out, int max)
{
    int n = 0;
    const char *p = s;
    while (*p) {
        char *end;
        long a = strtol(p, &end, 10), b = a;
        if (end == p) break;
        if (*end == '-') { p = end + 1; b = strtol(p, &end, 10); }
        for (; a <= b && n < max; a++) out[n++] = (int)a;
        p = (*end == ',') ? end + 1 : end;
        if (*end != ',') break;
    }
    return n;
}

static long parse_size(const char *s)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*end == 'K' || *end == 'k') v *= 1024;
    else if (*end == 'M' || *end == 'm') v *= 1024 * 1024;
    return v;
}

static void detect_caches(Topology *t)
{
    int i;
    char path[128], buf[64], type[32];
    long l1 = 0, l2 = 0;
    for (i = 0; i < 8; i++) {
        long level, size;
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu0/cache/index%d/level", i);
        if (!read_file(path, buf, sizeof buf)) break;
        level = atol(buf);
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu0/cache/index%d/type", i);
        if (!read_file(path, type, sizeof type)) continue;
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu0/cache/index%d/size", i);
        if (!read_file(path, buf, sizeof buf)) continue;
        size = parse_size(buf);
        if (level == 1 && type[0] == 'D') l1 = size;
        if (level == 2) l2 = size;
    }
    if (l1 > 0) { t->l1d = l1; strcpy(t->cache_source, "sysfs"); }
    if (l2 > 0) t->l2 = l2;
}

static void from_spec(Topology *t, const char *spec)
{
    char *copy = ht_strdup(spec), *save = NULL, *core;
    for (core = strtok_r(copy, ";", &save); core && t->ncores < MAX_CORES;
         core = strtok_r(NULL, ";", &save)) {
        int ids[2], n = parse_list(core, ids, 2);
        if (n < 1) continue;
        t->nsib[t->ncores] = n;
        t->cpus[t->ncores][0] = ids[0];
        t->cpus[t->ncores][1] = (n > 1) ? ids[1] : ids[0];
        t->ncpus += n;
        if (n > 1) t->smt_detected = 1;
        t->ncores++;
    }
    free(copy);
    strcpy(t->source, "user");
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static void from_sysfs(Topology *t)
{
    int cpu, all[1024], nall = 0;
    char path[160], buf[128];

    for (cpu = 0; cpu < 1024; cpu++) {
        int ids[16], n, min, i;
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d", cpu);
        if (access(path, F_OK) != 0) break;
        snprintf(path, sizeof path,
                 "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
        if (!read_file(path, buf, sizeof buf)) continue;        /* offline CPU */
        n = parse_list(buf, ids, 16);
        if (n < 1) continue;
        all[nall++] = cpu;
        for (min = ids[0], i = 1; i < n; i++) if (ids[i] < min) min = ids[i];
        if (min != cpu) continue;                               /* not the core leader */
        if (t->ncores >= MAX_CORES) break;
        qsort(ids, n, sizeof(int), cmp_int);
        t->nsib[t->ncores] = n > 2 ? 2 : n;
        t->cpus[t->ncores][0] = ids[0];
        t->cpus[t->ncores][1] = (n > 1) ? ids[1] : ids[0];
        if (n > 1) t->smt_detected = 1;
        t->ncores++;
    }
    t->ncpus = nall;
    if (t->ncores == 0) return;
    strcpy(t->source, "sysfs");

    if (!t->smt_detected) {
        /* no SMT on this host: group adjacent logical CPUs into virtual cores */
        int k;
        t->ncores = 0;
        for (k = 0; k < nall; k += 2) {
            t->cpus[t->ncores][0] = all[k];
            t->cpus[t->ncores][1] = (k + 1 < nall) ? all[k + 1] : all[k];
            t->nsib[t->ncores] = (k + 1 < nall) ? 2 : 1;
            t->ncores++;
        }
        strcpy(t->source, "sysfs (no SMT: virtual cores)");
        ht_warn("host has no SMT/Hyper-Threading; consecutive CPUs are paired as "
                "virtual cores (use --topology to describe the target machine)");
    }
}

void topo_detect(Topology *t, const char *spec, long l1_kb, long l2_kb)
{
    memset(t, 0, sizeof *t);
    t->l1d = 32 * 1024;
    t->l2 = 1024 * 1024;
    strcpy(t->cache_source, "default");

    if (spec && *spec) from_spec(t, spec);
    if (t->ncores == 0) from_sysfs(t);
    if (t->ncores == 0) {
        int c;
        for (c = 0; c < 4; c++) {
            t->nsib[c] = 2;
            t->cpus[c][0] = 2 * c;
            t->cpus[c][1] = 2 * c + 1;
        }
        t->ncores = 4;
        t->ncpus = 8;
        t->smt_detected = 1;
        strcpy(t->source, "assumed");
        ht_warn("could not read CPU topology; assuming 4 cores x 2 SMT threads "
                "(use --topology to override)");
    }
    detect_caches(t);
    if (l1_kb > 0) { t->l1d = l1_kb * 1024; strcpy(t->cache_source, "user"); }
    if (l2_kb > 0) t->l2 = l2_kb * 1024;
}
