#!/usr/bin/env python3
"""
pixpack.py - pack, unpack and lint Narrate theme archives (.pix).

  pixpack.py pack   folder/ [-o out.pix]   zip a theme folder (must contain theme.pid at its top level)
  pixpack.py unpack file.pix [-o folder]   extract an archive
  pixpack.py lint   folder/ | file.pix | file.pid   check a theme for mistakes the console would only warn about
"""
import argparse, os, re, struct, sys, zipfile

MAX_FILES, MAX_FILE, MAX_TOTAL = 400, 12 * 1024 * 1024, 40 * 1024 * 1024
ICON_NAMES = {"play", "pause", "prev", "next", "back", "fwd", "chapters", "sleep", "settings", "bookmark",
              "screen_off", "speed", "folder", "book", "check", "sun", "sort", "close"}
SECTIONS = {"theme", "colors", "colors.dark", "colors.light", "shape", "font", "images", "icons", "top", "bottom"}
COMMANDS = {"rect", "circle", "line", "image", "cover", "text", "bar", "button", "seek", "bright", "vu", "wave", "status"}
COLOR_KEYS = {"bg", "bg2", "surface", "surface2", "text", "dim", "on_accent", "line", "accent"}


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", head[16:24])


def strip(line):
    out, q = [], False
    for ch in line:
        if ch == '"':
            q = not q
        if ch == ";" and not q:
            break
        out.append(ch)
    return "".join(out).strip()


def lint_text(text, files, base_dir=None):
    """files: set of relative paths available in the archive/folder (None = single .pid, no assets allowed)."""
    problems, notes = [], []
    section, images = None, {}
    seen_theme = False
    for no, raw in enumerate(text.splitlines(), 1):
        line = strip(raw)
        if not line:
            continue
        m = re.match(r"^\[([^\]]+)\]$", line)
        if m:
            section = m.group(1).strip().lower()
            seen_theme |= section == "theme"
            if section not in SECTIONS:
                problems.append(f"line {no}: unknown section [{section}]")
            continue
        if section in ("top", "bottom"):
            cmd = line.split()[0].lower()
            if cmd not in COMMANDS:
                problems.append(f"line {no}: unknown drawing command '{cmd}'")
            if cmd == "image":
                parts = line.split()
                if len(parts) < 2 or parts[1] not in images:
                    problems.append(f"line {no}: image '{parts[1] if len(parts) > 1 else ''}' is not declared in [images]")
            continue
        if "=" not in line:
            problems.append(f"line {no}: expected key = value")
            continue
        k, v = [p.strip() for p in line.split("=", 1)]
        if section in ("colors", "colors.dark", "colors.light"):
            if k.lower() not in COLOR_KEYS:
                problems.append(f"line {no}: unknown colour '{k}'")
            elif not re.match(r"^#?[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$", v):
                problems.append(f"line {no}: '{v}' is not a #RRGGBB colour")
        elif section == "images":
            path = v.split()[0] if v else ""
            images[k] = path
            check_file(path, files, problems, no, base_dir, notes)
        elif section == "icons":
            if k.lower() not in ICON_NAMES:
                problems.append(f"line {no}: unknown icon '{k}'")
            check_file(v.split()[0] if v else "", files, problems, no, base_dir, notes)
        elif section == "font":
            check_file(v, files, problems, no, base_dir, notes)
    if not seen_theme:
        problems.append("no [theme] section")
    return problems, notes


def check_file(path, files, problems, no, base_dir, notes):
    if not path:
        return
    if path.startswith("/") or ".." in path or ":" in path:
        problems.append(f"line {no}: unsafe path '{path}'")
    elif files is None:
        problems.append(f"line {no}: a single .pid cannot reference files ('{path}') - use a .pix archive")
    elif path not in files:
        problems.append(f"line {no}: '{path}' is not in the theme")
    elif base_dir and path.lower().endswith(".png"):
        sz = png_size(os.path.join(base_dir, path))
        if not sz:
            problems.append(f"line {no}: '{path}' is not a valid PNG")
        elif max(sz) > 512:
            notes.append(f"{path}: {sz[0]}x{sz[1]} will be shrunk to fit 512 px on the console")


def collect(folder):
    out = {}
    for root, _, names in os.walk(folder):
        for n in names:
            full = os.path.join(root, n)
            out[os.path.relpath(full, folder).replace(os.sep, "/")] = full
    return out


def cmd_pack(a):
    folder = a.target.rstrip("/\\")
    files = collect(folder)
    if "theme.pid" not in files:
        sys.exit("error: theme.pid must be at the top level of the folder")
    if len(files) > MAX_FILES:
        sys.exit(f"error: {len(files)} files (limit {MAX_FILES})")
    total = 0
    for rel, full in files.items():
        sz = os.path.getsize(full)
        total += sz
        if sz > MAX_FILE:
            sys.exit(f"error: {rel} is {sz} bytes (limit {MAX_FILE})")
    if total > MAX_TOTAL:
        sys.exit(f"error: {total} bytes unpacked (limit {MAX_TOTAL})")
    out = a.output or folder + ".pix"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.write(files["theme.pid"], "theme.pid")
        for rel in sorted(files):
            if rel != "theme.pid":
                z.write(files[rel], rel)
    print(f"packed {len(files)} files, {total} bytes -> {out} ({os.path.getsize(out)} bytes)")


def cmd_unpack(a):
    out = a.output or os.path.splitext(a.target)[0]
    with zipfile.ZipFile(a.target) as z:
        for n in z.namelist():
            if n.startswith("/") or ".." in n:
                sys.exit(f"error: unsafe path {n}")
        z.extractall(out)
    print("unpacked to", out)


def cmd_lint(a):
    t = a.target
    base = None
    if os.path.isdir(t):
        files = set(collect(t))
        base = t
        if "theme.pid" not in files:
            sys.exit("error: no theme.pid")
        text = open(os.path.join(t, "theme.pid"), encoding="utf-8", errors="replace").read()
    elif t.lower().endswith(".pix"):
        with zipfile.ZipFile(t) as z:
            files = set(z.namelist())
            if "theme.pid" not in files:
                sys.exit("error: no theme.pid in the archive")
            text = z.read("theme.pid").decode("utf-8", "replace")
    else:
        files = None
        text = open(t, encoding="utf-8", errors="replace").read()
    problems, notes = lint_text(text, files, base)
    for n in notes:
        print("note:", n)
    for p in problems:
        print("problem:", p)
    print("OK - no problems" if not problems else f"{len(problems)} problem(s)")
    return 1 if problems else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    for name, fn in (("pack", cmd_pack), ("unpack", cmd_unpack), ("lint", cmd_lint)):
        p = sp.add_parser(name)
        p.add_argument("target")
        if name != "lint":
            p.add_argument("-o", "--output")
        p.set_defaults(fn=fn)
    a = ap.parse_args()
    sys.exit(a.fn(a) or 0)


if __name__ == "__main__":
    main()
