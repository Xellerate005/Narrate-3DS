// browser.c
#include "browser.h"
#include "progress.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MAX_ENTRIES 2048
static int g_sort;

static bool is_audio_file(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return !strcasecmp(dot, ".m4b") || !strcasecmp(dot, ".m4a") || !strcasecmp(dot, ".mp4");
}

// "Book 2" sorts before "Book 10"; case-insensitive.
static int natcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            const char *za = a, *zb = b;
            while (*za == '0') za++;
            while (*zb == '0') zb++;
            const char *ea = za, *eb = zb;
            while (isdigit((unsigned char)*ea)) ea++;
            while (isdigit((unsigned char)*eb)) eb++;
            size_t la = (size_t)(ea - za), lb = (size_t)(eb - zb);
            if (la != lb) return la < lb ? -1 : 1;
            int c = strncmp(za, zb, la);
            if (c) return c < 0 ? -1 : 1;
            a = ea;
            b = eb;
            continue;
        }
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca < cb ? -1 : 1;
        a++;
        b++;
    }
    return (*a ? 1 : 0) - (*b ? 1 : 0);
}

static int cmp(const void *pa, const void *pb) {
    const BrEntry *x = (const BrEntry *)pa, *y = (const BrEntry *)pb;
    bool xu = strcmp(x->name, "..") == 0, yu = strcmp(y->name, "..") == 0;
    if (xu != yu) return xu ? -1 : 1;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
    if (x->is_dir) return natcmp(x->name, y->name);
    switch (g_sort) {
    case SORT_RECENT:
        if ((x->ts > 0) != (y->ts > 0)) return x->ts > 0 ? -1 : 1;
        if (x->ts != y->ts) return x->ts > y->ts ? -1 : 1;
        break;
    case SORT_STATUS: {
        int rx = x->state == 1 ? 0 : (x->state == 0 ? 1 : 2);
        int ry = y->state == 1 ? 0 : (y->state == 0 ? 1 : 2);
        if (rx != ry) return rx < ry ? -1 : 1;
        if (rx == 0 && x->ts != y->ts) return x->ts > y->ts ? -1 : 1;     // in progress: most recent first
        break;
    }
    default:
        break;
    }
    return natcmp(x->name, y->name);
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

static void fill_file(const Browser *b, BrEntry *e) {
    char p[800];
    browser_path(b, e->name, p, sizeof p);
    e->state = progress_state(p, &e->frac);
    uint64_t ts = 0;
    progress_get_ts(p, &ts);
    e->ts = ts;
}

void browser_set_root(Browser *b, const char *root) {
    snprintf(b->root, sizeof b->root, "%s", root);
    size_t l = strlen(b->root);
    while (l > 6 && b->root[l - 1] == '/') b->root[--l] = 0;     // no trailing slash (keeps "sdmc:/" intact)
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
        if (!dir) fill_file(b, it);
    }
    closedir(d);
    g_sort = b->sort;
    qsort(b->items, b->count, sizeof(BrEntry), cmp);
}

void browser_enter(Browser *b, const char *dirname) {
    char p[512];
    int sort = b->sort;
    browser_path(b, dirname, p, sizeof p);
    b->sort = sort;
    browser_open(b, p);
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

void browser_set_sort(Browser *b, int mode) {
    b->sort = mode;
    if (!b->items || b->count < 2) return;
    char keep[256];
    bool have = b->sel >= 0 && b->sel < b->count;
    if (have) snprintf(keep, sizeof keep, "%s", b->items[b->sel].name);
    int start = b->items[0].is_cont ? 1 : 0;
    g_sort = mode;
    qsort(b->items + start, (size_t)(b->count - start), sizeof(BrEntry), cmp);
    if (have) {
        for (int i = 0; i < b->count; i++)
            if (!b->items[i].is_cont && strcmp(b->items[i].name, keep) == 0) { b->sel = i; break; }
    }
    if (b->sel < b->scroll) b->scroll = b->sel;
}

/* ---------------------------------------------------------- folder statistics */

typedef struct { int books, done; double sum; } Scan;

static bool entry_is_dir(const char *dir, struct dirent *e) {
    if (e->d_type == DT_DIR) return true;
    if (e->d_type == DT_REG) return false;
    char full[800];
    struct stat st;
    snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
    return stat(full, &st) == 0 && S_ISDIR(st.st_mode);
}

static void scan_dir(const char *path, int depth, Scan *s) {
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && s->books < 600) {
        if (e->d_name[0] == '.') continue;
        char full[800];
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        if (entry_is_dir(path, e)) { if (depth < 3) scan_dir(full, depth + 1, s); continue; }
        if (!is_audio_file(e->d_name)) continue;
        float f = 0;
        int st = progress_state(full, &f);
        s->books++;
        if (st == 2) { s->done++; s->sum += 1.0; }
        else s->sum += f;
    }
    closedir(d);
}

void browser_dir_stats(Browser *b, int idx) {
    if (idx < 0 || idx >= b->count) return;
    BrEntry *e = &b->items[idx];
    if (!e->is_dir || e->stats || !strcmp(e->name, "..")) return;
    char p[800];
    browser_path(b, e->name, p, sizeof p);
    Scan s = {0, 0, 0};
    scan_dir(p, 0, &s);
    e->books = s.books;
    e->done = s.done;
    e->sfrac = s.books ? (float)(s.sum / s.books) : 0;
    e->stats = true;
}

/* ------------------------------------------------------------------- marks */

static void mark_file(const char *p, bool fin) {
    uint64_t pos = 0, dur = 0;
    progress_get(p, &pos, &dur);
    if (!dur) dur = PROGRESS_UNKNOWN_DUR;
    progress_set_quiet(p, fin ? dur : 0, dur);
}

static void mark_dir(const char *path, bool fin, int depth) {
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) && n < 600) {
        if (e->d_name[0] == '.') continue;
        char full[800];
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        if (entry_is_dir(path, e)) { if (depth < 3) mark_dir(full, fin, depth + 1); continue; }
        if (!is_audio_file(e->d_name)) continue;
        mark_file(full, fin);
        n++;
    }
    closedir(d);
}

void browser_mark(Browser *b, int idx, bool finished) {
    if (idx < 0 || idx >= b->count) return;
    BrEntry *e = &b->items[idx];
    if (e->is_cont || !strcmp(e->name, "..")) return;
    char p[800];
    browser_path(b, e->name, p, sizeof p);
    if (e->is_dir) { mark_dir(p, finished, 0); e->stats = false; }
    else { mark_file(p, finished); fill_file(b, e); }
    progress_save();
}

/* ------------------------------------------------------------------ series */

typedef struct { char name[256]; } NameRec;

static int cmp_names(const void *a, const void *b) { return natcmp(((const NameRec *)a)->name, ((const NameRec *)b)->name); }

bool browser_next_book(const char *cur_path, char *out, size_t n) {
    char dir[800];
    snprintf(dir, sizeof dir, "%s", cur_path);
    char *slash = strrchr(dir, '/');
    if (!slash) return false;
    char cur[256];
    snprintf(cur, sizeof cur, "%s", slash + 1);
    *slash = 0;

    DIR *d = opendir(dir);
    if (!d) return false;
    NameRec *list = (NameRec *)malloc(sizeof(NameRec) * 512);
    if (!list) { closedir(d); return false; }
    int cnt = 0;
    struct dirent *e;
    while ((e = readdir(d)) && cnt < 512) {
        if (e->d_name[0] == '.' || entry_is_dir(dir, e) || !is_audio_file(e->d_name)) continue;
        snprintf(list[cnt++].name, sizeof list[0].name, "%s", e->d_name);
    }
    closedir(d);
    qsort(list, (size_t)cnt, sizeof(NameRec), cmp_names);

    bool found = false;
    int at = -1;
    for (int i = 0; i < cnt; i++) if (strcasecmp(list[i].name, cur) == 0) { at = i; break; }
    if (at >= 0) {
        for (int j = at + 1; j < cnt && !found; j++) {
            char full[800];
            snprintf(full, sizeof full, "%.500s/%.255s", dir, list[j].name);
            if (progress_state(full, NULL) != 2) { snprintf(out, n, "%s", full); found = true; }
        }
    }
    free(list);
    return found;
}
