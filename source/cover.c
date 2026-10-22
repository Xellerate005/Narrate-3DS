// cover.c - JPEG (libjpeg-turbo) and PNG (libpng) cover decoding -> 256x256 RGBA8 texture,
// plus the shared orientation (flip / rotate) used by covers and shelf thumbnails.
#include "cover.h"
#include "theme.h"
#include "thumbs.h"

#include <jpeglib.h>
#include <png.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// The PICA200 stores texture rows bottom-up (see Settings > Cover flip / rotate for the on-screen result).
#define COVER_FLIP 1

#define TEX_SIZE 256

static C3D_Tex g_tex;
static Tex3DS_SubTexture g_sub;
static C2D_Image g_img;
static bool g_have;
static float g_avg[3];
static int g_flip, g_rot;     // flip: bit0 vertical, bit1 horizontal (as seen on screen); rot: quarter turns

void cover_set_orient(int flip, int rot) {
    g_flip = flip & 3;
    g_rot = rot & 3;
}

// Draws any image centred at (cx, cy), honouring the cover flip/rotate settings plus an extra rotation (radians).
static void draw_img(const C2D_Image *img, float cx, float cy, float size, float extra) {
    // Flips are applied on screen *after* the rotation, so for 90/270 degrees the texture axes swap.
    int fv = g_flip & 1, fh = (g_flip >> 1) & 1;
    if (g_rot & 1) { int t = fv; fv = fh; fh = t; }
    // The base look (flip "None", rotation 0) has both texture axes swapped.
    bool swap_v = !fv, swap_h = !fh;
    Tex3DS_SubTexture st = *img->subtex;
    st.top = swap_v ? 0.0f : 1.0f;
    st.bottom = swap_v ? 1.0f : 0.0f;
    st.left = swap_h ? 1.0f : 0.0f;
    st.right = swap_h ? 0.0f : 1.0f;
    C2D_Image tmp = {img->tex, &st};

    C2D_DrawParams p;
    memset(&p, 0, sizeof p);
    p.pos.w = size; p.pos.h = size;
    p.center.x = size / 2; p.center.y = size / 2;       // rotate around the middle of the image...
    p.pos.x = cx; p.pos.y = cy;                         // ...and citro2d places that centre point at pos
    p.depth = 0.5f;
    p.angle = (float)g_rot * 1.57079633f + extra;
    C2D_DrawImage(tmp, &p, NULL);
}

void cover_draw_image(const C2D_Image *img, float x, float y, float size) {
    if (img) draw_img(img, x + size / 2, y + size / 2, size, 0.0f);
}

void cover_draw(float x, float y, float size) {
    if (g_have) draw_img(&g_img, x + size / 2, y + size / 2, size, 0.0f);
}

void cover_draw_centered(float cx, float cy, float size, float extra_rad) {
    if (g_have) draw_img(&g_img, cx, cy, size, extra_rad);
}

/* ---------------------------------------------------------------- decoders */

struct jerr {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
};

static void jerr_exit(j_common_ptr c) {
    struct jerr *e = (struct jerr *)c->err;
    longjmp(e->jb, 1);
}

static u8 *decode_jpeg(const u8 *d, u32 len, int *w, int *h) {
    struct jpeg_decompress_struct ci;
    struct jerr je;
    u8 *volatile out = NULL;
    u8 *volatile row = NULL;

    ci.err = jpeg_std_error(&je.pub);
    je.pub.error_exit = jerr_exit;
    if (setjmp(je.jb)) {
        jpeg_destroy_decompress(&ci);
        free(out);
        free(row);
        return NULL;
    }
    jpeg_create_decompress(&ci);
    jpeg_mem_src(&ci, (unsigned char *)d, len);
    jpeg_read_header(&ci, TRUE);
    ci.out_color_space = JCS_RGB;
    unsigned denom = 1;
    while (denom < 8 && (ci.image_width / denom > 512 || ci.image_height / denom > 512)) denom *= 2;
    ci.scale_num = 1;
    ci.scale_denom = denom;
    jpeg_start_decompress(&ci);

    *w = (int)ci.output_width;
    *h = (int)ci.output_height;
    out = (u8 *)malloc((size_t)*w * *h * 4);
    row = (u8 *)malloc((size_t)*w * 3);
    if (!out || !row) longjmp(je.jb, 1);

    while (ci.output_scanline < ci.output_height) {
        u8 *rp = row;
        jpeg_read_scanlines(&ci, &rp, 1);
        u8 *dst = out + (size_t)(ci.output_scanline - 1) * *w * 4;
        for (int x = 0; x < *w; x++) {
            dst[x * 4 + 0] = row[x * 3 + 0];
            dst[x * 4 + 1] = row[x * 3 + 1];
            dst[x * 4 + 2] = row[x * 3 + 2];
            dst[x * 4 + 3] = 255;
        }
    }
    jpeg_finish_decompress(&ci);
    jpeg_destroy_decompress(&ci);
    free(row);
    return out;
}

static u8 *decode_png(const u8 *d, u32 len, int *w, int *h) {
    png_image img;
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&img, d, len)) return NULL;
    if (img.width > 1400 || img.height > 1400) { png_image_free(&img); return NULL; }
    img.format = PNG_FORMAT_RGBA;
    u8 *buf = (u8 *)malloc(PNG_IMAGE_SIZE(img));
    if (!buf) { png_image_free(&img); return NULL; }
    if (!png_image_finish_read(&img, NULL, buf, 0, NULL)) { free(buf); return NULL; }
    *w = (int)img.width;
    *h = (int)img.height;
    return buf;
}

/* ------------------------------------------------------------- texture build */

static inline u32 morton_offset(u32 x, u32 y) {
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

// rgba: size*size*4 bytes (R,G,B,A), top-down. Creates a swizzled GPU texture.
bool cover_tex_from_rgba(C3D_Tex *tex, int size, const uint8_t *rgba) {
    if (!C3D_TexInit(tex, (u16)size, (u16)size, GPU_RGBA8)) return false;
    u32 *dst = (u32 *)tex->data;
    for (int y = 0; y < size; y++) {
        u32 ty = COVER_FLIP ? (u32)(size - 1 - y) : (u32)y;
        for (int x = 0; x < size; x++) {
            const u8 *p = rgba + ((size_t)y * size + x) * 4;
            u32 px = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | 0xFF;
            dst[(((ty >> 3) * (u32)(size >> 3) + ((u32)x >> 3)) << 6) + morton_offset((u32)x, ty)] = px;
        }
    }
    C3D_TexFlush(tex);
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    return true;
}

bool cover_load(const uint8_t *data, uint32_t len, const char *book_path) {
    cover_free();
    if (!data || len < 16) return false;

    int w = 0, h = 0;
    u8 *src = NULL;
    if (data[0] == 0xFF && data[1] == 0xD8) src = decode_jpeg(data, len, &w, &h);
    else if (data[0] == 0x89 && data[1] == 'P') src = decode_png(data, len, &w, &h);
    if (!src || w < 8 || h < 8) { free(src); return false; }

    u8 *out = (u8 *)malloc((size_t)TEX_SIZE * TEX_SIZE * 4);
    if (!out) { free(src); return false; }

    // center-crop to a square, box-filter down to 256x256
    int side = w < h ? w : h;
    int sx0 = (w - side) / 2, sy0 = (h - side) / 2;
    double step = (double)side / TEX_SIZE;
    double sum[3] = {0, 0, 0};

    for (int y = 0; y < TEX_SIZE; y++) {
        int y0 = sy0 + (int)floor(y * step);
        int y1 = sy0 + (int)ceil((y + 1) * step);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > sy0 + side) y1 = sy0 + side;
        for (int x = 0; x < TEX_SIZE; x++) {
            int x0 = sx0 + (int)floor(x * step);
            int x1 = sx0 + (int)ceil((x + 1) * step);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > sx0 + side) x1 = sx0 + side;
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) {
                const u8 *p = src + ((size_t)yy * w + x0) * 4;
                for (int xx = x0; xx < x1; xx++, p += 4) { r += p[0]; g += p[1]; b += p[2]; n++; }
            }
            if (!n) n = 1;
            r /= n; g /= n; b /= n;
            sum[0] += r; sum[1] += g; sum[2] += b;
            u8 *o = out + ((size_t)y * TEX_SIZE + x) * 4;
            o[0] = (u8)r; o[1] = (u8)g; o[2] = (u8)b; o[3] = 255;
        }
    }
    free(src);

    double cnt = (double)TEX_SIZE * TEX_SIZE;
    g_avg[0] = (float)(sum[0] / cnt);
    g_avg[1] = (float)(sum[1] / cnt);
    g_avg[2] = (float)(sum[2] / cnt);

    if (book_path) thumb_save(book_path, out);          // small copy for the library shelf

    bool ok = cover_tex_from_rgba(&g_tex, TEX_SIZE, out);
    free(out);
    if (!ok) return false;

    g_sub.width = TEX_SIZE;
    g_sub.height = TEX_SIZE;
    g_sub.left = 0.0f; g_sub.top = 1.0f; g_sub.right = 1.0f; g_sub.bottom = 0.0f;
    g_img.tex = &g_tex;
    g_img.subtex = &g_sub;
    g_have = true;
    return true;
}

void cover_free(void) {
    if (g_have) {
        C3D_TexDelete(&g_tex);
        g_have = false;
    }
}

bool cover_ready(void) { return g_have; }

bool cover_accent(u8 *r, u8 *g, u8 *b) {
    if (!g_have) return false;
    float R = g_avg[0] / 255.f, G = g_avg[1] / 255.f, B = g_avg[2] / 255.f;
    float mx = fmaxf(R, fmaxf(G, B)), mn = fminf(R, fminf(G, B)), d = mx - mn;
    float s = mx > 0 ? d / mx : 0, hue = 0;
    if (s < 0.10f) return false;                      // basically grey: keep the default accent
    if (d > 0) {
        if (mx == R) hue = fmodf((G - B) / d, 6.0f);
        else if (mx == G) hue = (B - R) / d + 2.0f;
        else hue = (R - G) / d + 4.0f;
        if (hue < 0) hue += 6.0f;
    }
    s = fmaxf(s, 0.45f);
    s = fminf(s, 0.80f);
    float v = 0.95f;
    int i = (int)hue;
    float f = hue - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float rr, gg, bb;
    switch (i % 6) {
    case 0: rr = v; gg = t; bb = p; break;
    case 1: rr = q; gg = v; bb = p; break;
    case 2: rr = p; gg = v; bb = t; break;
    case 3: rr = p; gg = q; bb = v; break;
    case 4: rr = t; gg = p; bb = v; break;
    default: rr = v; gg = p; bb = q; break;
    }
    *r = (u8)(rr * 255);
    *g = (u8)(gg * 255);
    *b = (u8)(bb * 255);
    return true;
}
