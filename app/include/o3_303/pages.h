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
#include <o3_303/seq/bank.h>
#include <o3_303/seq/ops.h>

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

inline constexpr unsigned kPageCount = 3;  // SOUND, FX, SEQ (MODS comes with the voice's extra parameters)
enum PageId : unsigned { kPageSound = 0, kPageFx = 1, kPageSeq = 2 };

const Page& page_at(unsigned index) noexcept;

// The units and values the entries reach. The app fills it once at setup.
struct Wiring {
  o3sdk::dsp::UnitId voice = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId dist = o3sdk::dsp::kNoUnit;
  o3sdk::dsp::UnitId delay = o3sdk::dsp::kNoUnit;
  seq::Bank* bank = nullptr;
  seq::GenParams gen;        // the generator's settings (the SEQ page)
  float send_a_pct = 30.0f;  // the voice's send A level in percent: the base; DELAY THROW raises it for a while
  float level_db = -6.0f;    // slot 0's level in dB (SOUND F8): headroom for the delay's wet signal and the accents
  bool throw_on = false;
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

  // The motion lane of an entry: the voice's ids 0..7 are the lanes of a pattern. false for anything else.
  bool lane(const Entry& entry, unsigned& lane) const noexcept;

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
