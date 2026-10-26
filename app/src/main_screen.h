// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The 303 screen (docs/DESIGN-303.md sections 2 to 5): the step view as the hero, the motion lane, the knob strip, the soft keys and
// every pad and button gesture that does not need the card or MIDI. Also the pattern grid it shows while Pattern is held.
//
// It is one class in three files: main_screen.cpp (the step editing, the pages, the tools, the drawing), main_screen_perf.cpp (the
// performance gestures of M3b: the four assignable performance buttons, the KEYS ladders, the Master gestures, the scale lock) and
// main_screen_snap.cpp (the SNAP page and its morph, the RANDOM popup).

#include "knob_strip.h"
#include "rig.h"

#include <o3sdk/app/screen.h>
#include <o3sdk/ui/components.h>
#include <o3_303/pad_map.h>
#include <o3_303/pages.h>
#include <o3_303/perform.h>
#include <o3_303/step_model.h>

#include <cstdint>

namespace o3_303 {

inline constexpr std::uint64_t kHoldMs = 300;  // a press shorter than this is a tap; Pattern held this long: the 48 pads are the patterns

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
  bool pattern_overlay() const noexcept { return overlay_; }
  int cursor() const noexcept { return cursor_; }
  bool rec() const noexcept { return rec_; }
  bool keys_live() const noexcept { return keys_live_; }
  int octave() const noexcept { return octave_; }
  unsigned page_index() const noexcept { return keys_.page_index(); }
  // A line in the info strip for a moment (what a tool or a load just did).
  // `off`: the line says something went off ("MOTION REC OFF"): drawn as an outline, not a filled chip.
  void toast(o3sdk::app::Context& ctx, const char* text, bool off = false) noexcept;

 private:
  // What the encoder is latched to by a tap of Master (R7), whatever page the F keys are on; an F key press gives it back to the page.
  enum class EncLatch : std::uint8_t { None, Tempo, Swing };

  static constexpr unsigned kRoles = static_cast<unsigned>(PadRole::None) + 1u;

  // ---- input
  void pad_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void button_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void turn_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void key_press(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void key_release(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void step_press(o3sdk::app::Context& ctx, unsigned k) noexcept;
  void modifier_tap(o3sdk::app::Context& ctx, PadRole role) noexcept;
  void modifier_with_steps(o3sdk::app::Context& ctx, PadRole role) noexcept;
  void tool(o3sdk::app::Context& ctx, unsigned index) noexcept;
  void plock_turn(o3sdk::app::Context& ctx, int lane, int detents, bool coarse) noexcept;
  void step_entry_turn(o3sdk::app::Context& ctx, const Entry& entry, int detents, bool coarse) noexcept;
  void ratchet_step(o3sdk::app::Context& ctx, int direction) noexcept;
  void rotate_layers(o3sdk::app::Context& ctx, int direction) noexcept;
  void assign_lane(o3sdk::app::Context& ctx, const Entry& entry) noexcept;
  int lane_of(const Entry& entry, const Pattern& p) const noexcept;
  void sync_step_focus() noexcept;

  // ---- performance (main_screen_perf.cpp)
  void perf_press(o3sdk::app::Context& ctx, unsigned button, bool shift) noexcept;
  void perf_release(o3sdk::app::Context& ctx, unsigned button) noexcept;
  void apply_perf(o3sdk::app::Context& ctx) noexcept;
  void wobble_pad(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void wobble_leds(o3sdk::app::PadLeds& leds) const noexcept;
  bool wobble_on() const noexcept { return (perf_applied_ & perf_bit(PerfAction::Wobble)) != 0u; }
  void sync_perf_assignments(o3sdk::app::Context& ctx) noexcept;
  void push_live_transpose() noexcept;
  void master_press(o3sdk::app::Context& ctx, bool shift) noexcept;
  void master_release(o3sdk::app::Context& ctx) noexcept;
  void master_turn(o3sdk::app::Context& ctx, int detents) noexcept;
  void nudge_end(o3sdk::app::Context& ctx) noexcept;
  void latch_turn(o3sdk::app::Context& ctx, int detents, bool coarse) noexcept;
  void ladder_press(o3sdk::app::Context& ctx, unsigned pad, bool shift) noexcept;
  void ladder_leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept;
  // ---- snapshots and the RANDOM popup (main_screen_snap.cpp)
  bool on_snap_page() const noexcept { return keys_.page_index() == kPageSnap; }
  void snap_press(o3sdk::app::Context& ctx, unsigned slot, bool shift) noexcept;
  void snap_turn(o3sdk::app::Context& ctx, int detents, bool coarse) noexcept;
  void snap_apply(o3sdk::app::Context& ctx, float ms) noexcept;     // the morph's blend on the sequencer's slews (nothing stored yet)
  void snap_finish(o3sdk::app::Context& ctx, bool at_once) noexcept;  // the blend stored in the engine, once the slews are over (or now)
  void random_toggle(o3sdk::app::Context& ctx) noexcept;
  void random_press(o3sdk::app::Context& ctx, unsigned key) noexcept;
  void random_generate(o3sdk::app::Context& ctx) noexcept;
  void draw_snap(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_random(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  bool scale_locked() const noexcept;                // a scale that leaves notes out is chosen (R8)
  bool scale_locked_out(int note) const noexcept;    // ... and `note` is one of them
  // An encoder move or a ladder tap is written as motion under the playhead (REC or the MOTION latch, while the pattern plays).
  bool recording_motion() const noexcept { return playing_ && (rec_ || motion_latched_); }
  void draw_state_chips(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& strip) noexcept;
  void choose_pattern(o3sdk::app::Context& ctx, unsigned index) noexcept;
  void copy_buttons(o3sdk::app::Context& ctx, o3sdk::app::Button which, bool shift) noexcept;
  void audition_on(o3sdk::app::Context& ctx, unsigned note, std::uint8_t step_flags) noexcept;
  void audition_off(o3sdk::app::Context& ctx) noexcept;
  void toggle_flag_on_held_or_cursor(o3sdk::app::Context& ctx, std::uint8_t flag) noexcept;

  // ---- model helpers
  unsigned sel() const noexcept { return rig_->bank->selected(); }
  const Pattern& pat() const noexcept { return rig_->bank->pattern(rig_->bank->selected()); }
  unsigned length() const noexcept { return pat().length != 0u ? pat().length : kDefaultLength; }
  void move_cursor(o3sdk::app::Context& ctx, int to) noexcept;
  void clamp_cursor() noexcept;
  bool any_step_held() const noexcept;
  void mark_held_used() noexcept;  // the held step pads did something: their release is not a tap
  std::uint32_t held_mask() const noexcept;
  std::uint8_t modifier_flag_down() const noexcept;
  std::uint32_t next_seed(o3sdk::app::Context& ctx) noexcept;
  bool tools_visible(const o3sdk::app::Context& ctx) const noexcept;

  // ---- drawing
  void draw_info(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_strip(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_tabs(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area, const char* hint) noexcept;
  void fill_knob(o3sdk::app::Context& ctx, unsigned k, const Entry& entry, view::Knob& out) noexcept;
  void draw_tools(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_grid(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;

  Rig* rig_ = nullptr;
  Controls* controls_ = nullptr;
  PageKeys keys_;

  // Modes.
  bool rec_ = false;
  bool keys_live_ = false;       // the keyboard plays the voice while the pattern runs (else it transposes)
  bool roll_ = false;
  bool motion_latched_ = false;  // encoder moves are recorded while playing, without REC
  bool playing_ = false;
  int octave_ = 0;
  int cursor_ = 0;
  int view_page_ = 0;
  int last_play_page_ = -1;
  bool was_playing_ = false;
  unsigned lane_ = 0;            // the motion lane shown: the last SOUND-page key the encoder drove
  std::uint32_t seed_ = 0x303u;
  int live_trn_ = 0;

  // The pitch window keeps what it has shown for the pattern, so it does not jump while notes are entered.
  int win_pattern_ = -1;
  int win_lo_ = 0;
  int win_hi_ = 0;
  bool win_roll_ = false;

  // What is held.
  int held_step_[kPageSteps] = {};  // the global step a held step pad k selected, -1 when up
  bool step_used_[kPageSteps] = {}; // the hold did something (a key, a modifier, the encoder): its release does not select the step
  bool mod_down_[kRoles] = {};      // by PadRole
  bool mod_used_[kRoles] = {};      // the hold did something (a tap would do the modifier's own job)
  std::uint64_t mod_down_ms_[kRoles] = {};  // when it went down: a tap is shorter than kHoldMs
  int key_note_[o3sdk::app::kPadCount] = {};  // the note a key pad started, -1 when up (a release ends that note)
  int audition_note_ = -1;          // a step's note sounding for the hand
  // The four performance buttons (Note, Instr, Vol, Fx: bit b = button b) and the actions they do now (a bit per PerfAction, applied).
  unsigned perf_held_ = 0;
  unsigned perf_latched_ = 0;
  std::uint16_t perf_applied_ = 0;
  FreezeOwner freeze_owner_;  // which of FREEZE, ROLL, STOP and REV has the beat freeze now (M3c R)
  std::uint8_t perf_seen_[kPerfButtons] = {};  // the assignment each button had at the last update: a reassigned button drops its latch
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

  // Pattern.
  bool pattern_down_ = false;
  bool overlay_ = false;
  bool chain_started_ = false;
  std::uint64_t pattern_down_ms_ = 0;
  bool copy_is_pattern_ = false;

  // The RANDOM popup (Shift + F1, C3): while it is open the knob strip is its three settings, a key to generate and a key to close.
  bool random_open_ = false;
  unsigned random_focus_ = 0;      // the setting the encoder drives, 0..kRandomEntries-1
  // The morph's slews are running (R2): at `snap_deadline_ms_` the blend is stored in the engine (sequencer.h, slew(): store = false).
  bool snap_pending_ = false;
  std::uint64_t snap_deadline_ms_ = 0;

  // Feedback.
  bool toast_off_ = false;
  char toast_[40] = {};
  std::uint64_t toast_until_ms_ = 0;
  bool lock_active_ = false;
  int lock_step_ = 0;
  float lock_norm_ = 0.0f;
  char lock_text_[16] = {};
};

}  // namespace o3_303
