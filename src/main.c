/*
 * Warehouse Supply Chain Simulator
 * Multithreaded concurrency simulation in C (POSIX threads, Linux).
 *
 * Producer-Consumer : producers restock SKUs, consumers fulfil orders.
 *                     Each SKU is a bounded buffer guarded by two counting
 *                     semaphores (full / empty) and a mutex.
 * Reader-Writer     : report threads read inventory records concurrently,
 *                     price-update threads take an exclusive write lock.
 * Spin lock         : protects tiny critical sections (shared counters).
 */
#include "config.h"
#include "inventory.h"
#include "logger.h"
#include "workers.h"

#include <stdio.h>

int main(int argc, char **argv) {
    config_t cfg;
    if (parse_args(argc, argv, &cfg) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    inventory_t inv;
    if (inventory_init(&inv) != 0) {
        fprintf(stderr, "failed to initialise inventory\n");
        return 1;
    }
    log_init(cfg.quiet);
    int rc = cfg.mode == MODE_AUTO ? run_automated(&inv, &cfg) : run_interactive(&inv, &cfg);
    inventory_destroy(&inv);
    return rc;
}
