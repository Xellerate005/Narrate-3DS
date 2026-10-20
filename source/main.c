// Narrate - M4B audiobook player for the Nintendo 3DS
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>

#include "bookmarks.h"
#include "browser.h"
#include "cover.h"
#include "log.h"
#include "m4b.h"
#include "player.h"
#include "progress.h"
#include "theme.h"
#include "ui.h"

// Leave most of the app memory for the heap (chapter tables of long books); the linear heap
// only needs room for GPU buffers, cover art and audio buffers.
u32 __ctru_linear_heap_size = 10 * 1024 * 1024;

#define LIB_ROOT  "sdmc:/AudioBook"
#define DATA_DIR  "sdmc:/3ds/narrate"

/* layout: lists */
#define ROW_H     34
#define LIST_Y    36
#define LIST_VIS  6

/* layout: player (bottom screen) */
#define BAR_X     24
#define BAR_W     272
#define BAR_Y     66
#define CTRL_Y    134
#define PILL_Y    190
#define PILL_H    38
#define PILL_W    96
#define HDR_Y     22
#define OFF_X     26
#define BM_X      294
#define GEAR_X    294

static const int BTN_X[5]  = {36, 94, 160, 226, 284};   // prev ch, back, play, forward, next ch
static const int PILL_X[3] = {8, 112, 216};

typedef enum { S_LIB, S_PLAYER, S_CHAPTERS, S_SETTINGS } State;

static State   g_state = S_LIB;
static State   g_settings_from = S_LIB;
static Browser g_br;
static bool    g_audio_ok;
static char    g_book_path[800];
static const M4b *g_book;
static int     g_cur_ch;

static int     g_vol = 10;                 // software volume (0..10), only used if the hardware slider can't be read
static float   g_last_vol_sent = -1;
static int     g_sleep_mode;               // 0 off, 1-4 timed, 5 end of chapter
static u64     g_sleep_end;
static int     g_sleep_ch;
static const int SLEEP_MIN[5] = {0, 15, 30, 45, 60};

static bool    g_lcd_ok, g_screen_off;
static bool    g_ptmu_ok, g_mcu_ok;
static u8      g_batt = 5, g_charging;
static u64     g_batt_check, g_slider_check;
static bool    g_slider_ok;
static u8      g_slider;                   // hardware volume slider, 0..63

static char    g_msg[400];
static u64     g_msg_until;
static u64     g_last_save;

static bool    g_seeking;
static float   g_seek_frac;

static int      g_ch_sel, g_ch_scroll;
static int      g_list_tab;                // 0 = chapters, 1 = bookmarks
static Bookmark g_bm[BM_MAX_PER_BOOK];
static int      g_nbm, g_bm_sel, g_bm_scroll;
static char     g_toast[96];
static u64      g_toast_until;
static char     g_cont_path[800];
static bool     g_have_cont;
static int      g_set_sel;
static u64      g_save_at;          // deferred progress save (keeps button presses snappy)
static u64      g_stats_log;
static u64      g_active_until;     // keep redrawing at full rate while the user is interacting

typedef struct { bool active, moved; int sx, sy, ly; float acc; } Drag;
static Drag g_drag;
static int g_rep[32];

/* --------------------------------------------------------------- helpers */

static void set_msg(const char *s) {
    snprintf(g_msg, sizeof g_msg, "%s", s);
    g_msg_until = osGetTime() + 4500;
}

static void toast(const char *s) {
    snprintf(g_toast, sizeof g_toast, "%s", s);
    g_toast_until = osGetTime() + 2200;
}

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static bool in_circle(int x, int y, int cx, int cy, int r) {
    int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

static bool key_rep(u32 kd, u32 kh, u32 key) {
    int bit = __builtin_ctz(key);
    if (kd & key) { g_rep[bit] = 0; return true; }
    if (kh & key) {
        g_rep[bit]++;
        return g_rep[bit] > 22 && (g_rep[bit] % 4) == 0;
    }
    g_rep[bit] = 0;
    return false;
}

static void fmt_left(char *b, size_t n, uint64_t ms) {
    unsigned m = (unsigned)(ms / 60000), h = m / 60;
    if (h) snprintf(b, n, "%uh %02um left", h, m % 60);
    else snprintf(b, n, "%um left", m);
}

static uint64_t ch_start(int i) { return g_book->chapters[i].start_ms; }

static uint64_t ch_end(int i) {
    return (i + 1 < g_book->nchapters) ? g_book->chapters[i + 1].start_ms : player_duration_ms();
}

static int skip_back_ms(void) { return SKIP_BACK_OPTS[g_set.skip_back] * 1000; }
static int skip_fwd_ms(void)  { return SKIP_FWD_OPTS[g_set.skip_fwd] * 1000; }

static void screen_off(bool off) {
    if (!g_lcd_ok) return;
    if (off) GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTH);
    else GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTH);
    g_screen_off = off;
}

// Volume shown in the app: the physical slider when we can read it, otherwise the software volume.
static int volume_level10(void) { return g_slider_ok ? ((int)g_slider * 10 + 31) / 63 : g_vol; }

static void update_status(u64 now) {
    if (g_ptmu_ok && now - g_batt_check >= 5000) {
        g_batt_check = now;
        PTMU_GetBatteryLevel(&g_batt);
        PTMU_GetBatteryChargeState(&g_charging);
    }
    if (g_mcu_ok && now - g_slider_check >= 80) {
        g_slider_check = now;
        u8 v = 0;
        if (R_SUCCEEDED(MCUHWC_GetSoundSliderLevel(&v))) {
            if (!g_slider_ok) { g_slider_ok = true; g_slider = v; }
            else if (v != g_slider) {
                g_slider = v;
                char m[40];
                snprintf(m, sizeof m, "Volume  %d%%", (int)v * 100 / 63);
                toast(m);
            }
        } else {
            g_slider_ok = false;
        }
    }
}

static void save_progress(void) {
    if (!g_book) return;
    progress_set(g_book_path, player_position_ms(), player_duration_ms());
    progress_save();
    g_last_save = osGetTime();
}

// The hardware slider already scales the output level, so the app only applies the sleep fade
// (or its own volume if the slider can't be read).
static void apply_volume(float fade) {
    float base = g_slider_ok ? 1.0f : g_vol / 10.0f;
    float v = base * fade;
    if (v != g_last_vol_sent) {
        player_set_volume(v);
        g_last_vol_sent = v;
    }
}

/* ---------------------------------------------- bookmarks / continue / library */

static void refresh_bookmarks(void) {
    g_nbm = g_book ? bm_get(g_book_path, g_bm, BM_MAX_PER_BOOK) : 0;
    if (g_bm_sel > g_nbm) g_bm_sel = g_nbm;
}

// "Continue listening" = the last book that was opened, if it is partly (not fully) listened to.
static void refresh_continue(void) {
    g_have_cont = false;
    char p[800];
    uint64_t pos = 0, dur = 0;
    struct stat st;
    if (!progress_get_last(p, sizeof p)) return;
    if (strncasecmp(p, LIB_ROOT "/", strlen(LIB_ROOT) + 1) != 0) return;   // only books inside the AudioBook folder
    if (stat(p, &st) != 0) return;
    if (!progress_get(p, &pos, &dur) || !dur || pos < 3000 || pos + 5000 >= dur) return;
    snprintf(g_cont_path, sizeof g_cont_path, "%s", p);
    g_have_cont = true;
}

static void lib_inject_continue(void) {
    if (!g_have_cont) return;
    const char *b = strrchr(g_cont_path, '/');
    char name[256];
    snprintf(name, sizeof name, "%.255s", b ? b + 1 : g_cont_path);
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;
    browser_insert_front(&g_br, name);
}

static void lib_open_dir(const char *path) {
    browser_open(&g_br, path);
    lib_inject_continue();
}

static void lib_up(void) {
    browser_up(&g_br);
    lib_inject_continue();
}

static void lib_enter(const char *dir) {
    browser_enter(&g_br, dir);
    lib_inject_continue();
}

/* ------------------------------------------------------ book open / close */

static void draw_loading(const char *name) {
    ui_begin();
    ui_scene(ui_top);
    ui_glow(UI_TOP_W, 140);
    ui_text("Opening...", 200, 92, 0.8f, UI_TEXT, UI_CENTER);
    ui_text_fit(name, 200, 130, 340, 0.5f, UI_DIM, UI_CENTER);
    ui_scene(ui_bot);
    ui_end();
}

static void close_book(void) {
    if (g_book) save_progress();
    player_close();
    cover_free();
    g_book = NULL;
    g_sleep_mode = 0;
    g_seeking = false;
    g_nbm = 0;
    ui_set_accent(0x7C, 0x9C, 0xFF);
    g_state = S_LIB;
    refresh_continue();
    char cur[512];
    snprintf(cur, sizeof cur, "%s", g_br.path);
    lib_open_dir(cur);
}

static void open_book(const char *path) {
    const char *base = strrchr(path, '/');
    draw_loading(base ? base + 1 : path);

    char err[160] = "";
    nlog("open_book: %s", path);
    if (player_open(path, err, sizeof err) != 0) {
        nlog("player_open failed: %s", err);
        set_msg(err[0] ? err : "Could not open book");
        return;
    }
    g_book = player_book();
    snprintf(g_book_path, sizeof g_book_path, "%s", path);
    progress_set_last(g_book_path);
    g_bm_sel = g_bm_scroll = 0;
    g_list_tab = 0;
    refresh_bookmarks();
    nlog("player_open ok");

    cover_free();
    u8 r, g, b;
    if (g_book->cover && cover_load(g_book->cover, g_book->cover_len) && cover_accent(&r, &g, &b)) {
        ui_set_accent(r, g, b);
    } else {
        unsigned h = 5381;
        for (const char *p = g_book->title; *p; p++) h = h * 33 + (unsigned char)*p;
        ui_set_accent_hue((float)(h % 360));
    }

    uint64_t pos = 0, dur = 0, total = player_duration_ms();
    if (progress_get(path, &pos, &dur) && pos > 0 && pos + 5000 < total) {
        player_seek_ms(pos > 3000 ? pos - 3000 : pos);   // rewind a little so you get your bearings
    }
    player_play();
    g_state = S_PLAYER;
    g_cur_ch = m4b_chapter_at(g_book, player_position_ms());
    g_sleep_mode = 0;
    g_seeking = false;
    g_last_save = osGetTime();
}

/* --------------------------------------------------------- playback actions */

static void do_toggle(void) {
    if (player_is_playing()) { player_pause(); g_save_at = osGetTime() + 500; }
    else player_play();
}

static void seek_rel(int64_t delta) {
    int64_t pos = (int64_t)player_position_ms() + delta;
    int64_t dur = (int64_t)player_duration_ms();
    if (pos < 0) pos = 0;
    if (pos > dur) pos = dur;
    player_seek_ms((uint64_t)pos);
}

static void goto_chapter(int i) {
    if (i < 0) i = 0;
    if (i >= g_book->nchapters) i = g_book->nchapters - 1;
    player_seek_ms(ch_start(i));
    g_cur_ch = i;
}

static void prev_chapter(void) {
    uint64_t pos = player_position_ms();
    if (pos > ch_start(g_cur_ch) + 3000 || g_cur_ch == 0) goto_chapter(g_cur_ch);
    else goto_chapter(g_cur_ch - 1);
}

static void next_chapter(void) {
    if (g_cur_ch + 1 < g_book->nchapters) goto_chapter(g_cur_ch + 1);
}

static void cycle_sleep(void) {
    g_sleep_mode = (g_sleep_mode + 1) % 6;
    if (g_sleep_mode >= 1 && g_sleep_mode <= 4) g_sleep_end = osGetTime() + (u64)SLEEP_MIN[g_sleep_mode] * 60000;
    if (g_sleep_mode == 5) g_sleep_ch = g_cur_ch;
}

// Only meaningful when the hardware slider can't be read.
static void change_volume(int d) {
    if (g_slider_ok) { toast("Use the volume slider"); return; }
    g_vol += d;
    if (g_vol < 0) g_vol = 0;
    if (g_vol > 10) g_vol = 10;
}

// Runs every frame while a book is open (also while browsing lists / settings).
static void tick_playback(u64 now) {
    if (!g_book) return;
    uint64_t pos = player_position_ms();
    g_cur_ch = m4b_chapter_at(g_book, pos);

    float fade = 1.0f;
    if (g_sleep_mode >= 1 && g_sleep_mode <= 4) {
        int64_t rem = (int64_t)g_sleep_end - (int64_t)now;
        if (rem <= 0) {
            if (player_is_playing()) { player_pause(); save_progress(); }
            g_sleep_mode = 0;
        } else if (rem < 10000) {
            fade = rem / 10000.0f;
        }
    } else if (g_sleep_mode == 5 && player_is_playing() && g_cur_ch != g_sleep_ch) {
        player_pause();
        save_progress();
        g_sleep_mode = 0;
    }
    apply_volume(fade);

    if (g_save_at && now >= g_save_at) { g_save_at = 0; save_progress(); }
    if (player_is_playing() && now - g_last_save > 30000) save_progress();

    if (player_is_playing() && now - g_stats_log > 30000) {
        PlayerStats ps;
        player_get_stats(&ps);
        if (g_stats_log) nlog("audio stats: decode load %d%% (peak %d%%), underruns %u, new3ds=%d", ps.load_pct, ps.peak_pct, ps.underruns, (int)ps.is_new3ds);
        g_stats_log = now;
    }

    if (player_ended()) {
        char m[320];
        player_pause();
        progress_set(g_book_path, player_duration_ms(), player_duration_ms());
        progress_save();
        snprintf(m, sizeof m, "Finished: %s", g_book->title);
        close_book();
        set_msg(m);
    }
}

/* ------------------------------------------------------------ drawing: shared */

static void draw_status(void) {
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    char clk[16] = "--:--";
    if (lt) strftime(clk, sizeof clk, "%H:%M", lt);
    ui_text(clk, 364, 6, 0.42f, UI_DIM, UI_RIGHT);

    // battery
    float bx = 370, by = 9;
    u32 body = ui_style->outline ? ui_pal.line : ui_pal.surface2;
    ui_rrect(bx, by, 22, 11, 3, body);
    ui_rect(bx + 22, by + 3, 2, 5, body);
    int lvl = g_batt > 5 ? 5 : g_batt;
    u32 col = g_charging ? ui_accent : (lvl <= 1 ? UI_ERROR : UI_DIM);
    for (int i = 0; i < lvl; i++) ui_rect(bx + 2 + i * 4, by + 2, 3, 7, col);

    // volume (follows the physical slider)
    int filled = (volume_level10() + 1) / 2;
    for (int i = 0; i < 5; i++) {
        float h = 3 + i * 2;
        ui_rect(296 + i * 5, 17 - h, 3, h, i < filled ? ui_accent : ui_pal.line);
    }
}

static void draw_toast(u64 now) {
    if (now >= g_toast_until || !g_toast[0]) return;
    float tw = ui_text_w(g_toast, 0.42f) + 26;
    ui_rrect(200 - tw / 2, 4, tw, 22, ui_style->r_pill < 0 ? 11 : ui_style->r_pill, ui_accent);
    ui_text(g_toast, 200, 8, 0.42f, UI_DARK, UI_CENTER);
}

typedef struct {
    const char *title, *artist, *chapter;
    int ch, nch;
    float frac;
    uint64_t left_ms;
} Np;

static void draw_np_top(const Np *np, u64 now, bool preview) {
    ui_scene(ui_top);
    ui_glow(UI_TOP_W, 150);
    draw_status();

    // cover with soft shadow (or a frame for outline styles)
    const float cx = 24, cy = 34, cs = 172;
    if (ui_style->outline) {
        u32 fc = ui_accent_dim;
        ui_rect(cx - 3, cy - 3, cs + 6, 2, fc);
        ui_rect(cx - 3, cy + cs + 1, cs + 6, 2, fc);
        ui_rect(cx - 3, cy - 3, 2, cs + 6, fc);
        ui_rect(cx + cs + 1, cy - 3, 2, cs + 6, fc);
    } else {
        for (int i = 4; i >= 1; i--) ui_rect(cx - i + 2, cy - i + 6, cs + 2 * i, cs + 2 * i, C2D_Color32(0, 0, 0, g_set.dark ? 38 : 20));
    }
    if (cover_ready()) cover_draw(cx, cy, cs);
    else ui_cover_placeholder(cx, cy, cs, np->title);

    const float x0 = 214, w = 170;
    ui_text(preview ? "PREVIEW" : "NOW PLAYING", x0, 32, 0.36f, ui_accent, UI_LEFT);
    int n = ui_text_wrap(np->title, x0, 48, w, 0.62f, UI_TEXT, 2, 22);
    float y = 48 + n * 22 + 3;
    if (np->artist && np->artist[0]) ui_text_fit(np->artist, x0, y, w, 0.44f, UI_DIM, UI_LEFT);

    char s[64];
    snprintf(s, sizeof s, "CH %d / %d", np->ch + 1, np->nch);
    float cw = ui_text_w(s, 0.38f) + 16;
    ui_pill(x0, 124, cw, 20, 1);
    ui_text(s, x0 + 8, 128, 0.38f, ui_accent, UI_LEFT);
    ui_text_wrap(np->chapter, x0, 150, w, 0.48f, UI_TEXT, preview ? 1 : 2, 17);

    if (preview) {
        ui_progress(x0, 176, w, np->frac);
        ui_icon_moon(x0 + 8, 205, 7, UI_DIM, UI_BG);
        ui_icon_prev(x0 + 38, 205, 8, UI_TEXT);
        ui_play_button(x0 + w / 2, 205, 0, 0.52f);
        ui_icon_next(x0 + w - 38, 205, 8, UI_TEXT);
        ui_icon_bookmark(x0 + w - 8, 205, UI_DIM, UI_BG);
    } else {
        ui_progress(x0, 198, w, np->frac);
        snprintf(s, sizeof s, "%d%%", (int)(np->frac * 100));
        ui_text(s, x0, 208, 0.38f, UI_DIM, UI_LEFT);
        fmt_left(s, sizeof s, np->left_ms);
        ui_text(s, x0 + w, 208, 0.38f, UI_DIM, UI_RIGHT);

        if (g_sleep_mode) {
            char z[48];
            if (g_sleep_mode == 5) snprintf(z, sizeof z, "Sleep: end of chapter");
            else {
                int64_t rem = (int64_t)g_sleep_end - (int64_t)now;
                snprintf(z, sizeof z, "Sleep in %dm", (int)((rem > 0 ? rem : 0) / 60000) + 1);
            }
            ui_text(z, 12, 6, 0.4f, ui_accent, UI_LEFT);
        }
    }
    draw_toast(now);
}

static void draw_player_top(uint64_t pos, u64 now) {
    uint64_t dur = player_duration_ms();
    Np np = {
        g_book->title, g_book->artist, g_book->chapters[g_cur_ch].title,
        g_cur_ch, g_book->nchapters,
        dur ? (float)((double)pos / (double)dur) : 0,
        dur > pos ? dur - pos : 0};
    draw_np_top(&np, now, false);
}

/* ------------------------------------------------------- drawing: player bottom */

static u32 btn_bg(void) { return ui_style->btn == BTN_FILLED ? ui_pal.surface2 : ui_pal.bg; }

static void draw_player_bottom(uint64_t pos, u64 now) {
    ui_scene(ui_bot);
    uint64_t dur = player_duration_ms();

    // thin whole-book progress along the very top edge
    ui_rect(0, 0, UI_BOT_W, 3, ui_pal.line);
    ui_rect(0, 0, UI_BOT_W * (dur ? (float)((double)pos / (double)dur) : 0), 3, ui_accent);

    char s[64];
    snprintf(s, sizeof s, "CHAPTER %d OF %d", g_cur_ch + 1, g_book->nchapters);
    ui_round_btn(OFF_X, HDR_Y, 17);
    ui_icon_screen(OFF_X, HDR_Y, UI_TEXT, btn_bg());
    ui_round_btn(BM_X, HDR_Y, 17);
    ui_icon_bookmark(BM_X, HDR_Y, g_nbm ? ui_accent : UI_TEXT, btn_bg());
    ui_text(s, 160, 12, 0.36f, ui_accent, UI_CENTER);
    ui_text_fit(g_book->chapters[g_cur_ch].title, 160, 26, 196, 0.55f, UI_TEXT, UI_CENTER);

    // chapter-scoped seek bar
    uint64_t cs = ch_start(g_cur_ch), ce = ch_end(g_cur_ch);
    uint64_t clen = ce > cs ? ce - cs : 1;
    float frac;
    uint64_t shown;
    if (g_seeking) { frac = g_seek_frac; shown = cs + (uint64_t)(frac * clen); }
    else { shown = pos; frac = pos > cs ? (float)((double)(pos - cs) / (double)clen) : 0; }
    if (frac > 1) frac = 1;
    ui_seekbar(BAR_X, BAR_Y + 3 - ui_style->bar_h / 2, BAR_W, frac, g_seeking);

    int hrs = clen >= 3600000;
    char a[24], b[24];
    ui_fmt_time(a, sizeof a, shown > cs ? shown - cs : 0, hrs);
    ui_text(a, BAR_X, 80, 0.42f, UI_DIM, UI_LEFT);
    ui_fmt_time(b, sizeof b, ce > shown ? ce - shown : 0, hrs);
    snprintf(s, sizeof s, "-%s", b);
    ui_text(s, BAR_X + BAR_W, 80, 0.42f, UI_DIM, UI_RIGHT);

    // transport
    char lb[8], lf[8];
    snprintf(lb, sizeof lb, "%d", SKIP_BACK_OPTS[g_set.skip_back]);
    snprintf(lf, sizeof lf, "%d", SKIP_FWD_OPTS[g_set.skip_fwd]);
    ui_icon_prev(BTN_X[0], CTRL_Y, 9, UI_TEXT);
    ui_icon_next(BTN_X[4], CTRL_Y, 9, UI_TEXT);
    ui_icon_skip(BTN_X[1], CTRL_Y, 0, lb);
    ui_icon_skip(BTN_X[3], CTRL_Y, 1, lf);
    ui_play_button(BTN_X[2], CTRL_Y, player_is_playing(), 1.0f);

    // pills: chapters | sleep | settings
    bool sleeping = g_sleep_mode != 0;
    ui_pill(PILL_X[0], PILL_Y, PILL_W, PILL_H, 0);
    ui_pill(PILL_X[1], PILL_Y, PILL_W, PILL_H, sleeping);
    ui_pill(PILL_X[2], PILL_Y, PILL_W, PILL_H, 0);

    ui_icon_list(PILL_X[0] + 20, PILL_Y + PILL_H / 2, 7, UI_TEXT);
    ui_text("Chapters", PILL_X[0] + 36, PILL_Y + 11, 0.42f, UI_TEXT, UI_LEFT);

    u32 sc = sleeping ? ui_accent : UI_TEXT;
    ui_icon_moon(PILL_X[1] + 20, PILL_Y + PILL_H / 2, 7, sc, ui_pill_bg(sleeping));
    if (g_sleep_mode == 0) snprintf(s, sizeof s, "Sleep");
    else if (g_sleep_mode == 5) snprintf(s, sizeof s, "Chapter");
    else {
        int64_t rem = (int64_t)g_sleep_end - (int64_t)now;
        snprintf(s, sizeof s, "%dm", (int)((rem > 0 ? rem : 0) / 60000) + 1);
    }
    ui_text(s, PILL_X[1] + 36, PILL_Y + 11, 0.42f, sc, UI_LEFT);

    ui_icon_gear(PILL_X[2] + 20, PILL_Y + PILL_H / 2, UI_TEXT, ui_pill_bg(0));
    ui_text("Settings", PILL_X[2] + 36, PILL_Y + 11, 0.42f, UI_TEXT, UI_LEFT);

    ui_text("A play   L/R chapter   X lists   Y sleep   SELECT bookmark", 160, 229, 0.3f, ui_pal.line, UI_CENTER);
}

/* ------------------------------------------------------ drawing: chapter / bookmark lists */

static void clamp_ch_scroll(void) {
    int max = g_book->nchapters - LIST_VIS;
    if (max < 0) max = 0;
    if (g_ch_scroll > max) g_ch_scroll = max;
    if (g_ch_scroll < 0) g_ch_scroll = 0;
}

static void ensure_ch_visible(void) {
    if (g_ch_sel < g_ch_scroll) g_ch_scroll = g_ch_sel;
    if (g_ch_sel >= g_ch_scroll + LIST_VIS) g_ch_scroll = g_ch_sel - LIST_VIS + 1;
    clamp_ch_scroll();
}

static void ensure_bm_visible(void) {
    int total = g_nbm + 1;
    if (g_bm_sel < g_bm_scroll) g_bm_scroll = g_bm_sel;
    if (g_bm_sel >= g_bm_scroll + LIST_VIS) g_bm_scroll = g_bm_sel - LIST_VIS + 1;
    int max = total - LIST_VIS;
    if (max < 0) max = 0;
    if (g_bm_scroll > max) g_bm_scroll = max;
    if (g_bm_scroll < 0) g_bm_scroll = 0;
}

static void open_lists(int tab) {
    g_list_tab = tab;
    g_ch_sel = g_cur_ch;
    ensure_ch_visible();
    refresh_bookmarks();
    ensure_bm_visible();
    g_state = S_CHAPTERS;
}

static void add_bookmark(void) {
    if (!g_book) return;
    uint64_t pos = player_position_ms();
    char t[24], m[64];
    int rc = bm_add(g_book_path, pos, g_book->chapters[g_cur_ch].title);
    ui_fmt_time(t, sizeof t, pos, 1);
    if (rc == 1) { snprintf(m, sizeof m, "Bookmarked  %s", t); toast(m); }
    else if (rc == 0) toast("Already bookmarked here");
    else toast("Bookmark list is full");
    refresh_bookmarks();
}

static void jump_bookmark(int i) {
    if (i < 0 || i >= g_nbm) return;
    player_seek_ms(g_bm[i].pos);
    if (!player_is_playing()) player_play();
    g_state = S_PLAYER;
}

static void delete_bookmark(int i) {
    if (i < 0 || i >= g_nbm) return;
    bm_remove(g_book_path, g_bm[i].pos);
    refresh_bookmarks();
    ensure_bm_visible();
    toast("Bookmark deleted");
}

static void draw_scrollbar(int total, int scroll) {
    if (total <= LIST_VIS) return;
    float track = LIST_VIS * ROW_H - 8;
    float th = track * LIST_VIS / total;
    if (th < 14) th = 14;
    float ty = LIST_Y + 4 + (track - th) * scroll / (total - LIST_VIS);
    ui_rrect(313, ty, 4, th, ui_style->r_card > 0 ? 2 : 0, ui_style->outline ? ui_accent_dim : ui_pal.surface2);
}

static void draw_lists_bottom(uint64_t pos) {
    ui_scene(ui_bot);

    ui_back_button(8, 4, 64, 26);
    ui_tab_container(78, 3, 236, 29);
    for (int t = 0; t < 2; t++) {
        float x = t ? 196 : 80, w = t ? 116 : 112;
        char lab[32];
        if (t == 0) snprintf(lab, sizeof lab, "Chapters");
        else if (g_nbm) snprintf(lab, sizeof lab, "Bookmarks (%d)", g_nbm);
        else snprintf(lab, sizeof lab, "Bookmarks");
        ui_tab(x, 5, w, 26, lab, g_list_tab == t);
    }

    char s[48];
    int total, scroll;
    if (g_list_tab == 0) {
        total = g_book->nchapters;
        scroll = g_ch_scroll;
        for (int r = 0; r < LIST_VIS; r++) {
            int i = scroll + r;
            if (i >= total) break;
            float y = LIST_Y + r * ROW_H;
            bool cur = (i == g_cur_ch), sel = (i == g_ch_sel);
            ui_row(8, y, 298, ROW_H, sel, cur);
            u32 fg = ui_row_fg(sel, cur), dim = cur ? fg : ui_row_dim(sel);

            snprintf(s, sizeof s, "%d", i + 1);
            ui_text(s, 36, y + 9, 0.42f, dim, UI_RIGHT);
            ui_text_fit(g_book->chapters[i].title, 46, y + 8, 196, 0.5f, fg, UI_LEFT);

            uint64_t len = ch_end(i) - ch_start(i);
            ui_fmt_time(s, sizeof s, len, len >= 3600000);
            ui_text(s, 298, y + 10, 0.4f, ui_row_dim(sel), UI_RIGHT);
        }
    } else {
        total = g_nbm + 1;
        scroll = g_bm_scroll;
        for (int r = 0; r < LIST_VIS; r++) {
            int i = scroll + r;
            if (i >= total) break;
            float y = LIST_Y + r * ROW_H;
            bool sel = (i == g_bm_sel);
            ui_row(8, y, 298, ROW_H, sel, 0);
            u32 fg = ui_row_fg(sel, 0), dim = ui_row_dim(sel);
            u32 icon = (ui_style->row == ROW_INVERT && sel) ? UI_DARK : ui_accent;
            if (i == 0) {
                ui_circle(28, y + ROW_H / 2, 9, icon);
                ui_text("+", 28, y + 5, 0.6f, (ui_style->row == ROW_INVERT && sel) ? ui_accent : UI_DARK, UI_CENTER);
                ui_text("Add bookmark here", 46, y + 9, 0.5f, fg, UI_LEFT);
                ui_fmt_time(s, sizeof s, pos, 1);
                ui_text(s, 298, y + 10, 0.4f, dim, UI_RIGHT);
            } else {
                const Bookmark *b = &g_bm[i - 1];
                ui_icon_bookmark(28, y + ROW_H / 2, icon, ui_row_bg(sel));
                ui_text_fit(b->label, 46, y + 4, 200, 0.46f, fg, UI_LEFT);
                ui_fmt_time(s, sizeof s, b->pos, 1);
                ui_text(s, 46, y + 19, 0.34f, dim, UI_LEFT);
                ui_icon_close(290, y + ROW_H / 2, 5, dim);
            }
        }
        if (g_nbm == 0) ui_text("No bookmarks yet", 160, LIST_Y + ROW_H + 22, 0.42f, UI_DIM, UI_CENTER);
    }
    draw_scrollbar(total, scroll);
}

/* -------------------------------------------------------- drawing: library */

static void clamp_lib_scroll(void) {
    int max = g_br.count - LIST_VIS;
    if (max < 0) max = 0;
    if (g_br.scroll > max) g_br.scroll = max;
    if (g_br.scroll < 0) g_br.scroll = 0;
}

static void ensure_lib_visible(void) {
    if (g_br.sel < g_br.scroll) g_br.scroll = g_br.sel;
    if (g_br.sel >= g_br.scroll + LIST_VIS) g_br.scroll = g_br.sel - LIST_VIS + 1;
    clamp_lib_scroll();
}

static void draw_library_top(u64 now) {
    ui_scene(ui_top);
    ui_glow(UI_TOP_W, 150);
    draw_status();

    ui_logo(28, 30, 56);
    ui_text("Narrate", 98, 30, 0.9f, UI_TEXT, UI_LEFT);
    ui_text("audiobook player", 100, 64, 0.42f, UI_DIM, UI_LEFT);

    ui_text_fit(g_br.path, 28, 102, 344, 0.4f, UI_DIM, UI_LEFT);

    ui_card(28, 124, 344, 82);
    if (g_br.count == 0 || g_br.sel >= g_br.count) {
        ui_text("Nothing here yet", 44, 138, 0.55f, UI_TEXT, UI_LEFT);
        ui_text("Copy .m4b files to  sdmc:/AudioBook", 44, 164, 0.42f, UI_DIM, UI_LEFT);
    } else {
        BrEntry *e = &g_br.items[g_br.sel];
        if (e->is_dir) {
            ui_text_fit(strcmp(e->name, "..") ? e->name : "Up one folder", 44, 140, 312, 0.6f, UI_TEXT, UI_LEFT);
            ui_text("Folder", 44, 168, 0.42f, UI_DIM, UI_LEFT);
        } else {
            char title[300];
            snprintf(title, sizeof title, "%s", e->name);
            if (!e->is_cont) {
                char *dot = strrchr(title, '.');
                if (dot) *dot = 0;
            }
            float y0 = 134;
            if (e->is_cont) {
                ui_text("CONTINUE LISTENING", 44, 128, 0.32f, ui_accent, UI_LEFT);
                y0 = 142;
            }
            int n = ui_text_wrap(title, 44, y0, 312, 0.55f, UI_TEXT, e->is_cont ? 1 : 2, 20);

            char p[800], s[64];
            if (e->is_cont) snprintf(p, sizeof p, "%s", g_cont_path);
            else browser_path(&g_br, e->name, p, sizeof p);
            uint64_t pos, dur;
            if (progress_get(p, &pos, &dur) && dur) {
                float f = (float)((double)pos / (double)dur);
                ui_progress(44, y0 + n * 20 + 10, 312, f);
                if (pos + 5000 >= dur) snprintf(s, sizeof s, "Finished");
                else {
                    char left[40];
                    fmt_left(left, sizeof left, dur - pos);
                    snprintf(s, sizeof s, "%d%%  -  %s", (int)(f * 100), left);
                }
                ui_text(s, 44, y0 + n * 20 + 20, 0.38f, UI_DIM, UI_LEFT);
            } else {
                ui_text("Not started", 44, y0 + n * 20 + 8, 0.4f, UI_DIM, UI_LEFT);
            }
        }
    }

    if (!g_audio_ok) {
        ui_text("DSP firmware missing - run DSP1 from the Homebrew Launcher", 200, 214, 0.38f, UI_ERROR, UI_CENTER);
    } else if (g_msg[0] && now < g_msg_until) {
        ui_text_fit(g_msg, 200, 214, 360, 0.42f, UI_ERROR, UI_CENTER);
    } else {
        ui_text("A open   B back   L/R page   SELECT settings   START exit", 200, 214, 0.36f, UI_DIM, UI_CENTER);
    }
    draw_toast(now);
}

static void draw_library_bottom(void) {
    ui_scene(ui_bot);
    ui_text("Library", 14, 8, 0.55f, UI_TEXT, UI_LEFT);
    char s[32];
    snprintf(s, sizeof s, "%d items", g_br.count - (g_have_cont ? 1 : 0));
    ui_text(s, 14 + ui_text_w("Library", 0.55f) + 10, 12, 0.4f, UI_DIM, UI_LEFT);
    ui_round_btn(GEAR_X, 18, 15);
    ui_icon_gear(GEAR_X, 18, UI_TEXT, btn_bg());

    for (int r = 0; r < LIST_VIS; r++) {
        int i = g_br.scroll + r;
        if (i >= g_br.count) break;
        BrEntry *e = &g_br.items[i];
        float y = LIST_Y + r * ROW_H;
        bool sel = (i == g_br.sel);
        ui_row(8, y, 298, ROW_H, sel, 0);
        u32 fg = ui_row_fg(sel, 0), dim = ui_row_dim(sel);
        u32 bg = ui_row_bg(sel);
        bool inv = (ui_style->row == ROW_INVERT && sel);
        u32 track = inv ? ui_pal.on_accent : ui_pal.surface2;
        u32 fill = inv ? ui_pal.on_accent : ui_accent;

        if (e->is_cont) {
            uint64_t pos = 0, dur = 0;
            progress_get(g_cont_path, &pos, &dur);
            ui_circle(28, y + ROW_H / 2, 10, inv ? ui_pal.on_accent : ui_accent);
            ui_icon_play(29, y + ROW_H / 2, 4.5f, inv ? ui_accent : UI_DARK);
            ui_text("CONTINUE LISTENING", 46, y + 2, 0.3f, inv ? ui_pal.on_accent : ui_accent, UI_LEFT);
            ui_text_fit(e->name, 46, y + 11, 250, 0.44f, fg, UI_LEFT);
            ui_bar(46, y + 29, 250, 3, dur ? (float)((double)pos / (double)dur) : 0, track, fill);
            continue;
        }
        u32 icon = sel ? (inv ? ui_pal.on_accent : ui_accent) : ui_pal.dim;
        if (e->is_dir) ui_icon_folder(28, y + ROW_H / 2, icon);
        else ui_icon_book(28, y + ROW_H / 2, icon, bg);

        char name[300];
        snprintf(name, sizeof name, "%s", strcmp(e->name, "..") ? e->name : "..  (up)");
        if (!e->is_dir) {
            char *dot = strrchr(name, '.');
            if (dot) *dot = 0;
        }
        bool has_prog = false;
        float frac = 0;
        if (!e->is_dir) {
            char p[800];
            uint64_t pos, dur;
            browser_path(&g_br, e->name, p, sizeof p);
            if (progress_get(p, &pos, &dur) && dur) { has_prog = true; frac = (float)((double)pos / (double)dur); }
        }
        (void)dim;
        ui_text_fit(name, 46, y + (has_prog ? 4 : 9), 250, 0.5f, fg, UI_LEFT);
        if (has_prog) ui_bar(46, y + 25, 250, 3, frac, track, fill);
    }
    draw_scrollbar(g_br.count, g_br.scroll);
}

/* ------------------------------------------------------------ drawing: settings */

#define SET_ROWS 8
#define SET_ROW_Y 32
#define SET_ROW_H 26

static void set_value_text(int row, char *out, size_t n) {
    static const char *FLIPS[4] = {"None", "Vertical", "Horizontal", "Both"};
    switch (row) {
    case 0: snprintf(out, n, "%s", theme_style_name(g_set.style)); break;
    case 1: snprintf(out, n, "%s", g_set.dark ? "Dark" : "Light"); break;
    case 2: snprintf(out, n, "%s", theme_accent_name(g_set.accent)); break;
    case 3: snprintf(out, n, "%s", g_set.icons == 0 ? "Style default" : g_set.icons == 1 ? "Solid" : "Outline"); break;
    case 4: snprintf(out, n, "%d s", SKIP_BACK_OPTS[g_set.skip_back]); break;
    case 5: snprintf(out, n, "%d s", SKIP_FWD_OPTS[g_set.skip_fwd]); break;
    case 6: snprintf(out, n, "%s", FLIPS[g_set.cover_flip & 3]); break;
    default: snprintf(out, n, "%d deg", 90 * (g_set.cover_rot & 3)); break;
    }
}

static void set_change(int row, int dir) {
    switch (row) {
    case 0: g_set.style = (g_set.style + dir + STYLE_COUNT) % STYLE_COUNT; break;
    case 1: g_set.dark = !g_set.dark; break;
    case 2: g_set.accent = (g_set.accent + dir + ACCENT_COUNT) % ACCENT_COUNT; break;
    case 3: g_set.icons = (g_set.icons + dir + 3) % 3; break;
    case 4: g_set.skip_back = (g_set.skip_back + dir + 3) % 3; break;
    case 5: g_set.skip_fwd = (g_set.skip_fwd + dir + 3) % 3; break;
    case 6: g_set.cover_flip = (g_set.cover_flip + dir + 4) % 4; break;
    default: g_set.cover_rot = (g_set.cover_rot + dir + 4) % 4; break;
    }
    cover_set_orient(g_set.cover_flip, g_set.cover_rot);
    theme_apply();
    theme_save();
}

static void open_settings(State from) {
    g_settings_from = from;
    g_set_sel = 0;
    g_state = S_SETTINGS;
}

static void draw_settings_top(u64 now) {
    Np np = {"The Magician's Nephew", "C. S. Lewis", "The Wrong Door", 2, 15, 0.42f, 6 * 3600000ull + 20 * 60000ull};
    draw_np_top(&np, now, true);
}

static void draw_settings_bottom(void) {
    static const char *LABELS[SET_ROWS] = {"Style", "Appearance", "Accent", "Icons", "Skip back", "Skip forward", "Cover flip", "Cover rotate"};
    ui_scene(ui_bot);
    ui_back_button(8, 3, 64, 26);
    ui_text("Settings", 84, 7, 0.55f, UI_TEXT, UI_LEFT);

    for (int r = 0; r < SET_ROWS; r++) {
        float y = SET_ROW_Y + r * SET_ROW_H;
        bool sel = (r == g_set_sel);
        ui_row(8, y, 298, SET_ROW_H, sel, 0);
        u32 fg = ui_row_fg(sel, 0), dim = ui_row_dim(sel);
        ui_text(LABELS[r], 20, y + 5, 0.46f, fg, UI_LEFT);

        char v[48];
        set_value_text(r, v, sizeof v);
        float cy = y + SET_ROW_H / 2;
        C2D_DrawTriangle(172, cy, dim, 178, cy - 5, dim, 178, cy + 5, dim, 0.5f);
        C2D_DrawTriangle(298, cy, dim, 292, cy - 5, dim, 292, cy + 5, dim, 0.5f);
        if (r == 2) {
            ui_circle(190, cy, 6, theme_accent_swatch(g_set.accent));
            ui_text_fit(v, 240, y + 5, 76, 0.42f, fg, UI_CENTER);
        } else {
            ui_text_fit(v, 235, y + 5, 100, 0.42f, fg, UI_CENTER);
        }
    }
}

/* ------------------------------------------------------------------- input */

static void lib_activate(int idx) {
    if (idx < 0 || idx >= g_br.count) return;
    BrEntry e = g_br.items[idx];
    if (e.is_cont) {
        if (!g_audio_ok) { set_msg("Audio unavailable (DSP firmware missing)"); return; }
        char p[800];
        snprintf(p, sizeof p, "%s", g_cont_path);
        open_book(p);
        return;
    }
    if (e.is_dir) {
        if (!strcmp(e.name, "..")) lib_up();
        else lib_enter(e.name);
        return;
    }
    if (!g_audio_ok) { set_msg("Audio unavailable (DSP firmware missing)"); return; }
    char p[800];
    browser_path(&g_br, e.name, p, sizeof p);
    open_book(p);
}

// Shared touch-scrolling for list screens. Returns the row index of a tap (>=0) or -1.
static int list_touch(u32 kd, u32 kh, u32 ku, touchPosition tp, int *scroll, int count) {
    int tapped = -1;
    if (kd & KEY_TOUCH) {
        g_drag.active = true;
        g_drag.moved = false;
        g_drag.sx = tp.px; g_drag.sy = tp.py; g_drag.ly = tp.py;
        g_drag.acc = 0;
    } else if ((kh & KEY_TOUCH) && g_drag.active) {
        int dy = tp.py - g_drag.ly;
        g_drag.ly = tp.py;
        if (abs(tp.py - g_drag.sy) > 8) g_drag.moved = true;
        if (g_drag.moved) {
            g_drag.acc -= dy;
            while (g_drag.acc >= ROW_H) { (*scroll)++; g_drag.acc -= ROW_H; }
            while (g_drag.acc <= -ROW_H) { (*scroll)--; g_drag.acc += ROW_H; }
            int max = count - LIST_VIS;
            if (max < 0) max = 0;
            if (*scroll > max) *scroll = max;
            if (*scroll < 0) *scroll = 0;
        }
    }
    if ((ku & KEY_TOUCH) && g_drag.active) {
        if (!g_drag.moved && g_drag.sy >= LIST_Y && g_drag.sx < 312) {
            int row = (g_drag.sy - LIST_Y) / ROW_H;
            int idx = *scroll + row;
            if (row < LIST_VIS && idx < count) tapped = idx;
        }
        g_drag.active = false;
    }
    return tapped;
}

static void update_library(u32 kd, u32 kh, u32 ku, touchPosition tp) {
    if (kd & KEY_SELECT) { open_settings(S_LIB); return; }
    if ((kd & KEY_TOUCH) && in_circle(tp.px, tp.py, GEAR_X, 18, 22)) { open_settings(S_LIB); return; }

    if (g_br.count) {
        if (key_rep(kd, kh, KEY_UP) && g_br.sel > 0) g_br.sel--;
        if (key_rep(kd, kh, KEY_DOWN) && g_br.sel < g_br.count - 1) g_br.sel++;
        if (kd & KEY_L) g_br.sel = g_br.sel > LIST_VIS ? g_br.sel - LIST_VIS : 0;
        if (kd & KEY_R) g_br.sel = g_br.sel + LIST_VIS < g_br.count ? g_br.sel + LIST_VIS : g_br.count - 1;
        ensure_lib_visible();
        if (kd & KEY_A) lib_activate(g_br.sel);
    }
    if (kd & (KEY_B | KEY_LEFT)) {
        lib_up();   // stops at the AudioBook folder
    }
    int tap = list_touch(kd, kh, ku, tp, &g_br.scroll, g_br.count);
    if (tap >= 0) { g_br.sel = tap; lib_activate(tap); }
}

static void list_activate(int idx) {
    if (g_list_tab == 0) {
        goto_chapter(idx);
        if (!player_is_playing()) player_play();
        g_state = S_PLAYER;
    } else if (idx == 0) {
        add_bookmark();
    } else {
        jump_bookmark(idx - 1);
    }
}

static void update_lists(u32 kd, u32 kh, u32 ku, touchPosition tp) {
    // touch header: Back button and tabs
    if ((kd & KEY_TOUCH) && tp.py < LIST_Y - 2) {
        if (in_rect(tp.px, tp.py, 0, 0, 76, LIST_Y - 2)) { g_state = S_PLAYER; return; }
        if (in_rect(tp.px, tp.py, 78, 0, 116, LIST_Y - 2)) { g_list_tab = 0; return; }
        if (in_rect(tp.px, tp.py, 194, 0, 126, LIST_Y - 2)) { g_list_tab = 1; ensure_bm_visible(); return; }
    }
    if (kd & (KEY_B | KEY_X)) { g_state = S_PLAYER; return; }
    if (kd & KEY_LEFT) { g_list_tab = 0; return; }
    if (kd & KEY_RIGHT) { g_list_tab = 1; ensure_bm_visible(); return; }

    bool bm = (g_list_tab == 1);
    int *sel = bm ? &g_bm_sel : &g_ch_sel;
    int *scroll = bm ? &g_bm_scroll : &g_ch_scroll;
    int count = bm ? g_nbm + 1 : g_book->nchapters;

    if (key_rep(kd, kh, KEY_UP) && *sel > 0) (*sel)--;
    if (key_rep(kd, kh, KEY_DOWN) && *sel < count - 1) (*sel)++;
    if (kd & KEY_L) *sel = *sel > LIST_VIS ? *sel - LIST_VIS : 0;
    if (kd & KEY_R) *sel = *sel + LIST_VIS < count ? *sel + LIST_VIS : count - 1;
    if (kd & (KEY_UP | KEY_DOWN | KEY_L | KEY_R)) { if (bm) ensure_bm_visible(); else ensure_ch_visible(); }

    if (kd & KEY_A) { list_activate(*sel); return; }
    if (bm && (kd & KEY_Y) && *sel > 0) delete_bookmark(*sel - 1);

    int tap = list_touch(kd, kh, ku, tp, scroll, count);
    if (tap >= 0) {
        if (bm && tap > 0 && g_drag.sx >= 268) delete_bookmark(tap - 1);   // the x on the right
        else { *sel = tap; list_activate(tap); }
    }
}

static void leave_settings(void) {
    g_state = (g_settings_from == S_PLAYER && g_book) ? S_PLAYER : S_LIB;
}

static void update_settings(u32 kd, u32 kh, u32 ku, touchPosition tp) {
    (void)ku;
    if (kd & (KEY_B | KEY_SELECT)) { leave_settings(); return; }
    if ((kd & KEY_TOUCH) && in_rect(tp.px, tp.py, 0, 0, 76, SET_ROW_Y - 1)) { leave_settings(); return; }

    if (key_rep(kd, kh, KEY_UP) && g_set_sel > 0) g_set_sel--;
    if (key_rep(kd, kh, KEY_DOWN) && g_set_sel < SET_ROWS - 1) g_set_sel++;
    if (key_rep(kd, kh, KEY_LEFT)) set_change(g_set_sel, -1);
    if (key_rep(kd, kh, KEY_RIGHT) || (kd & KEY_A)) set_change(g_set_sel, +1);

    if ((kd & KEY_TOUCH) && tp.py >= SET_ROW_Y) {
        int row = (tp.py - SET_ROW_Y) / SET_ROW_H;
        if (row >= 0 && row < SET_ROWS) {
            g_set_sel = row;
            if (tp.px >= 236) set_change(row, +1);
            else if (tp.px >= 160) set_change(row, -1);
        }
    }
}

static void seek_from_touch(int px) {
    float f = (px - BAR_X) / (float)BAR_W;
    g_seek_frac = f < 0 ? 0 : f > 1 ? 1 : f;
}

static void update_player(u32 kd, u32 kh, u32 ku, touchPosition tp) {
    if (kd & KEY_A) do_toggle();
    if (key_rep(kd, kh, KEY_LEFT)) seek_rel(-skip_back_ms());
    if (key_rep(kd, kh, KEY_RIGHT)) seek_rel(skip_fwd_ms());
    if (kd & KEY_L) prev_chapter();
    if (kd & KEY_R) next_chapter();
    if (key_rep(kd, kh, KEY_UP)) change_volume(+1);
    if (key_rep(kd, kh, KEY_DOWN)) change_volume(-1);
    if (kd & KEY_Y) cycle_sleep();
    if (kd & KEY_SELECT) add_bookmark();
    if (kd & KEY_X) { open_lists(0); return; }
    if (kd & KEY_B) { close_book(); return; }

    if (kd & KEY_TOUCH) {
        int x = tp.px, y = tp.py;
        if (in_rect(x, y, BAR_X - 12, BAR_Y - 18, BAR_W + 24, 44)) {
            g_seeking = true;
            seek_from_touch(x);
        } else if (in_circle(x, y, BM_X, HDR_Y, 24)) add_bookmark();
        else if (in_circle(x, y, OFF_X, HDR_Y, 24)) screen_off(true);
        else if (in_circle(x, y, BTN_X[2], CTRL_Y, 38)) do_toggle();
        else if (in_circle(x, y, BTN_X[1], CTRL_Y, 26)) seek_rel(-skip_back_ms());
        else if (in_circle(x, y, BTN_X[3], CTRL_Y, 26)) seek_rel(skip_fwd_ms());
        else if (in_circle(x, y, BTN_X[0], CTRL_Y, 26)) prev_chapter();
        else if (in_circle(x, y, BTN_X[4], CTRL_Y, 26)) next_chapter();
        else if (in_rect(x, y, PILL_X[0], PILL_Y, PILL_W, PILL_H)) open_lists(0);
        else if (in_rect(x, y, PILL_X[1], PILL_Y, PILL_W, PILL_H)) cycle_sleep();
        else if (in_rect(x, y, PILL_X[2], PILL_Y, PILL_W, PILL_H)) { open_settings(S_PLAYER); return; }
    } else if (g_seeking && (kh & KEY_TOUCH)) {
        seek_from_touch(tp.px);
    }
    if (g_seeking && (ku & KEY_TOUCH)) {
        uint64_t cs = ch_start(g_cur_ch), ce = ch_end(g_cur_ch);
        player_seek_ms(cs + (uint64_t)(g_seek_frac * (float)(ce > cs ? ce - cs : 0)));
        g_seeking = false;
    }
}

/* -------------------------------------------------------------------- main */

int main(void) {
    osSetSpeedupEnable(true);
    mkdir("sdmc:/3ds", 0777);
    mkdir(DATA_DIR, 0777);
    log_reset();
    nlog("Narrate starting");
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    theme_load();
    cover_set_orient(g_set.cover_flip, g_set.cover_rot);
    ui_init();
    nlog("ui ready; app mem free=%lu, linear free=%lu", (unsigned long)osGetMemRegionFree(MEMREGION_APPLICATION), (unsigned long)linearSpaceFree());

    g_lcd_ok = R_SUCCEEDED(gspLcdInit());
    g_ptmu_ok = R_SUCCEEDED(ptmuInit());
    g_mcu_ok = R_SUCCEEDED(mcuHwcInit());
    nlog("lcd=%d ptmu=%d mcu=%d", (int)g_lcd_ok, (int)g_ptmu_ok, (int)g_mcu_ok);

    mkdir(LIB_ROOT, 0777);
    progress_load();
    bm_load();
    int prc = player_init();
    nlog("player_init rc=%d", prc);
    g_audio_ok = (prc == 0);

    browser_set_root(&g_br, LIB_ROOT);
    refresh_continue();
    lib_open_dir(LIB_ROOT);
    nlog("library: %d entries in %s", g_br.count, g_br.path);

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown(), kh = hidKeysHeld(), ku = hidKeysUp();
        touchPosition tp = {0, 0};
        if (kh & KEY_TOUCH) hidTouchRead(&tp);

        u64 now = osGetTime();
        update_status(now);

        if (g_screen_off) {
            if (kd || (kh & KEY_TOUCH)) screen_off(false);
            kd = kh = ku = 0;      // swallow the wake-up press
        }
        if (kd & KEY_START) break;

        switch (g_state) {
        case S_LIB:      update_library(kd, kh, ku, tp); break;
        case S_PLAYER:   if (g_book) update_player(kd, kh, ku, tp); break;
        case S_CHAPTERS: if (g_book) update_lists(kd, kh, ku, tp); break;
        case S_SETTINGS: update_settings(kd, kh, ku, tp); break;
        }
        tick_playback(now);

        if (g_screen_off) {
            gspWaitForVBlank();
            continue;
        }

        // Only redraw when something visible changed (or while the user is interacting). On the old 2DS/3DS
        // this frees a lot of CPU/GPU time and memory bandwidth for the audio decoder.
        if (kh || kd || ku) g_active_until = now + 350;
        uint64_t spos = g_book ? player_position_ms() : 0;
        u64 sig = 1469598103934665603ULL;
#define MIX(v) sig = (sig ^ (u64)(v)) * 1099511628211ULL
        MIX(g_state); MIX(g_book != NULL); MIX(g_book ? player_is_playing() : 0); MIX(g_cur_ch); MIX(spos / 1000);
        MIX(g_sleep_mode); MIX(g_sleep_mode ? (g_sleep_end > now ? (g_sleep_end - now) / 60000 : 0) : 0);
        MIX(now < g_toast_until); MIX(now < g_msg_until); MIX(g_slider); MIX(g_batt); MIX(g_charging);
        MIX(g_nbm); MIX((u64)time(NULL) / 60); MIX(g_seeking);
#undef MIX
        static u64 last_sig;
        static unsigned since_draw;
        if (sig == last_sig && now >= g_active_until && since_draw < 14) {
            since_draw++;
            gspWaitForVBlank();
            continue;
        }
        since_draw = 0;
        last_sig = sig;

        ui_begin();
        if (g_state == S_SETTINGS) {
            draw_settings_top(now);
            draw_settings_bottom();
        } else if (g_state == S_LIB || !g_book) {
            draw_library_top(now);
            draw_library_bottom();
        } else {
            draw_player_top(spos, now);
            if (g_state == S_CHAPTERS) draw_lists_bottom(spos);
            else draw_player_bottom(spos, now);
        }
        ui_end();
    }

    if (g_book) save_progress();
    screen_off(false);
    player_exit();
    cover_free();
    browser_free(&g_br);
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    if (g_mcu_ok) mcuHwcExit();
    if (g_ptmu_ok) ptmuExit();
    if (g_lcd_ok) gspLcdExit();
    gfxExit();
    return 0;
}
