#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The obsidian skin's bitmaps: procedural art turned into FT81x bitmaps that live in flash (Python 3 standard library only).

docs/SKIN-PLAN.md section 3 (the nine assets) and section 6 (this pipeline). Every shape is our own drawing: nothing is traced from or
derived from any official firmware, and no font is baked into a bitmap (the wordmark is drawn here as skewed strokes). The art is the port
of the skin spike's generators (texture, knob parts, step key, 7-segment) plus the wordmark, the LED, the pad lamp and the action key.

Every byte is computed in integers, so the output is the same on every machine and every Python from 3.8 on:

  * positions are Q8 (1/256 px), light and coverage Q12 (4096 = 1.0), trigonometry comes from the sine table below (Q15, one degree a
    step, linearly interpolated), the exponential of the Gaussian from the 2^-x table, square roots are math.isqrt;
  * the noise is one fixed xorshift32 with seed 303, drawn in the order of ASSETS (nothing here iterates a set or hashes anything);
  * the ordered dither is a Bayer 4x4 matrix in integers.

The pipeline: the generators make cells of Q12 light, an Asset quantises them to the chip's format (L4: high nibble first; L8; rows padded to
a byte, each asset padded to a word), a layout puts the assets one after the other in the app's RAM_G region, and the writer emits
o3_303/skin_assets.h (the Id enum, the table of format, cells, stride, drawn size and offset, a BitmapDesc for each, the AppAsset table,
all of it static_asserted) and skin_assets.cpp (one alignas(4) const array each). tools/skin_assets.sha256 holds the hash of every asset
and of both files; nothing generated is in git.

Usage:
  gen_skin.py --out DIR [--check tools/skin_assets.sha256]   writes DIR/o3_303/skin_assets.h and DIR/skin_assets.cpp (files rewritten only
                                                             when they differ); --check exits 1 when the output is not the committed hash
  gen_skin.py --update tools/skin_assets.sha256              writes the hash file (a visible diff in review)
  gen_skin.py --fresh BUILD_DIR --check tools/skin_assets.sha256
                                                             generates twice into temporary directories and compares the runs with each
                                                             other, with BUILD_DIR's copy (what the build linked) and with the hash file
  gen_skin.py --report                                       each asset's size
  gen_skin.py --preview DIR                                  contact_sheet.png (every cell as a grey mask) and assembled.png (tinted
                                                             parts over the panel, to judge the look)
"""
import argparse
import hashlib
import math
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ONE = 4096                        # Q12: 1.0 of coverage or light
RAM_G_APP_BYTES = 0x2C000 - 64    # ui/plot/ram_g_map.h: kRamGAppBytes (1 MiB - 64 B - kRamGApp, kRamGApp = 1 MiB - 0x2C000)
MAX_TOTAL_BYTES = 110000          # docs/SKIN-PLAN.md section 8, SK1's acceptance

# ---- Tables (computed once with a float and pasted; nothing at run time touches a float) ---------------------------------

# sin(a) * 32768 for a = 0..90 degrees.
SIN_Q15 = (
    0, 572, 1144, 1715, 2286, 2856, 3425, 3993, 4560, 5126, 5690, 6252, 6813,
    7371, 7927, 8481, 9032, 9580, 10126, 10668, 11207, 11743, 12275, 12803, 13328, 13848,
    14365, 14876, 15384, 15886, 16384, 16877, 17364, 17847, 18324, 18795, 19261, 19720, 20174,
    20622, 21063, 21498, 21926, 22348, 22763, 23170, 23571, 23965, 24351, 24730, 25102, 25466,
    25822, 26170, 26510, 26842, 27166, 27482, 27789, 28088, 28378, 28660, 28932, 29197, 29452,
    29698, 29935, 30163, 30382, 30592, 30792, 30983, 31164, 31336, 31499, 31651, 31795, 31928,
    32052, 32166, 32270, 32365, 32449, 32524, 32588, 32643, 32688, 32723, 32748, 32763, 32768,
)
# 2^(-i/32) * 32768 for i = 0..32.
EXP2_NEG_Q15 = (
    32768, 32066, 31379, 30706, 30048, 29405, 28774, 28158, 27554, 26964, 26386, 25821, 25268, 24726, 24196, 23678, 23170,
    22674, 22188, 21713, 21247, 20792, 20347, 19911, 19484, 19066, 18658, 18258, 17867, 17484, 17109, 16743, 16384,
)
BAYER4 = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
BAYER_Q12 = tuple(tuple((v * 2 + 1) * 128 for v in row) for row in BAYER4)   # (v + 0.5) / 16 in Q12

isqrt = math.isqrt


def sin_deg(a):
    """sin of a whole number of degrees, Q15."""
    a %= 360
    if a <= 90:
        return SIN_Q15[a]
    if a <= 180:
        return SIN_Q15[180 - a]
    if a <= 270:
        return -SIN_Q15[a - 180]
    return -SIN_Q15[360 - a]


def cos_deg(a):
    return sin_deg(a + 90)


def sin_turns(t16):
    """sin of t16 / 65536 turns, Q15, interpolated between whole degrees."""
    ad = (t16 & 0xFFFF) * 360
    d, fr = ad >> 16, ad & 0xFFFF
    s0 = sin_deg(d)
    return s0 + (((sin_deg(d + 1) - s0) * fr) >> 16)


def clip12(v):
    return 0 if v < 0 else (ONE if v > ONE else v)


def cov(d_q8):
    """Coverage (Q12) of a pixel whose centre is d_q8 (Q8) inside an edge: a one pixel ramp."""
    if d_q8 <= -128:
        return 0
    if d_q8 >= 128:
        return ONE
    return (d_q8 + 128) << 4


class XorShift:
    """xorshift32, seed 303: the one random stream of the generator."""

    def __init__(self, seed=303):
        self.s = seed

    def next(self):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x
        return x

    def below(self, n):
        return self.next() % n

    def normal_field(self, h, w):
        """Roughly normal noise: the sum of four 16-bit uniforms, centred (std about 37,800)."""
        nxt = self.next
        return [[(nxt() >> 16) + (nxt() >> 16) + (nxt() >> 16) + (nxt() >> 16) - 131072 for _ in range(w)] for _ in range(h)]


# ---- Images: lists of rows of Q12 ints ------------------------------------------------------------------------------------

def new_image(h, w, v=0):
    return [[v] * w for _ in range(h)]


def gauss_kernel(sig_q8):
    """A Gaussian of sigma sig_q8 / 256 px, radius 3 sigma, weights summing to 65536 (or so)."""
    if sig_q8 <= 0:
        return [65536]
    r = (3 * sig_q8 + 255) // 256
    ws = []
    for i in range(-r, r + 1):
        u = ((i * i) << 23) // (sig_q8 * sig_q8)           # i^2 / (2 sigma^2), Q8
        v = (u * 5909) >> 12                               # times log2(e): e^-u = 2^-v
        n, f = v >> 8, v & 255
        if n >= 17:
            ws.append(0)
            continue
        e0, e1 = EXP2_NEG_Q15[f >> 3], EXP2_NEG_Q15[(f >> 3) + 1]
        ws.append((e0 + (((e1 - e0) * (f & 7)) >> 3)) >> n)
    tot = sum(ws)
    return [w * 65536 // tot for w in ws]


def conv_row(row, k, wrap):
    r = len(k) // 2
    if r == 0:
        return list(row)
    p = row[-r:] + row + row[:r] if wrap else [0] * r + row + [0] * r
    n = len(k)
    out = []
    for i in range(len(row)):
        acc = 0
        for j in range(n):
            acc += k[j] * p[i + j]
        out.append((acc + 32768) >> 16)
    return out


def blur(img, sx_q8, sy_q8=None, wrap_x=False):
    """Separable Gaussian blur; wrap_x keeps a tile seamless horizontally, the edges are otherwise zero."""
    sy_q8 = sx_q8 if sy_q8 is None else sy_q8
    kx = gauss_kernel(sx_q8)
    out = [conv_row(row, kx, wrap_x) for row in img]
    ky = gauss_kernel(sy_q8)
    cols = [conv_row(list(c), ky, False) for c in zip(*out)]
    return [list(r) for r in zip(*cols)]


def normalise(img):
    """Zero mean, unit standard deviation (Q12)."""
    n = len(img) * len(img[0])
    m = sum(map(sum, img)) // n
    var = sum((v - m) * (v - m) for row in img for v in row) // n
    sd = max(isqrt(var), 1)
    return [[((v - m) * ONE) // sd for v in row] for row in img]


def box_down(img, ss):
    h, w = len(img) // ss, len(img[0]) // ss
    out = new_image(h, w)
    n = ss * ss
    for y in range(h):
        rows = img[y * ss:(y + 1) * ss]
        for x in range(w):
            acc = 0
            for r in rows:
                acc += sum(r[x * ss:(x + 1) * ss])
            out[y][x] = acc // n
    return out


# ---- Strokes and polygons -------------------------------------------------------------------------------------------------

def draw_capsule(cv_, cw, ch, ax, ay, bx, by, hw):
    """The union (max) of cv_ and a line from a to b of half width hw with round ends; Q8 pixels, a one pixel ramp at the edge. It walks
    the segment along its longer axis and tests a few pixels either side, so a long diagonal stays cheap."""
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    pad = hw + 256
    steep = abs(dy) > abs(dx)
    if steep:
        lo_m, hi_m, lim_m, lim_n = (min(ay, by) - pad) >> 8, (max(ay, by) + pad) >> 8, ch, cw
    else:
        lo_m, hi_m, lim_m, lim_n = (min(ax, bx) - pad) >> 8, (max(ax, bx) + pad) >> 8, cw, ch
    for m in range(max(lo_m, 0), min(hi_m, lim_m - 1) + 1):
        pm = (m << 8) + 128
        if steep:
            pc = min(max(pm, min(ay, by)), max(ay, by))
            oc = ax + dx * (pc - ay) // dy
        else:
            pc = min(max(pm, min(ax, bx)), max(ax, bx))
            oc = ay + (dy * (pc - ax) // dx if dx else 0)
        for n in range(max((oc - pad) >> 8, 0), min((oc + pad) >> 8, lim_n - 1) + 1):
            pn = (n << 8) + 128
            qx, qy = (pn, pm) if steep else (pm, pn)
            if l2:
                t = min(max((qx - ax) * dx + (qy - ay) * dy, 0), l2)
                fx, fy = ax + dx * t // l2, ay + dy * t // l2
            else:
                fx, fy = ax, ay
            d = isqrt((qx - fx) * (qx - fx) + (qy - fy) * (qy - fy))
            c = hw + 128 - d
            if c > 0:
                c = ONE if c >= 256 else c << 4
                row = cv_[m] if steep else cv_[n]
                col = n if steep else m
                if c > row[col]:
                    row[col] = c


def draw_ring(cv_, cw, ch, cx, cy, rad, hw):
    """A circle outline of radius rad and half width hw (Q8 pixels), max-composited."""
    ext = rad + hw + 256
    for y in range(max((cy - ext) >> 8, 0), min((cy + ext) >> 8, ch - 1) + 1):
        py = (y << 8) + 128 - cy
        row = cv_[y]
        for x in range(max((cx - ext) >> 8, 0), min((cx + ext) >> 8, cw - 1) + 1):
            px = (x << 8) + 128 - cx
            c = hw + 128 - abs(isqrt(px * px + py * py) - rad)
            if c > 0:
                c = ONE if c >= 256 else c << 4
                if c > row[x]:
                    row[x] = c


def poly_cov(w, h, poly, ss=4):
    """Coverage (Q12) of a convex polygon (Q8 vertices), ss x ss samples a pixel."""
    n = len(poly)
    area = sum(poly[i][0] * poly[(i + 1) % n][1] - poly[(i + 1) % n][0] * poly[i][1] for i in range(n))
    sgn = 1 if area > 0 else -1
    edges = [(poly[i][0], poly[i][1], poly[(i + 1) % n][0] - poly[i][0], poly[(i + 1) % n][1] - poly[i][1]) for i in range(n)]
    out = new_image(h, w)
    step = 256 // ss
    x_lo, x_hi = min(p[0] for p in poly) >> 8, max(p[0] for p in poly) >> 8
    y_lo, y_hi = min(p[1] for p in poly) >> 8, max(p[1] for p in poly) >> 8
    for iy in range(max(y_lo, 0), min(y_hi, h - 1) + 1):
        for ix in range(max(x_lo, 0), min(x_hi, w - 1) + 1):
            hit = 0
            for sy in range(ss):
                py = iy * 256 + step * sy + step // 2
                for sx in range(ss):
                    px = ix * 256 + step * sx + step // 2
                    for ex, ey, edx, edy in edges:
                        if sgn * (edx * (py - ey) - edy * (px - ex)) < 0:
                            break
                    else:
                        hit += 1
            out[iy][ix] = hit * ONE // (ss * ss)
    return out


# ---- Generators -----------------------------------------------------------------------------------------------------------

def pcb_traces(rng, h, w, n=22):
    """Faint copper seen through smoked acrylic: buses of traces with 45-degree turns and pads, seamless in x. Drawn at 2x as capsules
    one pixel wide and box-filtered; returns coverage (Q12) of an h x w tile."""
    ss = 2
    cw, ch = w * ss, h * ss
    cv_ = new_image(ch, cw)
    unit = (256, 181)                                     # an axis step and a diagonal step (1/sqrt 2) of unit length, Q8
    dirs = ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1))
    hw = 256                                              # half a tile pixel is one canvas pixel
    for _ in range(n):
        x, y = rng.below(w * 256), rng.below(h * 256)
        di = rng.below(4) * 2
        pts = [(x, y)]
        for _s in range(2 + rng.below(3)):
            seg = 14 * 256 + rng.below(46 * 256)
            dx, dy = dirs[di]
            u = unit[di & 1]
            x, y = x + dx * u * seg // 256, y + dy * u * seg // 256
            pts.append((x, y))
            di = (di + rng.below(2) * 2 - 1) % 8
        lanes = 1 + rng.below(3)
        for lane in range(lanes):
            off = (2 * lane - (lanes - 1)) * 358          # (lane - (lanes - 1) / 2) * 4 px * 0.7, Q8
            for wrap in (-w * 256, 0, w * 256):
                pp = [((px + wrap + off) * ss, (py + off) * ss) for px, py in pts]
                for (ax, ay), (bx, by) in zip(pp, pp[1:]):
                    draw_capsule(cv_, cw, ch, ax, ay, bx, by, hw)
                for qx, qy in (pp[0], pp[-1]):
                    draw_ring(cv_, cw, ch, qx, qy, 1229, hw)   # radius 2.4 tile px
    return box_down(cv_, ss)


def tex_obsidian(rng):
    """Smoked red-black acrylic: a faint lift at the top, a fine grain, and the board's traces showing through. 256 x 480, REPEAT in x."""
    h, w = 480, 256
    grain = normalise(blur(rng.normal_field(h, w), 154, 154, wrap_x=True))
    traces = pcb_traces(rng, h, w)
    out = []
    for y in range(h):
        grad = 1720 - (1393 * (2 * y + 1)) // 960          # 0.42 - 0.34 y
        gr, tr = grain[y], traces[y]
        out.append([clip12(grad + ((gr[x] * 123) >> 12) + ((tr[x] * 1147) >> 12)) for x in range(w)])
    return out


def knob_parts(rng, d_px):
    """A knob of diameter d_px (even) as three alpha masks, each in a tight cell: scale (11 ticks over 300 degrees), body (the skirt with
    its drop shadow) and cap (the spun-metal cap and the skirt's sheen, shading folded in as less light). The pointer is a live line.
    Returns ({'scale', 'body', 'cap'}: Q12 cells). The cell sizes are d + 18, d + 12 and d + 2."""
    R = d_px * 128                                         # radius, Q8
    t0, t1 = R + 666, R + 1638                             # the ticks run from R + 2.6 to R + 6.4 px

    def side(c):
        return c + c % 2

    # scale
    cs = side((2 * (t1 + 563) + 255) // 256)               # ceil(2 * (t1 + 2.2 px))
    ticks = []
    for k in range(11):
        a = -150 + 30 * k
        sx, sy = sin_deg(a), -cos_deg(a)
        ext = 410 if k in (0, 5, 10) else 0
        ticks.append(((sx * t0) >> 15, (sy * t0) >> 15, (sx * (t1 + ext)) >> 15, (sy * (t1 + ext)) >> 15))
    scale = new_image(cs, cs)
    lo2, hi2 = (t0 - 512) ** 2, (t1 + 410 + 512) ** 2
    for iy in range(cs):
        py = (2 * iy + 1 - cs) * 128
        for ix in range(cs):
            px = (2 * ix + 1 - cs) * 128
            if not lo2 <= px * px + py * py <= hi2:
                continue
            best = 0
            for x0, y0, x1, y1 in ticks:
                dx, dy = x1 - x0, y1 - y0
                t = min(max((px - x0) * dx + (py - y0) * dy, 0), dx * dx + dy * dy)
                l2 = dx * dx + dy * dy
                fx, fy = x0 + dx * t // l2, y0 + dy * t // l2
                c = cov(159 - isqrt((px - fx) ** 2 + (py - fy) ** 2))
                if c > best:
                    best = c
            scale[iy][ix] = best
    # body: the skirt disc, and its shadow (the disc moved by (1.4, 2.6), blurred, at 0.78)
    cb = side(d_px + 12)
    sh = new_image(cb, cb)
    for iy in range(cb):
        py = (2 * iy + 1 - cb) * 128
        for ix in range(cb):
            px = (2 * ix + 1 - cb) * 128
            sh[iy][ix] = cov(R + 154 - isqrt((px - 358) ** 2 + (py - 666) ** 2))
    shadow = blur(sh, max(410, R * 9 // 100))
    body = new_image(cb, cb)
    for iy in range(cb):
        py = (2 * iy + 1 - cb) * 128
        for ix in range(cb):
            px = (2 * ix + 1 - cb) * 128
            body[iy][ix] = max(cov(R - isqrt(px * px + py * py)), (clip12(shadow[iy][ix]) * 3195) >> 12)
    # cap
    cc = side(d_px + 2)
    Rc = R * 66 // 100
    grain = normalise(blur(rng.normal_field(cc, cc), 115, 115))
    nth = 24 if d_px < 50 else 32
    cap = new_image(cc, cc)
    for iy in range(cc):
        py = (2 * iy + 1 - cc) * 128
        for ix in range(cc):
            px = (2 * ix + 1 - cc) * 128
            r = max(isqrt(px * px + py * py), 1)
            disc, capc = cov(R - r), cov(Rc - r)
            skirt = disc - capc
            ul = max(-ONE, min(ONE, -(px + py) * 2896 // r))                      # +1 towards the upper left
            a = (-py * 3228 - px * 2522) // r                                     # cos(theta + 38 deg): spun metal, a bright bow-tie
            aniso = (abs(a) ** 3) >> 24
            rings = sin_turns(r * 640 // 3 + 7302) >> 3                           # sin(2 pi r / 1.2 px + 0.7)
            q = min(r * ONE // Rc, ONE)
            cap_b = 1925 + ((1720 * aniso) >> 12) + ((164 * rings) >> 12) + ((grain[iy][ix] * 102) >> 12) - ((410 * ((q * q) >> 12)) >> 12)
            t = clip12(ONE - abs(r - (Rc - 282)) * ONE // 282)
            cap_b += (((1229 * t) >> 12) * ul) >> 12
            x = clip12(-py * ONE // R)
            x15 = (x * isqrt(x * ONE)) >> 12                                      # x ^ 1.5
            t2 = clip12(ONE - abs(r - (R - 256)) * ONE // 256)
            skirt_b = 287 + ((696 * x15) >> 12) + ((((655 * t2) >> 12)) * max(ul, 0) >> 12)
            light = ((capc * clip12(cap_b)) >> 12) + ((skirt * clip12(skirt_b)) >> 12)
            # the knurling: cos(nth * theta) from the unit vector (cos, sin) raised to the nth power in Q15
            zr, zi = max(-32768, min(32768, -py * 32768 // r)), max(-32768, min(32768, px * 32768 // r))
            pw = [(zr, zi)]
            for _i in range(5):
                a_, b_ = pw[-1]
                pw.append(((a_ * a_ - b_ * b_) >> 15, (2 * a_ * b_) >> 15))
            if nth == 24:                                                            # z^8 * z^16
                zr_n = (pw[3][0] * pw[4][0] - pw[3][1] * pw[4][1]) >> 15
            else:                                                                    # z^32
                zr_n = pw[5][0]
            u = (ONE + (zr_n >> 3)) // 2
            knurl = (u * u) >> 12
            e1 = clip12((r - Rc - 384) * ONE // 512)
            k1 = (2458 * ((knurl * e1) >> 12)) >> 12
            k2 = (1638 * clip12(py * ONE // R)) >> 12
            shade = (skirt * (k1 + k2)) >> 12
            e2 = clip12(ONE - abs(r - Rc) * ONE // 218)
            shade = clip12(shade + ((2662 * e2) >> 12))
            cap[iy][ix] = clip12((light * (ONE - shade)) >> 12)
    return {'scale': scale, 'body': body, 'cap': cap}


def rbox_cov(w, h, x0, y0, x1, y1, r):
    """Coverage (Q12) of the box [x0, x1] x [y0, y1] with corners of radius r (Q8)."""
    out = new_image(h, w)
    for iy in range(h):
        py = (2 * iy + 1) * 128
        dy = max(y0 + r - py, py - (y1 - r), 0)
        row = out[iy]
        for ix in range(w):
            px = (2 * ix + 1) * 128
            dx = max(x0 + r - px, px - (x1 - r), 0)
            row[ix] = cov(r - isqrt(dx * dx + dy * dy))
    return out


def key_cells():
    """A step key, 42 x 36: cell 0 the body (tinted with the key colour), 1 the light (white: the top-lit face and a bright top lip), 2 the
    dark (black: the drop shadow outside the body and the lower lip inside it, one mask since both are black)."""
    w, h = 42, 36
    x0, y0, x1, y1 = 1024, 1024, 38 * 256, 30 * 256        # the 34 x 26 body, 4 px in
    kh = y1 - y0
    body = rbox_cov(w, h, x0, y0, x1, y1, 1024)
    shadow = blur(rbox_cov(w, h, x0, y0 + 640, x1, y1 + 640, 1024), 410)
    face = rbox_cov(w, h, x0 + 384, y0 + 384, x1 - 384, y1 - 1024, 768)
    c_body, c_light, c_dark = new_image(h, w), new_image(h, w), new_image(h, w)
    for iy in range(h):
        py = (2 * iy + 1) * 128
        t = clip12((py - y0) * ONE // kh)
        top = clip12((y0 + 1024 - py) * ONE // 768)
        low = clip12((py - (y1 - 1280)) * ONE // 768)
        for ix in range(w):
            b, f = body[iy][ix], face[iy][ix]
            rim = max(b - f, 0)
            light = ((f * (1720 - ((901 * t) >> 12))) >> 12) + ((((rim * top) >> 12) * 3277) >> 12)
            lower = (rim * low) >> 12
            dark = ((((clip12(shadow[iy][ix]) * 3277) >> 12) * (ONE - b)) >> 12) + ((clip12(((lower * 3072) >> 12) + 328) * b) >> 12)
            c_body[iy][ix], c_light[iy][ix], c_dark[iy][ix] = b, clip12(light), clip12(dark)
    return [c_body, c_light, c_dark]


SEGMENTS = {
    '0': 'abcdef', '1': 'bc', '2': 'abged', '3': 'abgcd', '4': 'fgbc', '5': 'afgcd', '6': 'afgedc', '7': 'abc',
    '8': 'abcdefg', '9': 'abcdfg', 'A': 'abcefg', 'b': 'cdefg', 'C': 'adef', 'd': 'bcdeg', 'E': 'adefg',
    'F': 'aefg', 'G': 'acdef', 'H': 'bcefg', '-': 'g', ' ': '',
}
SEG_CHARS = '0123456789AbCdEFGH- '           # cells 0-9, A-H (A b C d E F G H), "-", blank
SEG_W, SEG_H = 27, 38
SEG_DOT_Q4 = (361, 514)                        # the decimal point's centre in a cell, 1/16 px: (22.6, 32.15), right of the digit's lower right segment


def seg_cells():
    """7-segment glyphs, our own drawing: pointed segments and a slight italic. One cell a character with everything a lit display shows:
    the faint ghost 8 (0.14), the lit segments at full light and their glow (0.55), all in the one red. The blank is the ghost alone."""
    dw, dh, h2, g, pad = 16 * 256, 30 * 256, 474, 192, 1024

    def tx(x, y):
        return (pad + x + (dh - y) // 10, pad + y)         # a slight italic: skew 0.10

    def hseg(y, x0, x1):
        return [tx(x0, y), tx(x0 + h2, y - h2), tx(x1 - h2, y - h2), tx(x1, y), tx(x1 - h2, y + h2), tx(x0 + h2, y + h2)]

    def vseg(x, y0, y1):
        return [tx(x, y0), tx(x + h2, y0 + h2), tx(x + h2, y1 - h2), tx(x, y1), tx(x - h2, y1 - h2), tx(x - h2, y0 + h2)]

    xa, xb = h2 + g, dw - h2 - g
    polys = {
        'a': hseg(h2, xa, xb), 'g': hseg(dh // 2, xa, xb), 'd': hseg(dh - h2, xa, xb),
        'f': vseg(h2, h2 + g, dh // 2 - g), 'b': vseg(dw - h2, h2 + g, dh // 2 - g),
        'e': vseg(h2, dh // 2 + g, dh - h2 - g), 'c': vseg(dw - h2, dh // 2 + g, dh - h2 - g),
    }
    assert (dw + dh // 10 + 2 * pad + 255) // 256 == SEG_W and (dh + 2 * pad + 255) // 256 == SEG_H
    lit = {s: poly_cov(SEG_W, SEG_H, p) for s, p in polys.items()}
    glow = {s: blur(c, 614) for s, c in lit.items()}           # the segments do not overlap, so the blur of the union is the sum
    cells = []
    for ch in SEG_CHARS:
        cell = new_image(SEG_H, SEG_W)
        for y in range(SEG_H):
            for x in range(SEG_W):
                on = sum(lit[s][y][x] for s in SEGMENTS[ch])
                gl = sum(glow[s][y][x] for s in SEGMENTS[ch])
                eight = sum(lit[s][y][x] for s in 'abcdefg')
                cell[y][x] = max(clip12(on), (clip12((gl * 5120) >> 12) * 2253) >> 12, (clip12(eight) * 573) >> 12)
        cells.append(cell)
    return cells


def arc_points(cx, cy, rx, ry, a0, a1, step=10):
    """Points of an elliptical arc, angles in degrees from 12 o'clock clockwise (Q8 pixels)."""
    pts = []
    a = a0
    while True:
        a = min(a, a1)
        pts.append((cx + ((rx * sin_deg(a)) >> 15), cy - ((ry * cos_deg(a)) >> 15)))
        if a >= a1:
            return pts
        a += step


def logo_cell():
    """The wordmark "O3-303", 150 x 50, our own: skewed strokes with round ends (italic), L4, no tagline (the tagline is live text)."""
    w, h = 150, 50
    cv_ = new_image(h, w)
    hw = 768                                                # a stroke 6 px wide
    skew = 41                                               # the italic: 0.16 of the height above the middle line, Q8
    mid = 26 * 256

    def slant(pts):
        return [(x + ((mid - y) * skew >> 8), y) for x, y in pts]

    def stroke(pts, closed=False):
        pts = slant(pts)
        if closed:
            pts = pts + pts[:1]
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            draw_capsule(cv_, w, h, ax, ay, bx, by, hw)

    def ring(cx, rx):                                       # an O or a 0: an ellipse, the 0 narrower
        stroke(arc_points(cx, mid, rx, 4096, 0, 350), True)

    def three(cx):                                          # two bowls open to the left, meeting in the middle
        stroke(arc_points(cx, 4544, 1638, 1984, -80, 180))
        stroke(arc_points(cx, 8640, 1843, 2112, 0, 245))

    def hyphen(cx):
        stroke([(cx - 1408, mid), (cx + 1408, mid)])

    # (drawing, centre line half width): the glyphs run left to right with a gap between their outer edges
    glyphs = ((lambda cx: ring(cx, 2355), 2355), (three, 1843), (hyphen, 1408), (three, 1843), (lambda cx: ring(cx, 1690), 1690), (three, 1843))
    x = 2 * 256
    for draw, half in glyphs:
        x += half + hw
        draw(x)
        x += half + hw + 640
    return [cv_]


def led_cell():
    """An LED, 16 x 16, L8, one mask tinted by state: the socket's ring, the domed lens (brightest in the middle, darker at the rim) and a
    specular spot up and left. The glow of a lit one is a POINT drawn behind it."""
    n = 16
    out = new_image(n, n)
    for iy in range(n):
        py = (2 * iy + 1 - n) * 128
        for ix in range(n):
            px = (2 * ix + 1 - n) * 128
            r = isqrt(px * px + py * py)
            lens = cov(1280 - r)                              # radius 5
            socket = (cov(1900 - r) - cov(1500 - r))          # a ring from 5.9 to 7.4: a soft bezel
            q = clip12(r * ONE // 1280)
            dome = ONE - (((q * q) >> 12) * 1884 >> 12)         # 1.0 in the middle, 0.54 at the rim
            spec = cov(430 - isqrt((px + 450) ** 2 + (py + 450) ** 2))
            v = (lens * dome >> 12) + (socket * 700 >> 12)
            out[iy][ix] = clip12(max(v, spec))
    return [out]


def cap_cell(w, h):
    """A raised key or pad cap, w x h, L8, one mask tinted by state: the face a little under full light with a gentle fall to the lower
    edge, a bright top lip, a darker lower lip, and a faint shadow under it (rows below the cap)."""
    x0, y0, x1, y1 = 256, 256, (w - 1) * 256, (h - 3) * 256
    body = rbox_cov(w, h, x0, y0, x1, y1, 1536)
    under = blur(rbox_cov(w, h, x0, y0 + 512, x1, y1 + 512, 1536), 410)
    inner = rbox_cov(w, h, x0 + 512, y0 + 512, x1 - 512, y1 - 768, 1024)
    out = new_image(h, w)
    ch = y1 - y0
    for iy in range(h):
        py = (2 * iy + 1) * 128
        t = clip12((py - y0) * ONE // ch)
        top = clip12((y0 + 768 - py) * ONE // 640)
        low = clip12((py - (y1 - 1024)) * ONE // 768)
        face = 3277 - ((737 * t) >> 12)                       # 0.80 -> 0.62
        for ix in range(w):
            b = body[iy][ix]
            rim = max(b - inner[iy][ix], 0)
            lit = face + ((819 * top) >> 12) - ((1229 * low) >> 12) + ((rim * 164) >> 12)
            shadow = (clip12(under[iy][ix]) * 246 >> 12) * (ONE - b) >> 12
            out[iy][ix] = clip12(max((b * clip12(lit)) >> 12, shadow))
    return [out]


# ---- Assets ---------------------------------------------------------------------------------------------------------------

BPP = {'L4': 4, 'L8': 8}


class Asset:
    """A bitmap for RAM_G: `cells` masks of one size (Q12 light), quantised to `fmt` the way the chip reads them."""

    def __init__(self, name, ident, fmt, cells, what, draw=None, repeat_x=False, dither=False):
        self.name, self.ident, self.fmt, self.what = name, ident, fmt, what
        self.h, self.w = len(cells[0]), len(cells[0][0])
        bpp = BPP[fmt]
        self.levels = (1 << bpp) - 1
        self.stride = (self.w * bpp + 7) // 8
        self.cells = len(cells)
        self.draw_w, self.draw_h = draw if draw else (self.w, self.h)
        self.repeat_x = repeat_x
        self.q = []
        for c in cells:
            if dither:
                self.q.append([[min(max((v * self.levels + BAYER_Q12[y & 3][x & 3]) >> 12, 0), self.levels) for x, v in enumerate(row)]
                               for y, row in enumerate(c)])
            else:
                self.q.append([[min(max((v * self.levels + 2048) >> 12, 0), self.levels) for v in row] for row in c])
        data = bytearray()
        for q in self.q:
            for row in q:
                if bpp == 8:
                    data += bytes(row)
                else:
                    r = row + [0] * (len(row) % 2)
                    data += bytes((r[i] << 4) | r[i + 1] for i in range(0, len(r), 2))
        data += bytes((-len(data)) % 4)
        self.data = bytes(data)
        self.bytes = len(self.data)
        self.offset = 0

    def sample(self, cell):
        """0..255, what the chip takes as the pixel's alpha: L8 the byte, L4 the nibble x 17."""
        k = 255 // self.levels
        return [[v * k for v in row] for row in self.q[cell]]


def build_assets():
    """The nine assets of docs/SKIN-PLAN.md section 3 (13 bitmaps), in the order of the table and of the random stream."""
    rng = XorShift(303)
    a = []
    a.append(Asset('tex_obsidian', 'TexObsidian', 'L4', [tex_obsidian(rng)], 'smoked acrylic with traces, 256 x 480 tile, drawn 800 wide, REPEAT in x',
                   draw=(800, 480), repeat_x=True, dither=True))
    for d in (42, 58):
        parts = knob_parts(rng, d)
        for part, fmt, what in (('scale', 'L4', 'the 11 ticks'), ('body', 'L4', 'skirt and drop shadow'), ('cap', 'L8', 'spun cap, sheen, shading')):
            a.append(Asset(f'knob{d}_{part}', f'Knob{d}{part.capitalize()}', fmt, [parts[part]], f'knob {d} px: {what}'))
    a.append(Asset('key', 'Key', 'L8', key_cells(), 'step key: body, light, dark'))
    a.append(Asset('seg', 'Seg', 'L4', seg_cells(), '7-segment 0-9 A b C d E F G H - and blank, ghost and glow in each cell'))
    a.append(Asset('logo', 'Logo', 'L4', logo_cell(), 'the O3-303 wordmark, no tagline'))
    a.append(Asset('led', 'Led', 'L8', led_cell(), 'LED: socket, lens, specular'))
    a.append(Asset('lamp', 'Lamp', 'L8', cap_cell(58, 40), 'raised pad cap'))
    a.append(Asset('actkey', 'ActKey', 'L8', cap_cell(84, 40), 'raised action key cap'))
    off = 0
    for s in a:
        s.offset = off
        off += s.bytes
    return a


def total_bytes(assets):
    return sum(s.bytes for s in assets)


# ---- Output ---------------------------------------------------------------------------------------------------------------

BANNER = ('// Generated by tools/gen_skin.py (python3 -I tools/gen_skin.py --out <dir>) from docs/SKIN-PLAN.md section 3. Do not edit: the build writes it,\n'
          '// tools/skin_assets.sha256 holds its hash, and nothing generated is in git.\n')


def header_text(assets):
    n = len(assets)
    out = ['// SPDX-License-Identifier: GPL-3.0-or-later\n', BANNER, '#pragma once\n\n',
           '// The obsidian skin\'s bitmaps (docs/SKIN-PLAN.md section 3) in the app\'s RAM_G region: one row of the AppAsset table each, laid one after the\n'
           '// other from kRamGApp, every one on a word. kInfo says how to draw them; desc() is the BitmapDesc for ui::plot::Canvas::bind().\n'
           '// The texture is one 256 x 480 cell drawn 800 wide (REPEAT in x); a screen that draws it as two bands changes draw_h and the position.\n\n',
           '#include <o3sdk/app/app_assets.h>\n#include <o3sdk/eve/dl.h>\n#include <o3sdk/ui/plot/canvas.h>\n#include <o3sdk/ui/plot/ram_g_map.h>\n\n',
           '#include <cstddef>\n#include <cstdint>\n\n', 'namespace o3_303::skin_assets {\n\n', 'enum class Id : std::uint8_t {\n']
    out += [f'  {s.ident},\n' for s in assets]
    out += ['};\n\n', f'inline constexpr std::size_t kCount = {n};\n\n',
            'struct Info {\n  const char* name;\n  o3sdk::eve::BitmapFormat format;\n  std::uint16_t cells;\n  std::uint16_t cell_w, cell_h;\n'
            '  std::uint16_t stride;           // bytes a row\n  std::uint16_t draw_w, draw_h;   // drawn size: the cell\'s, or wider with repeat_x\n'
            '  bool repeat_x;                  // Wrap::Repeat in x (the cell width is a power of two and the whole row)\n'
            '  std::uint32_t offset;           // from kRamGApp, a multiple of 4\n  std::uint32_t bytes;            // all the cells, padded to a word\n};\n\n',
            'inline constexpr Info kInfo[kCount] = {\n']
    for s in assets:
        out.append(f'    {{"{s.name}", o3sdk::eve::BitmapFormat::{s.fmt}, {s.cells}, {s.w}, {s.h}, {s.stride}, {s.draw_w}, {s.draw_h}, '
                   f'{"true" if s.repeat_x else "false"}, {s.offset}, {s.bytes}}},\n')
    out += ['};\n\n', f'inline constexpr std::uint32_t kTotalBytes = {total_bytes(assets)};\n',
            'static_assert(kTotalBytes <= o3sdk::ui::plot::kRamGAppBytes, "the skin\'s bitmaps fit the app\'s RAM_G region");\n',
            f'static_assert(kTotalBytes <= {MAX_TOTAL_BYTES}, "SK1\'s budget: docs/SKIN-PLAN.md section 8");\n\n',
            f'// The 7-segment cell\'s decimal point: its centre in a cell, in 1/16 px.\ninline constexpr int kSegDotX_q4 = {SEG_DOT_Q4[0]};\n'
            f'inline constexpr int kSegDotY_q4 = {SEG_DOT_Q4[1]};\n\n',
            'constexpr const Info& info(Id id) noexcept { return kInfo[static_cast<std::size_t>(id)]; }\n\n',
            '// The whole bitmap (cell 0 first; a sprite picks its cell by number).\n',
            'constexpr o3sdk::ui::plot::BitmapDesc desc(Id id) noexcept {\n  const Info& i = info(id);\n'
            '  return {o3sdk::ui::plot::kRamGApp + i.offset, i.format, i.stride, i.cell_w, i.cell_h, i.draw_w, i.draw_h,\n'
            '          i.repeat_x ? o3sdk::eve::Wrap::Repeat : o3sdk::eve::Wrap::Border, o3sdk::eve::Wrap::Border};\n}\n\n',
            'constexpr bool descs_valid() noexcept {\n  for (std::size_t i = 0; i < kCount; ++i) {\n'
            '    if (!o3sdk::ui::plot::bitmap_desc_valid(desc(static_cast<Id>(i)))) {\n      return false;\n    }\n  }\n  return true;\n}\n'
            'static_assert(descs_valid(), "every bitmap\'s desc is valid inside the app\'s region (Wrap::Repeat only where the chip can)");\n\n',
            '// The bytes, in flash (skin_assets.cpp), each on a word.\n']
    out += [f'alignas(4) extern const std::uint8_t k{s.ident}[{s.bytes}];\n' for s in assets]
    out += ['\n// The table App::assets() hands to the runtime.\ninline constexpr o3sdk::app::AppAsset kRows[kCount] = {\n']
    out += [f'    {{{s.offset}, k{s.ident}, {s.bytes}}},\n' for s in assets]
    out += ['};\nstatic_assert(o3sdk::app::app_assets_valid(kRows, kCount), "the table is inside the app\'s region, aligned and disjoint");\n\n',
            '}  // namespace o3_303::skin_assets\n']
    return ''.join(out)


def source_text(assets):
    out = ['// SPDX-License-Identifier: GPL-3.0-or-later\n', BANNER, '#include <o3_303/skin_assets.h>\n\nnamespace o3_303::skin_assets {\n\n']
    for s in assets:
        out.append(f'// {s.name}: {s.what} ({s.fmt}, {s.cells} cell{"s" if s.cells != 1 else ""} of {s.w} x {s.h}, stride {s.stride}, {s.bytes} bytes)\n')
        out.append(f'alignas(4) const std::uint8_t k{s.ident}[{s.bytes}] = {{')
        for i, b in enumerate(s.data):
            out.append(('\n    ' if i % 24 == 0 else ' ') + f'{b},')
        out.append('\n};\n\n')
    out.append('}  // namespace o3_303::skin_assets\n')
    return ''.join(out)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def hash_lines(assets, files):
    """One line a bitmap and one a generated file: a reviewer sees which one changed."""
    lines = [f'{sha(s.data)}  {s.name} {s.bytes}' for s in assets]
    lines += [f'{sha(text.encode("utf-8"))}  {name}' for name, text in files]
    return lines


HASH_BANNER = ('# tools/skin_assets.sha256: the SHA-256 of every bitmap tools/gen_skin.py makes (as packed for RAM_G) and of the two files it writes.\n'
               '# The build fails when the output differs (gen_skin.py --check); rewrite it with `python3 -I tools/gen_skin.py --update tools/skin_assets.sha256`\n'
               '# and review what changed.\n')


def read_hash_file(path):
    return [ln for ln in Path(path).read_text().splitlines() if ln and not ln.startswith('#')]


def write_if_changed(path, text):
    p = Path(path)
    if p.exists() and p.read_text() == text:
        return
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)


def generate():
    assets = build_assets()
    files = [('o3_303/skin_assets.h', header_text(assets)), ('skin_assets.cpp', source_text(assets))]
    return assets, files


def compare(label, got, want):
    if got == want:
        return True
    print(f'gen_skin.py: {label}: the output is not the committed hash', file=sys.stderr)
    for g in got:
        if g not in want:
            print(f'  generated  {g}', file=sys.stderr)
    for w in want:
        if w not in got:
            print(f'  committed  {w}', file=sys.stderr)
    return False


# ---- Preview --------------------------------------------------------------------------------------------------------------

def png_bytes(w, h, rows, channels):
    """rows: bytes of w * channels each (8-bit grey or RGB)."""
    raw = b''.join(b'\x00' + r for r in rows)

    def chunk(tag, body):
        return struct.pack('>I', len(body)) + tag + body + struct.pack('>I', zlib.crc32(tag + body) & 0xFFFFFFFF)

    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0 if channels == 1 else 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def contact_sheet(assets, path):
    """Every cell of every asset as a grey mask (the spike's assets/contact_sheet.png): tiles left to right, wrapped at 1000 px."""
    maxw, pad = 1000, 4
    placed, x, y, rowh = [], pad, pad, 0
    for s in assets:
        for c in range(s.cells):
            t = s.sample(c)
            if x + s.w + pad > maxw:
                x, y, rowh = pad, y + rowh + pad, 0
            placed.append((x, y, t))
            x += s.w + pad
            rowh = max(rowh, s.h)
    hh = y + rowh + pad
    img = [[30] * maxw for _ in range(hh)]
    for x0, y0, t in placed:
        for j, row in enumerate(t):
            dst = img[y0 + j]
            for i, v in enumerate(row):
                dst[x0 + i] = max(dst[x0 + i], v)
    Path(path).write_bytes(png_bytes(maxw, hh, [bytes(r) for r in img], 1))


def assembled(assets, path):
    """The parts tinted and put together over the panel, as a screen would: a look-check, not a model of the chip."""
    by = {s.name: s for s in assets}
    w, h = 800, 330
    base, tint = (12, 9, 11), (62, 24, 28)
    img = [[list(base) for _ in range(w)] for _ in range(h)]

    def put(asset, cell, x0, y0, rgb, alpha=255):
        t = asset.sample(cell)
        for j, row in enumerate(t):
            if not 0 <= y0 + j < h:
                continue
            for i, a in enumerate(row):
                if a and 0 <= x0 + i < w:
                    px = img[y0 + j][x0 + i]
                    k = a * alpha
                    for c in range(3):
                        px[c] += ((rgb[c] - px[c]) * k + 32512) // 65025

    def disc(cx, cy, rad, rgb, alpha):
        for y in range(cy - rad, cy + rad + 1):
            for x in range(cx - rad, cx + rad + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad and 0 <= x < w and 0 <= y < h:
                    px = img[y][x]
                    for c in range(3):
                        px[c] += ((rgb[c] - px[c]) * alpha + 127) // 255

    tex = by['tex_obsidian']
    t = tex.sample(0)
    for y in range(h):
        for x in range(w):
            a = t[y][x % 256]
            img[y][x] = [base[c] + ((tint[c] - base[c]) * a + 127) // 255 for c in range(3)]
    put(by['logo'], 0, 12, 7, (240, 104, 80))
    seg_x = 170
    for chn in 'A01':
        put(by['seg'], SEG_CHARS.index(chn), seg_x, 14, (255, 46, 30))
        seg_x += 27
    seg_x = 262
    for i, chn in enumerate('1250'):
        put(by['seg'], SEG_CHARS.index(chn), seg_x + 27 * i, 14, (255, 46, 30))
    disc(seg_x + 27 * 2 + SEG_DOT_Q4[0] // 16, 14 + SEG_DOT_Q4[1] // 16, 2, (255, 46, 30), 255)
    for i, (rgb, glow) in enumerate((((58, 12, 10), 0), ((160, 34, 24), 0), ((255, 50, 34), 1), ((90, 232, 80), 1))):
        cx = 400 + i * 30
        if glow:
            disc(cx + 8, 30, 12, rgb, 50)
        put(by['led'], 0, cx, 22, rgb)
    for k, d in enumerate((42, 58)):
        x0, y0 = 40 + k * 130, 100
        cs = by[f'knob{d}_scale']
        cb = by[f'knob{d}_body']
        cc = by[f'knob{d}_cap']
        cx, cy = x0 + 40, y0 + 40
        put(cs, 0, cx - cs.w // 2, cy - cs.h // 2, (190, 66, 54))
        put(cb, 0, cx - cb.w // 2, cy - cb.h // 2, (16, 16, 18))
        put(cc, 0, cx - cc.w // 2, cy - cc.h // 2, (150, 150, 160))
    key = by['key']
    for i, rgb in enumerate(((64, 58, 60), (36, 32, 34))):
        x0 = 330 + i * 50
        put(key, 0, x0, 100, rgb)
        put(key, 1, x0, 100, (255, 255, 255))
        put(key, 2, x0, 100, (0, 0, 0))
    for i, (rgb, a) in enumerate((((60, 18, 18), 255), ((120, 36, 30), 255), ((190, 60, 44), 255), ((240, 70, 46), 255))):
        put(by['lamp'], 0, 30 + i * 66, 200, rgb, a)
    for i, rgb in enumerate(((70, 44, 48), (210, 70, 50))):
        put(by['actkey'], 0, 330 + i * 90, 200, rgb)
    rows = [bytes(c for px in row for c in px) for row in img]
    Path(path).write_bytes(png_bytes(w, h, rows, 3))


def fresh(build_dir, hash_file):
    """Two separate runs of this script (each its own process, so a hash order that leaked into a byte would show), the build's copy and the
    committed hash file must all agree."""
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        runs = []
        for i in (1, 2):
            d = Path(tmp) / f'run{i}'
            r = subprocess.run([sys.executable, '-I', str(Path(__file__).resolve()), '--out', str(d), '--update', str(d / 'skin_assets.sha256')])
            if r.returncode != 0:
                print(f'gen_skin.py: run {i} failed', file=sys.stderr)
                return False
            runs.append(d)
        names = ('o3_303/skin_assets.h', 'skin_assets.cpp', 'skin_assets.sha256')
        for name in names:
            if (runs[0] / name).read_bytes() != (runs[1] / name).read_bytes():
                print(f'gen_skin.py: two runs differ in {name}', file=sys.stderr)
                ok = False
        for name in names[:2]:
            built = build_dir / name
            if not built.exists() or built.read_bytes() != (runs[0] / name).read_bytes():
                print(f'gen_skin.py: the build\'s {built} is not what the generator makes', file=sys.stderr)
                ok = False
        if hash_file:
            ok = compare('skin_assets.sha256', read_hash_file(runs[0] / 'skin_assets.sha256'), read_hash_file(hash_file)) and ok
    return ok


# ---- Main -----------------------------------------------------------------------------------------------------------------

def main(argv):
    ap = argparse.ArgumentParser(description='the obsidian skin\'s bitmaps (see the top of this file)')
    ap.add_argument('--out', help='write o3_303/skin_assets.h and skin_assets.cpp into this directory')
    ap.add_argument('--check', metavar='SHA256_FILE', help='fail when the output is not the hash in this file')
    ap.add_argument('--update', metavar='SHA256_FILE', help='write the hash file')
    ap.add_argument('--fresh', metavar='BUILD_DIR', help='ctest: two runs, the build\'s copy and the hash file all agree')
    ap.add_argument('--report', action='store_true', help='print each asset\'s size')
    ap.add_argument('--preview', metavar='DIR', help='write contact_sheet.png and assembled.png into this directory')
    args = ap.parse_args(argv)
    if not (args.out or args.update or args.fresh or args.report or args.preview or args.check):
        ap.error('nothing to do: --out, --check, --update, --fresh, --report or --preview')
    if args.fresh:
        return 0 if fresh(Path(args.fresh), args.check) else 1
    assets, files = generate()
    lines = hash_lines(assets, files)
    if total_bytes(assets) > MAX_TOTAL_BYTES or total_bytes(assets) > RAM_G_APP_BYTES:
        print(f'gen_skin.py: the assets take {total_bytes(assets)} bytes, over the budget', file=sys.stderr)
        return 1
    if args.report:
        print(f'{"asset":14s} {"fmt":3s} {"cell":>8s} {"cells":>5s} {"stride":>6s} {"offset":>7s} {"bytes":>7s}')
        for s in assets:
            print(f'{s.name:14s} {s.fmt:3s} {s.w:>4d}x{s.h:<3d} {s.cells:>5d} {s.stride:>6d} {s.offset:>7d} {s.bytes:>7d}  {s.what}')
        print(f'{"total":14s} {"":3s} {"":>8s} {"":>5s} {"":>6s} {"":>7s} {total_bytes(assets):>7d}')
    if args.out:
        for name, text in files:
            write_if_changed(Path(args.out) / name, text)
    if args.update:
        write_if_changed(args.update, HASH_BANNER + '\n'.join(lines) + '\n')
    if args.preview:
        Path(args.preview).mkdir(parents=True, exist_ok=True)
        contact_sheet(assets, Path(args.preview) / 'contact_sheet.png')
        assembled(assets, Path(args.preview) / 'assembled.png')
    ok = True
    if args.check:
        ok = compare('skin_assets.sha256', lines, read_hash_file(args.check)) and ok
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
