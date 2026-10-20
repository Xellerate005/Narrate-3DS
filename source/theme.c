// theme.c
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_FILE "sdmc:/3ds/narrate/settings.cfg"
#define RGB(h) C2D_Color32(((h) >> 16) & 255, ((h) >> 8) & 255, (h) & 255, 255)

Settings g_set = {0, 1, 0, 0, 1, 1, 0, 0};
const UiStyle *ui_style;
UiPal ui_pal;
u32 ui_accent, ui_accent_dim, ui_accent_soft;
u8  ui_ar, ui_ag, ui_ab;
bool ui_icons_outline;

const int SKIP_BACK_OPTS[3] = {10, 15, 30};
const int SKIP_FWD_OPTS[3]  = {15, 30, 60};

static u8 g_auto_r = 0x7C, g_auto_g = 0x9C, g_auto_b = 0xFF;

typedef struct { unsigned bg, surface, surface2, text, dim, line, accent; } Base;
typedef struct { UiStyle st; Base dark, light; } StyleDef;

//                name       r_card r_pill bar_h play               row          tab           bar           knob         btn          outline glow  grad  icons_outline
static const StyleDef STYLES[STYLE_COUNT] = {
    { {"Narrate",  14, 12, 5, PLAY_CIRCLE,         ROW_CARD,    TAB_PILL,      BAR_ROUND,    KNOB_CIRCLE, BTN_FILLED,  false, true,  false, false},
      {0x0B0D12, 0x161923, 0x232836, 0xF2F4F8, 0x8B93A7, 0x232836, 0x7C9CFF},
      {0xF3F4F8, 0xFFFFFF, 0xE4E7EF, 0x12141A, 0x6B7285, 0xE4E7EF, 0x4F6EF7} },
    { {"iOS",      12, 10, 4, PLAY_BARE,           ROW_FLAT,    TAB_SEG,       BAR_ROUND,    KNOB_CIRCLE, BTN_NONE,    false, false, false, false},
      {0x000000, 0x1C1C1E, 0x2C2C2E, 0xFFFFFF, 0x8E8E93, 0x38383A, 0x0A84FF},
      {0xF2F2F7, 0xFFFFFF, 0xE5E5EA, 0x000000, 0x8E8E93, 0xC6C6C8, 0x007AFF} },
    { {"Android",  24, -1, 8, PLAY_SQUIRCLE,       ROW_PILL,    TAB_UNDERLINE, BAR_ROUND,    KNOB_PILL,   BTN_FILLED,  false, false, false, false},
      {0x141218, 0x211F26, 0x2B2930, 0xE6E0E9, 0xCAC4D0, 0x49454F, 0xD0BCFF},
      {0xFEF7FF, 0xF3EDF7, 0xE6E0E9, 0x1D1B20, 0x49454F, 0xCAC4D0, 0x6750A4} },
    { {"Retro",     0,  0, 8, PLAY_SQUARE,         ROW_INVERT,  TAB_BRACKET,   BAR_SEGMENTS, KNOB_NONE,   BTN_OUTLINE, true,  false, false, false},
      {0x000000, 0x031203, 0x0A2A0A, 0x33FF33, 0x1F9A1F, 0x1F6A1F, 0x33FF33},
      {0x9BBC0F, 0x8BAC0F, 0x7A9B0D, 0x0F380F, 0x306230, 0x306230, 0x0F380F} },
    { {"Neon",     10, -1, 3, PLAY_OUTLINE_CIRCLE, ROW_OUTLINE, TAB_PILL,      BAR_LINE,     KNOB_GLOW,   BTN_OUTLINE, true,  true,  false, true},
      {0x05060F, 0x0B0E1F, 0x141A38, 0xE8F0FF, 0x6C7AA8, 0x1B2450, 0x00F0FF},
      {0xEEF0FF, 0xFFFFFF, 0xDDE1FF, 0x10122B, 0x5560A0, 0xB6BDF0, 0xD100A8} },
    { {"Minimal",   2,  2, 2, PLAY_BARE,           ROW_FLAT,    TAB_UNDERLINE, BAR_LINE,     KNOB_CIRCLE, BTN_NONE,    true,  false, false, true},
      {0x101010, 0x101010, 0x1C1C1C, 0xF0F0F0, 0x8A8A8A, 0x2E2E2E, 0xF0F0F0},
      {0xF6F4EE, 0xF6F4EE, 0xE9E6DD, 0x1A1A1A, 0x777777, 0xCFCBBF, 0x1A1A1A} },
    { {"Aurora",   18, 14, 6, PLAY_CIRCLE,         ROW_CARD,    TAB_PILL,      BAR_ROUND,    KNOB_CIRCLE, BTN_FILLED,  false, false, true,  false},
      {0x0B0D12, 0x161923, 0x232836, 0xF4F6FF, 0xA9B0CC, 0x3A3F5C, 0x8B7CFF},
      {0xF3F4F8, 0xFFFFFF, 0xE4E7EF, 0x1B1F33, 0x5A6285, 0xC9CEE6, 0x6C5CE7} },
};

static const char *ACCENT_NAMES[ACCENT_COUNT] = {
    "Auto (cover)", "Style default", "Blue", "Teal", "Green", "Lime", "Amber", "Orange", "Red", "Pink", "Purple", "Mono"};
static const unsigned PRESETS[9] = {0x3D8BFF, 0x2DD4BF, 0x4ADE80, 0xA3E635, 0xFBBF24, 0xFB923C, 0xF87171, 0xF472B6, 0xA78BFA};

const char *theme_style_name(int i) { return STYLES[(i >= 0 && i < STYLE_COUNT) ? i : 0].st.name; }
const char *theme_accent_name(int i) { return ACCENT_NAMES[(i >= 0 && i < ACCENT_COUNT) ? i : 0]; }

u32 theme_mix(u32 a, u32 b, float t) {
    int ar = a & 255, ag = (a >> 8) & 255, ab = (a >> 16) & 255;
    int br = b & 255, bg = (b >> 8) & 255, bb = (b >> 16) & 255;
    return C2D_Color32((u8)(ar + (br - ar) * t), (u8)(ag + (bg - ag) * t), (u8)(ab + (bb - ab) * t), 255);
}

static float lum_of(u32 c) {
    return (0.299f * (c & 255) + 0.587f * ((c >> 8) & 255) + 0.114f * ((c >> 16) & 255)) / 255.0f;
}

static void resolve_accent(int idx, int dark, int style, u8 *r, u8 *g, u8 *b) {
    const StyleDef *sd = &STYLES[style];
    const Base *bp = dark ? &sd->dark : &sd->light;
    unsigned h;
    if (idx == 0) { *r = g_auto_r; *g = g_auto_g; *b = g_auto_b; return; }
    if (idx == 1) h = bp->accent;
    else if (idx == 11) h = bp->text;
    else {
        h = PRESETS[idx - 2];
        if (!dark) {                       // pastel presets need to be deeper on light backgrounds
            *r = (u8)(((h >> 16) & 255) * 0.78f);
            *g = (u8)(((h >> 8) & 255) * 0.78f);
            *b = (u8)((h & 255) * 0.78f);
            return;
        }
    }
    *r = (h >> 16) & 255; *g = (h >> 8) & 255; *b = h & 255;
}

u32 theme_accent_swatch(int i) {
    u8 r, g, b;
    resolve_accent(i, g_set.dark, g_set.style, &r, &g, &b);
    return C2D_Color32(r, g, b, 255);
}

void theme_set_auto_accent(u8 r, u8 g, u8 b) {
    g_auto_r = r; g_auto_g = g; g_auto_b = b;
    theme_apply();
}

void theme_apply(void) {
    if (g_set.style < 0 || g_set.style >= STYLE_COUNT) g_set.style = 0;
    if (g_set.accent < 0 || g_set.accent >= ACCENT_COUNT) g_set.accent = 0;
    if (g_set.icons < 0 || g_set.icons > 2) g_set.icons = 0;
    if (g_set.skip_back < 0 || g_set.skip_back > 2) g_set.skip_back = 1;
    if (g_set.skip_fwd < 0 || g_set.skip_fwd > 2) g_set.skip_fwd = 1;
    if (g_set.cover_flip < 0 || g_set.cover_flip > 3) g_set.cover_flip = 0;
    if (g_set.cover_rot < 0 || g_set.cover_rot > 3) g_set.cover_rot = 0;

    const StyleDef *sd = &STYLES[g_set.style];
    ui_style = &sd->st;
    const Base *bp = g_set.dark ? &sd->dark : &sd->light;

    u8 r, g, b;
    resolve_accent(g_set.accent, g_set.dark, g_set.style, &r, &g, &b);
    ui_ar = r; ui_ag = g; ui_ab = b;
    ui_accent = C2D_Color32(r, g, b, 255);

    ui_pal.bg = RGB(bp->bg);
    ui_pal.bg2 = ui_pal.bg;
    ui_pal.surface = RGB(bp->surface);
    ui_pal.surface2 = RGB(bp->surface2);
    ui_pal.text = RGB(bp->text);
    ui_pal.dim = RGB(bp->dim);
    ui_pal.line = RGB(bp->line);
    ui_pal.error = g_set.dark ? RGB(0xFF6B6B) : RGB(0xD92D2D);

    if (ui_style->gradient) {                   // Aurora: accent-tinted gradient, frosted cards
        u32 white = RGB(0xFFFFFF);
        if (g_set.dark) {
            ui_pal.bg = theme_mix(RGB(0x0B0D12), ui_accent, 0.40f);
            ui_pal.bg2 = theme_mix(RGB(0x07080C), ui_accent, 0.07f);
        } else {
            ui_pal.bg = theme_mix(white, ui_accent, 0.28f);
            ui_pal.bg2 = theme_mix(white, ui_accent, 0.06f);
        }
        u32 mid = theme_mix(ui_pal.bg, ui_pal.bg2, 0.5f);
        if (g_set.dark) {
            ui_pal.surface = theme_mix(mid, white, 0.09f);
            ui_pal.surface2 = theme_mix(mid, white, 0.17f);
        } else {
            ui_pal.surface = theme_mix(mid, white, 0.65f);
            ui_pal.surface2 = theme_mix(mid, white, 0.40f);
        }
        ui_pal.line = theme_mix(mid, ui_pal.text, 0.20f);
    }

    float lum = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
    if (ui_style->gradient) {
        ui_pal.on_accent = lum > 0.55f ? RGB(0x0B0D12) : RGB(0xFFFFFF);
    } else {
        // use whichever of the style's own background / text colours contrasts most with the accent
        float lb = lum_of(ui_pal.bg), lt = lum_of(ui_pal.text);
        ui_pal.on_accent = fabsf(lum - lb) >= fabsf(lum - lt) ? ui_pal.bg : ui_pal.text;
    }

    ui_accent_dim = theme_mix(ui_accent, ui_pal.bg, 0.55f);
    ui_accent_soft = theme_mix(ui_pal.surface, ui_accent, g_set.dark ? 0.22f : 0.18f);
    ui_icons_outline = (g_set.icons == 0) ? ui_style->icons_outline : (g_set.icons == 2);
}

void theme_load(void) {
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (f) {
        char line[96];
        while (fgets(line, sizeof line, f)) {
            int v;
            if (sscanf(line, "style=%d", &v) == 1) g_set.style = v;
            else if (sscanf(line, "dark=%d", &v) == 1) g_set.dark = v ? 1 : 0;
            else if (sscanf(line, "accent=%d", &v) == 1) g_set.accent = v;
            else if (sscanf(line, "icons=%d", &v) == 1) g_set.icons = v;
            else if (sscanf(line, "skip_back=%d", &v) == 1) g_set.skip_back = v;
            else if (sscanf(line, "skip_fwd=%d", &v) == 1) g_set.skip_fwd = v;
            else if (sscanf(line, "cover=%d", &v) == 1) g_set.cover_flip = v;          // old single setting
            else if (sscanf(line, "cover_flip=%d", &v) == 1) g_set.cover_flip = v;
            else if (sscanf(line, "cover_rot=%d", &v) == 1) g_set.cover_rot = v;
        }
        fclose(f);
    }
    theme_apply();
}

void theme_save(void) {
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) return;
    fprintf(f, "style=%d\ndark=%d\naccent=%d\nicons=%d\nskip_back=%d\nskip_fwd=%d\ncover_flip=%d\ncover_rot=%d\n",
            g_set.style, g_set.dark, g_set.accent, g_set.icons, g_set.skip_back, g_set.skip_fwd, g_set.cover_flip, g_set.cover_rot);
    fclose(f);
}
