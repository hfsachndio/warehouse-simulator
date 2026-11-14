#include "inventory.h"
#include "logger.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const struct { const char *id, *name; int stock; double price; } SEED[] = {
    {"SKU-104", "Packaging tape",   60, 2.50},
    {"SKU-219", "Cardboard box L",  55, 1.20},
    {"SKU-330", "Bubble wrap roll", 30, 6.75},
    {"SKU-412", "Pallet wrap",      45, 9.90},
};

int inventory_init(inventory_t *inv) {
    memset(inv, 0, sizeof(*inv));
    inv->count = (int)(sizeof(SEED) / sizeof(SEED[0]));
    for (int i = 0; i < inv->count; i++) {
        sku_t *s = &inv->skus[i];
        snprintf(s->id, sizeof(s->id), "%s", SEED[i].id);
        snprintf(s->name, sizeof(s->name), "%s", SEED[i].name);
        s->stock = SEED[i].stock;
        s->price = SEED[i].price;
        if (sem_init(&s->full, 0, (unsigned)s->stock) != 0) return -1;
        if (sem_init(&s->empty, 0, (unsigned)(SKU_CAPACITY - s->stock)) != 0) return -1;
        pthread_mutex_init(&s->lock, NULL);
        inv->initial_stock += s->stock;
    }
    pthread_rwlock_init(&inv->records, NULL);
    pthread_spin_init(&inv->stats.lock, PTHREAD_PROCESS_PRIVATE);
    atomic_store(&inv->next_order_id, 1001);
    atomic_store(&inv->running, 1);
    return 0;
}

void inventory_destroy(inventory_t *inv) {
    for (int i = 0; i < inv->count; i++) {
        sem_destroy(&inv->skus[i].full);
        sem_destroy(&inv->skus[i].empty);
        pthread_mutex_destroy(&inv->skus[i].lock);
    }
    pthread_rwlock_destroy(&inv->records);
    pthread_spin_destroy(&inv->stats.lock);
}

int inventory_find(inventory_t *inv, const char *id) {
    for (int i = 0; i < inv->count; i++)
        if (strcasecmp(inv->skus[i].id, id) == 0) return i;
    return -1;
}

/* sem_wait that gives up when the simulation is stopping, so threads can always be joined. */
static int wait_or_stop(inventory_t *inv, sem_t *sem) {
    for (;;) {
        if (sem_trywait(sem) == 0) return 0;
        if (!atomic_load(&inv->running)) return -1;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100 * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        if (sem_timedwait(sem, &ts) == 0) return 0;
        if (errno != ETIMEDOUT && errno != EINTR) return -1;
    }
}

static void stat_add(inventory_t *inv, long *field, long v) {
    pthread_spin_lock(&inv->stats.lock);
    *field += v;
    pthread_spin_unlock(&inv->stats.lock);
}

int inventory_restock(inventory_t *inv, int idx, int qty, const char *who) {
    sku_t *s = &inv->skus[idx];
    int moved = 0;
    int value;
    sem_getvalue(&s->empty, &value);
    if (value < qty) {
        stat_add(inv, &inv->stats.waits_on_full, 1);
        log_event(who, "restock %s x%d   shelf nearly full, waiting for space", s->id, qty);
    }
    for (int i = 0; i < qty; i++) {
        if (wait_or_stop(inv, &s->empty) != 0) break;      /* take a free slot   */
        pthread_mutex_lock(&s->lock);
        s->stock++;
        pthread_mutex_unlock(&s->lock);
        sem_post(&s->full);                                  /* publish one unit   */
        moved++;
    }
    if (moved > 0) {
        pthread_spin_lock(&inv->stats.lock);
        inv->stats.units_restocked += moved;
        inv->stats.restocks++;
        pthread_spin_unlock(&inv->stats.lock);
        pthread_mutex_lock(&s->lock);
        int now = s->stock;
        pthread_mutex_unlock(&s->lock);
        log_event(who, "restocked %s x%-3d sem_post(full)   stock: %3d/%d", s->id, moved, now, SKU_CAPACITY);
    }
    return moved;
}

int inventory_order(inventory_t *inv, int idx, int qty, const char *who, int *order_id) {
    sku_t *s = &inv->skus[idx];
    int id = atomic_fetch_add(&inv->next_order_id, 1);
    if (order_id) *order_id = id;
    int value;
    sem_getvalue(&s->full, &value);
    if (value < qty) {
        stat_add(inv, &inv->stats.waits_on_empty, 1);
        log_event(who, "order #%d %s x%d   waiting... buffer empty, blocked", id, s->id, qty);
    }
    int moved = 0;
    for (int i = 0; i < qty; i++) {
        if (wait_or_stop(inv, &s->full) != 0) break;       /* take one unit      */
        pthread_mutex_lock(&s->lock);
        s->stock--;
        pthread_mutex_unlock(&s->lock);
        sem_post(&s->empty);                                 /* free one slot      */
        moved++;
    }
    if (moved > 0) {
        pthread_spin_lock(&inv->stats.lock);
        inv->stats.units_shipped += moved;
        if (moved == qty) inv->stats.orders_processed++;
        pthread_spin_unlock(&inv->stats.lock);
        pthread_mutex_lock(&s->lock);
        int now = s->stock;
        pthread_mutex_unlock(&s->lock);
        log_event(who, "order #%d %s x%-3d sem_wait(full)  stock: %3d/%d%s", id, s->id, moved, now,
                  SKU_CAPACITY, moved < qty ? "  (partial, shutting down)" : "");
    }
    return moved;
}

void inventory_report(inventory_t *inv, const char *who, int print_table) {
    pthread_rwlock_rdlock(&inv->records);          /* shared: other readers may run too */
    double value = 0;
    int units = 0;
    for (int i = 0; i < inv->count; i++) {
        sku_t *s = &inv->skus[i];
        pthread_mutex_lock(&s->lock);
        units += s->stock;
        value += s->stock * s->price;
        pthread_mutex_unlock(&s->lock);
    }
    if (print_table) inventory_print_bars(inv);
    pthread_rwlock_unlock(&inv->records);
    stat_add(inv, &inv->stats.reports, 1);
    log_event(who, "inventory report  rwlock_rdlock OK   %d units, value $%.2f", units, value);
}

int inventory_set_price(inventory_t *inv, int idx, double price, const char *who) {
    pthread_rwlock_wrlock(&inv->records);          /* exclusive: blocks readers and writers */
    double old = inv->skus[idx].price;
    inv->skus[idx].price = price;
    pthread_rwlock_unlock(&inv->records);
    stat_add(inv, &inv->stats.price_updates, 1);
    log_event(who, "price update %s $%.2f -> $%.2f  rwlock_wrlock exclusive", inv->skus[idx].id, old, price);
    return 0;
}

int inventory_total_stock(inventory_t *inv) {
    int total = 0;
    for (int i = 0; i < inv->count; i++) {
        pthread_mutex_lock(&inv->skus[i].lock);
        total += inv->skus[i].stock;
        pthread_mutex_unlock(&inv->skus[i].lock);
    }
    return total;
}

void inventory_print_bars(inventory_t *inv) {
    printf("\n  LIVE INVENTORY\n");
    for (int i = 0; i < inv->count; i++) {
        sku_t *s = &inv->skus[i];
        pthread_mutex_lock(&s->lock);
        int st = s->stock;
        double pr = s->price;
        pthread_mutex_unlock(&s->lock);
        int filled = st * 20 / SKU_CAPACITY;
        printf("  %-8s %-17s ", s->id, s->name);
        for (int b = 0; b < 20; b++) fputs(b < filled ? "#" : ".", stdout);
        printf(" %3d/%d  $%.2f\n", st, SKU_CAPACITY, pr);
    }
    putchar('\n');
    fflush(stdout);
}

/*
 * Invariants checked at the end of a run:
 *   final stock == initial stock + restocked - shipped
 *   for each SKU: semaphore 'full' == stock and 'full' + 'empty' == capacity
 */
int inventory_verify(inventory_t *inv) {
    int errors = 0;
    int total = inventory_total_stock(inv);
    long expected = inv->initial_stock + inv->stats.units_restocked - inv->stats.units_shipped;
    if (total != expected) errors++;
    for (int i = 0; i < inv->count; i++) {
        int full, empty;
        sem_getvalue(&inv->skus[i].full, &full);
        sem_getvalue(&inv->skus[i].empty, &empty);
        if (full != inv->skus[i].stock) errors++;
        if (full + empty != SKU_CAPACITY) errors++;
        if (inv->skus[i].stock < 0 || inv->skus[i].stock > SKU_CAPACITY) errors++;
    }
    return errors;
}
