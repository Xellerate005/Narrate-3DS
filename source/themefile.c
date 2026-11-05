// themefile.c
#include "themefile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PACK(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

static const char *COLOR_NAMES[TC_COUNT] = {"bg", "bg2", "surface", "surface2", "text", "dim", "on_accent", "line", "accent"};
static const char *FIELD_NAMES[] = {"", "title", "artist", "chapter", "elapsed", "remaining", "left", "percent", "chapnum", "clock", "speed", "sleep", "volume"};
static const char *ACTION_NAMES[ACT_COUNT] = {"", "play", "back", "fwd", "prev", "next", "chapters", "sleep", "settings", "bookmark", "screen_off", "speed"};
static const char *ICON_NAMES[IC_COUNT] = {"play", "pause", "prev", "next", "back", "fwd", "chapters", "sleep", "settings", "bookmark", "screen_off", "speed",
                                           "folder", "book", "check", "sun", "sort", "close"};
static const char *PLAY_N[] = {"circle", "bare", "squircle", "square", "outline"};
static const char *ROW_N[] = {"card", "flat", "pill", "invert", "outline"};
static const char *TAB_N[] = {"pill", "seg", "underline", "bracket"};
static const char *BAR_N[] = {"round", "segments", "line"};
static const char *KNOB_N[] = {"none", "circle", "pill", "glow"};
static const char *BTN_N[] = {"none", "filled", "outline"};
static const char *ICONS_N[] = {"solid", "outline"};
static const char *BSTYLE_N[] = {"none", "pill", "round", "key", "square"};

const char *tf_icon_name(int icon) { return (icon >= 0 && icon < IC_COUNT) ? ICON_NAMES[icon] : ""; }

static void warn(TfTheme *t, const char *fmt, const char *a) {
    t->warnings++;
    if (!t->first_warning[0]) {
        char b[96];
        snprintf(b, sizeof b, fmt, a ? a : "");
        snprintf(t->first_warning, sizeof t->first_warning, "%s", b);
    }
}

static int lookup(const char *s, const char *const *names, int n) {
    for (int i = 0; i < n; i++) if (strcasecmp(s, names[i]) == 0) return i;
    return -1;
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static float num(const char *s, float def) {
    if (!s || !*s) return def;
    char *e;
    float v = strtof(s, &e);
    return e == s ? def : v;
}

static void pair(const char *s, float *a, float *b) {          // "a,b"
    if (!s) return;
    char *e;
    float v = strtof(s, &e);
    if (e == s) return;
    *a = v;
    if (*e == ',') *b = strtof(e + 1, NULL);
}

// colour: "#RRGGBB", "#RRGGBBAA", "#RGB", "none", or a palette name (text, accent, ...)
static bool parse_color(const char *s, uint32_t *rgba, int *ref) {
    *ref = -1;
    if (!s || !*s) return false;
    int ci = lookup(s, COLOR_NAMES, TC_COUNT);
    if (ci >= 0) { *ref = ci; return true; }
    if (!strcasecmp(s, "none") || !strcasecmp(s, "transparent")) { *rgba = 0; return true; }
    if (s[0] != '#') return false;
    size_t n = strlen(s + 1);
    for (size_t i = 1; i <= n; i++) if (!isxdigit((unsigned char)s[i])) return false;
    unsigned v = (unsigned)strtoul(s + 1, NULL, 16);
    if (n == 6) { *rgba = PACK((v >> 16) & 255, (v >> 8) & 255, v & 255, 255); return true; }
    if (n == 8) { *rgba = PACK((v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255); return true; }
    if (n == 3) { *rgba = PACK(((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, 255); return true; }
    return false;
}

typedef struct {
    char *pos[16]; int npos;
    struct { char *k, *v; } kv[28]; int nkv;
} Args;

// in-place tokenizer: whitespace separated, "double quotes" keep spaces, key=value become attributes
static void tokenize(char *s, Args *a) {
    a->npos = a->nkv = 0;
    char *p = s;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *start = p, *w = p;
        bool q = false;
        while (*p && (q || !isspace((unsigned char)*p))) {
            if (*p == '"') { q = !q; p++; continue; }
            *w++ = *p++;
        }
        char *end = w;
        if (*p) p++;
        *end = 0;
        char *eq = strchr(start, '=');
        if (eq && eq != start && a->nkv < 28) { *eq = 0; a->kv[a->nkv].k = start; a->kv[a->nkv].v = eq + 1; a->nkv++; }
        else if (a->npos < 16) a->pos[a->npos++] = start;
    }
}

static const char *attr(const Args *a, const char *key) {
    for (int i = 0; i < a->nkv; i++) if (strcasecmp(a->kv[i].k, key) == 0) return a->kv[i].v;
    return NULL;
}

static void set_color(const Args *a, const char *key, uint32_t *rgba, int *ref, uint32_t def, int defref) {
    const char *v = attr(a, key);
    uint32_t c; int r;
    if (v && parse_color(v, &c, &r)) { *rgba = c; *ref = r; }
    else { *rgba = def; *ref = defref; }
}

enum { SEC_NONE, SEC_THEME, SEC_COLORS_ANY, SEC_COLORS_DARK, SEC_COLORS_LIGHT, SEC_SHAPE, SEC_FONT, SEC_IMAGES, SEC_ICONS, SEC_TOP, SEC_BOTTOM };

static void add_element(TfTheme *t, TfElem *arr, int *n, bool is_top, char *line) {
    Args a;
    tokenize(line, &a);
    if (a.npos == 0) return;
    const char *cmd = a.pos[0];
    if (*n >= TF_MAX_ELEMS) { warn(t, "too many elements (max %s)", "96"); return; }

    TfElem e;
    bool bad_action = false;
    memset(&e, 0, sizeof e);
    e.image = -1; e.icon = -1; e.alpha = 1.0f; e.size = 0.5f; e.lines = 1; e.segs = 0;
    e.color = PACK(255, 255, 255, 255); e.color_ref = TC_TEXT; e.color2_ref = -1;
    int p = 1;
#define NEXTNUM(dst) do { if (p < a.npos) (dst) = num(a.pos[p++], (dst)); } while (0)

    if (!strcasecmp(cmd, "rect")) {
        e.kind = EL_RECT; NEXTNUM(e.x); NEXTNUM(e.y); NEXTNUM(e.w); NEXTNUM(e.h);
        e.color_ref = TC_SURFACE;
    } else if (!strcasecmp(cmd, "circle")) {
        e.kind = EL_CIRCLE; NEXTNUM(e.x); NEXTNUM(e.y); NEXTNUM(e.radius);
        e.color_ref = TC_ACCENT;
    } else if (!strcasecmp(cmd, "line")) {
        e.kind = EL_LINE; NEXTNUM(e.x); NEXTNUM(e.y); NEXTNUM(e.x2); NEXTNUM(e.y2);
        e.width = 2; e.color_ref = TC_LINE;
    } else if (!strcasecmp(cmd, "image")) {
        e.kind = EL_IMAGE;
        if (p < a.npos) {
            const char *nm = a.pos[p++];
            for (int i = 0; i < t->nimages; i++) if (!strcasecmp(t->images[i].name, nm)) e.image = i;
            if (e.image < 0) warn(t, "unknown image '%s'", nm);
        }
        NEXTNUM(e.x); NEXTNUM(e.y);
        if (p < a.npos) { e.w = num(a.pos[p++], 0); e.has_w = true; }
        if (p < a.npos) { e.h = num(a.pos[p++], 0); e.has_h = true; }
    } else if (!strcasecmp(cmd, "cover")) {
        e.kind = EL_COVER; NEXTNUM(e.x); NEXTNUM(e.y); NEXTNUM(e.size);
        if (e.size < 4) e.size = 100;
    } else if (!strcasecmp(cmd, "text")) {
        e.kind = EL_TEXT;
        if (p < a.npos) {
            const char *f = a.pos[p++];
            int fi = lookup(f, FIELD_NAMES, 13);
            if (fi > 0) e.field = fi;
            else { e.field = F_LITERAL; snprintf(e.literal, sizeof e.literal, "%s", f); }
        }
        NEXTNUM(e.x); NEXTNUM(e.y);
    } else if (!strcasecmp(cmd, "bar")) {
        e.kind = EL_BAR;
        if (p < a.npos) e.follow = !strcasecmp(a.pos[p++], "chapter") ? FOLLOW_CHAPTER : FOLLOW_BOOK;
        NEXTNUM(e.x); NEXTNUM(e.y); NEXTNUM(e.w); NEXTNUM(e.h);
        e.color_ref = TC_ACCENT; e.color2_ref = TC_SURFACE2;
    } else if (!strcasecmp(cmd, "button")) {
        e.kind = EL_BUTTON;
        if (p < a.npos) {
            e.action = lookup(a.pos[p], ACTION_NAMES, ACT_COUNT);
            if (e.action <= 0) { warn(t, "unknown button action '%s'", a.pos[p]); e.action = ACT_NONE; bad_action = true; }
            p++;
        }
        NEXTNUM(e.x); NEXTNUM(e.y); NEXTNUM(e.w); NEXTNUM(e.h);
        e.style = BS_PILL;
    } else if (!strcasecmp(cmd, "seek") || !strcasecmp(cmd, "bright")) {
        e.kind = !strcasecmp(cmd, "seek") ? EL_SEEK : EL_BRIGHT;
        NEXTNUM(e.x); NEXTNUM(e.y); NEXTNUM(e.w); NEXTNUM(e.h);
    } else if (!strcasecmp(cmd, "vu")) {
        e.kind = EL_VU; NEXTNUM(e.x); NEXTNUM(e.y); NEXTNUM(e.w); NEXTNUM(e.h);
        e.color_ref = TC_ACCENT; e.color2_ref = TC_SURFACE2; e.segs = 12;
    } else if (!strcasecmp(cmd, "wave")) {
        e.kind = EL_WAVE; NEXTNUM(e.x); NEXTNUM(e.y); NEXTNUM(e.w);
        e.amp = 8; e.wavelen = 60; e.speed = 40; e.width = 2; e.color_ref = TC_ACCENT;
    } else if (!strcasecmp(cmd, "status")) {
        e.kind = EL_STATUS;
    } else {
        warn(t, "unknown command '%s'", cmd);
        return;
    }
#undef NEXTNUM
    // a trailing positional colour is allowed as shorthand: rect 0 0 400 240 #0A2A3C
    for (; p < a.npos && e.kind != EL_BUTTON; p++) {
        uint32_t c; int r;
        if (parse_color(a.pos[p], &c, &r)) { e.color = c; e.color_ref = r; break; }
    }

    const char *v;
    set_color(&a, "color", &e.color, &e.color_ref, e.color, e.color_ref);
    set_color(&a, "bg", &e.color2, &e.color2_ref, e.color2, e.color2_ref);
    if ((v = attr(&a, "color2"))) { uint32_t c; int r; if (parse_color(v, &c, &r)) { e.color2 = c; e.color2_ref = r; } }
    if ((v = attr(&a, "radius"))) e.radius = num(v, e.radius);
    if ((v = attr(&a, "width"))) e.width = num(v, e.width);
    if ((v = attr(&a, "size"))) e.size = num(v, e.size);
    if ((v = attr(&a, "rot"))) e.rot = num(v, 0);
    if ((v = attr(&a, "alpha"))) e.alpha = num(v, 1) > 1.0f ? num(v, 255) / 255.0f : num(v, 1);
    if ((v = attr(&a, "w"))) { e.w = num(v, e.w); e.has_w = true; }
    if ((v = attr(&a, "h"))) { e.h = num(v, e.h); e.has_h = true; }
    if ((v = attr(&a, "lines"))) e.lines = (int)num(v, 1) < 1 ? 1 : ((int)num(v, 1) > 3 ? 3 : (int)num(v, 1));
    if ((v = attr(&a, "align"))) { if (!strcasecmp(v, "center")) e.align = 1; else if (!strcasecmp(v, "right")) e.align = 2; }
    if ((v = attr(&a, "to"))) { e.tx = e.x; e.ty = e.y; pair(v, &e.tx, &e.ty); if (!e.follow) e.follow = FOLLOW_BOOK; }
    if ((v = attr(&a, "follow"))) e.follow = !strcasecmp(v, "chapter") ? FOLLOW_CHAPTER : (!strcasecmp(v, "book") ? FOLLOW_BOOK : FOLLOW_NONE);
    if ((v = attr(&a, "spin"))) e.spin = num(v, 0);
    if ((v = attr(&a, "bob"))) { e.bob_ms = 1000; pair(v, &e.bob_amp, &e.bob_ms); }
    if ((v = attr(&a, "sway"))) { e.sway_ms = 1000; pair(v, &e.sway_deg, &e.sway_ms); }
    if ((v = attr(&a, "pulse"))) { e.pulse_ms = 1000; pair(v, &e.pulse_amt, &e.pulse_ms); }
    if ((v = attr(&a, "scroll"))) e.scroll = num(v, 0);
    if ((v = attr(&a, "blink"))) e.blink_ms = (int)num(v, 0);
    if ((v = attr(&a, "when"))) e.when = !strcasecmp(v, "playing") ? WHEN_PLAYING : (!strcasecmp(v, "paused") ? WHEN_PAUSED : WHEN_ALWAYS);
    if ((v = attr(&a, "fit"))) e.fit = !strcasecmp(v, "stretch") ? FIT_STRETCH : (!strcasecmp(v, "cover") ? FIT_COVER : FIT_CONTAIN);
    if ((v = attr(&a, "icon"))) { e.icon = lookup(v, ICON_NAMES, IC_COUNT); if (e.icon < 0) warn(t, "unknown icon '%s'", v); }
    if ((v = attr(&a, "label"))) snprintf(e.label, sizeof e.label, "%s", v);
    if ((v = attr(&a, "style"))) { int s = lookup(v, BSTYLE_N, 5); if (s >= 0) e.style = s; }
    if ((v = attr(&a, "kind"))) { if (!strcasecmp(v, "needle")) e.vu_kind = 1; else if (!strcasecmp(v, "chapter")) e.follow = FOLLOW_CHAPTER; }
    if ((v = attr(&a, "channel"))) e.vu_ch = !strcasecmp(v, "left") ? 1 : (!strcasecmp(v, "right") ? 2 : 0);
    if ((v = attr(&a, "segs"))) e.segs = (int)num(v, 12);
    if ((v = attr(&a, "amp"))) e.amp = num(v, e.amp);
    if ((v = attr(&a, "len"))) e.wavelen = num(v, e.wavelen) < 4 ? 4 : num(v, e.wavelen);
    if ((v = attr(&a, "speed"))) e.speed = num(v, e.speed);
    if (e.kind == EL_BUTTON && e.action == ACT_NONE) { if (!bad_action) warn(t, "button without an action%s", ""); return; }
    (void)is_top;
    arr[(*n)++] = e;
}

static void set_enum(TfTheme *t, int *dst, const char *v, const char *const *names, int n, const char *what) {
    int i = lookup(v, names, n);
    if (i >= 0) *dst = i; else warn(t, what, v);
}

static void handle_kv(TfTheme *t, int sec, const char *key, const char *val) {
    int pal = (sec == SEC_COLORS_LIGHT) ? 1 : 0;
    switch (sec) {
    case SEC_THEME:
        if (!strcasecmp(key, "name")) snprintf(t->name, sizeof t->name, "%s", val);
        else if (!strcasecmp(key, "author")) snprintf(t->author, sizeof t->author, "%s", val);
        else if (!strcasecmp(key, "desc") || !strcasecmp(key, "description")) snprintf(t->desc, sizeof t->desc, "%s", val);
        else if (!strcasecmp(key, "base")) snprintf(t->base, sizeof t->base, "%s", val);
        else if (!strcasecmp(key, "layout")) snprintf(t->layout, sizeof t->layout, "%s", val);
        else if (!strcasecmp(key, "mode")) t->mode = !strcasecmp(val, "dark") ? 1 : (!strcasecmp(val, "light") ? 2 : 0);
        else if (!strcasecmp(key, "icon_tint")) t->icon_tint = !strcasecmp(val, "text") ? 1 : (!strcasecmp(val, "accent") ? 2 : 0);
        else if (!strcasecmp(key, "image_flip")) t->image_flip = !strcasecmp(val, "v") ? 1 : (!strcasecmp(val, "h") ? 2 : (!strcasecmp(val, "both") ? 3 : 0));
        else warn(t, "unknown [theme] key '%s'", key);
        break;
    case SEC_COLORS_ANY: case SEC_COLORS_DARK: case SEC_COLORS_LIGHT: {
        int ci = lookup(key, COLOR_NAMES, TC_COUNT);
        uint32_t c; int ref;
        if (ci < 0) { warn(t, "unknown colour '%s'", key); break; }
        if (!parse_color(val, &c, &ref) || ref >= 0) { warn(t, "bad colour value '%s'", val); break; }
        if (sec == SEC_COLORS_ANY) { t->pal[0][ci] = t->pal[1][ci] = c; t->has[0][ci] = t->has[1][ci] = true; }
        else { t->pal[pal][ci] = c; t->has[pal][ci] = true; }
        break;
    }
    case SEC_SHAPE:
        if (!strcasecmp(key, "r_card")) { t->r_card = num(val, 0); t->has_r_card = true; }
        else if (!strcasecmp(key, "r_pill")) { t->r_pill = num(val, 0); t->has_r_pill = true; }
        else if (!strcasecmp(key, "bar_h")) { t->bar_h = num(val, 5); t->has_bar_h = true; }
        else if (!strcasecmp(key, "play_r")) { t->play_r = num(val, 24); t->has_play_r = true; }
        else if (!strcasecmp(key, "play")) set_enum(t, &t->play, val, PLAY_N, 5, "bad play shape '%s'");
        else if (!strcasecmp(key, "row")) set_enum(t, &t->row, val, ROW_N, 5, "bad row style '%s'");
        else if (!strcasecmp(key, "tab")) set_enum(t, &t->tab, val, TAB_N, 4, "bad tab style '%s'");
        else if (!strcasecmp(key, "bar")) set_enum(t, &t->bar, val, BAR_N, 3, "bad bar style '%s'");
        else if (!strcasecmp(key, "knob")) set_enum(t, &t->knob, val, KNOB_N, 4, "bad knob style '%s'");
        else if (!strcasecmp(key, "btn")) set_enum(t, &t->btn, val, BTN_N, 3, "bad button style '%s'");
        else if (!strcasecmp(key, "icons")) set_enum(t, &t->icons, val, ICONS_N, 2, "bad icon style '%s'");
        else warn(t, "unknown [shape] key '%s'", key);
        break;
    case SEC_FONT:
        if (!strcasecmp(key, "file")) snprintf(t->font, sizeof t->font, "%s", val);
        else warn(t, "unknown [font] key '%s'", key);
        break;
    case SEC_ICONS: {
        int ic = lookup(key, ICON_NAMES, IC_COUNT);
        if (ic < 0) { warn(t, "unknown icon '%s'", key); break; }
        snprintf(t->icon_path[ic], sizeof t->icon_path[ic], "%s", val);
        break;
    }
    default: break;
    }
}

static void handle_image(TfTheme *t, const char *key, char *val) {
    if (t->nimages >= TF_MAX_IMAGES) { warn(t, "too many images (max %s)", "24"); return; }
    Args a;
    tokenize(val, &a);
    if (!a.npos) { warn(t, "image '%s' has no file", key); return; }
    TfImage *im = &t->images[t->nimages++];
    memset(im, 0, sizeof *im);
    snprintf(im->name, sizeof im->name, "%s", key);
    snprintf(im->path, sizeof im->path, "%s", a.pos[0]);
    im->frames = 1; im->fps = 8;
    const char *v;
    if ((v = attr(&a, "frames"))) { im->frames = (int)num(v, 1); if (im->frames < 1) im->frames = 1; if (im->frames > 64) im->frames = 64; }
    if ((v = attr(&a, "fps"))) { im->fps = num(v, 8); if (im->fps < 0.1f) im->fps = 0.1f; }
}

void tf_parse(const char *text, size_t len, TfTheme *t) {
    memset(t, 0, sizeof *t);
    t->play = t->row = t->tab = t->bar = t->knob = t->btn = t->icons = -1;
    int sec = SEC_NONE;
    size_t pos = 0;
    while (pos < len) {
        size_t e = pos;
        while (e < len && text[e] != '\n') e++;
        char line[400];
        size_t n = e - pos < sizeof line - 1 ? e - pos : sizeof line - 1;
        memcpy(line, text + pos, n);
        line[n] = 0;
        pos = e + 1;

        // strip comments: whole-line '#', '//' , or ';' outside quotes
        char *w = line;
        while (isspace((unsigned char)*w)) w++;
        if (*w == '#' || (w[0] == '/' && w[1] == '/')) continue;
        bool q = false;
        for (char *p = w; *p; p++) {
            if (*p == '"') q = !q;
            else if (*p == ';' && !q) { *p = 0; break; }
        }
        trim(w);
        if (!*w) continue;

        if (*w == '[') {
            char *close = strchr(w, ']');
            if (!close) { warn(t, "bad section header '%s'", w); sec = SEC_NONE; continue; }
            *close = 0;
            const char *s = w + 1;
            if (!strcasecmp(s, "theme")) sec = SEC_THEME;
            else if (!strcasecmp(s, "colors")) sec = SEC_COLORS_ANY;
            else if (!strcasecmp(s, "colors.dark")) sec = SEC_COLORS_DARK;
            else if (!strcasecmp(s, "colors.light")) sec = SEC_COLORS_LIGHT;
            else if (!strcasecmp(s, "shape")) sec = SEC_SHAPE;
            else if (!strcasecmp(s, "font")) sec = SEC_FONT;
            else if (!strcasecmp(s, "images")) sec = SEC_IMAGES;
            else if (!strcasecmp(s, "icons")) sec = SEC_ICONS;
            else if (!strcasecmp(s, "top")) { sec = SEC_TOP; t->has_top = true; }
            else if (!strcasecmp(s, "bottom")) { sec = SEC_BOTTOM; t->has_bottom = true; }
            else { warn(t, "unknown section [%s]", s); sec = SEC_NONE; }
            continue;
        }
        if (sec == SEC_TOP) { add_element(t, t->top, &t->ntop, true, w); continue; }
        if (sec == SEC_BOTTOM) { add_element(t, t->bottom, &t->nbottom, false, w); continue; }

        char *eq = strchr(w, '=');
        if (!eq || sec == SEC_NONE) { warn(t, "ignored line '%s'", w); continue; }
        *eq = 0;
        char *key = w, *val = eq + 1;
        trim(key);
        trim(val);
        if (sec == SEC_IMAGES) handle_image(t, key, val);
        else handle_kv(t, sec, key, val);
    }
}
