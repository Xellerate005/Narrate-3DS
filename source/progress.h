// progress.h - remembers the listening position of every book (and when it was last played)
#ifndef PROGRESS_H
#define PROGRESS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROGRESS_UNKNOWN_DUR 360000000ULL     // placeholder duration (100 h) for books marked before ever being opened

void progress_load(void);
bool progress_get(const char *path, uint64_t *pos_ms, uint64_t *dur_ms);
bool progress_get_ts(const char *path, uint64_t *ts);
void progress_set(const char *path, uint64_t pos_ms, uint64_t dur_ms);         // also stamps "last played"
void progress_set_quiet(const char *path, uint64_t pos_ms, uint64_t dur_ms);   // leaves the timestamp alone
int  progress_state(const char *path, float *frac);                            // 0 = new, 1 = in progress, 2 = finished
void progress_save(void);
void progress_set_last(const char *path);       // remember the most recently opened book
bool progress_get_last(char *out, size_t n);

#endif
