// progress.h - remembers the listening position of every book
#ifndef PROGRESS_H
#define PROGRESS_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

void progress_load(void);
bool progress_get(const char *path, uint64_t *pos_ms, uint64_t *dur_ms);
void progress_set(const char *path, uint64_t pos_ms, uint64_t dur_ms);
void progress_save(void);
void progress_set_last(const char *path);       // remember the most recently opened book
bool progress_get_last(char *out, size_t n);

#endif
