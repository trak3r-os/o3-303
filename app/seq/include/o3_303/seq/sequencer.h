// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The sequencer: a clocked hook of the engine (o3sdk dsp/engine.h) that plays the Bank's patterns on the voice unit.
//
// Origin. The timing and the note rules follow Replicant 303 by DanimanX (third_party/replicant303/DSP/PatternSequencer.cpp,
// triggerStep and process, commit d5369be028399f5a403067b58da2936caa070c0e): swing lengthening the even steps and shortening the odd
// ones by half the swing, the slide as a note-on before the held note's note-off at the same sample, a rest releasing the held note,
// the velocities 110 (accent) and 80. Replicant 303 is distributed under the MIT License:
//
//   MIT License
//
//   Copyright (c) 2026 Daniel Rojas Grasset (DanimanX)
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
//
// Timing (Replicant 303's, and the ties and gate of DESIGN-303 section 6). A
// step is a sixteenth (240 ticks). Swing lengthens the even steps and shortens the odd ones by half the swing of a step: the odd
// step starts `swing` x 120 ticks late and the pair still adds up to two sixteenths. The note of a step lasts `gate` percent of the
// step's own length, or the whole step when it slides. Events are placed at their frame inside the block, so any tempo of the
// transport (20..300 BPM) is sample-accurate.
//
//   note step   note-on (accent: dsp::kNoteAccent), note-off at the end of the gate. The note is the step's note + the pattern's
//               transpose + the live transpose, clamped to 0..127.
//   slide       the step's note is held to the end of the step and the NEXT note-on carries dsp::kNoteSlide; it is emitted before
//               the held note's note-off, at the same frame (Replicant's legato overlap).
//   tie         no new note: the held note goes on through this step; it ends with the gate of the last tied step. With nothing
//               held a tie is silent. A slide flag on a tie step holds the note into the next step.
//   rest        the held note is released at the step's start.
//   motion      the lane points of the step (four, at the quarters of the step) go to the lane's target; a point of kNoLock puts the
//               knob's value back (see update()).
//
// M3b (docs/M3B-PLAN.md), on top of those rules:
//
//   lanes       lane i drives Pattern::lanes[i], not "voice id i": the voice, the distortion or the delay as Param events, slot 0's
//               mixer through BlockContext::set_mix (the point maps linearly over kMixerRange). The default lanes are the SOUND page,
//               so a pattern that never touched them plays as in M3. A mixer write lands for the whole block (the mixer has one ramp
//               per block); a Param event is frame-exact. Two lanes on one target are the author's mistake: the later lane wins and
//               nothing sticks. A lane whose target does not exist (no distortion given to setup(), an id past the unit's table or
//               past kKnobs, kLaneNone) is silent.
//   ratchet     a playing step with a ratchet of n = 1..4 hits plays n notes at the starts of n equal parts of the step (hit k at
//               start + k * len / n, in ticks: swing included, so the hits fill the swung step and the next step is not moved). Each
//               hit has the pattern's gate of ITS part (a 50 % gate and 4 hits: 4 notes of an eighth of the step). The step's accent
//               is on the FIRST hit; the others are normal unless ACC ALL is on. Slide: a slide step holds its LAST hit into the next
//               step (that hit has no note-off; the next note-on glides), the earlier hits are plain separate notes; a step that
//               follows a slide glides on its first hit only (the other hits retrigger). A tie after a ratcheted step holds its last
//               hit as it holds a plain note. A ratchet on a rest or a tie step is ignored.
//   condition   a step with a trig condition (pattern.h) is evaluated when it starts; if it does not pass it acts as a rest: a held
//               note ends at its start and the slide that led to it is over. The tie look-ahead asks the next step's condition when
//               the note is not sliding (a probability is drawn then and kept for that step's own start: one draw a step, in step
//               order), so a note before a tie that fails ends with its own gate. A FILL or NOT FILL tie reads the fill as it stands
//               then (a change on that very boundary leaves the note held or released a step early). A rest has no condition. The pass counter is the number of times the playing pattern has started over since it was
//               loaded (Play, a jump, or the chain starting it): 0 on the first pass. A:B passes when pass % B == A - 1; FIRST is
//               pass 0. Probabilities draw from the sequencer's own Rng, one draw for each probability step evaluated, seeded by
//               set_seed(): the same seed and the same play give the same notes. Locks and motion points do not depend on the
//               condition: they are the pattern's automation.
//   fill        while FILL is held (kPerfFill, set_fill()) the pattern plays a variant of itself (FillVariant, ops.h; chosen with
//               set_fill_variant()), derived from a copy: the stored pattern never changes. The hook looks at FILL when a step starts,
//               so the variant starts and ends on a step boundary. The FILL / NOT FILL conditions follow the same state.
//   slew        slew() moves a target (a parameter of the voice, the distortion or the delay, or a field of slot 0's mixer) to a
//               value over a time: the hook emits Param events (or a mixer write) toward it, in the target's own curve (a cutoff glides
//               along its knob position, not in Hz). A parameter gets an event at frame 0 of every block (so it wins over a write the
//               main loop queued for the same frame) and one every 16 frames; with many slews on a unit, every 32 or 64 frames, so
//               that the slews put at most kSlewEvents events (16 with the ones that end a slew) in the unit's list of 32 in a block.
//               A mixer field gets one write per block, the value at the block's end. A lock or motion point on the target wins for as
//               long as it holds, to the frame, and the slew goes on along its line when it ends. While a slew runs it owns its target:
//               slew_stop() it before the main loop writes the target. Its end is exact: the goal is emitted at the frame the time
//               runs out. Cost: nothing while no slew runs; about a lerp, a curve inverse and an event for each slew each tick.
//   punch       set_punch() holds a unit's parameter at a value while a pad is down (H2): the hook writes it at frame 0 of every block, after
//               everything else, and the lock, the slew and the wobble keep their writes to themselves while it holds; clear_punch() puts the
//               target back where it would be (the lock's value, the slew's, or the knob; the wobble's base).
//
// M3c (docs/M3C-PLAN.md), on top of those:
//
//   wobble      a tempo-synced LFO that moves one target around its base value (set_wobble()): the hook emits a Param event toward the
//               target every 16 frames (frame 0 of the block, 16, 32, 48 at 64 frames a block; a mixer field one write a block), like the
//               slew. The target is any LaneRef whose parameter is not an Enum (the default: the voice's Cutoff; the growl's Vowel is the
//               classic; a mixer field works). The value is base + depth / 2 x wave in the target's normalised position (depth 1 sweeps
//               the whole range, centred on the base, clamped at its ends), through the descriptor's curve. The BASE is what the target would
//               be without the wobble: the p-lock or motion point holding it (to the frame, and back to the knob when it ends), else the
//               slew that is gliding to a goal, else the knob (update()). The wobble owns its target while it runs (a slew on it only counts
//               its time). Rates 1/1 1/2 1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32 1/8D 1/16D (seq/wobble.h), shapes sine, triangle, saw down,
//               saw up, square, sample and hold. The phase is counted from the transport (it restarts with Play) and, with the reset
//               on note (the default), restarts at 0 on every note that starts, ratchet hits and slides included, at that frame (one
//               extra event). It runs when the transport is stopped, free, at the transport's tempo. A rate change keeps the phase, so
//               nothing jumps. The sample and hold draws a new value when a cycle begins (a wrap seen at a tick or the end of a block,
//               so it lands within one block of it), from a generator seeded by set_seed(): same seed, same play, same values.
//               Cost: about four events, a sine and two curve conversions a tick, nothing when off.
//               The rate and the depth can also be sequenced: a motion lane on kLaneSeq (pattern.h) sets them for as long as its point
//               holds; the momentary pad override (wobble_rate_override()) beats the lane, and the lane beats set_wobble().
//   growl       given to set_unit(kLaneGrowl, id), the growl's parameters are lane and slew targets like the other units' (kLaneGrowl), and
//               the hook sets its Note parameter (kGrowlNoteParam, the MIDI note that starts, clamped as played) at every note start, ratchet
//               hits included: one Param event a note.
//   tract       given to set_unit(kLaneTract, id): its parameters are lane and slew targets like the growl's (no per-note event).
//   tract line  (docs/TRACT-KNOBS-PLAN.md A1b) the pattern's TRACT line (tract_line.h; the Bank's tract_step() and friends edit it), played
//               while PLAY TRACT is on (set_tract_line, on by default; it also lets the TONGUE and LIPS lane locks through) and the tract unit is given: at the frame a step starts, a vowel step writes the tract's VOWEL
//               (kTractVowelParam) like a lock; a "-" step gives the knob (or a slew's value) back, so the line only holds the parameter
//               while it has a vowel. A GLD step also glides: at its first quarter it sets GLIDE to a quarter of the step's length (5..500 ms)
//               and VOWEL to the next step's vowel (the knob's value when that is "-"), so the mouth is at the next vowel by the step's end
//               (three time constants, 95 %); a vowel step without GLD gives the knob's glide back before it writes. The line's length is its
//               own (4, 8, 16, 32, 64) and it runs on the same clock as the notes, counted from the pattern's load (a jump, a chain entry, a
//               song row, Play), so a line shorter or longer than the pattern is polymetric against it; SAME follows the pattern's step.
//               A motion lane on the tract's VOWEL or GLIDE wins over the line while its lock holds, and a slew on them waits like it does for a
//               lane. Cost: a few compares a step, a Param event or two on a vowel step, nothing between steps.
//
// SG1 (UX-303 5.7, the song), on top of those:
//
//   song        a Song (song.h) the sequencer plays instead of the selected pattern when the song mode is on (set_play_song(); it is read when
//               Play starts a run). Play starts at the start row (the first row from it that is not muted), and the row's pattern plays
//               `repeat` times, counted at the pattern's end (the pass counter goes on over the repeats, so a trig condition A:B works
//               across them; a new row starts it again, as a chain entry does). Then the next row that is not muted starts, and after the
//               last one the song starts again from its first (the loop, on by default) or, with the loop off, the sequencer finishes: it
//               goes quiet, the transport runs on and song_position().done says so (the app stops the engine). A row's transpose
//               is added to the note on top of the pattern's and the live transpose. A song with no row to play (empty, all muted) is
//               played as pattern mode when Play starts, and loops on the playing pattern if it comes to that mid-run. While a song plays the chain
//               and jump() are not followed (the song is the order). set_song() may be called at any time: a row is read when it is
//               started (its pattern, its transpose), when its pattern ends (its repeats) and when the next row is looked for (mute,
//               length); rows are single words, so a row is never torn, the rows of an edit may be seen across one block boundary. A song
//               in pattern mode costs nothing: the hook never looks at it.
//
//               SG2: the position follows an edit of the playing song (song_row_inserted(), song_row_deleted(): commands on the ring, called
//               after the sync): it stays on the same musical row where that row exists. A row inserted above it, or a row deleted above
//               it, moves the position with the row. The playing row deleted: its pattern plays on to the end of this pass (its
//               remaining repeats are dropped) and the song goes on with the row that moved up into its place (song_position().row is -1
//               meanwhile: the pattern belongs to no row). A song replaced under the position (CLEAR, FROM CHAIN, an undo or
//               redo that is not one inserted or deleted row, a load; song_replaced()) belongs to no row for the rest of the pass: the pattern plays
//               out and the song starts again from its first row (the loop) or finishes, whatever the new rows are by then (song_position().row is -1
//               meanwhile). A shrink that gives no command at all (a full ring) is clamped, as before: a position past the end ends the pass. A cell of the playing row is read at
//               the next pass: its repeats (fewer ends the row sooner, a muted row ends at its pass), its pattern (a new one starts at the next
//               repeat) and its transpose. The edit and its command reach the render in two steps: a pattern that ends in the one block between
//               them takes the new rows with the old position (one pass, once).
//
// Contexts. Everything not marked "render" is the main loop. The main loop never touches the render's state: it queues commands on a
// lock-free ring (the same way the engine takes its commands; a full ring refuses and returns false), sets a few atomics, and
// reads the playhead from one atomic word. The patterns are shared through the Bank's seqlock (bank.h).

#include <o3sdk/core/param.h>
#include <o3sdk/core/ring.h>
#include <o3sdk/dsp/engine.h>
#include <o3_303/pattern.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/rng.h>
#include <o3_303/seq/song.h>
#include <o3_303/seq/wobble.h>

#include <atomic>
#include <cstdint>

namespace o3_303::seq {

static_assert(std::atomic<float>::is_always_lock_free && std::atomic<std::uint32_t>::is_always_lock_free,
              "the hook and the main loop share these atomics: they must be plain loads and stores (no lock, no libatomic)");

inline constexpr unsigned kMaxQueue = 16;  // entries of the chain

// Performance flags (momentary or latched by the screen): the hook honours them at the next step.
inline constexpr std::uint8_t kPerfMute = 1u << 0;      // no new notes (the playhead goes on; a held note is released)
inline constexpr std::uint8_t kPerfAccAll = 1u << 1;    // every note is accented
inline constexpr std::uint8_t kPerfSlideAll = 1u << 2;  // every step slides
inline constexpr std::uint8_t kPerfFill = 1u << 3;      // FILL: the pattern plays its fill variant, from the next step (set_fill())

inline constexpr unsigned kMixerSlot = 0;   // the slot whose mixer the mixer lanes and slews write (pattern.h: "slot 0's mixer")
inline constexpr unsigned kKnobs = 32;      // parameter ids 0..31 of a unit can be a lane's or a slew's target
inline constexpr unsigned kMaxSlews = 32;   // slews running at once (a snapshot morph moves up to about 26 parameters)
inline constexpr unsigned kSlewEvents = 12; // the most Param events the slews put in one unit's list in one block (the list holds 32)
inline constexpr unsigned kPunchSlots = 4;  // set_punch(): targets held at a value at once (PERFORM's DIST takes two, CUT LO / CUT HI one, RESO one); a punch is one event a block
inline constexpr unsigned kWobbleEvents = 8;  // the most Param events the wobble puts in its unit's list in one block (4 ticks and up to 4 note resets)
inline constexpr std::uint8_t kGrowlNoteParam = 6;  // the growl's Note parameter id (units/growl: Growl::Note; tests/seq_wobble_test.cpp checks it)
inline constexpr std::uint8_t kTractVowelParam = 0;  // the tract's VOWEL parameter id (units/tract: TractFx; app/seq/tests/seq_tract_line_test.cpp checks it)
inline constexpr std::uint8_t kTractGlideParam = 4;  // its GLIDE parameter id (ms, the one-pole time of the vowel)
inline constexpr std::uint32_t kDefaultSeed = 0x30330303u;  // the Rng of the probability draws and of the Mutate fill, until set_seed()

// What the screens read: the playhead as of the end of the last block.
struct Playhead {
  bool playing = false;
  int pattern = -1;        // the pattern being played (0..kPatternCount-1), -1 when stopped
  int step = -1;           // the step that started last (0..63), -1 when stopped
  int point = 0;           // its quarter (0..3) now: where a motion recording lands
  int phase = 0;           // how far into that step the end of the last block is, in sixteenths of its length (0..15; phase / 4 == point): where a key
                           // played now is nearer to (HomeMap's nearest step)
  int queued = -1;         // the pattern that starts when this one ends (the next of the chain, or its first when a looping chain
                           // is at its end), -1 if the playing pattern simply repeats
  unsigned queue_length = 0;  // chain entries still to come after the playing one (a loop's wrap not counted), 0..kMaxQueue
  bool sounding = false;   // a note is held now
  bool fill = false;       // the fill variant is the one playing (FILL was held when the last step started)
};

// The chain as the sequencer holds it (the patterns that follow the playing one, in order), read-only for the screens.
struct ChainView {
  unsigned length = 0;                 // entries, 0..kMaxQueue
  std::uint8_t pattern[kMaxQueue] = {};  // the patterns, in play order
  int position = -1;                   // the entry playing now; -1: the playing pattern is not from the chain (or stopped)
  int next_entry = -1;                 // the entry that starts when the playing pattern ends (the first one when a loop is at its end),
                                       // -1 if the playing pattern simply repeats; it is position + 1 unless a jump moved the playing pattern
  int next = -1;                       // its pattern, as Playhead::queued
  bool loop = false;                   // the chain starts again after its last entry
};

// Where the song is, as of the end of the last block (Sequencer::song_position()).
struct SongPosition {
  bool active = false;   // a song is what this run plays (set_play_song() was on when it started and a row could play)
  bool done = false;     // the loop is off and the last row has ended: nothing plays any more (the transport still runs)
  int row = -1;          // the row playing now (the last one played when done), -1 when not active or when the playing row was deleted (its pattern plays out)
  unsigned repeat = 0;   // the play of the row's pattern now, from 0 (0 .. repeat - 1)
};

// The wobble as the main loop set it (Sequencer::wobble()).
struct Wobble {
  bool on = false;
  bool reset_on_note = true;
  unsigned rate = kWobble1_8;         // WobbleRate
  unsigned shape = kWobbleSine;       // WobbleShape
  float depth = 0.5f;                 // 0..1
  LaneRef target{kLaneVoice, 0};      // default: the voice's Cutoff
  int override_rate = kWobbleRateNone;  // the pad's momentary rate, or kWobbleRateNone
};

class Sequencer final : public o3sdk::dsp::Clocked {
 public:
  explicit Sequencer(Bank& bank) noexcept;

  // ---- Setup (main loop, before engine.start()) ---------------------------------------------------------------------------
  // `voice` is the instrument unit the notes go to (the target of the default lanes: its parameter ids 0..6); `dist` and `delay` are the
  // other units a lane or a slew may drive (kNoUnit: that target is not available). The units' parameter tables are read here.
  // Adds this hook to the engine: the app does not call engine.add_hook(). false: the engine refused the hook (full, started), or
  // `voice` has fewer than 8 parameters. Resets everything the hook remembers (slews, the pass counter, the Rng, the fill): setup()
  // runs before the render does.
  bool setup(o3sdk::dsp::Engine& engine, o3sdk::dsp::UnitId voice, o3sdk::dsp::UnitId dist = o3sdk::dsp::kNoUnit,
             o3sdk::dsp::UnitId delay = o3sdk::dsp::kNoUnit) noexcept;

  // Gives a unit to a lane target that setup() did not get: kLaneDist, kLaneDelay, kLaneGrowl (M3c: the growl, slot 0's insert 1) or kLaneTract (the tract, insert 2). Call it
  // after setup() (which forgets the units) and before engine.start() (the render reads the table); false otherwise, for another target or a
  // unit the engine does not know. kNoUnit takes the target away. With the growl given, the hook sets its Note parameter at every note start.
  bool set_unit(std::uint8_t target, o3sdk::dsp::UnitId unit) noexcept;

  // ---- Every UI frame (main loop) -----------------------------------------------------------------------------------------
  // Hands the hook the knob values (the engine's stored values of the voice's, the distortion's and the delay's parameters 0..kKnobs-1
  // and slot 0's mixer fields), which it puts back when a p-lock or a motion lane ends. Call it once per UI frame; a missing call only
  // leaves the old knob value after a lock.
  void update() noexcept;

  Bank& bank() noexcept { return bank_; }
  const Bank& bank() const noexcept { return bank_; }

  // ---- Playing patterns (main loop) ---------------------------------------------------------------------------------------
  // Play (engine.play()) starts the selected pattern, bank().selected(), from step 0.
  // The chain: a list of up to kMaxQueue patterns with a cursor. When the playing pattern ends (at the end of its length) the next
  // entry of the chain starts; when the chain is used up the playing pattern repeats (or, with the loop on, the chain starts again
  // from its first entry). Play starts at the selected pattern, outside the chain, and the cursor at the chain's first entry.
  // queue: appends the pattern to the chain (a tap while playing queues one; several taps make a chain). When the chain is full, the
  // entries already played are dropped to make room (not with the loop on: the chain is then refused). The call is a command: its
  // effect shows in chain() within a block. A pattern index >= kPatternCount is refused.
  // Return values: true = the command is on the ring (a full ring, or a bad index, gives false and changes nothing). The hook may
  // still refuse the entry later (the chain is full and looping): chain() is the truth, read it instead of counting calls.
  bool queue(unsigned pattern) noexcept;
  bool clear_queue() noexcept;  // empties the chain; the playing pattern repeats
  bool set_chain_loop(bool loop) noexcept;
  // The pattern starts at the next step, from its step 0; the chain's cursor stays where it was, so the chain goes on after it.
  // Stopped: only selects the pattern. Playing: selects it too, but only if the command went on the ring (false: nothing happened).
  bool jump(unsigned pattern) noexcept;

  // ---- The song (main loop; see "SG1" above) ------------------------------------------------------------------------------------
  // Hands the sequencer a copy of `song` to play (made canonical on the way: song_normalize()). Not a command on the ring: the rows are
  // atomic words the hook reads when it needs them.
  void set_song(const Song& song) noexcept;
  // set_song(bank().song()): after an edit, an undo / redo or a restore of the Bank's song.
  void sync_song() noexcept { set_song(bank_.song()); }
  // The song mode, read when Play starts a run: `on` plays the song from row `start_row` (a row past the end starts at 0); off, the selected
  // pattern as always. A run that is going keeps the mode it began with.
  void set_play_song(bool on, unsigned start_row = 0) noexcept;
  bool play_song() const noexcept { return (song_play_.load(std::memory_order_relaxed) & 1u) != 0u; }
  // After the last row: true starts the song again (the default), false finishes it.
  void set_song_loop(bool loop) noexcept { song_loop_.store(loop ? 1u : 0u, std::memory_order_relaxed); }
  bool song_loop() const noexcept { return song_loop_.load(std::memory_order_relaxed) != 0u; }
  SongPosition song_position() const noexcept;
  // The song's rows moved under a playing song (after sync_song()): a row was inserted at `at` (the rows from it down moved one down) or the row `at` was
  // deleted (the rows below it moved one up). The playing row's index follows (see "SG2" above). Nothing happens when no song is playing. false: the
  // command ring is full and the position did not move (the caller may ignore it: the position is then clamped like after any other edit).
  bool song_row_inserted(unsigned at) noexcept;
  bool song_row_deleted(unsigned at) noexcept;
  // The song was replaced as a whole (after sync_song(): CLEAR, FROM CHAIN, an undo that is not a row inserted or deleted, a loaded project): the playing
  // row belongs to no row of the new song, its pattern plays out and the song starts again at its first row (or finishes). Without it a position
  // kept across a CLEAR would name whatever row is built at that index later. false: the ring is full (the caller may ignore it).
  bool song_replaced() noexcept;

  // ---- Live control (main loop; atomics, effective at the next step) ----------------------------------------------------
  void set_live_transpose(int semitones) noexcept;  // added to the notes, -48..+48
  int live_transpose() const noexcept { return live_transpose_.load(std::memory_order_relaxed); }
  void set_perf(std::uint8_t flags) noexcept { perf_.store(flags, std::memory_order_relaxed); }
  void set_perf_flag(std::uint8_t flag, bool on) noexcept;
  std::uint8_t perf() const noexcept { return perf_.load(std::memory_order_relaxed); }

  // ---- The TRACT line (main loop; atomics, effective at the next step; see "tract line" above) -----------------------------------
  // PLAY TRACT on / off, project-wide, on by default (saved in the SND chunk as kAppTractLine, project.cpp). Off: the vowel line is not played
  // and the TONGUE / LIPS locks stand aside (emit_points), so the F1-F3 knobs rule.
  void set_tract_line(bool on) noexcept { tract_line_on_.store(on ? 1u : 0u, std::memory_order_relaxed); }
  bool tract_line_on() const noexcept { return tract_line_on_.load(std::memory_order_relaxed) != 0u; }
  // The line's step that started last (0..63), for the LEDs; -1 when stopped. It is published whether LINE is on or off.
  int tract_playhead() const noexcept { return tract_head_.load(std::memory_order_relaxed); }

  // ---- FILL and the trig conditions (main loop; atomics, effective at the next step) -------------------------------------------
  // FILL held / released: the performance flag kPerfFill (a screen that rewrites the whole flag word with set_perf() owns it too).
  void set_fill(bool held) noexcept { set_perf_flag(kPerfFill, held); }
  bool fill_held() const noexcept { return (perf() & kPerfFill) != 0u; }
  // What the fill plays (ops.h FillVariant); the choice is not part of a pattern. Default: Mutate.
  void set_fill_variant(FillVariant v) noexcept;
  FillVariant fill_variant() const noexcept;
  // Seeds the probability draws and the Mutate fill, from the next block on (it also restarts the fill counter). Same seed, same
  // play, same notes. The seed at construction is fixed, so a fresh app is reproducible without calling this.
  void set_seed(std::uint32_t seed) noexcept;

  // ---- Slews (main loop): a parameter or a mixer field glides to a value ---------------------------------------------------------
  // `target` is kLaneVoice, kLaneDist, kLaneDelay (pattern.h; `id` the unit's parameter id < kKnobs) or kLaneMixer (`id` a MixField of
  // slot 0, kMixerSlot). `value` is in the target's own units (the descriptor's, or the mixer's linear gain / pan / mute); it is clamped
  // as the engine clamps it. The target reaches `value` after `ms`; `ms` of 0 or less, or an enum parameter, is a plain write.
  // A second slew() on a target that is still gliding goes on from where the first has got to. A slew started while stopped runs.
  //
  // `store`: the engine's stored value (what param() and mix() report, what update() hands to the hook, what the screens show) becomes
  // `value` NOW, with the call, while the sound is on its way. That costs the unit's event list two events in the first block (the stored
  // write, and the slew's own first event, which wins it back to the start value): start at most about eight such slews on one unit in a
  // block. With store = false nothing is written to the engine: a morph of two dozen parameters starts them all that way (one event
  // each) and stores the values when it is done, because the hook puts back the STORED value when a lock ends, once its slew is over.
  // The immediate case always stores.
  //
  // More than kMaxSlews running at once: the hook has no room for the extra one and writes its goal at once instead (the target jumps
  // there whatever `store` says; slew_overflow() counts it), so a target always ends at its goal; slew() still returns true.
  //
  // Returns false when nothing was started or written (a bad target or id, a unit that was not given to setup()), or when the command
  // ring is full (the value was then written at once, so the target is where it should be, without the glide).
  bool slew(std::uint8_t target, std::uint8_t id, float value, float ms, bool store = true) noexcept;
  // Stops the slew of a target where it is (the sound stays at its last step; set the target afterwards if it has to be somewhere).
  bool slew_stop(std::uint8_t target, std::uint8_t id) noexcept;
  bool slew_stop_all() noexcept;
  // Slews running as of the end of the last block, and the commands the hook had no room for (more than kMaxSlews at once).
  unsigned slewing() const noexcept { return slewing_.load(std::memory_order_relaxed); }
  std::uint32_t slew_overflow() const noexcept { return slew_overflow_.load(std::memory_order_relaxed); }

  // ---- Punches (main loop; H2: PERFORM's DIST, CUT LO, CUT HI and RESO pads) -----------------------------------------------------
  // A punch holds a unit's parameter at `value` (clamped to its range) until clear_punch(): the hook writes it at frame 0 of every block, and it
  // wins over a p-lock or a motion point, a slew, the wobble and a write of the main loop (that one within a block). None of them is lost: clearing
  // the punch brings back where the target would be without it (a lock that holds, else the slew's value, else the knob; with a wobble on it the
  // wobble's base). A punch is not a lane: nothing records it and the engine does not store it, so a project never keeps it. `slot` < kPunchSlots;
  // `target` a unit target (not the mixer, not the sequencer's own values). false: refused, or the command ring is full. A slot given another
  // target lets go of its old one. Cost while a punch is held: one event a block each, nothing otherwise.
  bool set_punch(unsigned slot, std::uint8_t target, std::uint8_t id, float value) noexcept;
  bool clear_punch(unsigned slot) noexcept;
  // The slots held as of the end of the last block (bit n: slot n).
  unsigned punches() const noexcept { return punch_mask_.load(std::memory_order_relaxed); }

  // ---- Wobble (main loop; atomics, effective at the next block) ---------------------------------------------------------------
  // The wobble (see "M3c" above). `rate` is a WobbleRate, `shape` a WobbleShape, `depth` 0..1 (clamped), `target` a lane target whose parameter
  // is not an Enum and that is available (a unit that was not given gives false; kLaneSeq is not a target). With `on` false only the switch
  // changes (the rest of the arguments are not looked at): the target goes back to its base. The settings are kept while it is off. Moving the
  // target restores the old one. false: nothing changed.
  bool set_wobble(bool on, unsigned rate, unsigned shape, float depth, LaneRef target = LaneRef{kLaneVoice, 0}) noexcept;
  void set_wobble_reset(bool reset_on_note) noexcept;  // default on
  // The momentary rate of the pad gesture: the wobble runs at `rate` (a WobbleRate) while it holds, whatever the pattern's lane says;
  // kWobbleRateNone gives the pattern's back. Effective with the wobble off too (it takes the rate when it is switched on).
  void wobble_rate_override(int rate) noexcept;
  Wobble wobble() const noexcept;
  // The rate the wobble runs at as of the end of the last block (the override, else the lane's, else set_wobble()'s), for the LEDs; -1 while
  // it is off.
  int wobble_rate_now() const noexcept { return wob_rate_now_.load(std::memory_order_relaxed); }

  // ---- Readback (main loop) -----------------------------------------------------------------------------------------------
  Playhead playhead() const noexcept;
  // The chain, its cursor and the loop flag, as of the end of the last block (a consistent copy; the render publishes it when it
  // changes). Read-only: change it with queue(), clear_queue(), set_chain_loop().
  ChainView chain() const noexcept;
  // Writes `value` (the lane's target in its own units: the descriptor's for a unit's parameter, the mixer's linear value for a mixer
  // field) as the motion point under the playhead of the playing pattern, for the lane as that pattern's `lanes` table says: the live
  // recording of an encoder move. An undoable edit, joined per lane. false: not playing, bad lane, a lane with no target.
  bool record_motion(unsigned lane, float value) noexcept;
  // Events the hook could not place because the voice's list for a block was full.
  std::uint32_t dropped_events() const noexcept { return dropped_.load(std::memory_order_relaxed); }

  // ---- Render context -----------------------------------------------------------------------------------------------------
  void block(o3sdk::dsp::BlockContext& ctx) noexcept override;

 private:
  enum class Op : std::uint8_t { Queue, ClearQueue, ChainLoop, Jump, SongInsert, SongDelete, SongReplaced };
  struct Command {
    Op op;
    std::uint8_t arg;
    std::uint16_t pad;
  };
  static_assert(sizeof(Command) == 4, "a command is one word");

  enum class SlewOp : std::uint8_t { Start, Stop, StopAll, PunchSet, PunchClear };  // a punch's commands: `pad` is the slot, `to` the value
  struct SlewCommand {
    SlewOp op;
    std::uint8_t target;
    std::uint8_t id;
    std::uint8_t pad;
    std::uint32_t frames;  // the time in samples (>= 1)
    float to;              // the goal, in the target's units
    float from;            // where the target stands on the main side; used when no slew is running on it
  };
  static_assert(sizeof(SlewCommand) == 16, "a slew command is four words");

  // A running slew (render side). It interpolates between `a` and `b` in the target's domain: the normalised position for a unit's
  // parameter (so a cutoff glides along its knob), the value itself for a mixer field.
  struct Slew {
    bool active;
    bool mixer;
    bool snap;  // an Enum parameter: it takes its value at once
    std::uint8_t target;
    std::uint8_t id;
    const o3sdk::core::ParamDesc* desc;
    float a;
    float b;
    float goal;       // the exact value at the end
    float last;       // the last value emitted (valid once `emitted`)
    bool emitted;
    float inv_total;
    std::uint32_t total;  // samples
    std::uint32_t pos;    // samples since the start, as of the start of the current block
  };

  // A held punch (render side).
  struct Punch {
    bool active = false;
    std::uint8_t target = 0;
    std::uint8_t id = 0;
    float value = 0.0f;
  };

  // A unit a lane or a slew can address (setup()).
  struct UnitTable {
    o3sdk::dsp::UnitId unit = o3sdk::dsp::kNoUnit;
    const o3sdk::core::ParamDesc* params = nullptr;
    unsigned count = 0;
  };

  // ---- main loop side
  bool resolvable(std::uint8_t target, std::uint8_t id) const noexcept;
  const o3sdk::core::ParamDesc* desc_of(std::uint8_t target, std::uint8_t id) const noexcept;

  // ---- render side
  void drain_commands(o3sdk::dsp::BlockContext& ctx) noexcept;
  void sequence(o3sdk::dsp::BlockContext& ctx) noexcept;
  void begin_run(const o3sdk::dsp::Transport& t) noexcept;
  void end_run(o3sdk::dsp::BlockContext& ctx) noexcept;
  void start_step(o3sdk::dsp::BlockContext& ctx, unsigned frame) noexcept;
  void fire_hit(o3sdk::dsp::BlockContext& ctx, unsigned frame) noexcept;
  void emit_points(o3sdk::dsp::BlockContext& ctx, unsigned frame, unsigned quarter) noexcept;
  void release_held(o3sdk::dsp::BlockContext& ctx, unsigned frame) noexcept;
  void restore_lanes(o3sdk::dsp::BlockContext& ctx, unsigned frame) noexcept;
  void restore_target(o3sdk::dsp::BlockContext& ctx, std::uint8_t target, std::uint8_t id, unsigned frame) noexcept;
  bool write_target(o3sdk::dsp::BlockContext& ctx, std::uint8_t target, std::uint8_t id, float value, unsigned frame) noexcept;
  float knob_value(std::uint8_t target, std::uint8_t id) const noexcept;
  float unstored_or(unsigned target, unsigned id, float stored) noexcept;  // main loop: Unstored's goal, else `stored`
  void load_pattern(unsigned index) noexcept;
  void refresh_shadow() noexcept;
  void append_chain(unsigned pattern) noexcept;
  void advance_chain() noexcept;
  bool begin_song() noexcept;
  bool advance_song() noexcept;
  void move_song_row(unsigned at, bool inserted) noexcept;
  void load_row(unsigned row) noexcept;
  int next_row(unsigned from, unsigned length) const noexcept;
  void finish_song(o3sdk::dsp::BlockContext& ctx, unsigned frame) noexcept;
  int chain_position() const noexcept;
  int chain_queued() const noexcept;
  void publish() noexcept;
  void publish_chain() noexcept;
  bool emit(o3sdk::dsp::BlockContext& ctx, const o3sdk::dsp::Event& e) noexcept;
  bool emit_to(o3sdk::dsp::BlockContext& ctx, o3sdk::dsp::UnitId unit, const o3sdk::dsp::Event& e) noexcept;
  bool cond_passes(std::uint8_t cond, unsigned step, std::uint32_t pass, bool ahead) noexcept;
  bool variant_current() const noexcept;
  void build_variant() noexcept;
  void start_slew(o3sdk::dsp::BlockContext& ctx, const SlewCommand& c) noexcept;
  Slew* find_slew(std::uint8_t target, std::uint8_t id) noexcept;
  float slew_value(const Slew& s, std::uint32_t pos) const noexcept;
  bool slew_locked(const Slew& s) const noexcept;
  bool slew_locked_at(const Slew& s, unsigned frame) const noexcept;
  void mark_lock(std::uint8_t target, std::uint8_t id, unsigned frame, bool on) noexcept;
  void run_slews(o3sdk::dsp::BlockContext& ctx) noexcept;
  void run_wobble(o3sdk::dsp::BlockContext& ctx) noexcept;
  void note_started(o3sdk::dsp::BlockContext& ctx, unsigned frame, std::uint8_t note) noexcept;
  bool is_unit_target(std::uint8_t target) const noexcept;
  bool wobbleable(std::uint8_t target, std::uint8_t id) const noexcept;
  bool punched(std::uint8_t target, std::uint8_t id) const noexcept {
    for (const Punch& p : punch_) {
      if (p.active && p.target == target && p.id == id) {
        return true;
      }
    }
    return false;
  }
  void release_punched(o3sdk::dsp::BlockContext& ctx, std::uint8_t target, std::uint8_t id) noexcept;
  void run_punches(o3sdk::dsp::BlockContext& ctx) noexcept;
  bool wob_owns(const Slew& s) const noexcept { return wob_active_ && s.target == wob_target_ && s.id == wob_id_; }
  bool lane_lock_value(std::uint8_t target, std::uint8_t id, float& value) const noexcept;
  bool motion_holds(std::uint8_t target, std::uint8_t id) const noexcept;
  void line_step(o3sdk::dsp::BlockContext& ctx, unsigned frame, unsigned step, unsigned pattern_length) noexcept;
  void line_glide_q1(o3sdk::dsp::BlockContext& ctx, unsigned frame) noexcept;
  void line_release(o3sdk::dsp::BlockContext& ctx, unsigned frame, bool vowel, bool glide) noexcept;
  float wobble_value(std::uint8_t target, std::uint8_t id, float base, float amplitude) const noexcept;

  Bank& bank_;
  o3sdk::dsp::Engine* engine_ = nullptr;
  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  UnitTable tab_[kLaneTargetCount];  // indexed by a lane target; the mixer's and kLaneNone's are empty (the mixer has no unit); kLaneSeq's has the descriptors of its two values

  // ---- main -> render
  o3sdk::core::SpscRing<Command, 32> commands_;
  o3sdk::core::SpscRing<SlewCommand, 64> slew_commands_;
  std::atomic<int> live_transpose_{0};
  std::atomic<std::uint8_t> perf_{0};
  std::atomic<std::uint8_t> tract_line_on_{1};  // PLAY TRACT, on by default: the tract pattern (vowel line and the TONGUE / LIPS locks) drives the mouth; single writer (the main loop), read by the render at each step
  std::atomic<int> tract_head_{-1};             // single writer (the render), read by the main loop: the line's playhead
  std::atomic<std::uint8_t> fill_variant_{0};
  std::atomic<std::uint32_t> seed_;          // single writer (the main loop), read by the render when seed_version_ moves
  std::atomic<std::uint32_t> seed_version_{0};
  // The stored values of the targets' parameters (update()), per lane target; the mixer's fields are knob_[kLaneMixer][0..4].
  std::atomic<float> knob_[kLaneTargetCount][kKnobs];
  // The goal of the last slew(..., store = false) of each target and the stored value it left behind (main loop only): the next slew
  // starts from that goal, and update() gives it as the target's knob (the wobble's base), while the stored value is still the same.
  struct Unstored {
    float goal = 0.0f;
    float stored = 0.0f;
    bool valid = false;
  };
  Unstored unstored_[kLaneTargetCount][kKnobs];

  // The song, main -> render: a row is one word (pattern | repeat << 8 | transpose << 16 | flags << 24), so a row is never torn; a single
  // writer (the main loop). song_play_: bit 0 on | 8..15 the start row.
  std::atomic<std::uint32_t> song_rows_[kSongRows];
  std::atomic<std::uint32_t> song_len_{0};
  std::atomic<std::uint32_t> song_play_{0};
  std::atomic<std::uint32_t> song_loop_{1};

  // ---- render -> main
  std::atomic<std::uint32_t> song_pos_{0};  // bit 0 active | 1 done | 2 orphan | 8..15 row | 16..23 repeat
  std::atomic<std::uint32_t> playhead_{0};  // packed (sequencer.cpp)
  std::atomic<std::uint32_t> dropped_{0};
  std::atomic<std::uint32_t> slewing_{0};
  std::atomic<std::uint32_t> punch_mask_{0};  // single writer (the render, run_punches()): the slots held, for the screens
  Punch punch_[kPunchSlots];                  // render side only
  std::atomic<std::uint32_t> slew_overflow_{0};
  // The chain, published by the render when it changes: a seqlock (single writer: the render; odd while writing) over 4 words of
  // entries (4 bytes each) and a word of length, position and loop. chain() retries when a write overlapped its read.
  std::atomic<std::uint32_t> chain_seq_{0};
  std::atomic<std::uint32_t> chain_words_[kMaxQueue / 4];
  std::atomic<std::uint32_t> chain_meta_{0};
  mutable ChainView last_chain_;  // main loop: the last consistent view chain() read

  // ---- render state (only the render context touches it after setup)
  bool running_ = false;
  bool chain_loop_ = false;
  unsigned playing_ = 0;            // the pattern being played
  unsigned next_step_ = 0;          // index of the step that starts next
  int cur_step_ = -1;               // the step that started last
  int cur_point_ = 0;               // its quarter whose motion points went out last
  unsigned pt_q_ = 4;               // the next motion quarter of the current step (4: none left)
  int jump_to_ = -1;                // a pending jump
  std::uint64_t step_tick_ = 0;     // the tick the next step starts at
  std::uint64_t off_tick_ = 0;      // the tick the held note's gate ends at
  bool off_due_ = false;
  std::uint64_t pt_tick_ = 0;       // the tick of the next motion quarter
  std::uint64_t cur_start_ = 0;     // the current step's start and length in ticks
  std::uint32_t cur_len_ = 0;
  std::uint64_t block_end_tick_ = 0;  // the whole tick the last block ended at (the next block's first), for Playhead::phase
  int held_ = -1;                   // the note on the voice now (0..127), -1 none
  bool prev_slide_ = false;         // the last step held its note into the next one with a slide
  std::uint8_t chain_[kMaxQueue] = {};
  unsigned chain_len_ = 0;          // entries
  unsigned chain_next_ = 0;         // the entry that starts when the playing pattern ends (== chain_len_: none left)
  bool from_chain_ = false;         // the playing pattern is the entry before chain_next_ (not the selected one, not a jump)
  bool song_active_ = false;        // this run plays the song (begin_song())
  bool song_done_ = false;          // ... and it has finished (loop off): no more steps
  bool song_orphan_ = false;        // the playing row was deleted: song_row_ is the row that moved up into its place, which comes next (not song_row_ + 1)
  unsigned song_row_ = 0;           // the row playing
  unsigned song_rep_ = 0;           // plays of its pattern completed so far
  int song_transpose_ = 0;          // the row's transpose, added to the notes (0 outside a song)
  bool chain_dirty_ = true;         // the published chain is out of date
  std::uint8_t overridden_ = 0;     // motion lanes whose value the hook set (bit per lane)
  std::uint8_t lane_target_[kMotionLanes] = {};  // what an overridden lane drives (the pattern's table may change under it)
  std::uint8_t lane_id_[kMotionLanes] = {};
  float last_value_[kMotionLanes] = {};
  // The playing pattern, as of shadow_version_: two buffers, shadow_[cur_] is the one played from; the other receives the next
  // snapshot and becomes current only when the copy is consistent (refresh_shadow()).
  alignas(4) Pattern shadow_[2];
  unsigned cur_ = 0;
  std::uint32_t shadow_version_ = 0;
  bool shadow_valid_ = false;       // shadow_[cur_] is a consistent copy of the playing pattern at shadow_version_
  bool shadow_motion_ = false;      // it has at least one motion point (the Bank keeps the flag; the hook never scans)
  std::uint64_t pair_tick_ = 0;     // the tick the current pair (an even step and its odd step) began at

  // Ratchets: the hits of the current step still to come (hit 0 is played by start_step()).
  unsigned hit_n_ = 0;              // hits in the step (1 when it is not ratcheted)
  unsigned hit_k_ = 0;              // the next hit to play; == hit_n_: none left
  std::uint64_t hit_tick_ = 0;      // its tick
  std::uint8_t hit_note_ = 0;
  std::uint8_t hit_velocity_ = 0;   // the later hits: normal, or accented by ACC ALL
  std::uint8_t hit_note_flags_ = 0;
  std::uint8_t hit_gate_ = 0;       // the pattern's gate, percent
  bool hit_hold_ = false;           // the last hit goes on into the next step (slide, or a tie behind it)
  bool hit_slide_ = false;          // ... and the next note glides

  // Conditions and FILL.
  Rng rng_{kDefaultSeed};
  std::uint32_t seed_seen_ = 0;     // the seed_version_ rng_ was last seeded at
  // The TRACT line (render only). line_n_ counts the steps started since the pattern was loaded (not wrapped: the line wraps on its own
  // length). line_vowel_ / line_glide_: the line holds the tract's VOWEL / GLIDE (a vowel step wrote it, the knob is not back yet); the
  // values are what it wrote. line_gld_*: the glide the first quarter of the current GLD step applies.
  std::uint32_t line_n_ = 0;
  bool line_vowel_ = false;
  bool line_glide_ = false;
  float line_vowel_value_ = 0.0f;
  float line_glide_value_ = 0.0f;
  bool line_gld_pending_ = false;
  float line_gld_target_ = 0.0f;
  std::uint32_t pass_ = 0;          // times the playing pattern has started over since it was loaded
  // The draw of a probability condition that the look-ahead made for the step after a note (it asks a tie step's condition to know whether
  // the note is held into it): the step's own start uses it instead of drawing again, so there is one draw per step.
  bool draw_valid_ = false;
  unsigned draw_step_ = 0;
  std::uint32_t draw_pass_ = 0;
  unsigned draw_value_ = 0;         // 0..99
  bool fill_on_ = false;            // the fill variant is playing (FILL as the last step start saw it)
  std::uint32_t fill_count_ = 0;    // fills entered since the seed: the Mutate variant differs each time
  alignas(4) Pattern variant_;      // shadow_[cur_] with the fill applied, valid while variant_valid_ (a copy: the Bank is never touched)
  bool variant_valid_ = false;
  unsigned variant_pattern_ = 0;
  std::uint32_t variant_version_ = 0;
  std::uint8_t variant_kind_ = 0;
  std::uint32_t variant_fill_ = 0;

  // Where in this block the lanes' locks began and ended (reset at the start of a block): a slew is quiet exactly while its target is
  // locked, to the frame. A lock that lasts the whole block leaves no mark.
  struct LockMark {
    std::uint8_t target;
    std::uint8_t id;
    std::uint16_t from;  // the lock holds from this frame ...
    std::uint16_t to;    // ... up to (not including) this one; kOpen: to the end of the block
  };
  static constexpr unsigned kMaxMarks = 16;
  static constexpr std::uint16_t kOpen = 0xFFFF;
  LockMark marks_[kMaxMarks];
  unsigned mark_count_ = 0;

  // Slews.
  Slew slews_[kMaxSlews];
  unsigned slew_rr_ = 0;            // where the next block starts walking the table: a crowded block shares its events fairly

  // ---- Wobble. main -> render: one word (a single writer, the main loop) and the depth.
  //   bit 0 on | 1 reset on note | 2..5 rate | 6..8 shape | 9..11 target | 12..16 id | 17..21 override rate + 1 (0: none)
  std::atomic<std::uint32_t> wobble_;
  std::atomic<float> wobble_depth_{0.5f};
  std::atomic<int> wob_rate_now_{-1};  // render -> main
  // Render state.
  static constexpr unsigned kMaxWobbleResets = 4;
  static constexpr unsigned kMaxWobbleBase = 8;
  struct BaseEvent {
    std::uint16_t frame;
    float value;
  };
  std::uint32_t wob_cfg_ = 0;          // the word as of this block
  bool wob_active_ = false;            // the hook is moving wob_target_ / wob_id_
  std::uint8_t wob_target_ = 0;
  std::uint8_t wob_id_ = 0;
  std::uint64_t wob_phase_ = 0;        // Q0.64 turns at frame 0 of the next block
  std::uint64_t wob_inc_ = 0;          // per frame at the rate in use (wob_inc_tick_ is the transport inc and wob_inc_rate_ the rate it was made for)
  std::uint64_t wob_inc_tick_ = 0;
  unsigned wob_inc_rate_ = kWobbleRateCount;
  float wob_held_ = 0.0f;              // the sample and hold's value
  Rng wob_rng_{kDefaultSeed};
  std::uint16_t wob_resets_[kMaxWobbleResets] = {};  // frames of this block where a note started
  unsigned wob_reset_count_ = 0;
  BaseEvent wob_base_[kMaxWobbleBase] = {};          // writes to the wobbled target (locks, restores) in this block, in time order
  unsigned wob_base_count_ = 0;
  bool wob_hold_valid_ = false;        // a lane lock holds the base at the start of the block ...
  float wob_hold_ = 0.0f;              // ... at this value
  bool wob_emitted_ = false;
  float wob_last_ = 0.0f;
  int seq_rate_lane_ = -1;             // a motion lane on kLaneSeq: the rate it holds (-1: none) and the depth (< 0: none)
  float seq_depth_lane_ = -1.0f;
};

}  // namespace o3_303::seq
