// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MASTER (docs/UX-303.md 5.6, D5): the tempo and the output. A tap of Master opens it, at the release, with the encoder latched on TEMPO (F1); F2 TAP is
// the tap tempo, F3 NUDGE (hold + turn) the tempo nudge, F4 the master level. The status line, the pads and the keyboard are the home map's (home_map.h), the
// very one HOME has; the colour buttons do nothing here. Readout: the tempo large, the nudge, where the clock comes from (INTERNAL, or MIDI while the kit
// follows a master), and the master's peaks as two meters. (CPU % is left off MASTER, UX-303 D19: the kit's header strip already shows it.)
//
// The class is also the global Master's: o3_303_app.cpp takes the Master button and the encoder while it is held from every screen and hands them to
// master_press() / master_turn() / master_release(), so that no other screen sees them (UX-303 2.6):
//
//   tap (under kHoldMs, no turn, nothing else pressed)   MASTER opens at the release
//   hold + turn                                          the tempo nudge, 1 % a detent, at most 3 %, back at the release; no screen change
//   Shift + Master                                       one tap of the tap tempo, from any screen; the release opens nothing
//
// The view logic is apart from the drawing: the free functions below (pure, host-tested) make the text and the bars, MasterScreen::fill_view() makes a
// MasterView from the state, and draw() draws it, so the skin replaces the drawing and not the logic. Main loop only.

#include "frame.h"
#include "home_map.h"
#include "knob_strip.h"
#include "rig.h"

#include <o3sdk/app/screen.h>
#include <o3_303/pages.h>
#include <o3_303/tap_tempo.h>

#include <cstdint>

namespace o3_303 {

// The meters' scale: kMeterMax is full scale (0 dB), kMeterFloorDb the level at the left end.
inline constexpr unsigned kMeterMax = 1000;
inline constexpr float kMeterFloorDb = -48.0f;

// "125.0": the tempo (milli-BPM) to a tenth of a BPM, rounded.
unsigned tempo_text(std::uint32_t mbpm, char* out, unsigned cap) noexcept;
// "+0 %", "-2 %": the nudge's percent (clamped to the nudge's range).
unsigned nudge_text(int pct, char* out, unsigned cap) noexcept;
// A linear peak (1.0 full scale) as a place on the meter, 0..kMeterMax, on a dB scale from kMeterFloorDb to 0 dB.
unsigned meter_norm(float peak) noexcept;
// A meter's value after `dt_ms` with `now` measured: it jumps up to `now` and falls back by `fall_per_s` of the scale a second (whole units: call it with
// a dt of some milliseconds, not one).
unsigned meter_fall(unsigned held, unsigned now, unsigned dt_ms, unsigned fall_per_s) noexcept;

// What the screen shows besides the status line and the PADS line.
struct MasterView {
  view::Knob knob[view::kKnobs];
  char tempo[12] = "";       // "125.0" (a followed master's tempo while it is followed)
  char nudge[8] = "";        // "+0 %"
  bool nudged = false;       // the nudge is not 0
  const char* clock = "";    // "INTERNAL" or "MIDI"
  char taps[4] = "";         // "2" the taps of the measure, "" none
  unsigned level[2] = {};    // the meters: the level and the held peak of L and R, 0..kMeterMax
  unsigned peak[2] = {};
};

// F2 TAP and F3 NUDGE are actions, not values: the boxes the strip draws for them. `taps` is the measure's count (0: "--"), `pct` the nudge, `held` F3 is down.
void fill_tap_knob(unsigned taps, view::Knob& out) noexcept;
void fill_nudge_knob(int pct, bool held, view::Knob& out) noexcept;

class MasterScreen final : public o3sdk::app::Screen {
 public:
  // `home`: the home map HOME has.
  void init(Rig& rig, HomeMap& home) noexcept;

  // ---- The global Master (the app routes them from every screen)
  // The Master button went down (`shift`: Shift was held: a tap of the tap tempo).
  void master_press(o3sdk::app::Context& ctx, bool shift) noexcept;
  // Any other press while Master is down (a chord): the release is no tap.
  void master_chord() noexcept { master_used_ = true; }
  // The encoder moved while Master is down: the tempo nudge. false when Master is not down (the turn is the screen's).
  bool master_turn(o3sdk::app::Context& ctx, int detents) noexcept;
  // The Master button went up: the nudge ends; a tap opens MASTER.
  void master_release(o3sdk::app::Context& ctx) noexcept;
  bool master_down() const noexcept { return master_down_; }
  // Every pass of the main loop, whatever screen is up (the app calls it): a tempo restore that the engine's full command ring refused is tried again.
  void service(o3sdk::app::Context& ctx) noexcept;

  // One tap of the tap tempo (Shift + Master, F2): the tempo from the gaps once there are two taps.
  void tap(o3sdk::app::Context& ctx) noexcept;
  unsigned taps() const noexcept { return tap_tempo_.count(); }
  // The nudge, in percent, -3..+3 (0 when none): the base tempo is kept and comes back at nudge_end().
  int nudge() const noexcept { return nudge_pct_; }

  const char* title() const noexcept override { return "MASTER"; }
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& keys) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;
  // HELP (Shift + Config, help_texts.h): MASTER's texts and the home map's pads.
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& h) const noexcept override;

  // The F keys and the entry the encoder drives (the tests read them).
  const Page& page_table() const noexcept { return keys_.page(); }
  unsigned focus() const noexcept { return keys_.focus(); }
  const MasterView& last_view() const noexcept { return view_; }

 private:
  void open(o3sdk::app::Context& ctx) noexcept;
  void button_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void turn_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void nudge_turn(o3sdk::app::Context& ctx, int detents) noexcept;
  void nudge_end(o3sdk::app::Context& ctx) noexcept;
  void forget_base() noexcept;
  static std::uint32_t nudged_clamped(std::uint32_t base, int pct) noexcept;
  void fill_view(o3sdk::app::Context& ctx, MasterView& view) noexcept;
  void enc_text(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;

  Rig* rig_ = nullptr;
  Controls* controls_ = nullptr;
  HomeMap* home_ = nullptr;
  PageKeys keys_;  // F1 TEMPO and F4 MASTER; F2 and F3 are blank in the table, the screen takes them itself

  // The Master button.
  bool master_down_ = false;
  bool master_used_ = false;  // a turn, Shift at the press or another press: not a tap
  std::uint64_t master_ms_ = 0;

  // The nudge (the button's or F3's): the engine's own tempo is moved from `nudge_base_`, never the followed master's.
  int nudge_pct_ = 0;
  std::uint32_t nudge_base_ = 0;
  bool restore_pending_ = false;  // the base was not accepted by a full command ring: service() sends it again (Rig::tempo_base_mbpm stays set until then)
  bool f3_down_ = false;

  TapTempo tap_tempo_;
  std::uint32_t tool_mask_ = 0;  // F keys pressed with Shift (the Shift row is empty: their release is not the page's)

  // The meters (update(): the main loop's passes, faster than the frames, so a short peak is not missed).
  unsigned level_[2] = {};
  unsigned peak_[2] = {};
  std::uint64_t meter_ms_ = 0;
  MasterView view_;
};

}  // namespace o3_303
