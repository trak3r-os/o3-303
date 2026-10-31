// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The 303 screen (docs/DESIGN-303.md sections 2 to 5, docs/UX-303.md 5.1 and 5.2): the step view as the hero, the motion lane, the knob strip, the soft keys,
// the status line and the PADS line over it, and the Shift row. HOME has no pages: F1..F8 are the eight sound knobs, the SOUND page on Rig::sound_keys, so its
// latch is PERFORM's and INSTRUMENT VOICE's (UX-303 D3); Shift held, the soft keys are NEW, VARY, RHYTHM, NOTES, REVERSE, CLEAR, REDO and UNDO, and a press does
// its name at once (UX-303 5.1 and 7), each one UNDO step. Also the pattern grid it shows while Pattern is held, and PATTERN SETUP, its second page (a tap of
// Pattern): the same drawer and pads, the pattern's LENGTH / SWING / GATE / TRANSP and the generator's settings on F1..F8 (the screen's own PageKeys, so its
// latch is its own), and its own Shift row (COPY PAT, PASTE PAT, DUP>NEXT, CLEAR LOCKS, UNDO).
//
// The pads, the arrows, Copy / Insert / Delete, Rec and the colour buttons (the drawer rows: Note, Instr, Vol, Fx latch NOTE, GATE, PLAYS, LOCK on the
// encoder; an F key gives it back) are the home map's (home_map.h): this screen forwards them to its HomeMap and draws what it reads from it. It is one class
// in one file, main_screen.cpp. Master and Params are the app's (o3_303_app.cpp, MASTER is master_screen.h); what the four colour buttons do as live actions
// and the snapshots themselves are LiveActions' (live_actions.h), PERFORM's.

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

// The scale field of the info lines: the name in capitals without the table's padding ("PENTA MIN"), or its first three letters (`short_form`, when the
// strip is full); "--" when `locked` is false (the keyboard is chromatic: a scale never chosen locks nothing), whatever the generator's scale is.
void scale_value_text(unsigned scale, bool locked, bool short_form, char* out, unsigned cap) noexcept;

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
  // HELP (Shift + Config, help_texts.h): HOME's texts or PATTERN SETUP's, the Shift row's labels, and the home map's pads.
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& h) const noexcept override;

  // PATTERN SETUP (UX-303 5.2) is the second page of this screen: a tap of Pattern flips it and HOME.
  bool setup() const noexcept { return setup_; }
  bool pattern_overlay() const noexcept { return home_.overlay(); }
  int cursor() const noexcept { return home_.cursor(); }
  // The home map the screen forwards to: another home-map screen (INSTRUMENT) uses the same one, so the cursor, the octave, Rec and FILT are shared.
  HomeMap& home_map() noexcept { return home_; }
  bool rec() const noexcept { return home_.rec(); }
  int octave() const noexcept { return home_.octave(); }
  // What PERFORM's key row and encoder ask of the home map's keyboard (one keyboard, one octave, one scale lock, one Rec).
  void toggle_rec(o3sdk::app::Context& ctx) noexcept { home_.toggle_rec(ctx); }
  bool scale_locked_out(int note) const noexcept { return home_.scale_locked_out(note); }
  // An encoder move or a ladder tap is written as motion under the playhead (Rec while the pattern plays).
  bool recording_motion() const noexcept { return home_.recording_motion(); }
  // A line in the status line for a moment (what a tool or a load just did).
  // `off`: the line says something went off ("ENCODER: THE PAGE"): drawn as an outline, not a filled chip.
  void toast(o3sdk::app::Context& ctx, const char* text, bool off = false) noexcept { home_.toast(ctx, text, off); }

 private:
  // ---- input
  void button_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void turn_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  // The Shift row (UX-303 5.1 and 5.2): key `index` at the press of Shift + F(index + 1).
  void tool(o3sdk::app::Context& ctx, unsigned index) noexcept;
  void home_tool(o3sdk::app::Context& ctx, unsigned index) noexcept;
  // ---- PATTERN SETUP (main_screen.cpp)
  void setup_toggle(o3sdk::app::Context& ctx) noexcept;
  void setup_tool(o3sdk::app::Context& ctx, unsigned index) noexcept;
  void duplicate_to_next(o3sdk::app::Context& ctx) noexcept;
  // The F keys' page: SETUP's own, or the shared SOUND page (Rig::sound_keys).
  PageKeys& fkeys() noexcept { return setup_ ? setup_keys_ : rig_->sound_keys; }
  const PageKeys& fkeys() const noexcept { return setup_ ? setup_keys_ : rig_->sound_keys; }
  bool tool_dim(unsigned k) const noexcept;

  // ---- model helpers
  unsigned sel() const noexcept { return rig_->bank->selected(); }
  const Pattern& pat() const noexcept { return rig_->bank->pattern(rig_->bank->selected()); }
  unsigned length() const noexcept { return pat().length != 0u ? pat().length : kDefaultLength; }
  std::uint32_t next_seed(o3sdk::app::Context& ctx) noexcept;
  // What the encoder moves, for the status line: "F1 CUTOFF", a drawer row.
  void enc_text(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;

  // ---- drawing
  void draw_info(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_strip(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_info_generator(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& strip) noexcept;
  void draw_grid(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;

  Rig* rig_ = nullptr;
  Controls* controls_ = nullptr;
  LiveActions* live_ = nullptr;
  HomeMap home_;
  PageKeys setup_keys_;  // PATTERN SETUP's F keys (setup_page()): one page, key 0 (LENGTH) latched

  // Modes.
  bool setup_ = false;   // PATTERN SETUP is shown (a tap of Pattern); every other piece of the screen is shared with HOME

  // The pitch window keeps what it has shown for the pattern, so it does not jump while notes are entered.
  int win_pattern_ = -1;
  int win_lo_ = 0;
  int win_hi_ = 0;

  std::uint32_t tool_mask_ = 0;     // F keys pressed as tools (their release is not the pages')
};

}  // namespace o3_303
