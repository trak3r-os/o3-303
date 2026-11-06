// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pages of F1..F8 (docs/DESIGN-303.md section 3): the kit's ParamPage is one unit a page, and the 303's pages are not (INSTRUMENT's FX
// page reaches the distortion, the delay and a mixer send; MASTER's page reaches the engine's tempo, the master level and the delay's
// mode). So the app has its own page type over `Entry`s, the same controls convention as the kit's
// ParamKeys (o3sdk/app/param_keys.h):
//
//   tap an F key           the encoder drives that entry from now on (latched)
//   hold an F key + turn   that one moves while held; at the release the latched one comes back
//   hold two F keys + turn both move together
//   turn / Shift + turn    fine / coarse steps (the descriptor's `detents` and `coarse`)
//
// `Controls` is where an Entry meets its value: a parameter of a unit through the engine, or an app-level value (the tempo, a field
// of the current pattern, a generator setting, the send level, the master) through the descriptor tables below, so that all of them
// format, step and clamp through o3sdk's core::param_* the same way.

#include <o3sdk/app/context.h>
#include <o3sdk/app/input.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>
#include <o3_303/pattern.h>
#include <o3_303/perform.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/ops.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/snapshot.h>

#include <cstdint>

namespace o3_303 {

enum class Item : std::uint8_t {
  Blank,      // an unused key
  Voice,      // a parameter of the voice unit (id = its parameter id)
  Dist,       // ... of the distortion insert
  Delay,      // ... of the delay on send A
  Tempo,      // the engine's tempo
  Swing,      // the current pattern's swing
  Length,     // ... length
  Gate,       // ... gate
  Transpose,  // ... transpose
  Scale,      // the generator's scale
  Density,    // ... density
  SendA,      // the voice's level into send A (the delay)
  Master,     // the master level
  Level,      // slot 0's level after the distortion, in dB: the SOUND page's VOLUME (the voice's own volume stays at 0 dB)
  DistMode,   // the distortion's Active and Type as one key: OFF, GRIT, TUBE, HARD, FUZZ, FOLD
  Freeze,     // a parameter of the beat freeze on master insert 0 (id = its parameter id)
  FillVariant,  // what FILL plays (seq::FillVariant)
  Perf,       // the action of a performance button (id 0 Note, 1 Instr, 2 Vol, 3 Fx): a PerfAction
  Growl,      // M3c: a parameter of the growl on slot 0's insert 1 (id = its parameter id)
  Wobble,     // M3c: a setting of the wobble (id = a WobbleField), the sequencer's LFO; the settings are Wiring::wobble
  AccPct,     // H2: the generator's accent chance (project-wide, UX-303 D1); PATTERN SETUP's F7
  SldPct,     // H2: the generator's slide chance, its own entry; SETUP's F8
  RollMode,   // P2: the roll rate and the accelerate as one list (1/8, 1/16, 1/32, 1/64, BLD BEAT, BLD BAR): PERFORM SETUP's F7 (UX-303 5.4)
  Mods,       // B5a: the mods heard, the voice's Stock switch seen the other way round (1 = Stock off; docs/MODS-PLAN.md 5.11). An app item: no lane, no learn, no snapshot; MODDED's F8 and Shift + Sample Loader write it
  WobOn,      // MODS S2: the WOBBLE action as a key (WOBBLE's F8): the screen latches PERFORM's WOB action; the entry only gives the key its name and its ON / OFF. A snapshot target since B5a: set asks the latch (Wiring::wob_want)
  GrowlOn,    // B5a: GROWL's F8: the growl heard (Mix above 0); the screen flips Mix between 0 and Wiring::growl_last_mix
};

// The wobble's settings, the WOBBLE page's keys (docs/M3C-PLAN.md W). They apply while the WOBBLE performance action is on (a change while
// it is on applies at once); the reset-on-note is applied at once whatever.
enum WobbleField : std::uint16_t { kWobbleRate, kWobbleShape, kWobbleDepth, kWobbleReset, kWobbleTarget, kWobbleFieldCount };
enum WobbleTarget : std::uint8_t { kWobbleCutoff, kWobbleVowel, kWobbleTargetCount };  // the voice's Cutoff, the growl's Vowel

struct WobbleSettings {
  std::uint8_t rate = seq::kWobble1_8;      // a seq::WobbleRate
  std::uint8_t shape = seq::kWobbleSine;    // a seq::WobbleShape
  std::uint8_t depth_pct = 50;              // 0..100
  bool reset = false;                       // the phase restarts on each note (off by default: with it on a 16th-note line gets one bump a note, no wobble)
  std::uint8_t target = kWobbleCutoff;      // a WobbleTarget
};

struct Entry {
  Item item = Item::Blank;
  std::uint16_t id = 0;         // Voice, Dist, Delay: the unit's parameter id
  const char* label = nullptr;  // overrides the descriptor's name where the page wants a clearer one ("DLY TIME")
};

inline constexpr unsigned kKeys = 8;  // F1..F8

struct Page {
  const char* name;
  Entry entry[kKeys];
};

// The global table, two pages: SOUND is HOME's eight knobs (Rig::sound_keys; INSTRUMENT VOICE shares it) and PERF SETUP is PERFORM SETUP's page
// (UX-303 5.4): the four performance buttons' assignments, what FILL plays, the beat freeze's length, the ROLL mode (rate and accelerate in one
// list) and the STOP time. A PageKeys with no table of its own runs over it. The transport and the pattern's own settings are MASTER's and
// PATTERN SETUP's (`setup_page()`, the generator's settings too); a step's ratchet and condition are the PLAYS row (UX-303 D8); the SNAP page (R2)
// is PERFORM's row 2 now. The INSTRUMENT screen's pages are tables of their own, below.
inline constexpr unsigned kPageCount = 2;
enum PageId : unsigned { kPageSound = 0, kPagePerfSetup = 1 };

const Page& page_at(unsigned index) noexcept;  // an index past the table gives the first page

// The INSTRUMENT screen's key tables (docs/MODS-PLAN.md 5), one definition each, which the screen's groups point at and the tests read: FX (INSTRUMENT's
// second page), NOTES and TONE (MODDED), GROWL (the growl's seven parameters, in signal order) and WOBBLE (rate, shape, depth, target and reset, then F8
// WOB). Each PageKeys of the screen runs over one of them (set_table).
extern const Page kFxPage;
extern const Page kNotesPage;
extern const Page kTonePage;
extern const Page kGrowlPage;
extern const Page kWobblePage;

// PATTERN SETUP's page (UX-303 5.2), the screen's own table (PageKeys::set_table): F1 LENGTH, F2 SWING, F3 GATE, F4 TRANSP
// (this pattern's), F5 SCALE, F6 DENSITY, F7 ACC %, F8 SLD % (the generator's, project-wide). SCALE locks the keyboard (Wiring::scale_set).
inline constexpr unsigned kSetupPageCount = 1;
const Page& setup_page() noexcept;

// What a snapshot holds, in order: the voice's SOUND parameters, the distortion's, the delay's, slot 0's level and the delay send (the
// SOUND and FX pages' values), then, appended in P2 (UX-303 D29), the growl's VOWEL, FMT Q, GRL MIX and CMB MIX (GROWL), then (MODS S2, docs/MODS-PLAN.md 5.9) the voice's eleven
// mods (ids 8..18) and the growl's FMT MIX, CMB TUNE and CMB FB, then (B5a) the wobble's five settings and its latch, then the voice's RANGE and SUSTAIN; STOCK stays out. A project saved with fewer targets loads: the missing ones are filled
// from the sound (project.cpp). Each is an entry, so a value is in the units the entry shows and steps in.
inline constexpr unsigned kSnapTargetCount = 44;
// The first of B5a's six targets (the wobble's RATE, SHAPE, DEPTH, TARGET, RESET and its latch): no lane drives them, so a recall writes them at once (LiveActions::snap_apply). RANGE and
// SUSTAIN come after them and are the voice's, so they take the lane path as the other voice targets do.
inline constexpr unsigned kSnapWobbleFirst = 36;
// The targets before MODS S2's: from this index on a target that a recall or a morph leaves where it is stays untouched (LiveActions::snap_apply), so that a snapshot of a project that never used
// the mods does not write them: Sequencer::slew_value goes through the curve and back even when the blend is where the value is, and ACC DECAY comes back a few ulps off its
// stock constant (docs/MODS-PLAN.md 5.9).
inline constexpr unsigned kSnapTargetsBeforeMods = 22;
static_assert(kSnapTargetCount <= kSnapValues, "a snapshot has room for its targets");
const Entry& snap_target(unsigned index) noexcept;

// The units and values the entries reach. The app fills it once at setup.
// The ladder rows (backlog item 8): one kind of row, used three times. A row either follows the latched knob (target kLaneNone) or holds a knob of its own, stored
// as its lane target and parameter (not as an F key's number, so it survives a page change). HOME's FILT rows hold CUTOFF and RESO by default, PERFORM's row 3 follows.
inline constexpr unsigned kLadderRows = 3;
enum LadderRowId : unsigned { kLadderHome3 = 0, kLadderHome4 = 1, kLadderPerform = 2 };
inline constexpr LaneRef kDefaultLadder[kLadderRows] = {{kLaneVoice, 0}, {kLaneVoice, 1}, {kLaneNone, 0}};  // voice ids 0 Cutoff, 1 Reso

struct Wiring {
  o3sdk::dsp::UnitId voice = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId dist = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId delay = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId freeze = o3sdk::dsp::kNoUnit;  // master insert 0: the beat freeze (units/freeze, M3b R6)
  o3sdk::dsp::UnitId growl = o3sdk::dsp::kNoUnit;   // slot 0's insert 1: the growl (units/growl, M3c G); attached after the freeze (ids: voice 0, dist 1, delay 2, freeze 3)
  seq::Bank* bank = nullptr;
  seq::Sequencer* seq = nullptr;  // FILL's variant (PERFORM SETUP); null: the entry reads and writes nothing
  seq::GenParams gen;        // the generator's settings (PATTERN SETUP)
  float send_a_pct = 0.0f;   // the voice's send A level in percent: the base; DELAY THROW raises it for a while. A TB-303 is dry
  float level_db = -3.0f;    // slot 0's level in dB (SOUND F8): the voice peaks at 1 on its accents; headroom for the delay's wet signal
  bool throw_on = false;
  bool stock = false;        // the voice's Stock is on (the mirror of its parameter, for the chip of every screen): Controls::set(Item::Mods) is its one writer. Main loop only
  std::uint8_t perf[kPerfButtons] = {kDefaultPerf[0], kDefaultPerf[1], kDefaultPerf[2], kDefaultPerf[3]};  // PerfAction of Note, Instr, Vol, Fx
  bool scale_set = false;    // the player chose a scale (PATTERN SETUP's Scale): the keyboard is locked to it (R8). A boot has the generator's default
                             // scale without that, so the keyboard is chromatic until the Scale entry is touched; "No scale" or "Chromatic" unlocks it
  SnapBank snaps;            // the eight snapshots and the morph (PERFORM's row 2; saved in the project; the screens read and write it from the main loop)
  WobbleSettings wobble;     // the WOBBLE page's settings (main loop)
  std::int8_t wob_want = -1;  // B5a: the WOB latch a project load or a snapshot recall asks for (0 off, 1 on, -1 nothing): Controls::set(Item::WobOn) writes it, LiveActions::update takes it. Main loop only
  float growl_last_mix = 0.5f;  // B5a: GROWL's F8 gives Mix this back (Mix as a 0..1 fraction); the Mix F8 read from the engine when it last turned the growl off, 50 % at a boot, never stored. Main loop only
  LaneRef ladder[kLadderRows] = {kDefaultLadder[0], kDefaultLadder[1], kDefaultLadder[2]};  // what each ladder row holds (main loop; saved in the project's PERF chunk)
};

// How many fine detents one press of Left or Right moves a knob with an F key held: 1/20 of the range (twenty presses from minimum to maximum),
// and one for an enum, a switch or a knob of fewer than 20 detents.
int press_detents(const o3sdk::core::ParamDesc& d) noexcept;

class Controls {
 public:
  void init(Wiring& wiring) noexcept { w_ = &wiring; }
  Wiring& wiring() noexcept { return *w_; }
  const Wiring& wiring() const noexcept { return *w_; }

  // The descriptor of an entry (null for a blank or an unknown id).
  const o3sdk::core::ParamDesc* desc(const o3sdk::dsp::Engine& e, const Entry& entry) const noexcept;
  // press_detents() of the entry's descriptor (1 for a blank).
  int press_detents(const o3sdk::dsp::Engine& e, const Entry& entry) const noexcept;
  // true for a knob that takes the encoder's speed (Context::enc_scale): a continuous value, not an enum or a switch (one value a detent) nor a blank.
  bool continuous(const o3sdk::dsp::Engine& e, const Entry& entry) const noexcept;
  float get(const o3sdk::dsp::Engine& e, const Entry& entry) const noexcept;
  // Clamped through the descriptor and applied: to the engine, the pattern, the settings.
  void set(o3sdk::dsp::Engine& e, const Entry& entry, float value) noexcept;
  // `detents` encoder steps (`coarse`: a coarse step each); the new value is applied.
  void step(o3sdk::dsp::Engine& e, const Entry& entry, int detents, bool coarse) noexcept;
  // The value without its unit text ("1.20k", "SAW", "25/20") and the unit text ("Hz", "%").
  unsigned format(const o3sdk::dsp::Engine& e, const Entry& entry, char* out, unsigned cap) const noexcept;
  const char* unit_text(const o3sdk::dsp::Engine& e, const Entry& entry) const noexcept;
  const char* label(const o3sdk::dsp::Engine& e, const Entry& entry) const noexcept;

  // ---- Motion lanes (docs/M3B-PLAN.md C2): a pattern's `lanes` table says what each lane drives (pattern.h LaneRef), so an entry is a lane
  // target, not "the voice's id i". The point of a lane is the target's own range: the descriptor's curve for a unit's parameter, the
  // fixed range of pattern.h for the mixer.
  // What an entry drives when a lane points at it; false for the entries no lane can drive (tempo, swing, the generator, the master ...).
  // The SOUND page's VOLUME is slot 0's mixer Level and DLY SEND its SendA.
  bool lane_ref(const Entry& entry, LaneRef& out) const noexcept;
  // The entry that shows a lane's target (Blank when none of the pages does, e.g. the mixer's pan).
  Entry lane_entry(const LaneRef& ref) const noexcept;
  // The name of what a lane drives ("CUTOFF", "DRIVE", "SEND A"), "" for kLaneNone.
  const char* lane_name(const o3sdk::dsp::Engine& e, const LaneRef& ref) const noexcept;
  // An entry's value in the lane's own units (the sequencer's record_motion and slew take these) and back: the same for a unit's
  // parameter; for the mixer's Level the linear gain of the dB the entry shows, for the send the percent as a gain.
  float to_lane(const Entry& entry, float entry_value) const noexcept;
  float from_lane(const Entry& entry, float lane_value) const noexcept;
  // A motion point (0..254) for a value of the entry (in the entry's units) and the value of a point, through the target's own
  // descriptor (or the mixer's range). 0 and the entry's own value for an entry no lane can drive.
  std::uint8_t to_point(const o3sdk::dsp::Engine& e, const Entry& entry, float entry_value) const noexcept;
  float from_point(const o3sdk::dsp::Engine& e, const Entry& entry, std::uint8_t point) const noexcept;

  // The snapshot targets' values now (kSnapTargetCount of them, `out` holds kSnapValues; the rest is zeroed) and whether target `index` is
  // discrete (an enum: it switches, a morph does not glide it).
  void snap_capture(const o3sdk::dsp::Engine& e, float* out) const noexcept;
  bool snap_discrete(const o3sdk::dsp::Engine& e, unsigned index) const noexcept;

  // The wobble on or off with the settings of Wiring::wobble (set_wobble_reset always): the WOBBLE action calls it at its press and release, and a
  // change of a setting while the wobble runs calls it again. The target is {kLaneVoice, Cutoff} or {kLaneGrowl, Vowel}.
  void apply_wobble(bool on) const noexcept;
  LaneRef wobble_target() const noexcept;

  // The level DELAY THROW and the base level give the engine now: call after the base or the throw changes.
  void apply_send(o3sdk::dsp::Engine& e) const noexcept;
  // Slot 0's level from level_db (silence at or below -59.5 dB, the engine's 1.5 cap above): call after it changes.
  void apply_level(o3sdk::dsp::Engine& e) const noexcept;

 private:
  Wiring* w_ = nullptr;
};

// Focus handling for the pages: which page, which key the encoder drives, which keys are held. The routing of a turn is the caller's
// (a held step pad takes it for a p-lock first), `input()` takes F keys and turns.
class PageKeys {
 public:
  // The pages this one runs over: a table of `count` pages that lives as long as the keys (constexpr data of a screen). The default is the
  // global table (page_at, kPageCount), which the SOUND latch and PERFORM SETUP use; a screen with pages of its own sets its table and
  // starts on page 0 with key 0 latched.
  void set_table(const Page* table, unsigned count) noexcept;
  void set_page(unsigned index) noexcept;
  void next_page() noexcept { set_page((page_ + 1u) % page_count()); }
  unsigned page_index() const noexcept { return page_; }
  unsigned page_count() const noexcept { return table_ != nullptr ? count_ : kPageCount; }
  const Page& page() const noexcept { return table_ != nullptr ? table_[page_] : page_at(page_); }

  // The encoder moved while an F key was held without going through turn() (a p-lock on the held key's parameter): at the release the
  // latched key comes back, as after any hold-and-turn.
  void mark_turned() noexcept {
    if (held_ != 0u) {
      turned_ = true;
    }
  }

  // The key the encoder drives now (a held one, else the latched one), 0..7.
  unsigned focus() const noexcept { return held_ != 0u ? last_held_ : latched_; }
  unsigned held() const noexcept { return held_; }  // bit n = F(n+1)
  const Entry& focused() const noexcept { return page().entry[focus()]; }

  // An F key press or release (as the kit's convention) and an encoder turn. true when it took the input.
  bool input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in, Controls& controls) noexcept;
  void turn(o3sdk::app::Context& ctx, Controls& controls, int detents, bool coarse) noexcept;
  // A press of Left or Right with an F key held (no Shift) is a turn of 1/20 of the range (press_detents): true and `out` the encoder turn it stands for
  // (one detent either way), to go through the screen's turn path (a held step's lock, Rec's motion), which then calls end_press(). One shared path
  // for every screen whose F keys own knobs.
  bool arrow_turn(const o3sdk::app::Input& in, o3sdk::app::Input& out) noexcept;
  void end_press() noexcept { press_ = false; }
  // The buttons a screen with F-key knobs asks the kit to repeat while held (Screen::repeat_buttons): Left and Right. The mask does not depend on an F key being
  // held; a repeat with none held (or let go meanwhile) is dropped by the screen (arrow_turn is false and the screen returns on Input::repeat), so Left and Right
  // act as they always did. One undo per held arrow: the repeats are edits of one key, which the Bank coalesces as the encoder's turns.
  static std::uint64_t repeat_arrows() noexcept {
    return (std::uint64_t{1} << static_cast<unsigned>(o3sdk::app::Button::Left)) | (std::uint64_t{1} << static_cast<unsigned>(o3sdk::app::Button::Right));
  }
  // The detents of the turn in flight as a held step's lock takes them: an arrow's press is press_detents() of the knob, a turn its own, at the encoder's
  // speed (Context::enc_scale) for a continuous knob, not for Shift (coarse) nor an enum. The scale is asked once per turn: turn() takes the answer
  // from here when it follows (the knob path's one place for the speed; PageKeys::turn is the other half).
  int lock_detents(o3sdk::app::Context& ctx, const Controls& controls, const Entry& entry, int detents, bool coarse) noexcept;
  // The state the focus returns to when the screen is left with keys down.
  void release_all() noexcept;

 private:
  const Page* table_ = nullptr;  // null: the global table
  unsigned count_ = 0;
  unsigned page_ = 0;
  unsigned latched_ = 0;
  unsigned before_hold_ = 0;
  unsigned held_ = 0;
  unsigned last_held_ = 0;
  bool turned_ = false;
  int scaled(o3sdk::app::Context& ctx, int detents) noexcept;
  int fast_ = 0;  // enc_scale()'s answer for fast_raw_ detents, asked by lock_detents() and taken by the turn() after it
  int fast_raw_ = 0;
  std::uint32_t fast_at_ = 0;  // Context::now() when fast_ was asked: valid only in the same tick and for the same raw detents (one turn event)
  bool have_fast_ = false;
  bool press_ = false;  // the turn in flight is an arrow's press: each knob moves press_detents(), not the detents
};

}  // namespace o3_303
