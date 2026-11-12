// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PERFORM (docs/UX-303.md 5.3) and PERFORM SETUP (5.4): twenty minutes of play without a menu. The 48 pads are live controls and the screen is a
// 1:1 mirror of them, one lamp over each pad:
//
//   row 1   the eleven live actions (held while the pad is down; Shift latches it) and ALL OFF
//   row 2   snapshots 1..8 (a tap recalls and the encoder becomes MORPH; Shift stores), then NEW, VARY, UNDO, REDO
//   row 3   the ladder: twelve places of the knob it holds (CUTOFF, HOME's FILT row 3; while WOBBLE is on: the wobble's rate)
//   row 4   the keys: one octave, C..B, playing the voice or transposing the pattern
//
// The four colour buttons do their assigned actions too. Perform again opens KNOBS (docs/UX-303.md 5.3b, issue #2): the same four rows of pads, every one a ladder of a knob (rows 3 and 4
// are HOME's FILT rows; hold an F key and tap a row to give it that knob; Shift + F1..F4 walks a list of every knob; Shift + F8 puts the rows back), the F row and the four colour
// buttons as on PERFORM. Perform once more opens PERFORM SETUP (5.4): eight knobs, F1..F4 the buttons' assignments, F5 what
// FILL plays, F6 the freeze's length, F7 ROLL (rate and accelerate in one list), F8 STOP's time; no Shift row; the pads stay PERFORM's.
//
// The view logic is apart from the drawing: fill_view() makes a PerformView (what each lamp says and how bright it is), the LEDs are its levels
// and draw_view() draws it, so the skin can replace the drawing and not the logic. The actions, the snapshots and the keyboard's transposition
// are LiveActions' (live_actions.h), shared with the 303 screen; the pad map is pure (o3_303/perform.h).

#include "frame.h"
#include "live_actions.h"
#include "main_screen.h"
#include "perform_view.h"
#include "rig.h"

#include <o3sdk/app/panel.h>
#include <o3sdk/app/screen.h>

#include <cstdint>

namespace o3_303 {

class PerformScreen final : public o3sdk::app::Screen {
 public:
  // `home`: the 303 screen, whose keyboard (octave, scale lock, Rec) PERFORM's key row is.
  void init(Rig& rig, MainScreen& home) noexcept;

  // The Perform button: from another screen it opens page 1 (PERFORM); on this one it flips PERFORM and KNOBS, and on PERFORM SETUP it leaves for the page the setup was opened from.
  // Shift + Perform opens PERFORM SETUP from anywhere (R8, the review's S1: SETUP configures the two pages, it is not a third one to pass through while playing).
  // (A key held across the change would stay held in the keys it was pressed on: both sets let go.)
  enum class PerformPage : std::uint8_t { Perform, Knobs, Setup };
  void open_first_page() noexcept {
    release_keys();
    page_ = PerformPage::Perform;
    walk_row_ = -1;
  }
  void flip_page() noexcept {
    release_keys();
    page_ = page_ == PerformPage::Perform ? PerformPage::Knobs : (page_ == PerformPage::Knobs ? PerformPage::Perform : back_);
    walk_row_ = -1;
  }
  // `on_screen`: this screen is the one showing (the page it was on is where the Perform button comes back to); from another screen the way back is PERFORM.
  void open_setup(bool on_screen) noexcept {
    release_keys();
    if (page_ != PerformPage::Setup) {
      back_ = on_screen ? page_ : PerformPage::Perform;
    }
    page_ = PerformPage::Setup;
    walk_row_ = -1;
  }
  bool setup() const noexcept { return page_ == PerformPage::Setup; }
  bool knobs() const noexcept { return page_ == PerformPage::Knobs; }
  PerformPage page() const noexcept { return page_; }
  // The row (0..3) whose knob the encoder is walking through the list (Shift + F1..F4), -1 when none.
  int walking_row() const noexcept { return walk_row_; }
  // The encoder is MORPH (a snapshot was recalled and no F key was tapped since).
  bool morphing() const noexcept { return morph_; }
  // The view the last skinned frame drew (for the tests: its lamps are the pads' LED levels).
  const PerformView& last_view() const noexcept { return view_; }

  const char* title() const noexcept override { return page_ == PerformPage::Setup ? "PERFORM SETUP" : (page_ == PerformPage::Knobs ? "PERFORM KNOBS" : "PERFORM"); }
  // The obsidian skin (skin/skin.h): no kit chrome and the obsidian theme while it is active; otherwise the kit's.
  o3sdk::app::Chrome chrome(const o3sdk::app::Context& ctx) const noexcept override;
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context& ctx) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  std::uint64_t repeat_buttons(const o3sdk::app::Context&) const noexcept override { return PageKeys::repeat_arrows(); }  // held Left and Right with an F key held keep stepping its knob
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& keys) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;
  // HELP (Shift + Config, help_texts.h): PERFORM's, KNOBS' or PERFORM SETUP's texts, REDO / UNDO on the Shift row of PERFORM, and the pads' rows.
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& h) const noexcept override;

 private:
  // ---- input
  void pad_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void button_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void turn_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void snapshot_pad(o3sdk::app::Context& ctx, unsigned slot, bool shift) noexcept;
  void variation(o3sdk::app::Context& ctx, PerformRole role) noexcept;  // NEW, VARY, UNDO, REDO
  void ladder_press(o3sdk::app::Context& ctx, unsigned col) noexcept;
  void knobs_press(o3sdk::app::Context& ctx, unsigned row, unsigned col) noexcept;  // KNOBS: a tap on place `col` of row `row` (0..3)
  void knobs_shift_key(o3sdk::app::Context& ctx, unsigned k) noexcept;              // KNOBS: Shift + F1..F4 point the encoder at a row's list, Shift + F8 resets the rows
  void walk_turn(o3sdk::app::Context& ctx, int delta) noexcept;                    // the encoder walks the list for walk_row_
  void knobs_toast(o3sdk::app::Context& ctx, unsigned row, const char* before, const char* after) noexcept;
  void key_press(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void key_release(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void toast(o3sdk::app::Context& ctx, const char* text, bool off = false) noexcept;
  void release_keys() noexcept {
    rig_->sound_keys.release_all();
    setup_keys_.release_all();
  }
  PageKeys& keys() noexcept { return page_ == PerformPage::Setup ? setup_keys_ : rig_->sound_keys; }
  unsigned sel() const noexcept { return rig_->bank->selected(); }

  // ---- view
  void fill_view(o3sdk::app::Context& ctx, PerformView& view) const noexcept;
  void fill_knobs_view(o3sdk::app::Context& ctx, PerformView& view) const noexcept;  // fill_view() on KNOBS: four ladder rows
  void fill_status(o3sdk::app::Context& ctx, frame::StatusView& status) const noexcept;
  void enc_text(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;
  void pads_line(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;
  void draw_colour_buttons(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& line) const noexcept;
  void fill_chips(PerformView& view) const noexcept;
  // The F row's eight boxes: the page's knobs, or with Shift held on PERFORM the Shift row (REDO and UNDO). Returns whether it is the Shift row. `skin`: the obsidian row dims a key that has
  // nothing to do (the classic strip's boxes are not dimmed).
  bool fill_knob_row(o3sdk::app::Context& ctx, view::Knob (&knobs)[view::kKnobs], bool skin) noexcept;
  void draw_knob_row(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_classic(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_skinned(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r) noexcept;

  Rig* rig_ = nullptr;
  Controls* controls_ = nullptr;
  LiveActions* live_ = nullptr;
  MainScreen* home_ = nullptr;
  PageKeys setup_keys_;  // PERFORM SETUP's keys: the global table's PERF SETUP page (kPagePerfSetup), with a latch of their own
  PerformPage page_ = PerformPage::Perform;
  PerformPage back_ = PerformPage::Perform;  // the page PERFORM SETUP was opened from (PERFORM or KNOBS): where the Perform button leaves it for
  int walk_row_ = -1;                // KNOBS: the row the encoder walks the knob list for (Shift + F1..F4), -1 when none; any other key ends it
  std::uint64_t perf_down_ = 0;      // the pads pressed under PERFORM's roles (a release ends what the press began, whatever page the release finds)
  bool morph_ = false;
  int key_note_[o3sdk::app::kPadCount] = {};  // the note a key pad started, -1 when up (a release ends that note)
  std::uint32_t tool_mask_ = 0;               // F keys pressed with Shift (their release is not the keys')
  bool playing_ = false;
  char toast_[32] = {};
  bool toast_off_ = false;
  std::uint64_t toast_until_ms_ = 0;
  PerformView view_;  // the last skinned frame's
};

}  // namespace o3_303
