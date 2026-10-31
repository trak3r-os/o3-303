// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The home map (docs/UX-303.md 6.2, 2.2 and 2.3): what the 48 pads, the arrows, Copy / Insert / Delete, Rec and the four colour buttons (Note, Instr, Vol, Fx:
// they latch the NOTE, GATE, PLAYS and LOCK rows of the drawer on the encoder) do to the pattern on screen, and what the screen asks of it to draw. Every home-map screen (HOME and PATTERN SETUP, the two pages of MainScreen; INSTRUMENT; MASTER and RECORD later)
// forwards its input here, so the pads mean the same on all of them.
//
//   row 1   steps 1-8           ACC SLIDE TIE REST
//   row 2   steps 9-16          OCT- OCT+ TRNSP FILT
//   row 3   keys C3..B3         (FILT on: the CUTOFF ladder)
//   row 4   keys C2..B2         (FILT on: the RESO ladder)
//
// It holds the pattern cursor and the step page, what is held (step pads, the four modifiers, Pattern), the keyboard's octave and Rec, the lane the
// drawer shows and the toast; the pattern itself is the Bank's, the sound knobs and the keyboard's transposition are Controls' and LiveActions'.
// The screen owns the encoder's owner (the F keys, a latch of Master), so it passes the latched knob in: turn() takes a turn that is a step's
// (a held step, or a drawer row latched: Note, Instr, Vol, Fx), and gives back the others. An F key gives the encoder back to the knob (release_row()).
//
// The view logic (pads_line_text(), the drawer's marks, the STEP box, the status) is apart from the drawing, which is the screen's (frame.h,
// step_view.h). Main loop only.

#include "frame.h"
#include "help_texts.h"
#include "live_actions.h"
#include "rig.h"
#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/input.h>
#include <o3sdk/app/pad_leds.h>
#include <o3_303/pad_map.h>
#include <o3_303/pages.h>
#include <o3_303/pattern.h>

#include <cstdint>

namespace o3_303 {

inline constexpr std::uint64_t kHoldMs = 300;  // a press shorter than this is a tap; Pattern held this long: the 48 pads are the patterns

// The toast when a lock or a recorded turn finds all eight lanes in use (UX-303 D2). A lane is free once its last lock is gone; the toast names the
// way out that is always there (SETUP's CLEAR LOCKS).
inline constexpr const char* kLanesUsedToast = "8 LANES USED: CLEAR LOCKS (SETUP)";

// What the PADS line says (UX-303 6.2, the state table): a plain value, so the text is a pure function of it.
struct PadsLineState {
  bool overlay = false;     // Pattern held: the 48 pads are the patterns
  bool playing = false;     // the transport runs
  bool rec = false;         // write / record mode
  bool trnsp = false;       // the keyboard transposes the pattern
  bool filt = false;        // rows 3 and 4 are the ladders
  int octave = 0;           // the keyboard's octave shift (the keys' range is C2-B3 at 0)
  unsigned step_held = 0;   // the first held step, 1-based; 0 none
  PadRole attribute = PadRole::None;  // the attribute pad held (Acc, Slide, Tie, Rest), None when none is
  const char* turn = "";    // what a turn does on a held step ("LOCK CUTOFF", "RATCHET", "PLAYS"), "" the encoder's owner is not the step's
};

// The one sentence of the PADS line for `s` into `out` (cap bytes, terminated). The priorities: Pattern held, an attribute held, a step held, FILT,
// Rec (playing, then stopped), TRNSP, plain.
void pads_line_text(const PadsLineState& s, char* out, unsigned cap) noexcept;

class HomeMap {
 public:
  // Every piece of state starts afresh: the app object outlives a run in the simulator's tests.
  void init(Rig& rig) noexcept;
  // The screen is shown / left: what was held lets go (the releases would reach another screen), the keys that sound end.
  void enter(o3sdk::app::Context& ctx) noexcept;
  void leave(o3sdk::app::Context& ctx) noexcept;

  // Pads, Note / Instr / Vol / Fx (a tap latches the NOTE, GATE, PLAYS or LOCK row; Shift is the same, D22), Left / Right / Up / Down, Copy / Insert /
  // Delete, Rec and Pattern. true when it took the event; a tap of
  // Pattern (a release that did not open the grid) is the screen's (it flips HOME and PATTERN SETUP), so it answers false. Copy / Insert / Delete with
  // Pattern held do nothing but say where those went (PATTERN SETUP's COPY PAT, DUP>NEXT, CLEAR LOCKS, UX-303 5.2).
  bool input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  // The encoder moved by `detents` (`coarse`: Shift) with `knob` as its owner. true when it is a step's: a latched row takes the turn (on the held
  // steps, else the cursor's: NOTE the semitone, Shift the octave; GATE the list; PLAYS the list, Shift four places; LOCK the lock of the lock knob,
  // Shift coarse), and so does a held step: as the ratchet or the condition of the held steps when `knob` is that entry, as the parameter lock of
  // `knob` (its lane claimed) when a lane can drive it. false: the owner has it (the screen applies it, then calls turned()).
  bool turn(o3sdk::app::Context& ctx, int detents, bool coarse, const Entry& knob) noexcept;
  // The screen applied a turn to `knob`: Rec while the pattern plays writes the new value as motion under the playhead (the lane claimed first).
  void turned(o3sdk::app::Context& ctx, const Entry& knob) noexcept;
  // `knob` is the one the encoder owns now (an F key latched it, a page or a pattern changed under it): the lane the drawer shows follows it, and it is
  // the LOCK row's knob when a lane can drive it (SETUP's knobs cannot: the LOCK row keeps the last sound knob).
  void knob_latched(const Entry& knob) noexcept;
  // The encoder goes back to the knob (an F key was tapped, the page changed).
  void release_row() noexcept { row_ = DrawerRow::None; }
  DrawerRow row() const noexcept { return row_; }
  // What the encoder moves while a row owns it, for the ENC text: "NOTE", "GATE", "PLAYS", "LOCK CUTOFF".
  void row_text(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;
  // Every pass of the main loop while the screen is shown (and from the app's, while another is): the pattern being played is followed,
  // Pattern held long enough opens the grid, the lock readout and the toast expire.
  void update(o3sdk::app::Context& ctx) noexcept;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept;

  // ---- What the screen reads, to draw
  // The drawer: the marks of the step view for the page shown, with the LOCK row's texts (the pitch window and the geometry are the screen's).
  view::Marks drawer(o3sdk::app::Context& ctx) const noexcept;
  // The STEP box: the held step, else the cursor's, whole: "STEP 5: D#2 ACC SLD x2 CUTOFF 420Hz" (the lock of the LOCK row's knob when it has one).
  unsigned step_box(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;
  // The status line, but for the encoder's text (the screen's): the keyboard's mode and octave, Rec, the actions that are on, the toast.
  void status(frame::StatusView& out) const noexcept;
  // The PADS line.
  void pads_line(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;
  // The 48 pads for HELP (help_texts.h fill_home_pads): the step page shown, the pattern's length, the octave, FILT and the scale lock decide the labels and which pads are dark. The note names
  // the labels point into are this object's (help_names_), valid while HELP is open (the screen is asked once, when it opens).
  void help_pads(o3sdk::app::Help& h) const noexcept;

  // ---- State the screens share
  int cursor() const noexcept { return cursor_; }
  int view_page() const noexcept { return view_page_; }
  bool rec() const noexcept { return rec_; }
  void toggle_rec(o3sdk::app::Context& ctx) noexcept {
    rec_ = !rec_;
    ctx.redraw();
  }
  int octave() const noexcept { return octave_; }
  bool filt() const noexcept { return filt_; }
  bool overlay() const noexcept { return overlay_; }
  // The Pattern button is down and nothing was done with it (no grid, no Copy or Insert chord): its release is a tap, which is the screen's.
  bool pattern_tap() const noexcept { return pattern_down_ && !overlay_ && !pattern_used_; }
  bool playing() const noexcept { return playing_; }
  // The lane under the step view: the latched knob's, else the last one a lock, a ladder or a recorded turn touched. on_lane(): the latched knob has one.
  unsigned lane() const noexcept { return lane_; }
  bool on_lane() const noexcept { return on_lane_; }
  // A step is held: the step pads' own place, the encoder's owner on it, the lock readout.
  bool any_step_held() const noexcept;
  bool lock_active() const noexcept { return lock_active_; }
  int lock_step() const noexcept { return lock_step_; }
  float lock_norm() const noexcept { return lock_norm_; }
  const char* lock_text() const noexcept { return lock_text_; }
  // An encoder move or a ladder tap is written as motion under the playhead (Rec while the pattern plays).
  bool recording_motion() const noexcept { return playing_ && rec_; }
  // The keyboard is locked to a scale (R8) and `note` is not in it.
  bool scale_locked() const noexcept;
  bool scale_locked_out(int note) const noexcept;

  // A line for a moment on the status line (what a gesture or a tool just did). `off`: it says something did not happen (drawn as an outline).
  void toast(o3sdk::app::Context& ctx, const char* text, bool off = false) noexcept;
  // The cursor and the page kept inside the pattern (its length may have changed under them); and moved to a step.
  void clamp_cursor() noexcept;
  void move_cursor(o3sdk::app::Context& ctx, int to) noexcept;
  // The pattern changed under the cursor (a tool, undo, a load): clamp it and show its page.
  void refocus() noexcept {
    clamp_cursor();
    view_page_ = cursor_ / 16;
  }
  // The lane of `p` that drives what `entry` shows, -1 when the entry is not a lane target or no lane points at it.
  int lane_of(const Entry& entry, const Pattern& p) const noexcept;

 private:
  static constexpr unsigned kRoles = static_cast<unsigned>(PadRole::None) + 1u;

  // ---- input
  void pad_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void key_press(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void key_release(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  void step_press(o3sdk::app::Context& ctx, unsigned k) noexcept;
  void modifier_tap(o3sdk::app::Context& ctx, PadRole role) noexcept;
  void toggle_flag_on_held_or_cursor(o3sdk::app::Context& ctx, std::uint8_t flag) noexcept;
  bool lock_turn(o3sdk::app::Context& ctx, const Entry& entry, int detents, bool coarse) noexcept;
  // The steps a row edits: the held ones, else the cursor's (into `out`, the count).
  unsigned row_steps(unsigned (&out)[kPageSteps]) noexcept;
  void row_turn(o3sdk::app::Context& ctx, int detents, bool coarse) noexcept;
  void latch_row(o3sdk::app::Context& ctx, DrawerRow row) noexcept;
  // The lock of `entry` on `steps`, each moved `detents` from where it stands ("--" below the minimum removes it). NoTarget: no lane can drive the entry;
  // LanesFull: all eight are in use (the toast is up).
  enum class LockResult : std::uint8_t { Done, NoTarget, LanesFull };
  LockResult lock_steps(o3sdk::app::Context& ctx, const Entry& entry, const unsigned* steps, unsigned count, int detents, bool coarse) noexcept;
  // `entry`'s lock `point` as the text of a LOCK cell or the STEP box: the value and its unit, "420Hz".
  void lock_value_text(o3sdk::app::Context& ctx, const Entry& entry, std::uint8_t point, char* out, unsigned cap) const noexcept;
  void rotate_layers(o3sdk::app::Context& ctx, int direction) noexcept;
  void copy_buttons(o3sdk::app::Context& ctx, o3sdk::app::Button which, bool shift) noexcept;
  void arrow(o3sdk::app::Context& ctx, o3sdk::app::Button which, bool shift) noexcept;
  void choose_pattern(o3sdk::app::Context& ctx, unsigned index) noexcept;
  void audition_on(o3sdk::app::Context& ctx, unsigned note, std::uint8_t step_flags) noexcept;
  void audition_off(o3sdk::app::Context& ctx) noexcept;
  void ladder_press(o3sdk::app::Context& ctx, unsigned pad) noexcept;
  // ---- leds
  void ladder_leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) const noexcept;

  // ---- model helpers
  unsigned sel() const noexcept { return rig_->bank->selected(); }
  const Pattern& pat() const noexcept { return rig_->bank->pattern(rig_->bank->selected()); }
  unsigned length() const noexcept { return pat().length != 0u ? pat().length : kDefaultLength; }
  void mark_held_used() noexcept;  // the held step pads did something: their release is not a tap
  std::uint32_t held_mask() const noexcept;
  std::uint8_t modifier_flag_down() const noexcept;
  PadRole attribute_down() const noexcept;
  unsigned first_held_step() const noexcept;  // 1-based, 0 none

  Rig* rig_ = nullptr;
  Controls* controls_ = nullptr;
  LiveActions* live_ = nullptr;

  // Modes.
  bool rec_ = false;
  bool filt_ = false;
  bool playing_ = false;
  int octave_ = 0;
  int cursor_ = 0;
  int view_page_ = 0;
  int last_play_page_ = -1;
  unsigned lane_ = 0;
  bool on_lane_ = false;
  Entry knob_;  // the knob the encoder owns (knob_latched)
  Entry lock_knob_;  // the knob the LOCK row edits: knob_ when a lane can drive it, else the last such knob (CUTOFF at boot)
  DrawerRow row_ = DrawerRow::None;  // the row the encoder owns (a colour button latched it), None: the knob has it

  // What is held.
  int held_step_[kPageSteps] = {};   // the global step a held step pad k selected, -1 when up
  bool step_used_[kPageSteps] = {};  // the hold did something (a key, a modifier, the encoder): its release does not select the step
  bool mod_down_[kRoles] = {};       // by PadRole
  bool mod_used_[kRoles] = {};       // the hold did something (a tap would do the modifier's own job)
  std::uint64_t mod_down_ms_[kRoles] = {};  // when it went down: a tap is shorter than kHoldMs
  int key_note_[o3sdk::app::kPadCount] = {};  // the note a key pad started, -1 when up (a release ends that note)
  int audition_note_ = -1;           // a step's note sounding for the hand

  // Pattern held: the 48 pads are the patterns.
  bool pattern_down_ = false;
  bool overlay_ = false;
  bool chain_started_ = false;
  bool pattern_used_ = false;  // Copy, Insert or Delete went with it (SETUP's now): its release is no tap
  std::uint64_t pattern_down_ms_ = 0;

  // Feedback.
  bool toast_off_ = false;
  char toast_[64] = {};
  std::uint64_t toast_until_ms_ = 0;
  bool lock_active_ = false;
  int lock_step_ = 0;
  float lock_norm_ = 0.0f;
  char lock_text_[16] = {};

  // HELP's note names of the keys (help_pads()): written when HELP opens, read while it is open; both on the main loop.
  mutable help_texts::KeyNames help_names_ = {};
};

}  // namespace o3_303
