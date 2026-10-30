// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The 303 screen (docs/DESIGN-303.md sections 2 to 5, docs/UX-303.md 5.1): the step view as the hero, the motion lane, the knob strip, the soft keys, the
// status line and the PADS line over it, and the gestures of the pages, the tools and Master. Also the pattern grid it shows while Pattern is held.
//
// The pads, the arrows, Copy / Insert / Delete, Rec and the colour buttons are the home map's (home_map.h): this screen forwards them to its HomeMap
// and draws what it reads from it. It is one class in three files: main_screen.cpp (the pages, the tools, the drawing), main_screen_perf.cpp (the Master
// gestures) and main_screen_snap.cpp (the SNAP page, the RANDOM popup). What the four assignable performance buttons do and the snapshots themselves
// are LiveActions' (live_actions.h), shared with PERFORM.

#include "home_map.h"
#include "knob_strip.h"
#include "live_actions.h"
#include "rig.h"

#include <o3sdk/app/screen.h>
#include <o3sdk/ui/components.h>
#include <o3_303/pad_map.h>
#include <o3_303/pages.h>
#include <o3_303/perform.h>
#include <o3_303/step_model.h>

#include <cstdint>

namespace o3_303 {

class MainScreen final : public o3sdk::app::Screen {
 public:
  void init(Rig& rig) noexcept;

  const char* title() const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& keys) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;

  bool roll() const noexcept { return roll_; }
  bool pattern_overlay() const noexcept { return home_.overlay(); }
  int cursor() const noexcept { return home_.cursor(); }
  bool rec() const noexcept { return home_.rec(); }
  int octave() const noexcept { return home_.octave(); }
  // What PERFORM's key row and encoder ask of the home map's keyboard (one keyboard, one octave, one scale lock, one Rec).
  void toggle_rec(o3sdk::app::Context& ctx) noexcept { home_.toggle_rec(ctx); }
  bool scale_locked_out(int note) const noexcept { return home_.scale_locked_out(note); }
  // An encoder move or a ladder tap is written as motion under the playhead (Rec while the pattern plays).
  bool recording_motion() const noexcept { return home_.recording_motion(); }
  unsigned page_index() const noexcept { return keys_.page_index(); }
  // A line in the status line for a moment (what a tool or a load just did).
  // `off`: the line says something went off ("ENCODER: THE PAGE"): drawn as an outline, not a filled chip.
  void toast(o3sdk::app::Context& ctx, const char* text, bool off = false) noexcept { home_.toast(ctx, text, off); }

 private:
  // What the encoder is latched to by a tap of Master (R7), whatever page the F keys are on; an F key press gives it back to the page.
  enum class EncLatch : std::uint8_t { None, Tempo, Swing };

  // ---- input
  void button_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void turn_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void tool(o3sdk::app::Context& ctx, unsigned index) noexcept;

  // ---- performance (main_screen_perf.cpp; the four buttons' actions are LiveActions', the keyboard and the ladders the home map's)
  void master_press(o3sdk::app::Context& ctx, bool shift) noexcept;
  void master_release(o3sdk::app::Context& ctx) noexcept;
  void master_turn(o3sdk::app::Context& ctx, int detents) noexcept;
  void nudge_end(o3sdk::app::Context& ctx) noexcept;
  void latch_turn(o3sdk::app::Context& ctx, int detents, bool coarse) noexcept;
  // ---- the SNAP page and the RANDOM popup (main_screen_snap.cpp; the snapshots themselves are LiveActions')
  bool on_snap_page() const noexcept { return keys_.page_index() == kPageSnap; }
  void snap_press(o3sdk::app::Context& ctx, unsigned slot, bool shift) noexcept;
  void snap_turn(o3sdk::app::Context& ctx, int detents, bool coarse) noexcept;
  void random_toggle(o3sdk::app::Context& ctx) noexcept;
  void random_press(o3sdk::app::Context& ctx, unsigned key) noexcept;
  void random_generate(o3sdk::app::Context& ctx) noexcept;
  void draw_snap(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_random(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_state_chips(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& strip) noexcept;
  bool tools_visible(const o3sdk::app::Context& ctx) const noexcept;

  // ---- model helpers
  unsigned sel() const noexcept { return rig_->bank->selected(); }
  const Pattern& pat() const noexcept { return rig_->bank->pattern(rig_->bank->selected()); }
  unsigned length() const noexcept { return pat().length != 0u ? pat().length : kDefaultLength; }
  std::uint32_t next_seed(o3sdk::app::Context& ctx) noexcept;
  // What the encoder moves, for the status line: "F1 CUTOFF", "TEMPO", "MORPH".
  void enc_text(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;

  // ---- drawing
  void draw_info(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_strip(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_tabs(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area, const char* hint) noexcept;
  void fill_knob(o3sdk::app::Context& ctx, unsigned k, const Entry& entry, view::Knob& out) noexcept;
  void draw_tools(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_grid(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;

  Rig* rig_ = nullptr;
  Controls* controls_ = nullptr;
  LiveActions* live_ = nullptr;
  HomeMap home_;
  PageKeys keys_;

  // Modes.
  bool roll_ = false;

  // The pitch window keeps what it has shown for the pattern, so it does not jump while notes are entered.
  int win_pattern_ = -1;
  int win_lo_ = 0;
  int win_hi_ = 0;
  bool win_roll_ = false;

  EncLatch enc_latch_ = EncLatch::None;
  // Master (R7): a tap latches the encoder, a hold and a turn nudges the tempo and gives it back at the release.
  bool master_down_ = false;
  bool master_used_ = false;
  bool master_shift_ = false;      // Shift was down at the press: a tap latches SWING
  std::uint64_t master_down_ms_ = 0;
  int nudge_pct_ = 0;              // -3..+3
  std::uint32_t nudge_base_ = 0;   // the tempo (milli-BPM) the nudge started from
  std::uint32_t tool_mask_ = 0;     // F keys pressed as tools (their release is not the pages')
  std::uint64_t shift_ms_ = 0;      // when Shift went down, and when the encoder last moved: the tools row waits for stillness
  std::uint64_t last_turn_ms_ = 0;

  // The RANDOM popup (Shift + F1, C3): while it is open the knob strip is its three settings, a key to generate and a key to close.
  bool random_open_ = false;
  unsigned random_focus_ = 0;      // the setting the encoder drives, 0..kRandomEntries-1
};

}  // namespace o3_303
