// decks.c
#include "decks.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cover.h"
#include "ui.h"

#define TAU 6.2831853f

/* Spin angle: advances with wall-clock time while playing, frozen otherwise. One call per drawn frame. */
static float g_spin;
static uint64_t g_spin_t;

static float spin_update(const DeckInfo *d, float rad_per_sec) {
    if (g_spin_t && d->playing && d->now > g_spin_t) {
        uint64_t dt = d->now - g_spin_t;
        if (dt > 250) dt = 250;                      // after a pause / long gap don't jump
        g_spin += (float)dt * 0.001f * rad_per_sec;
        if (g_spin > TAU * 64) g_spin = fmodf(g_spin, TAU);
    }
    g_spin_t = d->now;
    return g_spin;
}

static void spokes(float cx, float cy, float r, int n, float ang, float thick, u32 col) {
    for (int i = 0; i < n; i++) {
        float a = ang + (float)i * TAU / (float)n;
        ui_line(cx, cy, cx + cosf(a) * r, cy + sinf(a) * r, thick, col);
    }
}

static u32 rgb(unsigned r, unsigned g, unsigned b) { return C2D_Color32((u8)r, (u8)g, (u8)b, 255); }

/* --------------------------------------------------------------- cassette */

void deck_cassette(const DeckInfo *d) {
    float a = spin_update(d, 3.4f);
    const M4b *b = d->book;
    u32 paper = rgb(0xEE, 0xE4, 0xCF), ink = rgb(0x2A, 0x1F, 0x14), ink2 = rgb(0x6E, 0x5E, 0x46);
    u32 tape = rgb(0x4A, 0x36, 0x26), hub = rgb(0xE8, 0xE0, 0xD0);

    ui_rrect(24, 20, 352, 200, 16, ui_pal.surface2);              // shell
    ui_rrect(30, 26, 340, 188, 13, ui_pal.surface);
    for (int i = 0; i < 4; i++)                                     // screws
        ui_circle(i & 1 ? 360 : 40, i & 2 ? 204 : 36, 3.5f, ui_pal.dim);

    ui_rrect(46, 38, 308, 68, 6, paper);                            // paper label
    ui_rect(46, 38, 308, 9, ui_accent);
    ui_text_fit(b->title, 58, 52, 284, 0.6f, ink, UI_LEFT);
    if (b->artist[0]) ui_text_fit(b->artist, 58, 74, 200, 0.4f, ink2, UI_LEFT);
    char s[48];
    snprintf(s, sizeof s, "CH %d/%d", d->ch + 1, b->nchapters);
    ui_text(s, 342, 74, 0.4f, ink2, UI_RIGHT);
    ui_text_fit(b->chapters[d->ch].title, 58, 89, 284, 0.38f, ink2, UI_LEFT);

    ui_rrect(84, 114, 232, 68, 12, ui_pal.bg);                      // tape window
    const float cy = 148, lc = 142, rc = 258;
    float rl = 15 + 17 * (1 - d->frac), rr = 15 + 17 * d->frac;     // tape moves from the left reel to the right
    ui_circle(lc, cy, rl, tape);
    ui_circle(rc, cy, rr, tape);
    ui_line(lc, cy + rl, rc, cy + rr, 3, tape);
    for (int k = 0; k < 2; k++) {
        float c = k ? rc : lc;
        ui_circle(c, cy, 13, hub);
        spokes(c, cy, 10.5f, 6, a + (float)k * 0.5f, 3, ui_pal.bg);
        ui_circle(c, cy, 4, ink);
    }
    ui_rrect(108, 190, 184, 18, 6, ui_pal.surface2);                // bottom plate
    ui_circle(132, 199, 5, ui_pal.bg);
    ui_circle(268, 199, 5, ui_pal.bg);
}

/* --------------------------------------------------------- reel-to-reel */

void deck_reel(const DeckInfo *d) {
    float a = spin_update(d, 2.6f);
    const M4b *b = d->book;
    u32 tape = rgb(0x5A, 0x42, 0x2E), metal = theme_mix(ui_pal.text, ui_pal.surface2, 0.45f);

    ui_rrect_outline(14, 14, 372, 212, ui_style->r_card + 6, 2, ui_pal.line, ui_pal.surface);
    const float cy = 106, lc = 108, rc = 292;
    float rl = 34 + 40 * (1 - d->frac), rr = 34 + 40 * d->frac;

    // tape path first (behind the reels): from the left pack, round two rollers past the head, to the right pack
    ui_line(lc, cy + rl, 166, 198, 2.6f, tape);
    ui_line(166, 198, 234, 198, 2.6f, tape);
    ui_line(234, 198, rc, cy + rr, 2.6f, tape);

    for (int k = 0; k < 2; k++) {
        float c = k ? rc : lc, r = k ? rr : rl;
        ui_circle(c, cy, 80, ui_pal.line);                          // flange
        ui_circle(c, cy, 77, ui_pal.bg);
        ui_circle(c, cy, r, tape);
        ui_circle(c, cy, 30, metal);
        for (int i = 0; i < 3; i++) {                               // three cut-outs, rotating
            float ang = a * (k ? 1.0f : 1.0f) + (float)i * TAU / 3.0f + (float)k;
            ui_circle(c + cosf(ang) * 17, cy + sinf(ang) * 17, 9, ui_pal.bg);
        }
        ui_circle(c, cy, 5, ui_pal.surface2);
    }
    ui_circle(166, 198, 6, metal);
    ui_circle(234, 198, 6, metal);
    ui_rrect(190, 190, 20, 16, 3, ui_accent);                       // head block
    ui_circle(200, 198, 2.5f, ui_pal.on_accent);

    char s[64];
    snprintf(s, sizeof s, "CH %d/%d", d->ch + 1, b->nchapters);
    ui_text(s, 26, 20, 0.4f, ui_accent, UI_LEFT);
    ui_text_fit(b->title, 200, 208, 330, 0.5f, UI_TEXT, UI_CENTER);
}

/* ------------------------------------------------------------------ vinyl */

void deck_vinyl(const DeckInfo *d) {
    float a = spin_update(d, 3.5f);
    const M4b *b = d->book;
    const float cx = 116, cy = 124, R = 92;
    u32 dark = rgb(0x0C, 0x0C, 0x0E), groove = rgb(0x15, 0x15, 0x19);

    ui_circle(cx, cy, R + 6, ui_pal.surface2);                      // platter
    ui_circle(cx, cy, R, dark);
    for (int i = 0; i < 5; i++) ui_circle(cx, cy, R - 8 - 9 * i, (i & 1) ? dark : groove);   // grooves
    for (int k = 0; k < 2; k++) {                                   // light sheen, rotates with the record
        float ang = a + (float)k * 3.14159265f;
        C2D_DrawTriangle(cx, cy, C2D_Color32(255, 255, 255, 20),
                         cx + cosf(ang) * R, cy + sinf(ang) * R, C2D_Color32(255, 255, 255, 20),
                         cx + cosf(ang + 0.45f) * R, cy + sinf(ang + 0.45f) * R, C2D_Color32(255, 255, 255, 0), 0.5f);
    }
    ui_circle(cx, cy, 34, ui_accent);                               // label
    if (cover_ready()) cover_draw_centered(cx, cy, 48, a);          // cover spins with the record (inscribed square)
    ui_circle(cx, cy, 3.5f, ui_pal.bg);

    // tonearm: sweeps from the outer groove to the inner one as the book progresses
    float r = (R - 6) - d->frac * ((R - 6) - 42);
    float tx = cx + cosf(-0.62f) * r + (d->playing ? 0 : 9), ty = cy + sinf(-0.62f) * r - (d->playing ? 0 : 6);
    const float px = 238, py = 38;
    ui_circle(px, py, 15, ui_pal.surface2);
    ui_circle(px, py, 8, ui_pal.dim);
    ui_line(px, py, tx, ty, 4, rgb(0xB8, 0xB8, 0xC0));
    ui_circle(tx, ty, 4.5f, ui_accent);

    const float x0 = 258, w = 132;
    char s[64];
    snprintf(s, sizeof s, "CH %d / %d", d->ch + 1, b->nchapters);
    ui_text(s, x0, 62, 0.36f, ui_accent, UI_LEFT);
    int n = ui_text_wrap(b->title, x0, 78, w, 0.55f, UI_TEXT, 3, 18);
    float y = 78 + n * 18 + 4;
    if (b->artist[0]) ui_text_fit(b->artist, x0, y, w, 0.4f, UI_DIM, UI_LEFT);
    ui_text_fit(b->chapters[d->ch].title, x0, y + 20, w, 0.42f, UI_TEXT, UI_LEFT);
    ui_progress(x0, 206, w, d->frac);
}

/* -------------------------------------------------------------- quest log */

void deck_quest(const DeckInfo *d) {
    const M4b *b = d->book;
    ui_text("QUEST LOG", 20, 12, 0.5f, ui_accent, UI_LEFT);
    ui_text_fit(b->title, 20, 30, 360, 0.62f, UI_TEXT, UI_LEFT);
    ui_rect(20, 56, 360, 2, ui_pal.line);

    int start = d->ch - 2;
    if (start > b->nchapters - 6) start = b->nchapters - 6;
    if (start < 0) start = 0;
    for (int r = 0; r < 6 && start + r < b->nchapters; r++) {
        int i = start + r;
        float y = 64 + r * 22;
        bool cur = (i == d->ch), done = (i < d->ch);
        u32 fg = cur ? ui_pal.on_accent : (done ? ui_pal.dim : ui_pal.text);
        if (cur) ui_rect(16, y - 1, 368, 21, ui_accent);
        if (done) ui_icon_check(32, y + 10, 5, fg);
        else if (cur) C2D_DrawTriangle(28, y + 4, fg, 28, y + 16, fg, 37, y + 10, fg, 0.5f);
        else ui_rrect_outline(28, y + 4, 11, 11, 0, 1.6f, fg, ui_pal.bg);
        ui_text_fit(b->chapters[i].title, 48, y + 3, 262, 0.44f, fg, UI_LEFT);
        if (cur) {
            char s[24];
            snprintf(s, sizeof s, "%d%%", d->clen ? (int)(d->elapsed * 100 / d->clen) : 0);
            ui_text(s, 374, y + 3, 0.44f, fg, UI_RIGHT);
        } else if (done) {
            ui_text("DONE", 374, y + 4, 0.36f, ui_pal.dim, UI_RIGHT);
        }
    }
    ui_text("XP", 20, 203, 0.4f, UI_TEXT, UI_LEFT);
    ui_progress(46, 208, 250, d->frac);
    char s[40];
    snprintf(s, sizeof s, "LV %d/%d", d->ch + 1, b->nchapters);
    ui_text(s, 380, 203, 0.4f, ui_accent, UI_RIGHT);
}
