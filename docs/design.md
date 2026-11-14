# Design notes

## Shared state

| Data | Protected by | Why |
|---|---|---|
| Units on a shelf (per SKU) | counting semaphores `full` and `empty` | classic bounded buffer: consumers block when a SKU is empty, producers block when it is full |
| The `stock` counter (per SKU) | `pthread_mutex_t` | keeps the counter consistent with the semaphores |
| Prices and inventory records | `pthread_rwlock_t` | many report readers can run together; a price update needs exclusive access |
| Global statistics | `pthread_spinlock_t` | the critical section is a few additions, so spinning is cheaper than sleeping |
| Order ids, run flag | C11 atomics | lock-free increments and a stop signal every thread can read |

## Producer–Consumer (restocking and orders)

```
producer:  sem_wait(empty) -> lock -> stock++ -> unlock -> sem_post(full)
consumer:  sem_wait(full)  -> lock -> stock-- -> unlock -> sem_post(empty)
```

Units move one at a time, so a large order never holds a lock while waiting.
All waits use `sem_timedwait` in a loop that also checks the run flag, so every
thread can be joined cleanly when the simulation stops.

## Reader–Writer (reports and price updates)

Report threads take `rwlock_rdlock` and can overlap with each other.
Price-update threads take `rwlock_wrlock`, which waits for active readers and
blocks new ones until the update is done.

## Correctness checks

At the end of every run the simulator verifies:

1. `final stock == initial stock + units restocked - units shipped`
2. for every SKU, the value of `full` equals the stock counter
3. for every SKU, `full + empty == capacity`
4. stock never goes below 0 or above capacity

`make test` runs 20 heavy configurations (up to 16 producers and 16 consumers)
and `make tsan` builds with ThreadSanitizer to catch data races.
