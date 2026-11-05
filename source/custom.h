// custom.h - custom themes (.pid single file, .pix archive): scanning, applying, drawing custom layouts
#ifndef CUSTOM_H
#define CUSTOM_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "theme.h"
#include "themefile.h"

#ifndef THEMES_DIR
#define THEMES_DIR "sdmc:/3ds/narrate/themes"
#endif

// Everything a custom layout can show; main fills it in every frame.
typedef struct {
    const char *title, *artist, *chapter;
    char elapsed[16], remaining[16], left[24], percent[8], chapnum[24], clock[8], speed[8], sleep[16], volume[8];
    float frac_book, frac_chapter, seek_frac;
    bool playing, has_cover, seeking, lcd_ok, sleeping;
    int bright_pct;
    uint64_t now;
    float vu_l, vu_r;                      // 0..1 audio levels
    void (*status)(void);                  // draws the clock / battery / volume bar
} CLCtx;

int         custom_scan(void);                         // list the files in THEMES_DIR; returns how many
int         custom_count(void);
const char *custom_file(int i);                        // file name incl. extension
const char *custom_label(int i);                       // what to show in Settings
bool        custom_apply(const char *file, char *msg, size_t n);   // false = failed (msg says why); msg is also set on success
void        custom_unapply(void);                      // removes the theme (and the files unpacked from a .pix)
bool        custom_active(void);
const char *custom_active_file(void);
const char *custom_name(void);

// hooks used by the theme engine and the UI toolkit
int         custom_base_style(void);
void        custom_patch_style(UiStyle *st);
void        custom_patch_palette(UiPal *pal, int dark, bool *accent_set, uint32_t *accent, bool *on_set);
bool        custom_draw_icon(int icon, float cx, float cy, float size);   // true if a themed PNG icon was drawn

// custom player screens
int         custom_fallback_layout(void);              // built-in layout for screens the theme doesn't define (or -1)
bool        custom_has_top(void);
bool        custom_has_bottom(void);
bool        custom_top_animated(void);                 // does the top screen change on its own (spin, bob, waves, VU, ...)?
void        custom_draw_top(const CLCtx *c);
void        custom_draw_bottom(const CLCtx *c);
int         custom_hit(int x, int y);                  // ACT_* of the button under the touch, or ACT_NONE
bool        custom_seek_rect(short r[4]);
bool        custom_bright_rect(short r[4]);

#endif
