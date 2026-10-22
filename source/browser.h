// browser.h - folder listing, sorting, finished marks and series helpers for the library
#ifndef BROWSER_H
#define BROWSER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SORT_NAME, SORT_RECENT, SORT_STATUS, SORT_COUNT };

typedef struct {
    char name[256];
    bool is_dir, is_cont;
    int state;                 // files: 0 new, 1 in progress, 2 finished
    float frac;                // files: listening progress 0..1
    uint64_t ts;               // files: last played (unix time, 0 = never)
    bool stats;                // folders: the numbers below are valid
    int books, done;           // folders: audiobooks inside (recursively) and how many are finished
    float sfrac;               // folders: average progress 0..1
} BrEntry;

typedef struct {
    char path[512];
    char root[512];            // if set, browsing never goes above this folder
    BrEntry *items;
    int count, sel, scroll;
    int sort;                  // SORT_*
} Browser;

void browser_open(Browser *b, const char *path);
void browser_enter(Browser *b, const char *dirname);
void browser_up(Browser *b);
void browser_path(const Browser *b, const char *name, char *out, size_t n);
void browser_free(Browser *b);
void browser_set_root(Browser *b, const char *root);
void browser_insert_front(Browser *b, const char *name);      // adds the "continue listening" row
void browser_set_sort(Browser *b, int mode);                  // re-sorts in place, keeps the selection
void browser_dir_stats(Browser *b, int idx);                  // fills folder statistics (cached)
void browser_mark(Browser *b, int idx, bool finished);        // mark a book (or every book in a folder) finished / unfinished
bool browser_next_book(const char *cur_path, char *out, size_t n);   // next unfinished book in the same folder

#endif
