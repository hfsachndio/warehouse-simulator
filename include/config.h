#ifndef CONFIG_H
#define CONFIG_H

typedef enum { MODE_AUTO, MODE_INTERACTIVE } run_mode_t;

typedef struct {
    run_mode_t mode;
    int producers;     /* restocking threads (Producer-Consumer) */
    int consumers;     /* order-processing threads               */
    int readers;       /* inventory report threads (Reader-Writer) */
    int writers;       /* price-update threads                    */
    int duration_sec;  /* automated mode run time                 */
    unsigned seed;
    int quiet;         /* only print summary                      */
} config_t;

int  parse_args(int argc, char **argv, config_t *cfg);
void print_usage(const char *prog);

#endif
