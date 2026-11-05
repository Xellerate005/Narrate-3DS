#!/usr/bin/env python3
"""
make_ember.py - generates the artwork + manifest of the "Ember" demo theme into ../themes/ember/ with nothing but the
Python standard library, as a worked example of what a .pix theme contains (see THEMES.md).

  python3 make_ember.py            # writes ../themes/ember/
  python3 pixpack.py pack ../themes/ember -o ../themes/ember.pix
"""
import math, os, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "themes", "ember")


def write_png(path, w, h, rgba):
    """rgba: bytes of w*h*4 (R,G,B,A) top-down."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgba[y * w * 4:(y + 1) * w * 4]

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


# ------------------------------------------------------------------ shape tests (64 x 64 icon space)
def poly(pts):
    def f(x, y):
        inside, j = False, len(pts) - 1
        for i in range(len(pts)):
            xi, yi = pts[i]; xj, yj = pts[j]
            if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi + 1e-12) + xi:
                inside = not inside
            j = i
        return inside
    return f

def circle(cx, cy, r): return lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r
def ring(cx, cy, r1, r2): return lambda x, y: r1 * r1 <= (x - cx) ** 2 + (y - cy) ** 2 <= r2 * r2

def rrect(x0, y0, x1, y1, r):
    def f(x, y):
        if not (x0 <= x <= x1 and y0 <= y <= y1): return False
        cx = min(max(x, x0 + r), x1 - r); cy = min(max(y, y0 + r), y1 - r)
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    return f

def seg(x0, y0, x1, y1, w):
    dx, dy = x1 - x0, y1 - y0
    L2 = dx * dx + dy * dy or 1e-9
    def f(x, y):
        t = max(0, min(1, ((x - x0) * dx + (y - y0) * dy) / L2))
        return (x - (x0 + t * dx)) ** 2 + (y - (y0 + t * dy)) ** 2 <= (w / 2) ** 2
    return f

def mirror(fn): return lambda x, y: fn(64 - x, y)

ICONS = {
    "play": ([poly([(22, 12), (22, 52), (54, 32)])], []),
    "pause": ([rrect(17, 12, 28, 52, 3), rrect(36, 12, 47, 52, 3)], []),
    "prev": ([rrect(12, 12, 19, 52, 2), poly([(52, 12), (52, 52), (24, 32)])], []),
    "next": ([poly([(12, 12), (12, 52), (40, 32)]), rrect(45, 12, 52, 52, 2)], []),
    "back": ([ring(32, 37, 17, 23), poly([(10, 14), (34, 0), (34, 28)])], [poly([(32, 37), (24, 8), (42, 8)])]),
    "fwd": ([mirror(ring(32, 37, 17, 23)), mirror(poly([(10, 14), (34, 0), (34, 28)]))], [mirror(poly([(32, 37), (24, 8), (42, 8)]))]),
    "chapters": ([seg(24, 18, 52, 18, 6), seg(24, 32, 52, 32, 6), seg(24, 46, 52, 46, 6), circle(13, 18, 3.5), circle(13, 32, 3.5), circle(13, 46, 3.5)], []),
    "sleep": ([circle(30, 32, 20)], [circle(42, 25, 17)]),
    "settings": ([circle(32, 32, 15)] + [circle(32 + 19 * math.cos(i * math.pi / 4), 32 + 19 * math.sin(i * math.pi / 4), 5.5) for i in range(8)], [circle(32, 32, 7)]),
    "bookmark": ([poly([(17, 8), (47, 8), (47, 56), (32, 43), (17, 56)])], []),
    "screen_off": ([rrect(8, 14, 56, 44, 5), rrect(24, 46, 40, 52, 2), seg(8, 56, 56, 8, 5)], [rrect(13, 19, 51, 39, 2)]),
    "speed": ([ring(32, 38, 17, 23), seg(32, 38, 44, 26, 4.5), rrect(26, 6, 38, 13, 2)], []),
    "folder": ([rrect(8, 14, 30, 26, 4), rrect(8, 22, 56, 52, 5)], []),
    "book": ([rrect(12, 8, 52, 56, 4)], [rrect(19, 17, 45, 22, 1), rrect(19, 28, 38, 33, 1), rrect(19, 39, 45, 44, 1)]),
    "check": ([seg(13, 34, 26, 47, 8), seg(26, 47, 51, 17, 8)], []),
    "sun": ([circle(32, 32, 11)] + [seg(32 + 17 * math.cos(i * math.pi / 4), 32 + 17 * math.sin(i * math.pi / 4),
                                         32 + 26 * math.cos(i * math.pi / 4), 32 + 26 * math.sin(i * math.pi / 4), 4.5) for i in range(8)], []),
    "sort": ([rrect(10, 12, 54, 21, 3), rrect(10, 28, 40, 37, 3), rrect(10, 44, 26, 53, 3)], []),
    "close": ([seg(15, 15, 49, 49, 8), seg(49, 15, 15, 49, 8)], []),
}


def render_icon(adds, subs, color=(255, 230, 189), size=64, ss=4):
    out = bytearray()
    for py in range(size):
        for px in range(size):
            hit = 0
            for sy in range(ss):
                for sx in range(ss):
                    x = (px + (sx + 0.5) / ss) * 64.0 / size
                    y = (py + (sy + 0.5) / ss) * 64.0 / size
                    if any(a(x, y) for a in adds) and not any(s(x, y) for s in subs):
                        hit += 1
            out += bytes((color[0], color[1], color[2], int(255 * hit / (ss * ss))))
    return bytes(out)


# ------------------------------------------------------------------ flame sprite sheet: 4 frames of 64 x 96
def flame_sheet(frames=4, fw=64, fh=96, ss=3):
    out = bytearray(fw * frames * fh * 4)
    layers = [(1.0, (255, 122, 26)), (0.64, (255, 194, 74)), (0.32, (255, 241, 184))]
    for k in range(frames):
        phase = k / frames
        for py in range(fh):
            for px in range(fw):
                cov = [0, 0, 0]
                for sy in range(ss):
                    for sx in range(ss):
                        x = px + (sx + 0.5) / ss
                        y = py + (sy + 0.5) / ss
                        t = y / fh
                        for li, (scale, _) in enumerate(layers):
                            sway = 6 * math.sin(2 * math.pi * (t * 1.3 + phase + li * 0.11)) * (1 - t)
                            hw = 27 * scale * math.sin(math.pi * (t ** 0.75)) * (1 + 0.08 * math.sin(2 * math.pi * (phase * 2 + t * 3)))
                            if abs(x - 32 - sway) <= hw:
                                cov[li] += 1
                n = ss * ss
                # innermost layers sit on top of the outer ones: weight each layer by the area only it covers
                a = cov[0] / n
                if cov[0]:
                    w0 = (cov[0] - cov[1]) / cov[0]
                    w1 = (cov[1] - cov[2]) / cov[0]
                    w2 = cov[2] / cov[0]
                    c0, c1, c2 = layers[0][1], layers[1][1], layers[2][1]
                    r = c0[0] * w0 + c1[0] * w1 + c2[0] * w2
                    g = c0[1] * w0 + c1[1] * w1 + c2[1] * w2
                    b = c0[2] * w0 + c1[2] * w1 + c2[2] * w2
                else:
                    r = g = b = 0.0
                i = ((py * fw * frames) + k * fw + px) * 4
                out[i:i + 4] = bytes((int(min(255, r)), int(min(255, g)), int(min(255, b)), int(255 * a)))
    return bytes(out), fw * frames, fh


def background(w, h, gx, gy, rad):
    out = bytearray()
    for y in range(h):
        t = y / (h - 1)
        base = (20 + 38 * t, 8 + 18 * t, 10 + 6 * t)
        for x in range(w):
            d = math.hypot(x - gx, y - gy) / rad
            glow = max(0.0, 1.0 - d) ** 2 * 0.55
            vig = 1.0 - 0.35 * (((x / w - 0.5) ** 2 + (y / h - 0.5) ** 2) * 2)
            out += bytes((min(255, int((base[0] + 255 * glow * 0.9) * vig)), min(255, int((base[1] + 255 * glow * 0.45) * vig)),
                          min(255, int((base[2] + 255 * glow * 0.1) * vig)), 255))
    return bytes(out)


def smoke(w=400, h=64):
    out = bytearray()
    for y in range(h):
        env = max(0.0, math.sin(math.pi * y / h)) ** 1.5
        for x in range(w):
            # every term is periodic in x with an integer number of cycles over w, so the image tiles seamlessly
            v = 0.5 + 0.5 * math.sin(2 * math.pi * (2 * x / w + y / 90.0)) * math.cos(2 * math.pi * (3 * x / w - y / 140.0))
            out += bytes((205, 192, 182, int(255 * max(0.0, min(1.0, v * env * 0.55)))))
    return bytes(out), w, h


MANIFEST = """\
; ============================================================================
;  Ember - a Narrate theme archive (.pix): custom icons, a background image,
;  an animated flame sprite sheet and a scrolling smoke layer.
;  Generated by tools/make_ember.py - see THEMES.md for the full format.
; ============================================================================

[theme]
name   = Ember
author = Narrate
desc   = Warm fire-side look with custom PNG icons and animated artwork.
base   = Campfire            ; inherit shapes from this built-in style
layout = campfire            ; any screen this theme does not define uses this built-in layout
mode   = dark                ; the artwork is dark, so always use the dark palette
icon_tint = none             ; show the icon PNGs in their own colours (text / accent would recolour them)

[colors.dark]
bg        = #14080A
surface   = #2A1410
surface2  = #3F2014
text      = #FFEBD2
dim       = #D2A583
line      = #5A3320
accent    = #FF8A2B
on_accent = #2A0F05

[shape]
r_card = 10
r_pill = -1
bar_h  = 6
play   = squircle
play_r = 18
btn    = filled
knob   = glow

; [font]
; file = fonts/ember.bcfnt   ; add a BCFNT font to the archive and un-comment to change every text in the app

[images]
bg      = images/bg.png
bgb     = images/bg_bottom.png
smoke   = images/smoke.png
flame   = images/flame.png frames=4 fps=7      ; 4 frames of 64x96 side by side (sheet is 256x96)

[icons]
play       = icons/play.png
pause      = icons/pause.png
prev       = icons/prev.png
next       = icons/next.png
back       = icons/back.png
fwd        = icons/fwd.png
chapters   = icons/chapters.png
sleep      = icons/sleep.png
settings   = icons/settings.png
bookmark   = icons/bookmark.png
screen_off = icons/screen_off.png
speed      = icons/speed.png
folder     = icons/folder.png
book       = icons/book.png
check      = icons/check.png
sun        = icons/sun.png
sort       = icons/sort.png
close      = icons/close.png

; ---------------------------------------------------------------- top screen (400 x 240)
[top]
image bg 0 0 400 240
image smoke 0 168 400 64 scroll=-12 alpha=0.55
image flame 304 84 w=72 bob=2,2200

cover 22 44 124
text "NOW PLAYING" 160 40 size=0.38 color=accent
text title    160 56  w=222 size=0.70 lines=2 color=text
text artist   160 104 w=140 size=0.44 color=dim
text chapter  160 124 w=140 size=0.46 color=accent
text elapsed  160 146 size=0.95 color=text
text left     160 184 size=0.40 color=dim
bar book 22 224 356 5 color=accent bg=surface2
status

; --------------------------------------------------------------- bottom screen (320 x 240)
[bottom]
image bgb 0 0 320 240

text chapnum 160 10 size=0.36 align=center color=accent
text chapter 160 24 w=200 size=0.52 align=center color=text

seek 12 48 296 44
text elapsed   24 80 size=0.42 color=dim
text remaining 296 80 size=0.42 align=right color=dim

button prev 8   108 54 54 style=round
button back 68  110 50 50 style=round
button play 122 96  76 72 style=round color=accent
button fwd  202 110 50 50 style=round
button next 258 108 54 54 style=round

bright 40 168 240 18

button chapters 8   190 74 36 style=pill label="Chapters"
button sleep    86  190 74 36 style=pill label="$sleep"
button speed    164 190 74 36 style=pill label="$speed"
button settings 242 190 70 36 style=pill label="More"

button screen_off 9   5 34 34 style=round
button bookmark   277 5 34 34 style=round
"""


def main():
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "theme.pid"), "w", newline="\n") as f:
        f.write(MANIFEST)
    for name, (adds, subs) in ICONS.items():
        write_png(os.path.join(OUT, "icons", name + ".png"), 64, 64, render_icon(adds, subs))
    data, w, h = flame_sheet()
    write_png(os.path.join(OUT, "images", "flame.png"), w, h, data)
    write_png(os.path.join(OUT, "images", "bg.png"), 400, 240, background(400, 240, 335, 175, 190))
    write_png(os.path.join(OUT, "images", "bg_bottom.png"), 320, 240, background(320, 240, 160, 250, 200))
    data, w, h = smoke()
    write_png(os.path.join(OUT, "images", "smoke.png"), w, h, data)
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    sys.exit(main())
