#include "logger.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static struct timespec start_ts;
static int log_quiet = 0;

void log_init(int quiet) {
    log_quiet = quiet;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);
}

double log_elapsed(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start_ts.tv_sec) + (now.tv_nsec - start_ts.tv_nsec) / 1e9;
}

void log_event(const char *actor, const char *fmt, ...) {
    if (log_quiet) return;
    double t = log_elapsed();
    va_list ap;
    va_start(ap, fmt);
    /* One lock around the whole line so output from many threads never interleaves. */
    pthread_mutex_lock(&log_lock);
    printf("[%02d:%05.2f] %-11s ", (int)(t / 60), t - 60 * (int)(t / 60), actor);
    vprintf(fmt, ap);
    putchar('\n');
    fflush(stdout);
    pthread_mutex_unlock(&log_lock);
    va_end(ap);
}
