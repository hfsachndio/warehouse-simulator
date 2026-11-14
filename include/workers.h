#ifndef WORKERS_H
#define WORKERS_H

#include "inventory.h"
#include "config.h"

typedef struct {
    inventory_t *inv;
    int          id;
    unsigned     seed;
} worker_arg_t;

void *producer_thread(void *arg);
void *consumer_thread(void *arg);
void *reader_thread(void *arg);
void *writer_thread(void *arg);
void *monitor_thread(void *arg);

int run_automated(inventory_t *inv, const config_t *cfg);
int run_interactive(inventory_t *inv, const config_t *cfg);

#endif
