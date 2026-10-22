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
#include "decks.h"
#include "thumbs.h"
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
#define SORT_X    258

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
static int      g_set_sel, g_set_scroll;
static bool     g_lid_blocked, g_bright_known, g_bright_drag;
static int      g_bright_pct = 100;
static u32      g_orig_bright[2];
static M4b      g_sample;             // fake book used for the live preview in Settings
static M4bChapter g_sample_ch[6];
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

/* ----------------------------------------- bottom-screen layouts: hit areas */

enum { A_NONE, A_PLAY, A_BACK, A_FWD, A_PREV, A_NEXT, A_CHAPTERS, A_SLEEP, A_SETTINGS, A_BOOKMARK, A_SCREEN_OFF };
typedef struct { int act; short x, y, w, h; } Hit;
enum { BF_CLASSIC, BF_BIG, BF_NIGHT, BF_ZEN, BF_DECK, BF_COUNT };
typedef struct { const Hit *hits; int n; short seek[4]; short bright[4]; } BDef;

#define SEEK_PAD 12      // the seek bar is inset from its touch area by this much on each side

static const Hit H_CLASSIC[] = {
    {A_SCREEN_OFF, 9, 5, 34, 34}, {A_BOOKMARK, 277, 5, 34, 34},
    {A_PREV, 10, 108, 52, 52}, {A_BACK, 70, 110, 48, 48}, {A_PLAY, 122, 96, 76, 72}, {A_FWD, 202, 110, 48, 48}, {A_NEXT, 258, 108, 52, 52},
    {A_CHAPTERS, 8, 190, 96, 38}, {A_SLEEP, 112, 190, 96, 38}, {A_SETTINGS, 216, 190, 96, 38}};
static const Hit H_BIG[] = {
    {A_BACK, 8, 50, 92, 88}, {A_PLAY, 104, 48, 112, 92}, {A_FWD, 220, 50, 92, 88},
    {A_PREV, 8, 144, 92, 34}, {A_CHAPTERS, 104, 144, 112, 34}, {A_NEXT, 220, 144, 92, 34},
    {A_SLEEP, 8, 206, 74, 32}, {A_BOOKMARK, 86, 206, 74, 32}, {A_SETTINGS, 164, 206, 74, 32}, {A_SCREEN_OFF, 242, 206, 70, 32}};
static const Hit H_NIGHT[] = {
    {A_SCREEN_OFF, 6, 4, 70, 40}, {A_CHAPTERS, 86, 4, 70, 40}, {A_BOOKMARK, 164, 4, 70, 40}, {A_SETTINGS, 242, 4, 70, 40},
    {A_BACK, 16, 110, 84, 62}, {A_PLAY, 110, 104, 100, 74}, {A_FWD, 220, 110, 84, 62}, {A_SLEEP, 16, 186, 92, 46}};
static const Hit H_ZEN[] = {
    {A_SCREEN_OFF, 6, 4, 56, 38}, {A_SLEEP, 68, 4, 56, 38}, {A_CHAPTERS, 130, 4, 56, 38}, {A_BOOKMARK, 192, 4, 56, 38}, {A_SETTINGS, 254, 4, 56, 38},
    {A_BACK, 18, 84, 80, 70}, {A_PLAY, 110, 66, 100, 100}, {A_FWD, 222, 84, 80, 70}};
static const Hit H_DECK[] = {
    {A_SCREEN_OFF, 6, 6, 40, 40}, {A_BOOKMARK, 274, 6, 40, 40},
    {A_PREV, 8, 110, 58, 56}, {A_BACK, 70, 110, 58, 56}, {A_PLAY, 132, 110, 58, 56}, {A_FWD, 194, 110, 58, 56}, {A_NEXT, 256, 110, 58, 56},
    {A_CHAPTERS, 8, 192, 96, 38}, {A_SLEEP, 112, 192, 96, 38}, {A_SETTINGS, 216, 192, 96, 38}};

#define NHITS(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const BDef BD[BF_COUNT] = {
    {H_CLASSIC, NHITS(H_CLASSIC), {12, 48, 296, 44}, {40, 170, 240, 18}},
    {H_BIG,     NHITS(H_BIG),     {8, 4, 304, 44},   {20, 182, 280, 22}},
    {H_NIGHT,   NHITS(H_NIGHT),   {16, 56, 288, 40}, {118, 190, 186, 40}},
    {H_ZEN,     NHITS(H_ZEN),     {16, 196, 288, 38}, {40, 172, 240, 22}},
    {H_DECK,    NHITS(H_DECK),    {12, 60, 296, 40}, {20, 170, 280, 18}},
};

static int bottom_family(int lay) {
    switch (lay) {
    case LAY_BIG: return BF_BIG;
    case LAY_NIGHT: return BF_NIGHT;
    case LAY_ZEN: return BF_ZEN;
    case LAY_CASSETTE: case LAY_REEL: return BF_DECK;
    default: return BF_CLASSIC;
    }
}

static const Hit *hit_of(int fam, int act) {
    const BDef *bd = &BD[fam];
    for (int i = 0; i < bd->n; i++) if (bd->hits[i].act == act) return &bd->hits[i];
    return NULL;
}

static int hit_test(int fam, int x, int y) {
    const BDef *bd = &BD[fam];
    for (int i = 0; i < bd->n; i++) {
        const Hit *h = &bd->hits[i];
        if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h) return h->act;
    }
    return A_NONE;
}

/* ------------------------------------------------ brightness / lid-closed playback */

#define BR_MIN 0x10
#define BR_MAX 0xAC

static void apply_brightness(int pct) {
    if (!g_lcd_ok) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    u32 raw = BR_MIN + (u32)pct * (BR_MAX - BR_MIN) / 100;
    GSPLCD_SetBrightnessRaw(GSPLCD_SCREEN_TOP, raw);
    GSPLCD_SetBrightnessRaw(GSPLCD_SCREEN_BOTTOM, raw);
    g_bright_pct = pct;
}

static void init_brightness(void) {
    if (!g_lcd_ok) return;
    u32 t = 0, b = 0;
    if (R_SUCCEEDED(GSPLCD_GetBrightness(GSPLCD_SCREEN_TOP, &t))) {
        g_orig_bright[0] = t;
        g_bright_known = true;
        int pct = t <= BR_MIN ? 0 : (int)((t - BR_MIN) * 100 / (BR_MAX - BR_MIN));
        g_bright_pct = pct > 100 ? 100 : pct;
    }
    if (R_SUCCEEDED(GSPLCD_GetBrightness(GSPLCD_SCREEN_BOTTOM, &b))) g_orig_bright[1] = b;
    else g_orig_bright[1] = g_orig_bright[0];
    if (g_set.bright >= 0) apply_brightness(g_set.bright);
}

static void restore_brightness(void) {
    if (g_lcd_ok && g_bright_known && g_set.bright >= 0) {
        GSPLCD_SetBrightnessRaw(GSPLCD_SCREEN_TOP, g_orig_bright[0]);
        GSPLCD_SetBrightnessRaw(GSPLCD_SCREEN_BOTTOM, g_orig_bright[1]);
    }
}

// Keep the console awake while a book plays so closing the lid doesn't stop the audio; when playback stops,
// allow sleep again (and sleep right away if the lid is already closed).
static void update_lid(void) {
    bool want = g_set.lidplay && g_book && player_is_playing();
    if (want == g_lid_blocked) return;
    g_lid_blocked = want;
    aptSetSleepAllowed(!want);
    if (!want) APT_SleepIfShellClosed();
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
    g_br.sort = g_set.sort;
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
    if (g_book->cover && cover_load(g_book->cover, g_book->cover_len, path) && cover_accent(&r, &g, &b)) {
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
    if (!g_book) { update_lid(); return; }
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
        char m[320], next[800];
        bool have_next = false;
        snprintf(m, sizeof m, "Finished: %s", g_book->title);
        player_pause();
        progress_set(g_book_path, player_duration_ms(), player_duration_ms());
        progress_save();
        if (g_set.autocont && g_audio_ok) have_next = browser_next_book(g_book_path, next, sizeof next);
        close_book();
        if (have_next) {                                  // series: carry straight on with the next unfinished book
            open_book(next);
            if (g_book) { toast("Next book of the series"); update_lid(); return; }
        }
        set_msg(m);
    }
    update_lid();
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

typedef struct {
    const M4b *book;
    int ch;
    float frac;
    uint64_t left_ms, elapsed, clen;
    bool playing;
} TopData;

static void draw_sleep_hint(u64 now) {
    if (!g_sleep_mode) return;
    char z[48];
    if (g_sleep_mode == 5) snprintf(z, sizeof z, "Sleep: end of chapter");
    else {
        int64_t rem = (int64_t)g_sleep_end - (int64_t)now;
        snprintf(z, sizeof z, "Sleep in %dm", (int)((rem > 0 ? rem : 0) / 60000) + 1);
    }
    ui_text(z, 12, 6, 0.4f, ui_accent, UI_LEFT);
}

static void top_big(const TopData *t) {
    const M4b *b = t->book;
    const float cx = 16, cy = 48, cs = 128;
    if (cover_ready()) cover_draw(cx, cy, cs);
    else ui_cover_placeholder(cx, cy, cs, b->title);

    const float x0 = 160, w = 224;
    ui_text("NOW PLAYING", x0, 30, 0.4f, ui_accent, UI_LEFT);
    ui_text_wrap(b->chapters[t->ch].title, x0, 46, w, 0.78f, UI_TEXT, 2, 28);
    int hrs = t->clen >= 3600000;
    char s[32], r[32], rem[40];
    ui_fmt_time(s, sizeof s, t->elapsed, hrs);
    ui_text(s, x0, 108, 1.4f, ui_accent, UI_LEFT);
    ui_fmt_time(r, sizeof r, t->clen > t->elapsed ? t->clen - t->elapsed : 0, hrs);
    snprintf(rem, sizeof rem, "-%s", r);
    ui_text(rem, x0, 160, 0.75f, UI_DIM, UI_LEFT);
    ui_progress(x0, 196, w, t->frac);
    ui_text_fit(b->title, x0, 208, w, 0.42f, UI_DIM, UI_LEFT);
}

static void top_night(const TopData *t) {
    const M4b *b = t->book;
    int hrs = t->clen >= 3600000;
    char s[32], left[40];
    ui_fmt_time(s, sizeof s, t->elapsed, hrs);
    ui_text(s, 200, 64, 2.0f, UI_TEXT, UI_CENTER);
    ui_text_fit(b->chapters[t->ch].title, 200, 140, 340, 0.55f, UI_DIM, UI_CENTER);
    fmt_left(left, sizeof left, t->left_ms);
    ui_text(left, 200, 166, 0.48f, UI_DIM, UI_CENTER);
    ui_progress(60, 198, 280, t->frac);
    ui_text_fit(b->title, 200, 212, 340, 0.4f, UI_DIM, UI_CENTER);
}

static void top_zen(const TopData *t) {
    const M4b *b = t->book;
    const float cs = 176, cx = (UI_TOP_W - cs) / 2, cy = 10;
    if (cover_ready()) cover_draw(cx, cy, cs);
    else ui_cover_placeholder(cx, cy, cs, b->title);
    ui_text_fit(b->title, 200, 194, 360, 0.52f, UI_TEXT, UI_CENTER);
    ui_text_fit(b->chapters[t->ch].title, 200, 214, 360, 0.4f, UI_DIM, UI_CENTER);
}

// Draws the whole top screen for the active layout. 'preview' = settings preview (sample data, always animating).
static void draw_top(const TopData *t, u64 now, bool preview) {
    int lay = theme_layout();
    if (lay == LAY_CLASSIC) {
        Np np = {t->book->title, t->book->artist, t->book->chapters[t->ch].title, t->ch, t->book->nchapters, t->frac, t->left_ms};
        draw_np_top(&np, now, preview);
        return;
    }
    ui_scene(ui_top);
    ui_glow(UI_TOP_W, 150);
    DeckInfo d = {t->book, t->ch, t->frac, t->left_ms, t->elapsed, t->clen, t->playing, now};
    switch (lay) {
    case LAY_BIG: top_big(t); break;
    case LAY_NIGHT: top_night(t); break;
    case LAY_ZEN: top_zen(t); break;
    case LAY_CASSETTE: deck_cassette(&d); break;
    case LAY_REEL: deck_reel(&d); break;
    case LAY_VINYL: deck_vinyl(&d); break;
    default: deck_quest(&d); break;
    }
    draw_status();
    if (!preview) draw_sleep_hint(now);
    draw_toast(now);
}

static void draw_player_top(uint64_t pos, u64 now) {
    uint64_t dur = player_duration_ms();
    uint64_t cs = ch_start(g_cur_ch), ce = ch_end(g_cur_ch);
    TopData t = {
        g_book, g_cur_ch,
        dur ? (float)((double)pos / (double)dur) : 0,
        dur > pos ? dur - pos : 0,
        pos > cs ? pos - cs : 0, ce > cs ? ce - cs : 1,
        player_is_playing()};
    draw_top(&t, now, false);
}

/* ------------------------------------------------------- drawing: player bottom */

static u32 btn_bg(void) { return ui_style->btn == BTN_FILLED ? ui_pal.surface2 : ui_pal.bg; }

typedef struct { uint64_t cs, ce, clen, shown; float frac; } ChInfo;

static ChInfo chinfo(uint64_t pos) {
    ChInfo c;
    c.cs = ch_start(g_cur_ch);
    c.ce = ch_end(g_cur_ch);
    c.clen = c.ce > c.cs ? c.ce - c.cs : 1;
    if (g_seeking) { c.frac = g_seek_frac; c.shown = c.cs + (uint64_t)(c.frac * c.clen); }
    else { c.shown = pos; c.frac = pos > c.cs ? (float)((double)(pos - c.cs) / (double)c.clen) : 0; }
    if (c.frac > 1) c.frac = 1;
    return c;
}

// seek bar + elapsed / remaining labels inside a seek touch area
static void draw_seek(const short *r, const ChInfo *c) {
    float x = r[0] + SEEK_PAD, w = r[2] - 2 * SEEK_PAD, cy = r[1] + r[3] / 2.0f;
    ui_seekbar(x, cy - ui_style->bar_h / 2, w, c->frac, g_seeking);
    int hrs = c->clen >= 3600000;
    char a[24], b[24], s[32];
    ui_fmt_time(a, sizeof a, c->shown > c->cs ? c->shown - c->cs : 0, hrs);
    ui_text(a, x, cy + 10, 0.42f, UI_DIM, UI_LEFT);
    ui_fmt_time(b, sizeof b, c->ce > c->shown ? c->ce - c->shown : 0, hrs);
    snprintf(s, sizeof s, "-%s", b);
    ui_text(s, x + w, cy + 10, 0.42f, UI_DIM, UI_RIGHT);
}

static void draw_bright(const short *r) {
    if (!g_lcd_ok) return;
    float x = r[0], w = r[2], cy = r[1] + r[3] / 2.0f;
    ui_icon_sun(x + 12, cy, 7, UI_DIM);
    ui_seekbar(x + 30, cy - ui_style->bar_h / 2, w - 68, g_bright_pct / 100.0f, g_bright_drag);
    char s[8];
    snprintf(s, sizeof s, "%d%%", g_bright_pct);
    ui_text(s, x + w - 2, cy - 7, 0.38f, UI_DIM, UI_RIGHT);
}

static void sleep_label(char *s, size_t n, u64 now) {
    if (g_sleep_mode == 0) snprintf(s, n, "Sleep");
    else if (g_sleep_mode == 5) snprintf(s, n, "Chapter");
    else {
        int64_t rem = (int64_t)g_sleep_end - (int64_t)now;
        snprintf(s, n, "%dm", (int)((rem > 0 ? rem : 0) / 60000) + 1);
    }
}

// a pill button with an icon for the given action (+ optional label)
static void action_pill(const Hit *h, const char *label, bool active) {
    float cy = h->y + h->h / 2.0f;
    ui_pill(h->x, h->y, h->w, h->h, active);
    u32 col = active ? ui_accent : UI_TEXT, bg = ui_pill_bg(active);
    float ix = h->x + ((label && label[0]) ? 20 : h->w / 2.0f);
    switch (h->act) {
    case A_CHAPTERS: ui_icon_list(ix, cy, 7, col); break;
    case A_SLEEP: ui_icon_moon(ix, cy, 7, col, bg); break;
    case A_SETTINGS: ui_icon_gear(ix, cy, col, bg); break;
    case A_BOOKMARK: ui_icon_bookmark(ix, cy, g_nbm ? ui_accent : col, bg); break;
    case A_SCREEN_OFF: ui_icon_screen(ix, cy, col, bg); break;
    case A_PREV: ui_icon_prev(ix, cy, 7, col); break;
    case A_NEXT: ui_icon_next(ix, cy, 7, col); break;
    default: break;
    }
    if (label && label[0]) ui_text(label, h->x + 36, cy - 7, 0.42f, col, UI_LEFT);
}

static void skip_labels(char *lb, char *lf, size_t n) {
    snprintf(lb, n, "%d", SKIP_BACK_OPTS[g_set.skip_back]);
    snprintf(lf, n, "%d", SKIP_FWD_OPTS[g_set.skip_fwd]);
}

static float hcx(const Hit *h) { return h->x + h->w / 2.0f; }
static float hcy(const Hit *h) { return h->y + h->h / 2.0f; }

/* ---- classic ---- */
static void draw_bottom_classic(uint64_t pos, u64 now) {
    const BDef *bd = &BD[BF_CLASSIC];
    ui_scene(ui_bot);
    uint64_t dur = player_duration_ms();
    ui_rect(0, 0, UI_BOT_W, 3, ui_pal.line);                    // whole-book progress hairline
    ui_rect(0, 0, UI_BOT_W * (dur ? (float)((double)pos / (double)dur) : 0), 3, ui_accent);

    ChInfo c = chinfo(pos);
    char s[64];
    snprintf(s, sizeof s, "CHAPTER %d OF %d", g_cur_ch + 1, g_book->nchapters);
    const Hit *ho = hit_of(BF_CLASSIC, A_SCREEN_OFF), *hm = hit_of(BF_CLASSIC, A_BOOKMARK);
    ui_round_btn(hcx(ho), hcy(ho), 17);
    ui_icon_screen(hcx(ho), hcy(ho), UI_TEXT, btn_bg());
    ui_round_btn(hcx(hm), hcy(hm), 17);
    ui_icon_bookmark(hcx(hm), hcy(hm), g_nbm ? ui_accent : UI_TEXT, btn_bg());
    ui_text(s, 160, 12, 0.36f, ui_accent, UI_CENTER);
    ui_text_fit(g_book->chapters[g_cur_ch].title, 160, 26, 196, 0.55f, UI_TEXT, UI_CENTER);
    draw_seek(bd->seek, &c);

    char lb[8], lf[8];
    skip_labels(lb, lf, sizeof lb);
    const Hit *hp = hit_of(BF_CLASSIC, A_PREV), *hn = hit_of(BF_CLASSIC, A_NEXT), *hb = hit_of(BF_CLASSIC, A_BACK),
              *hf = hit_of(BF_CLASSIC, A_FWD), *hpl = hit_of(BF_CLASSIC, A_PLAY);
    ui_icon_prev(hcx(hp), hcy(hp), 9, UI_TEXT);
    ui_icon_next(hcx(hn), hcy(hn), 9, UI_TEXT);
    ui_icon_skip(hcx(hb), hcy(hb), 0, lb);
    ui_icon_skip(hcx(hf), hcy(hf), 1, lf);
    ui_play_button(hcx(hpl), hcy(hpl), player_is_playing(), 1.0f);

    char sl[16];
    sleep_label(sl, sizeof sl, now);
    action_pill(hit_of(BF_CLASSIC, A_CHAPTERS), "Chapters", false);
    action_pill(hit_of(BF_CLASSIC, A_SLEEP), sl, g_sleep_mode != 0);
    action_pill(hit_of(BF_CLASSIC, A_SETTINGS), "Settings", false);
    draw_bright(bd->bright);
    ui_text("A play   L/R chapter   X lists   Y sleep   SELECT bookmark", 160, 229, 0.3f, ui_pal.line, UI_CENTER);
}

/* ---- big buttons ---- */
static void big_key(const Hit *h, bool accent) {
    ui_rrect(h->x, h->y, h->w, h->h, 18, accent ? ui_accent : ui_pal.surface2);
}

static void draw_bottom_big(uint64_t pos, u64 now) {
    const BDef *bd = &BD[BF_BIG];
    ui_scene(ui_bot);
    ChInfo c = chinfo(pos);
    draw_seek(bd->seek, &c);

    char lb[8], lf[8], sl[16];
    skip_labels(lb, lf, sizeof lb);
    const Hit *hb = hit_of(BF_BIG, A_BACK), *hp = hit_of(BF_BIG, A_PLAY), *hf = hit_of(BF_BIG, A_FWD);
    big_key(hb, false); big_key(hp, true); big_key(hf, false);
    char t1[12], t2[12];
    snprintf(t1, sizeof t1, "-%s", lb);
    snprintf(t2, sizeof t2, "+%s", lf);
    ui_text(t1, hcx(hb), hcy(hb) - 22, 1.0f, UI_TEXT, UI_CENTER);
    ui_text("sec", hcx(hb), hcy(hb) + 12, 0.45f, UI_DIM, UI_CENTER);
    ui_text(t2, hcx(hf), hcy(hf) - 22, 1.0f, UI_TEXT, UI_CENTER);
    ui_text("sec", hcx(hf), hcy(hf) + 12, 0.45f, UI_DIM, UI_CENTER);
    if (player_is_playing()) ui_icon_pause(hcx(hp), hcy(hp), 24, UI_DARK);
    else ui_icon_play(hcx(hp), hcy(hp), 26, UI_DARK);

    action_pill(hit_of(BF_BIG, A_PREV), "Prev", false);
    action_pill(hit_of(BF_BIG, A_CHAPTERS), "Chapters", false);
    action_pill(hit_of(BF_BIG, A_NEXT), "Next", false);
    sleep_label(sl, sizeof sl, now);
    action_pill(hit_of(BF_BIG, A_SLEEP), NULL, g_sleep_mode != 0);
    action_pill(hit_of(BF_BIG, A_BOOKMARK), NULL, false);
    action_pill(hit_of(BF_BIG, A_SETTINGS), NULL, false);
    action_pill(hit_of(BF_BIG, A_SCREEN_OFF), NULL, false);
    draw_bright(bd->bright);
}

/* ---- night reader / zen: icon-only header, quiet controls ---- */
static void icon_only(int fam, int act, bool active) {
    const Hit *h = hit_of(fam, act);
    if (!h) return;
    action_pill(h, NULL, active);
}

static void draw_bottom_night(uint64_t pos, u64 now) {
    const BDef *bd = &BD[BF_NIGHT];
    ui_scene(ui_bot);
    ChInfo c = chinfo(pos);
    icon_only(BF_NIGHT, A_SCREEN_OFF, false);
    icon_only(BF_NIGHT, A_CHAPTERS, false);
    icon_only(BF_NIGHT, A_BOOKMARK, false);
    icon_only(BF_NIGHT, A_SETTINGS, false);
    draw_seek(bd->seek, &c);

    char lb[8], lf[8], sl[16], t1[12], t2[12];
    skip_labels(lb, lf, sizeof lb);
    snprintf(t1, sizeof t1, "-%s", lb);
    snprintf(t2, sizeof t2, "+%s", lf);
    const Hit *hb = hit_of(BF_NIGHT, A_BACK), *hp = hit_of(BF_NIGHT, A_PLAY), *hf = hit_of(BF_NIGHT, A_FWD);
    ui_pill(hb->x, hb->y, hb->w, hb->h, 0);
    ui_text(t1, hcx(hb), hcy(hb) - 11, 0.8f, UI_TEXT, UI_CENTER);
    ui_pill(hf->x, hf->y, hf->w, hf->h, 0);
    ui_text(t2, hcx(hf), hcy(hf) - 11, 0.8f, UI_TEXT, UI_CENTER);
    ui_pill(hp->x, hp->y, hp->w, hp->h, 1);
    if (player_is_playing()) ui_icon_pause(hcx(hp), hcy(hp), 18, ui_accent);
    else ui_icon_play(hcx(hp), hcy(hp), 20, ui_accent);
    sleep_label(sl, sizeof sl, now);
    action_pill(hit_of(BF_NIGHT, A_SLEEP), sl, g_sleep_mode != 0);
    draw_bright(bd->bright);
}

static void draw_bottom_zen(uint64_t pos, u64 now) {
    const BDef *bd = &BD[BF_ZEN];
    (void)now;
    ui_scene(ui_bot);
    ChInfo c = chinfo(pos);
    icon_only(BF_ZEN, A_SCREEN_OFF, false);
    icon_only(BF_ZEN, A_SLEEP, g_sleep_mode != 0);
    icon_only(BF_ZEN, A_CHAPTERS, false);
    icon_only(BF_ZEN, A_BOOKMARK, false);
    icon_only(BF_ZEN, A_SETTINGS, false);

    char lb[8], lf[8], t1[12], t2[12];
    skip_labels(lb, lf, sizeof lb);
    snprintf(t1, sizeof t1, "-%s", lb);
    snprintf(t2, sizeof t2, "+%s", lf);
    const Hit *hb = hit_of(BF_ZEN, A_BACK), *hp = hit_of(BF_ZEN, A_PLAY), *hf = hit_of(BF_ZEN, A_FWD);
    ui_text(t1, hcx(hb), hcy(hb) - 12, 0.9f, UI_DIM, UI_CENTER);
    ui_text(t2, hcx(hf), hcy(hf) - 12, 0.9f, UI_DIM, UI_CENTER);
    ui_play_button(hcx(hp), hcy(hp), player_is_playing(), 1.3f);
    draw_bright(bd->bright);
    draw_seek(bd->seek, &c);
}

/* ---- cassette / reel deck: counter display + piano keys ---- */
static void deck_key(const Hit *h, bool accent) {
    ui_rrect(h->x, h->y, h->w, h->h, 6, theme_mix(ui_pal.surface2, ui_pal.bg, 0.5f));          // key well
    ui_rrect(h->x + 2, h->y + 2, h->w - 4, h->h - 9, 5, accent ? ui_accent : ui_pal.surface2);   // raised cap
}

static void draw_bottom_deck(uint64_t pos, u64 now) {
    const BDef *bd = &BD[BF_DECK];
    ui_scene(ui_bot);
    ChInfo c = chinfo(pos);

    ui_rrect(56, 8, 208, 46, 8, ui_pal.bg);                       // tape-counter display
    ui_rrect_outline(56, 8, 208, 46, 8, 1.6f, ui_accent_dim, ui_pal.bg);
    char s[32];
    ui_fmt_time(s, sizeof s, c.shown > c.cs ? c.shown - c.cs : 0, c.clen >= 3600000);
    ui_text(s, 160, 11, 1.0f, ui_accent, UI_CENTER);
    snprintf(s, sizeof s, "CH %d/%d", g_cur_ch + 1, g_book->nchapters);
    ui_text(s, 160, 40, 0.34f, UI_DIM, UI_CENTER);
    const Hit *ho = hit_of(BF_DECK, A_SCREEN_OFF), *hm = hit_of(BF_DECK, A_BOOKMARK);
    ui_round_btn(hcx(ho), hcy(ho), 18);
    ui_icon_screen(hcx(ho), hcy(ho), UI_TEXT, btn_bg());
    ui_round_btn(hcx(hm), hcy(hm), 18);
    ui_icon_bookmark(hcx(hm), hcy(hm), g_nbm ? ui_accent : UI_TEXT, btn_bg());
    draw_seek(bd->seek, &c);

    char lb[8], lf[8], t1[12], t2[12], sl[16];
    skip_labels(lb, lf, sizeof lb);
    snprintf(t1, sizeof t1, "-%s", lb);
    snprintf(t2, sizeof t2, "+%s", lf);
    const Hit *hp = hit_of(BF_DECK, A_PREV), *hn = hit_of(BF_DECK, A_NEXT), *hb = hit_of(BF_DECK, A_BACK),
              *hf = hit_of(BF_DECK, A_FWD), *hpl = hit_of(BF_DECK, A_PLAY);
    deck_key(hp, false); deck_key(hb, false); deck_key(hpl, true); deck_key(hf, false); deck_key(hn, false);
    ui_icon_prev(hcx(hp), hcy(hp) - 4, 8, UI_TEXT);
    ui_icon_next(hcx(hn), hcy(hn) - 4, 8, UI_TEXT);
    ui_text(t1, hcx(hb), hcy(hb) - 14, 0.7f, UI_TEXT, UI_CENTER);
    ui_text(t2, hcx(hf), hcy(hf) - 14, 0.7f, UI_TEXT, UI_CENTER);
    if (player_is_playing()) ui_icon_pause(hcx(hpl), hcy(hpl) - 4, 10, UI_DARK);
    else ui_icon_play(hcx(hpl), hcy(hpl) - 4, 11, UI_DARK);

    sleep_label(sl, sizeof sl, now);
    action_pill(hit_of(BF_DECK, A_CHAPTERS), "Chapters", false);
    action_pill(hit_of(BF_DECK, A_SLEEP), sl, g_sleep_mode != 0);
    action_pill(hit_of(BF_DECK, A_SETTINGS), "Settings", false);
    draw_bright(bd->bright);
}

static void draw_player_bottom(uint64_t pos, u64 now) {
    switch (bottom_family(theme_layout())) {
    case BF_BIG: draw_bottom_big(pos, now); break;
    case BF_NIGHT: draw_bottom_night(pos, now); break;
    case BF_ZEN: draw_bottom_zen(pos, now); break;
    case BF_DECK: draw_bottom_deck(pos, now); break;
    default: draw_bottom_classic(pos, now); break;
    }
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
            bool up = !strcmp(e->name, "..");
            ui_text_fit(up ? "Up one folder" : e->name, 44, 134, 312, 0.6f, UI_TEXT, UI_LEFT);
            if (up) {
                ui_text("Folder", 44, 168, 0.42f, UI_DIM, UI_LEFT);
            } else {
                browser_dir_stats(&g_br, g_br.sel);
                if (e->books > 0) {
                    char s[96];
                    snprintf(s, sizeof s, "%d book%s  -  %d finished", e->books, e->books == 1 ? "" : "s", e->done);
                    ui_progress(44, 166, 312, e->sfrac);
                    ui_text(s, 44, 178, 0.42f, UI_DIM, UI_LEFT);
                } else {
                    ui_text("No audiobooks in this folder", 44, 168, 0.42f, UI_DIM, UI_LEFT);
                }
            }
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
        ui_text("A open  B back  X mark done  Y sort  SELECT settings", 200, 214, 0.36f, UI_DIM, UI_CENTER);
    }
    draw_toast(now);
}

static const char *SORT_NAMES[SORT_COUNT] = {"Name", "Recent", "Status"};

static void draw_lib_header(void) {
    ui_text("Library", 14, 8, 0.55f, UI_TEXT, UI_LEFT);
    char s[32];
    snprintf(s, sizeof s, "%d items", g_br.count - (g_have_cont ? 1 : 0));
    ui_text(s, 14 + ui_text_w("Library", 0.55f) + 10, 12, 0.4f, UI_DIM, UI_LEFT);
    ui_round_btn(SORT_X, 18, 15);
    ui_icon_sort(SORT_X, 18, UI_TEXT);
    ui_round_btn(GEAR_X, 18, 15);
    ui_icon_gear(GEAR_X, 18, UI_TEXT, btn_bg());
}

static bool markable(const BrEntry *e) { return !e->is_cont && strcmp(e->name, "..") != 0; }

static void draw_library_list(void) {
    ui_scene(ui_bot);
    draw_lib_header();

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
        bool isdir = e->is_dir;
        if (isdir && markable(e)) browser_dir_stats(&g_br, i);
        u32 icon = sel ? (inv ? ui_pal.on_accent : ui_accent) : ui_pal.dim;
        if (isdir) ui_icon_folder(28, y + ROW_H / 2, icon);
        else ui_icon_book(28, y + ROW_H / 2, icon, bg);

        char name[300];
        snprintf(name, sizeof name, "%s", strcmp(e->name, "..") ? e->name : "..  (up)");
        if (!isdir) {
            char *dot = strrchr(name, '.');
            if (dot) *dot = 0;
        }
        bool has_bar = false;
        float frac = 0;
        if (isdir && e->stats && e->books > 0) { has_bar = true; frac = e->sfrac; }
        if (!isdir && e->state > 0) { has_bar = true; frac = e->frac; }
        float tw = markable(e) ? (isdir ? 196 : 226) : 250;
        ui_text_fit(name, 46, y + (has_bar ? 4 : 9), tw, 0.5f, fg, UI_LEFT);
        if (has_bar) ui_bar(46, y + 25, tw, 3, frac, track, fill);

        if (markable(e)) {
            bool fin = isdir ? (e->stats && e->books > 0 && e->done == e->books) : e->state == 2;
            float cx = 288, cy = y + ROW_H / 2.0f;
            if (fin) {
                ui_circle(cx, cy, 9, inv ? ui_pal.on_accent : ui_accent);
                ui_icon_check(cx, cy, 5, inv ? ui_accent : ui_pal.on_accent);
            } else {
                ui_circle(cx, cy, 9, dim);
                ui_circle(cx, cy, 7.4f, bg);
            }
            if (isdir && e->stats && e->books > 0) {
                char s[24];
                snprintf(s, sizeof s, "%d/%d", e->done, e->books);
                ui_text(s, 270, y + 9, 0.4f, dim, UI_RIGHT);
            }
        }
    }
    draw_scrollbar(g_br.count, g_br.scroll);
}

/* ---- shelf: covers on a bookshelf, 3 x 2 tiles per screen ---- */
#define SH_COLS 3
#define SH_ROWS 2
#define SH_TW   98
#define SH_TH   100
#define SH_PX   104
#define SH_PY   102

static void ensure_shelf_visible(void) {
    int row = g_br.sel / SH_COLS;
    int rows = (g_br.count + SH_COLS - 1) / SH_COLS;
    if (row < g_br.scroll) g_br.scroll = row;
    if (row >= g_br.scroll + SH_ROWS) g_br.scroll = row - SH_ROWS + 1;
    int max = rows - SH_ROWS;
    if (max < 0) max = 0;
    if (g_br.scroll > max) g_br.scroll = max;
    if (g_br.scroll < 0) g_br.scroll = 0;
}

static void ensure_view_visible(void) {
    if (theme_view() == VIEW_SHELF) ensure_shelf_visible();
    else ensure_lib_visible();
}

static void draw_library_shelf(void) {
    ui_scene(ui_bot);
    draw_lib_header();
    thumbs_new_frame();

    int first = g_br.scroll * SH_COLS;
    for (int k = 0; k < SH_COLS * SH_ROWS; k++) {
        int i = first + k;
        if (i >= g_br.count) break;
        BrEntry *e = &g_br.items[i];
        float x = 6 + (k % SH_COLS) * SH_PX, y = 36 + (k / SH_COLS) * SH_PY;
        bool sel = (i == g_br.sel);
        float rr = ui_style->r_card > 12 ? 12 : ui_style->r_card;
        if (ui_style->outline) ui_rrect_outline(x, y, SH_TW, SH_TH, rr, 1.6f, sel ? ui_accent : ui_pal.line, ui_pal.bg);
        else ui_rrect(x, y, SH_TW, SH_TH, rr, sel ? ui_accent_soft : ui_pal.surface);
        if (sel && !ui_style->outline) ui_rrect_outline(x, y, SH_TW, SH_TH, rr, 1.5f, ui_accent, ui_accent_soft);

        const float tx = x + 17, ty = y + 6;
        char name[300];
        snprintf(name, sizeof name, "%s", strcmp(e->name, "..") ? e->name : "Up");
        if (!e->is_dir) {
            char *dot = strrchr(name, '.');
            if (dot) *dot = 0;
        }
        if (e->is_dir) {
            u32 fc = sel ? ui_accent : ui_pal.dim;
            ui_rrect(tx + 4, ty + 10, 26, 12, 4, fc);
            ui_rrect(tx + 4, ty + 18, 56, 36, 6, fc);
            if (markable(e)) {
                browser_dir_stats(&g_br, i);
                if (e->stats && e->books > 0) {
                    char s[24];
                    snprintf(s, sizeof s, "%d/%d", e->done, e->books);
                    ui_text(s, tx + 32, ty + 28, 0.5f, ui_pal.bg, UI_CENTER);
                }
            }
        } else {
            char p[800];
            if (e->is_cont) snprintf(p, sizeof p, "%s", g_cont_path);
            else browser_path(&g_br, e->name, p, sizeof p);
            const C2D_Image *img = thumb_get(p);
            if (img) cover_draw_image(img, tx, ty, 64);
            else ui_cover_placeholder(tx, ty, 64, name);
            if (e->is_cont) {
                ui_circle(tx + 54, ty + 54, 9, ui_accent);
                ui_icon_play(tx + 55, ty + 54, 4, ui_pal.on_accent);
            }
            if (!e->is_cont && e->state == 1) ui_bar(tx, ty + 67, 64, 3, e->frac, ui_pal.surface2, ui_accent);
        }
        if (markable(e)) {
            bool fin = e->is_dir ? (e->stats && e->books > 0 && e->done == e->books) : e->state == 2;
            if (fin) {
                ui_circle(x + SH_TW - 13, y + 13, 9, ui_accent);
                ui_icon_check(x + SH_TW - 13, y + 13, 5, ui_pal.on_accent);
            }
        }
        ui_text_wrap(e->is_cont ? "Continue" : name, x + 5, y + 78, SH_TW - 10, 0.33f, sel ? ui_pal.text : ui_pal.dim, 2, 10);
    }
}

static void draw_library_bottom(void) {
    if (theme_view() == VIEW_SHELF) draw_library_shelf();
    else draw_library_list();
}

/* ------------------------------------------------------------ drawing: settings */

#define SET_ROWS 13
#define SET_VIS  8
#define SET_ROW_Y 32
#define SET_ROW_H 26

static void set_value_text(int row, char *out, size_t n) {
    static const char *FLIPS[4] = {"None", "Vertical", "Horizontal", "Both"};
    switch (row) {
    case 0: snprintf(out, n, "%s", theme_style_name(g_set.style)); break;
    case 1: snprintf(out, n, "%s", theme_layout_name(g_set.layout)); break;
    case 2: snprintf(out, n, "%s", theme_view_name(g_set.libview)); break;
    case 3: snprintf(out, n, "%s", g_set.dark ? "Dark" : "Light"); break;
    case 4: snprintf(out, n, "%s", theme_accent_name(g_set.accent)); break;
    case 5: snprintf(out, n, "%s", g_set.icons == 0 ? "Style default" : g_set.icons == 1 ? "Solid" : "Outline"); break;
    case 6: snprintf(out, n, "%s", g_set.anim ? "On" : "Off"); break;
    case 7: snprintf(out, n, "%d s", SKIP_BACK_OPTS[g_set.skip_back]); break;
    case 8: snprintf(out, n, "%d s", SKIP_FWD_OPTS[g_set.skip_fwd]); break;
    case 9: snprintf(out, n, "%s", FLIPS[g_set.cover_flip & 3]); break;
    case 10: snprintf(out, n, "%d deg", 90 * (g_set.cover_rot & 3)); break;
    case 11: snprintf(out, n, "%s", g_set.lidplay ? "On" : "Off"); break;
    default: snprintf(out, n, "%s", g_set.autocont ? "On" : "Off"); break;
    }
}

static void set_change(int row, int dir) {
    switch (row) {
    case 0: g_set.style = (g_set.style + dir + STYLE_COUNT) % STYLE_COUNT; break;
    case 1: g_set.layout = (g_set.layout + dir + LAY_COUNT + 1) % (LAY_COUNT + 1); break;
    case 2: g_set.libview = (g_set.libview + dir + 3) % 3; break;
    case 3: g_set.dark = !g_set.dark; break;
    case 4: g_set.accent = (g_set.accent + dir + ACCENT_COUNT) % ACCENT_COUNT; break;
    case 5: g_set.icons = (g_set.icons + dir + 3) % 3; break;
    case 6: g_set.anim = !g_set.anim; break;
    case 7: g_set.skip_back = (g_set.skip_back + dir + 3) % 3; break;
    case 8: g_set.skip_fwd = (g_set.skip_fwd + dir + 3) % 3; break;
    case 9: g_set.cover_flip = (g_set.cover_flip + dir + 4) % 4; break;
    case 10: g_set.cover_rot = (g_set.cover_rot + dir + 4) % 4; break;
    case 11: g_set.lidplay = !g_set.lidplay; break;
    default: g_set.autocont = !g_set.autocont; break;
    }
    cover_set_orient(g_set.cover_flip, g_set.cover_rot);
    theme_apply();
    theme_save();
    ensure_view_visible();
}

static void open_settings(State from) {
    g_settings_from = from;
    g_set_sel = 0;
    g_set_scroll = 0;
    g_state = S_SETTINGS;
}

static void draw_settings_top(u64 now) {
    TopData t = {&g_sample, 2, 0.42f, 6 * 3600000ull + 20 * 60000ull, 12 * 60000ull + 30000ull, 25 * 60000ull, true};
    draw_top(&t, now, true);
}

static void draw_settings_bottom(void) {
    static const char *LABELS[SET_ROWS] = {"Style", "Layout", "Library view", "Appearance", "Accent", "Icons", "Animations",
                                           "Skip back", "Skip forward", "Cover flip", "Cover rotate", "Lid-closed play", "Auto-next book"};
    ui_scene(ui_bot);
    ui_back_button(8, 3, 64, 26);
    ui_text("Settings", 84, 7, 0.55f, UI_TEXT, UI_LEFT);

    for (int r = 0; r < SET_VIS; r++) {
        int row = g_set_scroll + r;
        if (row >= SET_ROWS) break;
        float y = SET_ROW_Y + r * SET_ROW_H;
        bool sel = (row == g_set_sel);
        ui_row(8, y, 298, SET_ROW_H, sel, 0);
        u32 fg = ui_row_fg(sel, 0), dim = ui_row_dim(sel);
        ui_text(LABELS[row], 20, y + 5, 0.46f, fg, UI_LEFT);

        char v[48];
        set_value_text(row, v, sizeof v);
        float cy = y + SET_ROW_H / 2;
        C2D_DrawTriangle(172, cy, dim, 178, cy - 5, dim, 178, cy + 5, dim, 0.5f);
        C2D_DrawTriangle(298, cy, dim, 292, cy - 5, dim, 292, cy + 5, dim, 0.5f);
        if (row == 4) {
            ui_circle(190, cy, 6, theme_accent_swatch(g_set.accent));
            ui_text_fit(v, 240, y + 5, 76, 0.42f, fg, UI_CENTER);
        } else {
            ui_text_fit(v, 235, y + 5, 100, 0.42f, fg, UI_CENTER);
        }
    }
    if (SET_ROWS > SET_VIS) {
        float track = SET_VIS * SET_ROW_H - 6;
        float th = track * SET_VIS / SET_ROWS;
        float ty = SET_ROW_Y + 3 + (track - th) * g_set_scroll / (SET_ROWS - SET_VIS);
        ui_rrect(313, ty, 4, th, 2, ui_style->outline ? ui_accent_dim : ui_pal.surface2);
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

static void cycle_sort(void) {
    g_set.sort = (g_set.sort + 1) % SORT_COUNT;
    browser_set_sort(&g_br, g_set.sort);
    ensure_view_visible();
    theme_save();
    char m[48];
    snprintf(m, sizeof m, "Sort: %s", SORT_NAMES[g_set.sort]);
    toast(m);
}

static void toggle_mark(int idx) {
    if (idx < 0 || idx >= g_br.count || !markable(&g_br.items[idx])) return;
    BrEntry *e = &g_br.items[idx];
    bool fin;
    if (e->is_dir) { browser_dir_stats(&g_br, idx); fin = !(e->books > 0 && e->done == e->books); }
    else fin = e->state != 2;
    browser_mark(&g_br, idx, fin);
    if (e->is_dir) browser_dir_stats(&g_br, idx);
    else if (g_set.sort != SORT_NAME) { browser_set_sort(&g_br, g_set.sort); ensure_view_visible(); }
    toast(fin ? "Marked as finished" : "Marked as not finished");
}

static int shelf_tile_at(int x, int y) {
    if (x < 6 || y < 36) return -1;
    int c = (x - 6) / SH_PX, rx = (x - 6) % SH_PX, r = (y - 36) / SH_PY, ry = (y - 36) % SH_PY;
    if (c >= SH_COLS || r >= SH_ROWS || rx >= SH_TW || ry >= SH_TH) return -1;
    int i = (g_br.scroll + r) * SH_COLS + c;
    return i < g_br.count ? i : -1;
}

static void shelf_touch(u32 kd, u32 kh, u32 ku, touchPosition tp) {
    if (kd & KEY_TOUCH) {
        g_drag.active = true; g_drag.moved = false;
        g_drag.sx = tp.px; g_drag.sy = tp.py; g_drag.ly = tp.py; g_drag.acc = 0;
    } else if ((kh & KEY_TOUCH) && g_drag.active) {
        int dy = tp.py - g_drag.ly;
        g_drag.ly = tp.py;
        if (abs(tp.py - g_drag.sy) > 8) g_drag.moved = true;
        if (g_drag.moved) {
            g_drag.acc -= dy;
            int rows = (g_br.count + SH_COLS - 1) / SH_COLS, max = rows - SH_ROWS;
            if (max < 0) max = 0;
            while (g_drag.acc >= SH_PY) { g_br.scroll++; g_drag.acc -= SH_PY; }
            while (g_drag.acc <= -SH_PY) { g_br.scroll--; g_drag.acc += SH_PY; }
            if (g_br.scroll > max) g_br.scroll = max;
            if (g_br.scroll < 0) g_br.scroll = 0;
        }
    }
    if ((ku & KEY_TOUCH) && g_drag.active) {
        g_drag.active = false;
        if (!g_drag.moved) {
            int i = shelf_tile_at(g_drag.sx, g_drag.sy);
            if (i >= 0) {
                float tx = 6 + ((g_drag.sx - 6) / SH_PX) * SH_PX;       // the check badge in a tile's corner toggles "finished"
                bool badge = g_drag.sx >= tx + SH_TW - 26 && g_drag.sy >= 36 + ((g_drag.sy - 36) / SH_PY) * SH_PY && g_drag.sy < 36 + ((g_drag.sy - 36) / SH_PY) * SH_PY + 26;
                if (badge && markable(&g_br.items[i])) toggle_mark(i);
                else { g_br.sel = i; lib_activate(i); }
            }
        }
    }
}

static void update_library(u32 kd, u32 kh, u32 ku, touchPosition tp) {
    if (kd & KEY_SELECT) { open_settings(S_LIB); return; }
    if (kd & KEY_TOUCH) {
        if (in_circle(tp.px, tp.py, GEAR_X, 18, 22)) { open_settings(S_LIB); return; }
        if (in_circle(tp.px, tp.py, SORT_X, 18, 22)) { cycle_sort(); return; }
    }
    if (kd & KEY_Y) cycle_sort();
    if ((kd & KEY_X) && g_br.count) toggle_mark(g_br.sel);

    bool shelf = (theme_view() == VIEW_SHELF);
    if (g_br.count) {
        if (shelf) {
            if (key_rep(kd, kh, KEY_LEFT) && g_br.sel > 0) g_br.sel--;
            if (key_rep(kd, kh, KEY_RIGHT) && g_br.sel < g_br.count - 1) g_br.sel++;
            if (key_rep(kd, kh, KEY_UP) && g_br.sel >= SH_COLS) g_br.sel -= SH_COLS;
            if (key_rep(kd, kh, KEY_DOWN) && g_br.sel + SH_COLS < g_br.count) g_br.sel += SH_COLS;
            if (kd & KEY_L) g_br.sel = g_br.sel >= 6 ? g_br.sel - 6 : 0;
            if (kd & KEY_R) g_br.sel = g_br.sel + 6 < g_br.count ? g_br.sel + 6 : g_br.count - 1;
        } else {
            if (key_rep(kd, kh, KEY_UP) && g_br.sel > 0) g_br.sel--;
            if (key_rep(kd, kh, KEY_DOWN) && g_br.sel < g_br.count - 1) g_br.sel++;
            if (kd & KEY_L) g_br.sel = g_br.sel > LIST_VIS ? g_br.sel - LIST_VIS : 0;
            if (kd & KEY_R) g_br.sel = g_br.sel + LIST_VIS < g_br.count ? g_br.sel + LIST_VIS : g_br.count - 1;
        }
        ensure_view_visible();
        if (kd & KEY_A) lib_activate(g_br.sel);
    }
    if ((kd & KEY_B) || (!shelf && (kd & KEY_LEFT))) lib_up();

    if (shelf) {
        shelf_touch(kd, kh, ku, tp);
    } else {
        int tap = list_touch(kd, kh, ku, tp, &g_br.scroll, g_br.count);
        if (tap >= 0) {
            if (g_drag.sx >= 272 && markable(&g_br.items[tap])) toggle_mark(tap);   // the circle on the right
            else { g_br.sel = tap; lib_activate(tap); }
        }
    }
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

static void ensure_set_visible(void) {
    if (g_set_sel < g_set_scroll) g_set_scroll = g_set_sel;
    if (g_set_sel >= g_set_scroll + SET_VIS) g_set_scroll = g_set_sel - SET_VIS + 1;
    int max = SET_ROWS - SET_VIS;
    if (g_set_scroll > max) g_set_scroll = max;
    if (g_set_scroll < 0) g_set_scroll = 0;
}

static void update_settings(u32 kd, u32 kh, u32 ku, touchPosition tp) {
    if (kd & (KEY_B | KEY_SELECT)) { leave_settings(); return; }
    if ((kd & KEY_TOUCH) && in_rect(tp.px, tp.py, 0, 0, 76, SET_ROW_Y - 1)) { leave_settings(); return; }

    if (key_rep(kd, kh, KEY_UP) && g_set_sel > 0) g_set_sel--;
    if (key_rep(kd, kh, KEY_DOWN) && g_set_sel < SET_ROWS - 1) g_set_sel++;
    if (kd & (KEY_UP | KEY_DOWN)) ensure_set_visible();
    if (key_rep(kd, kh, KEY_LEFT)) set_change(g_set_sel, -1);
    if (key_rep(kd, kh, KEY_RIGHT) || (kd & KEY_A)) set_change(g_set_sel, +1);

    // touch: drag scrolls, a tap on the left/right half of a row changes its value
    if (kd & KEY_TOUCH) {
        g_drag.active = true; g_drag.moved = false;
        g_drag.sx = tp.px; g_drag.sy = tp.py; g_drag.ly = tp.py; g_drag.acc = 0;
    } else if ((kh & KEY_TOUCH) && g_drag.active) {
        int dy = tp.py - g_drag.ly;
        g_drag.ly = tp.py;
        if (abs(tp.py - g_drag.sy) > 8) g_drag.moved = true;
        if (g_drag.moved) {
            g_drag.acc -= dy;
            while (g_drag.acc >= SET_ROW_H) { g_set_scroll++; g_drag.acc -= SET_ROW_H; }
            while (g_drag.acc <= -SET_ROW_H) { g_set_scroll--; g_drag.acc += SET_ROW_H; }
            int max = SET_ROWS - SET_VIS;
            if (g_set_scroll > max) g_set_scroll = max;
            if (g_set_scroll < 0) g_set_scroll = 0;
        }
    }
    if ((ku & KEY_TOUCH) && g_drag.active) {
        g_drag.active = false;
        if (!g_drag.moved && g_drag.sy >= SET_ROW_Y) {
            int row = g_set_scroll + (g_drag.sy - SET_ROW_Y) / SET_ROW_H;
            if (row >= 0 && row < SET_ROWS && g_drag.sx < 312) {
                g_set_sel = row;
                if (g_drag.sx >= 236) set_change(row, +1);
                else if (g_drag.sx >= 160) set_change(row, -1);
            }
        }
    }
}

static void seek_from_touch(int px) {
    const BDef *bd = &BD[bottom_family(theme_layout())];
    float x0 = bd->seek[0] + SEEK_PAD, w = bd->seek[2] - 2 * SEEK_PAD;
    float f = (px - x0) / w;
    g_seek_frac = f < 0 ? 0 : f > 1 ? 1 : f;
}

static void bright_from_touch(int px) {
    const short *r = BD[bottom_family(theme_layout())].bright;
    float x0 = r[0] + 30, w = r[2] - 68;
    float f = (px - x0) / w;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    int pct = (int)(f * 100.0f + 0.5f);
    apply_brightness(pct);
    g_set.bright = pct;
}

static void do_action(int act) {
    switch (act) {
    case A_PLAY: do_toggle(); break;
    case A_BACK: seek_rel(-skip_back_ms()); break;
    case A_FWD: seek_rel(skip_fwd_ms()); break;
    case A_PREV: prev_chapter(); break;
    case A_NEXT: next_chapter(); break;
    case A_CHAPTERS: open_lists(0); break;
    case A_SLEEP: cycle_sleep(); break;
    case A_SETTINGS: open_settings(S_PLAYER); break;
    case A_BOOKMARK: add_bookmark(); break;
    case A_SCREEN_OFF: screen_off(true); break;
    default: break;
    }
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
        int fam = bottom_family(theme_layout());
        const BDef *bd = &BD[fam];
        int x = tp.px, y = tp.py;
        if (in_rect(x, y, bd->seek[0], bd->seek[1], bd->seek[2], bd->seek[3])) {
            g_seeking = true;
            seek_from_touch(x);
        } else if (g_lcd_ok && in_rect(x, y, bd->bright[0], bd->bright[1], bd->bright[2], bd->bright[3])) {
            g_bright_drag = true;
            bright_from_touch(x);
        } else {
            int act = hit_test(fam, x, y);
            if (act == A_SETTINGS) { open_settings(S_PLAYER); return; }
            do_action(act);
        }
    } else if (g_seeking && (kh & KEY_TOUCH)) {
        seek_from_touch(tp.px);
    } else if (g_bright_drag && (kh & KEY_TOUCH)) {
        bright_from_touch(tp.px);
    }
    if (g_seeking && (ku & KEY_TOUCH)) {
        uint64_t cs = ch_start(g_cur_ch), ce = ch_end(g_cur_ch);
        player_seek_ms(cs + (uint64_t)(g_seek_frac * (float)(ce > cs ? ce - cs : 0)));
        g_seeking = false;
    }
    if (g_bright_drag && (ku & KEY_TOUCH)) { g_bright_drag = false; theme_save(); }
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
    init_brightness();

    mkdir(LIB_ROOT, 0777);
    thumbs_init();

    // sample book for the live preview in Settings
    {
        static const char *SC[6] = {"Chapter 1 - The Wrong Door", "Chapter 2 - Digory and His Uncle", "Chapter 3 - The Wood Between the Worlds",
                                    "Chapter 4 - The Bell and the Hammer", "Chapter 5 - The Deplorable Word", "Chapter 6 - The Founding of Narnia"};
        snprintf(g_sample.title, sizeof g_sample.title, "The Magician's Nephew");
        snprintf(g_sample.artist, sizeof g_sample.artist, "C. S. Lewis");
        for (int i = 0; i < 6; i++) { g_sample_ch[i].title = (char *)SC[i]; g_sample_ch[i].start_ms = (uint64_t)i * 1500000ULL; }
        g_sample.chapters = g_sample_ch;
        g_sample.nchapters = 6;
        g_sample.duration_ms = 9000000ULL;
    }
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
        // this frees a lot of CPU/GPU time for the audio decoder. Animated layouts redraw just the top screen
        // at a low frame rate (the bottom screen keeps its last image).
        if (kh || kd || ku) g_active_until = now + 350;
        uint64_t spos = g_book ? player_position_ms() : 0;
        int lay = theme_layout();
        bool animated_layout = (lay == LAY_CASSETTE || lay == LAY_VINYL || lay == LAY_REEL);
        bool anim_on = g_set.anim && animated_layout &&
                       (g_state == S_SETTINGS || (g_book && player_is_playing() && (g_state == S_PLAYER || g_state == S_CHAPTERS)));
        u64 interval = player_is_new3ds() ? 42 : 83;          // ~24 fps on New 3DS, ~12 fps on the old models
        u64 atick = anim_on ? now / interval : 0;

        u64 sig = 1469598103934665603ULL;
#define MIX(v) sig = (sig ^ (u64)(v)) * 1099511628211ULL
        MIX(g_state); MIX(g_book != NULL); MIX(g_book ? player_is_playing() : 0); MIX(g_cur_ch); MIX(spos / 1000);
        MIX(g_sleep_mode); MIX(g_sleep_mode ? (g_sleep_end > now ? (g_sleep_end - now) / 60000 : 0) : 0);
        MIX(now < g_toast_until); MIX(now < g_msg_until); MIX(g_slider); MIX(g_batt); MIX(g_charging);
        MIX(g_nbm); MIX((u64)time(NULL) / 60); MIX(g_seeking); MIX(g_bright_pct); MIX(lay);
#undef MIX
        static u64 last_sig, last_atick, last_full;
        bool bot_dirty = (sig != last_sig) || now < g_active_until || now - last_full >= 1000;
        bool top_dirty = bot_dirty || (anim_on && atick != last_atick);
        if (!top_dirty) {
            gspWaitForVBlank();
            continue;
        }
        if (bot_dirty) { last_sig = sig; last_full = now; }
        last_atick = atick;

        ui_begin();
        if (g_state == S_SETTINGS) {
            draw_settings_top(now);
            if (bot_dirty) draw_settings_bottom();
        } else if (g_state == S_LIB || !g_book) {
            draw_library_top(now);
            if (bot_dirty) draw_library_bottom();
        } else {
            draw_player_top(spos, now);
            if (bot_dirty) {
                if (g_state == S_CHAPTERS) draw_lists_bottom(spos);
                else draw_player_bottom(spos, now);
            }
        }
        ui_end();
        if (thumbs_pending()) g_active_until = now + 120;     // some shelf thumbnails are still loading: draw again soon
    }

    if (g_book) save_progress();
    screen_off(false);
    restore_brightness();
    aptSetSleepAllowed(true);
    player_exit();
    cover_free();
    thumbs_exit();
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
