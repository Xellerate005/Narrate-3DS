// progress.c - tiny text database: "<pos_ms>|<dur_ms>|<last_played_unix>|<path>" per line
#include "progress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef PROGRESS_FILE
#define PROGRESS_FILE "sdmc:/3ds/narrate/progress.txt"
#endif
#ifndef LAST_FILE
#define LAST_FILE "sdmc:/3ds/narrate/last.txt"
#endif

typedef struct { char path[800]; uint64_t pos, dur, ts; } Entry;

static Entry *g_e;
static int g_n, g_cap;
static bool g_dirty;

static Entry *find(const char *path) {
    for (int i = 0; i < g_n; i++)
        if (strcmp(g_e[i].path, path) == 0) return &g_e[i];
    return NULL;
}

static Entry *add(const char *path) {
    if (g_n == g_cap) {
        int nc = g_cap ? g_cap * 2 : 32;
        Entry *p = (Entry *)realloc(g_e, (size_t)nc * sizeof *p);
        if (!p) return NULL;
        g_e = p;
        g_cap = nc;
    }
    Entry *e = &g_e[g_n++];
    memset(e, 0, sizeof *e);
    snprintf(e->path, sizeof e->path, "%.799s", path);
    return e;
}

void progress_load(void) {
    FILE *f = fopen(PROGRESS_FILE, "r");
    if (!f) return;
    char line[1000];
    while (fgets(line, sizeof line, f)) {
        unsigned long long pos = 0, dur = 0, ts = 0;
        int off = 0;
        int n = sscanf(line, "%llu|%llu|%llu|%n", &pos, &dur, &ts, &off);
        if (n < 3 || off <= 0) {                       // older two-field format: pos|dur|path
            ts = 0; off = 0;
            if (sscanf(line, "%llu|%llu|%n", &pos, &dur, &off) < 2 || off <= 0) continue;
        }
        char *path = line + off;
        size_t len = strlen(path);
        while (len && (path[len - 1] == '\n' || path[len - 1] == '\r')) path[--len] = 0;
        if (!len) continue;
        Entry *e = add(path);
        if (e) { e->pos = pos; e->dur = dur; e->ts = ts; }
    }
    fclose(f);
}

bool progress_get(const char *path, uint64_t *pos, uint64_t *dur) {
    Entry *e = find(path);
    if (!e) return false;
    if (pos) *pos = e->pos;
    if (dur) *dur = e->dur;
    return true;
}

bool progress_get_ts(const char *path, uint64_t *ts) {
    Entry *e = find(path);
    if (!e) return false;
    if (ts) *ts = e->ts;
    return true;
}

static void set_impl(const char *path, uint64_t pos, uint64_t dur, bool touch) {
    Entry *e = find(path);
    if (!e) e = add(path);
    if (!e) return;
    if (e->pos != pos || e->dur != dur) g_dirty = true;
    e->pos = pos;
    e->dur = dur;
    if (touch) { e->ts = (uint64_t)time(NULL); g_dirty = true; }
}

void progress_set(const char *path, uint64_t pos, uint64_t dur) { set_impl(path, pos, dur, true); }
void progress_set_quiet(const char *path, uint64_t pos, uint64_t dur) { set_impl(path, pos, dur, false); }

int progress_state(const char *path, float *frac) {
    uint64_t pos = 0, dur = 0;
    if (frac) *frac = 0;
    if (!progress_get(path, &pos, &dur) || !dur) return 0;
    if (frac) *frac = pos >= dur ? 1.0f : (float)((double)pos / (double)dur);
    if (pos + 5000 >= dur) return 2;
    return pos > 0 ? 1 : 0;
}

void progress_save(void) {
    if (!g_dirty) return;
    FILE *f = fopen(PROGRESS_FILE, "w");
    if (!f) return;
    for (int i = 0; i < g_n; i++)
        fprintf(f, "%llu|%llu|%llu|%s\n", (unsigned long long)g_e[i].pos, (unsigned long long)g_e[i].dur,
                (unsigned long long)g_e[i].ts, g_e[i].path);
    fclose(f);
    g_dirty = false;
}

void progress_set_last(const char *path) {
    FILE *f = fopen(LAST_FILE, "w");
    if (!f) return;
    fputs(path, f);
    fclose(f);
}

bool progress_get_last(char *out, size_t n) {
    FILE *f = fopen(LAST_FILE, "r");
    if (!f) return false;
    if (!fgets(out, (int)n, f)) { fclose(f); return false; }
    fclose(f);
    size_t l = strlen(out);
    while (l && (out[l - 1] == '\n' || out[l - 1] == '\r')) out[--l] = 0;
    return l > 0;
}
