# Narrate — M4B audiobook player for the Nintendo 3DS

Plays `.m4b` / `.m4a` (AAC) audiobooks with a dark, modern UI: cover art, accent colour taken from the
cover, per-chapter seek bar, chapter list, sleep timer, and automatic resume.

## Build

You already have devkitPro. You additionally need three portlibs (cover art decoding):

    # devkitPro MSYS2 shell on Windows (use "dkp-pacman" on Linux/macOS)
    pacman -S --needed 3ds-libjpeg-turbo 3ds-libpng 3ds-zlib

Then, from this folder in the devkitPro shell:

    ./setup_deps.sh      # clones the FAAD2 AAC decoder into deps/faad2 (needs git or curl)
    make

No git? Download https://github.com/knik0/faad2 as a zip and unpack it so that
`deps/faad2/libfaad/decoder.c` exists, then run `make`.

Output: `narrate.3dsx` (+ `narrate.smdh`). Copy `narrate.3dsx` to `sdmc:/3ds/` on the SD card.
(Want it on the HOME menu instead? See **Building a CIA** below.)

## Use

* Put books in **`sdmc:/AudioBook/`** (the folder is created on first launch). Narrate only reads this folder.
  Sub-folders work as groups (series, authors, ...) and can be nested; you can't browse above `AudioBook`.
* Needs `dspfirm.cdc` on the SD card (run **DSP1** once from the Homebrew Launcher) — same requirement as any 3DS audio app.
* Resume positions are stored in `sdmc:/3ds/narrate/progress.txt`.

### Controls

| Library              |                                            |
|----------------------|--------------------------------------------|
| D-pad / touch scroll | move / scroll (shelf view: Left/Right/Up/Down move across the grid) |
| A or tap             | open folder / book (first row = **Continue listening**) |
| B (or Left in list view) | up one folder                          |
| L / R                | page up / down                             |
| Y or sort button     | cycle sort: **Name** (natural: Book 2 before Book 10) / **Recent** / **Status** (in progress, new, finished) |
| X or the circle on a row | toggle **finished** for a book - on a folder it marks every book inside |
| SELECT / gear button | **Settings**                               |
| START                | quit                                       |

Folders show how many books are finished (`3/7`) and their combined progress, so a series folder acts as a series
overview. When a book ends, Narrate carries straight on with the next unfinished book in the same folder
(Settings > Auto-next book).

| Player               |                                            |
|----------------------|--------------------------------------------|
| A / big button       | play / pause                               |
| Left / Right         | skip back / forward (10/15/30 s and 15/30/60 s, set in Settings) |
| L / R                | previous / next chapter                    |
| X / "Chapters"       | open the chapter + bookmark lists          |
| Y / "Sleep"          | sleep timer: 15 -> 30 -> 45 -> 60 min -> end of chapter -> off (fades out) |
| SELECT / flag button | add a bookmark at the current position     |
| screen button        | turn the screens off (any button or touch wakes them) |
| brightness slider    | drag to set the screen brightness (restored when you quit) |
| "Settings"           | open Settings                              |
| B                    | save and return to library                 |
| Touch seek bar       | scrub within the current chapter           |
| START                | quit                                       |

**Closing the lid:** while a book is playing Narrate asks the system not to go to sleep, so the audio keeps playing with
the lid closed (headphones recommended - the speakers are muffled). When playback pauses or the book ends, the console is
allowed to sleep again, and sleeps immediately if the lid is already closed. Turn this off in Settings > Lid-closed play.

**Volume:** the physical volume slider *is* the volume. Narrate shows its position (bars next to the clock, plus a
"Volume 60%" banner when you move it). The app can't move the hardware slider, so there is no separate in-app volume -
unless the slider can't be read, in which case Up/Down adjust an in-app volume as a fallback.

| Chapter / bookmark lists |                                        |
|--------------------------|----------------------------------------|
| **Back** button (touch) or B / X | return to the player           |
| Tabs (touch) or Left / Right | Chapters <-> Bookmarks             |
| Up / Down, L / R, touch drag | move / page / scroll               |
| A or tap                 | jump to chapter / bookmark; "Add bookmark here" adds one |
| x on a bookmark (or Y)   | delete that bookmark                   |

Bookmarks are stored per book in `sdmc:/3ds/narrate/bookmarks.txt`.

### Styles, layouts and animations

**Style** = colours and shapes. **Layout** = how the player screens are arranged. Every style has a default layout, but
the two are separate settings, so you can mix them (Settings > Style / Layout).

| Style | Look | Default layout |
|-------|------|----------------|
| Narrate, iOS, Android, Retro, Neon, Minimal, Aurora | the original colour/shape styles | Classic |
| **Cassette** | warm tape-deck colours | Cassette (animated) |
| **Vinyl** | record-player colours | Vinyl (animated) |
| **Reel** | steel/mint tape-recorder colours | Reel-to-reel (animated) |
| **Night** | very dim amber for bedtime | Night |
| **Big Buttons** | high contrast, huge touch targets | Big buttons |
| **Zen** | quiet, minimal, outline icons | Zen |
| **Quest Log** | pixel-RPG parchment/purple | Quest log |
| **Shelf** | wood-and-leather colours | Classic, with the **Shelf** library |
| **Synthwave** | neon sunset gradient with a horizon grid | Classic |
| **HOME** | console-menu blue/grey | Classic |
| **Amp** | warm amber hi-fi, two analog VU meters driven by the real audio level | Amp |
| **Book** | cream pages and ribbon, a page turns every few seconds | Open book |
| **Journey** | the sun crosses the sky as the book progresses, parallax hills, a walking traveller | Journey |
| **Campfire** | flickering fire, rising sparks, twinkling stars | Campfire |
| **Night Sky** | twinkling stars, a drifting moon, one constellation star per chapter | Night sky |
| **Aquarium** | swimming fish, bubbles, swaying seaweed | Aquarium |
| **CRT** | green-phosphor terminal with scanlines and a blinking cursor | CRT terminal |

Layouts: **Classic** (cover + details), **Big buttons** (huge play/skip/chapter keys, large time), **Night** (no cover,
big dim clock, large sleep button), **Zen** (just the cover and one play button), **Cassette** (spinning reels, tape
moves from the left spool to the right as you listen, counter display + piano keys), **Vinyl** (spinning record with
the cover as its label, tonearm sweeps inward as the book progresses), **Reel-to-reel** (two big reels and a tape path),
**Quest log** (chapters as quests with ticks, an XP bar for book progress), and seven more animated top screens:
**Amp (VU meters)**, **Open book**, **Journey**, **Campfire**, **Night sky**, **Aquarium** and **CRT terminal**.

Animations only run while playing and redraw just the top screen on an even vblank cadence (60/30/15 fps); they are driven
by wall-clock time, and the bottom screen's clock/progress updates are drawn in the spare refreshes so they never hitch
the animation. Settings > Animations: Off, Auto (adaptive), or a fixed 15 / 30 / 60 fps. `log.txt` records `anim stats`
(draw cost and frame spacing) so the rate can be tuned.
**Library view** (Settings): List or **Shelf** - covers on a shelf, 3 x 2 per screen. Shelf thumbnails are saved in
`sdmc:/3ds/narrate/thumbs/` the first time you open a book, so books you haven't opened yet show a placeholder.

### Settings (live preview on the top screen)

| Setting      | Options |
|--------------|---------|
| Style        | 24 styles (see above) |
| Layout       | Style default, Classic, Big buttons, Night, Zen, Cassette, Vinyl, Reel-to-reel, Quest log, Amp, Open book, Journey, Campfire, Night sky, Aquarium, CRT terminal |
| Library view | Style default, List, Shelf |
| Appearance   | Dark / Light |
| Accent       | Auto (from the cover art), Style default, Blue, Teal, Green, Lime, Amber, Orange, Red, Pink, Purple, Mono |
| Icons        | Style default / Solid / Outline |
| Animations   | Off / **Auto** (picks the smoothest rate the console can sustain: up to 30 fps on the old 2DS/3DS, 60 fps on New 3DS) / 15 fps / 30 fps / 60 fps |
| Skip back / forward | 10, 15, 30 s / 15, 30, 60 s |
| Cover flip   | None / Vertical / Horizontal / Both (applied after the rotation, as you see it on screen) |
| Cover rotate | 0 / 90 / 180 / 270 degrees - step through until the cover looks right |
| Lid-closed play | On / Off |
| Auto-next book  | On / Off |
| Custom theme | None, or any `.pid` / `.pix` file from `sdmc:/3ds/narrate/themes/` (see **Custom themes**) |
| Playback speed | 80, 90, 100, 110, 125, 150 % - pitch is preserved (WSOLA time-stretching); also a button in custom themes |
| Loudness boost | Off / +3 dB / +6 dB, with a soft limiter so loud passages round off instead of clipping |
| Smart rewind | On / Off - after a pause, resumes 3 s (30 s - 5 min pause), 8 s (up to 1 h) or 15 s (longer) earlier |
| Screen off after | Off / 15 s / 30 s / 60 s / 2 min without input while playing (touch or press a button to wake) |
| Auto chapters | Off / 5 / 10 / 15 / 30 min - adds "Part N" chapters to books that have none |

### Custom themes

Drop a `.pid` (single text file) or `.pix` (ZIP with icons, images, a font) into `sdmc:/3ds/narrate/themes/` and pick it in
Settings > Custom theme. A theme can recolour everything, reshape widgets, replace icons with your own PNGs, change the
font, and draw both screens from scratch with images, sprite-sheet animations, scrolling layers, VU meters and waves.
Two examples are included in `themes/`, and **[THEMES.md](THEMES.md)** documents the whole format. `tools/pixpack.py`
packs, unpacks and lints themes on your PC; `tools/make_ember.py` generates the Ember demo artwork. Themes are
sandboxed: bad files only produce warnings, archives are CRC-checked and path-sanitised, and the unpacked files are
deleted again when you switch the theme off.

Tap a row's left/right half (or use Left/Right) to change it; drag to scroll the list. Changes apply instantly and are
saved to `sdmc:/3ds/narrate/settings.cfg`.

## Notes

* Supported: AAC-LC, HE-AAC (SBR/PS) in MP4 containers. Chapters from Nero `chpl` atoms and QuickTime chapter tracks.
  ALAC / MP3-in-m4b are not supported. Files are limited to ~4 GB (FAT32 anyway).
* HE-AAC is decoded at its core sample rate for speed (fine for speech).
* Old 3DS / 2DS: decoding runs on the system core with a 70 % CPU allowance; New 3DS uses the spare core.
* Screens-off mode turns the backlights off. If the app ever crashes while they are off, press the POWER button
  briefly / close and reopen the lid to bring them back.
* Cover art orientation: Settings > **Cover flip** and **Cover rotate** (separate settings). They update live in the preview.
* If audio sounds wrong, remove `-DFIXED_POINT` from `FAAD_DEFS` in the Makefile (slower float decoder).

## Building a CIA (installable app)

A CIA installs Narrate to the HOME menu (needs custom firmware, e.g. Luma3DS, and an installer such as FBI).
The `.3dsx` keeps working alongside it; both share `sdmc:/3ds/narrate/` (settings, progress, bookmarks).

One-time setup - two small tools must be on your PATH (the simplest place is `$DEVKITARM/bin`):

* **makerom** - https://github.com/3DSGuy/Project_CTR/releases (get the `makerom-...-windows_x86_64` zip, copy `makerom.exe`)
* **bannertool** - https://github.com/Steveice10/bannertool/releases (version 1.1.0; copy the `bannertool.exe` for your OS)

Then, in the devkitPro shell in this folder:

    make cia

This produces `narrate.cia` (it builds the ELF first if needed). Copy it to the SD card and install it with FBI
(Remote Install / SD > select the file > Install). It still needs `sdmc:/3ds/dspfirm.cdc` (DSP1) like the 3dsx.

Notes: `cia/narrate.rsf` is the title configuration (title ID `0xF8A42`; change it if it ever clashes with
another homebrew). Narrate asks for the volume slider (`mcu::HWC`) and backlight (`gsp::Lcd`) services; if your
firmware refuses either, the app falls back to its in-app volume / no screens-off button instead of failing.

## Performance notes (old 2DS / 3DS)

* Audio decoding runs on its own thread (system core on old models, spare core on New 3DS) with ~1.1 s of audio queued,
  a short first buffer so sound starts quickly after play/seek, and a lock that the decoder releases within one AAC frame
  so Pause/Seek never wait on a long decode.
* Animated layouts are paced on the screen refresh (every 1st/2nd/4th vblank), so frames are evenly spaced; parsed text is
  cached between frames; and everything that writes to the SD card (progress, bookmarks, settings, periodic log lines) is
  done by a low-priority background thread, so saving never causes a hitch in the animation.
* The UI only redraws when something visible changes (position tick, input, toast ...), and text measuring is cached,
  so an idle player uses a fraction of the CPU/GPU it would at a constant 60 fps.
* `log.txt` records the CPU time limit the OS granted the audio thread and, every 30 s of playback,
  `audio stats: decode load N% (peak M%), underruns U`. A load near or above 100% means the console cannot decode that
  file in real time (typical for HE-AAC on old models); underruns > 0 means the audio actually stuttered.

## Troubleshooting

Narrate writes a step-by-step log to `sdmc:/3ds/narrate/log.txt` (overwritten each launch). If it crashes, the
last line shows how far it got — send that file along with the crash screen.

## Layout

    source/m4b.c      MP4 demuxer (tables, chapters, tags, cover)           — unit-tested on a PC
    source/player.c   FAAD2 decode thread -> NDSP streaming, seeking, position tracking
    source/ui.c       theme-aware citro2d widgets (cards, rows, tabs, bars, icons, text fitting)
    source/theme.c    24 UI styles, palettes, accent colours, layouts, saved settings
    source/decks.c    animated cassette / reel / vinyl top screens + quest log
    source/decks2.c   animated Amp, Open book, Journey, Campfire, Night sky, Aquarium and CRT top screens
    source/wsola.c    pitch-preserving time stretch for playback speed (unit-tested on a PC)
    source/custom.c   custom themes: scan / apply, drawing engine for theme-defined screens
    source/themefile.c  theme.pid parser (fuzz-tested)      source/zipx.c  ZIP reader for .pix (CRC-checked, sanitised)
    source/gimage.c   PNG -> GPU texture for theme images and sprite sheets
    source/thumbs.c   shelf cover thumbnails (SD-card cache)
    source/browser.c  folder listing, natural sort, finished marks, folder stats, series lookup
    source/cover.c    JPEG/PNG cover -> GPU texture + accent colour
    source/main.c     screens and input
    source/log.c      crash-proof breadcrumb log
    source/bookmarks.c per-book bookmarks on the SD card
    tools/            test_m4b.c + gen_test_m4b.py (host-side demuxer test), make_icon.py, pixpack.py, make_ember.py
    themes/           sea.pid and ember.pix example themes
