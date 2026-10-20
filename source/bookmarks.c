// bookmarks.c - text database: "<pos_ms>|<label>|<path>" per line
#include "bookmarks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BM_FILE "sdmc:/3ds/narrate/bookmarks.txt"

typedef struct { char *path; uint64_t pos; char label[96]; } Ent;

static Ent *g_e;
static int g_n, g_cap;

static void clean_label(char *dst, const char *src) {
    size_t n = 0;
    for (; src && *src && n < sizeof(((Ent *)0)->label) - 1; src++) {
        char c = *src;
        if (c == '|' || c == '\n' || c == '\r') c = ' ';
        dst[n++] = c;
    }
    dst[n] = 0;
}

static Ent *push(const char *path, uint64_t pos, const char *label) {
    if (g_n == g_cap) {
        int nc = g_cap ? g_cap * 2 : 32;
        Ent *p = (Ent *)realloc(g_e, (size_t)nc * sizeof *p);
        if (!p) return NULL;
        g_e = p;
        g_cap = nc;
    }
    Ent *e = &g_e[g_n];
    e->path = strdup(path);
    if (!e->path) return NULL;
    e->pos = pos;
    clean_label(e->label, label);
    g_n++;
    return e;
}

static void save(void) {
    FILE *f = fopen(BM_FILE, "w");
    if (!f) return;
    for (int i = 0; i < g_n; i++)
        fprintf(f, "%llu|%s|%s\n", (unsigned long long)g_e[i].pos, g_e[i].label, g_e[i].path);
    fclose(f);
}

void bm_load(void) {
    FILE *f = fopen(BM_FILE, "r");
    if (!f) return;
    char line[1000];
    while (fgets(line, sizeof line, f)) {
        unsigned long long pos = 0;
        int off = 0;
        if (sscanf(line, "%llu|%n", &pos, &off) < 1 || off <= 0) continue;
        char *label = line + off;
        char *bar = strchr(label, '|');
        if (!bar) continue;
        *bar = 0;
        char *path = bar + 1;
        size_t n = strlen(path);
        while (n && (path[n - 1] == '\n' || path[n - 1] == '\r')) path[--n] = 0;
        if (n) push(path, pos, label);
    }
    fclose(f);
}

int bm_get(const char *path, Bookmark *out, int max) {
    int n = 0;
    for (int i = 0; i < g_n && n < max; i++) {
        if (strcmp(g_e[i].path, path) != 0) continue;
        Bookmark b;
        b.pos = g_e[i].pos;
        snprintf(b.label, sizeof b.label, "%s", g_e[i].label);
        int j = n++;
        while (j > 0 && out[j - 1].pos > b.pos) { out[j] = out[j - 1]; j--; }   // insertion sort by position
        out[j] = b;
    }
    return n;
}

int bm_add(const char *path, uint64_t pos, const char *label) {
    int count = 0;
    for (int i = 0; i < g_n; i++) {
        if (strcmp(g_e[i].path, path) != 0) continue;
        count++;
        uint64_t d = g_e[i].pos > pos ? g_e[i].pos - pos : pos - g_e[i].pos;
        if (d < 2000) return 0;
    }
    if (count >= BM_MAX_PER_BOOK) return -1;
    if (!push(path, pos, label)) return -1;
    save();
    return 1;
}

void bm_remove(const char *path, uint64_t pos) {
    for (int i = 0; i < g_n; i++) {
        if (g_e[i].pos == pos && strcmp(g_e[i].path, path) == 0) {
            free(g_e[i].path);
            memmove(&g_e[i], &g_e[i + 1], (size_t)(g_n - i - 1) * sizeof *g_e);
            g_n--;
            save();
            return;
        }
    }
}
