// SPDX-License-Identifier: GPL-3.0-or-later
#include "perform_screen.h"

#include "help_texts.h"
#include "home_map.h"
#include "ladder_rows.h"
#include "skin/header.h"
#include "skin/perform.h"
#include "skin/skin.h"
#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>
#include <o3sdk/hal/cycles.h>
#include <o3sdk/ui/components.h>
#include <o3_303/pad_map.h>
#include <o3_303/seq/ops.h>
#include <o3_303/seq/wobble.h>

namespace o3_303 {

namespace {

using namespace o3sdk;
using app::Input;
using ui::Color;
using ui::Face;
using ui::Rect;

constexpr std::uint64_t kToastMs = 1400;
constexpr unsigned kVaryPct = 25;  // VARY is MUTATE at 25 % (UX-303 section 7)

// The lamps' labels: the actions in the order of the PerfAction enum (the pads are in the mockup's order, perform.h), and the keys.
constexpr const char* kActionLabels[kPerfActionCount] = {"MUTE", "ACC", "SLD", "OCT+", "FILL", "THRW", "FRZ", "WOB", "ROLL", "STOP", "REV"};
constexpr const char* kNoteLabels[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// The drawing's geometry: a label column, then twelve lamps a row, four rows, one lamp over each pad.
constexpr int kLabelX = 8;
constexpr int kLampX = 124;
constexpr int kLampPitch = 55;
constexpr int kLampW = 51;
constexpr int kLampH = 62;
constexpr int kRowPitch = 76;
constexpr int kGridTop = frame::kStatusH + 10;
constexpr int kButtonsY = kGridTop + 3 * kRowPitch + kLampH + 10;  // the line of the four colour buttons under the lamps

// `s` cut to `max` characters into `out` (cap > max).
void cut(const char* s, unsigned max, char* out, unsigned cap) noexcept {
  unsigned n = 0;
  for (; s != nullptr && s[n] != '\0' && n < max && n + 1u < cap; ++n) {
    out[n] = s[n];
  }
  out[n] = '\0';
}

void append(char* out, unsigned cap, const char* s) noexcept {
  unsigned n = 0;
  while (n < cap && out[n] != '\0') {
    ++n;
  }
  for (; s != nullptr && *s != '\0' && n + 1u < cap; ++s) {
    out[n++] = *s;
  }
  if (n < cap) {
    out[n] = '\0';
  }
}

// The lane of `p` that drives what `entry` shows, -1 when none does.
int lane_for(const Controls& controls, const Entry& entry, const Pattern& p) noexcept {
  LaneRef ref{kLaneNone, 0};
  if (!controls.lane_ref(entry, ref)) {
    return -1;
  }
  return seq::find_lane(p, ref.target, ref.id);
}

// ---- The drawing of a PerformView ---------------------------------------------------------------------------------------------------

Rect lamp_rect(int area_y, unsigned row, unsigned col) noexcept {
  return Rect{kLampX + static_cast<int>(col) * kLampPitch, area_y + kGridTop + static_cast<int>(row) * kRowPitch, kLampW, kLampH};
}

// The lamps of rows [0, rows), in passes by colour (one colour word a group, not one a lamp: the display list is short).
void draw_view(ui::Renderer& r, const Rect& area, const PerformView& view, unsigned rows) noexcept {
  const ui::Theme& theme = r.theme();
  const unsigned count = rows * app::kPadCols;
  // The levels: off is an outline alone, dim a grey lamp, low and mid the blue one, on the green one.
  static constexpr Color kFill[4] = {Color::Pane, Color::Empty, Color::Sel, Color::Play};
  static constexpr Color kInk[4] = {Color::TextDim, Color::Text, Color::SelText, Color::PlayText};
  const auto group = [](std::uint8_t level) -> unsigned { return static_cast<unsigned>(lamp_tone(level)); };
  for (unsigned g = 0; g < 4; ++g) {
    for (unsigned pad = 0; pad < count; ++pad) {
      if (group(view.lamp[pad].level) == g) {
        const Rect lr = lamp_rect(area.y, app::pad_row(pad), app::pad_col(pad));
        if (g == 0u) {
          r.frame(lr, Color::Empty);
        } else {
          r.panel(lr, kFill[g]);
        }
      }
    }
  }
  for (unsigned pad = 0; pad < count; ++pad) {  // a latched action: an Accent frame round its lamp
    if (view.lamp[pad].latched) {
      r.frame(lamp_rect(area.y, app::pad_row(pad), app::pad_col(pad)), Color::Accent);
    }
  }
  for (unsigned g = 0; g < 4; ++g) {
    for (unsigned pad = 0; pad < count; ++pad) {
      if (group(view.lamp[pad].level) != g || view.lamp[pad].label[0] == '\0') {
        continue;
      }
      const char* label = view.lamp[pad].label;
      Face face = Face::Label;
      int w = r.text_width(label, face);
      if (w > kLampW - 6) {
        face = Face::Micro;
        w = r.text_width(label, face);
      }
      const Rect lr = lamp_rect(area.y, app::pad_row(pad), app::pad_col(pad));
      r.text(lr.x + (lr.w - w) / 2, ui::caps_y(theme, face, lr.y, lr.h), label, kInk[g], face);
    }
  }
}

}  // namespace

// ---- Setup and screen hooks -----------------------------------------------------------------------------------------------------

void PerformScreen::init(Rig& rig, MainScreen& home) noexcept {
  // Every piece of state starts afresh: the app object outlives a run in the simulator's tests.
  rig_ = &rig;
  controls_ = &rig.controls;
  live_ = rig.live;
  home_ = &home;
  setup_keys_ = PageKeys{};
  setup_keys_.set_page(kPagePerfSetup);
  page_ = PerformPage::Perform;
  knobs_page_ = Page{"KNOBS", {}};
  knobs_keys_ = PageKeys{};
  knobs_keys_.set_table(&knobs_page_, 1);
  sync_knobs();
  (void)knobs_keys_.latch(4);
  perf_down_ = 0;
  morph_ = false;
  for (int& n : key_note_) {
    n = -1;
  }
  tool_mask_ = 0;
  playing_ = false;
  toast_[0] = '\0';
  toast_off_ = false;
  toast_until_ms_ = 0;
}

void PerformScreen::enter(app::Context& ctx) noexcept {
  sync_knobs();
  ctx.redraw();
}

// KNOBS' F5..F8 are the four rows' knobs (the rows of the set in use); the page follows whatever changed them (a set, a re-pointed row, a project, HOME's FILT rows).
void PerformScreen::sync_knobs() noexcept {
  if (rig_->wiring.knob_set == kKnobSetGrowl || rig_->wiring.knob_set == kKnobSetTract) {
    f3_set_ = rig_->wiring.knob_set;
  }
  for (unsigned k = 0; k < kKnobsRows; ++k) {
    knobs_page_.entry[4u + k] = ladder_entry(*controls_, rig_->wiring, kKnobsRowId[k]);
  }
}

void PerformScreen::leave(app::Context& ctx) noexcept {
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    if (key_note_[pad] >= 0) {
      key_release(ctx, pad);
    }
  }
  rig_->sound_keys.release_all();
  setup_keys_.release_all();
  knobs_keys_.release_all();
  live_->snap_finish(ctx, true);
  live_->leave(ctx);  // the actions held end with the screen (their releases would reach another one); the latched ones stay on (D4)
  tool_mask_ = 0;
  perf_down_ = 0;
}

void PerformScreen::update(app::Context& ctx) noexcept {
  playing_ = ctx.engine().playing();
  sync_knobs();
  if (toast_[0] != '\0' && ctx.uptime_ms() >= toast_until_ms_) {
    toast_[0] = '\0';
    ctx.redraw();
  }
}

void PerformScreen::toast(app::Context& ctx, const char* text, bool off) noexcept {
  cut(text, sizeof toast_ - 1u, toast_, sizeof toast_);
  toast_off_ = off;
  toast_until_ms_ = ctx.uptime_ms() + kToastMs;
  ctx.redraw();
}

// ---- Input ------------------------------------------------------------------------------------------------------------------

void PerformScreen::input(app::Context& ctx, const Input& in) noexcept {
  playing_ = ctx.engine().playing();
  sync_knobs();
  if (in.kind == core::InputKind::Pad) {
    pad_input(ctx, in);
  } else if (in.kind == core::InputKind::Encoder) {
    turn_input(ctx, in);
  } else if (in.kind == core::InputKind::Button) {
    button_input(ctx, in);
  }
}

void PerformScreen::pad_input(app::Context& ctx, const Input& in) noexcept {
  const unsigned pad = in.pad();
  const PerformPad pp = perform_pad(pad);
  const std::uint64_t bit = static_cast<std::uint64_t>(1) << pad;
  if (!in.pad_press()) {  // a release: ends what the press began (on the page that began it: KNOBS' pads have nothing to end)
    if ((perf_down_ & bit) != 0u) {
      perf_down_ &= ~bit;
      if (pp.role == PerformRole::Action) {
        live_->action_release(ctx, static_cast<PerfAction>(pp.index));
      } else if (pp.role == PerformRole::Key) {
        key_release(ctx, pad);
      }
    }
    ctx.redraw();
    return;
  }
  if (page_ == PerformPage::Knobs) {
    knobs_press(ctx, app::pad_row(pad), app::pad_col(pad));
    ctx.redraw();
    return;
  }
  perf_down_ |= bit;
  switch (pp.role) {
    case PerformRole::Action:
      live_->action_press(ctx, static_cast<PerfAction>(pp.index), in.shift);
      break;
    case PerformRole::AllOff:
      live_->all_off(ctx);
      toast(ctx, "ALL OFF");
      break;
    case PerformRole::Snapshot:
      snapshot_pad(ctx, pp.index, in.shift);
      break;
    case PerformRole::New:
    case PerformRole::Vary:
    case PerformRole::Undo:
    case PerformRole::Redo:
      variation(ctx, pp.role);
      break;
    case PerformRole::Ladder:
      if (live_->wobble_on()) {  // WOBBLE on: row 3 is the wobble's rate; column 12 has none
        live_->wobble_rate(ctx, perform_wobble_rate(pad));
      } else {
        ladder_press(ctx, pp.index);
      }
      break;
    case PerformRole::Key:
      key_press(ctx, pad);
      break;
    case PerformRole::None:
      break;
  }
  ctx.redraw();
}

// A tap recalls (and gives the encoder to the morph), Shift + a tap stores; an empty slot says so and does nothing.
void PerformScreen::snapshot_pad(app::Context& ctx, unsigned slot, bool shift) noexcept {
  LiveActions::Message msg;
  live_->snap_press(ctx, slot, shift, msg);
  if (!shift && !msg.off) {
    morph_ = true;
  }
  toast(ctx, msg.text, msg.off);
}

// NEW and VARY change the pattern shown (one UNDO step each), as the 303 screen's tools do; UNDO and REDO walk the history.
void PerformScreen::variation(app::Context& ctx, PerformRole role) noexcept {
  seq::Bank& bank = *rig_->bank;
  switch (role) {
    case PerformRole::New:
      (void)bank.random_fill(sel(), rig_->wiring.gen, live_->next_seed(ctx));
      toast(ctx, "NEW LINE");
      break;
    case PerformRole::Vary:
      (void)bank.mutate(sel(), rig_->wiring.gen, live_->next_seed(ctx), kVaryPct);
      toast(ctx, "VARIED");
      break;
    case PerformRole::Undo: {
      const bool done = undo_step(*rig_, false);
      toast(ctx, done ? "UNDO" : "NOTHING TO UNDO", !done);
      break;
    }
    default: {
      const bool done = undo_step(*rig_, true);
      toast(ctx, done ? "REDO" : "NOTHING TO REDO", !done);
      break;
    }
  }
}

// Row 3: a ladder row (ladder_rows.h): twelve places on the knob it holds (CUTOFF, HOME's FILT row 3, until assigned with an F key held), by its normalised
// position, so a log cutoff is a log ladder; the first pad is the minimum and the last the maximum. A tap jumps there through a slew (LADDER SLEW, 30 ms by default: so a jump does
// not click and a p-lock or a motion point on the step wins over it); a switch (the wave) is set at once. With REC while the pattern plays the jump is written under the playhead like a turn.
// With an F key held the tap only assigns the row that key's knob (again: back to CUTOFF) and the value does not move.
void PerformScreen::ladder_press(app::Context& ctx, unsigned col) noexcept {
  Wiring& w = rig_->wiring;
  if (rig_->sound_keys.held() != 0u) {
    if (ladder_assign(w, *controls_, kLadderHome3, rig_->sound_keys.focused()) == LadderAssign::NoLane) {
      toast(ctx, "THAT KNOB HAS NO LADDER", true);
    }
    ctx.redraw();
    return;
  }
  const Entry entry = ladder_entry(*controls_, w, kLadderHome3);
  const LadderTap t = ladder_tap(ctx, *rig_, home_->recording_motion(), sel(), entry, col);
  if (t.lanes_full) {
    toast(ctx, kLanesUsedToast, true);
  }
  if (t.not_recorded) {
    toast(ctx, kNotRecordedToast, true);
  }
}

// KNOBS: row `row` (0..3, top to bottom) is a ladder row of its own (kKnobsRowId: rows 3 and 4 are HOME's FILT rows). A tap on place `col` jumps its knob there as PERFORM's row 3 does (a slew, a switch at
// once, REC writes the jump, a mod switches MODS on) and gives the encoder that row's knob (Q1-A: the latch moves to F5..F8; with an F key held the latch stays where it is).
void PerformScreen::knobs_press(app::Context& ctx, unsigned row, unsigned col) noexcept {
  if (row >= kKnobsRows) {
    return;
  }
  Wiring& w = rig_->wiring;
  const unsigned id = kKnobsRowId[row];
  if (knobs_keys_.latch(4u + row)) {
    morph_ = false;  // the encoder is a knob again
  }
  const LadderTap t = ladder_tap(ctx, *rig_, home_->recording_motion(), sel(), ladder_entry(*controls_, w, id), col);
  if (t.lanes_full) {
    toast(ctx, kLanesUsedToast, true);
  }
  if (t.not_recorded) {
    toast(ctx, kNotRecordedToast, true);
  }
}

// "ROW 2: GROWL / FMT Q".
void PerformScreen::knobs_toast(app::Context& ctx, unsigned row, const char* family, const char* knob) noexcept {
  char text[32];  // toast_'s size
  text[0] = '\0';
  append(text, sizeof text, "ROW ");
  const char num[2] = {static_cast<char>('1' + row), '\0'};
  append(text, sizeof text, num);
  append(text, sizeof text, ": ");
  if (family != nullptr && family[0] != '\0') {
    append(text, sizeof text, family);
    append(text, sizeof text, " / ");
  }
  append(text, sizeof text, knob);
  toast(ctx, text);
}

// F1..F4: the set in use becomes `set`; the four rows swap at once (the knob pads and the F row show the new knobs).
void PerformScreen::knobs_set(app::Context& ctx, unsigned set) noexcept {
  morph_ = false;
  knob_set_use(rig_->wiring, set);
  sync_knobs();
  char text[32];
  text[0] = '\0';
  append(text, sizeof text, "SET ");
  const char num[3] = {static_cast<char>('1' + (set == kKnobSetTract ? kKnobSetGrowl : set)), ' ', '\0'};  // TRACT is set 3's second knob set
  append(text, sizeof text, num);
  append(text, sizeof text, knob_set_name(set));
  toast(ctx, text);
}

// KNOBS with Shift held: F1..F4 put set 1..4 back to its factory knobs (the set in use follows at once), F7 and F8 are the pattern's REDO and UNDO as on PERFORM.
void PerformScreen::knobs_shift_key(app::Context& ctx, unsigned k) noexcept {
  morph_ = false;
  if (k < kKnobSetsOnKeys) {
    knob_set_factory(rig_->wiring, key_set(k));
    sync_knobs();
    char text[32];
    text[0] = '\0';
    append(text, sizeof text, "SET ");
    const char num[3] = {static_cast<char>('1' + k), ' ', '\0'};
    append(text, sizeof text, num);
    append(text, sizeof text, "FACTORY: ");
    append(text, sizeof text, knob_set_name(key_set(k)));
    toast(ctx, text);
  } else if (k == 6u) {
    variation(ctx, PerformRole::Redo);
  } else if (k == 7u) {
    variation(ctx, PerformRole::Undo);
  }
}

// A row's F key (F5..F8) held and Up / Down: the row holds the next (Up) or previous (Down) knob of the list of every knob a lane can drive, in family order; with Shift the first knob of the next or
// previous family. The set in use keeps it (the rows are the set's, written through). The F key stays latched: the encoder is that row's new knob after the release.
bool PerformScreen::knobs_repoint(app::Context& ctx, const Input& in) noexcept {
  const PageKeys& pk = knobs_keys_;
  if (pk.held() == 0u || pk.focus() < 4u || in.type != Input::Type::Press || !(in.is(app::Button::Up) || in.is(app::Button::Down))) {
    return false;
  }
  const unsigned row = pk.focus() - 4u;
  const int delta = in.is(app::Button::Up) ? 1 : -1;
  const char* family = "";
  const Entry en = in.shift ? ladder_walk_family(rig_->wiring, *controls_, kKnobsRowId[row], delta, &family) : ladder_walk(rig_->wiring, *controls_, kKnobsRowId[row], delta, &family);
  sync_knobs();
  knobs_toast(ctx, row, family, controls_->label(ctx.engine(), en));
  return true;
}

// Row 4: a key plays the voice, or (the keyboard in its transposing mode while the pattern runs) transposes the pattern: the bottom left C is no
// transposition. The octave, the scale lock and the mode are the 303 screen's keyboard's.
void PerformScreen::key_press(app::Context& ctx, unsigned pad) noexcept {
  const int note = key_note(pad, home_->octave());
  if (note < 0 || home_->scale_locked_out(note)) {  // not a key, or out of the scale the keyboard is locked to: dark, nothing
    return;
  }
  if (playing_ && live_->trnsp()) {
    int t = note - kLowestNote;
    t = t < -36 ? -36 : (t > 36 ? 36 : t);
    live_->set_live_transpose(t);
    return;
  }
  key_note_[pad] = note;
  (void)ctx.engine().note_on(rig_->wiring.voice, static_cast<std::uint8_t>(note), 90, 0);
}

void PerformScreen::key_release(app::Context& ctx, unsigned pad) noexcept {
  if (key_note_[pad] >= 0) {
    (void)ctx.engine().note_off(rig_->wiring.voice, static_cast<std::uint8_t>(key_note_[pad]));
    key_note_[pad] = -1;
  }
}

void PerformScreen::turn_input(app::Context& ctx, const Input& in) noexcept {
  if (in.delta() == 0) {
    return;
  }
  if (page_ != PerformPage::Setup && morph_) {  // a snapshot was recalled: the encoder is the morph until an F key is tapped
    LiveActions::Message msg;
    if (!live_->snap_turn(ctx, in.delta(), in.shift, msg)) {
      toast(ctx, msg.text, msg.off);
    }
    return;
  }
  PageKeys& pk = keys();
  const std::uint32_t affected = pk.held() != 0u ? pk.held() : (1u << pk.focus());
  const Page& pg = pk.page();
  pk.turn(ctx, *controls_, in.delta(), in.shift);
  if (page_ != PerformPage::Setup && home_->recording_motion()) {
    // Recording: an encoder move on a knob a lane drives is written as motion under the playhead, on the lane of the playing pattern.
    const seq::Playhead ph = rig_->seq->playhead();
    const Pattern& playing = rig_->bank->pattern(ph.playing && ph.pattern >= 0 ? static_cast<unsigned>(ph.pattern) : sel());
    for (unsigned k = 0; k < kKeys; ++k) {
      if ((affected >> k & 1u) == 0u) {
        continue;
      }
      const int lane = lane_for(*controls_, pg.entry[k], playing);
      if (lane >= 0) {
        (void)rig_->seq->record_motion(static_cast<unsigned>(lane), controls_->to_lane(pg.entry[k], controls_->get(ctx.engine(), pg.entry[k])));
      }
    }
  }
}

void PerformScreen::button_input(app::Context& ctx, const Input& in) noexcept {
  using app::Button;
  if (Input held_turn; keys().arrow_turn(in, held_turn)) {  // Left / Right with an F key held: its knob by 1/20 of the range, as a turn
    turn_input(ctx, held_turn);
    keys().end_press();
    return;
  }
  if (page_ == PerformPage::Knobs && knobs_repoint(ctx, in)) {  // KN3: a row's F key held and Up / Down: that row's next knob
    ctx.redraw();
    return;
  }
  if (page_ != PerformPage::Setup) {  // KN2: Left and Right move the latch, Up and Down the latched knob by 1/20 (repeating while held)
    if (keys().move_latch(in)) {
      morph_ = false;  // the encoder is the knob again
      ctx.redraw();
      return;
    }
    if (Input step; keys().value_arrow(in, step)) {
      turn_input(ctx, step);
      keys().end_press();
      return;
    }
  }
  if (in.repeat) {  // a held arrow's repeat with no F key held: nothing (a press of a key does its own thing)
    return;
  }
  if (app::is_fkey(in.which())) {
    const unsigned k = app::fkey_index(in.which());
    if (in.type == Input::Type::Press) {
      if (in.shift) {  // the Shift row: REDO on F7 and UNDO on F8, the others empty (KNOBS: FACTORY 1..4 on F1..F4, and the same REDO and UNDO); PERFORM SETUP has none
        tool_mask_ |= 1u << k;
        if (page_ == PerformPage::Knobs) {
          knobs_shift_key(ctx, k);
        } else if (page_ == PerformPage::Perform && k == 6u) {
          variation(ctx, PerformRole::Redo);
        } else if (page_ == PerformPage::Perform && k == 7u) {
          variation(ctx, PerformRole::Undo);
        }
        ctx.redraw();
        return;
      }
      if (page_ == PerformPage::Knobs && k < kKnobSetsOnKeys) {  // KNOBS' F1..F4 are the sets; F3 while its set is in use flips GROWL and TRACT (F1)
        const unsigned in_use = rig_->wiring.knob_set;
        knobs_set(ctx, k == 2u && (in_use == kKnobSetGrowl || in_use == kKnobSetTract) ? (in_use == kKnobSetGrowl ? kKnobSetTract : kKnobSetGrowl) : key_set(k));
        ctx.redraw();
        return;
      }
      morph_ = false;  // an F key gives the encoder back to the knob
      (void)keys().input(ctx, in, *controls_);
    } else if (in.type == Input::Type::Release) {
      if ((tool_mask_ >> k & 1u) != 0u) {
        tool_mask_ &= ~(1u << k);
        return;
      }
      if (page_ == PerformPage::Knobs && k < kKnobSetsOnKeys) {
        return;
      }
      (void)keys().input(ctx, in, *controls_);
    }
    return;
  }
  const Button b = in.which();
  const bool colour = b == Button::Note || b == Button::Instr || b == Button::Vol || b == Button::Fx;
  if (colour) {  // the four assignable buttons: the live actions (HOME, SETUP and INSTRUMENT have the drawer rows on them)
    const unsigned index = static_cast<unsigned>(b) - static_cast<unsigned>(Button::Note);
    if (in.type == Input::Type::Press) {
      live_->button_press(ctx, index, in.shift);
    } else if (in.type == Input::Type::Release) {
      live_->button_release(ctx, index);
    }
    return;
  }
  if (b == Button::Rec && in.type == Input::Type::Press) {  // Rec while playing records the ladder and the encoder as motion, as on the 303 screen
    home_->toggle_rec(ctx);
  }
}

// ---- Soft keys and LEDs -----------------------------------------------------------------------------------------------------

// The Shift row: REDO and UNDO on F7 and F8, the rest empty (the knob row carries the names otherwise).
void PerformScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  if (page_ == PerformPage::Setup || !ctx.panel().shift()) {
    return;
  }
  if (page_ == PerformPage::Knobs) {
    static constexpr const char* kFactoryLabels[kKnobSetsOnKeys] = {"FACTORY 1", "FACTORY 2", "FACTORY 3", "FACTORY 4"};
    for (unsigned k = 0; k < kKnobSetsOnKeys; ++k) {
      out.label[k] = kFactoryLabels[k];
    }
  }
  out.label[6] = "REDO";
  out.label[7] = "UNDO";
  if (!rig_->bank->can_redo()) {
    out.dim = static_cast<std::uint8_t>(out.dim | (1u << 6));
  }
  if (!rig_->bank->can_undo()) {
    out.dim = static_cast<std::uint8_t>(out.dim | (1u << 7));
  }
}

void PerformScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  (void)ctx;
  help_texts::fill(h, help_texts::text(page_ == PerformPage::Setup ? help_texts::Id::PerformSetup : (page_ == PerformPage::Knobs ? help_texts::Id::Knobs : help_texts::Id::Perform)));
  if (page_ == PerformPage::Knobs) {  // the Shift row of KNOBS (soft_keys()): FACTORY 1..4 on F1..F4, REDO and UNDO as on PERFORM
    static constexpr const char* kFactoryLabels[kKnobSetsOnKeys] = {"FACTORY 1", "FACTORY 2", "FACTORY 3", "FACTORY 4"};
    for (unsigned k = 0; k < kKnobSetsOnKeys; ++k) {
      h.fkey_shift[k] = kFactoryLabels[k];
    }
  }
  if (page_ != PerformPage::Setup) {  // the Shift row's REDO on F7 and UNDO on F8 (soft_keys())
    h.fkey_shift[6] = "REDO";
    h.fkey_shift[7] = "UNDO";
  }
  help_texts::fill_perform_pads(h, page_ == PerformPage::Knobs);
  if (page_ == PerformPage::Knobs) {
    return;
  }
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {  // the keys outside the scale the keyboard is locked to do nothing (key_press()) and are dark (leds())
    if (perform_pad(pad).role != PerformRole::Key) {
      continue;
    }
    const int note = key_note(pad, home_->octave());
    if (note < 0 || home_->scale_locked_out(note)) {
      h.pad_group[pad] = 0;
      h.pad_label[pad] = nullptr;
    }
  }
}

void PerformScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  PerformView view;
  fill_view(ctx, view);
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    leds.set(pad, view.lamp[pad].level);
  }
}

// ---- The view -----------------------------------------------------------------------------------------------------------------

// KNOBS: four ladder rows (kKnobsRowId), each lamp the value its pad jumps to; the row's knob and where it stands go in `row_knob` and `row_value`.
void PerformScreen::fill_knobs_view(app::Context& ctx, PerformView& view) const noexcept {
  const dsp::Engine& e = ctx.engine();
  view.knobs = true;
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    view.lamp[pad] = PadLamp{};
  }
  for (unsigned row = 0; row < kKnobsRows; ++row) {
    const Entry en = ladder_entry(*controls_, rig_->wiring, kKnobsRowId[row]);
    const core::ParamDesc* kd = controls_->desc(e, en);
    const float value = kd != nullptr ? controls_->shown(e, en) : 0.0f;
    KnobSpan span;
    const KnobSpan* sp = controls_->live_span(e, en, span) ? &span : nullptr;
    std::uint8_t levels[kLadderSteps];
    ladder_levels(kd, value, false, levels, sp);
    for (unsigned col = 0; col < kLadderSteps; ++col) {
      PadLamp& lamp = view.lamp[app::pad_at(row, col)];
      lamp.level = levels[col];
      if (kd != nullptr && col < ladder_places(*kd)) {
        char text[core::kParamTextChars + 1];
        (void)core::param_format(*kd, ladder_value(*kd, col, sp), text, sizeof text);
        cut(text, 5, lamp.label, sizeof lamp.label);
      }
    }
    cut(controls_->label(e, en), sizeof view.row_knob[row] - 1u, view.row_knob[row], sizeof view.row_knob[row]);
    if (kd != nullptr) {
      char text[core::kParamTextChars + 1];
      (void)core::param_format(*kd, value, text, sizeof text);
      cut(text, sizeof view.row_value[row] - 1u, view.row_value[row], sizeof view.row_value[row]);
    }
  }
}

void PerformScreen::fill_view(app::Context& ctx, PerformView& view) const noexcept {
  if (page_ == PerformPage::Knobs) {
    fill_knobs_view(ctx, view);
    return;
  }
  const dsp::Engine& e = ctx.engine();
  const SnapBank& snaps = rig_->wiring.snaps;
  const bool wobble = live_->wobble_on();
  const bool transposing = playing_ && live_->trnsp();
  // Row 3's knob (CUTOFF unless an F key assigned another) and where it stands.
  const Entry knob = ladder_entry(*controls_, rig_->wiring, kLadderHome3);
  const bool assigning = rig_->sound_keys.held() != 0u && app::PadLeds::blink(ctx.now(), 250);  // an F key is down: the row blinks, a tap assigns
  const core::ParamDesc* kd = controls_->desc(e, knob);
  std::uint8_t knob_level[kLadderSteps];  // the row's twelve lamps (a list has a pad a value)
  KnobSpan knob_span;
  const KnobSpan* sp = controls_->live_span(e, knob, knob_span) ? &knob_span : nullptr;
  ladder_levels(kd, kd != nullptr ? controls_->shown(e, knob) : 0.0f, assigning, knob_level, sp);
  const int wobble_now = rig_->seq->wobble_rate_now();
  const bool scale_shown = home_->scale_shown();

  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    PadLamp& lamp = view.lamp[pad];
    lamp = PadLamp{};
    const PerformPad pp = perform_pad(pad);
    switch (pp.role) {
      case PerformRole::Action: {
        const PerfAction a = static_cast<PerfAction>(pp.index);
        cut(kActionLabels[pp.index], 7, lamp.label, sizeof lamp.label);
        lamp.level = live_->is_on(a) ? app::kLedOn : app::kLedDim;
        lamp.latched = live_->latched(a);
        break;
      }
      case PerformRole::AllOff:
        cut("ALL OFF", 7, lamp.label, sizeof lamp.label);
        lamp.level = app::kLedDim;
        break;
      case PerformRole::Snapshot: {
        char num[4];
        view::fmt_uint(pp.index + 1u, num);
        cut(num, 7, lamp.label, sizeof lamp.label);
        lamp.level = snaps.last() == static_cast<int>(pp.index) ? app::kLedOn : (snaps.used(pp.index) ? app::kLedDim : app::kLedOff);
        break;
      }
      case PerformRole::New:
        cut("RANDOM", 7, lamp.label, sizeof lamp.label);
        lamp.level = app::kLedDim;
        break;
      case PerformRole::Vary:
        cut("VARY", 7, lamp.label, sizeof lamp.label);
        lamp.level = app::kLedDim;
        break;
      case PerformRole::Undo:
        cut("UNDO", 7, lamp.label, sizeof lamp.label);
        lamp.level = rig_->bank->can_undo() ? app::kLedDim : app::kLedOff;
        break;
      case PerformRole::Redo:
        cut("REDO", 7, lamp.label, sizeof lamp.label);
        lamp.level = rig_->bank->can_redo() ? app::kLedDim : app::kLedOff;
        break;
      case PerformRole::Ladder: {
        const unsigned col = pp.index;
        if (wobble) {  // the rate picker: the rate the wobble runs at now at full level
          const int rate = perform_wobble_rate(pad);
          if (rate >= 0) {
            cut(seq::kWobbleRateNames[rate], 7, lamp.label, sizeof lamp.label);
            lamp.level = rate == wobble_now ? app::kLedOn : app::kLedDim;
          }
        } else {  // a bar from the first pad to the position, the position itself at full level; each lamp says the value it jumps to
          if (kd != nullptr && col < ladder_places(*kd)) {
            char text[core::kParamTextChars + 1];
            (void)core::param_format(*kd, ladder_value(*kd, col, sp), text, sizeof text);
            cut(text, 5, lamp.label, sizeof lamp.label);
          }
          lamp.level = knob_level[col];
        }
        break;
      }
      case PerformRole::Key: {
        const int n = key_note(pad, home_->octave());
        if (n < 0) {
          break;
        }
        cut(kNoteLabels[pp.index], 7, lamp.label, sizeof lamp.label);
        if (home_->scale_locked_out(n)) {  // out of the scale KEYS: IN SCALE locks the keyboard to: dark
          break;
        }
        // naturals dim, sharps off (with a scale shown, the notes of the scale dim and the others off); the pad of the transposition in force on; a held pad on
        lamp.level = (scale_shown ? home_->in_scale_note(n) : is_natural(static_cast<unsigned>(n))) ? app::kLedDim : app::kLedOff;
        if (transposing && n == kLowestNote + live_->live_transpose()) {
          lamp.level = app::kLedOn;
        }
        if (ctx.panel().pad_held(pad)) {
          lamp.level = app::kLedOn;
        }
        break;
      }
      case PerformRole::None:
        break;
    }
  }
  if (wobble) {
    cut("WOBBLE RATE", 15, view.ladder_caption, sizeof view.ladder_caption);
  } else {
    ladder_caption(e, *controls_, rig_->wiring, kLadderHome3, view.ladder_caption, sizeof view.ladder_caption);
  }
  cut(transposing ? "TRANSPOSE" : "PLAY VOICE", 15, view.keys_caption, sizeof view.keys_caption);
}

// "F1 CUTOFF", or "MORPH 1>2 50%" after a recall.
void PerformScreen::enc_text(app::Context& ctx, char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  if (page_ != PerformPage::Setup && morph_) {
    const SnapBank& snaps = rig_->wiring.snaps;
    char num[8];
    append(out, cap, "MORPH ");
    if (snaps.previous() >= 0) {
      view::fmt_uint(static_cast<unsigned>(snaps.previous()) + 1u, num);
      append(out, cap, num);
    } else {
      append(out, cap, "LIVE");
    }
    append(out, cap, ">");
    view::fmt_uint(static_cast<unsigned>(snaps.last()) + 1u, num);
    append(out, cap, num);
    append(out, cap, " ");
    unsigned n = view::fmt_uint(snaps.position() * 100u / kMorphSteps, num);
    num[n] = '%';
    num[n + 1u] = '\0';
    append(out, cap, num);
    return;
  }
  const PageKeys& pk = page_ == PerformPage::Setup ? setup_keys_ : (page_ == PerformPage::Knobs ? knobs_keys_ : rig_->sound_keys);
  char fk[4] = {'F', static_cast<char>('1' + pk.focus()), ' ', '\0'};
  append(out, cap, fk);
  append(out, cap, controls_->label(ctx.engine(), pk.focused()));
}

void PerformScreen::fill_status(app::Context& ctx, frame::StatusView& status) const noexcept {
  enc_text(ctx, status.enc, sizeof status.enc);
  status.trnsp = live_->trnsp();
  status.octave = home_->octave();
  status.rec = home_->rec();
  fill_take_status(*rig_, status);
  status.actions = live_->on();
  status.toast = toast_;
  status.toast_off = toast_off_;
  status.save_failed = rig_->project.failed;
  status.no_autosave = !rig_->project.autosave;
}

// ROW 1 ACTIONS (SHIFT = LATCH) . ROW 2 SNAPSHOTS (SHIFT = STORE) . ROW 3 LADDER CUTOFF . ROW 4 KEYS  (WOBBLE on: ROW 3 WOBBLE RATE)
void PerformScreen::pads_line(app::Context& ctx, char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  if (page_ == PerformPage::Knobs) {  // ROW 1 ENV MOD . ROW 2 DECAY . ROW 3 CUTOFF . ROW 4 RESO
    for (unsigned row = 0; row < kKnobsRows; ++row) {
      const char num[3] = {static_cast<char>('1' + row), ' ', '\0'};
      append(out, cap, row == 0u ? "ROW " : " . ROW ");
      append(out, cap, num);
      append(out, cap, ladder_short_name(ctx.engine(), *controls_, rig_->wiring, kKnobsRowId[row]));
    }
    return;
  }
  append(out, cap, "ROW 1 ACTIONS (SHIFT = LATCH) . ROW 2 SNAPSHOTS (SHIFT = STORE) . ROW 3 ");
  if (live_->wobble_on()) {
    append(out, cap, "WOBBLE RATE");
  } else {
    append(out, cap, "LADDER ");
    append(out, cap, ladder_short_name(ctx.engine(), *controls_, rig_->wiring, kLadderHome3));
  }
  append(out, cap, " . ROW 4 KEYS");
}

// The four colour buttons and what each does now (the PERF page assigns them): an Accent chip while held, a Sel chip while latched, else an outline.
void PerformScreen::draw_colour_buttons(app::Context& ctx, ui::Renderer& r, const Rect& line) const noexcept {
  (void)ctx;
  static constexpr const char* kButtonNames[kPerfButtons] = {"NOTE", "INSTR", "VOL", "FX"};
  const int baseline = line.y + 15;
  ui::FieldRow row(r, line.x + kLabelX, baseline);
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    const unsigned a = rig_->wiring.perf[b];
    char text[24];
    text[0] = '\0';
    append(text, sizeof text, kButtonNames[b]);
    append(text, sizeof text, ": ");
    append(text, sizeof text, a < kPerfActionCount ? kPerfActionNames[a] : "");
    if ((live_->buttons_latched() >> b & 1u) != 0u) {
      row.chip(Face::Label, text, Color::Sel, Color::SelText);
    } else if ((live_->buttons_held() >> b & 1u) != 0u) {
      row.chip(Face::Label, text, Color::Accent, Color::AccentText);
    } else {
      row.put(Face::Label, text, Color::TextDim);
    }
    row.skip(22);
  }
  const char* hint = "SHIFT + BUTTON OR PAD = LATCH";
  r.text(line.x + line.w - 8 - r.text_width(hint, Face::Micro), line.y + 4, hint, Color::Empty, Face::Micro);
}

// The four colour buttons as the skin's chips show them: the button, its assigned action and whether it is held or latched.
void PerformScreen::fill_chips(PerformView& view) const noexcept {
  static constexpr const char* kButtonNames[kPerfButtons] = {"NOTE", "INSTR", "VOL", "FX"};
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    PerformChip& chip = view.chip[b];
    cut(kButtonNames[b], sizeof chip.name - 1u, chip.name, sizeof chip.name);
    const unsigned a = rig_->wiring.perf[b];
    cut(a < kPerfActionCount ? kPerfActionNames[a] : "", sizeof chip.action - 1u, chip.action, sizeof chip.action);
    chip.state = (live_->buttons_latched() >> b & 1u) != 0u ? ChipState::Latched : ((live_->buttons_held() >> b & 1u) != 0u ? ChipState::Held : ChipState::Idle);
  }
}

// The F row: the sound knobs (PERFORM), the setup page's (PERFORM SETUP), or, with Shift held, the Shift row (REDO and UNDO).
bool PerformScreen::fill_knob_row(app::Context& ctx, view::Knob (&knobs)[view::kKnobs], bool skin) noexcept {
  static constexpr const char* kFKeys[kKeys] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
  if (page_ != PerformPage::Setup && ctx.panel().shift()) {  // the Shift row: REDO and UNDO, and on KNOBS FACTORY 1..4 under F1..F4
    for (unsigned k = 0; k < kKeys; ++k) {
      knobs[k].fkey = kFKeys[k];
      knobs[k].blank = k < 6u && !(page_ == PerformPage::Knobs && k < kKnobSetsOnKeys);
      if (k < kKnobSetsOnKeys && page_ == PerformPage::Knobs) {
        const char num[7] = {'S', 'E', 'T', ' ', static_cast<char>('1' + k), '\0', '\0'};
        knobs[k].label = "FACTORY";
        cut(num, 7, knobs[k].value, sizeof knobs[k].value);
      }
      if (k == 6u || k == 7u) {
        const bool can = k == 6u ? rig_->bank->can_redo() : rig_->bank->can_undo();
        knobs[k].label = k == 6u ? "REDO" : "UNDO";
        cut(can ? (k == 6u ? "FWD" : "BACK") : "NONE", 7, knobs[k].value, sizeof knobs[k].value);
        knobs[k].dim = skin && !can;
      }
    }
    return true;
  }
  const PageKeys& pk = keys();
  const Page& pg = pk.page();
  for (unsigned k = 0; k < kKeys; ++k) {
    view::Knob& kn = knobs[k];
    if (page_ == PerformPage::Knobs && k < kKnobSetsOnKeys) {  // F1..F4 are the sets (F3: GROWL or TRACT, whichever is up): the one in use lit, "OWN n" once a row of it was re-pointed
      kn = view::Knob{};
      kn.fkey = kFKeys[k];
      const unsigned set = key_set(k);
      const bool factory = knob_set_is_factory(rig_->wiring, set);
      kn.label = knob_set_name(set);
      const char num[8] = {factory ? 'S' : 'O', factory ? 'E' : 'W', factory ? 'T' : 'N', ' ', static_cast<char>('1' + k), '\0', '\0', '\0'};
      cut(num, 7, kn.value, sizeof kn.value);
      kn.lit = set == rig_->wiring.knob_set;
      continue;
    }
    frame::fill_knob(ctx, *controls_, k, pg.entry[k], kn);
    if (kn.blank) {
      continue;
    }
    kn.focus = !(morph_ && page_ != PerformPage::Setup) && k == pk.focus();
    kn.held = (pk.held() >> k & 1u) != 0u;
  }
  return false;
}

void PerformScreen::draw_knob_row(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  view::Knob knobs[view::kKnobs];
  (void)fill_knob_row(ctx, knobs, false);
  view::draw_knobs(r, area, knobs);
}

app::Chrome PerformScreen::chrome(const app::Context& ctx) const noexcept { return skin::active(ctx) ? app::Chrome::None : app::Chrome::Full; }

const ui::Theme* PerformScreen::theme(const app::Context& ctx) const noexcept { return skin::active(ctx) ? &skin::theme() : nullptr; }

void PerformScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  if (skin::active(ctx)) {
    draw_skinned(ctx, r);
  } else {
    draw_classic(ctx, r, area);
  }
}

// The obsidian drawing (skin/perform.cpp): the header, the glass with the lamps, the F row. The same view the classic drawing takes, filled the same way.
void PerformScreen::draw_skinned(app::Context& ctx, ui::Renderer& r) noexcept {
  const std::uint32_t t0 = o3sdk::hal::cycles();
  fill_view(ctx, view_);
  fill_chips(view_);
  frame::StatusView status;
  fill_status(ctx, status);
  skin::HeaderInputs in;
  in.title = title();
  in.pattern = sel();
  in.tempo_mbpm = ctx.engine().tempo();
  in.playing = rig_->seq->playhead().playing;
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    in.perf[b] = rig_->wiring.perf[b];
  }
  in.now_ms = ctx.uptime_ms();
  in.covered = ctx.strip_area();
  view::Knob knobs[view::kKnobs];
  const bool shift_row = fill_knob_row(ctx, knobs, true);
  char line[128];
  pads_line(ctx, line, sizeof line);
  skin::draw_perform(r, view_, page_ == PerformPage::Setup, status, in, knobs, shift_row, line);
  constexpr std::uint32_t kCyclesPerUs = 180;  // the MK66's clock; the host's hal::cycles() is scaled to it
  skin::note_frame(static_cast<std::uint32_t>(r.words()), (o3sdk::hal::cycles() - t0) / kCyclesPerUs, ctx.uptime_ms());
}

void PerformScreen::draw_classic(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  frame::StatusView status;
  fill_status(ctx, status);
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);

  PerformView view;
  fill_view(ctx, view);
  const bool setup = page_ == PerformPage::Setup;
  const unsigned rows = setup ? 1u : 4u;  // PERFORM SETUP keeps a reminder of row 1 over an empty panel
  draw_view(r, area, view, rows);
  static constexpr const char* kRowNames[4] = {"ACTIONS", "SNAPS", "LADDER", "KEYS"};
  static constexpr const char* kKnobRowNames[4] = {"ROW 1", "ROW 2", "ROW 3", "ROW 4"};
  const char* captions[4] = {"", "", view.ladder_caption, view.keys_caption};
  char knob_caption[4][32] = {};  // KNOBS: each row's knob and where it stands
  if (view.knobs) {
    for (unsigned row = 0; row < 4u; ++row) {
      append(knob_caption[row], sizeof knob_caption[row], view.row_knob[row]);
      append(knob_caption[row], sizeof knob_caption[row], "  ");
      append(knob_caption[row], sizeof knob_caption[row], view.row_value[row]);
      captions[row] = knob_caption[row];
    }
  }
  for (unsigned row = 0; row < rows; ++row) {
    const int y = area.y + kGridTop + static_cast<int>(row) * kRowPitch;
    r.text(area.x + kLabelX, y + 8, view.knobs ? kKnobRowNames[row] : kRowNames[row], Color::Text, Face::Label);
    r.text(area.x + kLabelX, y + 30, captions[row], Color::TextDim, Face::Micro);
  }
  if (setup) {
    r.text(area.x + kLabelX, area.y + kGridTop + kRowPitch + 8, "PERFORM SETUP", Color::Text, Face::Data);
    r.text(area.x + kLabelX, area.y + kGridTop + kRowPitch + 40, "F1-F4 SET WHAT EACH COLOUR BUTTON DOES   F5 WHAT FILL PLAYS   F6-F8 FREEZE, ROLL, STOP", Color::TextDim,
           Face::Label);
    r.text(area.x + kLabelX, area.y + kGridTop + kRowPitch + 64, "THE PADS ARE PERFORM'S", Color::TextDim, Face::Label);
  }
  draw_colour_buttons(ctx, r, Rect{area.x, area.y + kButtonsY, area.w, 22});

  const Rect strip{area.x, area.y + area.h - app::kKnobStripH, area.w, app::kKnobStripH};
  char line[128];
  pads_line(ctx, line, sizeof line);
  frame::draw_pads_line(r, Rect{strip.x, strip.y, strip.w, frame::kPadsLineH}, line);
  draw_knob_row(ctx, r, strip);
}

}  // namespace o3_303
