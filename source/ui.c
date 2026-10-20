// ui.c
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

C3D_RenderTarget *ui_top, *ui_bot;

static C2D_TextBuf g_buf, g_tmp;

#define Z 0.5f

/* --------------------------------------------------------------- lifecycle */

void ui_init(void) {
    ui_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    ui_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_buf = C2D_TextBufNew(8192);
    g_tmp = C2D_TextBufNew(1024);
    theme_apply();
}

void ui_exit(void) {
    C2D_TextBufDelete(g_buf);
    C2D_TextBufDelete(g_tmp);
}

void ui_begin(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(g_buf);
}

void ui_end(void) { C3D_FrameEnd(0); }

void ui_scene(C3D_RenderTarget *t) {
    C2D_TargetClear(t, ui_pal.bg);
    C2D_SceneBegin(t);
    if (ui_style->gradient) {
        float w = (t == ui_top) ? UI_TOP_W : UI_BOT_W;
        C2D_DrawRectangle(0, 0, Z, w, UI_SCR_H, ui_pal.bg, ui_pal.bg, ui_pal.bg2, ui_pal.bg2);
    }
}

void ui_glow(float w, float h) {
    if (!ui_style->glow) return;
    u32 a = C2D_Color32(ui_ar, ui_ag, ui_ab, g_set.dark ? 70 : 45);
    u32 z = C2D_Color32(ui_ar, ui_ag, ui_ab, 0);
    C2D_DrawRectangle(0, 0, Z, w, h, a, a, z, z);
}

void ui_set_accent(u8 r, u8 g, u8 b) { theme_set_auto_accent(r, g, b); }

void ui_set_accent_hue(float hue) {
    float h = fmodf(hue, 360.0f) / 60.0f, s = 0.55f, v = 0.97f;
    int i = (int)h;
    float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r, g, b;
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    ui_set_accent((u8)(r * 255), (u8)(g * 255), (u8)(b * 255));
}

/* ------------------------------------------------------------------ shapes */

void ui_rect(float x, float y, float w, float h, u32 col) { C2D_DrawRectSolid(x, y, Z, w, h, col); }
void ui_circle(float cx, float cy, float r, u32 col) { C2D_DrawCircleSolid(cx, cy, Z, r, col); }
void ui_line(float x0, float y0, float x1, float y1, float t, u32 col) { C2D_DrawLine(x0, y0, col, x1, y1, col, t, Z); }

// Opaque colours only (corner circles overlap the body rects).
void ui_rrect(float x, float y, float w, float h, float r, u32 col) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r < 1.0f) { ui_rect(x, y, w, h, col); return; }
    ui_rect(x + r, y, w - 2 * r, h, col);
    ui_rect(x, y + r, r, h - 2 * r, col);
    ui_rect(x + w - r, y + r, r, h - 2 * r, col);
    ui_circle(x + r, y + r, r, col);
    ui_circle(x + w - r, y + r, r, col);
    ui_circle(x + r, y + h - r, r, col);
    ui_circle(x + w - r, y + h - r, r, col);
}

void ui_rrect_outline(float x, float y, float w, float h, float r, float t, u32 col, u32 fill) {
    ui_rrect(x, y, w, h, r, col);
    ui_rrect(x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 0, fill);
}

void ui_bar(float x, float y, float w, float h, float frac, u32 bg, u32 fg) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    ui_rrect(x, y, w, h, h / 2, bg);
    if (frac > 0.001f) {
        float fw = w * frac;
        if (fw < h) fw = h;
        ui_rrect(x, y, fw, h, h / 2, fg);
    }
}

/* -------------------------------------------------------------------- text */

static float measure(const char *s, float scale) {
    C2D_Text t;
    C2D_TextBufClear(g_tmp);
    C2D_TextParse(&t, g_tmp, s);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

float ui_text_w(const char *s, float scale) { return measure(s, scale); }

void ui_text(const char *s, float x, float y, float scale, u32 col, int align) {
    if (!s || !*s) return;
    char tmp[400];
    snprintf(tmp, sizeof tmp, "%s", s);
    C2D_Text t;
    C2D_TextParse(&t, g_buf, tmp);
    C2D_TextOptimize(&t);
    u32 flags = C2D_WithColor | (align == UI_CENTER ? C2D_AlignCenter : align == UI_RIGHT ? C2D_AlignRight : C2D_AlignLeft);
    C2D_DrawText(&t, flags, x, y, Z, scale, scale, col);
}

static int utf8_floor(const char *s, int n) {
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return n;
}

// Measuring text (C2D_TextParse) is the most expensive thing a frame does on the old 3DS/2DS, and
// the same titles are measured every frame - so results are cached by (text, scale, width).
static uint32_t hash_str(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

typedef struct { uint32_t h; uint16_t len; float scale, maxw; bool used; char out[420]; } FitEntry;
#define FIT_CACHE 48
static FitEntry g_fit[FIT_CACHE];
static int g_fit_next;

static void fit_string(const char *s, float scale, float maxw, char *out, size_t outn) {
    char tmp[400];
    snprintf(tmp, sizeof tmp, "%s", s);
    uint32_t h = hash_str(tmp);
    uint16_t len = (uint16_t)strlen(tmp);
    for (int i = 0; i < FIT_CACHE; i++) {
        FitEntry *e = &g_fit[i];
        if (e->used && e->h == h && e->len == len && e->scale == scale && e->maxw == maxw) {
            snprintf(out, outn, "%s", e->out);
            return;
        }
    }
    char res[420];
    if (measure(tmp, scale) <= maxw) {
        snprintf(res, sizeof res, "%s", tmp);
    } else {
        int lo = 0, hi = (int)strlen(tmp);
        char cand[420];
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            int cut = utf8_floor(tmp, mid);
            snprintf(cand, sizeof cand, "%.*s...", cut, tmp);
            if (measure(cand, scale) <= maxw) lo = mid; else hi = mid - 1;
        }
        snprintf(res, sizeof res, "%.*s...", utf8_floor(tmp, lo), tmp);
    }
    FitEntry *e = &g_fit[g_fit_next];
    g_fit_next = (g_fit_next + 1) % FIT_CACHE;
    e->used = true; e->h = h; e->len = len; e->scale = scale; e->maxw = maxw;
    snprintf(e->out, sizeof e->out, "%s", res);
    snprintf(out, outn, "%s", res);
}

void ui_text_fit(const char *s, float x, float y, float maxw, float scale, u32 col, int align) {
    if (!s || !*s) return;
    char o[420];
    fit_string(s, scale, maxw, o, sizeof o);
    ui_text(o, x, y, scale, col, align);
}

typedef struct { uint32_t h; uint16_t len; float scale, maxw; int maxlines, n; bool used; char lines[3][420]; } WrapEntry;
#define WRAP_CACHE 16
static WrapEntry g_wrap[WRAP_CACHE];
static int g_wrap_next;

static int wrap_compute(const char *s, float scale, float maxw, int maxlines, char lines[][420]) {
    char line[420], cand[420];
    line[0] = 0;
    int n = 0;
    const char *p = s;
    while (*p == ' ') p++;
    while (*p) {
        const char *e = p;
        while (*e && *e != ' ') e++;
        int wl = (int)(e - p);
        if (wl > 200) wl = 200;
        if (line[0]) snprintf(cand, sizeof cand, "%.200s %.*s", line, wl, p);
        else snprintf(cand, sizeof cand, "%.*s", wl, p);

        if (line[0] && measure(cand, scale) > maxw) {
            if (n + 1 >= maxlines) {                      // last allowed line: take the rest, ellipsise
                snprintf(cand, sizeof cand, "%s %s", line, p);
                fit_string(cand, scale, maxw, lines[n], 420);
                return n + 1;
            }
            fit_string(line, scale, maxw, lines[n], 420);
            n++;
            snprintf(line, sizeof line, "%.*s", wl, p);
        } else {
            snprintf(line, sizeof line, "%s", cand);
        }
        p = e;
        while (*p == ' ') p++;
    }
    if (line[0]) {
        fit_string(line, scale, maxw, lines[n], 420);
        n++;
    }
    return n;
}

int ui_text_wrap(const char *s, float x, float y, float maxw, float scale, u32 col, int maxlines, float line_h) {
    if (!s || !*s || maxlines < 1) return 0;
    if (maxlines > 3) maxlines = 3;
    char key[400];
    snprintf(key, sizeof key, "%s", s);
    uint32_t h = hash_str(key);
    uint16_t len = (uint16_t)strlen(key);
    WrapEntry *hit = NULL;
    for (int i = 0; i < WRAP_CACHE; i++) {
        WrapEntry *e = &g_wrap[i];
        if (e->used && e->h == h && e->len == len && e->scale == scale && e->maxw == maxw && e->maxlines == maxlines) { hit = e; break; }
    }
    if (!hit) {
        hit = &g_wrap[g_wrap_next];
        g_wrap_next = (g_wrap_next + 1) % WRAP_CACHE;
        hit->n = wrap_compute(key, scale, maxw, maxlines, hit->lines);
        hit->used = true; hit->h = h; hit->len = len; hit->scale = scale; hit->maxw = maxw; hit->maxlines = maxlines;
    }
    for (int i = 0; i < hit->n; i++) ui_text(hit->lines[i], x, y + i * line_h, scale, col, UI_LEFT);
    return hit->n;
}

/* ---------------------------------------------------------- themed widgets */

static float pill_r(float h) { return ui_style->r_pill < 0 ? h / 2 : ui_style->r_pill; }

static u32 outline_col(void) {
    if (ui_style->glow) return ui_accent;                 // neon
    if (ui_style->bar == BAR_SEGMENTS) return ui_accent_dim;   // retro
    return ui_pal.line;                                   // minimal
}

void ui_card(float x, float y, float w, float h) {
    if (ui_style->outline) ui_rrect_outline(x, y, w, h, ui_style->r_card, 1.6f, outline_col(), ui_pal.bg);
    else ui_rrect(x, y, w, h, ui_style->r_card, ui_pal.surface);
}

void ui_row(float x, float y, float w, float h, int sel, int cur) {
    switch (ui_style->row) {
    case ROW_CARD:
        if (sel) { ui_rrect(x, y + 1, w, h - 2, 10, ui_pal.surface2); ui_rrect(x, y + 8, 3, h - 16, 1.5f, ui_accent); }
        else if (cur) ui_rrect(x, y + 1, w, h - 2, 10, ui_pal.surface);
        break;
    case ROW_FLAT:
        if (sel) ui_rect(x, y, w, h, ui_pal.surface2);
        ui_rect(x + 10, y + h - 1, w - 10, 1, ui_pal.line);
        break;
    case ROW_PILL:
        if (sel) ui_rrect(x, y + 2, w, h - 4, (h - 4) / 2, ui_accent_soft);
        else if (cur) ui_rrect(x, y + 2, 4, h - 4, 2, ui_accent);
        break;
    case ROW_INVERT:
        if (sel) ui_rect(x, y + 1, w, h - 2, ui_accent);
        break;
    case ROW_OUTLINE:
        if (sel) ui_rrect_outline(x, y + 1, w, h - 2, 8, 1.5f, ui_accent, ui_pal.bg);
        break;
    }
}

u32 ui_row_fg(int sel, int cur) {
    if (ui_style->row == ROW_INVERT && sel) return ui_pal.on_accent;
    return cur ? ui_accent : ui_pal.text;
}

u32 ui_row_dim(int sel) {
    return (ui_style->row == ROW_INVERT && sel) ? ui_pal.on_accent : ui_pal.dim;
}

u32 ui_row_bg(int sel) {
    if (!sel) return ui_pal.bg;
    switch (ui_style->row) {
    case ROW_CARD: case ROW_FLAT: return ui_pal.surface2;
    case ROW_PILL: return ui_accent_soft;
    case ROW_INVERT: return ui_accent;
    default: return ui_pal.bg;
    }
}

void ui_pill(float x, float y, float w, float h, int active) {
    float r = pill_r(h);
    if (ui_style->outline) ui_rrect_outline(x, y, w, h, r, 1.5f, active ? ui_accent : outline_col(), ui_pal.bg);
    else ui_rrect(x, y, w, h, r, active ? ui_accent_soft : ui_pal.surface);
}

u32 ui_pill_bg(int active) {
    if (ui_style->outline) return ui_pal.bg;
    return active ? ui_accent_soft : ui_pal.surface;
}

void ui_progress(float x, float y, float w, float frac) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float h = ui_style->bar_h;
    switch (ui_style->bar) {
    case BAR_SEGMENTS: {
        int n = (int)(w / 7);
        if (n < 1) n = 1;
        float seg = w / n;
        int filled = (int)(frac * n + 0.5f);
        for (int i = 0; i < n; i++) ui_rect(x + i * seg, y, seg - 2, h, i < filled ? ui_accent : ui_pal.line);
        break;
    }
    case BAR_LINE:
        ui_rect(x, y, w, h, ui_pal.line);
        if (frac > 0) ui_rect(x, y, w * frac, h, ui_accent);
        break;
    default:
        ui_bar(x, y, w, h, frac, ui_pal.surface2, ui_accent);
        break;
    }
}

void ui_seekbar(float x, float y, float w, float frac, int dragging) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    ui_progress(x, y, w, frac);
    float cy = y + ui_style->bar_h / 2, kx = x + frac * w;
    switch (ui_style->knob) {
    case KNOB_CIRCLE:
        ui_circle(kx, cy, dragging ? 10 : 7, ui_pal.text);
        ui_circle(kx, cy, dragging ? 5 : 3, ui_accent);
        break;
    case KNOB_PILL:
        ui_rrect(kx - 3, cy - (dragging ? 15 : 12), 6, dragging ? 30 : 24, 3, ui_accent);
        break;
    case KNOB_GLOW:
        ui_circle(kx, cy, dragging ? 15 : 12, C2D_Color32(ui_ar, ui_ag, ui_ab, 60));
        ui_circle(kx, cy, dragging ? 7 : 5, ui_accent);
        break;
    default:
        break;
    }
}

void ui_play_button(float cx, float cy, int playing, float s) {
    switch (ui_style->play) {
    case PLAY_BARE:
        if (playing) ui_icon_pause(cx, cy, 16 * s, ui_pal.text); else ui_icon_play(cx, cy, 18 * s, ui_pal.text);
        break;
    case PLAY_SQUIRCLE:
        ui_rrect(cx - 34 * s, cy - 34 * s, 68 * s, 68 * s, 24 * s, ui_accent);
        if (playing) ui_icon_pause(cx, cy, 12 * s, ui_pal.on_accent); else ui_icon_play(cx, cy, 13 * s, ui_pal.on_accent);
        break;
    case PLAY_SQUARE:
        ui_rrect_outline(cx - 32 * s, cy - 32 * s, 64 * s, 64 * s, 0, 2.6f, ui_accent, ui_pal.bg);
        if (playing) ui_icon_pause(cx, cy, 12 * s, ui_accent); else ui_icon_play(cx, cy, 13 * s, ui_accent);
        break;
    case PLAY_OUTLINE_CIRCLE:
        ui_circle(cx, cy, 40 * s, C2D_Color32(ui_ar, ui_ag, ui_ab, 40));
        ui_circle(cx, cy, 34 * s, ui_accent);
        ui_circle(cx, cy, 31.5f * s, ui_pal.bg);
        if (playing) ui_icon_pause(cx, cy, 12 * s, ui_accent); else ui_icon_play(cx, cy, 13 * s, ui_accent);
        break;
    default:
        ui_circle(cx, cy, 35 * s, ui_accent_dim);
        ui_circle(cx, cy, 33 * s, ui_accent);
        if (playing) ui_icon_pause(cx, cy, 12 * s, ui_pal.on_accent); else ui_icon_play(cx, cy, 13 * s, ui_pal.on_accent);
        break;
    }
}

void ui_round_btn(float cx, float cy, float r) {
    if (ui_style->btn == BTN_FILLED) ui_circle(cx, cy, r, ui_pal.surface2);
    else if (ui_style->btn == BTN_OUTLINE) { ui_circle(cx, cy, r, outline_col()); ui_circle(cx, cy, r - 1.6f, ui_pal.bg); }
}

void ui_tab_container(float x, float y, float w, float h) {
    if (ui_style->tab == TAB_SEG) ui_rrect(x, y, w, h, 9, ui_pal.surface2);
}

void ui_tab(float x, float y, float w, float h, const char *label, int active) {
    float ty = y + (h - 13) / 2;
    switch (ui_style->tab) {
    case TAB_SEG:
        if (active) ui_rrect(x + 2, y + 2, w - 4, h - 4, 7, theme_mix(ui_pal.surface2, ui_pal.text, 0.22f));
        ui_text(label, x + w / 2, ty, 0.4f, active ? ui_pal.text : ui_pal.dim, UI_CENTER);
        break;
    case TAB_UNDERLINE:
        ui_text(label, x + w / 2, ty, 0.4f, active ? ui_accent : ui_pal.dim, UI_CENTER);
        if (active) ui_rrect(x + 14, y + h - 3, w - 28, 3, 1.5f, ui_accent);
        break;
    case TAB_BRACKET: {
        char b[48];
        snprintf(b, sizeof b, active ? "[ %s ]" : "  %s  ", label);
        ui_text(b, x + w / 2, ty, 0.4f, active ? ui_accent : ui_pal.dim, UI_CENTER);
        break;
    }
    default:
        ui_pill(x, y, w, h, active);
        ui_text(label, x + w / 2, ty, 0.4f, active ? ui_accent : ui_pal.dim, UI_CENTER);
        break;
    }
}

void ui_back_button(float x, float y, float w, float h) {
    ui_pill(x, y, w, h, 0);
    float cy = y + h / 2;
    C2D_DrawTriangle(x + 13, cy, ui_pal.text, x + 19, cy - 6, ui_pal.text, x + 19, cy + 6, ui_pal.text, Z);
    ui_text("Back", x + 25, y + (h - 13) / 2, 0.42f, ui_pal.text, UI_LEFT);
}

void ui_logo(float x, float y, float s) {
    float r = ui_style->r_card * 1.2f;
    if (ui_style->outline) {
        ui_rrect_outline(x, y, s, s, r, 2.6f, ui_accent, ui_pal.bg);
        ui_icon_play(x + s / 2 + 1, y + s / 2, s * 0.25f, ui_accent);
    } else {
        ui_rrect(x, y, s, s, r, ui_accent);
        ui_icon_play(x + s / 2 + 1, y + s / 2, s * 0.25f, ui_pal.on_accent);
    }
}

void ui_cover_placeholder(float x, float y, float size, const char *title) {
    u32 a = ui_accent;
    u32 b = theme_mix(ui_accent, ui_pal.bg, 0.65f);
    C2D_DrawRectangle(x, y, Z, size, size, a, b, b, a);
    char ch[2] = {'#', 0};
    for (const char *p = title; p && *p; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) {
            ch[0] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
            break;
        }
    }
    ui_text(ch, x + size / 2, y + size / 2 - size * 0.30f, size / 60.0f, ui_pal.on_accent, UI_CENTER);
}

/* ------------------------------------------------------------------- icons */

#define ICON_T 2.4f

static void tri_outline(float x0, float y0, float x1, float y1, float x2, float y2, u32 col) {
    ui_line(x0, y0, x1, y1, ICON_T, col);
    ui_line(x1, y1, x2, y2, ICON_T, col);
    ui_line(x2, y2, x0, y0, ICON_T, col);
    ui_circle(x0, y0, ICON_T / 2, col);
    ui_circle(x1, y1, ICON_T / 2, col);
    ui_circle(x2, y2, ICON_T / 2, col);
}

static void rect_outline(float x, float y, float w, float h, u32 col) {
    ui_line(x, y, x + w, y, ICON_T, col);
    ui_line(x + w, y, x + w, y + h, ICON_T, col);
    ui_line(x + w, y + h, x, y + h, ICON_T, col);
    ui_line(x, y + h, x, y, ICON_T, col);
}

void ui_icon_play(float cx, float cy, float s, u32 col) {
    float lx = cx - s * 0.55f;
    if (ui_icons_outline) tri_outline(lx + 1, cy - s + 1, lx + 1, cy + s - 1, cx + s * 0.9f, cy, col);
    else C2D_DrawTriangle(lx, cy - s, col, lx, cy + s, col, cx + s * 0.95f, cy, col, Z);
}

void ui_icon_pause(float cx, float cy, float s, u32 col) {
    if (ui_icons_outline) {
        rect_outline(cx - s * 0.75f, cy - s, s * 0.55f, s * 2, col);
        rect_outline(cx + s * 0.20f, cy - s, s * 0.55f, s * 2, col);
    } else {
        ui_rrect(cx - s * 0.75f, cy - s, s * 0.55f, s * 2, 2, col);
        ui_rrect(cx + s * 0.20f, cy - s, s * 0.55f, s * 2, 2, col);
    }
}

void ui_icon_next(float cx, float cy, float s, u32 col) {
    if (ui_icons_outline) tri_outline(cx - s, cy - s, cx - s, cy + s, cx + s * 0.55f, cy, col);
    else C2D_DrawTriangle(cx - s, cy - s, col, cx - s, cy + s, col, cx + s * 0.55f, cy, col, Z);
    ui_rect(cx + s * 0.7f, cy - s, s * 0.35f, s * 2, col);
}

void ui_icon_prev(float cx, float cy, float s, u32 col) {
    ui_rect(cx - s * 1.05f, cy - s, s * 0.35f, s * 2, col);
    if (ui_icons_outline) tri_outline(cx + s, cy - s, cx + s, cy + s, cx - s * 0.55f, cy, col);
    else C2D_DrawTriangle(cx + s, cy - s, col, cx + s, cy + s, col, cx - s * 0.55f, cy, col, Z);
}

void ui_icon_skip(float cx, float cy, int forward, const char *label) {
    ui_round_btn(cx, cy, 22);
    ui_text(label, cx, cy - 15, 0.5f, ui_pal.text, UI_CENTER);
    float d = forward ? 1.0f : -1.0f;
    for (int i = 0; i < 2; i++) {
        float bx = cx + d * (-4 + i * 7) + (forward ? -3.0f : 3.0f);
        C2D_DrawTriangle(bx - d * 3.5f, cy + 4, ui_pal.text, bx - d * 3.5f, cy + 12, ui_pal.text, bx + d * 3.5f, cy + 8, ui_pal.text, Z);
    }
}

void ui_icon_list(float cx, float cy, float s, u32 col) {
    for (int i = -1; i <= 1; i++) ui_rrect(cx - s, cy + i * s * 0.7f - 1.2f, s * 2, 2.4f, 1.2f, col);
}

void ui_icon_moon(float cx, float cy, float r, u32 col, u32 bg) {
    ui_circle(cx, cy, r, col);
    ui_circle(cx + r * 0.55f, cy - r * 0.35f, r * 0.85f, bg);
}

void ui_icon_bookmark(float cx, float cy, u32 col, u32 bg) {
    if (ui_icons_outline) {
        float l = cx - 5, r = cx + 5, t = cy - 8, b = cy + 8;
        ui_line(l, t, r, t, ICON_T, col);
        ui_line(r, t, r, b, ICON_T, col);
        ui_line(r, b, cx, cy + 3, ICON_T, col);
        ui_line(cx, cy + 3, l, b, ICON_T, col);
        ui_line(l, b, l, t, ICON_T, col);
        return;
    }
    ui_rrect(cx - 5, cy - 8, 10, 16, 2, col);
    C2D_DrawTriangle(cx - 5.5f, cy + 8.5f, bg, cx + 5.5f, cy + 8.5f, bg, cx, cy + 2.5f, bg, Z);
}

void ui_icon_close(float cx, float cy, float s, u32 col) {
    ui_line(cx - s, cy - s, cx + s, cy + s, 2.2f, col);
    ui_line(cx - s, cy + s, cx + s, cy - s, 2.2f, col);
}

void ui_icon_screen(float cx, float cy, u32 col, u32 bg) {
    ui_rrect(cx - 9, cy - 7, 18, 13, 3, col);
    ui_rect(cx - 7, cy - 5, 14, 9, bg);
    ui_line(cx - 10, cy + 8, cx + 10, cy - 9, 2.2f, col);
}

void ui_icon_gear(float cx, float cy, u32 col, u32 bg) {
    ui_circle(cx, cy, 6.5f, col);
    for (int i = 0; i < 8; i++) {
        float a = (float)i * 0.785398f;
        ui_circle(cx + cosf(a) * 8.0f, cy + sinf(a) * 8.0f, 2.0f, col);
    }
    ui_circle(cx, cy, 3.0f, bg);
}

void ui_icon_folder(float cx, float cy, u32 col) {
    ui_rrect(cx - 9, cy - 7, 8, 5, 2, col);
    ui_rrect(cx - 9, cy - 4, 18, 12, 3, col);
}

void ui_icon_book(float cx, float cy, u32 col, u32 bg) {
    ui_rrect(cx - 7, cy - 9, 14, 18, 3, col);
    ui_rect(cx - 4, cy - 5, 8, 2, bg);
    ui_rect(cx - 4, cy - 1, 6, 2, bg);
}

void ui_fmt_time(char *buf, size_t n, uint64_t ms, int force_hours) {
    uint64_t s = ms / 1000;
    unsigned h = (unsigned)(s / 3600), m = (unsigned)((s / 60) % 60), sec = (unsigned)(s % 60);
    if (h || force_hours) snprintf(buf, n, "%u:%02u:%02u", h, m, sec);
    else snprintf(buf, n, "%u:%02u", m, sec);
}
