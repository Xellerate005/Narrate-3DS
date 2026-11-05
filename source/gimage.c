// gimage.c
#include "gimage.h"
#include "cover.h"

#include <png.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DIM   512
#define MAX_BYTES (6u * 1024 * 1024)      // linear memory budget for all theme images together

static size_t g_bytes;

static int pow2(int v) { int p = 8; while (p < v) p <<= 1; return p; }

size_t gimage_bytes(void) { return g_bytes; }

bool gimage_load(GImage *g, const char *path, int frames, float fps) {
    memset(g, 0, sizeof *g);
    png_image img;
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&img, path)) return false;
    if (img.width < 1 || img.height < 1 || img.width > 4096 || img.height > 4096) { png_image_free(&img); return false; }
    img.format = PNG_FORMAT_RGBA;
    int w = (int)img.width, h = (int)img.height;
    u8 *src = (u8 *)malloc(PNG_IMAGE_SIZE(img));
    if (!src) { png_image_free(&img); return false; }
    if (!png_image_finish_read(&img, NULL, src, 0, NULL)) { free(src); return false; }

    // Too big for the GPU / our memory budget: shrink by an integer factor with a box filter.
    int k = 1;
    while (w / k > MAX_DIM || h / k > MAX_DIM) k++;
    if (frames < 1) frames = 1;
    int nw = w / k, nh = h / k;
    if (frames > 1) nw -= nw % frames;               // keep frames an exact width
    if (nw < 1 || nh < 1) { free(src); return false; }

    int tw = pow2(nw), th = pow2(nh);
    if (tw > 1024 || th > 1024 || g_bytes + (size_t)tw * th * 4 > MAX_BYTES) { free(src); return false; }
    u8 *buf = (u8 *)calloc((size_t)tw * th, 4);
    if (!buf) { free(src); return false; }

    for (int y = 0; y < nh; y++) {
        for (int x = 0; x < nw; x++) {
            unsigned r = 0, gg = 0, b = 0, a = 0, n = 0;
            for (int dy = 0; dy < k; dy++) {
                const u8 *p = src + (((size_t)(y * k + dy)) * w + (size_t)x * k) * 4;
                for (int dx = 0; dx < k; dx++, p += 4) {
                    // premultiply while averaging so transparent pixels don't bleed their colour into the edges
                    r += p[0] * p[3]; gg += p[1] * p[3]; b += p[2] * p[3]; a += p[3]; n++;
                }
            }
            u8 *o = buf + ((size_t)y * tw + x) * 4;
            if (a) { o[0] = (u8)(r / a); o[1] = (u8)(gg / a); o[2] = (u8)(b / a); }
            o[3] = (u8)(a / (n ? n : 1));
        }
    }
    free(src);

    bool ok = cover_tex_from_rgba_wh(&g->tex, tw, th, buf);
    free(buf);
    if (!ok) return false;
    g->w = nw; g->h = nh; g->tw = tw; g->th = th; g->frames = frames; g->fps = fps; g->ok = true;
    g_bytes += (size_t)tw * th * 4;
    return true;
}

void gimage_free(GImage *g) {
    if (g->ok) {
        C3D_TexDelete(&g->tex);
        size_t b = (size_t)g->tw * g->th * 4;
        g_bytes = g_bytes > b ? g_bytes - b : 0;
    }
    memset(g, 0, sizeof *g);
}

int gimage_frame_w(const GImage *g) { return g->frames > 0 ? g->w / g->frames : g->w; }

void gimage_draw(const GImage *g, int frame, float x, float y, float w, float h, float rot_deg, float alpha, int flip, uint32_t tint) {
    if (!g->ok || w <= 0 || h <= 0) return;
    int fw = gimage_frame_w(g);
    if (frame < 0) frame = 0;
    if (g->frames > 1) frame %= g->frames; else frame = 0;
    float x0 = (float)(frame * fw), x1 = x0 + fw, y0 = 0, y1 = (float)g->h;

    // Same orientation convention as the cover art: the texture rows are stored flipped and read through mirrored
    // coordinates, which makes an ordinary PNG show upright.
    Tex3DS_SubTexture st;
    st.width = (u16)fw;
    st.height = (u16)g->h;
    st.left = 1.0f - x0 / g->tw;
    st.right = 1.0f - x1 / g->tw;
    st.top = y0 / g->th;
    st.bottom = y1 / g->th;
    if (flip & 1) { float t = st.top; st.top = st.bottom; st.bottom = t; }
    if (flip & 2) { float t = st.left; st.left = st.right; st.right = t; }
    C2D_Image im = {(C3D_Tex *)&g->tex, &st};

    C2D_DrawParams p;
    memset(&p, 0, sizeof p);
    p.pos.w = w; p.pos.h = h;
    p.center.x = w / 2; p.center.y = h / 2;
    p.pos.x = x + w / 2; p.pos.y = y + h / 2;          // citro2d places the centre point at pos
    p.depth = 0.5f;
    p.angle = rot_deg * 0.01745329252f;

    C2D_ImageTint t;
    if (tint) {
        u32 a = (u32)((float)((tint >> 24) & 255) * (alpha < 0 ? 0 : (alpha > 1 ? 1 : alpha)));
        C2D_PlainImageTint(&t, (tint & 0x00FFFFFFu) | (a << 24), 1.0f);
        C2D_DrawImage(im, &p, &t);
    } else if (alpha < 0.999f) {
        C2D_AlphaImageTint(&t, alpha < 0 ? 0 : alpha);
        C2D_DrawImage(im, &p, &t);
    } else {
        C2D_DrawImage(im, &p, NULL);
    }
}
