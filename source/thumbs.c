// thumbs.c - 64x64 cover thumbnails, one raw RGBA file per book, with a small LRU texture cache
#include "thumbs.h"
#include "cover.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef THUMB_DIR
#define THUMB_DIR "sdmc:/3ds/narrate/thumbs"
#endif
#define THUMB_SZ    64
#define THUMB_BYTES (THUMB_SZ * THUMB_SZ * 4)
#define CACHE       20
#define LOADS_PER_FRAME 2

typedef struct {
    uint32_t h, stamp;
    bool used, missing;
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    C2D_Image img;
} TEnt;

static TEnt g_c[CACHE];
static uint32_t g_stamp;
static int g_loads;
static bool g_pending;
static uint8_t g_buf[THUMB_BYTES];       // static: the main thread's stack is small

static uint32_t hash_path(const char *p) {
    uint32_t h = 2166136261u;
    while (*p) { h ^= (unsigned char)*p++; h *= 16777619u; }
    return h;
}

static void file_for(uint32_t h, char *out, size_t n) { snprintf(out, n, THUMB_DIR "/%08x.t64", (unsigned)h); }

static void drop(TEnt *e) {
    if (e->used && !e->missing) C3D_TexDelete(&e->tex);
    e->used = false;
    e->missing = false;
}

void thumbs_init(void) { mkdir(THUMB_DIR, 0777); }

void thumbs_exit(void) {
    for (int i = 0; i < CACHE; i++) drop(&g_c[i]);
}

void thumbs_new_frame(void) { g_loads = 0; }

bool thumbs_pending(void) {
    bool p = g_pending;
    g_pending = false;
    return p;
}

void thumb_save(const char *book_path, const uint8_t *rgba256) {
    // 4x4 box filter: 256 -> 64
    for (int y = 0; y < THUMB_SZ; y++) {
        for (int x = 0; x < THUMB_SZ; x++) {
            unsigned r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 4; dy++) {
                const uint8_t *p = rgba256 + (((size_t)(y * 4 + dy)) * 256 + (size_t)x * 4) * 4;
                for (int dx = 0; dx < 4; dx++, p += 4) { r += p[0]; g += p[1]; b += p[2]; }
            }
            uint8_t *o = g_buf + ((size_t)y * THUMB_SZ + x) * 4;
            o[0] = (uint8_t)(r / 16); o[1] = (uint8_t)(g / 16); o[2] = (uint8_t)(b / 16); o[3] = 255;
        }
    }
    uint32_t h = hash_path(book_path);
    char file[128];
    file_for(h, file, sizeof file);
    FILE *f = fopen(file, "wb");
    if (f) { fwrite(g_buf, 1, THUMB_BYTES, f); fclose(f); }
    for (int i = 0; i < CACHE; i++) if (g_c[i].used && g_c[i].h == h) drop(&g_c[i]);   // reload the fresh one next time
}

const C2D_Image *thumb_get(const char *book_path) {
    uint32_t h = hash_path(book_path);
    for (int i = 0; i < CACHE; i++) {
        if (g_c[i].used && g_c[i].h == h) {
            g_c[i].stamp = ++g_stamp;
            return g_c[i].missing ? NULL : &g_c[i].img;
        }
    }
    if (g_loads >= LOADS_PER_FRAME) { g_pending = true; return NULL; }
    g_loads++;

    TEnt *slot = NULL;
    for (int i = 0; i < CACHE; i++) if (!g_c[i].used) { slot = &g_c[i]; break; }
    if (!slot) {
        slot = &g_c[0];
        for (int i = 1; i < CACHE; i++) if (g_c[i].stamp < slot->stamp) slot = &g_c[i];
        drop(slot);
    }
    slot->used = true;
    slot->missing = true;
    slot->h = h;
    slot->stamp = ++g_stamp;

    char file[128];
    file_for(h, file, sizeof file);
    FILE *f = fopen(file, "rb");
    if (!f) return NULL;
    size_t n = fread(g_buf, 1, THUMB_BYTES, f);
    fclose(f);
    if (n != THUMB_BYTES) return NULL;
    if (!cover_tex_from_rgba(&slot->tex, THUMB_SZ, g_buf)) return NULL;

    slot->sub.width = THUMB_SZ;
    slot->sub.height = THUMB_SZ;
    slot->sub.left = 0.0f; slot->sub.top = 1.0f; slot->sub.right = 1.0f; slot->sub.bottom = 0.0f;
    slot->img.tex = &slot->tex;
    slot->img.subtex = &slot->sub;
    slot->missing = false;
    return &slot->img;
}
