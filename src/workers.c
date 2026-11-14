#include "workers.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int rand_between(unsigned *seed, int lo, int hi) {
    return lo + (int)(rand_r(seed) % (unsigned)(hi - lo + 1));
}

static void nap_ms(unsigned *seed, int lo, int hi) {
    usleep((useconds_t)rand_between(seed, lo, hi) * 1000);
}

void *producer_thread(void *arg) {
    worker_arg_t *w = arg;
    char name[16];
    snprintf(name, sizeof(name), "PRODUCER-%d", w->id);
    while (atomic_load(&w->inv->running)) {
        int idx = rand_between(&w->seed, 0, w->inv->count - 1);
        int qty = rand_between(&w->seed, 10, 25);
        inventory_restock(w->inv, idx, qty, name);
        nap_ms(&w->seed, 150, 450);
    }
    return NULL;
}

void *consumer_thread(void *arg) {
    worker_arg_t *w = arg;
    char name[16];
    snprintf(name, sizeof(name), "CONSUMER-%d", w->id);
    while (atomic_load(&w->inv->running)) {
        int idx = rand_between(&w->seed, 0, w->inv->count - 1);
        int qty = rand_between(&w->seed, 2, 12);
        inventory_order(w->inv, idx, qty, name, NULL);
        nap_ms(&w->seed, 80, 300);
    }
    return NULL;
}

void *reader_thread(void *arg) {
    worker_arg_t *w = arg;
    char name[16];
    snprintf(name, sizeof(name), "READER-%d", w->id);
    while (atomic_load(&w->inv->running)) {
        inventory_report(w->inv, name, 0);
        nap_ms(&w->seed, 400, 900);
    }
    return NULL;
}

void *writer_thread(void *arg) {
    worker_arg_t *w = arg;
    char name[16];
    snprintf(name, sizeof(name), "WRITER-%d", w->id);
    while (atomic_load(&w->inv->running)) {
        int idx = rand_between(&w->seed, 0, w->inv->count - 1);
        pthread_rwlock_rdlock(&w->inv->records);
        double p = w->inv->skus[idx].price;
        pthread_rwlock_unlock(&w->inv->records);
        double change = (rand_between(&w->seed, -5, 5)) / 100.0;   /* +/- 5% */
        inventory_set_price(w->inv, idx, p * (1.0 + change), name);
        nap_ms(&w->seed, 900, 1600);
    }
    return NULL;
}

/* Prints a dashboard every second while the automated run is active. */
void *monitor_thread(void *arg) {
    worker_arg_t *w = arg;
    while (atomic_load(&w->inv->running)) {
        for (int i = 0; i < 10 && atomic_load(&w->inv->running); i++) usleep(100 * 1000);
        if (!atomic_load(&w->inv->running)) break;
        pthread_rwlock_rdlock(&w->inv->records);
        inventory_print_bars(w->inv);
        pthread_rwlock_unlock(&w->inv->records);
    }
    return NULL;
}

static void print_summary(inventory_t *inv, double secs, int threads) {
    stats_t *st = &inv->stats;
    int errs = inventory_verify(inv);
    printf("\n=== Simulation summary ===\n");
    printf("  run time           %.1f s with %d threads\n", secs, threads);
    printf("  orders processed   %ld\n", st->orders_processed);
    printf("  units shipped      %ld\n", st->units_shipped);
    printf("  restocks           %ld (%ld units)\n", st->restocks, st->units_restocked);
    printf("  inventory reports  %ld\n", st->reports);
    printf("  price updates      %ld\n", st->price_updates);
    printf("  blocked on empty   %ld   blocked on full %ld\n", st->waits_on_empty, st->waits_on_full);
    printf("  stock check        start %d + in %ld - out %ld = %d\n", inv->initial_stock,
           st->units_restocked, st->units_shipped, inventory_total_stock(inv));
    printf("  race conditions    %d %s\n", errs, errs == 0 ? "(all invariants hold)" : "(INVARIANT VIOLATED)");
}

int run_automated(inventory_t *inv, const config_t *cfg) {
    int total = cfg->producers + cfg->consumers + cfg->readers + cfg->writers + (cfg->quiet ? 0 : 1);
    pthread_t   *tids = calloc((size_t)total, sizeof(pthread_t));
    worker_arg_t *args = calloc((size_t)total, sizeof(worker_arg_t));
    if (!tids || !args) return 1;

    printf("=== Warehouse Supply Chain Simulator ===\n");
    printf("mode: AUTOMATED | producers=%d consumers=%d readers=%d writers=%d | %ds\n\n",
           cfg->producers, cfg->consumers, cfg->readers, cfg->writers, cfg->duration_sec);

    int n = 0;
    struct { int count; void *(*fn)(void *); } groups[] = {
        {cfg->producers, producer_thread}, {cfg->consumers, consumer_thread},
        {cfg->readers, reader_thread},     {cfg->writers, writer_thread},
        {cfg->quiet ? 0 : 1, monitor_thread},
    };
    for (size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++) {
        for (int i = 0; i < groups[g].count; i++, n++) {
            args[n] = (worker_arg_t){ .inv = inv, .id = i + 1, .seed = cfg->seed + (unsigned)n * 7919u };
            if (pthread_create(&tids[n], NULL, groups[g].fn, &args[n]) != 0) {
                perror("pthread_create");
                atomic_store(&inv->running, 0);
                break;
            }
        }
    }

    sleep((unsigned)cfg->duration_sec);
    atomic_store(&inv->running, 0);
    for (int i = 0; i < n; i++) pthread_join(tids[i], NULL);

    print_summary(inv, log_elapsed(), n);
    int errs = inventory_verify(inv);
    free(tids);
    free(args);
    return errs == 0 ? 0 : 2;
}

static void help(void) {
    puts("Commands:");
    puts("  order <SKU> <qty>     place an order (blocks if stock is short)");
    puts("  restock <SKU> <qty>   add stock (blocks if the shelf is full)");
    puts("  price <SKU> <amount>  update a price (writer lock)");
    puts("  report                show inventory (reader lock)");
    puts("  auto <seconds>        run background producers/consumers for a while");
    puts("  help | quit");
}

int run_interactive(inventory_t *inv, const config_t *cfg) {
    char line[128];
    printf("=== Warehouse Supply Chain Simulator ===\nmode: INTERACTIVE\n");
    help();
    inventory_print_bars(inv);
    for (;;) {
        printf("[INTERACTIVE] > ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        char cmd[16] = {0}, sku[16] = {0};
        double num = 0;
        int parts = sscanf(line, "%15s %15s %lf", cmd, sku, &num);
        if (parts <= 0) continue;
        if (!strcmp(cmd, "quit") || !strcmp(cmd, "q")) break;
        if (!strcmp(cmd, "help")) { help(); continue; }
        if (!strcmp(cmd, "report")) { inventory_report(inv, "USER", 1); continue; }
        if (!strcmp(cmd, "auto")) {
            config_t c = *cfg;
            c.duration_sec = parts >= 2 ? atoi(sku) : 3;
            c.quiet = 1;
            atomic_store(&inv->running, 1);
            run_automated(inv, &c);
            atomic_store(&inv->running, 1);
            continue;
        }
        int idx = inventory_find(inv, sku);
        if (parts < 3 || idx < 0) { puts("  unknown SKU or missing amount (try: order SKU-330 4)"); continue; }
        if (!strcmp(cmd, "order")) {
            int id;
            int got = inventory_order(inv, idx, (int)num, "USER", &id);
            printf("  queued as #%d, %d unit(s) shipped\n", id, got);
        } else if (!strcmp(cmd, "restock")) {
            int got = inventory_restock(inv, idx, (int)num, "USER");
            printf("  %d unit(s) added\n", got);
        } else if (!strcmp(cmd, "price")) {
            inventory_set_price(inv, idx, num, "USER");
        } else {
            puts("  unknown command, type 'help'");
        }
    }
    int errs = inventory_verify(inv);
    printf("bye. invariants: %s\n", errs == 0 ? "OK" : "VIOLATED");
    return errs == 0 ? 0 : 2;
}
