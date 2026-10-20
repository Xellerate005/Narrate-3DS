// browser.c
#include "browser.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MAX_ENTRIES 2048

static bool is_audio_file(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return !strcasecmp(dot, ".m4b") || !strcasecmp(dot, ".m4a") || !strcasecmp(dot, ".mp4");
}

static int cmp(const void *a, const void *b) {
    const BrEntry *x = (const BrEntry *)a, *y = (const BrEntry *)b;
    bool xu = strcmp(x->name, "..") == 0, yu = strcmp(y->name, "..") == 0;
    if (xu != yu) return xu ? -1 : 1;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

void browser_free(Browser *b) {
    free(b->items);
    b->items = NULL;
    b->count = 0;
}

void browser_path(const Browser *b, const char *name, char *out, size_t n) {
    size_t l = strlen(b->path);
    if (l && b->path[l - 1] == '/') snprintf(out, n, "%s%s", b->path, name);
    else snprintf(out, n, "%s/%s", b->path, name);
}

void browser_open(Browser *b, const char *path) {
    browser_free(b);
    snprintf(b->path, sizeof b->path, "%s", path);
    b->sel = b->scroll = 0;
    b->items = (BrEntry *)calloc(MAX_ENTRIES + 2, sizeof(BrEntry));
    if (!b->items) return;

    bool at_root = b->root[0] ? strcmp(path, b->root) == 0 : strcmp(path, "sdmc:/") == 0;
    if (!at_root) {
        snprintf(b->items[0].name, sizeof b->items[0].name, "..");
        b->items[0].is_dir = true;
        b->count = 1;
    }
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && b->count < MAX_ENTRIES) {
        if (e->d_name[0] == '.') continue;
        bool dir;
        if (e->d_type == DT_DIR) dir = true;
        else if (e->d_type == DT_REG) dir = false;
        else {
            char full[800];
            struct stat st;
            browser_path(b, e->d_name, full, sizeof full);
            if (stat(full, &st) != 0) continue;
            dir = S_ISDIR(st.st_mode);
        }
        if (!dir && !is_audio_file(e->d_name)) continue;
        BrEntry *it = &b->items[b->count++];
        snprintf(it->name, sizeof it->name, "%s", e->d_name);
        it->is_dir = dir;
    }
    closedir(d);
    qsort(b->items, b->count, sizeof(BrEntry), cmp);
}

void browser_enter(Browser *b, const char *dirname) {
    char p[512];
    browser_path(b, dirname, p, sizeof p);
    browser_open(b, p);
}

void browser_set_root(Browser *b, const char *root) {
    snprintf(b->root, sizeof b->root, "%s", root);
    size_t l = strlen(b->root);
    while (l > 6 && b->root[l - 1] == '/') b->root[--l] = 0;     // no trailing slash (keeps "sdmc:/" intact)
}

void browser_up(Browser *b) {
    if (b->root[0] && strcmp(b->path, b->root) == 0) return;       // already at the top
    char p[512];
    snprintf(p, sizeof p, "%s", b->path);
    size_t l = strlen(p);
    while (l > 1 && p[l - 1] == '/') p[--l] = 0;
    char *slash = strrchr(p, '/');
    if (!slash || slash == p || *(slash - 1) == ':') {
        browser_open(b, b->root[0] ? b->root : "sdmc:/");
        return;
    }
    *slash = 0;
    if (b->root[0] && strlen(p) < strlen(b->root)) browser_open(b, b->root);   // never above the root
    else browser_open(b, p);
}

void browser_insert_front(Browser *b, const char *name) {
    if (!b->items) return;
    memmove(b->items + 1, b->items, (size_t)b->count * sizeof(BrEntry));
    memset(&b->items[0], 0, sizeof(BrEntry));
    snprintf(b->items[0].name, sizeof b->items[0].name, "%s", name);
    b->items[0].is_cont = true;
    b->count++;
    b->sel = 0;
    b->scroll = 0;
}
