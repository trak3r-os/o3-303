#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A fake o3-303 unit for midi_check's self-test: no hardware.

    midi_check_fake.py --serve DIR [--screen S]     the unit: a pty console, two FIFOs for the raw MIDI node, a synth log; writes DIR/ready.json
    midi_check_fake.py --shim [arecord's arguments] a stand-in for `arecord`: renders the fake unit's voice as raw 8-channel S16 in real time

The unit is a separate process on purpose: it has its own interpreter lock, so that the follow loop it runs sees the clock sender's real timing and not the self-test's thread
scheduling. It models what midi_check looks at, the way the kit does it where that matters (counters, lock, release after 48 clocks, pickup learn) and no further: it is a
test of the tool, not a model of the firmware. It speaks the console's formats (the lines of platform/mk66/console midi_report.cpp) and answers `key`, `enc`, `state`, `midi`,
`cpu`, `crash`, `log`, `leds`, `version` and `reset`.
"""
import array
import json
import math
import os
import select
import signal
import sys
import threading
import time

PROMPT = b"o3> "
SECTIONS = ("firmware", "audio", "midi", "card", "display", "info")
PORTS = ("none", "din", "usb", "din+usb")
SCREEN_OF_KEY = {"master": "MASTER", "pattern": "STEPS", "performance": "PERFORM", "song": "SONG", "file": "FILE_PROJECTS", "samplerec": "RECORD", "sampleplay": "GROWL", "sampleedit": "WOBBLE"}
INSTR_PAGES = ("INSTRUMENT_VOICE", "INSTRUMENT_FX")
BUTTONS = ("f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "params", "performance", "file", "config", "master", "sampleload", "sampleplay", "sampleedit", "samplerec", "song", "note",
           "instr", "vol", "fx", "pattern", "play", "copy", "insert", "up", "delete", "rec", "shift", "left", "down", "right")
WINDOW = 36            # clocks in the follow fit
LOCK_AT = 20           # clocks before `locked`
LOCK_RMS = 0.15        # residual rms over the period that still counts as locked
STALE_CLOCKS = 48
MAX_DROPOUT = 24
DEFAULT_CUTOFF = 50
TEMPO_STEP_MBPM = 500


def mono_ns():
    return time.monotonic_ns()


def hex2(v):
    return "%02X" % v


class Model:
    """The unit's state and what the bytes of the MIDI node and the keys do to it."""

    def __init__(self, workdir, screen="STEPS"):
        self.dir = workdir
        self.lock = threading.RLock()
        self.cfg = {"channel": 4, "out_channel": 2, "ports": "din", "clock_out": False, "transport_out": False, "transport_in": True, "follow_on": False, "follow_port": "din"}
        self.binding = None
        self.own_mbpm = 120000
        self.first_screen = screen
        self.synth = open(os.path.join(workdir, "synth.log"), "a", buffering=1)
        self.rx_fd = None
        self.halt = False
        self.emit_gen = 0
        self.boot(first=True)

    # -- boot: what a `reset` gives back (the saved Config, the bindings and the tempo stay; the rest is the boot's)
    def boot(self, first=False):
        with self.lock:
            self.screen = self.first_screen if first else "STEPS"
            self.sys = 0
            self.section = 0
            self.cursor = 0
            self.asking_until = 0.0
            self.shift = False
            self.armed = False
            self.playing = False
            self.pending_play = 0
            self.cutoff = DEFAULT_CUTOFF
            self.picked = False
            self.last_cc = None
            self.held = []
            self.c = dict.fromkeys(("rx", "tx", "dropped", "messages", "notes", "held", "polled_dropped", "sent", "bad_out", "clock_out", "start_stop_out", "transport_in", "ignored", "requests",
                                    "attached", "released", "usb_frames", "events_out", "events_in", "bytes_in", "discarded", "no_time", "in_full", "loop_clocks", "reacquires", "dropouts",
                                    "missing", "in_clocks", "blocks", "renders"), 0)
            self.following = False
            self.window = []
            self.last_clock = 0
            self.bad_run = 0
            self.after_gap = False
            self.rx_log = []
            self.status = 0
            self.need = 0
            self.data = []
            self.sysex = False
            self.emit_gen += 1
            self.emitting = False
            self.synth_line("reset")

    def synth_line(self, kind, a=0, b=0):
        self.synth.write("%d %s %d %d\n" % (mono_ns(), kind, a, b))

    # -- the loop
    def period_ns(self):
        if len(self.window) < 2:
            return None
        (i0, t0), (i1, t1) = self.window[0], self.window[-1]
        return (t1 - t0) / float(i1 - i0)

    def fit(self):
        """(period in ns, residual rms in ns) of the window by least squares through (index, time)."""
        n = len(self.window)
        if n < 12:
            return None, None
        xs = [w[0] for w in self.window]
        ys = [w[1] for w in self.window]
        mx = sum(xs) / n
        my = sum(ys) / n
        sxx = sum((x - mx) ** 2 for x in xs)
        slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
        res = [y - (my + slope * (x - mx)) for x, y in zip(xs, ys)]
        return slope, math.sqrt(sum(r * r for r in res) / n)

    @property
    def locked(self):
        if not self.following or len(self.window) < LOCK_AT:
            return False
        slope, rms = self.fit()
        return slope is not None and rms <= LOCK_RMS * slope

    def loop_mbpm(self):
        slope, _ = self.fit()
        if slope is None:
            return 0
        return int(round(60e9 / (24.0 * slope) * 1000.0))

    def tempo_mbpm(self):
        if self.following and len(self.window) >= 12:
            return self.loop_mbpm()
        return self.own_mbpm

    def release(self):
        self.following = False
        self.window = []
        self.c["released"] += 1
        self.playing = False
        self.pending_play = 0
        self.stop_emit()

    def clock(self, t):
        cfg = self.cfg
        if not (cfg["follow_on"] and cfg["follow_port"] == "usb"):
            return
        c = self.c
        c["in_clocks"] += 1
        c["loop_clocks"] += 1
        if not self.following:
            self.following = True
            c["attached"] += 1
            self.window = [(0, t)]
            self.bad_run = 0
            self.after_gap = False
            if self.pending_play and t < self.pending_play:
                self.playing = True
            self.pending_play = 0
        else:
            period = self.period_ns()
            last_i, last_t = self.window[-1]
            iv = t - last_t
            if period is None:
                self.window.append((last_i + 1, t))
            elif iv > MAX_DROPOUT * period:
                c["reacquires"] += 1
                self.window = [(0, t)]
                self.bad_run = 0
            else:
                k = max(1, int(round(iv / period)))
                if k > 1 and 0.7 <= iv / (k * period) <= 1.3 and not self.after_gap:
                    # missing clocks, unless the next interval says it was a slower master: a second such gap in a row is a tempo change
                    c["dropouts"] += 1
                    c["missing"] += k - 1
                    self.window.append((last_i + k, t))
                    self.bad_run = 0
                    self.after_gap = True
                    self.last_clock = t
                    return self._after_clock(t)
                elif k > 1 and self.after_gap:
                    c["reacquires"] += 1
                    self.window = [(0, t)]
                    self.bad_run = 0
                    self.after_gap = False
                elif not (0.7 <= iv / period <= 1.3):
                    self.bad_run += 1
                    self.window.append((last_i + 1, t))
                    if self.bad_run >= 3:
                        c["reacquires"] += 1
                        self.window = self.window[-1:]
                        self.bad_run = 0
                else:
                    self.bad_run = 0
                    self.after_gap = False
                    self.window.append((last_i + 1, t))
            self.window = self.window[-WINDOW:]
        self.last_clock = t
        return self._after_clock(t)

    def _after_clock(self, t):
        cfg, c = self.cfg, self.c
        self.window = self.window[-WINDOW:]
        self.last_clock = t
        if cfg["clock_out"] and cfg["ports"] in ("usb", "din+usb"):
            self.emit(b"\xF8")
            c["clock_out"] += 1

    def tick(self, t):
        with self.lock:
            if self.following:
                period = self.period_ns() or 20e6
                if t - self.last_clock > STALE_CLOCKS * period:
                    self.release()
            if self.pending_play and t > self.pending_play:
                self.pending_play = 0
            if self.asking_until and time.monotonic() > self.asking_until:
                self.asking_until = 0.0

    # -- the output
    def emit(self, data):
        if self.rx_fd is None:
            return
        try:
            os.write(self.rx_fd, data)
        except (BlockingIOError, OSError):
            pass

    def start_emit(self):
        cfg = self.cfg
        if not (cfg["clock_out"] and cfg["ports"] in ("usb", "din+usb")) or self.following:
            return
        if cfg["transport_out"]:
            self.emit(b"\xFA")
            self.c["start_stop_out"] += 1
        self.emit_gen += 1
        gen = self.emit_gen
        self.emitting = True
        threading.Thread(target=self._emitter, args=(gen,), daemon=True).start()

    def stop_emit(self):
        if self.emitting:
            self.emit_gen += 1
            self.emitting = False
            if self.cfg["transport_out"] and self.cfg["ports"] in ("usb", "din+usb"):
                self.emit(b"\xFC")
                self.c["start_stop_out"] += 1

    def _emitter(self, gen):
        period = 60e9 / (24.0 * self.own_mbpm / 1000.0)
        t0 = mono_ns()
        k = 0
        while gen == self.emit_gen and not self.halt:
            target = int((t0 + k * period) // 1e6 * 1e6)  # a clock leaves in a 1 ms frame: its time is the frame's
            while True:
                left = target - mono_ns()
                if left <= 0 or gen != self.emit_gen:
                    break
                if left > 1_500_000:
                    time.sleep((left - 1_500_000) / 1e9)
            if gen != self.emit_gen:
                break
            self.emit(b"\xF8")
            self.c["clock_out"] += 1
            k += 1

    # -- the bytes of the node
    def feed(self, data, t):
        with self.lock:
            self.c["bytes_in"] += len(data)
            for b in data:
                if b >= 0xF8:
                    self.realtime(b, t)
                    continue
                if b >= 0x80:
                    self.sysex = b == 0xF0
                    if b >= 0xF0:
                        self.status = b if b == 0xF2 else 0
                        self.need = 2
                        self.data = []
                    else:
                        self.status = b
                        self.need = 1 if (b & 0xF0) in (0xC0, 0xD0) else 2
                        self.data = []
                    continue
                if self.sysex or self.status == 0:
                    continue
                self.data.append(b)
                if len(self.data) == self.need:
                    self.message(self.status, self.data)
                    self.data = []
                    if self.status == 0xF2:
                        self.status = 0
            self.c["events_in"] += 1

    def log_rx(self, status, data):
        for b in [status] + list(data):
            self.rx_log.append("U:" + hex2(b))
        self.rx_log = self.rx_log[-16:]

    def realtime(self, b, t):
        if b == 0xF8:
            self.clock(t)
        elif b in (0xFA, 0xFB, 0xFC):
            self.transport(b, t)

    def transport(self, b, t):
        cfg = self.cfg
        c = self.c
        if not (cfg["transport_in"] and cfg["follow_on"] and cfg["follow_port"] == "usb"):
            c["ignored"] += 1
            return
        c["transport_in"] += 1
        if b in (0xFA, 0xFB):
            if self.following:
                self.playing = True
            else:
                self.pending_play = t + 2_000_000_000
        else:
            self.playing = False
            self.pending_play = 0

    def accepts(self, ch):
        return self.cfg["channel"] in (0, ch)

    def message(self, status, data):
        c = self.c
        if status == 0xF2:
            cfg = self.cfg
            if cfg["transport_in"] and cfg["follow_on"] and cfg["follow_port"] == "usb":
                c["transport_in"] += 1
            else:
                c["ignored"] += 1
            return
        kind = status & 0xF0
        ch = (status & 0x0F) + 1
        c["rx"] += 1
        c["messages"] += 1
        self.log_rx(status, data)
        if not self.accepts(ch):
            return
        if kind == 0x90 and data[1] > 0:
            c["notes"] += 1
            self.note_on(data[0], data[1])
        elif kind == 0x80 or kind == 0x90:
            c["notes"] += 1
            self.note_off(data[0])
        elif kind == 0xB0:
            n, v = data
            if n in (120, 123):
                self.held = []
                self.synth_line("allq")
            elif n < 120:
                self.control(n, v)

    def note_on(self, n, vel):
        if n in self.held:
            self.held.remove(n)
        self.held.append(n)
        self.synth_line("on", n, vel)

    def note_off(self, n):
        if n in self.held:
            self.held.remove(n)
        if self.held:
            self.synth_line("on", self.held[-1], 100)
        else:
            self.synth_line("off")

    def control(self, n, v):
        if self.armed:
            self.armed = False
            self.binding = n
            self.picked = False
            self.last_cc = v
            return
        if self.binding is None or n != self.binding:
            return
        if not self.picked:
            prev = self.last_cc if self.last_cc is not None else v
            if (prev - self.cutoff) * (v - self.cutoff) <= 0:
                self.picked = True
            self.last_cc = v
        if self.picked:
            self.cutoff = v
            self.synth_line("cutoff", v)

    # -- keys and the encoder
    def key(self, name, gesture):
        with self.lock:
            if name == "shift":
                self.shift = gesture != "up"
                return
            if gesture == "up":
                return
            if name == "config":
                self.sys = 0 if self.sys else 1
                self.section = 0
                self.cursor = 0
                return
            if self.sys:
                if name in ("right", "left"):
                    self.section = max(0, min(len(SECTIONS) - 1, self.section + (1 if name == "right" else -1)))
                    self.cursor = 0
                elif name in ("up", "down") and SECTIONS[self.section] == "midi":
                    self.cursor = max(0, min(8, self.cursor + (1 if name == "down" else -1)))
                elif name == "f1" and SECTIONS[self.section] == "midi":
                    self.step_row(1, True)
                return
            if name == "play":
                if self.following:
                    return
                self.playing = not self.playing
                if self.playing:
                    self.start_emit()
                else:
                    self.stop_emit()
            elif name in SCREEN_OF_KEY:
                self.screen = SCREEN_OF_KEY[name]
            elif name == "params":
                if self.screen in INSTR_PAGES:
                    self.screen = INSTR_PAGES[(INSTR_PAGES.index(self.screen) + 1) % len(INSTR_PAGES)]
                else:
                    self.screen = INSTR_PAGES[0]
            elif name == "f1" and self.shift and self.screen == "INSTRUMENT_VOICE":
                self.armed = not self.armed

    def enc(self, n):
        with self.lock:
            if self.sys:
                if SECTIONS[self.section] == "midi":
                    self.step_row(n, False)
            elif self.screen == "MASTER":
                self.own_mbpm = max(30000, min(300000, self.own_mbpm + n * TEMPO_STEP_MBPM))

    def step_row(self, steps, wrap):
        row = self.cursor
        cfg = self.cfg

        def walk(value, lo, hi):
            v = value + steps
            if wrap:
                span = hi - lo + 1
                v = lo + ((v - lo) % span + span) % span
            return max(lo, min(hi, v))

        if row == 0:
            cfg["channel"] = walk(cfg["channel"], 0, 16)
        elif row == 1:
            cfg["out_channel"] = walk(cfg["out_channel"], 1, 16)
        elif row == 2:
            cfg["ports"] = PORTS[walk(PORTS.index(cfg["ports"]), 0, 3)]
        elif row == 3:
            cfg["clock_out"] = (not cfg["clock_out"]) if steps else cfg["clock_out"]
        elif row == 4:
            cfg["transport_out"] = (not cfg["transport_out"]) if steps else cfg["transport_out"]
        elif row == 5:
            if steps:
                cfg["follow_on"] = not cfg["follow_on"]
                if not cfg["follow_on"] and self.following:
                    self.release()
        elif row == 6:
            if steps:
                cfg["follow_port"] = "usb" if cfg["follow_port"] == "din" else "din"
        elif row == 7:
            cfg["transport_in"] = (not cfg["transport_in"]) if steps else cfg["transport_in"]
        elif row == 8 and steps:
            if self.asking_until and time.monotonic() <= self.asking_until:
                self.binding = None
                self.asking_until = 0.0
            else:
                self.asking_until = time.monotonic() + 5.0

    # -- the console's answers
    def v_state(self):
        return "app=O3-303 screen=%s playing=%d tempo=%d sys=%d section=%s usb=P8\n" % (self.screen, 1 if self.playing else 0, self.tempo_mbpm(), self.sys, SECTIONS[self.section])

    def v_midi(self):
        c, g = self.c, self.cfg
        with self.lock:
            lock = self.locked
            follow = "off" if not g["follow_on"] else g["follow_port"]
            t = []
            t.append("midi: following %s locked %s tempo %.2f bpm (loop %.2f bpm, clocks %d, error %d us)" % ("yes" if self.following else "no", "yes" if lock else "no",
                                                                                                         self.tempo_mbpm() / 1000.0 if self.following else 0.0, self.loop_mbpm() / 1000.0, c["in_clocks"], 0))
            t.append("midi: config channel %s out %d ports %s clock-out %s transport-out %s transport-in %s follow %s" % (
                "omni" if g["channel"] == 0 else g["channel"], g["out_channel"], g["ports"], "on" if g["clock_out"] else "off", "on" if g["transport_out"] else "off",
                "on" if g["transport_in"] else "off", follow))
            t.append("midi: rx %d tx %d dropped %d messages %d notes %d held %d polled-dropped %d sent %d bad-out %d" % (c["rx"], c["tx"], c["dropped"], c["messages"], c["notes"], len(self.held),
                                                                                                                     c["polled_dropped"], c["sent"], c["bad_out"]))
            t.append("midi: clock-out %d start-stop-out %d transport-in %d ignored %d requests %d attached %d released %d" % (c["clock_out"], c["start_stop_out"], c["transport_in"], c["ignored"],
                                                                                                                                  c["requests"], c["attached"], c["released"]))
            t.append("midi: din rx isrs 0 bytes 0 errors 0 overruns 0 full 0 multi 0 tx irqs 0 bytes 0 rt 0 late-max 0 us no-time 0 not-ready 0")
            t.append("midi: usb frames %d events-out %d events-in %d bytes-in %d discarded %d no-time %d in-full %d" % (c["usb_frames"], c["clock_out"], c["events_in"], c["bytes_in"], 0, 0, 0))
            t.append("midi: loop clocks %d reacquires %d dropouts %d missing %d" % (c["loop_clocks"], c["reacquires"], c["dropouts"], c["missing"]))
            t.append("midi: last rx " + " ".join(self.rx_log).strip())
            t.append("midi: last tx")
        return "\n".join(t).replace("last rx \n", "last rx\n") + "\n"

    def v_cpu(self):
        return ("audio  blocks 100000  renders 100000  underrun 0 (run 0, max 0)  mute 0 (events 0)  late 0  slips 0  hwfault 0\n"
                "render last 70000  max 76000 cyc = 41.3 % of the block (184000 cyc)  over-budget 0\n"
                "guard  shed 0 blocks in 0 runs  late 0  longest run since boot 0 blocks  n 8\n")

    def v_leds(self):
        return "leds online=1 errors=0 reinits=0 lit=%d sync=1 levels=%s\n" % (3 if self.playing else 0, "".join("A0" if (self.playing and i < 3) else "00" for i in range(48)))


class Console(threading.Thread):
    """The console on the pty master: echo, CR LF, the prompt, and the commands of the model."""

    def __init__(self, model, fd, workdir):
        super().__init__(daemon=True)
        self.m = model
        self.fd = fd
        self.log = open(os.path.join(workdir, "commands.log"), "a", buffering=1)
        self.quiet_until = 0.0
        self.line = b""

    def say(self, text):
        os.write(self.fd, text.replace("\n", "\r\n").encode())

    def command(self, text):
        words = text.split()
        verb = words[0] if words else ""
        m = self.m
        if verb == "version":
            return "Track3r o3-303, build fake0001\nusb 16D0:0E9F rev 0001 serial FAKE\n"
        if verb == "state":
            return m.v_state()
        if verb == "midi":
            return m.v_midi()
        if verb == "cpu":
            return m.v_cpu()
        if verb == "crash":
            return "no crash record\n"
        if verb == "log":
            return "log: 2 lines written, showing 2\n10 ms  boot: main reached\n20 ms  usb: configured\n"
        if verb == "leds":
            return m.v_leds()
        if verb == "usb":
            return "usb: Configured, address 7\n"
        if verb == "key" and len(words) >= 2 and words[1] in BUTTONS:
            m.key(words[1], words[2] if len(words) > 2 else "tap")
            return "ok key %s\n" % " ".join(words[1:])
        if verb == "key":
            return "ERR unknown key\n"
        if verb == "enc" and len(words) == 2:
            try:
                n = int(words[1])
            except ValueError:
                return "ERR enc needs a number\n"
            m.enc(max(-64, min(64, n)))
            return "ok enc %d\n" % n
        return "unknown command: %s (help lists the commands)\n" % text

    def run(self):
        os.write(self.fd, b"\r\nTrack3r diag - fake - 'help' lists the commands\r\n" + PROMPT)
        while not self.m.halt:
            try:
                ready, _, _ = select.select([self.fd], [], [], 0.05)
            except (OSError, ValueError):
                return
            if not ready:
                continue
            try:
                data = os.read(self.fd, 4096)
            except OSError:
                return
            if time.monotonic() < self.quiet_until:
                continue  # booting: the console is not there
            for byte in data:
                ch = bytes([byte])
                if ch in (b"\r", b"\n"):
                    os.write(self.fd, b"\r\n")
                    text = self.line.decode("utf-8", "replace")
                    self.line = b""
                    if text.strip():
                        self.log.write(text + "\n")
                    if text.strip() == "reset":
                        self.m.boot()
                        self.quiet_until = time.monotonic() + 2.0
                        self.say("\nTrack3r diag - fake reset\n")
                        os.write(self.fd, PROMPT)
                        continue
                    reply = self.command(text) if text.strip() else ""
                    os.write(self.fd, reply.replace("\n", "\r\n").encode() + PROMPT)
                else:
                    self.line += ch
                    os.write(self.fd, ch)


def serve(workdir, screen):
    os.makedirs(workdir, exist_ok=True)
    tx_path = os.path.join(workdir, "midi_in")      # the host writes, the unit reads
    rx_path = os.path.join(workdir, "midi_out")     # the unit writes, the host reads
    for p in (tx_path, rx_path):
        if os.path.exists(p):
            os.unlink(p)
        os.mkfifo(p)
    tx_fd = os.open(tx_path, os.O_RDWR | os.O_NONBLOCK)  # read side; the writer's open succeeds because this one is there
    rx_fd = os.open(rx_path, os.O_RDWR | os.O_NONBLOCK)
    master, slave = os.openpty()
    import tty
    tty.setraw(slave)  # no echo: what the console says must not come back as typing
    model = Model(workdir, screen)
    model.rx_fd = rx_fd
    console = Console(model, master, workdir)
    console.start()
    with open(os.path.join(workdir, "ready.json.tmp"), "w") as f:
        json.dump({"console": os.ttyname(slave), "midi_in": tx_path, "midi_out": rx_path, "synth": os.path.join(workdir, "synth.log"), "pid": os.getpid()}, f)
    os.rename(os.path.join(workdir, "ready.json.tmp"), os.path.join(workdir, "ready.json"))
    signal.signal(signal.SIGTERM, lambda *a: setattr(model, "halt", True))
    while not model.halt:
        try:
            ready, _, _ = select.select([tx_fd], [], [], 0.002)
        except (OSError, ValueError):
            break
        now = mono_ns()
        if ready:
            try:
                data = os.read(tx_fd, 65536)
            except BlockingIOError:
                data = b""
            if data:
                model.feed(data, now)
        model.tick(now)
        # a persistent dump of the Config for the self-test to read: only when asked
        if os.path.exists(os.path.join(workdir, "dump.request")):
            os.unlink(os.path.join(workdir, "dump.request"))
            with model.lock:
                with open(os.path.join(workdir, "dump.json"), "w") as f:
                    json.dump({"cfg": model.cfg, "binding": model.binding, "screen": model.screen, "sys": model.sys, "playing": model.playing, "own_mbpm": model.own_mbpm,
                               "cutoff": model.cutoff}, f)
    return 0


# ---- the stand-in for arecord -------------------------------------------------------------------------------------------------------------

class Voice:
    """A saw through two one-poles: its pitch is the note, its brightness is the cutoff; zero (exactly) at rest."""

    def __init__(self, rate):
        self.rate = rate
        self.f = 0.0
        self.phase = 0.0
        self.env = 0.0
        self.held = False
        self.y1 = 0.0
        self.y2 = 0.0
        self.cut = DEFAULT_CUTOFF

    def apply(self, kind, a, b):
        if kind == "on":
            self.f = 440.0 * 2.0 ** ((a - 69) / 12.0)
            self.held = True
            self.env = 1.0
        elif kind in ("off",):
            self.held = False
        elif kind == "allq":
            self.held = False
            self.env = 0.0
        elif kind == "cutoff":
            self.cut = a
        elif kind == "reset":
            self.held = False
            self.env = 0.0
            self.cut = DEFAULT_CUTOFF

    def block(self, n):
        if self.env == 0.0 and not self.held:
            return array.array("h", bytes(2 * n))
        fc = 200.0 * 2.0 ** (self.cut / 127.0 * 5.3)
        a = 1.0 - math.exp(-2.0 * math.pi * fc / self.rate)
        out = array.array("h")
        inc = self.f / self.rate
        for _ in range(n):
            self.phase += inc
            if self.phase >= 1.0:
                self.phase -= 1.0
            saw = 2.0 * self.phase - 1.0
            self.y1 += a * (saw - self.y1)
            self.y2 += a * (self.y1 - self.y2)
            if not self.held:
                self.env *= 0.9996
                if self.env < 1e-5:
                    self.env = 0.0
                    self.y1 = self.y2 = 0.0
            out.append(int(self.y2 * self.env * 14000.0))
        return out


def shim(argv):
    rate, channels = 48000, 8
    it = iter(argv)
    for a in it:
        if a == "-r":
            rate = int(next(it))
        elif a == "-c":
            channels = int(next(it))
    path = os.environ.get("FAKE_SYNTH")
    if not path:
        sys.stderr.write("fake arecord: FAKE_SYNTH is not set\n")
        return 1
    voice = Voice(rate)
    pos = 0
    pending = []
    block = rate // 100
    out = sys.stdout.buffer
    t0 = time.monotonic()
    k = 0
    try:
        while True:
            try:
                with open(path, "r") as f:
                    f.seek(pos)
                    text = f.read()
                if text and not text.endswith("\n"):  # a line still being written waits for the next block
                    text = text[:text.rfind("\n") + 1]
                pos += len(text)
            except OSError:
                text = ""
            for line in text.splitlines():
                parts = line.split()
                if len(parts) == 4:
                    pending.append((int(parts[0]), parts[1], int(parts[2]), int(parts[3])))
            t_ns = int(time.monotonic_ns())
            while pending and pending[0][0] <= t_ns:
                _, kind, a, b = pending.pop(0)
                voice.apply(kind, a, b)
            mono = voice.block(block)
            frames = array.array("h", bytes(2 * block * channels))
            if channels >= 8:
                frames[6::channels] = mono
                frames[7::channels] = mono
            out.write(frames.tobytes())
            out.flush()
            k += 1
            wait = t0 + k * 0.01 - time.monotonic()
            if wait > 0:
                time.sleep(wait)
    except (KeyboardInterrupt, BrokenPipeError):
        return 0



# ---- the self-test -------------------------------------------------------------------------------------------------------------------------

READER = r"""
import os, select, sys, time, json
path = sys.argv[1]
fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
print("ready", flush=True)
stamps = []
deadline = time.monotonic() + float(sys.argv[2])
done = False
while not done and time.monotonic() < deadline:
    r, _, _ = select.select([fd], [], [], 0.05)
    if not r:
        continue
    try:
        data = os.read(fd, 4096)
    except BlockingIOError:
        continue
    t = time.perf_counter_ns()
    for b in data:
        if b == 0xF8:
            stamps.append(t)
        elif b == 0xFF:
            done = True
print(json.dumps(stamps))
"""


def run_self_test(mc):
    """Everything midi_check does that does not need a unit, and the whole tool against the fake unit. Exit 0, or 77 when this machine cannot do it (no pty, no o3-sdk tools)."""
    import random
    import shutil
    import statistics
    import subprocess
    import tempfile

    results = []

    def check(name, ok, detail=""):
        results.append((name, bool(ok)))
        print("  %s  %s%s" % ("PASS" if ok else "FAIL", name, (": " + detail) if detail else ""), flush=True)
        return ok

    print("midi_check self-test")
    try:
        os.close(os.openpty()[0])
    except OSError as error:
        print("SKIP: no pseudo-terminal here (%s)" % error)
        return 77
    try:
        mc.load_sdk_tool("o3console")
        mc.load_sdk_tool("o3audio")
    except mc.Refused as error:
        print("SKIP: %s" % error)
        return 77
    here = os.path.dirname(os.path.abspath(__file__))
    only_full = os.environ.get("MIDI_CHECK_SELFTEST_ONLY") == "full"          # development knobs: skip everything before the whole run,
    only_phases = os.environ.get("MIDI_CHECK_SELFTEST_PHASES")                # and run only these phases of it (comma list)
    work = tempfile.mkdtemp(prefix="midi_check_selftest_")
    servers = []

    def start_server(name, screen="PERFORM"):
        d = os.path.join(work, name)
        os.makedirs(d)
        p = subprocess.Popen([sys.executable, os.path.join(here, "midi_check_fake.py"), "--serve", d, "--screen", screen])
        servers.append(p)
        ready = os.path.join(d, "ready.json")
        for _ in range(200):
            if os.path.exists(ready):
                break
            time.sleep(0.05)
        else:
            raise RuntimeError("the fake unit did not start")
        with open(ready) as f:
            info = json.load(f)
        info["dir"] = d
        return info

    def dump(info):
        path = os.path.join(info["dir"], "dump.json")
        if os.path.exists(path):
            os.unlink(path)
        open(os.path.join(info["dir"], "dump.request"), "w").close()
        for _ in range(100):
            if os.path.exists(path):
                time.sleep(0.05)
                with open(path) as f:
                    return json.load(f)
            time.sleep(0.05)
        raise RuntimeError("no dump from the fake unit")

    try:
        if not only_full:
            # -- the parsers, on the console's own lines (platform/mk66/tests/mk66_console_midi_test.cpp and audio_status_test.cpp)
            print("parsers")
            text = ("midi: following no locked no tempo 0.00 bpm (loop 0.00 bpm, clocks 0, error 0 us)\n"
                    "midi: config channel omni out 1 ports din+usb clock-out on transport-out on transport-in on follow off\n"
                    "midi: rx 0 tx 0 dropped 0 messages 0 notes 0 held 0 polled-dropped 0 sent 0 bad-out 0\n"
                    "midi: clock-out 5 start-stop-out 2 transport-in 3 ignored 1 requests 0 attached 4 released 6\n"
                    "midi: din rx isrs 0 bytes 0 errors 0 overruns 0 full 0 multi 0 tx irqs 0 bytes 0 rt 0 late-max 0 us no-time 0 not-ready 0\n"
                    "midi: usb frames 7 events-out 8 events-in 9 bytes-in 10 discarded 11 no-time 12 in-full 13\n"
                    "midi: loop clocks 0 reacquires 0 dropouts 0 missing 0\nmidi: last rx\nmidi: last tx\n")
            d = mc.parse_midi(text)
            check("midi: config row", d["config"] == {"channel": 0, "out_channel": 1, "ports": "din+usb", "clock_out": True, "transport_out": True, "transport_in": True, "follow": "off"}, str(d.get("config")))
            check("midi: counters", (d["rx"], d["messages"], d["clock_out"], d["transport_events"], d["released"], d["usb_bytes_in"], d["usb_in_full"], d["din_errors"]) == (0, 0, 5, 3, 6, 10, 13, 0))
            check("midi: following and loop", d["following"] is False and d["locked"] is False and d["tempo_bpm"] == 0.0 and d["loop_clocks"] == 0)
            d = mc.parse_midi("midi: following yes locked yes tempo 128.01 bpm (loop 128.01 bpm, clocks 99, error 12 us)\n"
                              "midi: config channel 5 out 3 ports usb clock-out off transport-out on transport-in on follow usb\n"
                              "midi: rx 1 tx 1 dropped 0 messages 1 notes 1 held 0 polled-dropped 0 sent 1 bad-out 0\nmidi: last rx D:95 D:3C D:64\nmidi: last tx 2:92:3D:5A\n")
            check("midi: a channel config, last rx", d["config"]["channel"] == 5 and d["config"]["follow"] == "usb" and d["last_rx"] == ["D:95", "D:3C", "D:64"] and d["tempo_bpm"] == 128.01 and d["locked"])
            v = mc.cfg_view(d)
            check("cfg_view splits FOLLOW CLOCK and its port", v["follow_on"] is True and v["follow_port"] == "usb" and "follow" not in v)
            st = mc.parse_state("app=O3-303 screen=PATTERN_SETUP playing=1 tempo=128000 cpu=31 frames=900 sys=0 section=midi usb=P8 dup=1\n")
            check("state line", st["app"] == "O3-303" and st["tempo"] == "128000" and st["screen"] == "PATTERN_SETUP" and st["usb"] == "P8")
            check("state: no state line", mc.parse_state("ERR what\n") is None)
            cpu = mc.parse_cpu("audio  blocks 12345  renders 12340  underrun 2 (run 0, max 1)  mute 0 (events 0)  late 1  slips 0  hwfault 0\n"
                               "render last 3120  max 4100 cyc = 1.5 % of the block (261120 cyc)  over-budget 0\n"
                               "guard  shed 222 blocks in 72 runs  late 3  longest run since boot 4 blocks  n 32\n")
            check("cpu lines", cpu["underrun"] == 2 and cpu["late"] == 1 and cpu["render_max_pct"] == 1.5 and cpu["over_budget"] == 0 and cpu["shed_blocks"] == 222, str(cpu))
            check("crash text", mc.crash_is_empty("no crash record") and not mc.crash_is_empty("crash: hardfault at 0x1234"))
            check("32-bit counters wrap", mc.d32(3, 0xFFFFFFFE) == 5)

            # -- signals
            print("signals")
            rate = 48000
            rng = random.Random(3)
            for note in (36, 43, 48, 60):
                f = mc.note_hz(note)
                saw = [((i * f / rate) % 1.0 * 2.0 - 1.0) * 9000.0 + rng.uniform(-60, 60) for i in range(16384)]
                f0, conf = mc.autocorr_pitch(saw, rate)
                err = abs(mc.cents(f0, f)) if f0 else 999.0
                check("pitch of a saw at note %d (%.1f Hz)" % (note, f), err < 5.0, "%.2f cents off, peak %.2f" % (err, conf))
            sine = [10000.0 * math.sin(2 * math.pi * 110.0 * i / rate) for i in range(16384)]
            f0, _ = mc.autocorr_pitch(sine, rate)
            check("pitch of a sine at 110 Hz", f0 is not None and abs(mc.cents(f0, 110.0)) < 3.0)
            check("no pitch in noise", mc.autocorr_pitch([rng.uniform(-8000, 8000) for _ in range(16384)], rate)[0] is None)
            check("pitch of silence is None", mc.autocorr_pitch([0.0] * 16384, rate)[0] is None)
            half = [16384.0 * math.sin(2 * math.pi * 1000.0 * i / rate) for i in range(48000)]
            check("rms of half scale", abs(mc.rms_db(half) - (-9.03)) < 0.05, "%.2f dBFS" % mc.rms_db(half))
            check("rms of nothing", mc.rms_db([0] * 100) < -100)
            gated = [0.0] * 24000 + half[:24000] + [0.0] * 24000
            env, n = mc.envelope_db(gated, rate, 10)
            onset = mc.first_onset(env, n / rate, -55.0)
            check("onset of a gated tone", onset is not None and abs(onset - 0.5) < 0.011, "%.3f s" % (onset or -1))
            end = mc.last_loud(env, n / rate, -60.0)
            check("end of a gated tone", end is not None and abs(end - 1.0) < 0.011, "%.3f s" % (end or -1))

            def lowpassed(fc, f=65.0):
                a = 1.0 - math.exp(-2 * math.pi * fc / rate)
                y1 = y2 = 0.0
                out = []
                for i in range(16384):
                    x = (i * f / rate) % 1.0 * 2.0 - 1.0
                    y1 += a * (x - y1)
                    y2 += a * (y1 - y2)
                    out.append(y2 * 12000.0)
                return out
            dull = mc.brightness_db(lowpassed(250.0)[4000:], rate)
            bright = mc.brightness_db(lowpassed(8000.0)[4000:], rate)
            mid = mc.brightness_db(lowpassed(1500.0)[4000:], rate)
            check("brightness orders the cutoffs", dull + 10.0 < mid < bright - 3.0, "%.1f, %.1f, %.1f dB for 250, 1500 and 8000 Hz" % (dull, mid, bright))

            # -- the clock-out analysis on synthetic stamps
            print("clock-out analysis")
            for bpm in (120.0, 174.0):
                period = 60e9 / (24 * bpm)
                stamps = [int(1e9 + math.floor((k * period) / 1e6) * 1e6) for k in range(int(60e9 / period))]  # frame-quantised
                a = mc.analyze_clock_out(stamps, bpm)
                check("clean %g BPM minute" % bpm, abs(a["err_pct"]) < 0.01 and a["missing"] == 0 and a["extra"] == 0 and a["interval_stdev_ms"] < 0.51 and a["interval_dev_max_ms"] <= 1.0,
                      "err %+.4f %%, stdev %.3f ms, max %.2f ms, missing %d, extra %d" % (a["err_pct"], a["interval_stdev_ms"], a["interval_dev_max_ms"], a["missing"], a["extra"]))
                bad = list(stamps)
                for k in (400, 1000, 1500):
                    del bad[k - (0 if k == 400 else 1 if k == 1000 else 2)]
                bad.insert(701, bad[700] + 3_000_000)    # a clock 3 ms after another
                bad.insert(1802, bad[1801] + 2_000_000)  # and one 2 ms after another
                a = mc.analyze_clock_out(bad, bpm)
                check("3 missing and 2 extra clocks found at %g BPM" % bpm, a["missing"] == 3 and a["extra"] == 2, "missing %d, extra %d" % (a["missing"], a["extra"]))
                stalled = list(stamps)
                stalled[900] = stalled[899] + int(1.7 * period)   # this computer's read slept: one clock arrives 1.7 periods late and the next one with it
                stalled[901] = stalled[900] + 20_000
                a = mc.analyze_clock_out(stalled, bpm)
                check("a host read stall is not a lost clock at %g BPM" % bpm, a["missing"] == 0 and a["extra"] == 0 and a["host_stalls"] == 1, "missing %d, extra %d, stalls %d" % (a["missing"], a["extra"], a["host_stalls"]))
                skew = [int(s * (1.0 + 300e-6)) for s in stamps]  # a clock 300 ppm slow
                a = mc.analyze_clock_out(skew, bpm)
                check("a 300 ppm error is seen at %g BPM" % bpm, abs(a["err_pct"] + 0.03) < 0.005 or abs(a["err_pct"] - 0.03) < 0.005, "%+.4f %%" % a["err_pct"])
            period = 60e9 / (24 * 120.0)
            sent = [int(k * period) for k in range(1200)]
            recv = [int(s + 800_000 + (s / 1e9) * 5_000 + rng.uniform(-100_000, 100_000)) for s in sent]
            o = mc.offset_drift(sent, recv, period)
            check("phase offset and drift", o is not None and abs(o["median_ms"] - 0.8) < 0.5 and abs(o["drift_ms"] - 0.005 * 1200 * period / 1e9 * 1.0) < 0.05 and o["stdev_ms"] < 0.1,
                  "median %.3f ms, drift %.4f ms, scatter %.3f ms" % (o["median_ms"], o["drift_ms"], o["stdev_ms"]) if o else "none")
            recv_late = [int(s + 800_000 + period) for s in sent]
            o = mc.offset_drift(sent, recv_late[1:], period)
            check("pairing shift, an index off", o is not None and o["shift"] in (-1, 0, 1, 2, -2) and 0.0 <= o["median_ms"] < period / 1e6, "shift %d, median %.2f ms" % (o["shift"], o["median_ms"]) if o else "none")

            # -- the clock sender, measured on this machine
            print("clock sender timing (a reader in another process stamps the arrivals)")
            for bpm, secs in ((120.0, 8.0), (300.0, 4.0)):
                fifo = os.path.join(work, "timing_%d" % int(bpm))
                os.mkfifo(fifo)
                reader = subprocess.Popen([sys.executable, "-c", READER, fifo, str(secs + 6.0)], stdout=subprocess.PIPE, text=True)
                reader.stdout.readline()  # ready
                wfd = os.open(fifo, os.O_WRONLY)
                events = list(mc.merge_events(mc.clock_events([("tempo", bpm, secs)], t_start_ns=mc.sec_ns(0.02)), [mc.at(secs + 0.2, b"\xFF", "x", 0)]))
                sender = mc.Sender(lambda b: os.write(wfd, b), events).go()
                sender.join(secs + 5.0)
                os.close(wfd)
                out, _ = reader.communicate(timeout=10)
                arrivals = json.loads(out.strip().splitlines()[-1])
                targets = [sender.t0 + t for t in sender.rec_target]
                n_clk = sender.count("clk")
                late = sender.lateness_us("clk")
                dev = [(a - t) for a, t in zip(arrivals, targets[:len(arrivals)])]
                med = statistics.median(dev)
                res = [(d - med) / 1e3 for d in dev]
                check("sender at %g BPM: %d clocks sent, %d read" % (bpm, n_clk, len(arrivals)), n_clk == len(arrivals) and n_clk > 0)
                print("        lateness (written - due): p50 %.0f us, p99 %.0f us, max %.0f us  [%s]" % (late[1], late[2], late[3], sender.sched or "normal"))
                print("        arrival jitter at the reader (after the fixed offset of %.0f us): stdev %.0f us, worst %.0f us" % (med / 1e3, statistics.pstdev(res), max(abs(r) for r in res)))
                check("sender at %g BPM: sane (median lateness under 2 ms, worst arrival under 20 ms)" % bpm, late[1] < 2000.0 and max(abs(r) for r in res) < 20000.0)
                mc_timing = {"bpm": bpm, "p50_us": late[1], "p99_us": late[2], "max_us": late[3], "arrival_stdev_us": statistics.pstdev(res), "arrival_worst_us": max(abs(r) for r in res)}
                with open(os.path.join(work, "timing.json"), "a") as f:
                    f.write(json.dumps(mc_timing) + "\n")

            # -- the refusal: a wrong version sends nothing but `version`
            print("refusal")
            info = start_server("refuse")
            ap = mc.build_parser()
            out_dir = os.path.join(work, "out_refuse")
            args = ap.parse_args(["--expect-version", "somebody-elses-build", "--port", info["console"], "--midi-dev", info["midi_in"], "--out", out_dir])
            code = mc.run(args)
            sent = [l.strip() for l in open(os.path.join(info["dir"], "commands.log")).read().splitlines() if l.strip()]
            check("wrong version: refused (exit 2)", code == 2, "exit %s" % code)
            check("wrong version: only `version` was sent", sent == ["version"], str(sent))
            before = dump(info)
            check("wrong version: the unit is as it was", before["cfg"]["channel"] == 4 and before["screen"] == "PERFORM" and not before["playing"])
            args = ap.parse_args(["--expect-version", "fake0001", "--port", os.path.join(work, "no_such_port"), "--out", out_dir])
            check("a missing console port is a refusal (exit 2), not a crash", mc.run(args) == 2)

            # -- a failure in the middle of a phase: the restore puts the unit back
            print("restore after a failure in the middle of a phase")
            info = start_server("boom")
            start_cfg = dump(info)

            def boom(ctx):
                ctx.panel.configure(channel=9, clock_out=True, transport_out=True, follow_on=True, follow_port="usb", ports="usb")
                ctx.panel.set_transport(True)
                ctx.panel.goto_screen("MASTER")
                ctx.panel.set_own_tempo(150000)
                raise RuntimeError("injected failure")

            mc.PHASES["boom"] = boom
            out_dir = os.path.join(work, "out_boom")
            args = ap.parse_args(["--expect-version", "fake0001", "--port", info["console"], "--midi-dev", info["midi_in"], "--midi-out-dev", info["midi_out"], "--no-audio", "--phases", "boom", "--out", out_dir])
            code = mc.run(args)
            end_cfg = dump(info)
            del mc.PHASES["boom"]
            check("boom: the run reports the failure (exit 1)", code == 1, "exit %s" % code)
            check("boom: Config restored (all eight rows, the hidden FOLLOW PORT too)", end_cfg["cfg"] == start_cfg["cfg"], "%s -> %s" % (start_cfg["cfg"], end_cfg["cfg"]))
            check("boom: screen, transport and tempo restored", (end_cfg["screen"], end_cfg["sys"], end_cfg["playing"], end_cfg["own_mbpm"]) == (start_cfg["screen"], start_cfg["sys"], start_cfg["playing"], start_cfg["own_mbpm"]),
                  "%s" % [(end_cfg["screen"], end_cfg["sys"], end_cfg["playing"], end_cfg["own_mbpm"])])
            rep = json.load(open(os.path.join(out_dir, "report.json")))
            check("boom: the report has the restore phase, passed", any(p["name"] == "restore" and p["status"] == "PASS" for p in rep["phases"]))
            # the same from a file: --restore-from after a Config change by hand
            info2 = start_server("restore_from")
            start2 = dump(info2)
            os.makedirs(os.path.join(work, "out_rf0"))
            shutil.copy(os.path.join(out_dir, "start_state.json"), os.path.join(work, "out_rf0", "start_state.json"))
            # change the unit (by the console, as a person would), then put it back
            console = mc.load_sdk_tool("o3console")
            unit = mc.Unit(info2["console"], console)
            unit.open()
            panel = mc.Panel(unit)
            panel.configure(channel=11, clock_out=True)
            unit.close()
            args = ap.parse_args(["--expect-version", "fake0001", "--port", info2["console"], "--midi-dev", info2["midi_in"], "--restore-from", os.path.join(work, "out_rf0"), "--out", os.path.join(work, "out_rf")])
            code = mc.run(args)
            check("--restore-from puts the Config back from the start file", code == 0 and dump(info2)["cfg"] == start2["cfg"], "exit %s" % code)

        # -- the whole tool against the fake unit
        print("the whole tool against the fake unit (short durations, fake audio)")
        shim_path = os.path.join(work, "arecord")
        with open(shim_path, "w") as f:
            f.write("#!/bin/sh\nexec %s %s --shim \"$@\"\n" % (sys.executable, os.path.join(here, "midi_check_fake.py")))
        os.chmod(shim_path, 0o755)
        ap = mc.build_parser()
        info = start_server("full")
        start_cfg = dump(info)
        os.environ["FAKE_SYNTH"] = info["synth"]
        saved = {k: getattr(mc, k) for k in ("SILENCE_WAIT_S", "PITCH_NOTES", "TEMPO_PROFILE", "SYSEX_COUNT", "LEARN_GAP_S", "SOAK_SAMPLE_S", "STORM_NOTES", "RAMP_S", "INTERVAL_STDEV_MS", "INTERVAL_MAX_DEV_MS")}
        mc.SILENCE_WAIT_S = 1.6
        mc.PITCH_NOTES = (36, 48, 60)
        mc.TEMPO_PROFILE = (90, 174, 300)
        mc.SYSEX_COUNT = 3
        mc.LEARN_GAP_S = 0.9
        mc.SOAK_SAMPLE_S = 4.0
        mc.STORM_NOTES = 800
        mc.RAMP_S = 8.0
        # The jitter limits of the clock-out phase are the unit's USB floor plus this machine's wake-up latency: here the machine is shared with builds and with the fake's own
        # processes, so the self-test checks the phase's logic with loose limits; the sender's and the reader's measured timing is printed above, not asserted to microseconds.
        mc.INTERVAL_STDEV_MS = 2.0
        mc.INTERVAL_MAX_DEV_MS = 15.0
        out_dir = os.path.join(work, "out_full")
        args = ap.parse_args(["--expect-version", "fake0001", "--port", info["console"], "--midi-dev", info["midi_in"], "--midi-out-dev", info["midi_out"], "--audio-device", "hw:9,0", "--audio-channels", "8",
                              "--arecord", shim_path, "--learn-baseline", "none", "--tempo-hold", "3", "--jitter-seconds", "4", "--long-seconds", "6", "--clockout-seconds", "4", "--flood-seconds", "2",
                              "--soak-seconds", "12", "--out", out_dir] + (["--phases", only_phases] if only_phases else []))
        t0 = time.monotonic()
        code = mc.run(args)
        elapsed = time.monotonic() - t0
        for k, v in saved.items():
            setattr(mc, k, v)
        rep = json.load(open(os.path.join(out_dir, "report.json")))
        for p in rep["phases"]:
            fails = [c for c in p["checks"] if c["ok"] is False]
            skips = [c for c in p["checks"] if c["ok"] is None]
            check("full run, phase %s: %s (%d checks)" % (p["name"], p["status"], len([c for c in p["checks"] if c["ok"] != "info"])), p["status"] == "PASS",
                  "; ".join("%s [%s]" % (c["name"], c["value"]) for c in (fails + skips)[:4]))
        check("full run: exit 0", code == 0, "exit %s after %.0f s" % (code, elapsed))
        end_cfg = dump(info)
        check("full run: the fake unit's Config, screen, tempo and transport are the start's", (end_cfg["cfg"], end_cfg["screen"], end_cfg["sys"], end_cfg["playing"], end_cfg["own_mbpm"]) ==
              (start_cfg["cfg"], start_cfg["screen"], start_cfg["sys"], start_cfg["playing"], start_cfg["own_mbpm"]), "%s" % ((end_cfg["cfg"], end_cfg["screen"], end_cfg["own_mbpm"]),))
        check("full run: no binding left", end_cfg["binding"] is None)
        check("full run: the files are there", all(os.path.exists(os.path.join(out_dir, n)) for n in ("report.json", "summary.md", "start_state.json", "console_transcript.txt")))
        allowed = set(mc.ALLOWED_COMMANDS)
        used = {l.split()[0] for l in open(os.path.join(info["dir"], "commands.log")).read().splitlines() if l.split()}
        check("full run: only the allowed console commands were sent", used <= allowed, str(sorted(used - allowed)))
    finally:
        for p in servers:
            p.terminate()
        for p in servers:
            try:
                p.wait(5)
            except subprocess.TimeoutExpired:
                p.kill()
        shutil.rmtree(work, ignore_errors=True)
    failed = [n for n, ok in results if not ok]
    print("")
    print("self-test: %d checks, %d failed" % (len(results), len(failed)))
    for n in failed:
        print("  FAILED: " + n)
    return 1 if failed else 0


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--serve":
        screen = "STEPS"
        if "--screen" in sys.argv:
            screen = sys.argv[sys.argv.index("--screen") + 1]
        sys.exit(serve(sys.argv[2], screen))
    if len(sys.argv) >= 2 and sys.argv[1] == "--shim":
        sys.exit(shim(sys.argv[2:]))
    print(__doc__)
    sys.exit(2)
