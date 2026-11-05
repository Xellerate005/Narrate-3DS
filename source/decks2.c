// decks2.c - seven more animated top screens: Amp (VU meters), Open book, Journey, Campfire, Night sky, Aquarium, CRT.
// All of them only move while the book is playing, and are kept to a few dozen primitives per frame so the old 2DS keeps up.
#include "decks.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cover.h"
#include "ui.h"

#define TAU 6.2831853f
#define PI  3.1415927f

static u32 rgb(unsigned r, unsigned g, unsigned b) { return C2D_Color32((u8)r, (u8)g, (u8)b, 255); }
static u32 rgba(unsigned r, unsigned g, unsigned b, unsigned a) { return C2D_Color32((u8)r, (u8)g, (u8)b, (u8)a); }
static u32 with_alpha(u32 c, unsigned a) { return (c & 0x00FFFFFFu) | ((u32)a << 24); }

/* animation clock in seconds: advances only while playing */
static float g_t;
static uint64_t g_t_last;
static float clk(const DeckInfo *d) {
    if (g_t_last && d->playing && d->now > g_t_last) {
        uint64_t dt = d->now - g_t_last;
        if (dt > 250) dt = 250;
        g_t += (float)dt * 0.001f;
        if (g_t > 100000.0f) g_t = 0;
    }
    g_t_last = d->now;
    return g_t;
}

static unsigned hash32(unsigned x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static float rnd(unsigned seed) { return (float)(hash32(seed) & 0xFFFF) / 65535.0f; }

static void title_block(const DeckInfo *d, float x, float y, float w, u32 col, u32 dim) {
    const M4b *b = d->book;
    ui_text_fit(b->title, x, y, w, 0.6f, col, UI_LEFT);
    ui_text_fit(b->chapters[d->ch].title ? b->chapters[d->ch].title : "", x, y + 20, w, 0.44f, dim, UI_LEFT);
}

static void gradient(float x, float y, float w, float h, u32 top, u32 bot) { C2D_DrawRectangle(x, y, 0.5f, w, h, top, top, bot, bot); }

/* ------------------------------------------------------------------- amp */

static void meter(float x, float y, float w, float h, float lvl, float t, bool playing, u32 face, u32 ink, u32 red, u32 needle) {
    ui_rrect(x - 4, y - 4, w + 8, h + 8, 8, ink);
    ui_rrect(x, y, w, h, 6, face);
    float px = x + w / 2, py = y + h - 6, r = h * 0.92f;
    for (int i = 0; i <= 10; i++) {                                           // scale ticks, the last 3 are the red zone
        float a = (-55.0f + 11.0f * i) * PI / 180.0f;
        float r0 = r * (i % 5 == 0 ? 0.78f : 0.86f);
        ui_line(px + sinf(a) * r0, py - cosf(a) * r0, px + sinf(a) * r * 0.95f, py - cosf(a) * r * 0.95f, i % 5 == 0 ? 2.0f : 1.2f, i >= 8 ? red : ink);
    }
    float v = lvl;
    if (!playing) v = 0.0f;
    else if (v < 0.02f) v = 0.02f + 0.02f * (0.5f + 0.5f * sinf(t * 7.0f));   // idle flutter so it never looks frozen
    if (v > 1) v = 1;
    float a = (-55.0f + 110.0f * v) * PI / 180.0f;
    ui_line(px, py, px + sinf(a) * r * 0.97f, py - cosf(a) * r * 0.97f, 2.0f, needle);
    ui_circle(px, py + 2, 6, ink);
}

void deck_amp(const DeckInfo *d) {
    float t = clk(d);
    u32 wood = theme_mix(ui_pal.bg, ui_accent, 0.12f), face = theme_mix(rgb(0xF4, 0xE6, 0xC4), ui_pal.bg, 0.15f), ink = rgb(0x2B, 0x1D, 0x12);
    ui_rrect(8, 8, 384, 224, 12, wood);
    ui_rrect_outline(8, 8, 384, 224, 12, 2, ui_pal.line, wood);
    meter(26, 24, 168, 96, d->vu_l, t, d->playing, face, ink, rgb(0xC0, 0x30, 0x20), rgb(0x20, 0x20, 0x20));
    meter(206, 24, 168, 96, d->vu_r, t + 1.7f, d->playing, face, ink, rgb(0xC0, 0x30, 0x20), rgb(0x20, 0x20, 0x20));
    ui_text("L", 36, 98, 0.4f, ink, UI_LEFT);
    ui_text("R", 356, 98, 0.4f, ink, UI_RIGHT);

    title_block(d, 26, 134, 348, ui_pal.text, ui_pal.dim);
    // LED progress strip: 28 segments, green -> amber -> red like a level meter
    int lit = (int)(d->frac * 28.0f + 0.5f);
    for (int i = 0; i < 28; i++) {
        u32 on = i < 18 ? rgb(0x5C, 0xE0, 0x6A) : (i < 24 ? rgb(0xFF, 0xC0, 0x40) : rgb(0xFF, 0x50, 0x40));
        ui_rect(26 + i * 12.5f, 186, 10, 12, i < lit ? on : with_alpha(on, 40));
    }
    char s[48];
    snprintf(s, sizeof s, "CH %d/%d", d->ch + 1, d->book->nchapters);
    ui_text(s, 26, 206, 0.4f, ui_accent, UI_LEFT);
    ui_text("- Narrate -", 374, 206, 0.4f, ui_pal.dim, UI_RIGHT);
}

/* ------------------------------------------------------------------ book */

void deck_book(const DeckInfo *d) {
    float t = clk(d);
    u32 page = rgb(0xF6, 0xEE, 0xDA), page2 = rgb(0xE6, 0xDA, 0xBF), ink = rgb(0x3A, 0x2E, 0x20), cover = theme_mix(rgb(0x5A, 0x3A, 0x22), ui_accent, 0.25f);
    ui_rrect(10, 12, 380, 214, 10, cover);                                      // cover boards peeking out
    ui_rect(16, 18, 184, 200, page);
    ui_rect(200, 18, 184, 200, page2);
    ui_rect(198, 18, 4, 200, rgb(0xB8, 0xA8, 0x88));                            // gutter
    for (int i = 0; i < 4; i++) ui_rect(384 + i, 20 + i, 1, 196, rgb(0xD8, 0xCC, 0xAE));   // page edges

    // left page: chapter heading + a few "text" lines whose length hints at progress through the chapter
    char s[40];
    snprintf(s, sizeof s, "Chapter %d", d->ch + 1);
    ui_text(s, 108, 30, 0.5f, ui_accent, UI_CENTER);
    ui_text_wrap(d->book->chapters[d->ch].title ? d->book->chapters[d->ch].title : "", 32, 52, 152, 0.5f, ink, 3, 20);
    float cf = d->clen ? (float)d->elapsed / (float)d->clen : 0;
    for (int i = 0; i < 8; i++) ui_rect(32, 124 + i * 11, i == 7 ? 70.0f : 152.0f, 3, with_alpha(ink, i < (int)(cf * 8.0f) ? 120 : 40));
    // right page: the book itself
    ui_text_wrap(d->book->title, 216, 40, 152, 0.58f, ink, 3, 22);
    ui_text_fit(d->book->artist, 216, 112, 152, 0.42f, rgb(0x7A, 0x6A, 0x50), UI_LEFT);
    for (int i = 0; i < 5; i++) ui_rect(216, 140 + i * 11, i == 4 ? 80.0f : 152.0f, 3, with_alpha(ink, 40));

    // a page turns every few seconds while playing
    float ph = fmodf(t / 7.0f, 1.0f);
    if (d->playing && ph < 0.12f) {
        float a = ph / 0.12f * PI, x = 200 + cosf(a) * 184;
        float x0 = x < 200 ? x : 200, w = fabsf(x - 200);
        ui_rect(x0, 18, w, 200, a < PI / 2 ? page2 : page);
        ui_rect(x0, 18, w, 200, rgba(0, 0, 0, (unsigned)(40 * sinf(a))));
    }
    // ribbon bookmark whose position follows book progress along the bottom edge
    float rx = 24 + d->frac * 352;
    ui_rect(rx - 4, 200, 8, 26, ui_accent);
    C2D_DrawTriangle(rx - 4, 226, ui_accent, rx + 4, 226, ui_accent, rx, 218, ui_pal.bg, 0.5f);
}

/* --------------------------------------------------------------- journey */

static void ridge(float base, float amp, float period, float off, u32 col) {
    for (float x = -period; x < 400 + period; x += period) {
        float cx = x - fmodf(off, period);
        C2D_DrawTriangle(cx, base, col, cx + period, base, col, cx + period / 2, base - amp, col, 0.5f);
    }
}

void deck_journey(const DeckInfo *d) {
    float t = clk(d);
    gradient(0, 0, 400, 170, theme_mix(rgb(0x1B, 0x2A, 0x5A), ui_accent, 0.18f), theme_mix(rgb(0xF2, 0x9A, 0x4A), ui_accent, 0.12f));
    for (int i = 0; i < 14; i++)                                              // a few stars near the top that fade at dusk
        ui_rect(rnd((unsigned)i * 3 + 1) * 400.0f, rnd((unsigned)i * 3 + 2) * 70.0f, 1.5f, 1.5f, rgba(255, 255, 255, 150));
    float sx = 40 + 320 * d->frac, sy = 150 - 112 * sinf(PI * d->frac);       // the sun crosses the sky as the book progresses
    ui_circle(sx, sy, 24, rgba(255, 220, 140, 70));
    ui_circle(sx, sy, 15, rgb(0xFF, 0xE9, 0xA8));
    ridge(150, 60, 120, t * 6.0f, rgb(0x3A, 0x4C, 0x7A));
    ridge(170, 46, 90, t * 14.0f, rgb(0x2A, 0x38, 0x5E));
    ui_rect(0, 170, 400, 70, rgb(0x1A, 0x24, 0x3E));
    ui_rect(0, 170, 400, 3, rgb(0x34, 0x48, 0x74));
    for (int i = 0; i < 9; i++) ui_rect(fmodf(i * 52.0f - t * 40.0f, 468.0f) - 20, 214, 26, 4, rgba(255, 255, 255, 40));   // trail marks sliding past
    float wx = 30 + d->frac * 340, bob = d->playing ? fabsf(sinf(t * 6.0f)) * 3.0f : 0;     // the traveller
    ui_circle(wx, 196 - bob, 5, rgb(0xFF, 0xD7, 0xA8));
    ui_line(wx, 201 - bob, wx, 214, 3, ui_accent);
    ui_line(wx, 214, wx - 3 + sinf(t * 6) * 3, 224, 2, ui_accent);
    ui_line(wx, 214, wx + 3 - sinf(t * 6) * 3, 224, 2, ui_accent);
    char s[40];
    snprintf(s, sizeof s, "Day %d of %d", d->ch + 1, d->book->nchapters);
    ui_text(s, 16, 12, 0.42f, rgb(0xFF, 0xE9, 0xC8), UI_LEFT);
    ui_text_fit(d->book->title, 16, 28, 368, 0.62f, rgb(0xFF, 0xFF, 0xFF), UI_LEFT);
}

/* -------------------------------------------------------------- campfire */

void deck_campfire(const DeckInfo *d) {
    float t = clk(d);
    ui_rect(0, 0, 400, 240, rgb(0x0B, 0x07, 0x08));
    for (int i = 0; i < 26; i++) {                                              // stars
        float tw = d->playing ? 0.5f + 0.5f * sinf(t * (1.0f + rnd(i + 90) * 2.0f) + (float)i) : 0.8f;
        ui_rect(rnd((unsigned)i * 2) * 400.0f, rnd((unsigned)i * 2 + 1) * 120.0f, 1.5f, 1.5f, rgba(255, 245, 220, (unsigned)(60 + 140 * tw)));
    }
    const float fx = 292, fy = 198;
    float fl = d->playing ? 0.5f + 0.5f * sinf(t * 9.0f) * sinf(t * 5.3f + 1.0f) : 0.5f;
    for (int i = 4; i >= 1; i--) ui_circle(fx, fy - 20, 30 + i * 18 + fl * 6, rgba(255, 120, 30, 14));   // warm glow on the ground
    ui_line(fx - 46, fy + 12, fx + 40, fy - 2, 9, rgb(0x4A, 0x2C, 0x18));       // logs
    ui_line(fx - 40, fy - 2, fx + 46, fy + 12, 9, rgb(0x5A, 0x34, 0x1C));
    // flame: three stacked tongues that sway and flicker
    for (int k = 0; k < 3; k++) {
        float h = (46 - k * 11) + fl * 10 + (d->playing ? sinf(t * (7.0f + k * 2.0f) + k) * 4 : 0);
        float w = 26 - k * 7, sw = d->playing ? sinf(t * (4.0f + k) + k * 1.7f) * (5 - k) : 0;
        u32 col = k == 0 ? rgb(0xFF, 0x7A, 0x1A) : (k == 1 ? rgb(0xFF, 0xB8, 0x40) : rgb(0xFF, 0xF0, 0xB0));
        C2D_DrawTriangle(fx - w, fy, col, fx + w, fy, col, fx + sw, fy - h - 14, col, 0.5f);
        ui_circle(fx, fy - 3, w * 0.92f, col);
    }
    if (d->playing) {
        for (int i = 0; i < 9; i++) {                                           // sparks rising and fading
            float life = fmodf(t * (0.35f + rnd(i) * 0.3f) + rnd(i + 40), 1.0f);
            float sx = fx + (rnd(i + 7) - 0.5f) * 40 + sinf(t * 2 + i) * 10 * life, sy = fy - 30 - life * 120;
            ui_rect(sx, sy, 2, 2, rgba(255, 190, 80, (unsigned)(220 * (1 - life))));
        }
    }
    char s[40];
    snprintf(s, sizeof s, "Chapter %d of %d", d->ch + 1, d->book->nchapters);
    ui_text(s, 18, 22, 0.42f, ui_accent, UI_LEFT);
    ui_text_wrap(d->book->title, 18, 40, 200, 0.62f, rgb(0xFF, 0xEB, 0xD2), 3, 24);
    ui_text_fit(d->book->artist, 18, 120, 200, 0.42f, rgb(0xC8, 0xA0, 0x80), UI_LEFT);
    ui_bar(18, 218, 190, 5, d->frac, rgb(0x3A, 0x22, 0x18), ui_accent);
}

/* ------------------------------------------------------------- night sky */

void deck_nightsky(const DeckInfo *d) {
    float t = clk(d);
    gradient(0, 0, 400, 240, rgb(0x05, 0x08, 0x16), theme_mix(rgb(0x14, 0x1C, 0x48), ui_accent, 0.10f));
    for (int i = 0; i < 46; i++) {
        float tw = d->playing ? 0.55f + 0.45f * sinf(t * (0.8f + rnd(i + 5) * 2.2f) + (float)i * 1.3f) : 0.8f;
        float s = rnd((unsigned)i + 300) > 0.85f ? 2.5f : 1.5f;
        ui_rect(rnd((unsigned)i * 2 + 11) * 400.0f, rnd((unsigned)i * 2 + 12) * 190.0f, s, s, rgba(235, 240, 255, (unsigned)(80 + 150 * tw)));
    }
    // constellation: one star per chapter (up to 24) joined by lines; the ones you have reached are lit
    int n = d->book->nchapters < 2 ? 2 : (d->book->nchapters > 24 ? 24 : d->book->nchapters);
    int cur = d->book->nchapters > 24 ? (int)(d->frac * 23.0f + 0.5f) : d->ch;
    float px = 0, py = 0;
    for (int i = 0; i < n; i++) {
        float x = 30 + (340.0f * i) / (n - 1), y = 150 + sinf(i * 1.7f) * 18 + sinf(i * 0.6f + 1.0f) * 12;
        if (i) ui_line(px, py, x, y, 1.2f, i <= cur ? with_alpha(ui_accent, 190) : rgba(140, 150, 200, 60));
        px = x; py = y;
        bool on = i <= cur;
        if (on) ui_circle(x, y, 7, with_alpha(ui_accent, 60));
        ui_circle(x, y, on ? 3.2f : 2.0f, on ? ui_accent : rgba(160, 170, 220, 140));
    }
    float mx = 60 + 280 * d->frac;                                              // the moon drifts across with the book
    ui_circle(mx, 52, 22, rgba(255, 244, 210, 30));
    ui_circle(mx, 52, 15, rgb(0xFF, 0xF4, 0xD2));
    ui_circle(mx + 6, 48, 13, rgb(0x08, 0x0C, 0x1E));                           // cut-out makes it a crescent
    float ph = fmodf(t, 11.0f);                                                 // an occasional shooting star
    if (d->playing && ph < 0.9f) {
        float f = ph / 0.9f, x = 340 - f * 200, y = 20 + f * 70;
        ui_line(x, y, x + 24, y - 10, 1.6f, rgba(255, 255, 255, (unsigned)(220 * (1 - f))));
    }
    title_block(d, 20, 196, 360, ui_pal.text, rgb(0x9A, 0xA6, 0xD4));
}

/* -------------------------------------------------------------- aquarium */

static void fish(float x, float y, float s, float dir, u32 col, float wag) {
    ui_circle(x, y, 9 * s, col);
    ui_circle(x - dir * 5 * s, y, 7 * s, col);
    C2D_DrawTriangle(x - dir * 12 * s, y, col, x - dir * (21 * s + wag), y - 8 * s, col, x - dir * (21 * s + wag), y + 8 * s, col, 0.5f);
    ui_circle(x + dir * 4 * s, y - 2 * s, 1.6f * s, rgb(0x10, 0x20, 0x28));
}

void deck_aquarium(const DeckInfo *d) {
    float t = clk(d);
    gradient(0, 0, 400, 240, theme_mix(rgb(0x1B, 0x8C, 0xB8), ui_accent, 0.10f), rgb(0x06, 0x2A, 0x44));
    for (int i = 0; i < 3; i++) ui_rect(40 + i * 130.0f + sinf(t * 0.6f + i) * 20, 0, 24, 150, rgba(255, 255, 255, 10));   // light shafts
    for (int k = 0; k < 4; k++) {                                               // seaweed
        float bx = 24 + k * 120.0f;
        float px = bx, py = 240;
        for (int i = 1; i <= 7; i++) {
            float x = bx + sinf(t * 1.3f + k + i * 0.5f) * (3.0f + i * 1.2f), y = 240 - i * 13.0f;
            ui_line(px, py, x, y, 4, rgb(0x1E, 0x8A, 0x5A));
            px = x; py = y;
        }
    }
    ui_rect(0, 226, 400, 14, rgb(0xC8, 0xB0, 0x78));
    fish(fmodf(t * 26.0f, 480.0f) - 40, 70 + sinf(t * 1.3f) * 8, 1.1f, 1.0f, rgb(0xFF, 0x9A, 0x3C), sinf(t * 8) * 2);
    fish(440 - fmodf(t * 18.0f + 100, 500.0f), 128 + sinf(t * 1.1f + 2) * 6, 0.8f, -1.0f, rgb(0xFF, 0x5E, 0x8A), sinf(t * 7 + 1) * 2);
    fish(fmodf(t * 12.0f + 200, 480.0f) - 40, 176 + sinf(t * 0.9f) * 5, 0.65f, 1.0f, with_alpha(ui_accent, 255), sinf(t * 9) * 2);
    for (int i = 0; i < 12; i++) {                                              // bubbles
        float life = fmodf(t * (0.12f + rnd(i) * 0.1f) + rnd(i + 60), 1.0f);
        ui_circle(rnd((unsigned)i + 20) * 400.0f + sinf(t * 2 + i) * 6, 230 - life * 230, 2.0f + rnd((unsigned)i + 9) * 3.0f, rgba(255, 255, 255, 80));
    }
    ui_rrect(14, 14, 372, 44, 10, rgba(0, 20, 40, 120));
    title_block(d, 26, 17, 348, rgb(0xFF, 0xFF, 0xFF), rgb(0xBF, 0xEA, 0xF5));
    ui_bar(26, 212, 348, 6, d->frac, rgba(0, 20, 40, 130), ui_accent);
}

/* ------------------------------------------------------------------- crt */

void deck_crt(const DeckInfo *d) {
    float t = clk(d);
    u32 fg = ui_pal.text, dim = ui_pal.dim;
    ui_rect(0, 0, 400, 240, ui_pal.bg);
    char s[96];
    ui_text("NARRATE TERMINAL v1.0", 16, 12, 0.46f, fg, UI_LEFT);
    ui_rect(16, 32, 368, 1, dim);
    ui_text("> LOAD", 16, 42, 0.46f, dim, UI_LEFT);
    ui_text_fit(d->book->title, 80, 42, 304, 0.46f, fg, UI_LEFT);
    snprintf(s, sizeof s, "> CH %02d/%02d", d->ch + 1, d->book->nchapters);
    ui_text(s, 16, 66, 0.46f, dim, UI_LEFT);
    ui_text_fit(d->book->chapters[d->ch].title ? d->book->chapters[d->ch].title : "", 130, 66, 254, 0.46f, fg, UI_LEFT);
    char a[24], b[24];
    int hrs = d->clen >= 3600000;
    ui_fmt_time(a, sizeof a, d->elapsed, hrs);
    ui_fmt_time(b, sizeof b, d->clen, hrs);
    snprintf(s, sizeof s, "> POS %s / %s", a, b);
    ui_text(s, 16, 90, 0.46f, fg, UI_LEFT);
    // text progress bar: [#######-------]  42%
    int cells = 26, on = (int)(d->frac * cells + 0.5f);
    char bar[40];
    for (int i = 0; i < cells; i++) bar[i] = i < on ? '#' : '-';
    bar[cells] = 0;
    snprintf(s, sizeof s, "> [%s] %d%%", bar, (int)(d->frac * 100.0f + 0.5f));
    ui_text(s, 16, 114, 0.46f, fg, UI_LEFT);
    snprintf(s, sizeof s, "> %s", d->playing ? "PLAYING" : "PAUSED");
    ui_text(s, 16, 138, 0.46f, dim, UI_LEFT);
    if (((int)(t * 2.0f) & 1) || !d->playing) ui_rect(16 + ui_text_w(s, 0.46f) + 6, 140, 9, 14, fg);   // blinking cursor
    // VU line made of characters stays cheap; a faint rolling band and scanlines complete the look
    float band = fmodf(t * 40.0f, 300.0f) - 30;
    ui_rect(0, band, 400, 22, with_alpha(fg, 10));
    for (int y = 0; y < 240; y += 4) ui_rect(0, (float)y, 400, 1.5f, rgba(0, 0, 0, 70));
    ui_rrect_outline(2, 2, 396, 236, 14, 3, ui_pal.line, rgba(0, 0, 0, 0));
}
