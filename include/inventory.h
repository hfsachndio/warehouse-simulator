#ifndef INVENTORY_H
#define INVENTORY_H

#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>

#define MAX_SKUS      8
#define SKU_CAPACITY  100

/*
 * One stock-keeping unit.
 *
 * Producer-Consumer: every SKU is a bounded buffer of SKU_CAPACITY units.
 *   full  - counts units on the shelf   (consumers wait on it)
 *   empty - counts free shelf slots     (producers wait on it)
 *   lock  - mutex protecting the stock counter itself
 */
typedef struct {
    char            id[12];
    char            name[32];
    int             stock;
    double          price;
    sem_t           full;
    sem_t           empty;
    pthread_mutex_t lock;
} sku_t;

/* Totals updated from many threads; guarded by a spin lock (tiny critical sections). */
typedef struct {
    pthread_spinlock_t lock;
    long orders_processed;
    long units_shipped;
    long units_restocked;
    long restocks;
    long reports;
    long price_updates;
    long waits_on_empty;
    long waits_on_full;
} stats_t;

typedef struct {
    sku_t             skus[MAX_SKUS];
    int               count;
    int               initial_stock;
    /* Reader-Writer: many report readers at once, price writers get exclusive access. */
    pthread_rwlock_t  records;
    stats_t           stats;
    atomic_int        next_order_id;
    atomic_int        running;
} inventory_t;

int    inventory_init(inventory_t *inv);
void   inventory_destroy(inventory_t *inv);
int    inventory_find(inventory_t *inv, const char *id);

/* Blocking operations with a stop-aware timeout. Return units moved. */
int    inventory_restock(inventory_t *inv, int idx, int qty, const char *who);
int    inventory_order(inventory_t *inv, int idx, int qty, const char *who, int *order_id);

void   inventory_report(inventory_t *inv, const char *who, int print_table);
int    inventory_set_price(inventory_t *inv, int idx, double price, const char *who);
int    inventory_total_stock(inventory_t *inv);
void   inventory_print_bars(inventory_t *inv);
int    inventory_verify(inventory_t *inv);   /* 0 = consistent, otherwise mismatch count */

#endif
