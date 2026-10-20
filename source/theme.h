// theme.h - UI styles, palettes, accent colours and persisted settings
#ifndef THEME_H
#define THEME_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

enum { PLAY_CIRCLE, PLAY_BARE, PLAY_SQUIRCLE, PLAY_SQUARE, PLAY_OUTLINE_CIRCLE };
enum { ROW_CARD, ROW_FLAT, ROW_PILL, ROW_INVERT, ROW_OUTLINE };
enum { TAB_PILL, TAB_SEG, TAB_UNDERLINE, TAB_BRACKET };
enum { BAR_ROUND, BAR_SEGMENTS, BAR_LINE };
enum { KNOB_NONE, KNOB_CIRCLE, KNOB_PILL, KNOB_GLOW };
enum { BTN_NONE, BTN_FILLED, BTN_OUTLINE };

typedef struct {
    const char *name;
    float r_card, r_pill;          // r_pill < 0 means fully round
    float bar_h;
    int play, row, tab, bar, knob, btn;
    bool outline, glow, gradient, icons_outline;
} UiStyle;

typedef struct { u32 bg, bg2, surface, surface2, text, dim, on_accent, line, error; } UiPal;

typedef struct { int style, dark, accent, icons, skip_back, skip_fwd, cover_flip, cover_rot; } Settings;

#define STYLE_COUNT  7
#define ACCENT_COUNT 12     // 0 = auto (cover art), 1 = style default, 2..10 presets, 11 = mono

extern Settings g_set;
extern const UiStyle *ui_style;
extern UiPal ui_pal;
extern u32 ui_accent, ui_accent_dim, ui_accent_soft;
extern u8  ui_ar, ui_ag, ui_ab;
extern bool ui_icons_outline;
extern const int SKIP_BACK_OPTS[3], SKIP_FWD_OPTS[3];

void theme_load(void);
void theme_save(void);
void theme_apply(void);                          // recompute palette + accent from g_set
void theme_set_auto_accent(u8 r, u8 g, u8 b);    // accent derived from the cover art
const char *theme_style_name(int i);
const char *theme_accent_name(int i);
u32  theme_accent_swatch(int i);                 // colour a given accent choice resolves to right now
u32  theme_mix(u32 a, u32 b, float t);

#endif
