# Making Narrate themes

A custom theme is either

* a **`.pid`** file - one plain-text file, shapes and colours only, or
* a **`.pix`** archive - a ZIP containing `theme.pid` plus icons, images and a font.

Copy it to `sdmc:/3ds/narrate/themes/` and pick it under **Settings > Custom theme**
(`sea.pid` or `sea.pix` shows up as "sea"). Choose *None* to go back to the built-in styles.
Two examples ship with the app: `themes/sea.pid` (shapes only) and `themes/ember.pix` (full archive).

Narrate never crashes on a bad theme: unknown keys or commands are counted as warnings and shown in a
message when you apply it. Check a theme on your PC first with `tools/pixpack.py lint`.

## Making a `.pix`

```
mytheme/
  theme.pid          <- required, top level
  icons/play.png ...
  images/bg.png ...
  fonts/my.bcfnt     <- optional
python3 tools/pixpack.py pack mytheme/        # -> mytheme.pix
python3 tools/pixpack.py lint mytheme.pix
```

Limits: 400 files, 12 MB per file, 40 MB unpacked. Paths must be relative (no `..`, no `:`).
PNGs larger than 512 px are shrunk on the console; all images together may use about 6 MB of texture memory.

## theme.pid

INI style. `;` starts a comment. Section and key names are case-insensitive.

```
[theme]
name = Ember
author = You
desc = One line shown nowhere important
base = Campfire        ; built-in style to inherit shapes from (any name from Settings > Style)
layout = campfire      ; built-in layout used for any screen the theme does not draw itself
mode = dark            ; dark | light | both  (which palette(s) the theme provides)
icon_tint = none       ; none | text | accent  (recolour the icon PNGs?)
image_flip = none      ; none | v | h | both   (if your art shows up upside down)

[colors.dark]          ; also [colors.light], or [colors] for both
bg  bg2  surface  surface2  text  dim  on_accent  line  accent      ; each  name = #RRGGBB
; giving bg2 makes the background a vertical gradient bg -> bg2

[shape]
r_card r_pill bar_h play_r         ; numbers (r_pill = -1: fully round)
play = circle|squircle|square|outline
row  = card|flat|pill|invert       tab = pill|underline|bracket
bar  = round|line|segments         knob = circle|pill|glow|none
btn  = filled|outline              icons = solid|outline

[font]
file = fonts/my.bcfnt              ; BCFNT font (use 3ds-devkitpro's mkbcfnt)

[images]
name = images/file.png [frames=N fps=F]     ; frames: a sprite sheet laid out left to right

[icons]
play pause prev next back fwd chapters sleep settings bookmark screen_off speed folder book check sun sort close
  = icons/name.png
```

## Drawing the screens

`[top]` is 400x240, `[bottom]` is 320x240. Elements are drawn in order (later = on top).
If a section is missing, the `layout`/style provides it, so you can theme only the top screen.

| command | arguments |
|---|---|
| `rect x y w h #colour [radius=] [bg=#c2]` | `bg=` makes a vertical gradient |
| `circle x y r #colour` | |
| `line x1 y1 x2 y2 #colour width=` | |
| `image name x y [w=] [h=] [fit=contain\|cover\|stretch] [alpha=]` | `name` from `[images]` |
| `cover x y size` | the book cover |
| `text field x y [w=] [size=] [align=] [lines=] [color=] [bg=]` | `bg=` = drop shadow |
| `bar book\|chapter x y w h color= bg=` | progress bar |
| `button action x y w h [style=] [label=] [icon=] [color=]` | touch button |
| `seek x y w h` | chapter seek bar (touch + drag) |
| `bright x y w h` | brightness slider |
| `vu x y w h [kind=segments\|needle] [ch=l\|r] [segs=] [color=] [bg=]` | audio level |
| `wave x y w amp= len= speed= color= width=` | animated sine wave |
| `status` | clock, battery and volume |

**text fields:** `title artist chapter elapsed remaining left percent chapnum clock speed sleep volume`,
or any quoted literal: `text "NOW PLAYING" 160 40`.
**colours:** `#RRGGBB`, `#RRGGBBAA`, or a palette name (`text dim accent surface ...`) so the element follows dark/light and the accent.
**button actions:** `play back fwd prev next chapters sleep settings bookmark screen_off speed`.
**button styles:** `plain pill round key square`. Labels may use `$speed` and `$sleep`.

### Animation attributes (any element that makes sense)

`spin=deg/s`, `bob=amp,ms`, `sway=deg,ms`, `pulse=amount,ms`, `scroll=px/s` (images tile endlessly),
`blink=ms`, `when=always|playing|paused`, `follow=book|chapter` with `to=x,y` (the element travels from its
position to `x,y` as the book/chapter progresses). Animations run only while playing and only the top screen
is redrawn at the animation frame rate, so keep the top screen light on slow consoles.

## Tips

* Start from `sea.pid`; it is short and shows most commands.
* A theme with no animated elements costs no extra CPU: the screen only redraws when something changes.
* `tools/make_ember.py` shows how to generate PNG icons and a sprite sheet with just Python.
