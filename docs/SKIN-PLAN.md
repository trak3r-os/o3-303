# o3-303: the obsidian skin (plan)

Status: planned, 2026-10-30. The maintainer approved the obsidian look (mockup A as HOME, B as INSTRUMENT) and asked on 2026-10-30 to queue
the skin and the o3-sdk changes it needs. UX-303 section 15 says what the skin is: a new drawing of the same view structs, with no change to
the interaction. This plan says what each screen draws, what it costs, the kit contracts it needs (UX-PLAN section 7: N4 to N7, plus N0 and N9),
how the art gets into the image, and the slices. The rule over all of it is the maintainer's: "todo lo más simple, intuitivo y divertido posible".

Sources: the skin spike (`gen_skin.py`, `NOTES.md`, `budget.json`, mockups A and B), the redesign's mockups (`gen_ux.py`, `01_home` to
`09_master`), the FT812 fill-rate run (`docs/hw/2026-10-30-fillrate.md`), and the kit as it is at o3-sdk `8156b09`. Every number marked
"modelled" comes from the spike's generator, rerun with a counter of primitives per scan line and with every label drawn as live text
(section 4). Numbers marked "sim" come from `build-host/app/o3_303_sim` on today's tree.

## 1. Rules

- UX-PLAN section 2 applies unchanged: stay in your files, build in `build-skin-<slice>-host`/`-mk66` under `nice -n 10`, never flash,
  do not commit, one firmware-reviewer per slice, ctest green before stopping, 0 warnings, no heap, no `double`, `O3SDK_BIG` for big buffers.
- **One lane.** One engineer at a time on the skin, because every slice touches `app/src/skin/` and the scenarios test. SG1 (`app/seq`
  only) may run beside it. SG2 slots in after SK3 (section 8).
- **The skin replaces the drawing, not the logic.** A screen fills its view struct exactly as today. When the skin is active, `draw()` calls
  the skin's function for that view; otherwise it calls today's. No input, LED, pad or sound path changes. **Every WAV stays byte-identical
  in every slice.**
- **Switch and fallback.** Config page "303" gets key 7, SKIN, with CLASSIC (0) and OBSIDIAN (1). Its default stays CLASSIC until SK8, so
  every existing scenario's PNG stays byte-identical through SK1 to SK7. Any screen whose skinned frame breaks a budget of section 4 keeps
  today's layout, with the kit chrome, drawn in the obsidian colours. The fix is one line (`chrome()` returns `Full`), and the screen's skin
  code is kept for later. If the app assets fail to upload, the whole app runs classic.
- o3-sdk is never edited from here. Section 5 is done there, under its rules, as one batch: N0 and N4 before SK1, the rest before SK2.
- **Ported kit code stays identical to track3r-os** (o3-sdk CLAUDE.md). The Renderer, `assets.h`, `theme.h` and the rest of the ported
  `ui` and `eve` are not edited. Every kit step diffs its files against track3r-os first, and does not edit a file that is identical (section 5).

## 2. The look

### 2.1 The frame every skinned screen shares (800 x 480, no kit chrome)

| Element | Where (px) | Drawn as | Shows |
|---|---|---|---|
| Panel | header y 0-66, F row y 384-480 (INSTRUMENT: full height) | CLEAR to the glass colour, base RECTS, the `tex_obsidian` tile (L4, REPEAT in x, tinted) as two bands, two translucent sheen LINES scissored to the header | nothing; the material |
| Brand | x 12-162, y 7-57 | the `logo` sprite (the "O3-303" wordmark), with the tagline as live 6x12 text under it | "ACID BASS VOICE"; red "SAVE FAILED" while the last autosave failed (`StatusView::save_failed`) |
| PATTERN display | x 168-248 | window plus three `seg` sprites | the selected pattern, "A01" |
| TEMPO display | x 256-358 | window plus four `seg` sprites, the decimal point as a POINT, "BPM" | the engine's tempo ("125.0"; a leading blank is a ghost 8); the master's tempo while following a MIDI clock |
| TRANSPORT | x 368-424 | `led` sprites plus glow POINTS; live 6x12 captions | PLAY green while playing; REC red in write/record mode (`rec`); TAKE amber with the take as its caption ("ARMED" blinking, "0:05" red, "READY", "SAVING") |
| KEYBOARD | x 432-490 | `led` sprites, captions, "OCT 2-3" | KEYS lit (plays) or TRNSP lit (transposes); both dark when the pads are not a keyboard (`keys` false); the octave |
| PERFORM | x 498-612 | four `led` sprites and captions | dark: the four colour buttons' assigned actions (PERFORM SETUP). An action that is on lights a slot, taken from the left, using `draw_action_chips`' rule: as many as fit, the last one "+N" |
| PAGE window | x 620-792, y 6-28 | rounded window, 8x16 text | `Screen::title()`; a toast or take message in amber for its life (an outline in red when it says something did not happen) |
| ENC window | x 620-792, y 36-58 | rounded window, cyan 8x16 text | `StatusView::enc` ("F1 CUTOFF", "MORPH 1>2 50%", "LIST") |
| Glass | y 66-384 (x 5-795) | the frame's CLEAR colour, a lip and face (two round rects) | the screen's content, drawn by today's functions in the obsidian theme (section 5, N7 `Screen::theme`) |
| PADS line | the glass's last 16 px | live 6x12 text | today's sentence (`draw_pads_line`); with PAD LEGEND off it is drawn empty |
| F row | y 384-480, eight 100 px slots | section 2.3 | `view::Knob[8]` (or the Shift row's labels) |

Text is always live, in the kit's four faces (Terminus 16x32 and 10x20, Spleen 8x16 and 6x12): every label, value, caption and message, the
silkscreen included. It is drawn after the shapes by `Renderer::text()`, in the obsidian theme's colours (N6, N7). Sprites are shapes only: the texture, the wordmark, the 7-segment cells, knob parts, LEDs, step keys, PERFORM lamps and
action keys. Primitives: windows (round rects), rules, sheen, knob pointers, glows, meter bars.

### 2.2 StatusView in the header (no status line under the skin)

| `StatusView` field | Skin |
|---|---|
| `enc` | ENC window |
| `keys`, `trnsp`, `octave` | KEYBOARD LEDs and "OCT 2-3" |
| `rec` | REC LED |
| `take`, `take_msg`, `take_msg_off` | TAKE LED and caption; the message in the PAGE window for its 2.5 s |
| `actions` | PERFORM LEDs (2.1) |
| `toast`, `toast_off` | PAGE window, amber; outlined red when `_off` |
| `save_failed` | the tagline turns red "SAVE FAILED" |

HOME's own status (LEN, SW, GATE, TR, SCALE, NEXT and the STEP box) stays a line at the top of the glass, as in mockup 01.

### 2.3 The F row: one slot kind per key (a pure function, host-tested)

| `view::Knob` | Slot | Drawn as |
|---|---|---|
| `blank` | empty box | window with a dim "Fn" |
| `norm < 0` (an action: NEW, TAP, LOAD, ARM ...) | action key | an `actkey` sprite with its label in the key; `value` (if any) as the hint under it |
| `toggle` (new flag: a two-place list such as WAVE SAW/SQR) | switch | rounded slide switch, both names as captions, the value under it |
| otherwise | knob | `knob42` scale, body and cap, the pointer as one LINE at `norm` over 300 degrees, the latch LED, the readout window |

Every slot has the label line "Fn NAME": a red tab with "Fn", then the name in 8x16 live text. The states:

- `focus` (the encoder's knob): the LED is lit and the readout is filled red;
- `held`: an amber frame on the readout;
- `locked`: the value in the accent colour.

While Shift is held, the row is the screen's Shift row (the same SoftKeys labels as today), drawn as action keys at once (UX-303 2.4).

### 2.4 Screen by screen

| Screen | View today | Main area (skin) | F row | Notes |
|---|---|---|---|---|
| HOME (Pattern) | `MainScreen`: `HomeMap` status, `view::Marks`, `draw_steps`, `draw_motion`, `fill_knob` | glass: info line plus STEP box; step numbers (cursor blue, playhead green); NOTE lane; GATE, PLAYS and LOCK rows with captions in the buttons' colours (NOTE teal, GATE yellow, PLAYS purple, LOCK cyan; the owned row filled); the motion lane; PADS line | the sound knobs; WAVE a switch | Mockup 01. The NOTE lane is about 26 px shorter than today (`lane_height` adapts). "EMPTY PATTERN" text as today |
| HOME, Shift held | same | same, plus a red "SHIFT > PATTERN TOOLS, ALL UNDO" banner where the STEP box is | eight action keys: NEW VARY RHYTHM NOTES REVERSE CLEAR REDO UNDO, with hints | Mockup 03 |
| HOME, Pattern held | `draw_grid` | today's A01..D12 grid in the glass, obsidian colours | the sound knobs | Mockup 04's layout |
| PATTERN SETUP | `MainScreen` page 2 | HOME's glass with the generator line on top (`draw_info_generator`) | LENGTH SWING GATE TRANSP SCALE DENSITY ACC % SLD % as knobs | Shift row: COPY PAT, PASTE PAT, DUP>NEXT, CLEAR LOCKS, UNDO as action keys, the rest empty boxes |
| PERFORM | `PerformView` (48 `PadLamp`, captions) | four rows of twelve `lamp` sprites, tinted by the pad's LED level (off, dim, mid, on) with a frame when latched, labels live; row captions in small caps (ACTIONS, SNAPSHOTS, LADDER <knob> or WOBBLE RATE, KEYS <mode>); the colour-buttons line (four chips in the buttons' colours with their actions); PADS line | the sound knobs | Mockup 05's layout. The mockup's raised lamps (four primitives each) cost 1,773 words modelled, so they are one sprite each here |
| PERFORM SETUP | `PerformScreen` setup page | the row-1 reminder and the panel text, in the glass | its eight knobs (NOTE, INSTR, VOL, FX, FILL, FREEZE, ROLL, STOP) | |
| INSTRUMENT VOICE / FX / MODS | `InstrumentView` | Mockup 06: no glass, the panel at full height. Top: the page's eight knobs as `knob58`, with labels, latch LEDs and readouts. Middle: the section boxes (DISTORTION, DELAY, MODS) with their readouts, from `InstrumentSection`. Then the step strip of the page: note text, ACCENT, SLIDE and STEP LEDs, a `key` sprite each (cursor ringed, groups of four tinted), step numbers. Bottom: page tabs VOICE FX MODS, PADS line | the top knobs are the F row | Shift: each label reads LEARN (lit while armed) or "-" (dim), as C1 built it. A held step shows its lock in the knob's readout (`apply_lock`) |
| MASTER | `MasterView` | glass: TEMPO as a second `seg` display with BPM; NUDGE, TAP and CLOCK readouts; OUTPUT L and R as LED-segment bars (RECTS, green to amber); CPU % (`Context::cpu_percent`, N7; "--" in the simulator); USB | F1 TEMPO knob, F2 TAP action key, F3 NUDGE knob, F4 MASTER knob, F5-F8 empty | Mockup 09 |
| FILE PROJECTS | `FileView` | glass: the path, NOW and AUTOSAVED line, the list (cursor row highlighted, "*" current, NO CARD), today's help text, PADS line; NameEntry (SAVE AS, NEW) is the kit's keyboard drawn in the glass with the obsidian theme | LOAD SAVE SAVE AS NEW REVERT DELETE action keys, F7 empty, F8 LIBRARY | Mockup 08, with F7 empty as UX-303 D17 says |
| FILE LIBRARY | `VaultScreen` | **today's layout with the kit chrome** (`Chrome::Full`), in the obsidian theme | the kit's soft-key row | The fallback: skinned it would be about 1,700 to 1,800 words (section 4.3). Revisit after SU1 |
| RECORD | `RecordView` | glass: SOURCE, BARS and the take's state as readouts; IN and OUT as LED-segment bars; the envelope as drawn today; the file name `Samples/take_NNN.wav` | F1 ARM/STOP action key (lit red while recording), F2 SOURCE and F3 BARS action keys with their value as hint, F4 GAIN knob, F5 MONITOR action key, F6 SAVE, F7 DISCARD, F8 empty | The take also shows on TRANSPORT's TAKE LED on every screen |
| HOOKS | `HooksScreen` | the 4 x 12 grid as dark `lamp` sprites, the one line of text | eight empty boxes | |
| SONG (SG2, to come) | new `SongView` | Mockup 07: list columns # / PATTERN / REPEAT / TRANSP / MUTE with chip headers in the buttons' colours; the cursor row highlighted, the playing row with a green bar; a right panel with SONG, LENGTH, LOOP and PLAY readouts and help; PADS line | F1 PLAYS (hint SONG or PATTERN), F2 LOOP (hint on/off), F3 FROM CHAIN, F4-F6 empty, F7 CLEAR (hint "tap twice"), F8 UNDO, all action keys | SG2 builds both drawings from its first commit |
| Config | the kit's system screen | **unchanged**: the runtime always draws Config with its full chrome and the context theme (N7) | the kit's | The skin adds only the SKIN item (key 7) to page "303" |
| Boot screen | the kit's | unchanged | | |

## 3. Assets (obsidian only, shapes only)

| Asset | Format | Cell (px) x cells | Bytes | Used by |
|---|---|---|---:|---|
| `tex_obsidian` | L4 | 256 x 480 (drawn 800 wide, REPEAT x) | 61,440 | every skinned screen |
| `knob42_scale` / `_body` / `_cap` | L4 / L4 / L8 | 60² / 54² / 44² | 1,800 / 1,460 / 1,936 | F rows |
| `knob58_scale` / `_body` / `_cap` | L4 / L4 / L8 | 76² / 70² / 60² | 2,888 / 2,452 / 3,600 | INSTRUMENT |
| `key` | L8 | 42 x 36 x 3 (body, light, dark) | 4,536 | INSTRUMENT step strip |
| `seg` | L4 | 27 x 38 x 20 (0-9, A-H, "-", blank; ghost and glow in each cell) | 10,640 | PATTERN, TEMPO, MASTER |
| `logo` | L4 | 150 x 50 (the wordmark, no tagline) | 3,752 | header |
| `led` | L8 | 16 x 16 (socket, lens, specular; tinted by state) | 256 | every LED |
| `lamp` | L8 | 58 x 40 (a raised pad cap) | 2,320 | PERFORM, HOOKS |
| `actkey` | L8 | 84 x 40 (a raised key cap) | 3,360 | F-row actions |
| **Total** | | | **100,440** | |

All of these are tinted alpha masks (COLOR_RGB and COLOR_A), as in the spike. They need no new bitmap format: L4 and L8 are in kit/eve and
the simulator. PALETTED565 exists there too (the boot logo) but the skin does not use it, and the kit has no RGB565 or ARGB format.
Dropped from the spike: `tex_silver` (only obsidian was approved), `knob46`, `logoB`, and the baked label atlases `lab`, `sml` and `num`
(all text is live, decision D2).

## 4. Budgets

### 4.1 RAM_G (the app region, `kRamGApp` 0xD4000, `kRamGAppBytes` 180,160 B = 175.9 KiB)

| | Bytes | Of the region |
|---|---:|---:|
| Section 3 | 100,440 | 55.8 % |
| Free | 79,720 | 44.2 % |
| Ceiling for later art (SG2's and any later screen's) | 144,128 | 80 % |

The spike's set with both skins was 177,072 B (98 %). The redesign's mockups with baked labels and obsidian only were 136,388 B (75.7 %).
Nothing is re-uploaded at run time: the assets go up once at boot, and switching SKIN changes no RAM_G.

### 4.2 Flash

The assets are 100,440 B raw, about 38 KB if zlib-compressed, so compression is not worth a decompressor. The skin code is estimated at 20
to 30 KB. The image goes from 436,424 B (C1 tree; U5 was 432,956 B, 41.29 % of 1 MiB) to about 560 KB, **about 54 %**.

### 4.3 Display-list words per screen (the list holds 2,048; the scenarios test bounds every frame at 1,800)

Today's frame spends about 375 words on its chrome: begin 35, the kit header about 45, the status line about 45, the knob strip about 170 and
the soft keys about 80. The skin's frame spends about 625:
- begin 35;
- the header about 320 and the F row about 265, with every bind at six words;
- 2 to 5 for its one Canvas section (N6): SAVE and RESTORE, and the Renderer's colour and primitive again after it.

That is about **+250 words** on a screen with a knob row.

The modelled column is the spike's generator on the redesign's mockups, with live labels and corrected for the LED, lamp and action-key
sprites. Add about 25 to it for the binds and the section. The estimates include both, rounded up to 10.

| Screen | Today (sim peak, scenario) | Skinned (estimate) | Modelled | Bound |
|---|---:|---:|---:|---|
| HOME | 1,001 (`home_rows`) | 1,260 to 1,310 | 1,227 + motion lane about 50 | ceiling |
| HOME, Shift | 950 (`home_tools`) | 1,210 | 1,162 | ceiling |
| HOME, Pattern held | 981 (`patterns`) | 1,240 | 1,477 (the mockup's heavier cells) | ceiling |
| PATTERN SETUP | 1,003 | 1,260 | | ceiling |
| PERFORM | 1,057 (`perform_pads`) | 1,310 | 1,390 (lamp sprites) | ceiling |
| PERFORM SETUP | 947 | 1,200 | | ceiling |
| INSTRUMENT | 1,067 | 1,130 | 1,100 | ceiling |
| MASTER | 847 | 900 | 850 | ceiling |
| FILE PROJECTS (NameEntry) | 910 | 900 (1,170) | 870 | ceiling |
| FILE LIBRARY | 1,481 (`library`) | **1,481, classic** | skinned about 1,700 to 1,800 | 1,800 |
| RECORD | 952 | 1,210 | | ceiling |
| HOOKS | under 847 | 1,030 | | ceiling |
| SONG (SG2) | | 780 to 930 | 730 | ceiling |
| Config (kit) | 848 | 848 | | 1,800 |

**The ceiling for skinned frames: 1,350 words until SU1, then what SU1 measures.** The scenarios test checks it on every `skin_*.txt` frame.

- **Why not 1,600.** The only list length measured to flicker is 1,392 words (MIX K16 at cell 800, ballast 768). Its line is not the skin's
  kind of line. That line had 16 large primitives and 12,800 px, a third of them bilinear. The skin's busiest line has 80 to 100 small
  primitives and about 4,400 px. The run cannot say which costs the chip more time. A start value above 1,392 would rest on nothing.
- **Why 1,350.** It is below that first measured failure. It is also above every HOME frame's estimate (1,210 to 1,310), and HOME is all
  that SK2 and SK3 build. Today's longest frame (LIBRARY, 1,481 words) proves nothing either way: no session has looked for broken lines
  at that length.
- **After SU1.** The ceiling becomes `min(1,600, L - 200)`, where L is the longest clean list in SU1's ballast check (4.6) on the heaviest
  skinned frame. The cap keeps 200 words below the test's 1,800. SK4 writes the new value into the scenarios test. SU2 repeats the check on
  PERFORM and INSTRUMENT, the heaviest lines, and the lowest result stands.
- **Over the ceiling.** A screen above it falls back (section 1) or sheds words (4.6). Two screens are close:
  - **HOME** is at most 1,310, 40 words under 1,350. If it lands over, the smallest cut is the sheen and the tagline, about 25 words.
  - **PERFORM** is 1,310 estimated but about 1,415 modelled, which is over 1,350. The smallest cut is the colour-buttons line and the PADS
    line, about 100 words together. Both repeat what the header's PERFORM LEDs and the lamps already show. If PERFORM is still over
    after that, it falls back to classic.

### 4.4 Load on a scan line

The chip fetches a bitmap's whole drawn box on every line the box covers, transparent pixels included. So pixels are counted as boxes:

- the model counts each bitmap by its drawn width;
- RECTS are boxes;
- POINTS and LINES count as their bounding boxes, which is the conservative choice.

| | Measured on the unit (fill-rate run; ballast 0 unless noted) | Skin (modelled, live labels) |
|---|---|---|
| Primitives on the busiest line | L8 at cell 100: clean at 208 (20,800 px; 216 flickered). L8 at cell 50: clean at 224 (11,200 px). MIX (a third bilinear): clean at 128, at both cell 100 and cell 50 | 79 on HOME's NOTE lane (row 283); header 64; F row 66; INSTRUMENT 72; PERFORM 67 |
| Box pixels on the busiest line | MIX: clean at 6,400 at cell 50; at 18,400 at cell 800 (ballast 256) | 4,208 (mockup A's knob row); INSTRUMENT 4,430 |
| Budget | | **at most 100 primitives and 4,800 box px**, on any one line. That is 48 % of L8's 208 and 78 % of MIX's 128; 75 % of MIX's 6,400 px at cell 50 |
| Bilinear | about 4 times as costly | none: the API has no filter option (N6) |

**The limit has two axes.** L8 stopped at 208 primitives with 20,800 px at cell 100, and at 224 with only 11,200 px at cell 50. Small
cells are bound by the count, large ones by the pixels. The skin's cells are small, so it is designed in primitives first:

- one sprite per LED, lamp and key;
- three tight knob parts;
- one 7-segment cell per character;
- on glass screens, texture only in the header and F-row bands.

**How the budget is checked.** With N9 (section 5) the simulator counts both axes, and the scenarios test holds every skinned frame to
them. Without N9, the table rests on the model.

**Not measured.** POINTS, LINES, the REPEAT band and COLOR_A were not in the fill-rate run. SU1 covers them.

### 4.5 CPU

- **Drawing (main loop):** about 50 to 60 cycles a word, 1,200 to 1,600 words a frame (the most the ceiling can become). That is 0.35 to 0.5 ms at 180 MHz, about 1 to 1.5 % of
  the main loop at 30 frames a second.
- **The skin's own work:**
  - the layer queue: at most 256 items in a static 4 KB array in SRAM, flushed in about 12 passes;
  - the knob pointers: a Q15 sine table, no libm, no `double`.
- **No audio-path cost:** the render (35 to 45 % of a block, `docs/hw/2026-10-26-m3c.md`) does not change.
- **The display list:** at most 6.4 KB a frame by DMA, at about 2.8 ms of bus time. No CPU.
- **Boot:** the 100 KB upload takes about 60 to 100 ms on the display link.

All of this is estimated, and SU1 measures it.

### 4.6 What is unproven, and the first unit check (SU1)

Unproven:

1. **The line time with the skin's real list.** The run shows that small cells are bound by the primitives on a line. It never combined a
   1,200 to 1,600-word list with lines of 80 to 100 small primitives. The one list length measured to flicker (1,392 words) carried a
   different kind of line: MIX K16 at cell 800. Nobody has looked for broken lines on today's longest frames either (LIBRARY, 1,481 words).
2. **POINTS, LINES, the REPEAT band and COLOR_A.** They draw the knob pointers, the glows, the decimal point, the texture drawn 800 wide
   with BITMAP_SIZE_H, and the sheen. The simulator models them. Nobody has timed or photographed them on the unit.
3. The RGB666 dither on obsidian's dark reds and blues, and banding in the 16-level texture.
4. Legibility at arm's length: the 6x12 silkscreen, the 8x16 F labels, the 7-segment cells.
5. Sprites on handle 0 past x = 511 (VERTEX_TRANSLATE_X). This is proven on the unit for glyphs only.
6. Boot time with the upload, and the draw time.
7. `diag shot` with handle 0 and the app region in use.

**SU1**, on SK3's image, with OBSIDIAN set in Config (or `skin on` on the console). Pass criteria:

| # | Check | Pass |
|---|---|---|
| 1 | HOME stopped, playing, with Shift held, Pattern held, a toast, TRNSP and REC on, for 30 s each | no broken, shifted or flickering line |
| 2 | `skin ballast N` (N/2 one-pixel RECTS at (0, 479), after the frame; the fill-rate app's method), raised 64 words at a time, on the skinned frame with the heaviest line (N9's count; Pattern held without N9) | record L, the longest clean total list. Pass: L ≥ 1,550 |
| 3 | `--shot` of HOME against the simulator's PNG | the same layout; knobs F6-F8 (x > 511) in place; as in the simulator: pointers (LINES), glows and the decimal point (POINTS), the texture band (REPEAT, SIZE_H), the sheen (COLOR_A). These are in every skinned frame, so checks 1 and 2 time them |
| 4 | Photos at arm's length | the silkscreen, labels and digits are read without effort |
| 5 | Dither and texture | no banding the maintainer objects to |
| 6 | Boot to the first HOME frame | under 0.3 s more than the classic image |
| 7 | `skin stats` | draw µs under 2,000 at the peak; `frames_busy` not higher than classic |
| 8 | Two minutes playing with knob turns | 0 underruns (`o3console.py cpu`) |
| 9 | Config over a skinned HOME; hold Power for the strip | the kit's Config intact; the strip at the bottom (over the F row's read-outs while it is up) |

**The gate.**
- **The ceiling.** It becomes `min(1,600, L - 200)` (4.3).
- **L under 1,550.** HOME sheds words to the new ceiling before SK4:
  - drop the sheen (about 10 words);
  - draw the texture as one band;
  - shorten the drawer's motion lane.

  A screen that cannot shed enough falls back (section 1).
- **Check 1 fails.** The per-line budget of 4.4 drops to 80 % of HOME's measured line load (N9), and HOME is trimmed the same way.
- **SK4 waits.** It starts only after SU1 is read.

## 5. o3-sdk contracts (done in o3-sdk under its rules; order N0, N9, N5, N4, N6, N7)

**The ported-code rule.** o3-sdk's CLAUDE.md says code ported from track3r-os stays identical to it, apart from the rename and the
documented cuts. That covers `kit/core`, `hal`, `eve`, `ui`, `usb`, `platform/host`, the MK66 drivers and the USB tools. These files are
byte-identical to track3r-os after the o3os to o3sdk rename (checked 2026-10-30):
- `kit/ui`: `renderer.{h,cpp}`, `assets.{h,cpp}`, `theme.h`, `style.h`, `font.h`, `plot/src/wave_view.cpp`;
- `sim`: `raster_display.{h,cpp}`;
- tests: `raster_test.cpp`, `raster_gfx_test.cpp`;
- `platform/host`: `fakes.h`.

So the kit work follows three rules:
- **No ported file changes.** Every step adds new files or edits o3sdk's own.
- **Diff before every edit.** Before editing any kit or simulator file, the engineer diffs it against `/home/anortes/Code/track3r-os`
  after the rename. A file that is identical is not edited. A need that would touch one goes to the maintainer as a finding.
- **Report, don't fix.** A defect found in ported code is reported, not fixed or worked around.

| Need | Files | Ported? |
|---|---|---|
| N0 | `sim/include/o3sdk/sim/eve_raster.h`, `sim/src/eve_raster.cpp` | no: they already differ from track3r-os (1 and 49 lines), and `sim` is not on the ported list |
| N0 | `sim/src/sim_main.cpp`; `tests/CMakeLists.txt` | own (sim_main has no counterpart; tests/CMakeLists.txt differs by 672 lines) |
| N0 | new `tools/digest_check.py`, new `tests/digests/<app>.txt`, new `tests/raster_digest_test.cpp` | new |
| N9 | `eve_raster.{h,cpp}`, `sim_main.cpp`, `tests/CMakeLists.txt`; new `tests/raster_line_load_test.cpp` | as N0; new |
| N5 | new `kit/ui/plot/include/o3sdk/ui/plot/canvas.h` (the constant); `eve_raster.{h,cpp}` (the counter); new `tests/handle_app_test.cpp` | new; as N0 |
| N4 | new `kit/app/include/o3sdk/app/app_assets.h`; `kit/app/include/o3sdk/app/app.h`; `runtime.h`, `runtime.cpp`, `context.h`; new `tests/app_assets_test.cpp` | new; own (app.h's track3r-os namesake differs by 629 lines; the rest have no counterpart) |
| N6 | `canvas.h`, new `kit/ui/plot/src/canvas.cpp`; `kit/ui/CMakeLists.txt` (one source line); new `tests/canvas_test.cpp` | new; o3sdk-modified (kit/ui/CMakeLists.txt already differs by 23 lines) |
| N7 | `kit/app/include/o3sdk/app/screen.h`, `runtime.h`, `context.h`, `kit/app/src/runtime.cpp`; new `tests/runtime_chrome_test.cpp` | own |
| docs | `docs/FRAMEWORK.md` | own |

| Order | Need | Size | Testable alone by | o3-303 needs it for |
|---|---|---|---|---|
| 1 | N0 frame digests in the simulator (new) | S | raster tests; today's digests pinned | the proof that every later step leaves the other apps' frames unchanged |
| 2 | N9 line load: primitives and box pixels per scan line (recommended; same files as N0, best landed with it) | S | raster tests; the fill-rate band counted exactly | the per-line check of SK2 onward |
| 3 | N5 the app's bitmap handle | S | static_asserts; a raster counter at zero over every pinned run | SK2 |
| 4 | N4 app asset upload | S | a fake display wrapper; pinned digests unchanged | SK1 |
| 5 | N6 `ui::plot::Canvas`: app bitmaps and shapes in a raw section | M | word and pixel tests | SK2 |
| 6 | N7 Screen chrome and theme, Context CPU | S | a runtime test with a fake screen; pinned digests unchanged | SK2 |
| 7 | `docs/FRAMEWORK.md`: kRamGApp, `assets()`, handle 0, Canvas, chrome and theme, digests, line load | S | review | |

**Compatibility for the kit's other apps.** These are hello, fillrate, crashtest, `templates/app` and the two logo apps in
`tests/logo_apps`. crashtest's FAULTS screen is an app screen with Full chrome; the kit has no crash screen of its own. No ported file
changes, and every default keeps today's behaviour:
- `assets()` returns none;
- `chrome()` is `Full` and `theme()` is null;
- no kit app uses Canvas.

Today no test pins a frame's words: the golden-frame tests named in a renderer.h comment are not in o3-sdk, and scenario PNGs are not
compared. N0 adds the proof, and every step of the batch keeps every pinned digest. The fillrate app keeps its own uploads into
`kRamGApp` and its handles 9-11. N3 (`kRamGApp`) already exists.

### N0: frame digests (sim: `eve_raster.{h,cpp}`, `sim_main.cpp`; tests)

```cpp
// eve_raster.h, class EveRaster: FNV-1a (32 bit) over the words of the last list render() was given, in order. Words, not pixels: they
// are what a kit change must not move. RasterDisplay (ported, unchanged) hands every committed list to render().
std::uint32_t last_frame_digest() const noexcept;
```

- **Output:** `<app>_sim` writes `digests.txt` into `--out`, one line per snap (`<snap> <digest>`, read through `Harness::raster()`),
  beside the PNGs.
- **Pinned:** a ctest per app runs that app's scenarios, and `tools/digest_check.py` compares `digests.txt` with a committed
  `tests/digests/<app>.txt`. The apps:
  - HELLO's scenarios, crashtest, fillrate, `logo_colour`, `logo_tint` and the template app;
  - the boot screen, whose digest `boot_screen_test` compares (that test already differs from track3r-os).

  A change that moves a digest fails until the file is updated in review. A frame showing a measured float that differs between the two
  CI machines is left out by name.
- **Host test:** the FNV of a known list; a list one word different has a different digest.
- **Unit:** none.

### N9 (recommended): line load per scan line (sim: `eve_raster.{h,cpp}`, `sim_main.cpp`)

- **API:** `peak_line_prims()` and `peak_line_px()` on `EveRaster`, plus `last_frame_` versions. They give the most primitives, and the
  most box pixels, on one scan line of a rendered list. What counts:
  - a bitmap counts its drawn box (BITMAP_SIZE, clipped to the scissor and the screen), not its ink, because the chip fetches the whole box;
  - a RECTS pair counts its box;
  - a POINT or a LINE counts its bounding box;
  - a REPEAT band counts once, at its drawn width.
- **Reporting:** `<app>_sim` prints "peak line N prims, M px" beside "peak list".
- **Host test** (a new file; `raster_test.cpp` is ported and stays as it is):
  - 10 overlapping glyphs on a row give 10 primitives and 10 boxes of pixels;
  - the fillrate band at (K, W) gives `sprites_per_line(K, W)` from `apps/fillrate/src/band.h` and K x 800 px, which ties the counter to the
    unit's results;
  - a 16 px bitmap drawn with SIZE_H left set by an 800-wide one counts 528 px. Canvas's six-word bind exists to prevent this.
- **Unit:** none: it is a count, not a timing. It turns 4.4's budget (both axes) into a host check, and gives today's frames a number.

### N5: bitmap handle 0 is the app's (new `canvas.h`; the counter in `eve_raster`)

```cpp
// kit/ui/plot/include/o3sdk/ui/plot/canvas.h. Bitmap handle 0 holds the app's bitmaps, in its RAM_G region (ram_g_map.h kRamGApp). The
// kit leaves it alone (assets.h says so and is not edited); Canvas::bind() is how an app uses it.
inline constexpr std::uint8_t kHandleApp = 0;
static_assert(kHandleApp != kHandleHexLo && kHandleApp != kHandleHexHi && kHandleApp != kHandleNotes && kHandleApp != kHandleIcons &&
              kHandleApp != kHandleWaveLane0 && kHandleApp != kHandleWaveLane1 && kHandleApp != kHandleBootLogo &&
              kHandleApp != kHandleNotesHi && kHandleApp != kHandleSmallHex && kHandleApp != kHandleSmallNotes && kHandleApp != 15,
              "the app's handle is none of the kit's, and not the coprocessor's");
```

- **The faces:** their handles (1..4) are generated, so a host test checks `font_face(f).handle != kHandleApp` for every face.
- **Raster check:** `EveRaster::handle_app_uses()` counts BITMAP_SOURCE, _LAYOUT[_H] and _SIZE[_H] words executed while handle 0 is current,
  and bitmaps drawn with handle 0. A ctest asserts zero over every N0-pinned run.
- **Unit:** on SU1 (sprites from handle 0 draw; `diag shot` works with it bound).

### N4: app asset upload (kit/app: new `app_assets.h`, `app.h`, `runtime.{h,cpp}`, `context.h`)

```cpp
// app_assets.h
struct AppAsset {
  std::uint32_t offset;       // from ui::plot::kRamGApp; a multiple of hal::kUploadAlign
  const std::uint8_t* bytes;  // alignas(4), in flash
  std::uint32_t size;         // > 0, a multiple of hal::kUploadAlign
};
// Each row inside [0, kRamGAppBytes), aligned, sizes > 0, no two overlapping. An app static_asserts its table with it.
constexpr bool app_assets_valid(const AppAsset* rows, std::size_t count) noexcept;

// app.h, class App: the bitmaps the runtime puts in the app's RAM_G region at boot. Points `rows` at a table that lives as long as the
// app and returns its length; asked once, by Runtime::start(). Default: none.
virtual std::size_t assets(const AppAsset*& rows) const noexcept { rows = nullptr; return 0; }

// context.h: true when the app has assets and every byte of them is in RAM_G. False with none, after a refusal, or when the kit's own
// upload failed.
bool app_assets_ok() const noexcept;
```

- **Upload path.** It runs in `Runtime::start()`, right after `ui::upload_assets()` (ported, unchanged) and before `App::setup()`, and
  only when the kit's upload succeeded.
  - It refuses a table that fails `app_assets_valid`. Nothing is uploaded, and a check aborts debug and host builds.
  - Each row goes up in chunks of `hal::kMaxUploadBytes` (4,096), straight from flash, to `kRamGApp + offset + k * 4096`. Every chunk
    starts on a 4 KB boundary of an aligned array, so the source and the address stay word-aligned.
- **No retry,** as in `upload_assets`. The first chunk that is not Ok stops the upload, and `app_assets_ok()` turns false. A retry would buy
  nothing at boot:
  - the MK66 `upload()` already waits up to `kUploadWaitMs` (70 ms) for the bus before each chunk;
  - nothing else uses the bus yet.

  So the most the app's assets can add to the boot is one such wait.
- **`start()` keeps returning only the kit's result.** A failed app upload never fails the boot. On the unit a false start holds the
  display dark and blinks `kAppFailure`, and the simulator exits 1. Instead, the app reads `app_assets_ok()` and runs without its bitmaps
  (o3-303 runs classic).
- **No re-upload,** the same as the kit's faces: a display re-initialisation loses both.
- **Simulator:** nothing new. `RasterDisplay` already takes uploads anywhere in RAM_G.
- **Host tests** `tests/app_assets_test.cpp`. They use a `hal::Display` wrapper defined in the test, around `platform::host::FakeDisplay`
  (ported; not edited). It logs each upload, and can turn `set_busy(true)` on after the kit's uploads:
  1. a 10,000 B row and a 4 B row arrive at their offsets in 3 + 1 chunks, each at most 4,096 B and aligned;
  2. busy after the kit's uploads: one app chunk tried and no more, `app_assets_ok()` false, `start()` true;
  3. the kit's upload failing: no app chunk tried, `start()` false as today;
  4. tables out of the region, misaligned, overlapping or of size zero: refused, nothing written;
  5. an app without assets: the same uploads as today, every N0 digest unchanged;
  6. `app_assets_valid` in static_asserts.
- **Unit:** on SU1 (textures intact, boot time).

### N6: `ui::plot::Canvas`, the app's bitmaps and shapes in a raw section (new `canvas.{h,cpp}`)

The Renderer is ported and frozen, so N6 is a new o3sdk layer on its public raw-section API: `begin_raw()`, `raw()`, `end_raw()`
(renderer.h, "Raw sections"). This is the same way `ui::plot`'s wave view draws today.

```cpp
struct BitmapDesc {                 // a bitmap in the app's RAM_G region, drawn through kHandleApp
  std::uint32_t ram_g;              // absolute RAM_G address, a multiple of 4
  eve::BitmapFormat format;         // L1, L2, L4 or L8
  std::uint16_t stride;             // bytes a row, < 4096
  std::uint16_t cell_w, cell_h;     // one cell; cells follow each other (stride * cell_h bytes)
  std::uint16_t draw_w, draw_h;     // drawn size, < 2048: the cell's, or wider with Wrap::Repeat
  eve::Wrap wrap_x = eve::Wrap::Border;
  eve::Wrap wrap_y = eve::Wrap::Border;
};
// True when `d` can be drawn: one of the four formats, aligned, the sizes in range, and Wrap::Repeat only on an axis whose cell size is a
// power of two. An app static_asserts its descs with it; Canvas::bind() checks it.
constexpr bool bitmap_desc_valid(const BitmapDesc& d) noexcept;

// Draws inside raw sections of `r`. Between open() and close() only the Canvas emits words: a Renderer drawing call there would draw with
// state the Canvas changed behind the Renderer's private caches (colour, primitive, origin). A check, like the raw-section one, aborts
// debug and host builds when r.words() moved between two Canvas calls. Text goes outside, through Renderer::text(). One Canvas lives for
// one frame's draw(). Its bound-desc cache lasts the frame: handle 0's properties are outside the graphics context, and only the Canvas
// writes them. Every other cache lasts one section.
class Canvas {
 public:
  explicit Canvas(Renderer& r) noexcept;
  void open() noexcept;    // r.begin_raw(): SAVE_CONTEXT, plus END and VERTEX_TRANSLATE_X 0 when the Renderer had them pending
  void close() noexcept;   // r.end_raw(): RESTORE_CONTEXT brings back colour, alpha, line width, point size, scissor and translation
  // Six words, always: BITMAP_HANDLE, _SOURCE, _LAYOUT, _LAYOUT_H, _SIZE, _SIZE_H. Both _H words go out even when zero: a handle's
  // properties persist across binds and frames, so a bind that skipped them would inherit the high bits of the bind before. After the
  // 800-wide texture, a small bitmap would get a box 512 px wider: invisible in a PNG (Border wrap), but walked by the chip. None when
  // `d` (by value) is already bound in this frame.
  void bind(const BitmapDesc& d) noexcept;
  // Cell 0..127 of the bitmap bound last, top-left at (x, y) in screen pixels: one VERTEX2II, moving the Canvas's own origin for x > 511;
  // BITMAP_HANDLE + CELL + VERTEX2F for x < 0, or always after use_translate(false) (the Renderer's plan B, [A] in renderer.h).
  void sprite(unsigned cell, int x, int y, Rgb rgb, std::uint8_t alpha = 255) noexcept;
  void point(int cx_q4, int cy_q4, unsigned radius_q4, Rgb rgb, std::uint8_t alpha = 255) noexcept;  // a disc, 1/16 px
  void line(int x0_q4, int y0_q4, int x1_q4, int y1_q4, unsigned w_q4, Rgb rgb, std::uint8_t alpha = 255) noexcept;  // round ends
  void rect(const Rect& r, Rgb rgb, std::uint8_t alpha = 255) noexcept;  // the Renderer's exact-pixel box (RECTS at LINE_WIDTH 8)
  // Corners of radius radius_q4, clamped to half the shorter side (radius_q4 <= 8 * min(w, h)); covers `r` exactly.
  void round_rect(const Rect& r, unsigned radius_q4, Rgb rgb, std::uint8_t alpha = 255) noexcept;
  void scissor(const Rect& r) noexcept;  // until scissor_reset() or close()
  void scissor_reset() noexcept;
  void use_translate(bool on) noexcept;
};
```

- **State inside a section.** Every vertex is relative to the Canvas's origin (`x - origin`), as the Renderer's `fill()` does.

| State | At `open()` | Emitted |
|---|---|---|
| COLOR_RGB | unknown | when the Rgb differs from the last one in this section |
| COLOR_A | 255: the Renderer never emits it, `raw()` belongs inside sections, and every section restores it | when different |
| LINE_WIDTH | 8: set by `Renderer::begin()`, and only changed inside sections | when different (`rect` uses 8) |
| POINT_SIZE | unknown | when different |
| Primitive | none | BEGIN when the next call needs another |
| Origin | 0 (`begin_raw` resets it) | VERTEX_TRANSLATE_X, moved like the Renderer's (reach 256 px, steps of 64) |
| Scissor | the screen | when different |
| Bound desc | the frame's | six words when another desc |

- **Words a section costs.**
  - SAVE_CONTEXT and RESTORE_CONTEXT: 2 words.
  - At `open()`, END and VERTEX_TRANSLATE_X 0 when the Renderer had a primitive open or a moved origin: 0 to 2.
  - After `close()`, the Renderer's next call re-emits its COLOR_RGB and BEGIN, because `end_raw` forgot them: 0 to 2, often emitted
    anyway.
- **Sections a frame.** A skinned frame needs one. `draw()` opens it before any Renderer call, so it costs 2 words. The order:
  1. the Canvas section: every shape, layer by layer (texture, windows, lips and faces, each knob part for all knobs, pointers, LEDs and
     glows, 7-segment cells, keys, lamps, readout fills);
  2. all live text, by token colour;
  3. the glass content, drawn by today's functions (with their own raw sections where they have them today);
  4. the runtime's strip.

  No skin shape sits over text. A later need for one costs one more section, at most 6 words.
- **Live text goes outside the section,** through `Renderer::text()` in the screen's theme (N7). The cost is the glyph words and one
  COLOR_RGB per colour run, as modelled: nothing is re-implemented. What it gives up: text colours are the obsidian theme's 17 tokens
  (section 7), tested against `kContrastPairs`. The alternative would draw glyphs inside the section from `font_face()` in any colour,
  re-implementing `text()`'s placement and origin in the Canvas. That is rejected.
- **Simulator:** nothing new. L1/L2/L4/L8, REPEAT, `_H`, COLOR_A, POINTS, LINES, SCISSOR, VERTEX_TRANSLATE_X and SAVE/RESTORE are
  modelled. Handle properties persist across frames, as on the chip. A too-large box shows only in N9's pixel count, so the word test
  below guards the bind.
- **Host tests** `tests/canvas_test.cpp`:
  - words:
    - every bind is six words;
    - bind A (an 800-wide REPEAT band, SIZE_H non-zero), then B (a 16 px LED): B carries LAYOUT_H and SIZE_H zero;
    - a rebind in the same frame is elided, also across two sections; a new Canvas binds again;
    - `open()` as the first draw of a frame emits SAVE_CONTEXT only, and `close()` RESTORE_CONTEXT only;
  - the boundary:
    - a sprite at x 600 moves the Canvas's origin, and `Renderer::text()` at x 600 after `close()` lands where it should;
    - `point`, `line`, `rect` and `round_rect` after that sprite land at their screen coordinates;
  - checks:
    - `bitmap_desc_valid` is false for REPEAT on a 100 px cell, a stride of 4,096, and Paletted565;
    - a Renderer call inside a section trips the check;
  - caches: the table, row by row;
  - pixels through `sim::raster`:
    - an L4 2 x 2 cell tinted at alpha 128 gives the mul255 values;
    - an L8 cell;
    - an 800-wide REPEAT band of a 256 tile;
    - `round_rect` covers exactly its rect, also with a radius clamped;
    - a wave view drawn after a section that ended at alpha 128 draws its own pixels;
  - unchanged: every N0 digest.
- **Unit:** on SU1 (LINES, POINTS, COLOR_A, the 800-wide REPEAT band with BITMAP_SIZE_H, sprites past x 511).

### N7: who draws the edges, and with which theme (kit/app: `screen.h`, `runtime.{h,cpp}`, `context.h`)

```cpp
// screen.h
enum class Chrome : std::uint8_t { Full, None };
// Full (default): the runtime draws its header strip and soft-key row and draw() gets kContentArea. None: draw() gets kScreenArea
// (800 x 480) and shows title, tempo, transport and F labels itself. Asked every frame. Ignored while the system Config screen is open.
virtual Chrome chrome(const Context&) const noexcept { return Chrome::Full; }
// The theme the whole frame is drawn with while this screen is up (CLEAR, the screen's content and text, the system strip); null
// (default): the context's. The theme lives as long as the app. Ignored while the system Config screen is open.
virtual const ui::Theme* theme(const Context&) const noexcept { return nullptr; }

// runtime.h
inline constexpr ui::Rect kScreenArea{0, 0, ui::kScreenW, ui::kScreenH};

// context.h. What the header showed; MASTER needs it too (UX-303 D19). Context has no Platform, so the Runtime publishes both: the meter
// flag from Platform::cpu_meter at construction, the percent every frame.
bool cpu_meter() const noexcept;            // the platform has a real cycle counter (the unit)
std::uint32_t cpu_percent() const noexcept; // the header's formula: 0 or more, above 100 when a block overran; 0 without a meter
```

- **`Runtime::draw()`:**
  - Theme: the runtime builds the frame's `ui::Renderer` with `screen->theme()` when one is given and Config is closed; otherwise with the
    context's theme. The ported Renderer already takes its theme at construction, so it is not touched.
  - Chrome None and Config closed: no `draw_header` and no `draw_keys`, and `soft_keys()` is not asked. `draw()` gets `kScreenArea`.
  - Chrome Full: exactly today. The header keeps calling the same `cpu_percent`, so its text does not change.
  - The system Config screen always has the full chrome and the context theme. So does the boot screen, which is drawn outside
    `Runtime::draw()`.
- **The system strip** (the power-off hold, and the 3 s notices: card, USB) goes last, at `{0, 452, 800, 28}` under Chrome None. While it is
  up, it covers the F row's read-outs and labels. The skin accepts that: a system message wins for its few seconds, as it does over the
  content today.
- **Simulator:** nothing (it runs the same `Runtime`).
- **Host tests** `tests/runtime_chrome_test.cpp`, with a fake app and screen:
  1. Full: the area is `kContentArea`, and every N0 digest is unchanged;
  2. None: the area is `kScreenArea`, the header's pixels are the screen's, `soft_keys()` is never called, and the strip is at the bottom;
  3. Config opened over a None screen: full chrome, the context theme, `kContentArea`;
  4. `theme()`: the frame's CLEAR is that theme's Pane; with Config open, the context's;
  5. `cpu_percent()`:
     - it is the header's formula on a fake engine;
     - it is above 100 for an overrun block;
     - the header's text is unchanged.
- **Unit:** on SU1 (no kit header on a skinned screen; Config intact over it; the power-off strip at the bottom; CPU % equal to `o3console.py cpu`).

## 6. The asset pipeline

| | |
|---|---|
| Generator | `tools/gen_skin.py` in o3-303, a port of the spike's shape generators (texture, knob parts, keys, 7-segment, LED, lamp, action key) plus the wordmark drawn as our own skewed-stroke glyphs. **Python 3 standard library only**: no numpy, Pillow or system fonts, the same rule as the kit's `logo_convert.py`. Procedural art, our own; nothing from any official firmware; no third-party font in any bitmap |
| Determinism | Integer and fixed-point arithmetic only in everything that reaches a byte (angles from a Q15 table in the script, `random` replaced by a fixed xorshift with seed 303, Bayer dither in integers). `-I` leaves hash randomisation on, so no set iteration and no `hash()` may reach a byte (dicts keep insertion order). The same bytes on every machine and Python 3.8 or later. `tools/skin_assets.sha256` (text, committed) holds the output's hash |
| Into the image | CMake `add_custom_command` in `app/CMakeLists.txt`, the way `o3sdk_app`'s LOGO option is built. `python3 -I tools/gen_skin.py --out <build>/app/generated --check tools/skin_assets.sha256` writes `o3_303/skin_assets.h` (an `Id` enum, a constexpr table of format, size, cells, stride, drawn size, offset; `kTotalBytes` with `static_assert(kTotalBytes <= kRamGAppBytes)`; the `AppAsset` table, statically checked with `app_assets_valid`) and `skin_assets.cpp` (one `alignas(4) const std::uint8_t` array per asset). Both are the custom command's OUTPUT and are added with `target_sources` and the include directory: the `src/*.cpp` glob cannot see the build directory. `--check` fails the build when the output's hash is not the committed one, so the MK66 build verifies it too. It depends on the script, so it runs again only when the script changes. Nothing generated is in git |
| Formats | L4 (texture, knob scale and body, 7-segment, wordmark) and L8 (knob caps, keys, LED, lamp, action key), high nibble first, rows padded to a byte, each asset padded to a word. No RGB formats |
| Checks | The ctest `skin_assets_fresh` runs the generator into a temporary directory twice and compares both runs with each other, with the build's copy and with `skin_assets.sha256`. A hash that changes is a visible diff in review, and a machine that disagrees fails CI |
| Review | `--preview <dir>` writes a contact sheet PNG (a standard-library PNG writer over zlib); `--report` prints each asset's size. The reviewer compares the sheet with the spike's `assets/contact_sheet.png` |
| Speed | Under 30 s on the build machine (the shapes are small; the texture's traces are drawn as integer thick lines at 2x and box-filtered) |

## 7. Code layout in o3-303

| File | What |
|---|---|
| `app/src/skin/skin.{h,cpp}` | `active(ctx)` (SKIN on and `ctx.app_assets_ok()`); the obsidian `ui::Theme`: its 17 tokens are every text colour of the skin too (labels, silkscreen, readouts, the buttons' colours, amber, red), and it must pass `ui::kContrastPairs`; the `Rgb` palette for shapes; a `ui::plot::BitmapDesc` for each asset (the texture as two descs, one per band) |
| `app/src/skin/panel.{h,cpp}` | the layer queue. Shapes are queued and flushed into one `ui::plot::Canvas` section one layer at a time (lips, faces, each knob part for all knobs, pointers, LEDs, glows), so state changes once a layer, as in the spike. Then the texts are flushed by token colour through `Renderer::text()` |
| `app/src/skin/header.{h,cpp}` | `HeaderView` (pattern, tempo, playing, rec, take, keys, octave, four PERFORM slots, page, enc, message, save failed), `fill_header()` from `StatusView` and the context (2.2), `draw_header()` |
| `app/src/skin/frow.{h,cpp}` | the slot kind (2.3), `draw_frow()` from `view::Knob[8]` or a Shift row |
| `app/src/skin/seg.h`, `knob.h` | pure: a string to 7-segment cells and decimal point; `norm` to the pointer's end points in 1/16 px |
| `app/src/skin/<screen>.cpp` | one drawing per view struct (home, perform, instrument, master, record, hooks, file; song in SG2) |
| the screens | `chrome()` and `theme()` overrides, and in `draw()` one branch: `skin::active(ctx) ? skin::draw_x(...) : draw_classic(...)` |

## 8. Slices

Sizes as in UX-PLAN: **S** about an hour plus a review, **M** two to three hours plus a review.

| Order | Slice | Size | Depends | o3-sdk | Unit |
|---|---|---|---|---|---|
| 1 | SK1 assets: generator, build step, upload | M | C1 folded | N0, N4 | no |
| 2 | SK2 skin core, header and glass on HOME / SETUP | M | SK1 | N5, N6, N7 (N9 recommended) | no |
| 3 | SK3 F row, Shift row, Pattern held, console `skin` | M | SK2 | | **SU1** |
| 4 | SK4 PERFORM and PERFORM SETUP | M | SU1 read | | SU2 |
| 5 | SK5 INSTRUMENT | M | SK4 | | SU2 |
| 6 | SK6 MASTER, RECORD, HOOKS | M | SK5 | | SU2 |
| 7 | SK7 FILE PROJECTS, LIBRARY fallback | S | SK6 | | SU2 |
| 8 | SK8 OBSIDIAN by default | S | SU2 passed | | next U image |
| (W7) | SG2 SONG with both drawings (UX-PLAN) | M | SG1, C1, SK3 | | U6 |

### SK1: skin assets (M)

- **Goal:** section 6. The nine assets of section 3 are generated at build time and uploaded at boot through `App::assets()`. Nothing draws
  them yet.
- **Owns:** new `tools/gen_skin.py`, `tools/skin_assets.sha256`; `app/CMakeLists.txt` (the custom command, the generated include directory,
  the glob gains `src/skin/*.cpp`); `app/src/o3_303_app.cpp` (`assets()` only); new `tests/skin_assets_test.cpp`; `tests/CMakeLists.txt`
  (ctest `skin_assets_fresh`).
- **Acceptance:**
  - two runs byte-identical and equal to the hash; the generator runs under `python3 -I` with no import outside the standard library;
  - the table is valid and the total is at most 110,000 B;
  - the contact sheet looks like the spike's;
  - the image grows by the assets' bytes (reported), with no new `__aeabi_d*`;
  - every scenario's PNG and WAV byte-identical; ctest green.
- **Scenario:** none new. **Expected PNG change:** none.

### SK2: skin core, header and glass on HOME and SETUP (M)

- **Goal:**
  - the skin module (section 7) and the SKIN key (page 303, key 7, CLASSIC/OBSIDIAN, default CLASSIC);
  - `MainScreen` returns `Chrome::None` and the obsidian theme while the skin is active, and draws the header (2.1, 2.2), the glass and
    today's content inside it; HOME's knob strip stays today's `draw_knobs` in the obsidian theme at y 384-480 until SK3;
  - the step view's row captions take the buttons' colours (a flag in `view::Marks`).
- **Owns:** new `app/src/skin/{skin,panel,header}.{h,cpp}`, `seg.h`, `knob.h`; `app/src/main_screen.{h,cpp}`; `app/src/step_view.{h,cpp}`;
  `app/include/o3_303/config_keys.h`, `app/src/config_pages.{h,cpp}`; `app/src/o3_303_app.cpp` (the `skin=0|1` state pair); tests: new
  `skin_core_test.cpp` (seg mapping, header mapping, the layer order), new `skin_theme_test.cpp` (the contrast pairs),
  `config_pages_test.cpp` (key 7), `o3_303_scenarios_test.cpp` (the required list; frames of `skin_*` scenarios under the ceiling, a constant at 1,350 until SU1; with N9,
  at most 100 primitives and 4,800 box px a line), `hands.h` (`skin_on()`); new `app/scenarios/skin_home.txt`.
- **Scenario setting:** a scenario turns the skin on through Config: `tap config`, `tap left` twice, `tap down` five times, `turn 4`
  (it clamps at OBSIDIAN, so it is correct whatever the default), `tap config`.
- **Acceptance:**
  - with CLASSIC, every existing PNG and WAV is byte-identical;
  - `skin_home` snaps: 01 stopped, 02 playing (the PLAY LED, the playhead), 03 a toast in the PAGE window, 04 TRNSP and the octave,
    05 REC, 06 SETUP;
  - the header reads like mockup 01's;
  - frames under the ceiling (1,350);
  - SKIN survives a reboot with a card;
  - `skin::active` is false when `app_assets_ok()` is false (a test);
  - ctest green, 0 warnings, no new `__aeabi_d*`.
- **Expected PNG change:** the new `skin_home` pictures only.

### SK3: the F row, HOME's Shift row and Pattern held (M)

- **Goal:**
  - the F row (2.3) replaces the temporary strip on HOME and SETUP: knobs, the WAVE switch, action keys, empty boxes, LEDs, readouts;
  - the Shift rows as action keys; Pattern held as today's grid in the glass;
  - the console command `skin` (`on`, `off`, `ballast N`, `stats`: the last frame's words, the peak draw µs over 1 s).
- **Owns:** new `app/src/skin/frow.{h,cpp}`; `app/src/knob_strip.h` (`view::Knob::toggle`); `app/src/frame.cpp` (`fill_knob` sets
  `toggle` for two-place lists); `app/src/main_screen.{h,cpp}`; `app/src/o3_303_app.cpp` (`diag_commands`); new `tests/skin_frow_test.cpp`
  (the slot rule, the pointer angle at 0, 0.5 and 1, the readout text); `app/scenarios/skin_home.txt`.
- **Acceptance:**
  - `skin_home` updated and extended: 07 the F row as mockup 01, F1 latched (lit LED, red readout), a held F key (amber), a p-lock value
    (accent); 08 Shift held as mockup 03; 09 Pattern held;
  - every other scenario byte-identical;
  - the Pattern-held frame under the ceiling (1,350);
  - ctest green.
- **Unit:** SU1 (section 4.6). The integrator builds the image, runs `fw-gate-check` and writes the hw-session page.

### SK4: PERFORM and PERFORM SETUP (M)

- **Goal:** 2.4's rows from `PerformView`: lamp sprites tinted by level with a latched frame, captions, the colour-buttons line, the PADS
  line, the F row. PERFORM SETUP's panel and knobs.
- **Owns:** `app/src/perform_screen.{h,cpp}`; new `app/src/skin/perform.cpp`; new `app/scenarios/skin_perform.txt`;
  `tests/o3_303_scenarios_test.cpp` (first: the ceiling set from SU1, 4.3).
- **Acceptance:**
  - snaps: idle; actions held and latched; a snapshot recalled with MORPH in the ENC window; the ladder bar; WOBBLE RATE; keys TRNSP; SETUP;
  - the lamps mirror the pad LEDs one to one;
  - under the ceiling SU1 set;
  - everything else byte-identical.

### SK5: INSTRUMENT (M)

- **Goal:** mockup 06 from `InstrumentView`, three pages: `knob58` row on top, section boxes, step strip with LEDs and keys, page tabs,
  PADS line. Shift shows LEARN or "-".
- **Owns:** `app/src/instrument_screen.{h,cpp}`; new `app/src/skin/instrument.cpp`; new `app/scenarios/skin_instrument.txt`.
- **Acceptance:**
  - snaps: VOICE, FX, MODS; a held step with its lock; LEARN armed; the cursor key ringed; the playhead lamp;
  - under the ceiling SU1 set;
  - everything else byte-identical.

### SK6: MASTER, RECORD and HOOKS (M)

- **Goal:** 2.4's rows for the three screens; the TAKE LED in the header (from `Rig::take`); CPU % from N7.
- **Owns:** `app/src/master_screen.{h,cpp}`, `record_screen.{h,cpp}`, `hooks_screen.{h,cpp}`; new `app/src/skin/{master,record,hooks}.cpp`;
  new `app/scenarios/skin_master.txt`, `skin_record.txt` (with a HOOKS snap).
- **Acceptance:**
  - snaps: MASTER with the 7-segment tempo, nudged, meters moving; RECORD armed, recording (the TAKE LED on another screen too) and ready;
    HOOKS;
  - under the ceiling SU1 set;
  - everything else byte-identical.

### SK7: FILE PROJECTS, and LIBRARY's fallback (S)

- **Goal:** 2.4's FILE row; NameEntry in the glass. LIBRARY returns `Chrome::Full` and the obsidian theme (its layout unchanged).
- **Owns:** `app/src/file_screen.{h,cpp}`, `app/src/vault_screen.{h,cpp}` (`theme()` only); new `app/src/skin/file.cpp`; new
  `app/scenarios/skin_file.txt`.
- **Acceptance:**
  - snaps: the list; NO CARD; SAVE AS on the keyboard; REVERT armed; LIBRARY (obsidian colours, today's layout, its peak at most 1,481 words);
  - everything else byte-identical.
- **Then SU2:** an image with every skinned screen, by hand:
  - each screen of 2.4, LIBRARY, Config over the skin;
  - a 30 s take recorded with the skin on: 0 underruns, draw µs, frames busy;
  - a regression pass of SU1's list;
  - SU1's ballast check on PERFORM and INSTRUMENT; the lowest L sets the ceiling (4.3).

### SK8: OBSIDIAN by default (S)

- **Goal:** SKIN's default becomes OBSIDIAN. UX-303 section 15 and DESIGN-303 are updated to what was built. A new
  `app/scenarios/skin_classic.txt` turns CLASSIC on (`turn -4`) and snaps HOME, PERFORM and INSTRUMENT, so the classic drawing stays exercised.
- **Owns:** `app/src/config_pages.cpp` (the default); new `skin_classic.txt`; `docs/UX-303.md`, `docs/DESIGN-303.md`; README's status line.
- **Acceptance:**
  - every non-skin scenario's PNGs change to the skinned look, each compared with its `skin_*` counterpart; the kit's Config pictures do not
    change; every WAV is byte-identical;
  - a settings file without key 7 gives OBSIDIAN;
  - ctest green; gate.
- **Unit:** the next U image.

**SG2 (UX-PLAN wave 7), gains:** a `SongView` drawn by both a classic function and `skin/song.cpp` (mockup 07), with `skin_song.txt`. SG2
owns those files and runs between SK3 and SK8, in the skin lane's gap.

## 9. Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | The line time at the real list length is unmeasured, so lines may break at 1,200 to 1,600 words | SU1 right after HOME, the cheapest point; the ballast check; the 1,350 start ceiling, raised only by measurement; per-screen fallback; the word cuts of 4.6 |
| 2 | The pure-Python port drifts from the approved look, or is slow | port the spike's algorithms (kept in its directory as the reference); the contact sheet and `skin_home` reviewed against mockups A and 01; the 30 s bound |
| 3 | The generator's bytes differ on another machine or Python | integer arithmetic only; the committed hash fails CI at once |
| 4 | Live 6x12 silkscreen looks plainer than the baked Liberation Narrow | `skin_home` is shown at SK2 and SK3. Baking the about 30 fixed header words is costed: +12 KB RAM_G, about −45 words, and a font in the build. The F labels cannot be baked without coupling the art to the page tables |
| 5 | The obsidian palette fails a kit contrast pair | `skin_theme_test`; retune the colour |
| 6 | Kit widgets (ListView, NameEntry) look wrong in obsidian tokens | looked at in `skin_file`; the theme's tokens are tuned there |
| 7 | All PNGs change at once at SK8 | one flip, after SU2, each picture compared with its `skin_*` counterpart |
| 8 | The o3-sdk batch slips | the UX waves do not depend on it; SK1 needs only N0 and N4 |
| 9 | Legibility and dither on the panel | SU1 checks 4 and 5, before any other screen |
| 10 | A Renderer call slips into a Canvas section, and the Renderer draws with a colour or primitive the Canvas changed | Canvas checks that `r.words()` did not move between its calls (debug and host builds abort); `canvas_test` covers it |
| 11 | 17 tokens are too few for the skin's text colours | the skin's text uses nine (label, silkscreen, dim, readout, amber, red, green, cyan, the row colours); `skin_theme_test` checks the pairs |
| 12 | A kit need turns out to touch a ported file | every N item's files are listed in section 5 and are own or already diverged; any other goes to the maintainer as a finding, not edited |

## 10. Decisions (taken by the maintainer's rule; nobody was asked)

| # | Decision | Why |
|---|---|---|
| D1 | Obsidian only; no silver | only obsidian was approved; saves 61,440 B of RAM_G and flash; one palette |
| D2 | Every word is live text in the kit's faces; baked art is shapes only | labels come from the page tables and the units' descriptors and cannot drift from them; RAM_G 100 KB instead of 136 KB; the generator needs no font. Cost: about +100 words a frame (modelled 1,175 to 1,273 on HOME before the sprite savings) |
| D3 | A pure standard-library Python generator run at build time, integer maths, a committed hash | no binaries in git; reproducible gate builds; no numpy, Pillow or system fonts on the build machine (the kit's own converters follow the same rule) |
| D4 | One sprite per LED, lamp and action key | fewer words and fewer primitives a line: PERFORM with the mockup's four-primitive lamps models at 1,773 words |
| D5 | N6 is required, as a new o3sdk file (`ui::plot::Canvas`) on the Renderer's public raw-section API, not as Renderer calls | the Renderer is ported and stays identical to track3r-os. Without a kit layer each app would copy the six-word binds, the origin moves past x 511 and the caches into its own raw sections |
| D6 | `Screen::theme()` (in N7): the runtime builds the frame's Renderer with the screen's theme | the ported Renderer already takes a theme when it is built; the frame's CLEAR becomes the glass colour (the spike's trick for light drawer lines); the skin's text and the step view, ListView and NameEntry draw in skin colours with no change of their own |
| D7 | LIBRARY keeps its layout under the skin | skinned it would be about 1,700 to 1,800 words, over any ceiling SU1 can give (at most 1,600). It is a browser, used rarely. Revisit after SU1 |
| D8 | Config stays the kit's | the system screen looks the same in every app (kit rule); the SKIN item lives there |
| D9 | SKIN key 7 (key 6 is reserved), default CLASSIC until SK8 | every slice keeps every existing PNG byte-identical; one reviewed flip; a way back on the unit |
| D10 | A skinned-frame ceiling of 1,350 words until SU1, then `min(1,600, L - 200)` | the only list length measured to flicker is 1,392 (with another kind of line), so the start stays under it while still fitting every HOME frame; SU1's ballast check gives the real margin |
| D11 | Toasts and take messages in the PAGE window; SAVE FAILED on the tagline; a TAKE LED | the header must hold all of `StatusView` with no status line, and the page name can wait 2.5 s |
| D12 | HOME keeps its motion lane under the skin | the skin changes the drawing, not the content; the NOTE lane gives up 26 px |
| D13 | Order HOME, PERFORM, INSTRUMENT, MASTER/RECORD/HOOKS, FILE, flip, with the unit gate after HOME | most-used first; the riskiest unknown is learned before five more screens are built |
| D14 | N9 is asked for, but does not block | it turns the unit's measured limit into a host check for every future screen |
| D15 | SONG is designed now (mockup 07) and SG2 builds both drawings | no second pass later |
| D16 | The app's upload never fails the boot and does not retry | a false `start()` holds the unit's display dark; the kit's own upload does not retry, and at boot a retry buys nothing but up to 70 ms a try. The app runs classic instead |
| D17 | Every app bind is six words, both `_H` words always | handle properties persist across binds and frames; tracking the high bits would save about 20 words a frame for a state machine across frames |
| D18 | N0 frame digests first in the o3-sdk batch | "today's words unchanged" has no test today; every later step is proved against pinned digests |
| D19 | One Canvas section a frame, opened first; all live text outside it, through `Renderer::text()` | 2 words a frame; no glyph code copied into the Canvas; the Renderer's private caches stay right. The price: text colours are theme tokens |
| D20 | `kHandleApp`, N0's digest and N9's counters go in new or already-diverged files (`canvas.h`, `eve_raster`) | `assets.h` and `raster_display.{h,cpp}` are identical to track3r-os and stay so |

## Appendix A: other kit fixes queued for the o3-sdk step (to batch with N4 to N7 if the maintainer wants)

| From | Fix (files: own unless marked) |
|---|---|
| C1 | `App::config_run` returns a refusal or message for the kit to show; `config_set` says whether a value is the user's change or the settings load; `Context` exposes whether the system Config is open |
| U5 row 33 | `ticks_per_sample_q32` (kit/dsp/src/transport.cpp, about lines 25-30) rounds the Q32.32 increment down, so whole-sample steps land one sample late (a one-bar take at 125 BPM is 84,673 frames, not 84,672): round up |
| U5 | a `Files` job that removes a stale `_o3save.tmp` (kit/app/include/o3sdk/app/files.h, own). It only reads `kTempName` from kit/hal/include/o3sdk/hal/save_guard.h:79, which is ported and identical and is not edited |
| U5 | the recorder counts 44 overruns after its buffer is full; it should stop counting at full |
| U5 | a `Files` WAV write holds the main loop about 48 ms a step (1,479 step overruns on a 30 s write): smaller steps |
| U5 | the console's `sd` text says "read only" when the card is writable. platform/mk66/storage/src/sd_text.cpp is ported and identical to track3r-os, so it is **reported to the maintainer**, not fixed in o3-sdk |
| UX-PLAN 7 | N1 the app's power-off notice; N8 MIDI learn of an app value (already queued) |
| all | the section 5 rule applies: diff each file against track3r-os before editing it; an identical file is not edited (`recorder.cpp`, `transport.cpp`, `files.*` and `app.h` are own or already diverged) |
