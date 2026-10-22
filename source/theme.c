// theme.c
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_FILE "sdmc:/3ds/narrate/settings.cfg"
#define RGB(h) C2D_Color32(((h) >> 16) & 255, ((h) >> 8) & 255, (h) & 255, 255)

//                        style dark acc icons sb sf cflip crot layout view anim auto lid sort bright
Settings g_set = {0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, -1};
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

static const StyleDef STYLES[STYLE_COUNT] = {
  /* 0 */ { .st = {.name = "Narrate", .r_card = 14, .r_pill = 12, .bar_h = 5, .play_r = 24, .play = PLAY_CIRCLE, .row = ROW_CARD, .tab = TAB_PILL, .bar = BAR_ROUND, .knob = KNOB_CIRCLE, .btn = BTN_FILLED, .glow = true, .def_layout = LAY_CLASSIC},
            .dark = {0x0B0D12, 0x161923, 0x232836, 0xF2F4F8, 0x8B93A7, 0x232836, 0x7C9CFF},
            .light = {0xF3F4F8, 0xFFFFFF, 0xE4E7EF, 0x12141A, 0x6B7285, 0xE4E7EF, 0x4F6EF7} },
  /* 1 */ { .st = {.name = "iOS", .r_card = 12, .r_pill = 10, .bar_h = 4, .play_r = 24, .play = PLAY_BARE, .row = ROW_FLAT, .tab = TAB_SEG, .bar = BAR_ROUND, .knob = KNOB_CIRCLE, .btn = BTN_NONE, .def_layout = LAY_CLASSIC},
            .dark = {0x000000, 0x1C1C1E, 0x2C2C2E, 0xFFFFFF, 0x8E8E93, 0x38383A, 0x0A84FF},
            .light = {0xF2F2F7, 0xFFFFFF, 0xE5E5EA, 0x000000, 0x8E8E93, 0xC6C6C8, 0x007AFF} },
  /* 2 */ { .st = {.name = "Android", .r_card = 24, .r_pill = -1, .bar_h = 8, .play_r = 24, .play = PLAY_SQUIRCLE, .row = ROW_PILL, .tab = TAB_UNDERLINE, .bar = BAR_ROUND, .knob = KNOB_PILL, .btn = BTN_FILLED, .def_layout = LAY_CLASSIC},
            .dark = {0x141218, 0x211F26, 0x2B2930, 0xE6E0E9, 0xCAC4D0, 0x49454F, 0xD0BCFF},
            .light = {0xFEF7FF, 0xF3EDF7, 0xE6E0E9, 0x1D1B20, 0x49454F, 0xCAC4D0, 0x6750A4} },
  /* 3 */ { .st = {.name = "Retro", .r_card = 0, .r_pill = 0, .bar_h = 8, .play_r = 0, .play = PLAY_SQUARE, .row = ROW_INVERT, .tab = TAB_BRACKET, .bar = BAR_SEGMENTS, .knob = KNOB_NONE, .btn = BTN_OUTLINE, .outline = true, .def_layout = LAY_CLASSIC},
            .dark = {0x000000, 0x031203, 0x0A2A0A, 0x33FF33, 0x1F9A1F, 0x1F6A1F, 0x33FF33},
            .light = {0x9BBC0F, 0x8BAC0F, 0x7A9B0D, 0x0F380F, 0x306230, 0x306230, 0x0F380F} },
  /* 4 */ { .st = {.name = "Neon", .r_card = 10, .r_pill = -1, .bar_h = 3, .play_r = 24, .play = PLAY_OUTLINE_CIRCLE, .row = ROW_OUTLINE, .tab = TAB_PILL, .bar = BAR_LINE, .knob = KNOB_GLOW, .btn = BTN_OUTLINE, .outline = true, .glow = true, .icons_outline = true, .def_layout = LAY_CLASSIC},
            .dark = {0x05060F, 0x0B0E1F, 0x141A38, 0xE8F0FF, 0x6C7AA8, 0x1B2450, 0x00F0FF},
            .light = {0xEEF0FF, 0xFFFFFF, 0xDDE1FF, 0x10122B, 0x5560A0, 0xB6BDF0, 0xD100A8} },
  /* 5 */ { .st = {.name = "Minimal", .r_card = 2, .r_pill = 2, .bar_h = 2, .play_r = 24, .play = PLAY_BARE, .row = ROW_FLAT, .tab = TAB_UNDERLINE, .bar = BAR_LINE, .knob = KNOB_CIRCLE, .btn = BTN_NONE, .outline = true, .icons_outline = true, .def_layout = LAY_CLASSIC},
            .dark = {0x101010, 0x101010, 0x1C1C1C, 0xF0F0F0, 0x8A8A8A, 0x2E2E2E, 0xF0F0F0},
            .light = {0xF6F4EE, 0xF6F4EE, 0xE9E6DD, 0x1A1A1A, 0x777777, 0xCFCBBF, 0x1A1A1A} },
  /* 6 */ { .st = {.name = "Aurora", .r_card = 18, .r_pill = 14, .bar_h = 6, .play_r = 24, .play = PLAY_CIRCLE, .row = ROW_CARD, .tab = TAB_PILL, .bar = BAR_ROUND, .knob = KNOB_CIRCLE, .btn = BTN_FILLED, .gradient = GRAD_AURORA, .def_layout = LAY_CLASSIC},
            .dark = {0x0B0D12, 0x161923, 0x232836, 0xF4F6FF, 0xA9B0CC, 0x3A3F5C, 0x8B7CFF},
            .light = {0xF3F4F8, 0xFFFFFF, 0xE4E7EF, 0x1B1F33, 0x5A6285, 0xC9CEE6, 0x6C5CE7} },
  /* 7 */ { .st = {.name = "Cassette", .r_card = 8, .r_pill = 6, .bar_h = 6, .play_r = 8, .play = PLAY_SQUIRCLE, .row = ROW_CARD, .tab = TAB_PILL, .bar = BAR_LINE, .knob = KNOB_PILL, .btn = BTN_FILLED, .def_layout = LAY_CASSETTE},
            .dark = {0x1B1612, 0x2A211A, 0x3A2E24, 0xF1E6D0, 0xA8957A, 0x4A3B2D, 0xE8742A},
            .light = {0xE9DFC8, 0xF6EEDC, 0xDACBAB, 0x2A1F14, 0x7A6A52, 0xC9B994, 0xC8521A} },
  /* 8 */ { .st = {.name = "Vinyl", .r_card = 14, .r_pill = -1, .bar_h = 4, .play_r = 24, .play = PLAY_CIRCLE, .row = ROW_PILL, .tab = TAB_UNDERLINE, .bar = BAR_ROUND, .knob = KNOB_CIRCLE, .btn = BTN_FILLED, .def_layout = LAY_VINYL},
            .dark = {0x0E0E10, 0x1A1A1E, 0x26262C, 0xF2F0EA, 0x9A968C, 0x2C2C32, 0xF2B544},
            .light = {0xEFEBE2, 0xFBF8F0, 0xDDD6C6, 0x1C1A16, 0x6E685A, 0xCFC7B3, 0xB8741A} },
  /* 9 */ { .st = {.name = "Reel", .r_card = 4, .r_pill = 4, .bar_h = 5, .play_r = 6, .play = PLAY_SQUARE, .row = ROW_CARD, .tab = TAB_UNDERLINE, .bar = BAR_SEGMENTS, .knob = KNOB_NONE, .btn = BTN_OUTLINE, .def_layout = LAY_REEL},
            .dark = {0x14181A, 0x1F2528, 0x2D3539, 0xE6ECEE, 0x8C999E, 0x3A454A, 0x7FD1B9},
            .light = {0xDDE3E5, 0xEEF2F3, 0xC9D2D5, 0x14181A, 0x5C6B70, 0xB5C0C4, 0x1F8F73} },
  /* 10 */ { .st = {.name = "Night", .r_card = 12, .r_pill = 12, .bar_h = 3, .play_r = 24, .play = PLAY_OUTLINE_CIRCLE, .row = ROW_FLAT, .tab = TAB_UNDERLINE, .bar = BAR_LINE, .knob = KNOB_CIRCLE, .btn = BTN_OUTLINE, .def_layout = LAY_NIGHT},
            .dark = {0x000000, 0x0A0805, 0x14100B, 0xC48C3E, 0x8A6430, 0x2A1E10, 0xD08A24},
            .light = {0xECDFC2, 0xF4EAD2, 0xDCCBA5, 0x3B2A12, 0x7A6339, 0xCDB98A, 0x8A5A1E} },
  /* 11 */ { .st = {.name = "Big Buttons", .r_card = 18, .r_pill = -1, .bar_h = 12, .play_r = 28, .play = PLAY_SQUIRCLE, .row = ROW_PILL, .tab = TAB_PILL, .bar = BAR_ROUND, .knob = KNOB_CIRCLE, .btn = BTN_FILLED, .def_layout = LAY_BIG},
            .dark = {0x0A0A0A, 0x1E1E1E, 0x2E2E2E, 0xFFFFFF, 0xB0B0B0, 0x4A4A4A, 0xFFD60A},
            .light = {0xFFFFFF, 0xF0F0F0, 0xDCDCDC, 0x000000, 0x444444, 0x999999, 0x0050D0} },
  /* 12 */ { .st = {.name = "Zen", .r_card = 20, .r_pill = -1, .bar_h = 2, .play_r = 24, .play = PLAY_OUTLINE_CIRCLE, .row = ROW_FLAT, .tab = TAB_UNDERLINE, .bar = BAR_LINE, .knob = KNOB_CIRCLE, .btn = BTN_NONE, .icons_outline = true, .def_layout = LAY_ZEN},
            .dark = {0x0F1412, 0x151C19, 0x1E2823, 0xDCE8E0, 0x7E8F86, 0x26332C, 0x9CCFB0},
            .light = {0xF1F5F0, 0xFFFFFF, 0xDCE6DE, 0x1F2A24, 0x6A7A70, 0xCCDAD0, 0x4F8F6A} },
  /* 13 */ { .st = {.name = "Quest Log", .r_card = 0, .r_pill = 0, .bar_h = 8, .play_r = 0, .play = PLAY_SQUARE, .row = ROW_INVERT, .tab = TAB_BRACKET, .bar = BAR_SEGMENTS, .knob = KNOB_NONE, .btn = BTN_OUTLINE, .outline = true, .def_layout = LAY_QUEST},
            .dark = {0x120F1E, 0x1C1730, 0x2A2347, 0xF4EBD0, 0x9B8FB8, 0x3B3260, 0xFFC83D},
            .light = {0xE8D8B0, 0xF1E5C4, 0xD8C493, 0x3A2A10, 0x7A6636, 0xB8A06A, 0xA8321A} },
  /* 14 */ { .st = {.name = "Shelf", .r_card = 10, .r_pill = 10, .bar_h = 5, .play_r = 24, .play = PLAY_CIRCLE, .row = ROW_CARD, .tab = TAB_PILL, .bar = BAR_ROUND, .knob = KNOB_CIRCLE, .btn = BTN_FILLED, .def_layout = LAY_CLASSIC, .def_view = VIEW_SHELF},
            .dark = {0x17120E, 0x261C15, 0x372A1F, 0xF0E4D2, 0xA89580, 0x4A3727, 0xD9A05B},
            .light = {0xEAD9BF, 0xF6EBD8, 0xD9C3A0, 0x2B1D10, 0x7A6244, 0xC8B08A, 0xA45B1E} },
  /* 15 */ { .st = {.name = "Synthwave", .r_card = 12, .r_pill = -1, .bar_h = 5, .play_r = 24, .play = PLAY_CIRCLE, .row = ROW_CARD, .tab = TAB_PILL, .bar = BAR_ROUND, .knob = KNOB_GLOW, .btn = BTN_FILLED, .gradient = GRAD_SYNTH, .decor = DECOR_GRID, .def_layout = LAY_CLASSIC},
            .dark = {0x0D0221, 0x1A0B3A, 0x2A1255, 0xF6EEFF, 0xB59FDA, 0x4A2A7A, 0xFF2E97},
            .light = {0xFFE9F7, 0xFFFFFF, 0xF3D8FF, 0x2A0B4A, 0x7A4A9A, 0xE0B9F5, 0xD6007A} },
  /* 16 */ { .st = {.name = "HOME", .r_card = 16, .r_pill = 10, .bar_h = 6, .play_r = 24, .play = PLAY_CIRCLE, .row = ROW_CARD, .tab = TAB_SEG, .bar = BAR_ROUND, .knob = KNOB_CIRCLE, .btn = BTN_FILLED, .def_layout = LAY_CLASSIC},
            .dark = {0x1C2026, 0x2A3038, 0x363E48, 0xF2F6FA, 0x98A4B2, 0x3F4852, 0x49B6E8},
            .light = {0xE6E8EB, 0xFFFFFF, 0xD5D9DE, 0x2B2F36, 0x5C6670, 0xC3C9D0, 0x0A78B8} },
};

static const char *ACCENT_NAMES[ACCENT_COUNT] = {
    "Auto (cover)", "Style default", "Blue", "Teal", "Green", "Lime", "Amber", "Orange", "Red", "Pink", "Purple", "Mono"};
static const unsigned PRESETS[9] = {0x3D8BFF, 0x2DD4BF, 0x4ADE80, 0xA3E635, 0xFBBF24, 0xFB923C, 0xF87171, 0xF472B6, 0xA78BFA};
static const char *LAYOUT_NAMES[LAY_COUNT + 1] = {
    "Style default", "Classic", "Big buttons", "Night", "Zen", "Cassette", "Vinyl", "Reel-to-reel", "Quest log"};
static const char *VIEW_NAMES[3] = {"Style default", "List", "Shelf"};

const char *theme_style_name(int i) { return STYLES[(i >= 0 && i < STYLE_COUNT) ? i : 0].st.name; }
const char *theme_accent_name(int i) { return ACCENT_NAMES[(i >= 0 && i < ACCENT_COUNT) ? i : 0]; }
const char *theme_layout_name(int s) { return LAYOUT_NAMES[(s >= 0 && s <= LAY_COUNT) ? s : 0]; }
const char *theme_view_name(int s) { return VIEW_NAMES[(s >= 0 && s <= 2) ? s : 0]; }

int theme_layout(void) {
    int v = g_set.layout;
    if (v <= 0 || v > LAY_COUNT) return ui_style ? ui_style->def_layout : LAY_CLASSIC;
    return v - 1;
}

int theme_view(void) {
    int v = g_set.libview;
    if (v <= 0 || v > 2) return ui_style ? ui_style->def_view : VIEW_LIST;
    return v - 1;
}

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

static int clampi(int v, int lo, int hi, int def) { return (v < lo || v > hi) ? def : v; }

void theme_apply(void) {
    g_set.style = clampi(g_set.style, 0, STYLE_COUNT - 1, 0);
    g_set.accent = clampi(g_set.accent, 0, ACCENT_COUNT - 1, 0);
    g_set.icons = clampi(g_set.icons, 0, 2, 0);
    g_set.skip_back = clampi(g_set.skip_back, 0, 2, 1);
    g_set.skip_fwd = clampi(g_set.skip_fwd, 0, 2, 1);
    g_set.cover_flip = clampi(g_set.cover_flip, 0, 3, 0);
    g_set.cover_rot = clampi(g_set.cover_rot, 0, 3, 0);
    g_set.layout = clampi(g_set.layout, 0, LAY_COUNT, 0);
    g_set.libview = clampi(g_set.libview, 0, 2, 0);
    g_set.anim = g_set.anim ? 1 : 0;
    g_set.autocont = g_set.autocont ? 1 : 0;
    g_set.lidplay = g_set.lidplay ? 1 : 0;
    g_set.sort = clampi(g_set.sort, 0, 2, 0);
    if (g_set.bright < -1 || g_set.bright > 100) g_set.bright = -1;

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

    if (ui_style->gradient) {                   // gradient backgrounds with frosted cards
        u32 white = RGB(0xFFFFFF);
        if (ui_style->gradient == GRAD_AURORA) {
            if (g_set.dark) {
                ui_pal.bg = theme_mix(RGB(0x0B0D12), ui_accent, 0.40f);
                ui_pal.bg2 = theme_mix(RGB(0x07080C), ui_accent, 0.07f);
            } else {
                ui_pal.bg = theme_mix(white, ui_accent, 0.28f);
                ui_pal.bg2 = theme_mix(white, ui_accent, 0.06f);
            }
        } else {                                // synthwave: deep violet sky into a magenta horizon
            if (g_set.dark) {
                ui_pal.bg = theme_mix(RGB(0x0D0221), ui_accent, 0.14f);
                ui_pal.bg2 = theme_mix(RGB(0x3A0B57), ui_accent, 0.28f);
            } else {
                ui_pal.bg = theme_mix(RGB(0xFFE9F7), ui_accent, 0.10f);
                ui_pal.bg2 = theme_mix(RGB(0xE3C9FF), ui_accent, 0.22f);
            }
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

    float lum = lum_of(ui_accent);
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
            else if (sscanf(line, "layout=%d", &v) == 1) g_set.layout = v;
            else if (sscanf(line, "libview=%d", &v) == 1) g_set.libview = v;
            else if (sscanf(line, "anim=%d", &v) == 1) g_set.anim = v;
            else if (sscanf(line, "autocont=%d", &v) == 1) g_set.autocont = v;
            else if (sscanf(line, "lidplay=%d", &v) == 1) g_set.lidplay = v;
            else if (sscanf(line, "sort=%d", &v) == 1) g_set.sort = v;
            else if (sscanf(line, "bright=%d", &v) == 1) g_set.bright = v;
        }
        fclose(f);
    }
    theme_apply();
}

void theme_save(void) {
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) return;
    fprintf(f, "style=%d\ndark=%d\naccent=%d\nicons=%d\nskip_back=%d\nskip_fwd=%d\ncover_flip=%d\ncover_rot=%d\n"
               "layout=%d\nlibview=%d\nanim=%d\nautocont=%d\nlidplay=%d\nsort=%d\nbright=%d\n",
            g_set.style, g_set.dark, g_set.accent, g_set.icons, g_set.skip_back, g_set.skip_fwd, g_set.cover_flip,
            g_set.cover_rot, g_set.layout, g_set.libview, g_set.anim, g_set.autocont, g_set.lidplay, g_set.sort, g_set.bright);
    fclose(f);
}
