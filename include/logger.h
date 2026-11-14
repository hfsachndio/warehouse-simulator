#ifndef LOGGER_H
#define LOGGER_H

void log_init(int quiet);
void log_event(const char *actor, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
double log_elapsed(void);

#endif
