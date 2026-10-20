// ui.h - theme-aware immediate-mode drawing toolkit on top of citro2d
#ifndef UI_H
#define UI_H

#include <3ds.h>
#include <citro2d.h>
#include <stdint.h>
#include <stddef.h>
#include "theme.h"

#define UI_TOP_W 400
#define UI_BOT_W 320
#define UI_SCR_H 240

enum { UI_LEFT = 0, UI_CENTER = 1, UI_RIGHT = 2 };

// colours come from the active theme
#define UI_BG       (ui_pal.bg)
#define UI_SURFACE  (ui_pal.surface)
#define UI_SURFACE2 (ui_pal.surface2)
#define UI_TEXT     (ui_pal.text)
#define UI_DIM      (ui_pal.dim)
#define UI_DARK     (ui_pal.on_accent)   /* text/icons drawn on top of the accent colour */
#define UI_ERROR    (ui_pal.error)

extern C3D_RenderTarget *ui_top, *ui_bot;

void ui_init(void);
void ui_exit(void);
void ui_begin(void);
void ui_end(void);
void ui_scene(C3D_RenderTarget *t);          // clears / paints the themed background
void ui_glow(float w, float h);              // accent glow at the top (only for styles that use it)

void ui_set_accent(u8 r, u8 g, u8 b);        // "auto" accent (from cover art)
void ui_set_accent_hue(float hue);

/* primitives */
void  ui_rect(float x, float y, float w, float h, u32 col);
void  ui_rrect(float x, float y, float w, float h, float r, u32 col);
void  ui_rrect_outline(float x, float y, float w, float h, float r, float t, u32 col, u32 fill);
void  ui_circle(float cx, float cy, float r, u32 col);
void  ui_line(float x0, float y0, float x1, float y1, float t, u32 col);
void  ui_bar(float x, float y, float w, float h, float frac, u32 bg, u32 fg);   // simple rounded bar (style independent)

/* text */
float ui_text_w(const char *s, float scale);
void  ui_text(const char *s, float x, float y, float scale, u32 col, int align);
void  ui_text_fit(const char *s, float x, float y, float maxw, float scale, u32 col, int align);
int   ui_text_wrap(const char *s, float x, float y, float maxw, float scale, u32 col, int maxlines, float line_h);

/* themed widgets */
void  ui_card(float x, float y, float w, float h);
void  ui_row(float x, float y, float w, float h, int selected, int current);
u32   ui_row_fg(int selected, int current);     // main text colour for a row
u32   ui_row_dim(int selected);                 // secondary text colour for a row
u32   ui_row_bg(int selected);                  // colour behind icons in a row
void  ui_pill(float x, float y, float w, float h, int active);
u32   ui_pill_bg(int active);
void  ui_progress(float x, float y, float w, float frac);
void  ui_seekbar(float x, float y, float w, float frac, int dragging);
void  ui_play_button(float cx, float cy, int playing, float scale);
void  ui_round_btn(float cx, float cy, float r);
void  ui_tab_container(float x, float y, float w, float h);
void  ui_tab(float x, float y, float w, float h, const char *label, int active);
void  ui_back_button(float x, float y, float w, float h);
void  ui_logo(float x, float y, float s);
void  ui_cover_placeholder(float x, float y, float size, const char *title);

/* icons (solid or outline depending on the icon setting) */
void  ui_icon_play(float cx, float cy, float s, u32 col);
void  ui_icon_pause(float cx, float cy, float s, u32 col);
void  ui_icon_next(float cx, float cy, float s, u32 col);
void  ui_icon_prev(float cx, float cy, float s, u32 col);
void  ui_icon_skip(float cx, float cy, int forward, const char *label);
void  ui_icon_list(float cx, float cy, float s, u32 col);
void  ui_icon_moon(float cx, float cy, float r, u32 col, u32 bg);
void  ui_icon_bookmark(float cx, float cy, u32 col, u32 bg);
void  ui_icon_close(float cx, float cy, float s, u32 col);
void  ui_icon_screen(float cx, float cy, u32 col, u32 bg);
void  ui_icon_gear(float cx, float cy, u32 col, u32 bg);
void  ui_icon_folder(float cx, float cy, u32 col);
void  ui_icon_book(float cx, float cy, u32 col, u32 bg);

void  ui_fmt_time(char *buf, size_t n, uint64_t ms, int force_hours);

#endif
