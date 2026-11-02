#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""midi_check: an automated MIDI test of o3-303 on the real Tracker, driven from this computer over USB (stdlib only; Linux).

    midi_check.py --expect-version <text of `version`> [--port P] [--midi-dev /dev/snd/midiC1D0] [--audio-device auto|hw:C,D]
                  [--phases identity,notes,follow,clockout,learn,stress,soak,restore] [--out DIR]
                  [--learn-baseline none] [--long-seconds 600] [--soak-seconds 1800] [--clockout-seconds 60] [--flood-seconds 30]
                  [--arecord arecord] [--no-audio] [--quick]
    midi_check.py --restore-from DIR --expect-version TEXT     put the Config back from a run that died (DIR/start_state.json)
    midi_check.py --self-test                                  against a fake unit (a pty console, FIFOs for the MIDI node, a fake arecord): no hardware

What it is: the HIL version of the MIDI claims of docs/MANUAL.md section 10, with numbers. The computer is the other end of the unit's
USB MIDI port (the unit shows up as "Track3r MIDI", a raw node /dev/snd/midiC<card>D<device>), it listens to the unit's USB audio
(profile P8: the master is on channels 7-8 of the capture stream), and it reads the unit's console (`midi`, `state`, `cpu`, `crash`)
and presses its keys (`key`, `enc`) as a finger would. Each phase prints PASS or FAIL with its measured numbers against the thresholds
at the top of this file, each with its reason; a JSON report and a Markdown summary go to --out.

THE UNIT IS SHARED. The tool opens nothing but the console until `version` contains --expect-version (and `state` names the app
O3-303), so it can never drive a unit that runs another firmware. It records the start state (the Config's MIDI rows, the screen, the
transport, the tempo) and, in a finally block, puts back every value it changed, stops the transport and goes back to the screen it
found. It sends only these console commands: version, state, midi, cpu, crash, log, key, enc, leds, usb, reset (learn phase only).
It never touches the card: the only writes are the kit's own settings file (Apps/o3_303/system.o3s) that the Config rows save by
themselves. DIN MIDI is out of scope (no DIN interface on this computer): it is owed.

The pieces of o3-sdk's platform/mk66/tools (o3console: the console port; o3audio: the capture and the WAV) are imported from
$O3SDK_DIR (default ../o3-sdk), never edited. Exit status: 0 every selected phase passed, 1 a check failed, 2 refused or a usage
error, 3 nothing failed but a check could not run (INCOMPLETE), 4 the restore did not put the unit back.
"""
import argparse
import array
import bisect
import cmath
import heapq
import importlib.util
import json
import math
import os
import random
import re
import select
import signal
import statistics
import subprocess
import sys
import threading
import time
import traceback

sys.dont_write_bytecode = True  # loading the tools of o3-sdk (and the fake) must leave no __pycache__ behind, in that repository least of all
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

# ---- thresholds ---------------------------------------------------------------------------------------------------------------------
# Every pass or fail below is against one of these. They are PROPOSALS written before the first run on a unit: where a number is a physical
# floor (a USB frame is 1 ms) it says so; where it is an engineering choice the reason is the margin it gives. The first run calibrates the
# ones marked (to calibrate); change them here and nowhere else.

EXPECT_APP = "O3-303"             # `state` app= must say so: the version text alone could name another image of the same build label

# -- notes
SOUND_MIN_DB = -50.0              # RMS of channels 7-8 while a note is held: it only says "there is a tone", the level is the patch's (to calibrate)
SILENCE_DB = -60.0                # o3audio.SILENCE_DB: the digital master is exactly zero at rest; the converter's last bit is not there
SILENCE_WAIT_S = 5.0              # after the last note-off the capture must be silent by then: the voice's release and the delay's tail, not a stuck note (to calibrate)
PITCH_TOL_CENTS = 20.0            # the brief; a 303's tuning drifts more than this only when TUNE is not at its default
PITCH_NOTES = (36, 43, 48, 55, 60)  # C2 G2 C3 G3 C4: two octaves of the bass range, 65 to 262 Hz
NOTE_HOLD_S = 1.0
NOTE_GAP_S = 0.7
NOTE_VELOCITY = 100
IDLE_FLOOR_PRE_S = 0.6            # silence needed before the first note of a capture to trust the rest of it (the stream's start pops are skipped before this)

# -- follow (the computer is the clock master)
LOCK_CLOCKS_MAX = 48              # clocks sent between the first and `locked yes`: the loop fits 12 clocks (half a beat) and judges lock on 8 more; two beats is the margin
TEMPO_TOL_BPM = 0.1               # the brief: the readout within 0.1 BPM, as the mean over the settled window
TEMPO_SAMPLE_TOL_BPM = 0.3        # any single readout in the settled window: three times the mean's, for the loop's ripple on a 1 ms USB clock (to calibrate)
TEMPO_PROFILE = (60, 90, 120, 140, 174, 200, 300)  # the brief; 300 BPM is the engine's ceiling (dsp kMaxTempoMilliBpm)
JITTER_MS = 2.0                   # the brief: +-2 ms on every clock
JITTER_BPM = 128
JITTER_TEMPO_TOL_BPM = 0.5        # the brief
REACQUIRE_MAX_S = 3.0             # tempo step to the readout within TEMPO_TOL_BPM and locked: the loop re-fits 12 clocks after three bad ones, about 1 s (to calibrate)
RAMP_FROM_BPM, RAMP_TO_BPM, RAMP_S = 100.0, 140.0, 20.0   # 2 BPM a second
RAMP_MEAN_ERR_BPM = 1.5           # mean |readout - commanded|: the loop's lag on a ramp at 0.5 Hz bandwidth plus the readout's delay (to calibrate)
RAMP_MAX_ERR_BPM = 3.0
STALE_CLOCKS = 48                 # the kit's kStaleClocks: two beats without a clock lets the master go
RELEASE_SLACK_S = 0.75            # past 48 clocks the release shows in `midi` after the render's block and our poll: 0.75 s covers the poll's 2 console round trips
STOP_WITHIN_BEATS = 1.0           # the brief: `playing` 0 within a beat of the Stop
SLIP_TOL = 1                      # the brief: the unit's clocks out against the clocks sent, +-1 at the end
SLIP_MID_TOL = 3                  # the same while running: the sample is a console round trip apart from the count
DRIFT_MAX_MS = 1.0                # the phase offset (unit's clock out minus ours) may not move more than one USB frame over the whole run: the loop has zero steady error
OFFSET_STDEV_MAX_MS = 1.0         # its scatter: the 1 ms frame's 0.41 ms rms, the read's timestamp noise and the loop (to calibrate)

# -- clock out (the unit is the master)
CLOCKOUT_BPMS = (120.0, 174.0)
CLOCKOUT_ERR_PCT = 0.02           # the brief: the mean BPM error
EDGE_UNCERT_S = 0.003             # the ends of a measurement are known to about 3 ms (a 1 ms frame and the host's read wake-up): it widens the limit on a short run only
INTERVAL_STDEV_MS = 0.6           # a 1 ms USB frame quantises each clock to the frame: the intervals' rms error is 0.41 ms at best; 0.6 leaves the host's read jitter
INTERVAL_MAX_DEV_MS = 2.0         # the same worst case: 1 ms from the frame plus the host's wake-up
INTERVAL_MISSING_FACTOR = 1.5     # an interval over 1.5 periods is a missing clock, under 0.5 periods an extra one
COUNT_TOL = 1                     # clocks against the expected count: the ends

# -- learn
BRIGHT_DELTA_MIN_DB = 6.0         # the HF/LF ratio of the notes at CC 127 over CC 0: a cutoff swept over its range moves it by tens of dB (to calibrate)
BRIGHT_MONO_MARGIN_DB = 1.0       # CC 64 sits between CC 0 and CC 127 by at least this
BRIGHT_CONTROL_MAX_DB = 3.0       # the same sweep with no binding: the notes are the same notes, so the spread is the measurement's

# -- stress
FLOOD_RATE = 1000                 # the brief: control changes a second
FLOOD_S = 30.0
STORM_NOTES = 2000                # the brief: notes in STORM_S seconds
STORM_S = 2.0
SYSEX_BYTES = 1024                # the brief: 1 KB blobs
SYSEX_COUNT = 10
RENDER_MAX_PCT = 75.0             # the render's worst block as a share of the block: U6a measured the app at 47.7 %; MIDI is estimated under 1.5 %; 100 % is an underrun
WIRE_BYTES_TOL = 0                # bytes the unit counted off USB against the bytes written to the node: every byte has a packet, so exact

# -- soak
SOAK_SAMPLE_S = 60.0              # the brief: `midi`, `cpu`, `crash` every 60 s
SOAK_BPM = 128
SOAK_LOCKED_MIN_PCT = 99.9        # of the readouts after the first minute
SOAK_TEMPO_TOL_BPM = 0.3

# ---- small helpers -------------------------------------------------------------------------------------------------------------------

pc_ns = time.perf_counter_ns
U32 = 1 << 32


def d32(after, before):
    """The increase of a 32-bit counter, wrap-safe."""
    return (int(after) - int(before)) % U32


def sec_ns(s):
    return int(round(s * 1e9))


def now_s():
    return time.monotonic()


def log_line(text, out=sys.stdout):
    print(text, file=out)
    out.flush()


class UnitError(Exception):
    """The unit did not answer, or answered something that cannot be."""


class Fatal(UnitError):
    """The run cannot go on (the port is gone, the firmware is not the expected one)."""


class Refused(Exception):
    """The safety check said no: nothing else is touched."""


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def sdk_tools_dir():
    sdk = os.environ.get("O3SDK_DIR") or os.path.join(REPO, "..", "o3-sdk")
    return os.path.join(os.path.abspath(sdk), "platform", "mk66", "tools")


def load_sdk_tool(name):
    path = os.path.join(sdk_tools_dir(), name + ".py")
    if not os.path.exists(path):
        raise Refused("%s not found: o3-sdk's platform/mk66/tools is expected at %s (set O3SDK_DIR to the o3-sdk checkout)" % (name, sdk_tools_dir()))
    return load_module("o3_" + name, path)


# ---- parsing what the unit says -------------------------------------------------------------------------------------------------------

def parse_pairs(line):
    out = {}
    for word in line.split():
        if "=" in word:
            key, _, value = word.partition("=")
            out[key] = value
    return out


def parse_state(text):
    """The `state` line as a dict of strings (app, screen, playing, tempo in milli-BPM, sys, section, ...), or None."""
    for line in text.splitlines():
        if line.startswith("app="):
            return parse_pairs(line)
    return None


def _on(word):
    return word == "on"


def _yes(word):
    return word == "yes"


def parse_midi(text):
    """The `midi` report (platform/mk66/console/midi_report.h) as a flat dict; lines the image does not print are simply absent. Counters are ints, tempo a float in BPM."""
    d = {"last_rx": [], "last_tx": []}
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith("midi: "):
            continue
        body = line[6:]
        m = re.match(r"following (yes|no) locked (yes|no) tempo ([\d.]+) bpm \(loop ([\d.]+) bpm, clocks (\d+), error (\d+) us\)", body)
        if m:
            d.update(following=_yes(m.group(1)), locked=_yes(m.group(2)), tempo_bpm=float(m.group(3)), loop_bpm=float(m.group(4)), in_clocks=int(m.group(5)), error_us=int(m.group(6)))
            continue
        m = re.match(r"config channel (omni|\d+) out (\d+) ports (din\+usb|din|usb|none) clock-out (on|off) transport-out (on|off) transport-in (on|off) follow (off|din|usb)", body)
        if m:
            d["config"] = {"channel": 0 if m.group(1) == "omni" else int(m.group(1)), "out_channel": int(m.group(2)), "ports": m.group(3), "clock_out": _on(m.group(4)),
                           "transport_out": _on(m.group(5)), "transport_in": _on(m.group(6)), "follow": m.group(7)}
            continue
        m = re.match(r"rx (\d+) tx (\d+) dropped (\d+) messages (\d+) notes (\d+) held (\d+) polled-dropped (\d+) sent (\d+) bad-out (\d+)", body)
        if m:
            d.update(zip(("rx", "tx", "dropped", "messages", "notes", "held", "polled_dropped", "sent", "bad_out"), map(int, m.groups())))
            continue
        m = re.match(r"clock-out (\d+) start-stop-out (\d+) transport-in (\d+) ignored (\d+) requests (\d+) attached (\d+) released (\d+)", body)
        if m:
            d.update(zip(("clock_out", "start_stop_out", "transport_events", "ignored", "requests", "attached", "released"), map(int, m.groups())))
            continue
        m = re.match(r"din rx isrs (\d+) bytes (\d+) errors (\d+) overruns (\d+) full (\d+) multi (\d+) tx irqs (\d+) bytes (\d+) rt (\d+) late-max (\d+) us no-time (\d+) not-ready (\d+)", body)
        if m:
            d.update(zip(("din_isrs", "din_bytes", "din_errors", "din_overruns", "din_full", "din_multi", "din_tx_irqs", "din_tx_bytes", "din_rt", "din_late_max_us", "din_no_time",
                          "din_not_ready"), map(int, m.groups())))
            continue
        m = re.match(r"usb frames (\d+) events-out (\d+) events-in (\d+) bytes-in (\d+) discarded (\d+) no-time (\d+) in-full (\d+)", body)
        if m:
            d.update(zip(("usb_frames", "usb_events_out", "usb_events_in", "usb_bytes_in", "usb_discarded", "usb_no_time", "usb_in_full"), map(int, m.groups())))
            continue
        m = re.match(r"loop clocks (\d+) reacquires (\d+) dropouts (\d+) missing (\d+)", body)
        if m:
            d.update(zip(("loop_clocks", "reacquires", "dropouts", "missing"), map(int, m.groups())))
            continue
        if body.startswith("last rx"):
            d["last_rx"] = body.split()[2:]
            continue
        if body.startswith("last tx"):
            d["last_tx"] = body.split()[2:]
    return d


def parse_cpu(text):
    """The `cpu` lines (audio/diag.h format_cpu and format_shed) as a dict: underrun, max_consecutive, mute_events, late, slips, hwfault, render_max_cyc, render_max_pct, over_budget, shed."""
    d = {}
    m = re.search(r"blocks (\d+)\s+renders (\d+)\s+underrun (\d+) \(run (\d+), max (\d+)\)\s+mute (\d+) \(events (\d+)\)\s+late (\d+)\s+slips (\d+)\s+hwfault (\d+)", text)
    if m:
        d.update(zip(("blocks", "renders", "underrun", "run", "max_consecutive", "mute", "mute_events", "late", "slips", "hwfault"), map(int, m.groups())))
    m = re.search(r"render last (\d+)\s+max (\d+) cyc = ([\d.]+) % of the block \((\d+) cyc\)\s+over-budget (\d+)", text)
    if m:
        d.update(render_last=int(m.group(1)), render_max_cyc=int(m.group(2)), render_max_pct=float(m.group(3)), block_cyc=int(m.group(4)), over_budget=int(m.group(5)))
    m = re.search(r"guard\s+shed (\d+) blocks in (\d+) runs\s+late (\d+)", text)
    if m:
        d.update(shed_blocks=int(m.group(1)), shed_runs=int(m.group(2)), shed_late=int(m.group(3)))
    return d


def crash_is_empty(text):
    return "no crash record" in text


def crash_clean(ctx, text):
    """No new crash: the record is empty, or it is the very one found at the start (another session's, not this run's)."""
    return crash_is_empty(text) or (ctx.start is not None and ctx.start.get("crash_text") == text.strip())


# ---- signal analysis ----------------------------------------------------------------------------------------------------------------

def fft(values):
    """In-place radix-2 FFT of a list of complex (length a power of two)."""
    n = len(values)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            values[i], values[j] = values[j], values[i]
    length = 2
    while length <= n:
        step = cmath.exp(-2j * math.pi / length)
        half = length >> 1
        for start in range(0, n, length):
            w = 1.0 + 0j
            for k in range(start, start + half):
                a = values[k]
                b = values[k + half] * w
                values[k] = a + b
                values[k + half] = a - b
                w *= step
        length <<= 1
    return values


def ifft(values):
    n = len(values)
    conj = [v.conjugate() for v in values]
    fft(conj)
    return [v.conjugate() / n for v in conj]


def db(x):
    return 20.0 * math.log10(x) if x > 1e-9 else -180.0


def rms_db(samples, start=0, stop=None):
    """RMS in dBFS (16-bit full scale) of samples[start:stop]; -180 for nothing."""
    seg = samples[start:stop]
    if len(seg) == 0:
        return -180.0
    return db(math.sqrt(sum(v * v for v in seg) / len(seg)) / 32768.0)


def envelope_db(samples, fs, frame_ms=10):
    """RMS in dBFS of consecutive frames: (list of dB, frame length in samples)."""
    n = max(1, int(fs * frame_ms / 1000))
    out = []
    for i in range(0, len(samples) - n + 1, n):
        seg = samples[i:i + n]
        out.append(db(math.sqrt(sum(v * v for v in seg) / n) / 32768.0))
    return out, n


def first_onset(env, frame_s, thresh_db, skip_s=0.0):
    """Time (s) of the first frame at or above thresh_db after skip_s, or None."""
    for i in range(int(skip_s / frame_s), len(env)):
        if env[i] >= thresh_db:
            return i * frame_s
    return None


def last_loud(env, frame_s, floor_db):
    """The end time (s) of the last frame above floor_db, or None when there is none."""
    for i in range(len(env) - 1, -1, -1):
        if env[i] > floor_db:
            return (i + 1) * frame_s
    return None


def autocorr_pitch(samples, fs, fmin=30.0, fmax=1200.0, size=8192):
    """(frequency in Hz, peak height 0..1) by autocorrelation, or (None, 0.0). The first peak within 90 % of the highest is taken (no octave down), parabolic interpolation on it."""
    n = len(samples)
    if n < 2 * int(fs / fmin) or n < 1024:
        return None, 0.0
    # the loudest `size`-long stretch's start is the caller's business: this takes the first `size` samples
    size = min(size, 1 << (n.bit_length() - 1))
    x = [float(v) for v in samples[:size]]
    mean = sum(x) / size
    x = [v - mean for v in x]
    energy = sum(v * v for v in x)
    if energy <= 0.0 or math.sqrt(energy / size) < 3.0:
        return None, 0.0
    buf = [complex(v, 0.0) for v in x] + [0j] * size
    fft(buf)
    power = [complex(abs(v) ** 2, 0.0) for v in buf]
    r = [v.real for v in ifft(power)]
    lag_min = max(2, int(fs / fmax))
    lag_max = min(size // 2, int(fs / fmin))
    norm = [r[k] / r[0] * size / (size - k) for k in range(lag_max + 2)]  # the overlap shrinks with the lag: the bias is undone
    # the first lag where the function has fallen below zero: peaks before it are the main lobe of lag 0
    k0 = lag_min
    while k0 < lag_max and norm[k0] > 0.0:
        k0 += 1
    peaks = [k for k in range(max(k0, lag_min), lag_max) if norm[k] > norm[k - 1] and norm[k] >= norm[k + 1]]
    if not peaks:
        return None, 0.0
    best = max(norm[k] for k in peaks)
    if best < 0.3:
        return None, best
    peak = next(k for k in peaks if norm[k] >= 0.9 * best)
    a, b, c = norm[peak - 1], norm[peak], norm[peak + 1]
    denom = a - 2.0 * b + c
    shift = 0.5 * (a - c) / denom if denom != 0.0 else 0.0
    shift = max(-0.5, min(0.5, shift))
    return fs / (peak + shift), norm[peak]


def cents(f, ref):
    return 1200.0 * math.log2(f / ref)


def note_hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def brightness_db(samples, fs, split_hz=1500.0, size=8192):
    """The energy above split_hz over the energy from 40 Hz to split_hz of the first `size` samples (Hann), in dB: how bright a note is, whatever its level."""
    n = len(samples)
    size = min(size, 1 << (n.bit_length() - 1)) if n >= 256 else 0
    if size < 256:
        return -180.0
    mean = sum(samples[:size]) / size
    buf = [complex((samples[i] - mean) * (0.5 - 0.5 * math.cos(2.0 * math.pi * i / size)), 0.0) for i in range(size)]
    fft(buf)
    lo, mid, hi = int(40.0 * size / fs), int(split_hz * size / fs), int(20000.0 * size / fs)
    hf = sum(abs(buf[k]) ** 2 for k in range(mid, min(hi, size // 2)))
    lf = sum(abs(buf[k]) ** 2 for k in range(lo, mid))
    return 10.0 * math.log10((hf + 1e-6) / (lf + 1e-6))


# ---- the clock out of the unit: analysis of timestamps ---------------------------------------------------------------------------------

def analyze_clock_out(stamps_ns, bpm, expected_span_s=None, edge_uncert_s=EDGE_UNCERT_S):
    """What a series of F8 arrival times (ns) says about a master at `bpm`: count, the mean BPM and its error in per cent, the intervals' stdev and worst deviation from the nominal period (ms),
    the clocks missing and extra (an interval over INTERVAL_MISSING_FACTOR periods is `k` missing clocks, under half a period an extra one), and the count against the expected one."""
    n = len(stamps_ns)
    out = {"count": n}
    if n < 3:
        return out
    period_ns = 60e9 / (24.0 * bpm)
    span = (stamps_ns[-1] - stamps_ns[0]) / 1e9
    mean_bpm = (n - 1) / 24.0 / span * 60.0
    intervals = [(stamps_ns[i + 1] - stamps_ns[i]) for i in range(n - 1)]
    missing = extra = stalls = 0
    clean = []
    i = 0
    while i < len(intervals):
        iv = intervals[i]
        if iv > INTERVAL_MISSING_FACTOR * period_ns:
            # a late interval followed by early ones that make up for it is the HOST's read that stalled and then took several clocks at once (they share a stamp): nothing was lost
            j = i + 1
            total = iv
            while j < len(intervals) and intervals[j] < 0.5 * period_ns:
                total += intervals[j]
                j += 1
            if j > i + 1 and int(round(total / period_ns)) == j - i:
                stalls += 1
                i = j
                continue
            missing += max(0, int(round(iv / period_ns)) - 1)
        elif iv < 0.5 * period_ns:
            extra += 1
        else:
            clean.append(iv)
        i += 1
    # the deviation of a clock from the nominal grid through the first clock: what the host would see as jitter however the intervals pair up
    ideal = [stamps_ns[0] + i * (span * 1e9 / (n - 1)) for i in range(n)]
    dev_ms = [(stamps_ns[i] - ideal[i]) / 1e6 for i in range(n)]
    out.update(span_s=span, mean_bpm=mean_bpm, err_pct=(mean_bpm - bpm) / bpm * 100.0, missing=missing, extra=extra, host_stalls=stalls,
               interval_stdev_ms=(statistics.pstdev(clean) / 1e6) if len(clean) > 1 else 0.0,
               interval_dev_max_ms=(max(abs(iv - period_ns) for iv in clean) / 1e6) if clean else 0.0,
               grid_dev_max_ms=max(abs(v) for v in dev_ms))
    out["err_limit_pct"] = max(CLOCKOUT_ERR_PCT, 100.0 * 2.0 * edge_uncert_s / span)
    if expected_span_s is not None:
        out["expected"] = expected_span_s * 24.0 * bpm / 60.0
        out["count_diff"] = n - out["expected"]
    return out


def offset_drift(sent_ns, recv_ns, period_ns):
    """The phase offset between the clocks the unit sent back (recv) and the clocks we sent, paired by index: the pairing shift that puts the median offset inside [0, one period) is taken (the unit's
    clock cannot precede ours). Returns {median_ms, stdev_ms, slope_ppm, drift_ms (the line's change over the run), max_dev_ms (worst point from the line), shift, n} or None."""
    n = min(len(sent_ns), len(recv_ns))
    if n < 50:
        return None
    best = None
    for shift in (-1, 0, 1, 2, -2):
        pairs = [(sent_ns[i], recv_ns[i + shift] - sent_ns[i]) for i in range(max(0, -shift), min(len(sent_ns), len(recv_ns) - shift))]
        if len(pairs) < 50:
            continue
        med = statistics.median(p[1] for p in pairs)
        if 0 <= med < period_ns:
            best = (shift, pairs)
            break
    if best is None:
        shift = 0
        pairs = [(sent_ns[i], recv_ns[i] - sent_ns[i]) for i in range(n)]
    else:
        shift, pairs = best
    xs = [p[0] / 1e9 for p in pairs]
    ys = [p[1] / 1e6 for p in pairs]  # ms
    mx = sum(xs) / len(xs)
    my = sum(ys) / len(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx if sxx > 0 else 0.0  # ms per second
    resid = [y - (my + slope * (x - mx)) for x, y in zip(xs, ys)]
    return {"median_ms": statistics.median(ys), "stdev_ms": statistics.pstdev(resid), "slope_ppm": slope * 1000.0,
            "drift_ms": slope * (xs[-1] - xs[0]), "max_dev_ms": max(abs(r) for r in resid), "shift": shift, "n": len(pairs)}


# ---- the console of the unit ----------------------------------------------------------------------------------------------------------

# The only commands this tool sends. `reset` is the learn phase's (the binding must survive it); the rest read, or inject a finger's events.
ALLOWED_COMMANDS = ("version", "state", "midi", "cpu", "crash", "log", "key", "enc", "leds", "usb", "reset")


class Unit:
    """The unit's console on an open port, with a transcript of every line exchanged."""

    def __init__(self, port, console, timeout=3.0, transcript=None):
        self.port = port
        self.console = console
        self.timeout = timeout
        self.fd = None
        self.transcript = transcript if transcript is not None else []
        self.t_origin = time.monotonic()

    def open(self):
        try:
            self.fd = self.console.open_port(self.port)
        except OSError as error:
            raise Fatal("cannot open the console %s: %s" % (self.port, error))
        if not self.console.wake(self.fd, max(self.timeout, 3.0)):
            self.close()
            raise Fatal("no prompt from the unit on %s within %.1f s (is another program using the port?)" % (self.port, max(self.timeout, 3.0)))

    def close(self):
        if self.fd is not None:
            try:
                os.close(self.fd)
            except OSError:
                pass
            self.fd = None

    def cmd(self, line, timeout=None, check=True):
        verb = line.split()[0]
        if verb not in ALLOWED_COMMANDS:
            raise UnitError("midi_check never sends %r (allowed: %s)" % (verb, ", ".join(ALLOWED_COMMANDS)))
        if self.fd is None:
            raise Fatal("the console is closed")
        try:
            text, ok = self.console.exchange(self.fd, line, timeout or self.timeout)
        except OSError as error:
            raise Fatal("the console failed on %r: %s" % (line, error))
        self.transcript.append((round(time.monotonic() - self.t_origin, 3), line, text[:400]))
        if check and not ok:
            raise UnitError("no prompt after %r within %.1f s" % (line, timeout or self.timeout))
        if check and re.match(r"\s*ERR", text):
            raise UnitError("%r was refused: %s" % (line, text.strip()[:200]))
        return text

    def inject(self, line):
        text = self.cmd(line)
        if not re.search(r"^ok ", text, re.MULTILINE):
            raise UnitError("%r: %s" % (line, text.strip()[:200]))

    def state(self):
        st = parse_state(self.cmd("state"))
        if st is None:
            raise UnitError("`state` gave no line of key=value pairs")
        return st

    def midi(self):
        text = self.cmd("midi")
        if "not in this image" in text:
            raise Fatal("this image has no MIDI (`midi` says so)")
        d = parse_midi(text)
        if "config" not in d or "rx" not in d:
            raise UnitError("`midi` is not the report this tool knows: %r" % text[:300])
        return d

    def cpu(self):
        return parse_cpu(self.cmd("cpu"))

    def crash(self):
        return self.cmd("crash").strip()

    def tempo_mbpm(self):
        return int(self.state()["tempo"])

    def wait_state(self, pred, within, poll=0.1, what="the state"):
        """The `state` dict once pred(state) is true, within `within` seconds; UnitError otherwise."""
        deadline = time.monotonic() + within
        last = None
        while True:
            last = self.state()
            if pred(last):
                return last
            if time.monotonic() >= deadline:
                raise UnitError("%s did not come within %.1f s (last state: %s)" % (what, within, {k: last.get(k) for k in ("screen", "sys", "section", "playing", "tempo")}))
            time.sleep(poll)

    def reset_and_wait(self, expect_version, expect_app=EXPECT_APP, timeout=90.0, log=log_line):
        """`reset`, then wait for the console to come back and prove again that it is the expected firmware. The port may vanish and return (USB enumerates again)."""
        if self.fd is None:
            raise Fatal("the console is closed")
        os.write(self.fd, b"reset\r")
        self.transcript.append((round(time.monotonic() - self.t_origin, 3), "reset", ""))
        time.sleep(0.5)
        self.close()
        time.sleep(1.5)
        deadline = time.monotonic() + timeout
        last_error = "no answer"
        while time.monotonic() < deadline:
            try:
                if os.path.exists(self.port):
                    self.open()
                    st = self.state()
                    version = self.cmd("version")
                    if expect_version not in version:
                        raise Fatal("after the reset `version` says %r, not %r: stopping" % (version.strip()[:120], expect_version))
                    if st.get("app") != expect_app:
                        raise Fatal("after the reset the app is %r, not %r: stopping" % (st.get("app"), expect_app))
                    return time.monotonic() - (deadline - timeout)
            except Fatal as error:
                if "stopping" in str(error):
                    raise
                last_error = str(error)
                self.close()
            except (UnitError, OSError) as error:
                last_error = str(error)
                self.close()
            time.sleep(0.5)
        raise Fatal("the unit did not come back within %.0f s after `reset` (%s)" % (timeout, last_error))


# ---- the raw MIDI node ----------------------------------------------------------------------------------------------------------------

class MidiTx:
    """The computer's output to the unit (the unit's MIDI IN): the raw node opened for writing."""

    def __init__(self, path):
        self.path = path
        self.fd = None
        self.bytes_written = 0

    def open(self, wait=0.0):
        deadline = time.monotonic() + wait
        while True:
            try:
                self.fd = os.open(self.path, os.O_WRONLY)
                return
            except OSError as error:
                if time.monotonic() >= deadline:
                    raise UnitError("cannot open the MIDI node %s for writing: %s (the unit's USB MIDI port must be plugged in; `amidi -l` names it; --midi-dev says which)" % (self.path, error))
                time.sleep(0.3)

    def close(self):
        if self.fd is not None:
            try:
                os.close(self.fd)
            except OSError:
                pass
            self.fd = None

    def reopen(self, wait=60.0):
        self.close()
        self.open(wait)

    def write(self, data):
        view = memoryview(data)
        while len(view):
            n = os.write(self.fd, view)
            self.bytes_written += n
            view = view[n:]


class MidiRx(threading.Thread):
    """The unit's output to the computer (its USB MIDI IN as we see it): the raw node read in a thread, every real-time byte stamped with perf_counter_ns when read() returned.
    The stamps are good to the node's own quantisation, a 1 ms USB frame, and the host's wake-up."""

    def __init__(self, path):
        super().__init__(daemon=True)
        self.path = path
        self.halt = threading.Event()
        self.clocks = array.array("q")   # F8 stamps
        self.clock_pos = array.array("q")  # the byte's place in the stream: a stamp is a read's, so bytes of one read share it and only the place orders them
        self.events = []                 # (stamp, byte, place) of FA FB FC FF
        self.other_bytes = 0
        self.total_bytes = 0
        self.error = None
        self.fd = None

    def open(self):
        try:
            self.fd = os.open(self.path, os.O_RDONLY | os.O_NONBLOCK)
        except OSError as error:
            raise UnitError("cannot open the MIDI node %s for reading: %s" % (self.path, error))
        self.start()
        return self

    def run(self):
        fd = self.fd
        try:
            if hasattr(os, "sched_setscheduler"):
                try:
                    os.sched_setscheduler(0, os.SCHED_RR, os.sched_param(10))  # the stamps are the measurement: like the sender, the reader asks for real-time priority when it may
                except (OSError, PermissionError):
                    pass
            while not self.halt.is_set():
                ready, _, _ = select.select([fd], [], [], 0.02)
                if not ready:
                    continue
                try:
                    data = os.read(fd, 4096)
                except BlockingIOError:
                    continue
                t = pc_ns()
                if not data:
                    time.sleep(0.005)  # a FIFO with no writer reads as end of file; a real node never does
                    continue
                place = self.total_bytes
                self.total_bytes += len(data)
                for i, b in enumerate(data):
                    if b == 0xF8:
                        self.clocks.append(t)
                        self.clock_pos.append(place + i)
                    elif b in (0xFA, 0xFB, 0xFC, 0xFF):
                        self.events.append((t, b, place + i))
                    elif b != 0xFE:
                        self.other_bytes += 1
        except OSError as error:
            self.error = error
        finally:
            try:
                os.close(fd)
            except OSError:
                pass

    def stop(self):
        self.halt.set()
        self.join(2.0)

    def mark(self):
        return len(self.clocks), len(self.events)


# ---- the clock master and every other thing the computer sends on a schedule ----------------------------------------------------------------

KIND_CODE = {"clk": 0, "on": 1, "off": 2, "cc": 3, "sx": 4, "tr": 5, "x": 6}
SPIN_NS = 1_000_000          # the last millisecond is a busy wait: time.sleep() overshoots by 50 to 100 us at best
START_DELAY_NS = 40_000_000


def clock_events(segments, jitter_ns=0, rng=None, t_start_ns=0, kind="clk"):
    """The clocks (F8) of a master as (t_rel_ns, b"\\xF8", "clk", 0), lazily. `segments`: ("tempo", bpm, seconds), ("ramp", bpm0, bpm1, seconds) or ("gap", seconds) (no clocks: a master that fell silent).
    A tempo segment's clocks are on the grid t_start + k * period (no accumulated error); jitter moves each clock by a uniform random +-jitter_ns from its grid time, never out of order."""
    rng = rng or random.Random(1)
    t = float(t_start_ns)
    last = -1
    for seg in segments:
        if seg[0] == "gap":
            t += seg[1] * 1e9
            continue
        if seg[0] == "tempo":
            period = 60e9 / (24.0 * seg[1])
            n = int(seg[2] * 1e9 / period)
            for k in range(n):
                ts = int(t + k * period + (rng.uniform(-jitter_ns, jitter_ns) if jitter_ns else 0.0))
                ts = max(ts, last + 50_000)
                last = ts
                yield (ts, b"\xF8", kind, 0)
            t += n * period
        elif seg[0] == "ramp":
            _, b0, b1, secs = seg
            end = t + secs * 1e9
            tc = t
            while tc < end:
                frac = (tc - t) / (secs * 1e9)
                bpm = b0 + (b1 - b0) * frac
                ts = int(tc + (rng.uniform(-jitter_ns, jitter_ns) if jitter_ns else 0.0))
                ts = max(ts, last + 50_000)
                last = ts
                yield (ts, b"\xF8", kind, 0)
                tc += 60e9 / (24.0 * bpm)
            t = tc
        else:
            raise ValueError("segment %r" % (seg,))


def segments_seconds(segments):
    return sum(seg[1] if seg[0] == "gap" else seg[-1] for seg in segments)


def at(t_s, data, kind="x", meta=0):
    return (sec_ns(t_s), bytes(data), kind, meta)


def merge_events(*streams):
    return heapq.merge(*streams, key=lambda e: e[0])


def clock_count(segments):
    """How many clocks `clock_events` makes for these segments (exact: the generator is the definition)."""
    return sum(1 for _ in clock_events(segments))


class Sender(threading.Thread):
    """Writes scheduled events to the node on a dedicated thread: time.perf_counter_ns, a sleep up to the last millisecond and a busy wait for it. The target and the real time of every
    event are kept (rel_target, rel_actual in ns from t0, kind, meta), so the report can say how late the computer was."""

    def __init__(self, writer, events, name="sender"):
        super().__init__(daemon=True, name=name)
        self.write = writer
        self.events = events
        self.abort = threading.Event()
        self.t0 = 0
        self.rec_target = array.array("q")
        self.rec_actual = array.array("q")
        self.rec_kind = array.array("b")
        self.rec_meta = array.array("h")
        self.bytes_sent = 0
        self.error = None
        self.sched = None
        self.ready = threading.Event()

    def go(self, t0=None):
        self.t0 = t0 if t0 is not None else pc_ns() + START_DELAY_NS
        self.start()
        return self

    def _wait_until(self, target):
        while True:
            left = target - pc_ns()
            if left <= 0:
                return True
            if self.abort.is_set():
                return False
            if left > SPIN_NS:
                time.sleep(min((left - SPIN_NS) / 1e9, 0.05))

    def run(self):
        try:
            if hasattr(os, "sched_setscheduler"):
                try:
                    os.sched_setscheduler(0, os.SCHED_RR, os.sched_param(10))
                    self.sched = "SCHED_RR"
                except (OSError, PermissionError):
                    self.sched = "normal"
        except Exception:
            self.sched = "normal"
        self.ready.set()
        try:
            for t_rel, data, kind, meta in self.events:
                if self.abort.is_set():
                    break
                if not self._wait_until(self.t0 + t_rel):
                    break
                a = pc_ns()
                self.write(data)
                self.rec_target.append(t_rel)
                self.rec_actual.append(a - self.t0)
                self.rec_kind.append(KIND_CODE[kind])
                self.rec_meta.append(meta)
                self.bytes_sent += len(data)
        except Exception as error:  # a node that went away: the owner sees it in .error
            self.error = error

    def stop(self):
        self.abort.set()
        self.join(3.0)

    def count(self, kind):
        code = KIND_CODE[kind]
        return sum(1 for k in self.rec_kind if k == code)

    def actuals(self, kind):
        """[(abs_ns, meta)] of the events of one kind as they were sent."""
        code = KIND_CODE[kind]
        return [(self.t0 + a, m) for a, k, m in zip(self.rec_actual, self.rec_kind, self.rec_meta) if k == code]

    def clocks_sent_by(self, abs_ns):
        """How many clocks had been written by the absolute time abs_ns."""
        code = KIND_CODE["clk"]
        n = 0
        for a, k in zip(self.rec_actual, self.rec_kind):
            if self.t0 + a > abs_ns:
                break
            n += 1 if k == code else 0
        return n

    def lateness_us(self, kind=None):
        """(count, p50, p99, max) in microseconds of actual - target over the events of `kind` (all when None). Late only: a thread cannot be early."""
        code = KIND_CODE[kind] if kind else None
        late = sorted((a - t) / 1e3 for t, a, k in zip(self.rec_target, self.rec_actual, self.rec_kind) if code is None or k == code)
        if not late:
            return (0, 0.0, 0.0, 0.0)
        return (len(late), late[len(late) // 2], late[min(len(late) - 1, int(len(late) * 0.99))], late[-1])


class ClockIndex:
    """Clock send times by index for the poll loop: the clocks `sent_by` a time without walking the arrays every poll (the sender appends, this keeps the clock ones)."""

    def __init__(self, sender):
        self.sender = sender
        self.n_seen = 0
        self.times = []

    def sent_by(self, abs_ns):
        s = self.sender
        code = KIND_CODE["clk"]
        total = len(s.rec_actual)
        while self.n_seen < total:
            if s.rec_kind[self.n_seen] == code:
                self.times.append(s.t0 + s.rec_actual[self.n_seen])
            self.n_seen += 1
        return bisect.bisect_right(self.times, abs_ns)


# ---- the unit's audio over USB ----------------------------------------------------------------------------------------------------------

class AudioError(Exception):
    pass


class AudioCapture:
    """`arecord` of the unit's USB capture stream, raw, read in a thread. Keeps the two channels of the master (7-8 by default) as arrays; the stamp of the first data is kept for the alignment
    notes (the stream's own clock is the samples: events are aligned to the first onset, see align_offset_ns)."""

    def __init__(self, device, arecord, channels, pair=(6, 7), rate=48000, keep=True, rms_frame_s=0.1):
        self.device = device
        self.arecord = arecord
        self.channels = channels
        self.pair = pair
        self.rate = rate
        self.keep = keep
        self.rms_frame_s = rms_frame_s
        self.left = array.array("h")
        self.right = array.array("h")
        self.rms_env = []        # (stream_time_s, dB of the pair) every rms_frame_s, kept even when keep is False
        self.frames = 0
        self.t_first_ns = None
        self.proc = None
        self.thread = None
        self.error = None
        self.stderr_text = ""

    def start(self):
        cmd = [self.arecord, "-q", "-D", self.device, "-f", "S16_LE", "-r", str(self.rate), "-c", str(self.channels), "-t", "raw", "-"]
        try:
            self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        except OSError as error:
            raise AudioError("cannot run %s: %s" % (self.arecord, error))
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()
        deadline = time.monotonic() + 6.0
        while self.t_first_ns is None:
            if self.proc.poll() is not None and self.t_first_ns is None:
                self.thread.join(1.0)
                raise AudioError("arecord exited %s before any audio: %s" % (self.proc.returncode, self.stderr_text.strip()[:300] or "no message"))
            if time.monotonic() > deadline:
                self.stop()
                raise AudioError("no audio from %s within 6 s (is the unit's capture stream open elsewhere?)" % self.device)
            time.sleep(0.02)
        return self

    def _read(self):
        frame_bytes = 2 * self.channels
        leftover = b""
        l_idx, r_idx = self.pair
        acc_sq = 0.0
        acc_n = 0
        next_env_at = int(self.rms_frame_s * self.rate)
        stride = 16  # the envelope is a monitor: a sixteenth of the samples tell the level, and the GIL belongs to the clock sender
        try:
            while True:
                chunk = self.proc.stdout.read1(65536)
                if not chunk:
                    break
                if self.t_first_ns is None:
                    self.t_first_ns = pc_ns()
                data = leftover + chunk
                whole = len(data) - len(data) % frame_bytes
                leftover = data[whole:]
                if whole == 0:
                    continue
                samples = array.array("h")
                samples.frombytes(data[:whole])
                if sys.byteorder == "big":
                    samples.byteswap()
                left = samples[l_idx::self.channels]
                right = samples[r_idx::self.channels]
                if self.keep:
                    self.left.extend(left)
                    self.right.extend(right)
                n = len(left)
                for i in range(0, n, stride):
                    mono = (left[i] + right[i]) * 0.5
                    acc_sq += mono * mono
                    acc_n += 1
                    if self.frames + i + 1 >= next_env_at:
                        self.rms_env.append((next_env_at / self.rate, db(math.sqrt(acc_sq / max(1, acc_n)) / 32768.0)))
                        acc_sq = 0.0
                        acc_n = 0
                        next_env_at += int(self.rms_frame_s * self.rate)
                self.frames += n
        except (OSError, ValueError) as error:
            self.error = error
        try:
            self.stderr_text = self.proc.stderr.read().decode("utf-8", "replace")
        except (OSError, ValueError):
            pass

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            try:
                self.proc.send_signal(signal.SIGINT)
            except OSError:
                pass
            try:
                self.proc.wait(3.0)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        if self.thread is not None:
            self.thread.join(3.0)
        return self

    def mono(self):
        return array.array("f", ((a + b) * 0.5 for a, b in zip(self.left, self.right)))

    def seconds(self):
        return self.frames / float(self.rate)

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.stop()
        return False


# ---- the unit's Config and screens, by the keys of a finger ---------------------------------------------------------------------------------

CONFIG_ROWS = {"channel": 0, "out_channel": 1, "ports": 2, "clock_out": 3, "transport_out": 4, "follow_on": 5, "follow_port": 6, "transport_in": 7, "clear_learn": 8}
SECTION_ORDER = ("firmware", "audio", "midi", "card", "display", "info")
PORT_ORDER = ("none", "din", "usb", "din+usb")
SCREEN_KEY = {"STEPS": "pattern", "PATTERN_SETUP": "pattern", "PATTERNS": "pattern", "MASTER": "master", "INSTRUMENT_VOICE": "params", "INSTRUMENT_FX": "params",
              "MODDED_NOTES": "sampleload", "MODDED_TONE": "sampleload", "GROWL": "sampleplay", "WOBBLE": "sampleedit", "PERFORM": "performance", "PERFORM_SETUP": "performance", "SONG": "song", "FILE_PROJECTS": "file",
              "FILE_LIBRARY": "file", "RECORD": "samplerec"}
KEY_GAP_S = 0.15


def cfg_view(midi_dict):
    """The Config as the rows show it: follow_on and follow_port split (the port is only visible while FOLLOW CLOCK is on)."""
    c = dict(midi_dict["config"])
    follow = c.pop("follow")
    c["follow_on"] = follow != "off"
    c["follow_port"] = follow if follow != "off" else None
    return c


class Panel:
    """Config's MIDI section and the screens, driven by `key` and `enc` and read back through `midi` and `state`. Every change is verified against the report, and no other row may have moved."""

    def __init__(self, unit, log=log_line):
        self.unit = unit
        self.log = log
        self.cursor = None            # the MIDI section's cursor row while Config stays open on it; None when it is unknown
        self.port_touched = False     # FOLLOW PORT was changed by this tool
        self.start_follow_port = None  # the port found, once it has been seen (it is only visible while FOLLOW CLOCK is on)
        self.cleared_learn = False

    def key(self, name, gap=KEY_GAP_S):
        self.unit.inject("key " + name)
        time.sleep(gap)

    # -- Config
    def config_open(self):
        st = self.unit.state()
        if st.get("sys") != "1":
            self.key("config", 0.4)
            self.unit.wait_state(lambda s: s.get("sys") == "1", 3.0, what="Config opening")
            self.cursor = None

    def config_close(self):
        st = self.unit.state()
        if st.get("sys") == "1":
            self.key("config", 0.4)
            self.unit.wait_state(lambda s: s.get("sys") == "0", 3.0, what="Config closing")
        self.cursor = None

    def goto_midi(self):
        self.config_open()
        for _ in range(8):
            section = self.unit.state().get("section")
            if section == "midi":
                return
            if section not in SECTION_ORDER:
                raise UnitError("Config is on section %r, which this tool does not know how to leave" % section)
            self.key("right" if SECTION_ORDER.index(section) < SECTION_ORDER.index("midi") else "left", 0.35)
            self.cursor = None
        raise UnitError("could not reach Config's MIDI section (the unit says section=%s)" % self.unit.state().get("section"))

    def goto_row(self, row):
        self.goto_midi()
        if self.cursor is None:
            for _ in range(10):  # Up clamps at the first row
                self.key("up", 0.12)
            self.cursor = 0
        while self.cursor < row:
            self.key("down", 0.12)
            self.cursor += 1
        while self.cursor > row:
            self.key("up", 0.12)
            self.cursor -= 1

    def read_cfg(self):
        return cfg_view(self.unit.midi())

    def _settle(self, pred, within=1.5):
        deadline = time.monotonic() + within
        while True:
            cfg = self.read_cfg()
            if pred(cfg):
                return cfg
            if time.monotonic() >= deadline:
                return cfg
            time.sleep(0.1)

    def set_field(self, name, want):
        cur = self.read_cfg()
        if cur[name] == want:
            return
        if name == "follow_port" and not cur["follow_on"]:
            raise UnitError("FOLLOW PORT shows only while FOLLOW CLOCK is on")
        row = CONFIG_ROWS[name]
        self.goto_row(row)
        if name in ("channel", "out_channel"):
            lo = 0 if name == "channel" else 1
            self.unit.inject("enc -64")
            time.sleep(0.3)
            if want > lo:
                self.unit.inject("enc %d" % (want - lo))
                time.sleep(0.3)
            got = self._settle(lambda c: c[name] == want)
            tries = 0
            while got[name] != want and tries < 17:  # the encoder's steps were not what was asked: F1 steps by one and wraps
                self.key("f1", 0.3)
                got = self._settle(lambda c: c[name] == want, 0.6)
                tries += 1
        else:
            got = cur
            tries = 0
            while got[name] != want and tries < 4:
                self.key("f1", 0.3)
                got = self._settle(lambda c: c[name] == want, 0.8)
                tries += 1
        if got[name] != want:
            raise UnitError("Config %s is %r after the keys, wanted %r" % (name, got[name], want))
        ignore = {name} | ({"follow_port"} if name == "follow_on" else set())
        moved = {k: (cur[k], got[k]) for k in cur if k not in ignore and cur[k] != got[k]}
        if moved:
            raise UnitError("a key moved other Config rows while setting %s: %s" % (name, moved))
        if name == "follow_port":
            self.port_touched = True

    def ensure_follow(self, on, port=None):
        """FOLLOW CLOCK on or off and, when it is on, FOLLOW PORT. The port found is remembered the first time it can be seen."""
        cur = self.read_cfg()
        if on:
            if not cur["follow_on"]:
                self.set_field("follow_on", True)
                cur = self.read_cfg()
            if self.start_follow_port is None and not self.port_touched:
                self.start_follow_port = cur["follow_port"]
            if port is not None and cur["follow_port"] != port:
                self.set_field("follow_port", port)
        else:
            if cur["follow_on"]:
                if port is not None and cur["follow_port"] != port:
                    self.set_field("follow_port", port)
                self.set_field("follow_on", False)
            elif port is not None and self.port_touched:
                self.set_field("follow_on", True)
                self.ensure_follow(False, port)

    def configure(self, **want):
        """Put the named Config rows at these values (channel, out_channel, ports, clock_out, transport_out, transport_in, follow_on, follow_port) and close Config."""
        try:
            for name in ("out_channel", "ports", "clock_out", "transport_out", "transport_in", "channel"):
                if name in want:
                    self.set_field(name, want[name])
            if "follow_on" in want or "follow_port" in want:
                cur = self.read_cfg()
                on = want.get("follow_on", cur["follow_on"])
                self.ensure_follow(on, want.get("follow_port"))
        finally:
            self.config_close()

    def clear_learn(self):
        """CLEAR LEARN: F1, then F1 again within the kit's 5 s question."""
        self.goto_row(CONFIG_ROWS["clear_learn"])
        self.key("f1", 0.5)
        self.key("f1", 0.8)
        self.cleared_learn = True
        self.config_close()

    # -- screens
    def goto_screen(self, want, tries=7):
        self.config_close()
        for _ in range(tries):
            cur = self.unit.state().get("screen")
            if cur == want:
                return
            key = SCREEN_KEY.get(want)
            if key is None:
                raise UnitError("this tool cannot reach the screen %r" % want)
            self.key(key, 0.45)
        cur = self.unit.state().get("screen")
        if cur != want:
            raise UnitError("could not reach the screen %s (the unit says %s)" % (want, cur))

    def set_transport(self, playing):
        """The own transport on or off by the Play key (not following)."""
        self.config_close()
        st = self.unit.state()
        if (st.get("playing") == "1") != playing:
            self.key("play", 0.3)
            self.unit.wait_state(lambda s: (s.get("playing") == "1") == playing, 2.0, what="the transport %s" % ("starting" if playing else "stopping"))

    def set_own_tempo(self, target_mbpm):
        """The tempo of the unit's own clock on MASTER's TEMPO knob, by the encoder, closed on the `state` readout (the knob's step is learnt from its first move)."""
        self.goto_screen("MASTER")
        step = 500.0
        for _ in range(40):
            st = self.unit.state()
            cur = int(st["tempo"])
            if cur == target_mbpm:
                return
            delta = target_mbpm - cur
            n = int(round(delta / step))
            if n == 0:
                raise UnitError("the tempo knob cannot reach %.3f BPM from %.3f (its step is about %.0f mBPM)" % (target_mbpm / 1000.0, cur / 1000.0, step))
            n = max(-64, min(64, n))
            self.unit.inject("enc %d" % n)
            time.sleep(0.3)
            new = int(self.unit.state()["tempo"])
            if new != cur and (new - cur) * n > 0:
                step = abs(new - cur) / abs(n)
        raise UnitError("the tempo did not settle on %.3f BPM (%.3f now)" % (target_mbpm / 1000.0, int(self.unit.state()["tempo"]) / 1000.0))


# ---- what a phase says -----------------------------------------------------------------------------------------------------------------

class Phase:
    """The checks of one phase: each has a name, a pass (True), fail (False) or could-not-run (None), the measured value and the limit it was held to."""

    def __init__(self, name, out=sys.stdout):
        self.name = name
        self.out = out
        self.checks = []
        self.notes = []
        self.data = {}
        self.error = None
        self.seconds = 0.0

    def check(self, name, ok, value, limit=""):
        self.checks.append({"name": name, "ok": ok, "value": str(value), "limit": str(limit)})
        tag = "PASS" if ok else ("FAIL" if ok is False else "SKIP")
        log_line("  %-4s  %s: %s%s" % (tag, name, value, (" (" + str(limit) + ")") if limit else ""), self.out)
        return ok

    def info(self, name, value):
        self.checks.append({"name": name, "ok": "info", "value": str(value), "limit": ""})
        log_line("  info  %s: %s" % (name, value), self.out)

    def note(self, text):
        self.notes.append(text)
        log_line("  note  " + text, self.out)

    @property
    def status(self):
        if self.error:
            return "FAIL"
        oks = [c["ok"] for c in self.checks if c["ok"] != "info"]
        if any(o is False for o in oks):
            return "FAIL"
        if any(o is None for o in oks) or not oks:
            return "INCOMPLETE"
        return "PASS"

    def as_dict(self):
        return {"name": self.name, "status": self.status, "seconds": round(self.seconds, 1), "error": self.error, "checks": self.checks, "notes": self.notes, "data": self.data}


class Ctx:
    """Everything a phase needs: the console, the panel, the node, the audio, the start state and the options."""

    def __init__(self, args, unit, panel, out_dir, log=log_line):
        self.args = args
        self.unit = unit
        self.panel = panel
        self.out_dir = out_dir
        self.log = log
        self.tx = MidiTx(args.midi_dev)
        self.rx = None
        self.start = None
        self.senders = []
        self.o3audio = None
        self.audio_device = None
        self.audio_channels = None
        self.audio_note = None
        self.expect_version = args.expect_version
        self.learn_bound = False
        self.patch_dirty = False      # the learn phase moved a parameter in RAM: the restore resets
        self.notes_user_channel = 1

    # -- the node
    def node(self):
        if self.tx.fd is None:
            self.tx.open()
        return self.tx

    def listen(self):
        if self.rx is not None:
            self.rx.stop()
        self.rx = MidiRx(self.args.midi_out_dev or self.args.midi_dev).open()
        return self.rx

    def unlisten(self):
        if self.rx is not None:
            self.rx.stop()
            self.rx = None

    # -- the audio
    def audio(self):
        """The USB capture device and its channel count, or None with the reason in self.audio_note."""
        if self.audio_device is not None:
            return self.audio_device
        if self.args.no_audio:
            self.audio_note = "--no-audio"
            return None
        try:
            if self.o3audio is None:
                self.o3audio = load_sdk_tool("o3audio")
            dev = self.args.audio_device
            if dev == "auto":
                dev = self.o3audio.find_unit(self.o3audio.list_devices(self.args.arecord))
                if dev is None:
                    self.audio_note = "no capture device names the unit (`arecord -l`); --audio-device hw:CARD,DEV"
                    return None
            self.audio_device = dev
            n = self.args.audio_channels or self.o3audio.capture_channels(dev) or self.o3audio.BOOT_CHANNELS
            if n < 8:
                self.audio_note = "the capture stream has %d channels: the master is on channels 7-8 only in profile P8" % n
                self.audio_device = None
                return None
            self.audio_channels = n
            return dev
        except Exception as error:
            self.audio_note = "audio unavailable: %s" % error
            return None

    def capture(self, keep=True):
        dev = self.audio()
        if dev is None:
            raise AudioError(self.audio_note or "no audio")
        return AudioCapture(dev, self.args.arecord, self.audio_channels, keep=keep)

    def audio_layout_ok(self):
        st = self.unit.state()
        usb = st.get("usb")
        if usb is not None and usb != "P8":
            self.audio_note = "the USB profile is %s, not P8: the master is not on channels 7-8" % usb
            return False
        return True

    def stop_senders(self):
        for s in list(self.senders):
            s.stop()
        self.senders.clear()

    def sample(self):
        """One poll: `midi` and `state` in two console round trips, stamped in the middle."""
        t0 = pc_ns()
        m = self.unit.midi()
        st = self.unit.state()
        t1 = pc_ns()
        m["t_ns"] = (t0 + t1) // 2
        m["state"] = st
        m["playing"] = st.get("playing") == "1"
        m["tempo_state_bpm"] = int(st["tempo"]) / 1000.0
        return m


def run_master(ctx, events, poll_s, on_sample, tail_s=0.0):
    """Runs a Sender over `events` on its thread and polls the unit in this one until it ends; on_sample(sample, sender, index) gets each poll. Returns the Sender (joined)."""
    sender = Sender(ctx.node().write, events)
    ctx.senders.append(sender)
    sender.go()
    index = ClockIndex(sender)
    try:
        while sender.is_alive():
            t = pc_ns()
            s = ctx.sample()
            on_sample(s, sender, index)
            rest = poll_s - (pc_ns() - t) / 1e9
            if rest > 0:
                time.sleep(rest)
        sender.join(3.0)
        if tail_s:
            time.sleep(tail_s)
    finally:
        if sender.is_alive():
            sender.stop()
        if sender in ctx.senders:
            ctx.senders.remove(sender)
    if sender.error is not None:
        raise UnitError("writing to the MIDI node failed: %s" % sender.error)
    return sender


def sender_note(phase, sender, what="clock sender"):
    n, p50, p99, mx = sender.lateness_us()
    phase.info("%s lateness" % what, "%d events, p50 %.0f us, p99 %.0f us, max %.0f us (%s)" % (n, p50, p99, mx, sender.sched or "normal"))
    return mx


def wait_released(ctx, within=8.0):
    """Waits until the unit no longer follows (the master was quiet for 48 clocks) so that the next test starts from a loose unit."""
    deadline = time.monotonic() + within
    while time.monotonic() < deadline:
        if not ctx.unit.midi()["following"]:
            return True
        time.sleep(0.2)
    return False


# ---- phase: identity -------------------------------------------------------------------------------------------------------------------

def save_text(ctx, name, text):
    d = os.path.join(ctx.out_dir, "baseline")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "w") as f:
        f.write(text if text.endswith("\n") else text + "\n")


def phase_identity(ctx):
    ph = Phase("identity")
    u = ctx.unit
    version = u.cmd("version")
    ph.check("version", ctx.expect_version in version, version.strip().splitlines()[0] if version.strip() else "(empty)", "contains %r" % ctx.expect_version)
    st = u.state()
    ph.check("app", st.get("app") == EXPECT_APP, st.get("app"), "is %s" % EXPECT_APP)
    midi_text = u.cmd("midi")
    mid = parse_midi(midi_text)
    ph.check("`midi` is the report this tool knows", "config" in mid and "rx" in mid, "ok" if "config" in mid else midi_text.strip()[:80])
    ph.check("the unit follows no clock now", mid.get("following") is False, "following %s" % ("yes" if mid.get("following") else "no"), "another master is a stop: nothing is run against it")
    crash = u.crash()
    if crash_is_empty(crash):
        ph.check("crash at start", True, "no crash record", "no crash record")
    else:
        ph.info("crash at start", "a record is there already (%s): it is saved in baseline/crash.txt and every later check requires it to stay exactly as it is" % crash.splitlines()[0])
    cpu_text = u.cmd("cpu")
    log_text = u.cmd("log 60")
    for name, text in (("version.txt", version), ("state.txt", u.cmd("state")), ("log.txt", log_text), ("midi.txt", midi_text), ("cpu.txt", cpu_text), ("crash.txt", crash)):
        save_text(ctx, name, text)
    ph.data["state"] = st
    ph.data["midi_config"] = mid.get("config")
    ph.data["cpu"] = parse_cpu(cpu_text)
    ph.info("MIDI config found", cfg_view(mid))
    ph.info("screen / transport / own tempo", "%s, playing %s, tempo %.3f BPM, Config %s" % (st.get("screen"), st.get("playing"), int(st["tempo"]) / 1000.0, "open" if st.get("sys") == "1" else "closed"))
    ph.info("USB audio set", st.get("usb", "(none reported)"))
    ctx.start = {
        "config": cfg_view(mid), "follow_port": None, "screen": st.get("screen"), "sys": st.get("sys"), "section": st.get("section"), "playing": st.get("playing") == "1",
        "tempo_mbpm": int(st["tempo"]), "following": bool(mid.get("following")), "crash_empty": crash_is_empty(crash), "crash_text": crash.strip(), "usb": st.get("usb"), "version": version.strip().splitlines()[0],
        "app": st.get("app"), "midi": {k: v for k, v in mid.items() if isinstance(v, (int, float, bool))},
    }
    with open(os.path.join(ctx.out_dir, "start_state.json"), "w") as f:
        json.dump(ctx.start, f, indent=1)
    if mid.get("following"):
        raise Fatal("the unit follows a MIDI clock already: stop that master and run again")
    return ph


# ---- the audio stream, aligned with what was sent --------------------------------------------------------------------------------------------

AUDIO_PREROLL_S = 1.5   # the capture runs this long before the first event: arecord and the stream's start pop settle


class Stream:
    """A finished capture lined up with the sender: the stream's time is its samples, and its zero is put where the first note-on's sound begins, so that every event time the sender
    logged maps to a sample (the host's and the unit's clocks agree to well under a millisecond over the seconds of a test)."""

    def __init__(self, cap, sender):
        self.fs = cap.rate
        self.mono = cap.mono()
        self.env, n = envelope_db(self.mono, self.fs, 10)
        self.frame_s = n / float(self.fs)
        self.ok = False
        self.offset_ns = 0
        self.seconds = cap.seconds()
        ons = sender.actuals("on")
        onset = first_onset(self.env, self.frame_s, -55.0, skip_s=IDLE_FLOOR_PRE_S)
        self.onset_s = onset
        if onset is not None and ons:
            self.offset_ns = ons[0][0] - sec_ns(onset)
            self.ok = True

    def t(self, abs_ns):
        return (abs_ns - self.offset_ns) / 1e9

    def idx(self, abs_ns):
        return max(0, int(self.t(abs_ns) * self.fs))

    def rms(self, a_ns, b_ns):
        return rms_db(self.mono, self.idx(a_ns), self.idx(b_ns))

    def idle_db(self, until_ns):
        """The RMS of the stream between the settle of its start and `until_ns`."""
        a = int(IDLE_FLOOR_PRE_S * self.fs)
        b = self.idx(until_ns)
        return rms_db(self.mono, a, b) if b > a + 1000 else None

    def tail(self, a_ns, b_ns, floor=SILENCE_DB):
        """(seconds from a_ns to the end of the last frame above `floor` before b_ns, stuck): stuck when the stream is still loud at b_ns."""
        i0 = int(self.t(a_ns) / self.frame_s)
        i1 = min(len(self.env), int(self.t(b_ns) / self.frame_s))
        last = None
        for i in range(max(0, i0), i1):
            if self.env[i] > floor:
                last = i
        if last is None:
            return 0.0, False
        return max(0.0, (last + 1) * self.frame_s - self.t(a_ns)), last >= i1 - 2


def audio_run(ctx, events, tail_s, max_s=180.0):
    """Captures the unit's master while `events` are sent; returns (Stream, Sender). The capture starts AUDIO_PREROLL_S before the first event and ends tail_s after the last."""
    cap = ctx.capture()
    sender = Sender(ctx.node().write, events)
    try:
        cap.start()
        time.sleep(AUDIO_PREROLL_S)
        ctx.senders.append(sender)
        sender.go()
        sender.join(max_s)
        if sender.error is not None:
            raise UnitError("writing to the MIDI node failed: %s" % sender.error)
        time.sleep(tail_s)
    finally:
        if sender in ctx.senders:
            ctx.senders.remove(sender)
        if sender.is_alive():
            sender.stop()
        cap.stop()
    return Stream(cap, sender), sender


def note_events(notes, hold_s, gap_s, ch, vel=NOTE_VELOCITY, t0=0.0):
    t = t0
    for n in notes:
        yield at(t, (0x90 | (ch - 1), n, vel), "on", n)
        t += hold_s
        yield at(t, (0x80 | (ch - 1), n, 64), "off", n)
        t += gap_s


def user_channel(ctx):
    cfg = ctx.unit.midi()["config"]
    return cfg["channel"] or 1


COUNTER_KEYS = ("rx", "tx", "dropped", "messages", "notes", "held", "polled_dropped", "clock_out", "transport_events", "ignored", "released", "attached", "din_errors", "din_overruns",
                "din_full", "usb_events_in", "usb_bytes_in", "usb_discarded", "usb_in_full", "usb_no_time", "loop_clocks", "reacquires", "dropouts", "missing", "in_clocks", "start_stop_out")


def deltas(after, before):
    return {k: d32(after[k], before[k]) for k in COUNTER_KEYS if k in after and k in before}


def audio_unavailable(ph, ctx, what):
    ph.check(what, None, "not run: %s" % (ctx.audio_note or "no audio"))


# ---- phase: notes ----------------------------------------------------------------------------------------------------------------------

def phase_notes(ctx):
    ph = Phase("notes")
    u = ctx.unit
    ctx.panel.config_close()
    ctx.panel.set_transport(False)
    ch = user_channel(ctx)
    ph.info("channel used", "%d (the user's: IN CHANNEL %s)" % (ch, "OMNI, so 1" if u.midi()["config"]["channel"] == 0 else u.midi()["config"]["channel"]))
    have_audio = ctx.audio() is not None and ctx.audio_layout_ok()
    if not have_audio:
        ph.note("audio checks cannot run: %s" % (ctx.audio_note or "no audio"))
    ph.note("the patch is whatever the unit holds (its defaults after a `reset`); pitch assumes TUNE at its default")

    # N1: notes, pitch, the counters
    m0 = u.midi()
    events = list(note_events(PITCH_NOTES, NOTE_HOLD_S, NOTE_GAP_S, ch))
    n_msgs = 2 * len(PITCH_NOTES)
    stream = None
    if have_audio:
        stream, sender = audio_run(ctx, events, SILENCE_WAIT_S + 0.5)
    else:
        sender = Sender(ctx.node().write, events).go()
        sender.join(30.0)
        time.sleep(0.5)
    m1 = u.midi()
    d = deltas(m1, m0)
    ph.check("notes: messages counted", d["messages"] == n_msgs, "%d of %d" % (d["messages"], n_msgs), "every note-on and note-off")
    ph.check("notes: rx up, dropped 0", d["rx"] >= n_msgs and d["dropped"] == 0, "rx +%d, dropped +%d" % (d["rx"], d["dropped"]))
    ph.check("notes: engine got them", d["notes"] == n_msgs, "notes +%d" % d["notes"])
    ph.info("last rx", " ".join(m1["last_rx"][-6:]))
    sender_note(ph, sender, "note sender")
    if stream is None:
        audio_unavailable(ph, ctx, "notes: the voice sounds")
    elif not stream.ok:
        ph.check("notes: the voice sounds", False, "no sound found in %.1f s of channels 7-8 (idle floor %s)" % (stream.seconds, stream.idle_db(sender.t0 + sender.rec_actual[0]) if sender.rec_actual else "?"), "RMS above -55 dBFS")
    else:
        ons = sender.actuals("on")
        offs = sender.actuals("off")
        idle = stream.idle_db(ons[0][0] - sec_ns(0.1))
        ph.check("notes: silence before the first note", idle is not None and idle < SILENCE_DB, "%.1f dBFS" % (idle if idle is not None else 0.0), "< %.0f dBFS" % SILENCE_DB)
        errs = []
        rms_hold = []
        for (t_on, n), (t_off, _) in zip(ons, offs):
            level = stream.rms(t_on + sec_ns(0.2), t_off - sec_ns(0.05))
            rms_hold.append(level)
            seg = stream.mono[stream.idx(t_on + sec_ns(0.30)):stream.idx(t_off - sec_ns(0.05))]
            f0, conf = autocorr_pitch(seg, stream.fs)
            errs.append(None if f0 is None else cents(f0, note_hz(n)))
        ph.check("notes: RMS rises during every note", all(v >= SOUND_MIN_DB for v in rms_hold), ", ".join("%.0f" % v for v in rms_hold) + " dBFS", ">= %.0f dBFS" % SOUND_MIN_DB)
        found = [e for e in errs if e is not None]
        ph.check("notes: pitch found on every note", len(found) == len(errs), "%d of %d" % (len(found), len(errs)), "autocorrelation, peak over 0.3")
        if found:
            worst = max(abs(e) for e in found)
            ph.check("notes: pitch within %.0f cents" % PITCH_TOL_CENTS, worst <= PITCH_TOL_CENTS, "cents per note %s, worst %.1f" % (", ".join("%+.1f" % e if e is not None else "?" for e in errs), worst),
                     "<= %.0f cents" % PITCH_TOL_CENTS)
            spread = max(found) - min(found)
            if worst > PITCH_TOL_CENTS and spread <= 8.0:
                ph.note("the error is the same on every note (%+.1f cents, spread %.1f): that is TUNE or the patch, not MIDI" % (statistics.mean(found), spread))
        ph.data["pitch_cents"] = errs
        tts, stuck = stream.tail(offs[-1][0], offs[-1][0] + sec_ns(SILENCE_WAIT_S))
        ph.check("notes: silence after the offs", not stuck, "silent %.2f s after the last note-off" % tts, "within %.1f s (voice release and delay tail)" % SILENCE_WAIT_S)

    # N2: velocity 0 is a note-off
    if have_audio:
        ev = list(merge_events([at(0.0, (0x90 | (ch - 1), 48, NOTE_VELOCITY), "on", 48), at(1.0, (0x80 | (ch - 1), 48, 64), "off", 48),
                                at(1.0 + SILENCE_WAIT_S + 0.5, (0x90 | (ch - 1), 48, NOTE_VELOCITY), "on", 48), at(2.0 + SILENCE_WAIT_S + 0.5, (0x90 | (ch - 1), 48, 0), "off", 48)]))
        stream, sender = audio_run(ctx, ev, SILENCE_WAIT_S + 0.5)
        if not stream.ok:
            ph.check("notes: velocity 0 ends a note", False, "no sound captured", "")
        else:
            ons, offs = sender.actuals("on"), sender.actuals("off")
            tts_80, stuck_80 = stream.tail(offs[0][0], ons[1][0] - sec_ns(0.1))
            tts_v0, stuck_v0 = stream.tail(offs[1][0], offs[1][0] + sec_ns(SILENCE_WAIT_S))
            ph.check("notes: velocity 0 ends the note", not stuck_v0 and tts_v0 <= tts_80 + 0.25, "silent %.2f s after the velocity 0, %.2f s after a real note-off" % (tts_v0, tts_80),
                     "no later than the real off + 0.25 s")

    # N3: all notes off (CC 123) and all sound off (CC 120)
    if have_audio:
        cc = lambda n: (0xB0 | (ch - 1), n, 0)
        ev = list(merge_events([at(0.0, (0x90 | (ch - 1), 43, NOTE_VELOCITY), "on", 43), at(1.2, cc(123), "cc", 123),
                                at(1.2 + SILENCE_WAIT_S + 0.5, (0x90 | (ch - 1), 43, NOTE_VELOCITY), "on", 43), at(2.4 + SILENCE_WAIT_S + 0.5, cc(120), "cc", 120)]))
        stream, sender = audio_run(ctx, ev, SILENCE_WAIT_S + 0.5)
        if not stream.ok:
            ph.check("notes: CC 123 silences", False, "no sound captured", "")
        else:
            ons = sender.actuals("on")
            ccs = sender.actuals("cc")
            for (t_cc, number), t_on, label in zip(ccs, [o[0] for o in ons], ("CC 123 (all notes off)", "CC 120 (all sound off)")):
                before = stream.rms(t_cc - sec_ns(0.4), t_cc - sec_ns(0.05))
                tts, stuck = stream.tail(t_cc, t_cc + sec_ns(SILENCE_WAIT_S))
                ph.check("notes: %s silences a held note" % label, before >= SOUND_MIN_DB and not stuck, "%.1f dBFS before, silent %.2f s after" % (before, tts), "sounding before, silent within %.1f s" % SILENCE_WAIT_S)

    # N4: no stuck note after a burst of overlapping notes
    rng = random.Random(7)
    ev = []
    t = 0.0
    sent_msgs = 0
    for _ in range(50):
        group = [rng.randint(24, 84) for _ in range(4)]
        for n in group:
            ev.append(at(t, (0x90 | (ch - 1), n, rng.randint(1, 127)), "on", n))
            t += 0.005
            sent_msgs += 1
        order = list(group)
        rng.shuffle(order)
        for n in order:
            ev.append(at(t, (0x80 | (ch - 1), n, 64), "off", n))
            t += 0.005
            sent_msgs += 1
    m0 = u.midi()
    if have_audio:
        stream, sender = audio_run(ctx, ev, SILENCE_WAIT_S + 0.5)
    else:
        sender = Sender(ctx.node().write, ev).go()
        sender.join(30.0)
        time.sleep(0.5)
        stream = None
    d = deltas(u.midi(), m0)
    ph.check("burst: messages counted", d["messages"] == sent_msgs and d["dropped"] == 0, "%d of %d, dropped +%d" % (d["messages"], sent_msgs, d["dropped"]), "200 overlapping note-ons, 200 offs in shuffled order, 5 ms apart")
    if stream is None:
        audio_unavailable(ph, ctx, "burst: no stuck note")
    elif not stream.ok:
        ph.check("burst: no stuck note", False, "no sound captured", "")
    else:
        t_end = sender.actuals("off")[-1][0]
        tts, stuck = stream.tail(t_end, t_end + sec_ns(SILENCE_WAIT_S))
        ph.check("burst: no stuck note", not stuck, "silent %.2f s after the last off" % tts, "within %.1f s" % SILENCE_WAIT_S)
    c = ctx.unit.cpu()
    ph.data["cpu"] = c
    return ph


# ---- phase: follow ----------------------------------------------------------------------------------------------------------------------

def save_start(ctx):
    """The start state again, with the FOLLOW PORT found (it is only readable while FOLLOW CLOCK is on): a run that dies can be put back from this file."""
    if ctx.start is not None:
        ctx.start["follow_port"] = ctx.panel.start_follow_port
        with open(os.path.join(ctx.out_dir, "start_state.json"), "w") as f:
            json.dump(ctx.start, f, indent=1)


def follow_prepare(ctx, ph):
    ctx.panel.config_close()
    ctx.panel.set_transport(False)
    ctx.panel.configure(follow_on=True, follow_port="usb", transport_in=True, clock_out=True, transport_out=True)
    cfg = ctx.panel.read_cfg()
    if "usb" not in cfg["ports"]:
        ctx.panel.configure(ports="din+usb")
    ph.info("follow set up", "FOLLOW CLOCK ON, FOLLOW PORT USB, TRANSPORT IN ON, CLOCK OUT ON, TRANSPORT OUT ON, OUT PORTS %s" % ctx.panel.read_cfg()["ports"].upper())
    save_start(ctx)
    ctx.listen()


def s_time(sample, sender):
    return (sample["t_ns"] - sender.t0) / 1e9


def settled(samples, sender, after_s):
    return [(s, n) for s, n in samples if s_time(s, sender) >= after_s]


def tempo_err_stats(rows, target):
    errs = [abs(s["tempo_state_bpm"] - target) for s, _ in rows]
    return (statistics.mean(errs), max(errs)) if errs else (None, None)


def collect(samples):
    def on_sample(s, sender, index):
        samples.append((s, index.sent_by(s["t_ns"])))
    return on_sample


def hold_scale(ctx):
    """The fixed lengths of the follow tests scale with --tempo-hold (10 s is 1.0): a short run keeps every test, shorter."""
    return max(0.4, min(1.0, ctx.args.tempo_hold / 10.0))


def f_start_lock(ctx, ph):
    ph.note("-- Start, then clock at 120 BPM")
    m0 = ctx.sample()
    events = list(merge_events([at(0.0, b"\xFA", "tr", 0xFA)], clock_events([("tempo", 120.0, ctx.args.tempo_hold)], t_start_ns=sec_ns(0.02))))
    samples = []
    sender = run_master(ctx, events, 0.03, collect(samples))
    sender_note(ph, sender)
    first_play = next((s_time(s, sender) for s, _ in samples if s["playing"]), None)
    ph.check("start: playing 1 after the Start", first_play is not None, "%s" % ("%.2f s after the Start" % first_play if first_play is not None else "never"), "within the run")
    lock_clocks = next((n for s, n in samples if s["locked"]), None)
    ph.check("start: locked yes", lock_clocks is not None, "after %s clocks" % lock_clocks if lock_clocks is not None else "never", "<= %d clocks" % LOCK_CLOCKS_MAX)
    if lock_clocks is not None:
        ph.check("start: time to lock in clocks", lock_clocks <= LOCK_CLOCKS_MAX, "%d clocks (%.2f s)" % (lock_clocks, lock_clocks * 60.0 / (24 * 120.0)), "<= %d" % LOCK_CLOCKS_MAX)
    rows = settled(samples, sender, ctx.args.tempo_hold * 0.5)
    mean_err, max_err = tempo_err_stats(rows, 120.0)
    ph.check("start: tempo within 0.1 BPM", mean_err is not None and mean_err <= TEMPO_TOL_BPM, "mean error %.3f, worst %.3f BPM over %d readouts" % (mean_err or 0, max_err or 0, len(rows)), "<= %.2f mean" % TEMPO_TOL_BPM)
    ph.check("start: still locked", bool(rows) and all(s["locked"] for s, _ in rows), "%d of %d readouts locked" % (sum(1 for s, _ in rows if s["locked"]), len(rows)))
    d = deltas(samples[-1][0], m0)
    ph.check("start: Start taken", d["transport_events"] >= 1 and d["ignored"] == 0, "transport events +%d, ignored +%d" % (d["transport_events"], d["ignored"]))
    ph.data["start"] = {"lock_clocks": lock_clocks, "first_play_s": first_play}
    wait_released(ctx)


def f_profile(ctx, ph):
    ph.note("-- tempo profile %s" % ", ".join(str(t) for t in TEMPO_PROFILE))
    table = []
    for bpm in TEMPO_PROFILE:
        if not wait_released(ctx, 8.0):
            ph.note("the unit still followed after 8 s of quiet before %d BPM" % bpm)
        events = list(merge_events([at(0.0, b"\xFA", "tr", 0xFA)], clock_events([("tempo", float(bpm), ctx.args.tempo_hold)], t_start_ns=sec_ns(0.02))))
        samples = []
        sender = run_master(ctx, events, 0.05, collect(samples))
        lock_clocks = next((n for s, n in samples if s["locked"]), None)
        rows = settled(samples, sender, ctx.args.tempo_hold * 0.4)
        mean_err, max_err = tempo_err_stats(rows, float(bpm))
        locked_all = bool(rows) and all(s["locked"] for s, _ in rows)
        ok = lock_clocks is not None and lock_clocks <= LOCK_CLOCKS_MAX and mean_err is not None and mean_err <= TEMPO_TOL_BPM and max_err <= TEMPO_SAMPLE_TOL_BPM and locked_all
        ph.check("profile %d BPM" % bpm, ok, "locked after %s clocks, tempo error mean %.3f worst %.3f BPM, %d/%d readouts locked, lateness max %.0f us" %
                 (lock_clocks, mean_err or 0, max_err or 0, sum(1 for s, _ in rows if s["locked"]), len(rows), sender.lateness_us()[3]),
                 "lock <= %d clocks, mean <= %.2f, worst <= %.2f BPM, all locked" % (LOCK_CLOCKS_MAX, TEMPO_TOL_BPM, TEMPO_SAMPLE_TOL_BPM))
        table.append({"bpm": bpm, "lock_clocks": lock_clocks, "mean_err": mean_err, "max_err": max_err, "locked_all": locked_all})
    ph.data["profile"] = table


def f_jitter(ctx, ph):
    ph.note("-- +-%.0f ms of jitter on every clock at %d BPM" % (JITTER_MS, JITTER_BPM))
    wait_released(ctx)
    secs = ctx.args.jitter_seconds
    events = list(merge_events([at(0.0, b"\xFA", "tr", 0xFA)], clock_events([("tempo", float(JITTER_BPM), secs)], jitter_ns=sec_ns(JITTER_MS / 1000.0), rng=random.Random(2), t_start_ns=sec_ns(0.02))))
    samples = []
    sender = run_master(ctx, events, 0.05, collect(samples))
    lock_i = next((i for i, (s, _) in enumerate(samples) if s["locked"]), None)
    ph.check("jitter: locks", lock_i is not None, "after %s clocks" % (samples[lock_i][1] if lock_i is not None else "never"), "")
    if lock_i is None:
        return
    rows = samples[lock_i:]
    errs = [abs(s["tempo_state_bpm"] - JITTER_BPM) for s, _ in rows]
    unlocked = sum(1 for s, _ in rows if not s["locked"])
    ph.check("jitter: tempo within +-%.1f BPM" % JITTER_TEMPO_TOL_BPM, max(errs) <= JITTER_TEMPO_TOL_BPM, "mean error %.3f, worst %.3f BPM over %d readouts" % (statistics.mean(errs), max(errs), len(errs)), "<= %.1f" % JITTER_TEMPO_TOL_BPM)
    ph.check("jitter: never unlocks", unlocked == 0, "%d readouts unlocked of %d" % (unlocked, len(rows)), "0")
    d = deltas(rows[-1][0], rows[0][0])
    ph.check("jitter: no reacquire, no missing clock", d["reacquires"] == 0 and d["missing"] == 0, "reacquires +%d, missing +%d, dropouts +%d" % (d["reacquires"], d["missing"], d["dropouts"]), "0 after the lock")
    sender_note(ph, sender)


def f_steps(ctx, ph):
    ph.note("-- tempo steps")
    wait_released(ctx)
    hold = ctx.args.tempo_hold
    seq = (100.0, 140.0, 100.0, 174.0, 90.0)
    segs = [("tempo", b, hold) for b in seq]
    events = list(merge_events([at(0.0, b"\xFA", "tr", 0xFA)], clock_events(segs, t_start_ns=sec_ns(0.02))))
    samples = []
    sender = run_master(ctx, events, 0.04, collect(samples))
    re0 = samples[-1][0]["reacquires"] - samples[0][0]["reacquires"]
    for i in range(1, len(seq)):
        step_s = 0.02 + i * hold
        new = seq[i]
        hit = next((s_time(s, sender) - step_s for s, _ in samples if s_time(s, sender) >= step_s and abs(s["tempo_state_bpm"] - new) <= TEMPO_TOL_BPM and s["locked"]), None)
        ph.check("step %.0f -> %.0f BPM" % (seq[i - 1], new), hit is not None and hit <= REACQUIRE_MAX_S, "reacquired in %s" % ("%.2f s (%d clocks)" % (hit, hit * 24 * new / 60) if hit is not None else "never"),
                 "<= %.1f s within %.1f BPM and locked" % (REACQUIRE_MAX_S, TEMPO_TOL_BPM))
    ph.info("reacquires over the steps", "+%d for %d steps" % (re0, len(seq) - 1))


def f_ramp(ctx, ph):
    ph.note("-- a tempo ramp %.0f to %.0f BPM in %.0f s" % (RAMP_FROM_BPM, RAMP_TO_BPM, RAMP_S))
    wait_released(ctx)
    lead, tail = 6.0 * hold_scale(ctx), 8.0 * hold_scale(ctx)
    segs = [("tempo", RAMP_FROM_BPM, lead), ("ramp", RAMP_FROM_BPM, RAMP_TO_BPM, RAMP_S), ("tempo", RAMP_TO_BPM, tail)]
    events = list(merge_events([at(0.0, b"\xFA", "tr", 0xFA)], clock_events(segs, t_start_ns=sec_ns(0.02))))
    samples = []
    sender = run_master(ctx, events, 0.05, collect(samples))

    def commanded(t):
        t -= 0.02 + 0.1  # the master's time, less the readout's delay
        if t <= lead:
            return RAMP_FROM_BPM
        if t >= lead + RAMP_S:
            return RAMP_TO_BPM
        return RAMP_FROM_BPM + (RAMP_TO_BPM - RAMP_FROM_BPM) * (t - lead) / RAMP_S

    rows = [(s, n) for s, n in samples if s_time(s, sender) >= 3.0]
    errs = [abs(s["tempo_state_bpm"] - commanded(s_time(s, sender))) for s, _ in rows]
    ph.check("ramp: tempo follows", statistics.mean(errs) <= RAMP_MEAN_ERR_BPM and max(errs) <= RAMP_MAX_ERR_BPM, "mean |error| %.2f, worst %.2f BPM over %d readouts" % (statistics.mean(errs), max(errs), len(errs)),
             "mean <= %.1f, worst <= %.1f BPM" % (RAMP_MEAN_ERR_BPM, RAMP_MAX_ERR_BPM))
    on_ramp = [s for s, _ in rows if lead + 0.5 <= s_time(s, sender) <= lead + RAMP_S]
    ph.info("locked during the ramp", "%d of %d readouts (the loop's lag may exceed its lock band while the tempo moves)" % (sum(1 for s in on_ramp if s["locked"]), len(on_ramp)))
    last = [s for s, _ in samples[-8:]]
    ph.check("ramp: final tempo and lock", all(abs(s["tempo_state_bpm"] - RAMP_TO_BPM) <= TEMPO_TOL_BPM and s["locked"] for s in last), "last readout %.3f BPM, locked %s" % (last[-1]["tempo_state_bpm"], last[-1]["locked"]),
             "within %.1f BPM, locked" % TEMPO_TOL_BPM)


def f_dropout(ctx, ph):
    ph.note("-- dropouts at 120 BPM")
    wait_released(ctx)
    bpm = 120.0
    period = 60.0 / (24 * bpm)
    short_gap = 12 * period            # 12 missing clocks: under the loop's max_dropout of 24
    long_gap = 3.0
    sc = hold_scale(ctx)
    segs = [("tempo", bpm, 8.0 * sc), ("gap", short_gap), ("tempo", bpm, 6.0 * sc), ("gap", long_gap), ("tempo", bpm, 8.0 * sc)]
    events = list(merge_events([at(0.0, b"\xFA", "tr", 0xFA)], clock_events(segs, t_start_ns=sec_ns(0.02))))
    samples = []
    sender = run_master(ctx, events, 0.03, collect(samples))
    clocks = [a for a, _ in sender.actuals("clk")]
    t_short = next(((clocks[i], clocks[i + 1]) for i in range(len(clocks) - 1) if clocks[i + 1] - clocks[i] > 5 * period * 1e9 and clocks[i + 1] - clocks[i] < 1e9), None)
    t_long = next(((clocks[i], clocks[i + 1]) for i in range(len(clocks) - 1) if clocks[i + 1] - clocks[i] > 2e9), None)
    # (a) the short gap
    if t_short:
        a, b = t_short
        around = [s for s, _ in samples if a - sec_ns(1.0) <= s["t_ns"] <= b + sec_ns(1.5)]
        before = next((s for s, _ in reversed(samples) if s["t_ns"] < a), None)
        after = next((s for s, _ in samples if s["t_ns"] > b + sec_ns(1.5)), None)
        kept = all(s["following"] for s in around)
        ph.check("dropout of 12 clocks: still following", kept and before is not None and after is not None and after["released"] == before["released"],
                 "following in %d of %d readouts, released +%d" % (sum(1 for s in around if s["following"]), len(around), (after["released"] - before["released"]) if before and after else -1), "no release inside the stale window")
        if after is not None:
            ph.check("dropout of 12 clocks: tempo holds", abs(after["tempo_state_bpm"] - bpm) <= TEMPO_TOL_BPM + 0.2, "%.3f BPM 1.5 s after" % after["tempo_state_bpm"], "within %.1f BPM" % (TEMPO_TOL_BPM + 0.2))
            ph.info("clocks the loop counted missing", "+%d" % d32(after["missing"], before["missing"]) if before else "?")
    else:
        ph.check("dropout of 12 clocks", None, "the gap was not found in what was sent")
    # (b) the long gap
    if t_long:
        a, b = t_long
        stale_s = STALE_CLOCKS * period
        window = [s for s, _ in samples if a + sec_ns(0.15) <= s["t_ns"] <= a + sec_ns(stale_s * 0.8)]
        held = bool(window) and all(s["following"] and abs(s["tempo_state_bpm"] - bpm) <= TEMPO_TOL_BPM + 0.2 for s in window)
        ph.check("silence: tempo holds inside the stale window", held, "%d readouts between 0.15 s and %.2f s after the last clock, following and within %.1f BPM" % (len(window), stale_s * 0.8, TEMPO_TOL_BPM + 0.2),
                 "the unit keeps going on the loop's tempo until %d clocks have passed" % STALE_CLOCKS)
        rel = next((s for s, _ in samples if s["t_ns"] > a and not s["following"]), None)
        if rel is None:
            ph.check("silence: the unit releases after %d clocks" % STALE_CLOCKS, False, "still following %.1f s after the last clock" % ((b - a) / 1e9), "")
        else:
            rel_s = (rel["t_ns"] - a) / 1e9
            ph.check("silence: the unit releases after %d clocks" % STALE_CLOCKS, 0.85 * stale_s <= rel_s <= stale_s + RELEASE_SLACK_S,
                     "released %.2f s after the last clock (%d clocks = %.2f s)" % (rel_s, STALE_CLOCKS, stale_s), "between %.2f and %.2f s" % (0.85 * stale_s, stale_s + RELEASE_SLACK_S))
            before = next((s for s, _ in reversed(samples) if s["t_ns"] < a), None)
            ph.check("silence: counted once", before is not None and rel["released"] - before["released"] == 1, "released +%d" % (rel["released"] - before["released"]) if before else "?", "+1")
            ph.info("after the release", "tempo %.3f BPM (the unit's own), playing %s: the engine hands its own tempo back and stops the run (dsp Engine::release_follow)" % (rel["tempo_state_bpm"], rel["playing"]))
        back = next((s for s, _ in samples if s["t_ns"] > b and s["following"]), None)
        ph.check("silence: the master's return is followed again", back is not None, "following %.2f s after the clocks came back" % ((back["t_ns"] - b) / 1e9) if back else "never", "")
    else:
        ph.check("silence of %.0f s" % long_gap, None, "the gap was not found in what was sent")
    wait_released(ctx)


def f_transport(ctx, ph):
    ph.note("-- Stop, Continue and Song Position Pointer at 120 BPM")
    wait_released(ctx)
    bpm = 120.0
    beat_s = 60.0 / bpm
    spp_beats = 8
    sc = hold_scale(ctx)
    plan = [(0.0, b"\xFA"), (6.0 * sc, b"\xFC"), (9.0 * sc, b"\xFB"), (13.0 * sc, b"\xFC"), (16.0 * sc, bytes([0xF2, spp_beats & 0x7F, spp_beats >> 7])), (17.0 * sc, b"\xFB"), (22.0 * sc, b"\xFC")]
    tr = [at(t, d, "tr", d[0]) for t, d in plan]
    events = list(merge_events(tr, clock_events([("tempo", bpm, 24.0 * sc)], t_start_ns=sec_ns(0.02))))
    samples = []
    leds = []

    def on_sample(s, sender, index):
        samples.append((s, index.sent_by(s["t_ns"])))
        t = s_time(s, sender)
        if (17.1 * hold_scale(ctx)) <= t <= (17.9 * hold_scale(ctx)) and len(leds) < 6:
            text = ctx.unit.cmd("leds")
            m = re.search(r"levels=([0-9A-Fa-f]+)", text)
            if m:
                leds.append((round(t, 2), m.group(1)))

    m0 = ctx.sample()
    sender = run_master(ctx, events, 0.03, on_sample)
    tr_times = [(m, a) for a, m in sender.actuals("tr")]
    t_fc1 = tr_times[1][1]
    t_fb1 = tr_times[2][1]
    t_spp = tr_times[4][1]
    stop_s = next(((s["t_ns"] - t_fc1) / 1e9 for s, _ in samples if s["t_ns"] > t_fc1 and not s["playing"]), None)
    ph.check("stop: playing 0 within a beat", stop_s is not None and stop_s <= STOP_WITHIN_BEATS * beat_s, "%s after the Stop" % ("%.0f ms" % (stop_s * 1000) if stop_s is not None else "never"), "<= %.0f ms (one beat)" % (STOP_WITHIN_BEATS * beat_s * 1000))
    cont_s = next(((s["t_ns"] - t_fb1) / 1e9 for s, _ in samples if s["t_ns"] > t_fb1 and s["playing"]), None)
    ph.check("continue: playing 1 within a beat", cont_s is not None and cont_s <= STOP_WITHIN_BEATS * beat_s, "%s after the Continue" % ("%.0f ms" % (cont_s * 1000) if cont_s is not None else "never"), "<= %.0f ms" % (STOP_WITHIN_BEATS * beat_s * 1000))
    # SPP: counted on its own?
    before = next((s for s, _ in reversed(samples) if s["t_ns"] < t_spp), None)
    after = next((s for s, _ in samples if s["t_ns"] > t_spp + sec_ns(0.15)), None)
    if before and after:
        handled = d32(after["transport_events"], before["transport_events"]) >= 1
        ph.info("Song Position Pointer", "%s: the kit %s it (transport events +%d between the SPP and the Continue after it)" %
                ("handled" if handled else "ignored", "takes" if handled else "ignores", d32(after["transport_events"], before["transport_events"])))
    spp_play = next((s for s, _ in samples if s["t_ns"] > tr_times[5][1] + sec_ns(0.5)), None)
    ph.check("spp then continue: playing", spp_play is not None and spp_play["playing"], "playing %s 0.5 s after the Continue" % (spp_play["playing"] if spp_play else "?"), "1")
    ph.data["spp_leds"] = leds
    ph.note("the play position after an SPP is not on the console (`state` has no step); `leds` after the Continue is saved in the report (%d reads): the playhead pad is the unit check" % len(leds))
    d = deltas(samples[-1][0], m0)
    ph.check("transport: every byte taken", d["transport_events"] >= len(plan) and d["ignored"] == 0, "transport events +%d of %d sent, ignored +%d" % (d["transport_events"], len(plan), d["ignored"]), ">= %d, ignored 0" % len(plan))
    wait_released(ctx)


def f_long(ctx, ph):
    secs = ctx.args.long_seconds
    ph.note("-- a long run: %.0f s at 120 BPM with CLOCK OUT on: clocks out against clocks sent" % secs)
    wait_released(ctx)
    rx = ctx.listen()
    time.sleep(0.3)
    mark_c, _ = rx.mark()
    bpm = 120.0
    events = list(merge_events([at(0.0, b"\xFA", "tr", 0xFA)], clock_events([("tempo", bpm, secs)], t_start_ns=sec_ns(0.02))))
    events.append(at(secs + 0.05, b"\xFC", "tr", 0xFC))
    m0 = ctx.sample()
    trace = []
    unlocked = [0]
    next_sample = [0.0]

    def on_sample(s, sender, index):
        t = s_time(s, sender)
        if t >= next_sample[0]:
            next_sample[0] = t + max(2.0, secs / 60.0)
            out = d32(s["clock_out"], m0["clock_out"])
            sent_n = index.sent_by(s["t_ns"])
            trace.append((round(t, 1), sent_n, out, sent_n - out, s["locked"], round(s["tempo_state_bpm"], 3)))
        if t > 20.0 and not s["locked"]:
            unlocked[0] += 1

    sender = run_master(ctx, events, 1.0, on_sample)
    time.sleep(0.6)
    m1 = ctx.sample()
    d = deltas(m1, m0)
    sent_n = sender.count("clk")
    out_n = d["clock_out"]
    rx_n = len(rx.clocks) - mark_c
    sender_note(ph, sender)
    ph.check("long run: clocks out against clocks sent", abs(out_n - sent_n) <= SLIP_TOL, "sent %d, the unit counted %d out (diff %+d), the computer read %d" % (sent_n, out_n, out_n - sent_n, rx_n),
             "|diff| <= %d at the end: no slip" % SLIP_TOL)
    ph.check("long run: what the unit sent is what we read", abs(rx_n - out_n) <= SLIP_TOL and d["usb_discarded"] == 0, "read %d, counted %d, discarded +%d" % (rx_n, out_n, d["usb_discarded"]), "|diff| <= %d, discarded 0" % SLIP_TOL)
    mid = [abs(row[3]) for row in trace[1:]]
    ph.check("long run: no slip while running", not mid or max(mid) <= SLIP_MID_TOL, "worst |sent - out| %s over %d samples" % (max(mid) if mid else "n/a", len(trace)), "<= %d (a sample is a console round trip from the count)" % SLIP_MID_TOL)
    ph.check("long run: stays locked", unlocked[0] == 0, "%d unlocked readouts after the first 20 s" % unlocked[0], "0")
    ph.check("long run: no reacquire after the start", d["reacquires"] <= 1, "reacquires +%d" % d["reacquires"], "<= 1 (the Start)")
    sent_abs = [a for a, _ in sender.actuals("clk")]
    off = offset_drift(sent_abs, list(rx.clocks)[mark_c:], 60e9 / (24 * bpm))
    if off is None:
        ph.check("long run: phase offset", None, "too few clocks read (%d)" % rx_n)
    else:
        ph.info("phase offset, unit's clock out minus ours", "median %.2f ms (pairing shift %+d), scatter %.3f ms rms, worst %.2f ms from the line" % (off["median_ms"], off["shift"], off["stdev_ms"], off["max_dev_ms"]))
        ph.check("long run: phase offset drift", abs(off["drift_ms"]) <= DRIFT_MAX_MS, "%+.3f ms over the run (%+.2f ppm)" % (off["drift_ms"], off["slope_ppm"]), "|drift| <= %.1f ms" % DRIFT_MAX_MS)
        ph.check("long run: phase offset scatter", off["stdev_ms"] <= OFFSET_STDEV_MAX_MS and off["max_dev_ms"] < 0.5 * 60e3 / (24 * bpm), "%.3f ms rms, worst %.2f ms" % (off["stdev_ms"], off["max_dev_ms"]),
                 "rms <= %.1f ms and worst < half a period (a slip is a whole one)" % OFFSET_STDEV_MAX_MS)
    ph.data["long_trace"] = trace
    ph.data["long"] = {"sent": sent_n, "out": out_n, "read": rx_n, "offset": off}
    wait_released(ctx)


def phase_follow(ctx):
    ph = Phase("follow")
    follow_prepare(ctx, ph)
    ph.info("clock sender", "perf_counter_ns, thread at %s, last %.1f ms busy-waited" % ("SCHED_RR if allowed" if hasattr(os, "sched_setscheduler") else "normal", SPIN_NS / 1e6))
    tests = [("start", f_start_lock), ("profile", f_profile), ("jitter", f_jitter), ("steps", f_steps), ("ramp", f_ramp), ("dropout", f_dropout), ("transport", f_transport), ("long", f_long)]
    wanted = ctx.args.follow_tests.split(",") if ctx.args.follow_tests else [n for n, _ in tests]
    for name, fn in tests:
        if name in wanted:
            try:
                fn(ctx, ph)
            except (UnitError, AudioError) as error:
                if isinstance(error, Fatal):
                    raise
                ph.check("follow %s ran" % name, False, str(error))
                ctx.stop_senders()
    m = ctx.unit.midi()
    ph.data["end"] = {k: m[k] for k in ("dropped", "dropouts", "missing", "reacquires", "released", "din_errors", "usb_in_full") if k in m}
    ph.data["cpu"] = ctx.unit.cpu()
    ph.check("follow: dropped 0", m["dropped"] == 0, "dropped %d" % m["dropped"], "0")
    return ph


# ---- phase: clockout (the unit is the master) ------------------------------------------------------------------------------------------------

def phase_clockout(ctx):
    ph = Phase("clockout")
    ctx.panel.config_close()
    ctx.panel.set_transport(False)
    want = {"follow_on": False, "clock_out": True, "transport_out": True}
    if "usb" not in ctx.panel.read_cfg()["ports"]:
        want["ports"] = "din+usb"
    ctx.panel.configure(**want)
    cfg = ctx.panel.read_cfg()
    ph.info("set up", "FOLLOW CLOCK OFF, CLOCK OUT ON, TRANSPORT OUT ON, OUT PORTS %s; each tempo for %.0f s" % (cfg["ports"].upper(), ctx.args.clockout_seconds))
    ph.note("the 1 ms USB frame is the floor: a clock leaves the unit inside a frame, so intervals quantise by +-1 ms (0.41 ms rms at best); the mean over a minute is not affected")
    for bpm in CLOCKOUT_BPMS:
        try:
            clockout_one(ctx, ph, bpm)
        except (UnitError, AudioError) as error:
            if isinstance(error, Fatal):
                raise
            ph.check("clock out at %g BPM ran" % bpm, False, str(error))
            ctx.panel.set_transport(False)
    ctx.unlisten()
    ph.data["cpu"] = ctx.unit.cpu()
    return ph


def clockout_one(ctx, ph, bpm):
    secs = ctx.args.clockout_seconds
    tag = "%g BPM" % bpm
    ctx.panel.set_transport(False)
    try:
        ctx.panel.set_own_tempo(int(round(bpm * 1000)))
    except UnitError as error:
        ph.note("the tempo %s could not be set exactly (%s): the run is measured against the unit's own readout" % (tag, error))
    actual = ctx.unit.tempo_mbpm() / 1000.0
    ph.check("%s: unit tempo set" % tag, abs(actual - bpm) < 0.0005, "%.3f BPM on the MASTER screen's readout" % actual, "exactly %.3f" % bpm)
    rx = ctx.listen()
    time.sleep(0.5)
    mc, me = rx.mark()
    m0 = ctx.unit.midi()
    t_play = pc_ns()
    ctx.panel.key("play", 0.0)
    time.sleep(secs)  # no console traffic in the window: the CDC endpoint shares the bus with the MIDI one
    t_stop = pc_ns()
    ctx.panel.key("play", 0.0)
    time.sleep(1.2)
    m1 = ctx.unit.midi()
    clocks = list(rx.clocks)[mc:]
    places = list(rx.clock_pos)[mc:]
    events = list(rx.events)[me:]
    st = ctx.unit.state()
    ph.check("%s: stopped again" % tag, st.get("playing") == "0", "playing %s" % st.get("playing"), "0")
    n = len(clocks)
    if n < 100:
        ph.check("%s: clocks arrived" % tag, False, "%d clocks in %.0f s" % (n, secs), ">= 100")
        return
    a = analyze_clock_out(clocks, actual, expected_span_s=None)
    span = a["span_s"]
    a2 = analyze_clock_out(clocks, actual, expected_span_s=span)
    limit = a["err_limit_pct"]
    note = "" if limit <= CLOCKOUT_ERR_PCT + 1e-9 else ", widened from %.3f by the %.0f ms edges of a %.0f s span" % (CLOCKOUT_ERR_PCT, EDGE_UNCERT_S * 1000, span)
    ph.check("%s: mean BPM error" % tag, abs(a["err_pct"]) <= limit, "%.3f BPM mean over %d clocks in %.1f s, error %+.4f %%" % (a["mean_bpm"], n, span, a["err_pct"]), "<= %.3f %%%s" % (limit, note))
    ph.check("%s: interval jitter" % tag, a["interval_stdev_ms"] <= INTERVAL_STDEV_MS, "stdev %.3f ms (period %.2f ms)" % (a["interval_stdev_ms"], 60e3 / (24 * actual)), "<= %.1f ms; the 1 ms USB floor is 0.41 ms" % INTERVAL_STDEV_MS)
    ph.check("%s: worst interval" % tag, a["interval_dev_max_ms"] <= INTERVAL_MAX_DEV_MS, "%.3f ms from the period" % a["interval_dev_max_ms"], "<= %.1f ms" % INTERVAL_MAX_DEV_MS)
    ph.check("%s: no missing clock, no extra" % tag, a["missing"] == 0 and a["extra"] == 0, "missing %d, extra %d" % (a["missing"], a["extra"]), "0 and 0 (an interval over %.1f periods is missing, under 0.5 extra)" % INTERVAL_MISSING_FACTOR)
    if a["host_stalls"]:
        ph.info("%s: host read stalls" % tag, "%d (a late stamp followed by early ones that make up for it: this computer's read waited, no clock was lost; the jitter figures include them)" % a["host_stalls"])
    ph.check("%s: count against the expected" % tag, abs(a2["count_diff"] - 1.0) <= COUNT_TOL, "%d clocks, %.2f expected for a %.2f s span at %.3f BPM" % (n, a2["expected"], span, actual), "+-%d" % COUNT_TOL)
    d = deltas(m1, m0)
    ph.check("%s: read what the unit counted" % tag, abs(d["clock_out"] - n) <= SLIP_TOL, "unit clock-out +%d, read %d" % (d["clock_out"], n), "+-%d (the Stop's last clock may fall on either side)" % SLIP_TOL)
    ph.check("%s: no clock lost on the unit's side" % tag, d["usb_discarded"] == 0 and d["usb_no_time"] == 0 and d["dropped"] == 0, "usb discarded +%d, no-time +%d, dropped +%d" % (d["usb_discarded"], d["usb_no_time"], d["dropped"]), "0")
    fa = [e for e in events if e[1] == 0xFA]
    fc = [e for e in events if e[1] == 0xFC]
    first_place = places[0]
    ph.check("%s: Start (FA) on Play, before the first clock" % tag, len(fa) == 1 and fa[0][2] < first_place, "%d FA, first clock %s" % (len(fa), "after it" if fa and fa[0][2] < first_place else "not after it"), "exactly one, ahead of the clocks")
    if fa:
        ph.info("%s: Play key to FA" % tag, "%.0f ms (the key's console round trip is in it)" % ((fa[0][0] - t_play) / 1e6))
    ph.check("%s: Stop (FC) on Stop" % tag, len(fc) == 1, "%d FC, %.0f ms after the key" % (len(fc), (fc[0][0] - t_stop) / 1e6) if fc else "0 FC", "exactly one")
    if fc:
        after_fc = sum(1 for p in places if p > fc[0][2])
        ph.check("%s: no clock after the Stop" % tag, after_fc <= 1, "%d clocks after FC" % after_fc, "<= 1")
    ph.check("%s: start-stop counted" % tag, d["start_stop_out"] >= 2, "start-stop-out +%d" % d["start_stop_out"], ">= 2 (FA and FC)")
    ph.data["clockout_%g" % bpm] = {k: a[k] for k in ("count", "span_s", "mean_bpm", "err_pct", "missing", "extra", "host_stalls", "interval_stdev_ms", "interval_dev_max_ms", "grid_dev_max_ms")}
    ctx.unlisten()


# ---- phase: learn ---------------------------------------------------------------------------------------------------------------------------

LEARN_CC = 74
LEARN_NOTE = 36
LEARN_HOLD_S = 0.9
LEARN_GAP_S = 1.2
LEARN_SPREAD_STEP_S = 0.008


def send_now(ctx, events, wait=120.0):
    sender = Sender(ctx.node().write, events)
    ctx.senders.append(sender)
    sender.go()
    sender.join(wait)
    if sender in ctx.senders:
        ctx.senders.remove(sender)
    if sender.error is not None:
        raise UnitError("writing to the MIDI node failed: %s" % sender.error)
    return sender


def cc_sweep(ch, cc, a, b, step_s=LEARN_SPREAD_STEP_S, t0=0.0):
    """A controller moving from value a to value b one step at a time."""
    n = abs(b - a)
    sign = 1 if b >= a else -1
    return [at(t0 + i * step_s, (0xB0 | (ch - 1), cc, a + sign * i), "cc", cc) for i in range(n + 1)]


def priming_sweep(ctx, ch):
    """Learn is PICKUP: the controller does nothing until it passes the parameter's own value, so it sweeps the whole range up and down first."""
    ev = cc_sweep(ch, LEARN_CC, 0, 127) + cc_sweep(ch, LEARN_CC, 127, 0, t0=128 * LEARN_SPREAD_STEP_S)
    send_now(ctx, ev)
    time.sleep(0.3)


def plateau_run(ctx, ch, values):
    """For each value: the controller there, then a held note; the brightness (HF over LF, dB) of each note's body. Returns the list or None when nothing sounded."""
    ev = []
    t = 0.3
    for v in values:
        ev.append(at(t, (0xB0 | (ch - 1), LEARN_CC, v), "cc", v))
        t += 0.2
        ev.append(at(t, (0x90 | (ch - 1), LEARN_NOTE, 110), "on", LEARN_NOTE))
        t += LEARN_HOLD_S
        ev.append(at(t, (0x80 | (ch - 1), LEARN_NOTE, 64), "off", LEARN_NOTE))
        t += LEARN_GAP_S
    stream, sender = audio_run(ctx, ev, 0.8)
    if not stream.ok:
        return None
    out = []
    for t_on, _ in sender.actuals("on"):
        i = stream.idx(t_on + sec_ns(0.25))
        out.append(brightness_db(stream.mono[i:i + 8192], stream.fs))
    return out


def after_reset(ctx):
    """`reset`, the proof again that it is the expected firmware, and the node and the audio device found again (USB enumerates again)."""
    ctx.stop_senders()
    ctx.unlisten()
    took = ctx.unit.reset_and_wait(ctx.expect_version)
    ctx.node().close()
    ctx.tx.open(60.0)
    ctx.audio_device = None
    ctx.audio_note = None
    ctx.panel.cursor = None
    time.sleep(2.5)
    return took


def fmt_list(values):
    return ", ".join("%.1f" % v for v in values)


def phase_learn(ctx):
    ph = Phase("learn")
    if ctx.args.learn_baseline != "none":
        ph.check("learn: baseline declared", None, "not run: the console shows no binding count, and CLEAR LEARN erases every binding, so the phase runs only with --learn-baseline none (the maintainer says there is none)")
        return ph
    if ctx.audio() is None or not ctx.audio_layout_ok():
        ph.check("learn: audio to see the parameter move", None, "not run: %s (the console has no readout of a parameter's value or of the bindings: audio is the only witness)" % (ctx.audio_note or "no audio"))
        return ph
    ctx.panel.config_close()
    ctx.panel.set_transport(False)
    ch = user_channel(ctx)
    ctx.panel.goto_screen("INSTRUMENT_VOICE")
    ph.info("what is learnt", "INSTRUMENT VOICE, Shift + F1 (CUTOFF), controller CC %d on channel %d, note %d held %.1f s per plateau" % (LEARN_CC, ch, LEARN_NOTE, LEARN_HOLD_S))
    ph.note("learn is PICKUP: a sweep 0-127-0 comes first; the witness is the notes' brightness (energy above 1.5 kHz over the energy below it)")
    values = [127, 0, 64, 127, 0]

    # 1. no binding yet: the controller must change nothing
    control = plateau_run(ctx, ch, values)
    if control is None:
        ph.check("learn: the voice sounds", False, "no sound found on channels 7-8")
        return ph
    spread = max(control) - min(control)
    ph.check("learn: control, no binding", spread <= BRIGHT_CONTROL_MAX_DB, "brightness at CC %s: %s dB, spread %.1f" % (values, fmt_list(control), spread), "spread <= %.0f dB: the same notes sound the same" % BRIGHT_CONTROL_MAX_DB)

    # 2. arm Shift + F1, send a CC
    m0 = ctx.unit.midi()
    try:
        ctx.panel.key("shift down", 0.25)
        ctx.panel.key("f1", 0.4)
    finally:
        ctx.panel.key("shift up", 0.25)
    ctx.learn_bound = True
    ctx.patch_dirty = True
    send_now(ctx, [at(0.0, (0xB0 | (ch - 1), LEARN_CC, 64), "cc", LEARN_CC)])
    time.sleep(0.4)
    m1 = ctx.unit.midi()
    d = deltas(m1, m0)
    ph.check("learn: the CC arrived", d["messages"] == 1 and d["dropped"] == 0, "messages +%d, dropped +%d, last rx %s" % (d["messages"], d["dropped"], " ".join(m1["last_rx"][-3:])), "+1, 0")
    ph.note("the binding itself has no console readout (the toast LEARN CUTOFF: MOVE A KNOB and the strip are on the screen only): it is proved by the audio below")

    # 3. bound: the sweep, then the plateaus
    priming_sweep(ctx, ch)
    bound = plateau_run(ctx, ch, values)
    if bound is None:
        ph.check("learn: bound, the voice sounds", False, "no sound found")
        return ph
    hi1, lo1, mid, hi2, lo2 = bound
    ph.check("learn: CC 127 brighter than CC 0", hi1 - lo1 >= BRIGHT_DELTA_MIN_DB and hi2 - lo2 >= BRIGHT_DELTA_MIN_DB, "%s dB: 127 over 0 by %+.1f and %+.1f" % (fmt_list(bound), hi1 - lo1, hi2 - lo2), ">= %.0f dB (cutoff up with the controller)" % BRIGHT_DELTA_MIN_DB)
    ph.check("learn: CC 64 in between", lo1 + BRIGHT_MONO_MARGIN_DB <= mid <= hi1 - BRIGHT_MONO_MARGIN_DB, "0: %.1f, 64: %.1f, 127: %.1f dB" % (lo1, mid, hi1), "between, %.0f dB clear of each end" % BRIGHT_MONO_MARGIN_DB)
    ph.check("learn: repeatable", abs(hi1 - hi2) <= BRIGHT_CONTROL_MAX_DB and abs(lo1 - lo2) <= BRIGHT_CONTROL_MAX_DB, "127: %.1f vs %.1f, 0: %.1f vs %.1f dB" % (hi1, hi2, lo1, lo2), "<= %.0f dB apart" % BRIGHT_CONTROL_MAX_DB)
    ph.data["bright"] = {"control": control, "bound": bound}

    # 4. reset: the binding is in the settings file
    time.sleep(3.5)  # the kit saves the bindings a moment after the change
    took = after_reset(ctx)
    ph.info("reset", "the unit was back in %.0f s" % took)
    priming_sweep(ctx, ch)
    kept = plateau_run(ctx, ch, [127, 0])
    if kept is None:
        ph.check("learn: after reset the voice sounds", False, "no sound found")
    else:
        ph.check("learn: the binding survives `reset`", kept[0] - kept[1] >= BRIGHT_DELTA_MIN_DB, "%s dB: 127 over 0 by %+.1f" % (fmt_list(kept), kept[0] - kept[1]), ">= %.0f dB" % BRIGHT_DELTA_MIN_DB)

    # 5. CLEAR LEARN
    ctx.panel.clear_learn()
    ctx.learn_bound = False
    priming_sweep(ctx, ch)
    cleared = plateau_run(ctx, ch, [127, 0, 127])
    if cleared is None:
        ph.check("learn: after CLEAR LEARN the voice sounds", False, "no sound found")
    else:
        spread = max(cleared) - min(cleared)
        ph.check("learn: CLEAR LEARN frees the controller", spread <= BRIGHT_CONTROL_MAX_DB, "%s dB, spread %.1f" % (fmt_list(cleared), spread), "<= %.0f dB" % BRIGHT_CONTROL_MAX_DB)

    # 6. the patch the controller moved is the unit's RAM only (AUTOSAVE is off): a reset gives it back
    after_reset(ctx)
    ctx.patch_dirty = False
    ph.info("end", "reset again so that the cutoff the controller left is the saved one; no binding left (CLEAR LEARN)")
    return ph


# ---- phase: stress --------------------------------------------------------------------------------------------------------------------------

CPU_ZERO_KEYS = ("underrun", "mute_events", "late", "slips", "hwfault", "over_budget")
FLOOD_CCS = (1, 2, 7, 10, 11, 16, 71, 74, 91, 93)   # none of 120-127 (channel mode) and none the app maps: values change, nothing sounds


def snapshot(ctx):
    return ctx.unit.midi(), ctx.unit.cpu(), ctx.node().bytes_written


def silent_probe(ctx, wait_s, ph, name):
    """The voice is silent `wait_s` after the end of a part (no stuck note, no runaway tail): 1.5 s of the master."""
    time.sleep(wait_s)
    if ctx.audio() is None or not ctx.audio_layout_ok():
        ph.check(name, None, "not run: %s" % (ctx.audio_note or "no audio"))
        return
    cap = ctx.capture()
    with cap:
        time.sleep(1.5)
    level = rms_db(cap.mono(), int(0.3 * cap.rate))
    ph.check(name, level < SILENCE_DB, "%.1f dBFS" % level, "< %.0f dBFS" % SILENCE_DB)


def stress_verdict(ctx, ph, label, before, after, sent_msgs, check_bytes=True, allow_drops=False):
    """The common numbers of a stress part: counters, wire bytes, the CPU counters, the crash record."""
    m0, c0, w0 = before
    m1, c1, w1 = after
    d = deltas(m1, m0)
    if sent_msgs is not None:
        ph.check("%s: every message counted" % label, d["messages"] == sent_msgs, "messages +%d of %d sent" % (d["messages"], sent_msgs), "equal")
    if allow_drops:
        ph.info("%s: dropped (counted, by design)" % label, "dropped +%d, polled-dropped +%d: a full ring (the app's poll ring is 64 deep, the note ring 32) drops the newest and counts it; the kit then sends an all-notes-off, so the pass is `counted` and `silent`" % (d["dropped"], d["polled_dropped"]))
    else:
        ph.check("%s: dropped 0" % label, d["dropped"] == 0 and d["polled_dropped"] == 0, "dropped +%d, polled-dropped +%d" % (d["dropped"], d["polled_dropped"]), "0")
    if check_bytes:
        ph.check("%s: every byte arrived" % label, abs(d["usb_bytes_in"] - (w1 - w0)) <= WIRE_BYTES_TOL, "usb bytes-in +%d of %d written" % (d["usb_bytes_in"], w1 - w0), "equal (every byte has a USB packet)")
    ph.check("%s: no overrun" % label, d["usb_in_full"] == 0 and d["usb_discarded"] == 0 and d["usb_no_time"] == 0 and d["din_overruns"] == 0 and d["din_errors"] == 0,
             "usb in-full +%d, discarded +%d, no-time +%d, din overruns +%d, errors +%d" % (d["usb_in_full"], d["usb_discarded"], d["usb_no_time"], d["din_overruns"], d["din_errors"]), "all 0")
    bad = {k: d32(c1.get(k, 0), c0.get(k, 0)) for k in CPU_ZERO_KEYS}
    ph.check("%s: cpu counters" % label, not any(bad.values()) and c1.get("render_max_pct", 0.0) <= RENDER_MAX_PCT, ", ".join("%s +%d" % kv for kv in bad.items()) + ", render max %.1f %%" % c1.get("render_max_pct", 0.0),
             "counters 0, render max <= %.0f %%" % RENDER_MAX_PCT)
    crash = ctx.unit.crash()
    ph.check("%s: crash record" % label, crash_clean(ctx, crash), crash.splitlines()[0] if crash else "(empty)", "no crash record (or the one found at the start, unchanged)")
    return d


def s_flood(ctx, ph):
    ph.note("-- S1: control change flood, %d a second for %.0f s on %d controllers and 4 channels" % (FLOOD_RATE, ctx.args.flood_seconds, len(FLOOD_CCS)))
    wait_released(ctx)
    n = int(FLOOD_RATE * ctx.args.flood_seconds)
    period = 1.0 / FLOOD_RATE
    ev = (at(k * period, (0xB0 | (k % 4), FLOOD_CCS[k % len(FLOOD_CCS)], (k * 7) & 0x7F), "cc", k & 0x7F) for k in range(n))
    before = snapshot(ctx)
    sender = send_now(ctx, ev, ctx.args.flood_seconds + 60)
    time.sleep(0.4)
    after = snapshot(ctx)
    sender_note(ph, sender, "flood sender")
    ph.check("flood: all sent", sender.count("cc") == n, "%d of %d" % (sender.count("cc"), n))
    stress_verdict(ctx, ph, "flood", before, after, n)
    silent_probe(ctx, 0.5, ph, "flood: voice silent at the end")


def s_storm(ctx, ph):
    ph.note("-- S2: note storms, %d notes in %.0f s" % (STORM_NOTES, STORM_S))
    ch = user_channel(ctx)
    rng = random.Random(11)
    dt = STORM_S / STORM_NOTES
    # (a) on and off in pairs, half a millisecond apart
    ev = []
    for k in range(STORM_NOTES):
        n = rng.randint(24, 96)
        ev.append(at(k * dt, (0x90 | (ch - 1), n, rng.randint(1, 127)), "on", n))
        ev.append(at(k * dt + dt / 2, (0x80 | (ch - 1), n, 64), "off", n))
    before = snapshot(ctx)
    sender = send_now(ctx, ev, 60)
    time.sleep(0.4)
    after = snapshot(ctx)
    sender_note(ph, sender, "storm sender")
    stress_verdict(ctx, ph, "storm pairs", before, after, 2 * STORM_NOTES, allow_drops=True)
    silent_probe(ctx, SILENCE_WAIT_S, ph, "storm pairs: voice silent after")
    # (b) all the note-ons first, the note-offs a second later in shuffled order: more notes held than the kit's list holds (32)
    notes = [rng.randint(24, 96) for _ in range(STORM_NOTES)]
    ev = [at(k * dt, (0x90 | (ch - 1), n, rng.randint(1, 127)), "on", n) for k, n in enumerate(notes)]
    order = list(notes)
    rng.shuffle(order)
    t_off = STORM_S + 1.0
    ev += [at(t_off + k * dt, (0x80 | (ch - 1), n, 64), "off", n) for k, n in enumerate(order)]
    before = snapshot(ctx)
    sender = send_now(ctx, ev, 60)
    time.sleep(0.4)
    after = snapshot(ctx)
    stress_verdict(ctx, ph, "storm held", before, after, 2 * STORM_NOTES, allow_drops=True)
    silent_probe(ctx, SILENCE_WAIT_S, ph, "storm held: no stuck note (every note-off was sent)")


def sysex_blob(rng, size):
    return bytes([0xF0]) + bytes(rng.randint(0, 127) for _ in range(size - 2)) + bytes([0xF7])


def s_sysex(ctx, ph):
    ph.note("-- S3: %d SysEx blobs of %d bytes in 16-byte writes, 2 ms apart, with clocks at 120 BPM and notes between the writes" % (SYSEX_COUNT, SYSEX_BYTES))
    follow_prepare(ctx, ph)
    wait_released(ctx)
    ch = user_channel(ctx)
    rng = random.Random(5)
    chunk, chunk_dt, blob_gap = 16, 0.002, 1.0
    lead = 4.0
    ev = []
    blobs = []
    for b in range(SYSEX_COUNT):
        data = sysex_blob(rng, SYSEX_BYTES)
        t = lead + b * (blob_gap + (SYSEX_BYTES / chunk) * chunk_dt)
        t0 = t
        for i in range(0, len(data), chunk):
            ev.append(at(t, data[i:i + chunk], "sx", b))
            t += chunk_dt
        blobs.append((t0, t))
    total = blobs[-1][1] + 2.0
    notes = []
    k = 0
    tn = lead + 0.3
    while tn < total - 1.0:
        # a channel message may not come inside a SysEx (only a realtime byte may: a status byte ends it): the notes go between the blobs
        if not any(b0 - 0.05 <= tn <= b1 + 0.05 or b0 - 0.05 <= tn + 0.15 <= b1 + 0.05 for b0, b1 in blobs):
            n = 36 + (k * 5) % 24
            notes.append(at(tn, (0x90 | (ch - 1), n, 100), "on", n))
            notes.append(at(tn + 0.15, (0x80 | (ch - 1), n, 64), "off", n))
            k += 1
        tn += 0.5
    clocks = clock_events([("tempo", 120.0, total)], t_start_ns=sec_ns(0.02))
    events = list(merge_events([at(0.0, b"\xFA", "tr", 0xFA)], clocks, sorted(ev + notes, key=lambda e: e[0])))
    msgs = len(notes)
    samples = []
    before = snapshot(ctx)
    sender = run_master(ctx, events, 0.5, collect(samples))
    time.sleep(0.4)
    after = snapshot(ctx)
    sender_note(ph, sender, "sender")
    sent_clk = [a for a, _ in sender.actuals("clk")]
    inside = sum(1 for a in sent_clk if any(sender.t0 + sec_ns(b0) <= a <= sender.t0 + sec_ns(b1) for b0, b1 in blobs))
    ph.info("clocks written inside a SysEx blob", "%d of %d (a realtime byte between two writes of a running message; ALSA gives it its own USB packet)" % (inside, len(sent_clk)))
    d = stress_verdict(ctx, ph, "sysex", before, after, msgs + 0)
    lock_i = next((i for i, (s, _) in enumerate(samples) if s["locked"]), None)
    rows = samples[lock_i:] if lock_i is not None else []
    ph.check("sysex: the clock stays locked", bool(rows) and all(s["locked"] for s, _ in rows), "%d of %d readouts locked after the lock" % (sum(1 for s, _ in rows if s["locked"]), len(rows)), "all")
    if rows:
        errs = [abs(s["tempo_state_bpm"] - 120.0) for s, _ in rows]
        ph.check("sysex: tempo holds", max(errs) <= TEMPO_SAMPLE_TOL_BPM + 0.2, "worst error %.3f BPM" % max(errs), "<= %.1f" % (TEMPO_SAMPLE_TOL_BPM + 0.2))
        r = rows[-1][0]["reacquires"] - rows[0][0]["reacquires"]
        ph.check("sysex: no reacquire, no missing clock", r == 0 and d["missing"] == 0, "reacquires +%d, missing +%d" % (r, d["missing"]), "0 after the lock")
    wait_released(ctx)
    silent_probe(ctx, SILENCE_WAIT_S, ph, "sysex: voice silent after")


def s_realtime_in_messages(ctx, ph):
    ph.note("-- S4: a clock byte in the middle of 1,000 running control changes (B0 F8 cc vv), 10 ms apart")
    follow_prepare(ctx, ph)
    wait_released(ctx)
    ch = user_channel(ctx)
    n = 1000
    ev = [at(0.0, b"\xFA", "tr", 0xFA)]
    ev += [at(0.05 + k * 0.01, bytes([0xB0 | (ch - 1), 0xF8, FLOOD_CCS[k % len(FLOOD_CCS)], (k * 5) & 0x7F]), "cc", k & 0x7F) for k in range(n)]
    ev.append(at(0.05 + n * 0.01 + 0.3, b"\xFC", "tr", 0xFC))
    samples = []
    before = snapshot(ctx)
    run_master(ctx, ev, 0.5, collect(samples))
    time.sleep(0.4)
    after = snapshot(ctx)
    d = stress_verdict(ctx, ph, "clock in message", before, after, n)
    ph.check("clock in message: the clocks counted", abs(d["loop_clocks"] - n) <= 4 or abs(d["in_clocks"] - n) <= 4, "loop clocks +%d, in-clocks +%d of %d sent inside the messages" % (d["loop_clocks"], d["in_clocks"], n),
             "+-4 of %d (every F8 is a clock: the 10 ms spacing is 250 BPM)" % n)
    rows = [s for s, _ in samples if s["locked"]]
    ph.check("clock in message: locked at 250 BPM", bool(rows) and abs(statistics.median(s["tempo_state_bpm"] for s in rows) - 250.0) <= 0.5, "%d locked readouts, median %.2f BPM" % (len(rows), statistics.median(s["tempo_state_bpm"] for s in rows) if rows else 0.0), "250 +-0.5 BPM")
    wait_released(ctx)
    silent_probe(ctx, 1.0, ph, "clock in message: voice silent after")


def s_channels(ctx, ph):
    ph.note("-- S5: other channels are ignored when IN CHANNEL is not OMNI")
    ctx.panel.configure(channel=3)
    ctx.panel.set_transport(False)
    ph.info("IN CHANNEL", "3")
    hold, gap = 0.4, 0.8
    others = [c for c in range(1, 17) if c != 3]
    ev = []
    t = 0.5
    for c in others:
        ev.append(at(t, (0x90 | (c - 1), 48, 100), "on", c))
        ev.append(at(t + hold, (0x80 | (c - 1), 48, 64), "off", c))
        t += hold + gap
    ev.append(at(t, (0x90 | 2, 48, 100), "on", 3))
    ev.append(at(t + hold, (0x80 | 2, 48, 64), "off", 3))
    m0 = ctx.unit.midi()
    if ctx.audio() is not None and ctx.audio_layout_ok():
        # the capture's alignment is the first onset: a note on channel 3 comes first so that the others can be measured against it
        ev = [at(0.5, (0x90 | 2, 48, 100), "on", 3), at(0.5 + hold, (0x80 | 2, 48, 64), "off", 3)] + [(e[0] + sec_ns(hold + SILENCE_WAIT_S), e[1], e[2], e[3]) for e in ev if e[3] != 3]
        stream, sender = audio_run(ctx, ev, 1.0)
        if not stream.ok:
            ph.check("channels: channel 3 sounds", False, "no sound found")
        else:
            ons = sender.actuals("on")
            offs = sender.actuals("off")
            levels = [stream.rms(a + sec_ns(0.1), b - sec_ns(0.05)) for (a, _), (b, _) in zip(ons, offs)]
            ph.check("channels: the note on channel 3 sounds", levels[0] >= SOUND_MIN_DB, "%.1f dBFS" % levels[0], ">= %.0f dBFS" % SOUND_MIN_DB)
            worst = max(levels[1:])
            ph.check("channels: 15 other channels are silent", worst < SILENCE_DB, "loudest of %d other-channel notes %.1f dBFS" % (len(levels) - 1, worst), "< %.0f dBFS" % SILENCE_DB)
    else:
        send_now(ctx, ev, 60)
        ph.check("channels: the audio check", None, "not run: %s" % (ctx.audio_note or "no audio"))
    d = deltas(ctx.unit.midi(), m0)
    ph.info("counters over the 16 notes", "messages +%d, notes +%d (notes counts what the engine took; the filter is the kit's)" % (d["messages"], d["notes"]))
    ph.check("channels: nothing dropped", d["dropped"] == 0, "dropped +%d" % d["dropped"], "0")


def phase_stress(ctx):
    ph = Phase("stress")
    ctx.panel.config_close()
    ctx.panel.set_transport(False)
    ph.info("set up", "the Config found, except where a part says; the parts of the clock run with FOLLOW CLOCK on USB")
    for name, fn in (("flood", s_flood), ("storm", s_storm), ("sysex", s_sysex), ("clock in message", s_realtime_in_messages), ("channels", s_channels)):
        try:
            fn(ctx, ph)
        except (UnitError, AudioError) as error:
            if isinstance(error, Fatal):
                raise
            ph.check("stress %s ran" % name, False, str(error))
            ctx.stop_senders()
    ph.data["cpu"] = ctx.unit.cpu()
    return ph


# ---- phase: soak ----------------------------------------------------------------------------------------------------------------------------

SOAK_POLL_S = 5.0
SOAK_STEP_NOTES = (36, 36, 48, 36, 39, 36, 43, 36, 36, 48, 36, 51, 36, 43, 36, 39)


def soak_pattern(ch, secs, bpm):
    """16th notes, accent every fourth: a bass pattern as long as the run."""
    p = 60.0 / (bpm * 4)
    for k in range(int(secs / p)):
        n = SOAK_STEP_NOTES[k % len(SOAK_STEP_NOTES)]
        t = 0.1 + k * p
        yield at(t, (0x90 | (ch - 1), n, 127 if k % 4 == 0 else 90), "on", n)
        yield at(t + 0.6 * p, (0x80 | (ch - 1), n, 64), "off", n)


def soak_sweep(ch, secs, period_s=60.0, rate_hz=20):
    """CC 74 on a sine, a step every 50 ms and only when the value changes."""
    last = -1
    for k in range(int(secs * rate_hz)):
        t = k / rate_hz
        v = int(63.5 + 63.5 * math.sin(2.0 * math.pi * t / period_s))
        if v != last:
            last = v
            yield at(t + 0.1, (0xB0 | (ch - 1), 74, v), "cc", v)


def phase_soak(ctx):
    ph = Phase("soak")
    secs = ctx.args.soak_seconds
    follow_prepare(ctx, ph)
    wait_released(ctx)
    ch = user_channel(ctx)
    ph.info("plan", "%.0f s: clock in at %d BPM, a 16th-note pattern on channel %d, CC 74 swept on a 60 s sine, `midi`/`cpu`/`crash` every %.0f s; CLOCK OUT is on and counted" % (secs, SOAK_BPM, ch, SOAK_SAMPLE_S))
    events = merge_events([at(0.0, b"\xFA", "tr", 0xFA)], clock_events([("tempo", float(SOAK_BPM), secs)], t_start_ns=sec_ns(0.02)), soak_pattern(ch, secs, SOAK_BPM), soak_sweep(ch, secs),
                          [at(secs + 0.1, b"\xFC", "tr", 0xFC)])
    start_midi = ctx.unit.midi()
    start_cpu = ctx.unit.cpu()
    m0 = ctx.sample()
    series = []
    state = {"next_full": SOAK_SAMPLE_S}
    crash_ok = [True]

    def on_sample(s, sender, index):
        t = s_time(s, sender)
        row = {"t": round(t, 1), "following": s["following"], "locked": s["locked"], "tempo": s["tempo_state_bpm"], "dropped": s["dropped"], "reacquires": s["reacquires"], "dropouts": s["dropouts"],
               "missing": s["missing"], "rx": s["rx"], "clock_out": s["clock_out"], "sent": index.sent_by(s["t_ns"])}
        if t >= state["next_full"]:
            state["next_full"] += SOAK_SAMPLE_S
            c = ctx.unit.cpu()
            crash = ctx.unit.crash()
            crash_ok[0] = crash_ok[0] and crash_clean(ctx, crash)
            row.update(underrun=c.get("underrun"), mute_events=c.get("mute_events"), late=c.get("late"), slips=c.get("slips"), render_max_pct=c.get("render_max_pct"), over_budget=c.get("over_budget"),
                       crash_empty=crash_clean(ctx, crash), din_errors=s["din_errors"], din_overruns=s["din_overruns"], usb_discarded=s["usb_discarded"], usb_in_full=s["usb_in_full"])
            log_line("  [%4.0f s] following %s locked %s %.3f BPM  dropped %d  reacquires %d  dropouts %d  underrun %s  render max %s %%  crash %s" %
                     (t, "yes" if s["following"] else "no", "yes" if s["locked"] else "no", s["tempo_state_bpm"], s["dropped"], s["reacquires"], s["dropouts"], c.get("underrun"), c.get("render_max_pct"),
                      "empty" if row["crash_empty"] else "RECORD"), ph.out)
        series.append(row)

    sender = run_master(ctx, events, SOAK_POLL_S, on_sample)
    time.sleep(0.6)
    end_midi = ctx.unit.midi()
    end_cpu = ctx.unit.cpu()
    crash = ctx.unit.crash()
    sender_note(ph, sender, "sender")
    ph.data["series"] = series
    ph.check("soak: ran its length", len(series) >= 0.8 * secs / SOAK_POLL_S, "%d readouts over %.0f s" % (len(series), secs), ">= 80 %% of one every %.0f s" % SOAK_POLL_S)
    ph.check("soak: crash record empty", crash_ok[0] and crash_clean(ctx, crash), "empty at every sample and at the end" if crash_ok[0] and crash_clean(ctx, crash) else crash.splitlines()[0], "no crash record (or the one found at the start, unchanged)")
    d = deltas(end_midi, start_midi)
    ph.check("soak: dropped 0", d["dropped"] == 0 and d["polled_dropped"] == 0, "dropped +%d, polled-dropped +%d over %d messages" % (d["dropped"], d["polled_dropped"], d["messages"]), "0")
    ph.check("soak: din and usb errors 0", d["din_errors"] == 0 and d["din_overruns"] == 0 and d["usb_discarded"] == 0 and d["usb_in_full"] == 0 and d["usb_no_time"] == 0,
             "din errors +%d, overruns +%d, usb discarded +%d, in-full +%d, no-time +%d" % (d["din_errors"], d["din_overruns"], d["usb_discarded"], d["usb_in_full"], d["usb_no_time"]), "all 0")
    bad = {k: d32(end_cpu.get(k, 0), start_cpu.get(k, 0)) for k in CPU_ZERO_KEYS}
    ph.check("soak: overruns and underruns 0", not any(bad.values()), ", ".join("%s +%d" % kv for kv in bad.items()), "all 0")
    ph.check("soak: render peak within budget", end_cpu.get("render_max_pct", 0.0) <= RENDER_MAX_PCT, "render max %.1f %% of the block" % end_cpu.get("render_max_pct", 0.0), "<= %.0f %%" % RENDER_MAX_PCT)
    after_first = [r for r in series if r["t"] >= SOAK_SAMPLE_S]
    if after_first:
        r_first = after_first[0]
        r_new = series[-1]["reacquires"] - r_first["reacquires"]
        ph.check("soak: reacquires only at the start", r_new == 0, "+%d after the first minute (%d at the start)" % (r_new, r_first["reacquires"] - m0["reacquires"]), "0")
        locked = sum(1 for r in after_first if r["locked"])
        pct = 100.0 * locked / len(after_first)
        ph.check("soak: locked", pct >= SOAK_LOCKED_MIN_PCT, "%.2f %% of %d readouts after the first minute" % (pct, len(after_first)), ">= %.1f %%" % SOAK_LOCKED_MIN_PCT)
        worst = max(abs(r["tempo"] - SOAK_BPM) for r in after_first)
        ph.check("soak: tempo", worst <= SOAK_TEMPO_TOL_BPM, "worst error %.3f BPM" % worst, "<= %.1f" % SOAK_TEMPO_TOL_BPM)
        miss = series[-1]["missing"] - r_first["missing"]
        drop = series[-1]["dropouts"] - r_first["dropouts"]
        ph.check("soak: no missing clock, no dropout", miss == 0 and drop == 0, "missing +%d, dropouts +%d" % (miss, drop), "0")
    sent_n = sender.count("clk")
    out_n = d32(end_midi["clock_out"], start_midi["clock_out"])
    ph.check("soak: clocks out against clocks sent", abs(out_n - sent_n) <= SLIP_TOL, "sent %d, counted out %d (diff %+d)" % (sent_n, out_n, out_n - sent_n), "+-%d" % SLIP_TOL)
    ph.info("midi at the end", "rx +%d, messages +%d, notes +%d, usb frames +%d" % (d["rx"], d["messages"], d["notes"], d32(end_midi["usb_frames"], start_midi["usb_frames"])))
    wait_released(ctx)
    # the voice answers after the run
    ch_probe = ch
    ev = list(note_events((48,), 1.0, 0.0, ch_probe))
    if ctx.audio() is not None and ctx.audio_layout_ok():
        stream, sdr = audio_run(ctx, ev, SILENCE_WAIT_S + 0.5)
        if stream.ok:
            on, off = sdr.actuals("on")[0][0], sdr.actuals("off")[0][0]
            level = stream.rms(on + sec_ns(0.2), off - sec_ns(0.05))
            tts, stuck = stream.tail(off, off + sec_ns(SILENCE_WAIT_S))
            ph.check("soak: the voice answers after the run", level >= SOUND_MIN_DB and not stuck, "%.1f dBFS while held, silent %.2f s after" % (level, tts), ">= %.0f dBFS, silent within %.1f s" % (SOUND_MIN_DB, SILENCE_WAIT_S))
        else:
            ph.check("soak: the voice answers after the run", False, "no sound found")
    else:
        ph.check("soak: the voice answers after the run", None, "not run: %s" % (ctx.audio_note or "no audio"))
    return ph


# ---- phase: restore (always, in a finally block) -------------------------------------------------------------------------------------------

def state_view(unit):
    """The unit as the restore compares it: Config rows, screen, Config open, transport, tempo, following."""
    m = unit.midi()
    st = unit.state()
    return {"config": cfg_view(m), "screen": st.get("screen"), "sys": st.get("sys"), "section": st.get("section"), "playing": st.get("playing") == "1", "tempo_mbpm": int(st["tempo"]),
            "following": bool(m.get("following"))}


def phase_restore(ctx, start):
    """Puts back everything this tool may have changed. Every step is tried on its own; the result is a check per item and the end state printed against the start state."""
    ph = Phase("restore")
    u = ctx.unit
    p = ctx.panel
    ctx.stop_senders()
    ctx.unlisten()

    def step(name, fn):
        try:
            fn()
            return True
        except (UnitError, AudioError, OSError) as error:
            ph.check("restore: %s" % name, False, str(error))
            return False

    # a binding made by the learn phase and the patch it moved
    if ctx.learn_bound:
        if step("CLEAR LEARN", p.clear_learn):
            ctx.learn_bound = False
            ph.info("restore: learn", "CLEAR LEARN done (no binding was there at the start: --learn-baseline none)")
    if ctx.patch_dirty:
        if step("reset for the patch", lambda: after_reset(ctx)):
            ctx.patch_dirty = False
            ph.info("restore: patch", "reset: the patch is the saved one again")

    # the transport and the clock
    def transport():
        wait_released(ctx, 10.0)
        p.config_close()
        if u.midi().get("following"):
            raise UnitError("the unit still follows a clock 10 s after the last one")
        p.set_transport(bool(start["playing"]))
    step("transport", transport)

    # the Config's MIDI rows
    c = start["config"]
    fport = start.get("follow_port") or p.start_follow_port or c.get("follow_port")
    if p.start_follow_port is None and start.get("follow_port"):
        p.start_follow_port = start["follow_port"]
        p.port_touched = True
    want = {"channel": c["channel"], "out_channel": c["out_channel"], "ports": c["ports"], "clock_out": c["clock_out"], "transport_out": c["transport_out"], "transport_in": c["transport_in"],
            "follow_on": c["follow_on"]}
    if fport is not None:
        want["follow_port"] = fport
    step("Config rows", lambda: p.configure(**want))

    # the own tempo
    def tempo():
        if u.tempo_mbpm() != start["tempo_mbpm"] and not u.midi().get("following"):
            p.set_own_tempo(start["tempo_mbpm"])
    step("tempo", tempo)

    # the screen
    def screen():
        if start.get("sys") == "1":
            p.config_open()
        else:
            p.config_close()
            if start.get("screen") and u.state().get("screen") != start["screen"]:
                if start["screen"] in SCREEN_KEY:
                    p.goto_screen(start["screen"])
                else:
                    raise UnitError("the screen found (%s) is not one this tool can navigate to" % start["screen"])
    step("screen", screen)

    # the comparison
    try:
        end = state_view(u)
    except (UnitError, OSError) as error:
        ph.check("restore: read the end state", False, str(error))
        return ph, None
    ph.data["end_state"] = end
    ph.data["start_state"] = {k: start.get(k) for k in ("config", "screen", "sys", "section", "playing", "tempo_mbpm", "following")}
    log_line("  end state against start state:", ph.out)
    rows = []
    for key in ("channel", "out_channel", "ports", "clock_out", "transport_out", "transport_in", "follow_on"):
        rows.append(("config " + key, c[key], end["config"][key]))
    if end["config"]["follow_on"] and c.get("follow_port") is not None:
        rows.append(("config follow_port", c["follow_port"], end["config"]["follow_port"]))
    elif end["config"]["follow_on"] is False and fport is not None:
        ph.info("FOLLOW PORT (hidden while FOLLOW CLOCK is off)", "put back to %s, found %s: not readable on the console in this state" % (fport, "that" if p.start_follow_port == fport else "unknown"))
    rows += [("screen", start.get("screen"), end["screen"]), ("Config open", start.get("sys"), end["sys"]), ("playing", start["playing"], end["playing"]),
             ("tempo (mBPM)", start["tempo_mbpm"], end["tempo_mbpm"]), ("following", start["following"], end["following"])]
    for name, was, now in rows:
        log_line("    %-20s start %-12s end %-12s %s" % (name, was, now, "same" if str(was) == str(now) else "DIFFERENT"), ph.out)
        ph.check("restore: %s" % name, str(was) == str(now), "start %s, end %s" % (was, now), "the same")
    try:
        crash = u.crash()
        ph.info("crash at the end", crash.splitlines()[0] if crash else "(empty)")
        ph.data["end_crash_empty"] = crash_is_empty(crash)
        ph.check("restore: crash record as at the start", crash_clean(ctx, crash), crash.splitlines()[0] if crash else "(empty)", "no new crash")
    except UnitError:
        pass
    return ph, end


# ---- the report -----------------------------------------------------------------------------------------------------------------------------

def write_report(out_dir, phases, meta):
    os.makedirs(out_dir, exist_ok=True)
    report = {"tool": "midi_check", "meta": meta, "phases": [p.as_dict() for p in phases]}
    with open(os.path.join(out_dir, "report.json"), "w") as f:
        json.dump(report, f, indent=1, default=str)
    lines = ["# midi_check report", "", "- version: `%s`" % meta.get("version"), "- when: %s" % meta.get("when"), "- command: `%s`" % meta.get("argv"), "- run time: %.0f s" % meta.get("seconds", 0), ""]
    lines += ["| phase | status | seconds | checks |", "|---|---|---|---|"]
    for p in phases:
        n_ok = sum(1 for c in p.checks if c["ok"] is True)
        n_fail = sum(1 for c in p.checks if c["ok"] is False)
        n_skip = sum(1 for c in p.checks if c["ok"] is None)
        lines.append("| %s | %s | %.0f | %d pass, %d fail, %d not run |" % (p.name, p.status, p.seconds, n_ok, n_fail, n_skip))
    lines.append("")
    for p in phases:
        lines += ["## %s: %s" % (p.name, p.status), ""]
        if p.error:
            lines += ["**Error:** `%s`" % p.error.splitlines()[-1], ""]
        lines += ["| | check | measured | limit |", "|---|---|---|---|"]
        for c in p.checks:
            tag = {True: "PASS", False: "FAIL", None: "SKIP", "info": "info"}[c["ok"]]
            lines.append("| %s | %s | %s | %s |" % (tag, c["name"].replace("|", "/"), c["value"].replace("|", "/"), c["limit"].replace("|", "/")))
        for n in p.notes:
            lines.append("")
            lines.append("- note: " + n)
        lines.append("")
    with open(os.path.join(out_dir, "summary.md"), "w") as f:
        f.write("\n".join(lines) + "\n")


# ---- main -----------------------------------------------------------------------------------------------------------------------------------

PHASES = {"identity": phase_identity, "notes": phase_notes, "follow": phase_follow, "clockout": phase_clockout, "learn": phase_learn, "stress": phase_stress, "soak": phase_soak}
PHASE_ORDER = ("identity", "notes", "follow", "clockout", "learn", "stress", "soak")


def build_parser():
    ap = argparse.ArgumentParser(prog="midi_check.py", description="MIDI test of o3-303 on the real Tracker over USB (see the top of this file).")
    ap.add_argument("--expect-version", help="text the console's `version` must contain: the tool touches nothing else until it does (required)")
    ap.add_argument("--port", help="the console's serial port (default: the first /dev/serial/by-id/*Track3r*)")
    ap.add_argument("--midi-dev", default="/dev/snd/midiC1D0", help="the unit's raw ALSA MIDI node (`amidi -l`); opened for writing and, for clock out, for reading")
    ap.add_argument("--midi-out-dev", help="the node to read the unit's MIDI from when it is another than --midi-dev")
    ap.add_argument("--audio-device", default="auto", help="the capture device of the unit's USB audio (default: found in `arecord -l`), or hw:CARD,DEV")
    ap.add_argument("--audio-channels", type=int, default=0, help="the capture stream's channel count (default: asked of the device, else 8)")
    ap.add_argument("--no-audio", action="store_true", help="no audio checks: they are reported as not run")
    ap.add_argument("--arecord", default="arecord")
    ap.add_argument("--phases", default=",".join(PHASE_ORDER), help="comma list of: %s (restore is always run)" % ", ".join(PHASE_ORDER))
    ap.add_argument("--out", help="directory for report.json, summary.md and the baseline (default: ./midi-check-<time>)")
    ap.add_argument("--learn-baseline", choices=("none", "unknown"), default="unknown", help="`none` declares that the unit has no MIDI learn binding: the learn phase runs only then")
    ap.add_argument("--tempo-hold", type=float, default=None, help="seconds at each tempo of the follow tests (default 10)")
    ap.add_argument("--jitter-seconds", type=float, default=None, help="default 30")
    ap.add_argument("--long-seconds", type=float, default=None, help="the long run of the follow phase (default 600)")
    ap.add_argument("--clockout-seconds", type=float, default=None, help="per tempo (default 60)")
    ap.add_argument("--flood-seconds", type=float, default=None, help="default 30")
    ap.add_argument("--soak-seconds", type=float, default=None, help="default 1800")
    ap.add_argument("--follow-tests", default="", help="comma list of start,profile,jitter,steps,ramp,dropout,transport,long (default all)")
    ap.add_argument("--quick", action="store_true", help="short versions of every duration (about 10 minutes in all): a smoke run, not a proof")
    ap.add_argument("--restore-from", metavar="DIR", help="put the Config back from DIR/start_state.json (a run that died) and exit")
    ap.add_argument("--self-test", action="store_true", help="run against a fake unit on a pty: no hardware")
    return ap


def apply_durations(args):
    full = {"tempo_hold": 10.0, "jitter_seconds": 30.0, "long_seconds": 600.0, "clockout_seconds": 60.0, "flood_seconds": 30.0, "soak_seconds": 1800.0}
    quick = {"tempo_hold": 5.0, "jitter_seconds": 8.0, "long_seconds": 30.0, "clockout_seconds": 10.0, "flood_seconds": 5.0, "soak_seconds": 60.0}
    for key, value in (quick if args.quick else full).items():
        if getattr(args, key) is None:
            setattr(args, key, value)


def find_console_port(args):
    if args.port:
        return args.port
    console = load_sdk_tool("o3console")
    ports = console.find_ports()
    if not ports:
        raise Refused("no Track3r console port under /dev/serial/by-id (is the unit plugged in? --port says which)")
    return ports[0]


def safety_check(unit, args):
    """The refusal: nothing but the console is touched until the version text and the app name are the expected ones."""
    try:
        version = unit.cmd("version")
    except UnitError as error:
        raise Refused("the unit's console does not answer `version`: %s" % error)
    if args.expect_version not in version:
        raise Refused("`version` says %r, which does not contain --expect-version %r: nothing was sent but `version`" % (version.strip().splitlines()[0][:100] if version.strip() else "", args.expect_version))
    st = unit.state()
    if st.get("app") != EXPECT_APP:
        raise Refused("`state` says app=%s, not %s: nothing was sent but `version` and `state`" % (st.get("app"), EXPECT_APP))


def run(args, log=log_line, console=None):
    t_begin = time.monotonic()
    apply_durations(args)
    out_dir = args.out or os.path.join(os.getcwd(), "midi-check-" + time.strftime("%Y%m%d-%H%M%S"))
    os.makedirs(out_dir, exist_ok=True)
    port = find_console_port(args)
    console = console or load_sdk_tool("o3console")
    transcript = []
    unit = Unit(port, console, transcript=transcript)
    try:
        unit.open()
    except Fatal as error:
        log("REFUSED: %s" % error)
        return 2
    ctx = None
    phases = []
    status = 0
    restored_ok = False
    try:
        safety_check(unit, args)
        panel = Panel(unit)
        ctx = Ctx(args, unit, panel, out_dir)
        sys.setswitchinterval(0.0002)  # the sender thread must get the interpreter back within a fraction of a clock period
        if args.restore_from:
            with open(os.path.join(args.restore_from, "start_state.json")) as f:
                start = json.load(f)
            ctx.start = start
            log("restoring from %s" % args.restore_from)
            ph, _ = phase_restore(ctx, start)
            phases.append(ph)
            restored_ok = ph.status == "PASS"
            return finish(args, ctx, phases, t_begin, out_dir, status, restored_ok, unit)
        wanted = [p.strip() for p in args.phases.split(",") if p.strip() and p.strip() != "restore"]
        for name in wanted:
            if name not in PHASES:
                raise Refused("unknown phase %r (known: %s)" % (name, ", ".join(PHASE_ORDER)))
        order = ["identity"] + sorted((n for n in wanted if n != "identity"), key=lambda n: PHASE_ORDER.index(n) if n in PHASE_ORDER else len(PHASE_ORDER))
        fatal = None
        for name in order:
            log("")
            log("== phase %s ==" % name)
            t0 = time.monotonic()
            try:
                ph = PHASES[name](ctx)
            except Fatal as error:
                ph = Phase(name)
                ph.error = "stopped: %s" % error
                ph.check(name + " ran", False, str(error))
                fatal = error
            except KeyboardInterrupt:
                ph = Phase(name)
                ph.error = "interrupted"
                ph.check(name + " ran", False, "interrupted by the keyboard")
                fatal = Fatal("interrupted")
            except Exception as error:  # a bug here must not stop the restore
                ph = Phase(name)
                ph.error = traceback.format_exc()
                ph.check(name + " ran", False, "%s: %s" % (type(error).__name__, error))
                log(ph.error)
            ph.seconds = time.monotonic() - t0
            ctx.stop_senders()
            phases.append(ph)
            log("== %s: %s in %.0f s ==" % (name, ph.status, ph.seconds))
            if name == "identity" and ph.status != "PASS":
                break
            if fatal is not None:
                break
        ph, _ = restore_now(ctx, log)
        phases.append(ph)
        restored_ok = ph.status == "PASS"
        return finish(args, ctx, phases, t_begin, out_dir, status, restored_ok, unit)
    except Refused as error:
        log("REFUSED: %s" % error)
        return 2
    finally:
        if ctx is not None:
            ctx.stop_senders()
            ctx.unlisten()
            ctx.tx.close()
        unit.close()
        with open(os.path.join(out_dir, "console_transcript.txt"), "w") as f:
            for t, line, text in transcript:
                f.write("%9.3f > %s\n%s\n" % (t, line, text))


def restore_now(ctx, log):
    log("")
    log("== phase restore ==")
    t0 = time.monotonic()
    start = ctx.start
    if start is None:
        ph = Phase("restore")
        ph.check("restore", True, "nothing was changed: the identity phase did not finish, and no Config row is touched before it")
        return ph, None
    try:
        ph, end = phase_restore(ctx, start)
    except Exception as error:
        ph = Phase("restore")
        ph.error = traceback.format_exc()
        ph.check("restore ran", False, "%s: %s; run `--restore-from %s` once the console answers" % (type(error).__name__, error, ctx.out_dir))
        end = None
    ph.seconds = time.monotonic() - t0
    log("== restore: %s ==" % ph.status)
    return ph, end


def finish(args, ctx, phases, t_begin, out_dir, status, restored_ok, unit):
    meta = {"version": args.expect_version, "when": time.strftime("%Y-%m-%d %H:%M:%S"), "argv": " ".join(sys.argv), "seconds": time.monotonic() - t_begin, "start_state": ctx.start,
            "midi_dev": args.midi_dev, "audio": ctx.audio_device or ctx.audio_note}
    write_report(out_dir, phases, meta)
    log_line("")
    log_line("%-10s %-11s %s" % ("phase", "status", "seconds"))
    for p in phases:
        log_line("%-10s %-11s %.0f" % (p.name, p.status, p.seconds))
    log_line("report: %s" % os.path.join(out_dir, "summary.md"))
    statuses = [p.status for p in phases]
    rest = next((p for p in phases if p.name == "restore"), None)
    if rest is not None and rest.status != "PASS":
        log_line("THE UNIT WAS NOT FULLY RESTORED: compare the restore checks above with the start state in %s and put the rows back by hand, or run --restore-from" % os.path.join(out_dir, "start_state.json"))
        return 4
    if "FAIL" in statuses:
        return 1
    if "INCOMPLETE" in statuses:
        return 3
    return 0


def self_test():
    """Against a fake unit (tools/midi_check_fake.py): no hardware. 0 passed, 1 failed, 77 this machine cannot run it."""
    fake = load_module("midi_check_fake", os.path.join(HERE, "midi_check_fake.py"))
    return fake.run_self_test(sys.modules[__name__])


def main(argv=None):
    ap = build_parser()
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.expect_version:
        ap.error("--expect-version is required: the tool refuses to run against firmware it was not told to expect")
    try:
        return run(args)
    except Refused as error:
        print("REFUSED: %s" % error)
        return 2
    except KeyboardInterrupt:
        print("interrupted: the Config was restored if the run got that far; see --restore-from")
        return 130


if __name__ == "__main__":
    sys.exit(main())
