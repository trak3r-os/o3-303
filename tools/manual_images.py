#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The player's manual's pictures (docs/MANUAL.md), made from the simulator's scenarios.

    python3 -I tools/manual_images.py --sim build-host/app/o3_303_sim    # runs the scenarios, writes docs/manual/img/*.png
    python3 -I tools/manual_images.py --check                            # the ctest manual_images: the manual and the table below agree

IMAGES is the one table: the picture's name in docs/manual/img, the scenario of app/scenarios that takes it, the snap's name in that scenario, and the crop
("screen" keeps the display, the top 800 x 480; "full" keeps the pad grid the simulator draws under it). Each scenario runs once, with an empty card of its
own, into a temporary directory. The pictures are written again without loss and small: a palette when the picture has 256 colours or fewer, else RGB with
the PNG filter that packs best. Nothing is compared byte for byte: --check only asks that every picture the manual shows is here and named in the table,
and that every row names a scenario that exists and a snap that it really takes.

The screens that are not skinned yet (MASTER, FILE, LIBRARY, RECORD, HOOKS: SK6 and SK7 in docs/SKIN-PLAN.md) are taken by manual_screens.txt with SKIN on
OBSIDIAN, so they draw their classic layout today and their obsidian one when those slices land: running this script again is the whole update.
Python 3 standard library only.
"""

import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENARIOS = os.path.join(ROOT, "app", "scenarios")
MANUAL = os.path.join(ROOT, "docs", "MANUAL.md")
IMG = os.path.join(ROOT, "docs", "manual", "img")

SCREEN_W = 800
SCREEN_H = 480
MAX_TOTAL = 3 * 1024 * 1024  # the pictures together stay under 3 MB

# (picture, scenario, snap, crop)
IMAGES = [
    # 1 What o3-303 is
    ("hero-home-playing", "skin_home", "02_playing", "full"),
    ("boot-logo", "boot_logo", "01_boot_logo", "screen"),
    # 3 The panel at a glance
    ("help-home-buttons", "help_skin", "00_home_buttons", "screen"),
    # 4 The first five minutes
    ("tour-play-turn", "manual_tour", "01_play_and_turn", "full"),
    ("tour-empty-pattern", "manual_tour", "02_empty_b01", "screen"),
    ("tour-rec-writes", "manual_tour", "03_rec_writes", "full"),
    ("tour-accent-slide", "manual_tour", "04_accent_slide", "screen"),
    ("tour-saved", "manual_tour", "06_saved", "screen"),
    # 5 The screens
    ("home", "skin_home", "01_stopped", "full"),
    ("home-shift-row", "skin_home", "08_shift_row", "screen"),
    ("home-plays-row", "manual_tour", "08_plays_row", "screen"),
    ("home-pattern-held", "skin_home", "09_pattern_held", "full"),
    ("home-filt", "manual_tour", "07_filt_ladders", "full"),
    ("setup", "skin_home", "06_setup", "screen"),
    ("perform", "skin_perform", "01_idle", "full"),
    ("perform-latched", "skin_perform", "03_actions_latched", "screen"),
    ("perform-morph", "skin_perform", "04_snapshot_morph", "screen"),
    ("perform-wobble", "skin_perform", "06_wobble_rate", "screen"),
    ("perform-setup", "skin_perform", "08_setup", "screen"),
    ("instrument-voice", "skin_instrument", "01_voice", "screen"),
    ("instrument-fx", "skin_instrument", "02_fx", "screen"),
    ("instrument-mods", "skin_instrument", "03_mods", "screen"),
    ("instrument-learn", "skin_instrument", "07_shift_learn_row", "screen"),
    ("master", "manual_screens", "03_master", "screen"),
    ("song", "skin_song", "03_playing", "screen"),
    ("song-empty", "skin_song", "01_empty", "screen"),
    ("file-projects", "manual_screens", "11_file_projects", "screen"),
    ("file-name", "manual_screens", "09_file_name", "screen"),
    ("library", "manual_screens", "12_library", "screen"),
    ("record", "manual_screens", "04_record", "screen"),
    ("record-done", "manual_screens", "07_record_done", "screen"),
    ("hooks", "manual_screens", "08_hooks", "screen"),
    # 6 The pads
    ("help-home-pads", "help_skin", "01_home_pads", "screen"),
    ("help-perform-pads", "skin_perform", "help_01_perform_pads", "screen"),
    ("help-song-pads", "manual_screens", "14_help_song_pads", "screen"),
    ("help-name-pads", "manual_screens", "10_help_name_pads", "screen"),
    ("help-library-pads", "manual_screens", "13_help_library_pads", "screen"),
    # 9 Settings
    ("config-303", "manual_screens", "01_config_303", "screen"),
    ("config-deep", "manual_screens", "02_config_deep", "screen"),
    # 13 Two looks
    ("look-classic-home", "home", "01_plain", "screen"),
    ("look-obsidian-home", "skin_home", "01_stopped", "screen"),
]

CROPS = ("screen", "full")


# ---- The scenarios --------------------------------------------------------------------------------------------------------------------------


def scenario_path(scenario):
    return os.path.join(SCENARIOS, scenario + ".txt")


def snaps_of(path):
    """The names after `snap` (and `boot`, the boot screen's picture) on the lines of a scenario, as tests/o3_303_scenarios_test.cpp reads them."""
    names = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            words = line.split("#", 1)[0].split()
            if len(words) >= 2 and words[0] in ("snap", "boot"):
                names.append(words[1])
    return names


# ---- PNG in and out -------------------------------------------------------------------------------------------------------------------------


def read_png(path):
    """(width, height, rows of RGB bytes) of an 8-bit RGB or RGBA PNG, as the simulator writes them (any filter, no interlace)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(path + ": not a PNG")
    pos = 8
    idat = []
    width = height = depth = colour = interlace = None
    while pos < len(data):
        (n,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if depth != 8 or colour not in (2, 6) or interlace != 0:
        raise ValueError("%s: an 8-bit RGB or RGBA PNG without interlace is expected (depth %s, colour type %s)" % (path, depth, colour))
    raw = zlib.decompress(b"".join(idat))
    bpp = 3 if colour == 2 else 4
    stride = width * bpp
    rows = []
    prev = bytearray(stride)
    i = 0
    for _ in range(height):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        if f == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                left = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((left + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa = abs(p - a)
                pb = abs(p - b)
                pc = abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        elif f != 0:
            raise ValueError("%s: unknown PNG filter %d" % (path, f))
        rows.append(line)
        prev = line
    if bpp == 4:  # the simulator's alpha is always opaque: keep the colour
        rgb = []
        for line in rows:
            out = bytearray(width * 3)
            out[0::3] = line[0::4]
            out[1::3] = line[1::4]
            out[2::3] = line[2::4]
            rgb.append(out)
        rows = rgb
    return width, height, [bytes(r) for r in rows]


def _chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def _filtered(rows, bpp, f):
    out = bytearray()
    prev = bytes(len(rows[0]))
    for r in rows:
        if f == 0:
            out += b"\x00" + r
        elif f == 1:
            out += b"\x01" + r[:bpp] + bytes((r[i] - r[i - bpp]) & 255 for i in range(bpp, len(r)))
        else:
            out += b"\x02" + bytes((a - b) & 255 for a, b in zip(r, prev))
        prev = r
    return zlib.compress(bytes(out), 9)


def write_png(path, width, rows):
    """Writes the RGB rows without loss: an 8-bit palette when there are 256 colours or fewer, else RGB with the whole-image filter (none, sub, up) that packs best."""
    colours = set()
    for r in rows:
        for x in range(0, len(r), 3):
            colours.add(r[x:x + 3])
            if len(colours) > 256:
                break
        if len(colours) > 256:
            break
    if len(colours) <= 256:
        palette = sorted(colours)
        index = {c: i for i, c in enumerate(palette)}
        idx_rows = [bytes(index[r[x:x + 3]] for x in range(0, len(r), 3)) for r in rows]
        packed = min((_filtered(idx_rows, 1, f) for f in (0, 2)), key=len)
        header = struct.pack(">IIBBBBB", width, len(rows), 8, 3, 0, 0, 0)
        body = _chunk(b"IHDR", header) + _chunk(b"PLTE", b"".join(palette)) + _chunk(b"IDAT", packed)
    else:
        packed = min((_filtered(rows, 3, f) for f in (0, 1, 2)), key=len)
        header = struct.pack(">IIBBBBB", width, len(rows), 8, 2, 0, 0, 0)
        body = _chunk(b"IHDR", header) + _chunk(b"IDAT", packed)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + body + _chunk(b"IEND", b""))


# ---- The two jobs ---------------------------------------------------------------------------------------------------------------------------


def manual_pictures():
    """The pictures docs/MANUAL.md shows (manual/img/<name>.png, in a Markdown image or an <img> tag)."""
    with open(MANUAL, encoding="utf-8") as f:
        text = f.read()
    return re.findall(r"manual/img/([A-Za-z0-9_.-]+)\.png", text)


def check():
    problems = []
    names = [row[0] for row in IMAGES]
    for name in sorted(set(n for n in names if names.count(n) > 1)):
        problems.append("IMAGES names %s more than once" % name)
    snaps = {}
    for name, scenario, snap, crop in IMAGES:
        path = scenario_path(scenario)
        if not os.path.isfile(path):
            problems.append("%s: the scenario app/scenarios/%s.txt does not exist" % (name, scenario))
            continue
        if scenario not in snaps:
            snaps[scenario] = snaps_of(path)
        if snap not in snaps[scenario]:
            problems.append("%s: app/scenarios/%s.txt takes no snap %s" % (name, scenario, snap))
        if crop not in CROPS:
            problems.append("%s: the crop %r is not one of %s" % (name, crop, ", ".join(CROPS)))
    if not os.path.isfile(MANUAL):
        problems.append("docs/MANUAL.md does not exist")
        shown = []
    else:
        shown = manual_pictures()
    for name in sorted(set(shown)):
        if not os.path.isfile(os.path.join(IMG, name + ".png")):
            problems.append("docs/MANUAL.md shows manual/img/%s.png, and it is not there" % name)
        if name not in names:
            problems.append("docs/MANUAL.md shows manual/img/%s.png, and IMAGES has no row for it (it could not be made again)" % name)
    for name in names:
        if name not in shown:
            problems.append("IMAGES makes %s, and docs/MANUAL.md does not show it" % name)
    total = sum(os.path.getsize(os.path.join(IMG, n + ".png")) for n in set(names) if os.path.isfile(os.path.join(IMG, n + ".png")))
    if total > MAX_TOTAL:
        problems.append("the pictures take %d bytes, over the %d the manual allows" % (total, MAX_TOTAL))
    for p in problems:
        print("manual_images: " + p, file=sys.stderr)
    print("manual_images: %d pictures in the table, %d shown by docs/MANUAL.md, %d bytes in docs/manual/img" % (len(IMAGES), len(set(shown)), total))
    return 0 if not problems else 1


def make(sim):
    if not os.access(sim, os.X_OK):
        print("manual_images: %s is not an executable simulator" % sim, file=sys.stderr)
        return 1
    os.makedirs(IMG, exist_ok=True)
    order = []
    for row in IMAGES:
        if row[1] not in order:
            order.append(row[1])
    total = 0
    with tempfile.TemporaryDirectory(prefix="o3_303_manual_") as tmp:
        for scenario in order:
            out = os.path.join(tmp, scenario, "out")
            card = os.path.join(tmp, scenario, "card")
            os.makedirs(out)
            os.makedirs(card)
            run = subprocess.run([sim, "--scenario", scenario_path(scenario), "--card", card, "--out", out],
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600, check=False)
            if run.returncode != 0:
                sys.stderr.write(run.stdout.decode("utf-8", "replace"))
                print("manual_images: %s failed (exit %d)" % (scenario, run.returncode), file=sys.stderr)
                return 1
            for name, row_scenario, snap, crop in IMAGES:
                if row_scenario != scenario:
                    continue
                width, height, rows = read_png(os.path.join(out, snap + ".png"))
                if crop == "screen":
                    if width != SCREEN_W or height < SCREEN_H:
                        print("manual_images: %s/%s is %d x %d, smaller than the screen" % (scenario, snap, width, height), file=sys.stderr)
                        return 1
                    rows = rows[:SCREEN_H]
                dest = os.path.join(IMG, name + ".png")
                write_png(dest, width, rows)
                size = os.path.getsize(dest)
                total += size
                print("%-22s %-16s %-24s %-6s %7d bytes" % (name, scenario, snap, crop, size))
    print("manual_images: %d pictures, %d bytes" % (len(IMAGES), total))
    return 0


def main():
    parser = argparse.ArgumentParser(description="The player's manual's pictures, from the simulator's scenarios.")
    job = parser.add_mutually_exclusive_group(required=True)
    job.add_argument("--sim", help="the simulator (build-host/app/o3_303_sim): run the scenarios and write docs/manual/img")
    job.add_argument("--check", action="store_true", help="check that docs/MANUAL.md and the table agree (the ctest)")
    args = parser.parse_args()
    return check() if args.check else make(args.sim)


if __name__ == "__main__":
    sys.exit(main())
