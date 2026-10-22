// theme.h - UI styles, layouts, palettes, accent colours and persisted settings
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
enum { GRAD_NONE, GRAD_AURORA, GRAD_SYNTH };
enum { DECOR_NONE, DECOR_GRID };

// Screen layouts (how the player screens are arranged). Independent from the style, but every style has a default.
enum { LAY_CLASSIC, LAY_BIG, LAY_NIGHT, LAY_ZEN, LAY_CASSETTE, LAY_VINYL, LAY_REEL, LAY_QUEST, LAY_COUNT };
enum { VIEW_LIST, VIEW_SHELF };

typedef struct {
    const char *name;
    float r_card, r_pill;          // r_pill < 0 means fully round
    float bar_h, play_r;
    int play, row, tab, bar, knob, btn;
    bool outline, glow, icons_outline;
    int gradient, decor, def_layout, def_view;
} UiStyle;

typedef struct { u32 bg, bg2, surface, surface2, text, dim, on_accent, line, error; } UiPal;

typedef struct {
    int style, dark, accent, icons, skip_back, skip_fwd, cover_flip, cover_rot;
    int layout;        // 0 = style default, 1..LAY_COUNT = explicit layout
    int libview;       // 0 = style default, 1 = list, 2 = shelf
    int anim;          // animated decks on/off
    int autocont;      // continue with the next book of a series folder
    int lidplay;       // keep playing when the lid is closed
    int sort;          // library sort mode
    int bright;        // screen brightness 0..100, -1 = leave the system setting alone
} Settings;

#define STYLE_COUNT  17
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
const char *theme_layout_name(int setting);      // setting value 0..LAY_COUNT
const char *theme_view_name(int setting);        // setting value 0..2
u32  theme_accent_swatch(int i);                 // colour a given accent choice resolves to right now
u32  theme_mix(u32 a, u32 b, float t);
int  theme_layout(void);                         // effective layout (LAY_*)
int  theme_view(void);                           // effective library view (VIEW_*)

#endif
