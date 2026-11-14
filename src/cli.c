#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void print_usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [--mode=auto|interactive] [--producers=N] [--consumers=N]\n"
        "          [--readers=N] [--writers=N] [--duration=SEC] [--seed=N] [--quiet]\n", prog);
}

static int int_arg(const char *a, const char *key, int *out, int lo, int hi) {
    size_t n = strlen(key);
    if (strncmp(a, key, n) != 0) return 0;
    int v = atoi(a + n);
    if (v < lo || v > hi) { fprintf(stderr, "%s must be between %d and %d\n", key, lo, hi); return -1; }
    *out = v;
    return 1;
}

int parse_args(int argc, char **argv, config_t *cfg) {
    *cfg = (config_t){ MODE_AUTO, 3, 4, 2, 1, 5, (unsigned)time(NULL), 0 };
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int r = 0, seed = 0;
        if (!strcmp(a, "--mode=auto")) { cfg->mode = MODE_AUTO; continue; }
        if (!strcmp(a, "--mode=interactive")) { cfg->mode = MODE_INTERACTIVE; continue; }
        if (!strcmp(a, "--quiet")) { cfg->quiet = 1; continue; }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) return -1;
        if ((r = int_arg(a, "--producers=", &cfg->producers, 1, 64)) ||
            (r = int_arg(a, "--consumers=", &cfg->consumers, 1, 64)) ||
            (r = int_arg(a, "--readers=", &cfg->readers, 0, 64)) ||
            (r = int_arg(a, "--writers=", &cfg->writers, 0, 64)) ||
            (r = int_arg(a, "--duration=", &cfg->duration_sec, 1, 3600))) {
            if (r < 0) return -1;
            continue;
        }
        if ((r = int_arg(a, "--seed=", &seed, 0, 2147483647))) {
            if (r < 0) return -1;
            cfg->seed = (unsigned)seed;
            continue;
        }
        fprintf(stderr, "unknown option: %s\n", a);
        return -1;
    }
    return 0;
}
