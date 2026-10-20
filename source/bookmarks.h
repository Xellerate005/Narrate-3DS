// bookmarks.h - per-book bookmarks stored on the SD card
#ifndef BOOKMARKS_H
#define BOOKMARKS_H
#include <stdint.h>

#define BM_MAX_PER_BOOK 200

typedef struct { uint64_t pos; char label[96]; } Bookmark;

void bm_load(void);
int  bm_get(const char *path, Bookmark *out, int max);        // sorted by position; returns count
int  bm_add(const char *path, uint64_t pos, const char *label); // 1 = added, 0 = already there, -1 = full/error
void bm_remove(const char *path, uint64_t pos);

#endif
