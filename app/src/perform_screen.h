// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PERFORM (docs/UX-303.md 5.3) and PERFORM SETUP (5.4): twenty minutes of play without a menu. The 48 pads are live controls and the screen is a
// 1:1 mirror of them, one lamp over each pad:
//
//   row 1   the eleven live actions (held while the pad is down; Shift latches it) and ALL OFF
//   row 2   snapshots 1..8 (a tap recalls and the encoder becomes MORPH; Shift stores), then NEW, VARY, UNDO, REDO
//   row 3   the ladder: twelve places of the latched sound knob (while WOBBLE is on: the wobble's rate)
//   row 4   the keys: one octave, C..B, playing the voice or transposing the pattern
//
// The four colour buttons do their assigned actions too. Perform again opens PERFORM SETUP, which until slice P2 is the PERF page as it was: the
// buttons' assignments and the freeze's settings on F1..F8, over the same pads.
//
// The view logic is apart from the drawing: fill_view() makes a PerformView (what each lamp says and how bright it is), the LEDs are its levels
// and draw_view() draws it, so the skin can replace the drawing and not the logic. The actions, the snapshots and the keyboard's transposition
// are LiveActions' (live_actions.h), shared with the 303 screen; the pad map is pure (o3_303/perform.h).

#include "frame.h"
#include "live_actions.h"
#include "main_screen.h"
#include "rig.h"

#include <o3sdk/app/panel.h>
#include <o3sdk/app/screen.h>

#include <cstdint>

namespace o3_303 {

// One pad as the screen shows it.
struct PadLamp {
  char label[8] = "";
  std::uint8_t level = 0;  // the pad's LED level (the kit's kLedOff .. kLedOn)
  bool latched = false;    // an action latched: drawn with a frame
};

struct PerformView {
  PadLamp lamp[o3sdk::app::kPadCount];
  char ladder_caption[16] = "";  // what row 3 is: the latched knob's name, or "WOBBLE RATE"
  char keys_caption[16] = "";    // what row 4 does: "PLAY VOICE" or "TRANSPOSE"
};

class PerformScreen final : public o3sdk::app::Screen {
 public:
  // `home`: the 303 screen, whose keyboard (octave, scale lock, Rec) PERFORM's key row is.
  void init(Rig& rig, MainScreen& home) noexcept;

  // The Perform button: from another screen it opens page 1; on this one it goes to the other page.
  // (A key held across the change would stay held in the keys it was pressed on: both sets let go.)
  void open_first_page() noexcept {
    release_keys();
    setup_ = false;
  }
  void toggle_page() noexcept {
    release_keys();
    setup_ = !setup_;
  }
  bool setup() const noexcept { return setup_; }
  // The encoder is MORPH (a snapshot was recalled and no F key was tapped since).
  bool morphing() const noexcept { return morph_; }

  const char* title() const noexcept override { return setup_ ? "PERFORM SETUP" : "PERFORM"; }
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& keys) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;

 private:
  // ---- input
  void pad_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void button_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void turn_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void snapshot_pad(o3sdk::app::Context& ctx, unsigned slot, bool shift) noexcept;
  void variation(o3sdk::app::Context& ctx, PerformRole role) noexcept;  // NEW, VARY, UNDO, REDO
  void ladder_press(o3sdk::app::Context& ctx, unsigned col) noexcept;
  void key_press(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void key_release(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void toast(o3sdk::app::Context& ctx, const char* text, bool off = false) noexcept;
  void release_keys() noexcept {
    rig_->sound_keys.release_all();
    setup_keys_.release_all();
  }
  PageKeys& keys() noexcept { return setup_ ? setup_keys_ : rig_->sound_keys; }
  unsigned sel() const noexcept { return rig_->bank->selected(); }

  // ---- view
  void fill_view(o3sdk::app::Context& ctx, PerformView& view) const noexcept;
  void fill_status(o3sdk::app::Context& ctx, frame::StatusView& status) const noexcept;
  void enc_text(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;
  void pads_line(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;
  void draw_colour_buttons(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& line) const noexcept;
  void draw_knob_row(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;

  Rig* rig_ = nullptr;
  Controls* controls_ = nullptr;
  LiveActions* live_ = nullptr;
  MainScreen* home_ = nullptr;
  PageKeys setup_keys_;  // PERFORM SETUP's keys: the global table's PERF page (until P2), with a latch of their own
  bool setup_ = false;
  bool morph_ = false;
  int key_note_[o3sdk::app::kPadCount] = {};  // the note a key pad started, -1 when up (a release ends that note)
  std::uint32_t tool_mask_ = 0;               // F keys pressed with Shift (their release is not the keys')
  bool playing_ = false;
  char toast_[32] = {};
  bool toast_off_ = false;
  std::uint64_t toast_until_ms_ = 0;
};

}  // namespace o3_303
