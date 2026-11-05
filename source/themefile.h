// themefile.h - parser for Narrate theme files (.pid, or theme.pid inside a .pix archive). Portable C, no 3DS code.
#ifndef THEMEFILE_H
#define THEMEFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TF_MAX_ELEMS  96
#define TF_MAX_IMAGES 24

enum { TC_BG, TC_BG2, TC_SURFACE, TC_SURFACE2, TC_TEXT, TC_DIM, TC_ON_ACCENT, TC_LINE, TC_ACCENT, TC_COUNT };
enum { EL_RECT, EL_CIRCLE, EL_LINE, EL_IMAGE, EL_COVER, EL_TEXT, EL_BAR, EL_BUTTON, EL_SEEK, EL_BRIGHT, EL_VU, EL_WAVE, EL_STATUS };
enum { F_NONE, F_TITLE, F_ARTIST, F_CHAPTER, F_ELAPSED, F_REMAINING, F_LEFT, F_PERCENT, F_CHAPNUM, F_CLOCK, F_SPEED, F_SLEEP, F_VOLUME, F_LITERAL };
enum { ACT_NONE, ACT_PLAY, ACT_BACK, ACT_FWD, ACT_PREV, ACT_NEXT, ACT_CHAPTERS, ACT_SLEEP, ACT_SETTINGS, ACT_BOOKMARK, ACT_SCREEN_OFF, ACT_SPEED, ACT_COUNT };
enum { IC_PLAY, IC_PAUSE, IC_PREV, IC_NEXT, IC_BACK, IC_FWD, IC_CHAPTERS, IC_SLEEP, IC_SETTINGS, IC_BOOKMARK, IC_SCREEN_OFF, IC_SPEED,
       IC_FOLDER, IC_BOOK, IC_CHECK, IC_SUN, IC_SORT, IC_CLOSE, IC_COUNT };
enum { BS_NONE, BS_PILL, BS_ROUND, BS_KEY, BS_SQUARE };
enum { WHEN_ALWAYS, WHEN_PLAYING, WHEN_PAUSED };
enum { FOLLOW_NONE, FOLLOW_BOOK, FOLLOW_CHAPTER };
enum { FIT_CONTAIN, FIT_STRETCH, FIT_COVER };

typedef struct {
    int kind;
    float x, y, w, h;
    float x2, y2;                 // end point of a line
    float tx, ty;                 // "to=" target: where the element ends up when the book / chapter is finished
    bool has_w, has_h;
    uint32_t color, color2;       // packed like C2D_Color32: r | g<<8 | b<<16 | a<<24
    int color_ref, color2_ref;    // >= 0: a TC_* palette colour instead of the literal
    float radius, width, size, rot, alpha;
    int align, lines;
    int field;
    char literal[48];
    int image;                    // index into TfTheme.images, -1 = none
    int action, icon, style;
    char label[24];
    float spin, bob_amp, bob_ms, sway_deg, sway_ms, pulse_amt, pulse_ms, scroll;
    int blink_ms, when, follow, fit;
    int vu_kind, vu_ch, segs;
    float amp, wavelen, speed;
} TfElem;

typedef struct { char name[32]; char path[96]; int frames; float fps; } TfImage;

typedef struct {
    char name[48], author[48], desc[96];
    char base[24];                // built-in style to inherit shapes/behaviour from ("" = Narrate)
    char layout[24];              // built-in layout used for any screen the theme doesn't define ("" = style default)
    int mode;                     // 0 both, 1 dark only, 2 light only
    int icon_tint;                // 0 none, 1 text colour, 2 accent colour
    int image_flip;               // 0 none, 1 vertical, 2 horizontal, 3 both (fix for PNGs that show flipped)

    uint32_t pal[2][TC_COUNT];    // [0] = dark, [1] = light
    bool has[2][TC_COUNT];

    float r_card, r_pill, bar_h, play_r;
    bool has_r_card, has_r_pill, has_bar_h, has_play_r;
    int play, row, tab, bar, knob, btn, icons;      // -1 = keep the base style's value

    char font[96];
    TfImage images[TF_MAX_IMAGES]; int nimages;
    char icon_path[IC_COUNT][96];

    TfElem top[TF_MAX_ELEMS]; int ntop; bool has_top;
    TfElem bottom[TF_MAX_ELEMS]; int nbottom; bool has_bottom;

    int warnings;
    char first_warning[96];
} TfTheme;

// Parses theme text. Never fails hard: unknown lines are skipped and counted in t->warnings.
void tf_parse(const char *text, size_t len, TfTheme *t);
const char *tf_icon_name(int icon);

#endif
