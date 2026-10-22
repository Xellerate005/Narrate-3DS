# Narrate — M4B audiobook player for the Nintendo 3DS

Plays `.m4b` / `.m4a` (AAC) audiobooks with a dark, modern UI: cover art, accent colour taken from the
cover, per-chapter seek bar, chapter list, sleep timer, and automatic resume.


# Fbi install

![Fbi Install](Narrate_V2.png)
       

## Build

You need to have devkitPro. You additionally need three portlibs (cover art decoding):

    # devkitPro MSYS2 shell on Windows (use "dkp-pacman" on Linux/macOS)
    pacman -S --needed 3ds-libjpeg-turbo 3ds-libpng 3ds-zlib

Then, from this folder in the devkitPro shell:

    ./setup_deps.sh      # clones the FAAD2 AAC decoder into deps/faad2 (needs git or curl)
    make

If you don't have git you can download https://github.com/knik0/faad2 as a zip and unpack it so that
`deps/faad2/libfaad/decoder.c` exists, then run `make`.

Output: `narrate.3dsx` (+ `narrate.smdh`). Copy `narrate.3dsx` to `sdmc:/3ds/` on the SD card.

(If you want it on the HOME menu instead? See **Building a CIA** below.)

## Use

* Put books in **`sdmc:/AudioBook/`** (the folder is created on first launch). Narrate only reads this folder.
  Sub-folders work as groups (series, authors, ...) and can be nested; you can't browse above `AudioBook`.
* Needs `dspfirm.cdc` on the SD card (run **DSP1** once from the Homebrew Launcher) — same requirement as any 3DS audio app.
* Resume positions are stored in `sdmc:/3ds/narrate/progress.txt`.

### Controls

| Library              |                                            |
|----------------------|--------------------------------------------|
| D-pad / touch scroll | move / scroll                              |
| A or tap             | open folder / book (first row = **Continue listening**) |
| B or Left            | up one folder                              |
| L / R                | page up / down                             |
| SELECT / gear button | **Settings**                               |
| START                | quit                                       |

| Player               |                                            |
|----------------------|--------------------------------------------|
| A / big button       | play / pause                               |
| Left / Right         | skip back / forward (10/15/30 s and 15/30/60 s, set in Settings) |
| L / R                | previous / next chapter                    |
| X / "Chapters"       | open the chapter + bookmark lists          |
| Y / "Sleep"          | sleep timer: 15 → 30 → 45 → 60 min → end of chapter → off (fades out) |
| SELECT / flag button | add a bookmark at the current position     |
| screen button (top left of the touch screen) | turn the screens off (any button or touch wakes them) |
| "Settings" pill      | open Settings                              |
| B                    | save and return to library                 |
| Touch seek bar       | scrub within the current chapter           |
| START                | quit                                       |

**Volume:** the physical volume slider *is* the volume. Narrate shows its position (bars next to the clock, plus a
"Volume 60%" banner when you move it). The app can't move the hardware slider, so there is no separate in-app volume —
unless the slider can't be read, in which case Up/Down adjust an in-app volume as a fallback.

| Chapter / bookmark lists |                                        |
|--------------------------|----------------------------------------|
| **Back** button (touch) or B / X | return to the player           |
| Tabs (touch) or Left / Right | Chapters ↔ Bookmarks               |
| Up / Down, L / R, touch drag | move / page / scroll               |
| A or tap                 | jump to chapter / bookmark; "Add bookmark here" adds one |
| ✕ on a bookmark (or Y)   | delete that bookmark                   |

Bookmarks are stored per book in `sdmc:/3ds/narrate/bookmarks.txt`.

### Settings (live preview on the top screen)

| Setting      | Options |
|--------------|---------|
| Style        | **Narrate** (rounded dark cards), **iOS** (grouped lists, bare play glyph, segmented tabs), **Android** (Material-style: pill rows, squircle play button, thick bar, underlined tabs), **Retro** (terminal green / Game Boy, square outlines, segmented bar), **Neon** (glowing outlines), **Minimal** (flat, hairlines, monochrome), **Aurora** (accent-tinted gradient, frosted cards) |
| Appearance   | Dark / Light |
| Accent       | Auto (from the cover art), Style default, Blue, Teal, Green, Lime, Amber, Orange, Red, Pink, Purple, Mono |
| Icons        | Style default / Solid / Outline |
| Skip back / forward | 10, 15, 30 s / 15, 30, 60 s |
| Cover flip   | None / Vertical / Horizontal / Both (applied after the rotation, as you see it on screen) |
| Cover rotate | 0 / 90 / 180 / 270 degrees - step through until the cover looks right; both apply live in the preview |

Tap a row's left/right half (or use Left/Right) to change it; changes apply instantly and are saved to
`sdmc:/3ds/narrate/settings.cfg`.

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
* **bannertool** - https://github.com/Epicpkmn11/bannertool/releases (copy the `bannertool.exe` for your OS)

Then, in the devkitPro shell in this folder:

    make cia

This produces `narrate.cia` (it builds the ELF first if needed). Copy it to the SD card and install it with FBI
(Remote Install / SD > select the file > Install). It still needs `sdmc:/3ds/dspfirm.cdc` (DSP1) like the 3dsx.

Notes: `cia/narrate.rsf` is the title configuration (title ID `0xF8A42`; change it if it ever clashes with
another homebrew). Narrate asks for the volume slider (`mcu::HWC`) and backlight (`gsp::Lcd`) services; if your
firmware refuses either, the app falls back to its in-app volume / no screens-off button instead of failing.

## Performance notes (old 2DS / 3DS)

* Audio decoding runs on its own thread (system core on old models, spare core on New 3DS, The new 3ds is not tested so i need some feedback) with ~1.1 s of audio queued,
  a short first buffer so sound starts quickly after play/seek, and a lock that the decoder releases within one AAC frame
  so Pause/Seek never wait on a long decode.
* The UI only redraws when something visible changes (position tick, input, toast ...), and text measuring is cached,
  so an idle player uses a fraction of the CPU/GPU it would at a constant 60 fps.
* `log.txt` records the CPU time limit the OS granted the audio thread and, every 30 s of playback,
  `audio stats: decode load N% (peak M%), underruns U`. A load near or above 100% means the console cannot decode that
  file in real time (typical for HE-AAC on old models); underruns > 0 means the audio actually stuttered.

## Troubleshooting

Narrate writes a step-by-step log to `sdmc:/3ds/narrate/log.txt` (overwritten each launch). If it crashes, the
last line shows how far it got — you can send that file along with the crash screen, so that I may debug it, when a new issue is reported

## Features/Requests

use the github issue to also report ask for any other/new features you think i should add
