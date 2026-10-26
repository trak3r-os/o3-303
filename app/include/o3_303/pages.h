// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pages of F1..F8 (docs/DESIGN-303.md section 3): the kit's ParamPage is one unit a page, and the 303's pages are not (the FX page
// reaches the distortion, the delay, a mixer send and the master; the SEQ page reaches the engine's tempo, the current pattern's
// fields and the generator's settings). So the app has its own page type over `Entry`s, the same controls convention as the kit's
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
  AccSlide,   // ... accent chance (the slide chance follows at four fifths of it)
  SendA,      // the voice's level into send A (the delay)
  Master,     // the master level
  Level,      // slot 0's level after the distortion, in dB: the SOUND page's VOLUME (the voice's own volume stays at 0 dB)
  DistMode,   // the distortion's Active and Type as one key: OFF, GRIT, TUBE, HARD, FUZZ, FOLD
  Freeze,     // a parameter of the beat freeze on master insert 0 (id = its parameter id)
  Ratchet,    // the ratchet of a step: wiring.step_focus (the held step, else the cursor's), x1 .. x4
  Cond,       // ... its trig condition, as a place on the encoder's walk (seq::step_cond)
  FillVariant,  // what FILL plays (seq::FillVariant)
  Perf,       // the action of a performance button (id 0 Note, 1 Instr, 2 Vol, 3 Fx): a PerfAction
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

// SOUND, FX, SEQ and PERF (MODS comes with the voice's extra parameters). PERF is o3-303's own page until the kit's system Config exists
// (docs/M3B-PLAN.md C4): the four performance buttons' assignments, what FILL plays, the beat freeze's length, and the ratchet and the
// trig condition of a step. M3b's C3 moves the last three to the SEQ page when the RANDOM popup frees F6..F8 there.
inline constexpr unsigned kPageCount = 4;
enum PageId : unsigned { kPageSound = 0, kPageFx = 1, kPageSeq = 2, kPagePerf = 3 };

const Page& page_at(unsigned index) noexcept;

// The units and values the entries reach. The app fills it once at setup.
struct Wiring {
  o3sdk::dsp::UnitId voice = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId dist = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId delay = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId freeze = o3sdk::dsp::kNoUnit;  // master insert 0: the beat freeze (units/freeze, M3b R6)
  seq::Bank* bank = nullptr;
  seq::Sequencer* seq = nullptr;  // FILL's variant (the PERF page); null: the entry reads and writes nothing
  seq::GenParams gen;        // the generator's settings (the SEQ page)
  float send_a_pct = 30.0f;  // the voice's send A level in percent: the base; DELAY THROW raises it for a while
  float level_db = -6.0f;    // slot 0's level in dB (SOUND F8): headroom for the delay's wet signal and the accents
  bool throw_on = false;
  std::uint8_t perf[kPerfButtons] = {kDefaultPerf[0], kDefaultPerf[1], kDefaultPerf[2], kDefaultPerf[3]};  // PerfAction of Note, Instr, Vol, Fx
  bool scale_set = false;    // the player chose a scale (the SEQ page's Scale): the keyboard is locked to it (R8). A boot has the generator's default
                             // scale without that, so the keyboard is chromatic until the Scale entry is touched; "No scale" or "Chromatic" unlocks it
  unsigned step_focus = 0;   // the step the Ratchet and Cond entries show and edit: the screen sets it (a held step, else the cursor)
};

class Controls {
 public:
  void init(Wiring& wiring) noexcept { w_ = &wiring; }
  Wiring& wiring() noexcept { return *w_; }
  const Wiring& wiring() const noexcept { return *w_; }

  // The descriptor of an entry (null for a blank or an unknown id).
  const o3sdk::core::ParamDesc* desc(const o3sdk::dsp::Engine& e, const Entry& entry) const noexcept;
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

  // The entries that belong to one step (Ratchet, Cond): edited on `step` rather than on wiring.step_focus. `detents` as step() gives them.
  static bool is_step_entry(const Entry& entry) noexcept { return entry.item == Item::Ratchet || entry.item == Item::Cond; }
  void step_at(o3sdk::dsp::Engine& e, const Entry& entry, unsigned step, int detents, bool coarse) noexcept;

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
  void set_page(unsigned index) noexcept;
  void next_page() noexcept { set_page((page_ + 1u) % kPageCount); }
  void prev_page() noexcept { set_page((page_ + kPageCount - 1u) % kPageCount); }
  unsigned page_index() const noexcept { return page_; }
  const Page& page() const noexcept { return page_at(page_); }

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
  // The state the focus returns to when the screen is left with keys down.
  void release_all() noexcept;

 private:
  unsigned page_ = 0;
  unsigned latched_ = 0;
  unsigned before_hold_ = 0;
  unsigned held_ = 0;
  unsigned last_held_ = 0;
  bool turned_ = false;
};

}  // namespace o3_303
