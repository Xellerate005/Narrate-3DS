// custom.c
#include "custom.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "cover.h"
#include "gimage.h"
#include "log.h"
#include "ui.h"
#include "zipx.h"

#define MAX_THEMES 64
#define ACTIVE_DIR THEMES_DIR "/.active"
#define TAU 6.2831853f

typedef struct { char file[64]; char label[80]; } ThemeEnt;

static ThemeEnt g_list[MAX_THEMES];
static int g_nlist;

static TfTheme g_t;
static bool g_active;
static char g_file[64], g_dispname[64], g_root[160];
static GImage g_img[TF_MAX_IMAGES];
static GImage g_icon[IC_COUNT];
static C2D_Font g_font;

/* ------------------------------------------------------------------ scanning */

static int cmp_ent(const void *a, const void *b) { return strcasecmp(((const ThemeEnt *)a)->file, ((const ThemeEnt *)b)->file); }

static bool has_ext(const char *name, const char *ext) {
    size_t n = strlen(name), e = strlen(ext);
    return n > e && strcasecmp(name + n - e, ext) == 0;
}

int custom_scan(void) {
    g_nlist = 0;
    zipx_mkdir_p(THEMES_DIR);
    DIR *d = opendir(THEMES_DIR);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) && g_nlist < MAX_THEMES) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof g_list[0].file) continue;
        if (!has_ext(e->d_name, ".pid") && !has_ext(e->d_name, ".pix")) continue;
        char full[300];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", THEMES_DIR, e->d_name);
        if (stat(full, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        snprintf(g_list[g_nlist++].file, sizeof g_list[0].file, "%s", e->d_name);
    }
    closedir(d);
    qsort(g_list, (size_t)g_nlist, sizeof g_list[0], cmp_ent);
    for (int i = 0; i < g_nlist; i++) {          // label = name before the extension ("sea.pix" -> "sea")
        char base[64];
        snprintf(base, sizeof base, "%s", g_list[i].file);
        char *dot = strrchr(base, '.');
        if (dot) *dot = 0;
        bool dup = false;
        for (int j = 0; j < g_nlist; j++) {
            if (j == i) continue;
            char b2[64];
            snprintf(b2, sizeof b2, "%.63s", g_list[j].file);
            char *d2 = strrchr(b2, '.');
            if (d2) *d2 = 0;
            if (!strcasecmp(base, b2)) dup = true;
        }
        if (dup) snprintf(g_list[i].label, sizeof g_list[i].label, "%s (%s)", base, g_list[i].file + strlen(base) + 1);
        else snprintf(g_list[i].label, sizeof g_list[i].label, "%s", base);
    }
    return g_nlist;
}

int custom_count(void) { return g_nlist; }
const char *custom_file(int i) { return (i >= 0 && i < g_nlist) ? g_list[i].file : ""; }
const char *custom_label(int i) { return (i >= 0 && i < g_nlist) ? g_list[i].label : ""; }

/* ------------------------------------------------------------ apply / unapply */

static bool safe_rel(const char *p) {
    if (!p || !*p || p[0] == '/' || p[0] == '\\' || strstr(p, "..") || strchr(p, ':')) return false;
    return true;
}

static void release(void) {
    for (int i = 0; i < TF_MAX_IMAGES; i++) gimage_free(&g_img[i]);
    for (int i = 0; i < IC_COUNT; i++) gimage_free(&g_icon[i]);
    if (g_font) { ui_set_font(NULL); C2D_FontFree(g_font); g_font = NULL; }
    g_active = false;
    memset(&g_t, 0, sizeof g_t);
}

static void load_assets(void) {
    if (!g_root[0]) return;                                  // a single .pid has no files to load
    char p[340];
    if (g_t.font[0] && safe_rel(g_t.font)) {
        snprintf(p, sizeof p, "%s/%s", g_root, g_t.font);
        g_font = C2D_FontLoad(p);
        if (g_font) ui_set_font(g_font); else nlog("theme: font failed: %s", p);
    }
    for (int i = 0; i < g_t.nimages; i++) {
        if (!safe_rel(g_t.images[i].path)) continue;
        snprintf(p, sizeof p, "%s/%s", g_root, g_t.images[i].path);
        if (!gimage_load(&g_img[i], p, g_t.images[i].frames, g_t.images[i].fps)) nlog("theme: image failed: %s", p);
    }
    for (int i = 0; i < IC_COUNT; i++) {
        if (!g_t.icon_path[i][0] || !safe_rel(g_t.icon_path[i])) continue;
        snprintf(p, sizeof p, "%s/%s", g_root, g_t.icon_path[i]);
        if (!gimage_load(&g_icon[i], p, 1, 1)) nlog("theme: icon failed: %s", p);
    }
}

bool custom_apply(const char *file, char *msg, size_t n) {
    if (msg && n) msg[0] = 0;
    release();
    g_root[0] = 0;

    char path[300], manifest[330];
    snprintf(path, sizeof path, "%s/%s", THEMES_DIR, file);
    struct stat st;
    if (stat(path, &st) != 0) { snprintf(msg, n, "Theme file not found"); return false; }
    bool pix = has_ext(file, ".pix");

    if (pix) {
        // Unpack to a temporary folder (skipped if this exact file is already unpacked there).
        char stamp[160], have[160] = "";
        snprintf(stamp, sizeof stamp, "%.64s|%ld|%ld", file, (long)st.st_size, (long)st.st_mtime);
        FILE *sf = fopen(ACTIVE_DIR "/.stamp", "r");
        if (sf) {
            if (fgets(have, sizeof have, sf)) { size_t l = strlen(have); while (l && (have[l - 1] == '\n' || have[l - 1] == '\r')) have[--l] = 0; }
            fclose(sf);
        }
        snprintf(manifest, sizeof manifest, "%s/theme.pid", ACTIVE_DIR);
        struct stat ms;
        if (strcmp(stamp, have) != 0 || stat(manifest, &ms) != 0) {
            zipx_rmtree(ACTIVE_DIR);
            char err[96] = "";
            if (zipx_extract(path, ACTIVE_DIR, err, sizeof err) != 0) {
                snprintf(msg, n, "Could not unpack: %s", err);
                zipx_rmtree(ACTIVE_DIR);
                return false;
            }
            sf = fopen(ACTIVE_DIR "/.stamp", "w");
            if (sf) { fputs(stamp, sf); fclose(sf); }
        }
        if (stat(manifest, &ms) != 0) { snprintf(msg, n, "No theme.pid inside the archive"); return false; }
        snprintf(g_root, sizeof g_root, "%s", ACTIVE_DIR);
    } else {
        snprintf(manifest, sizeof manifest, "%s", path);
    }

    FILE *f = fopen(manifest, "rb");
    if (!f) { snprintf(msg, n, "Cannot read the theme file"); return false; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 96 * 1024) { fclose(f); snprintf(msg, n, "Theme file is empty or too large"); return false; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); snprintf(msg, n, "Out of memory"); return false; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    tf_parse(buf, got, &g_t);
    free(buf);

    snprintf(g_file, sizeof g_file, "%s", file);
    if (g_t.name[0]) snprintf(g_dispname, sizeof g_dispname, "%s", g_t.name);
    else {
        snprintf(g_dispname, sizeof g_dispname, "%s", file);
        char *dot = strrchr(g_dispname, '.');
        if (dot) *dot = 0;
    }
    load_assets();
    g_active = true;
    if (g_t.warnings) {
        snprintf(msg, n, "%s: %d warning(s) - %s", g_dispname, g_t.warnings, g_t.first_warning);
        nlog("theme %s: %d warnings, first: %s", file, g_t.warnings, g_t.first_warning);
    } else {
        snprintf(msg, n, "Theme applied: %s", g_dispname);
    }
    return true;
}

void custom_unapply(void) {
    bool had = g_active;
    release();
    g_file[0] = 0;
    g_root[0] = 0;
    if (had) zipx_rmtree(ACTIVE_DIR);          // the unpacked files were only needed while the theme was applied
}

bool custom_active(void) { return g_active; }
const char *custom_active_file(void) { return g_file; }
const char *custom_name(void) { return g_dispname; }

/* ----------------------------------------------------------- theme engine hooks */

int custom_base_style(void) {
    int s = g_t.base[0] ? theme_style_by_name(g_t.base) : 0;
    return s < 0 ? 0 : s;
}

void custom_patch_style(UiStyle *st) {
    if (g_t.has_r_card) st->r_card = g_t.r_card;
    if (g_t.has_r_pill) st->r_pill = g_t.r_pill;
    if (g_t.has_bar_h) st->bar_h = g_t.bar_h < 1 ? 1 : g_t.bar_h;
    if (g_t.has_play_r) st->play_r = g_t.play_r;
    if (g_t.play >= 0) st->play = g_t.play;
    if (g_t.row >= 0) st->row = g_t.row;
    if (g_t.tab >= 0) st->tab = g_t.tab;
    if (g_t.bar >= 0) st->bar = g_t.bar;
    if (g_t.knob >= 0) st->knob = g_t.knob;
    if (g_t.btn >= 0) st->btn = g_t.btn;
    if (g_t.icons >= 0) st->icons_outline = (g_t.icons == 1);
    bool has_bg2 = g_t.has[0][TC_BG2] || g_t.has[1][TC_BG2];
    bool has_bg = g_t.has[0][TC_BG] || g_t.has[1][TC_BG];
    if (has_bg2) { st->gradient = GRAD_CUSTOM; st->decor = DECOR_NONE; }
    else if (has_bg) { st->gradient = GRAD_NONE; st->decor = DECOR_NONE; }
    int L = g_t.layout[0] ? theme_layout_by_name(g_t.layout) : -1;
    if (L >= 0) st->def_layout = L;
    st->name = g_dispname;
}

void custom_patch_palette(UiPal *pal, int dark, bool *accent_set, uint32_t *accent, bool *on_set) {
    *accent_set = *on_set = false;
    int s = dark ? 0 : 1;
    if (g_t.mode == 1) s = 0;
    else if (g_t.mode == 2) s = 1;
    if (g_t.has[s][TC_BG]) { pal->bg = g_t.pal[s][TC_BG]; pal->bg2 = pal->bg; }
    if (g_t.has[s][TC_BG2]) pal->bg2 = g_t.pal[s][TC_BG2];
    if (g_t.has[s][TC_SURFACE]) pal->surface = g_t.pal[s][TC_SURFACE];
    if (g_t.has[s][TC_SURFACE2]) pal->surface2 = g_t.pal[s][TC_SURFACE2];
    if (g_t.has[s][TC_TEXT]) pal->text = g_t.pal[s][TC_TEXT];
    if (g_t.has[s][TC_DIM]) pal->dim = g_t.pal[s][TC_DIM];
    if (g_t.has[s][TC_LINE]) pal->line = g_t.pal[s][TC_LINE];
    if (g_t.has[s][TC_ON_ACCENT]) { pal->on_accent = g_t.pal[s][TC_ON_ACCENT]; *on_set = true; }
    if (g_t.has[s][TC_ACCENT]) { *accent = g_t.pal[s][TC_ACCENT]; *accent_set = true; }
}

bool custom_draw_icon(int icon, float cx, float cy, float size) {
    if (!g_active || icon < 0 || icon >= IC_COUNT || !g_icon[icon].ok) return false;
    const GImage *g = &g_icon[icon];
    float iw = (float)gimage_frame_w(g), ih = (float)g->h;
    float s = fminf(size / iw, size / ih);                    // scale to fit the box, keep the aspect ratio
    float w = iw * s, h = ih * s;
    uint32_t tint = g_t.icon_tint == 1 ? ui_pal.text : (g_t.icon_tint == 2 ? ui_accent : 0);
    gimage_draw(g, 0, cx - w / 2, cy - h / 2, w, h, 0, 1.0f, g_t.image_flip, tint);
    return true;
}

/* -------------------------------------------------------------- layout engine */

int custom_fallback_layout(void) { return g_active && g_t.layout[0] ? theme_layout_by_name(g_t.layout) : -1; }
bool custom_has_top(void) { return g_active && g_t.has_top; }
bool custom_has_bottom(void) { return g_active && g_t.has_bottom; }

static float g_anim_ms;                 // animation clock: advances only while playing
static uint64_t g_anim_last;

static void anim_update(const CLCtx *c) {
    if (g_anim_last && c->playing && c->now > g_anim_last) {
        uint64_t dt = c->now - g_anim_last;
        if (dt > 250) dt = 250;
        g_anim_ms += (float)dt;
    }
    g_anim_last = c->now;
}

static u32 pal_color(int ref) {
    switch (ref) {
    case TC_BG: return ui_pal.bg;
    case TC_BG2: return ui_pal.bg2;
    case TC_SURFACE: return ui_pal.surface;
    case TC_SURFACE2: return ui_pal.surface2;
    case TC_TEXT: return ui_pal.text;
    case TC_DIM: return ui_pal.dim;
    case TC_ON_ACCENT: return ui_pal.on_accent;
    case TC_LINE: return ui_pal.line;
    default: return ui_accent;
    }
}

static u32 ecolor(int ref, uint32_t lit, float alpha) {
    u32 c = ref >= 0 ? pal_color(ref) : lit;
    if (alpha < 0.999f) {
        float a = (float)((c >> 24) & 255) * (alpha < 0 ? 0 : alpha);
        c = (c & 0x00FFFFFFu) | ((u32)a << 24);
    }
    return c;
}

static bool visible(const TfElem *e, const CLCtx *c) {
    if (e->when == WHEN_PLAYING && !c->playing) return false;
    if (e->when == WHEN_PAUSED && c->playing) return false;
    if (e->blink_ms > 0 && ((c->now / (uint64_t)e->blink_ms) & 1)) return false;
    return true;
}

static const char *field_text(const TfElem *e, const CLCtx *c) {
    switch (e->field) {
    case F_TITLE: return c->title;
    case F_ARTIST: return c->artist;
    case F_CHAPTER: return c->chapter;
    case F_ELAPSED: return c->elapsed;
    case F_REMAINING: return c->remaining;
    case F_LEFT: return c->left;
    case F_PERCENT: return c->percent;
    case F_CHAPNUM: return c->chapnum;
    case F_CLOCK: return c->clock;
    case F_SPEED: return c->speed;
    case F_SLEEP: return c->sleep;
    case F_VOLUME: return c->volume;
    default: return e->literal;
    }
}

static void draw_image(const TfElem *e, const CLCtx *c) {
    if (e->image < 0 || e->image >= g_t.nimages) return;
    const GImage *g = &g_img[e->image];
    if (!g->ok) return;
    float nw = (float)gimage_frame_w(g), nh = (float)g->h, w, h;
    if (!e->has_w && !e->has_h) { w = nw; h = nh; }
    else if (e->has_w && !e->has_h) { w = e->w; h = w * nh / nw; }
    else if (!e->has_w && e->has_h) { h = e->h; w = h * nw / nh; }
    else {
        w = e->w; h = e->h;
        if (e->fit != FIT_STRETCH) {                       // keep the aspect ratio inside / over the box
            float sx = w / nw, sy = h / nh, s = e->fit == FIT_COVER ? fmaxf(sx, sy) : fminf(sx, sy);
            w = nw * s; h = nh * s;
        }
    }
    float px = e->x, py = e->y;
    if (e->has_w && e->has_h) { px += (e->w - w) / 2; py += (e->h - h) / 2; }
    if (e->follow) {                                        // slide along a path as the book / chapter progresses
        float f = e->follow == FOLLOW_CHAPTER ? c->frac_chapter : c->frac_book;
        px += (e->tx - e->x) * f;
        py += (e->ty - e->y) * f;
    }
    float t = g_anim_ms;
    if (e->bob_ms > 0) py += e->bob_amp * sinf(t * TAU / e->bob_ms);
    float rot = e->rot + e->spin * t / 1000.0f;
    if (e->sway_ms > 0) rot += e->sway_deg * sinf(t * TAU / e->sway_ms);
    if (e->pulse_ms > 0) {
        float sc = 1.0f + e->pulse_amt * sinf(t * TAU / e->pulse_ms);
        px += w * (1 - sc) / 2; py += h * (1 - sc) / 2; w *= sc; h *= sc;
    }
    int frame = g->frames > 1 ? (int)(t / 1000.0f * (g->fps > 0 ? g->fps : 8)) : 0;
    if (e->scroll != 0 && w > 1) {                          // endless horizontal scrolling (parallax layers)
        float off = fmodf(t / 1000.0f * e->scroll, w);
        if (off > 0) off -= w;
        for (int k = 0; k < 3; k++) gimage_draw(g, frame, px + off + k * w, py, w, h, rot, e->alpha, g_t.image_flip, 0);
    } else {
        gimage_draw(g, frame, px, py, w, h, rot, e->alpha, g_t.image_flip, 0);
    }
}

static void draw_text(const TfElem *e, const CLCtx *c) {
    const char *s = field_text(e, c);
    if (!s || !*s) return;
    u32 col = ecolor(e->color_ref, e->color, e->alpha);
    float maxw = e->has_w ? e->w : 0;
    int al = e->align == 1 ? UI_CENTER : (e->align == 2 ? UI_RIGHT : UI_LEFT);
    if (e->color2 != 0 || e->color2_ref >= 0) {              // optional drop shadow: bg=colour
        u32 sh = ecolor(e->color2_ref, e->color2, e->alpha);
        if (maxw > 0 && e->lines <= 1) ui_text_fit(s, e->x + 1, e->y + 1, maxw, e->size, sh, al);
        else ui_text(s, e->x + 1, e->y + 1, e->size, sh, al);
    }
    if (e->lines > 1) ui_text_wrap(s, e->x, e->y, maxw > 0 ? maxw : 160, e->size, col, e->lines, e->size * 36.0f);
    else if (maxw > 0) ui_text_fit(s, e->x, e->y, maxw, e->size, col, al);
    else ui_text(s, e->x, e->y, e->size, col, al);
}

static void builtin_action_icon(int act, bool playing, float cx, float cy, float sz, u32 col, u32 bg, const CLCtx *c) {
    float s = sz / 2;
    char b[12];
    switch (act) {
    case ACT_PLAY: if (playing) ui_icon_pause(cx, cy, s * 0.9f, col); else ui_icon_play(cx, cy, s, col); break;
    case ACT_PREV: ui_icon_prev(cx, cy, s * 0.8f, col); break;
    case ACT_NEXT: ui_icon_next(cx, cy, s * 0.8f, col); break;
    case ACT_CHAPTERS: ui_icon_list(cx, cy, s * 0.8f, col); break;
    case ACT_SLEEP: ui_icon_moon(cx, cy, s * 0.8f, col, bg); break;
    case ACT_SETTINGS: ui_icon_gear(cx, cy, col, bg); break;
    case ACT_BOOKMARK: ui_icon_bookmark(cx, cy, col, bg); break;
    case ACT_SCREEN_OFF: ui_icon_screen(cx, cy, col, bg); break;
    case ACT_BACK: snprintf(b, sizeof b, "-%d", SKIP_BACK_OPTS[g_set.skip_back]); ui_text(b, cx, cy - 8, 0.55f, col, UI_CENTER); break;
    case ACT_FWD: snprintf(b, sizeof b, "+%d", SKIP_FWD_OPTS[g_set.skip_fwd]); ui_text(b, cx, cy - 8, 0.55f, col, UI_CENTER); break;
    case ACT_SPEED: ui_text(c->speed, cx, cy - 8, 0.55f, col, UI_CENTER); break;
    default: break;
    }
}

static int default_icon(int act, bool playing) {
    switch (act) {
    case ACT_PLAY: return playing ? IC_PAUSE : IC_PLAY;
    case ACT_BACK: return IC_BACK;
    case ACT_FWD: return IC_FWD;
    case ACT_PREV: return IC_PREV;
    case ACT_NEXT: return IC_NEXT;
    case ACT_CHAPTERS: return IC_CHAPTERS;
    case ACT_SLEEP: return IC_SLEEP;
    case ACT_SETTINGS: return IC_SETTINGS;
    case ACT_BOOKMARK: return IC_BOOKMARK;
    case ACT_SCREEN_OFF: return IC_SCREEN_OFF;
    case ACT_SPEED: return IC_SPEED;
    default: return -1;
    }
}

static void draw_button(const TfElem *e, const CLCtx *c) {
    float x = e->x, y = e->y, w = e->w, h = e->h, cx = x + w / 2, cy = y + h / 2;
    bool active = (e->action == ACT_SLEEP && c->sleeping);
    bool filled = (e->color_ref != TC_TEXT);                  // color=... on a button fills it with that colour
    u32 fill = filled ? ecolor(e->color_ref, e->color, e->alpha) : (active ? ui_accent_soft : ui_pal.surface2);
    u32 bg = ui_pal.bg;
    switch (e->style) {
    case BS_PILL:
        if (filled) ui_rrect(x, y, w, h, h / 2, fill); else ui_pill(x, y, w, h, active);
        bg = filled ? fill : ui_pill_bg(active);
        break;
    case BS_ROUND: ui_circle(cx, cy, fminf(w, h) / 2, fill); bg = fill; break;
    case BS_KEY:
        ui_rrect(x, y, w, h, 6, theme_mix(ui_pal.surface2, ui_pal.bg, 0.5f));
        ui_rrect(x + 2, y + 2, w - 4, h - 9, 5, filled ? fill : (e->action == ACT_PLAY ? ui_accent : ui_pal.surface2));
        bg = fill;
        cy -= 3;
        break;
    case BS_SQUARE: ui_rect(x, y, w, h, fill); bg = fill; break;
    default: break;
    }
    int ic = e->icon >= 0 ? e->icon : default_icon(e->action, c->playing);
    if (e->action == ACT_PLAY && (ic == IC_PLAY || ic == IC_PAUSE)) ic = c->playing ? IC_PAUSE : IC_PLAY;
    float isz = fminf(w, h) * 0.55f;
    u32 col = ((e->style == BS_KEY && e->action == ACT_PLAY && !filled) || (filled && e->color_ref == TC_ACCENT)) ? ui_pal.on_accent : ui_pal.text;
    bool has_label = e->label[0] != 0;
    float icy = has_label ? cy - 7 : cy;
    if (!custom_draw_icon(ic, cx, icy, isz)) builtin_action_icon(e->action, c->playing, cx, icy, isz, col, bg, c);
    if (has_label) {
        const char *l = e->label;
        if (!strcmp(l, "$speed")) l = c->speed;
        else if (!strcmp(l, "$sleep")) l = c->sleep;
        ui_text(l, cx, cy + h * 0.18f, 0.42f, col, UI_CENTER);
    }
}

static void draw_vu(const TfElem *e, const CLCtx *c) {
    float lvl = e->vu_ch == 1 ? c->vu_l : (e->vu_ch == 2 ? c->vu_r : (c->vu_l + c->vu_r) * 0.5f);
    if (lvl < 0) lvl = 0;
    if (lvl > 1) lvl = 1;
    u32 on = ecolor(e->color_ref, e->color, e->alpha), off = ecolor(e->color2_ref, e->color2, e->alpha);
    if (e->vu_kind == 0) {                                     // segment bars
        int n = e->segs < 2 ? 2 : (e->segs > 40 ? 40 : e->segs);
        float sw = e->w / n;
        int lit = (int)(lvl * n + 0.5f);
        for (int i = 0; i < n; i++) ui_rect(e->x + i * sw, e->y, sw - 2, e->h, i < lit ? on : off);
    } else {                                                   // analog needle
        float px = e->x + e->w / 2, py = e->y + e->h, r = e->h * 0.92f;
        for (int i = 0; i <= 6; i++) {
            float a = (-60.0f + 20.0f * i) * 0.0174533f;
            ui_line(px + sinf(a) * r * 0.8f, py - cosf(a) * r * 0.8f, px + sinf(a) * r * 0.96f, py - cosf(a) * r * 0.96f, 1.5f, off);
        }
        float a = (-60.0f + 120.0f * lvl) * 0.0174533f;
        ui_line(px, py, px + sinf(a) * r, py - cosf(a) * r, 2.0f, on);
        ui_circle(px, py, 4, on);
    }
}

static void draw_wave(const TfElem *e) {
    u32 col = ecolor(e->color_ref, e->color, e->alpha);
    float phase = g_anim_ms / 1000.0f * e->speed, px = e->x, py = e->y + e->amp * sinf(phase / e->wavelen * TAU);
    for (float xs = 6; xs <= e->w + 5; xs += 6) {
        float yy = e->y + e->amp * sinf((xs + phase) / e->wavelen * TAU);
        ui_line(px, py, e->x + xs, yy, e->width, col);
        px = e->x + xs;
        py = yy;
    }
}

// follow / bob / pulse moves any shape, not just images: one .pid file can have a boat that sails across the screen
static void animate_shape(TfElem *e, const CLCtx *c) {
    float dx = 0, dy = 0, t = g_anim_ms;
    if (e->follow) {
        float f = e->follow == FOLLOW_CHAPTER ? c->frac_chapter : c->frac_book;
        dx += (e->tx - e->x) * f;
        dy += (e->ty - e->y) * f;
    }
    if (e->bob_ms > 0) dy += e->bob_amp * sinf(t * TAU / e->bob_ms);
    if (e->pulse_ms > 0) {
        float sc = 1.0f + e->pulse_amt * sinf(t * TAU / e->pulse_ms);
        if (e->kind == EL_CIRCLE) e->radius *= sc;
        else if (e->kind == EL_RECT) { e->x += e->w * (1 - sc) / 2; e->y += e->h * (1 - sc) / 2; e->w *= sc; e->h *= sc; }
        else if (e->kind == EL_TEXT) e->size *= sc;
    }
    e->x += dx; e->y += dy;
    e->x2 += dx; e->y2 += dy;
}

static void draw_elems(const TfElem *arr, int n, const CLCtx *c) {
    for (int i = 0; i < n; i++) {
        const TfElem *e = &arr[i];
        if (!visible(e, c)) continue;
        TfElem moved;
        if ((e->kind == EL_RECT || e->kind == EL_CIRCLE || e->kind == EL_LINE || e->kind == EL_TEXT || e->kind == EL_WAVE || e->kind == EL_VU) &&
            (e->follow || e->bob_ms > 0 || e->pulse_ms > 0)) {
            moved = *e;
            animate_shape(&moved, c);
            e = &moved;
        }
        switch (e->kind) {
        case EL_RECT: {
            u32 col = ecolor(e->color_ref, e->color, e->alpha);
            if (e->color2 != 0 || e->color2_ref >= 0) {                      // vertical gradient to the second colour
                u32 c2 = ecolor(e->color2_ref, e->color2, e->alpha);
                C2D_DrawRectangle(e->x, e->y, 0.5f, e->w, e->h, col, col, c2, c2);
            } else if (e->radius > 0.5f) ui_rrect(e->x, e->y, e->w, e->h, e->radius, col);
            else ui_rect(e->x, e->y, e->w, e->h, col);
            break;
        }
        case EL_CIRCLE: ui_circle(e->x, e->y, e->radius, ecolor(e->color_ref, e->color, e->alpha)); break;
        case EL_LINE: ui_line(e->x, e->y, e->x2, e->y2, e->width, ecolor(e->color_ref, e->color, e->alpha)); break;
        case EL_IMAGE: draw_image(e, c); break;
        case EL_COVER:
            if (!c->has_cover) ui_cover_placeholder(e->x, e->y, e->size, c->title);
            else if (e->spin != 0 || e->rot != 0) cover_draw_centered(e->x + e->size / 2, e->y + e->size / 2, e->size, (e->rot + e->spin * g_anim_ms / 1000.0f) * 0.0174533f);
            else cover_draw(e->x, e->y, e->size);
            break;
        case EL_TEXT: draw_text(e, c); break;
        case EL_BAR: {
            float f = e->follow == FOLLOW_CHAPTER ? c->frac_chapter : c->frac_book;
            ui_bar(e->x, e->y, e->w, e->h, f, ecolor(e->color2_ref, e->color2, e->alpha), ecolor(e->color_ref, e->color, e->alpha));
            break;
        }
        case EL_BUTTON: draw_button(e, c); break;
        case EL_SEEK: {
            float cy = e->y + e->h / 2.0f;
            ui_seekbar(e->x + 12, cy - ui_style->bar_h / 2, e->w - 24, c->seeking ? c->seek_frac : c->frac_chapter, c->seeking);
            break;
        }
        case EL_BRIGHT:
            if (c->lcd_ok) {
                float cy = e->y + e->h / 2.0f;
                ui_icon_sun(e->x + 12, cy, 7, ui_pal.dim);
                ui_seekbar(e->x + 30, cy - ui_style->bar_h / 2, e->w - 68, c->bright_pct / 100.0f, false);
                char b[8];
                snprintf(b, sizeof b, "%d%%", c->bright_pct);
                ui_text(b, e->x + e->w - 2, cy - 7, 0.38f, ui_pal.dim, UI_RIGHT);
            }
            break;
        case EL_VU: draw_vu(e, c); break;
        case EL_WAVE: draw_wave(e); break;
        case EL_STATUS: if (c->status) c->status(); break;
        default: break;
        }
    }
}

bool custom_top_animated(void) {
    for (int i = 0; i < g_t.ntop; i++) {
        const TfElem *e = &g_t.top[i];
        if (e->spin != 0 || e->bob_ms > 0 || e->sway_ms > 0 || e->pulse_ms > 0 || e->scroll != 0 || e->blink_ms > 0 || e->follow) return true;
        if (e->kind == EL_WAVE || e->kind == EL_VU) return true;
        if (e->kind == EL_IMAGE && e->image >= 0 && e->image < g_t.nimages && g_img[e->image].frames > 1) return true;
    }
    return false;
}

void custom_draw_top(const CLCtx *c) {
    anim_update(c);
    draw_elems(g_t.top, g_t.ntop, c);
}

void custom_draw_bottom(const CLCtx *c) {
    anim_update(c);
    draw_elems(g_t.bottom, g_t.nbottom, c);
}

int custom_hit(int x, int y) {
    for (int i = g_t.nbottom - 1; i >= 0; i--) {                // later elements are drawn on top, so they win
        const TfElem *e = &g_t.bottom[i];
        if (e->kind != EL_BUTTON) continue;
        if (x >= e->x && x < e->x + e->w && y >= e->y && y < e->y + e->h) return e->action;
    }
    return ACT_NONE;
}

static bool find_rect(int kind, short r[4]) {
    for (int i = 0; i < g_t.nbottom; i++) {
        const TfElem *e = &g_t.bottom[i];
        if (e->kind != kind) continue;
        r[0] = (short)e->x; r[1] = (short)e->y; r[2] = (short)e->w; r[3] = (short)e->h;
        return true;
    }
    return false;
}

bool custom_seek_rect(short r[4]) { return g_active && find_rect(EL_SEEK, r); }
bool custom_bright_rect(short r[4]) { return g_active && find_rect(EL_BRIGHT, r); }
