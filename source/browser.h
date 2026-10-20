// browser.h - folder listing for the library screen
#ifndef BROWSER_H
#define BROWSER_H
#include <stdbool.h>
#include <stddef.h>

typedef struct { char name[256]; bool is_dir; bool is_cont; } BrEntry;

typedef struct {
    char path[512];
    char root[512];            // if set, browsing never goes above this folder
    BrEntry *items;
    int count, sel, scroll;
} Browser;

void browser_open(Browser *b, const char *path);
void browser_enter(Browser *b, const char *dirname);
void browser_up(Browser *b);
void browser_path(const Browser *b, const char *name, char *out, size_t n);
void browser_free(Browser *b);
void browser_set_root(Browser *b, const char *root);
void browser_insert_front(Browser *b, const char *name);   // adds the "continue listening" row

#endif
