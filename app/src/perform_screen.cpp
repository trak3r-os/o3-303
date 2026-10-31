// SPDX-License-Identifier: GPL-3.0-or-later
#include "perform_screen.h"

#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>
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
  const auto group = [](std::uint8_t level) -> unsigned {
    return level >= app::kLedOn ? 3u : (level >= app::kLedLow ? 2u : (level >= app::kLedDim ? 1u : 0u));
  };
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
  setup_ = false;
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

void PerformScreen::enter(app::Context& ctx) noexcept { ctx.redraw(); }

void PerformScreen::leave(app::Context& ctx) noexcept {
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    if (key_note_[pad] >= 0) {
      key_release(ctx, pad);
    }
  }
  rig_->sound_keys.release_all();
  setup_keys_.release_all();
  live_->snap_finish(ctx, true);
  live_->leave(ctx);  // the actions held end with the screen (their releases would reach another one); the latched ones stay on (D4)
  tool_mask_ = 0;
}

void PerformScreen::update(app::Context& ctx) noexcept {
  playing_ = ctx.engine().playing();
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
  if (!in.pad_press()) {  // a release: ends what the press began
    if (pp.role == PerformRole::Action) {
      live_->action_release(ctx, static_cast<PerfAction>(pp.index));
    } else if (pp.role == PerformRole::Key) {
      key_release(ctx, pad);
    }
    ctx.redraw();
    return;
  }
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
      const int i = bank.undo();
      if (i >= 0) {
        bank.select(static_cast<unsigned>(i));
      }
      toast(ctx, i >= 0 ? "UNDO" : "NOTHING TO UNDO", i < 0);
      break;
    }
    default: {
      const int i = bank.redo();
      if (i >= 0) {
        bank.select(static_cast<unsigned>(i));
      }
      toast(ctx, i >= 0 ? "REDO" : "NOTHING TO REDO", i < 0);
      break;
    }
  }
}

// Row 3: twelve places on the latched sound knob (its normalised position, so a log cutoff is a log ladder): the first pad is the minimum and the
// last the maximum. A tap jumps there through a slew (LADDER SLEW, 30 ms by default: so a jump does not click and a p-lock or a motion point on the step wins over it); a
// switch (the wave) is set at once. With REC while the pattern plays the jump is written under the playhead like a turn.
void PerformScreen::ladder_press(app::Context& ctx, unsigned col) noexcept {
  const Entry& entry = rig_->sound_keys.focused();
  dsp::Engine& e = ctx.engine();
  const core::ParamDesc* d = controls_->desc(e, entry);
  if (d == nullptr) {
    return;
  }
  const float value = core::param_denorm(*d, ladder_norm(col));
  LaneRef ref{kLaneNone, 0};
  const bool lane = controls_->lane_ref(entry, ref);
  if (!lane || d->unit == core::ParamUnit::Enum) {
    controls_->set(e, entry, value);
  } else {
    (void)rig_->seq->slew(ref.target, ref.id, controls_->to_lane(entry, value), live_->ladder_slew_ms());
  }
  if (lane && home_->recording_motion()) {
    const seq::Playhead ph = rig_->seq->playhead();
    const int ln = seq::find_lane(rig_->bank->pattern(ph.playing && ph.pattern >= 0 ? static_cast<unsigned>(ph.pattern) : sel()), ref.target, ref.id);
    if (ln >= 0) {
      (void)rig_->seq->record_motion(static_cast<unsigned>(ln), controls_->to_lane(entry, value));
    }
  }
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
  (void)ctx.engine().note_on(rig_->wiring.voice, static_cast<std::uint8_t>(note), 100, 0);
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
  if (!setup_ && morph_) {  // a snapshot was recalled: the encoder is the morph until an F key is tapped
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
  if (!setup_ && home_->recording_motion()) {
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
  if (app::is_fkey(in.which())) {
    const unsigned k = app::fkey_index(in.which());
    if (in.type == Input::Type::Press) {
      if (in.shift) {  // the Shift row: REDO on F7 and UNDO on F8, the others empty; PERFORM SETUP has none
        tool_mask_ |= 1u << k;
        if (!setup_ && k == 6u) {
          variation(ctx, PerformRole::Redo);
        } else if (!setup_ && k == 7u) {
          variation(ctx, PerformRole::Undo);
        }
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
  if (setup_ || !ctx.panel().shift()) {
    return;
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

void PerformScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  PerformView view;
  fill_view(ctx, view);
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    leds.set(pad, view.lamp[pad].level);
  }
}

// ---- The view -----------------------------------------------------------------------------------------------------------------

void PerformScreen::fill_view(app::Context& ctx, PerformView& view) const noexcept {
  const dsp::Engine& e = ctx.engine();
  const SnapBank& snaps = rig_->wiring.snaps;
  const bool wobble = live_->wobble_on();
  const bool transposing = playing_ && live_->trnsp();
  // Row 3's knob: the latched sound knob and where it stands.
  const Entry& knob = rig_->sound_keys.focused();
  const core::ParamDesc* kd = controls_->desc(e, knob);
  const unsigned knob_col = kd != nullptr ? ladder_col(core::param_norm(*kd, controls_->get(e, knob))) : 0u;
  const int wobble_now = rig_->seq->wobble_rate_now();
  const bool scale_locked = rig_->wiring.scale_set && rig_->wiring.gen.scale < core::kScaleCount && core::kScaleDef[rig_->wiring.gen.scale] != 0xFFFu;

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
        cut("NEW", 7, lamp.label, sizeof lamp.label);
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
          if (kd != nullptr) {
            char text[core::kParamTextChars + 1];
            (void)core::param_format(*kd, core::param_denorm(*kd, ladder_norm(col)), text, sizeof text);
            cut(text, 5, lamp.label, sizeof lamp.label);
          }
          lamp.level = col < knob_col ? app::kLedMid : (col == knob_col ? app::kLedOn : app::kLedOff);
        }
        break;
      }
      case PerformRole::Key: {
        const int n = key_note(pad, home_->octave());
        if (n < 0) {
          break;
        }
        cut(kNoteLabels[pp.index], 7, lamp.label, sizeof lamp.label);
        if (home_->scale_locked_out(n)) {  // out of the scale the keyboard is locked to: dark
          break;
        }
        // naturals dim, sharps off (with a scale chosen, lit means playable); the pad of the transposition in force on; a held pad on
        lamp.level = is_natural(static_cast<unsigned>(n)) || scale_locked ? app::kLedDim : app::kLedOff;
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
    cut(controls_->label(e, knob), 15, view.ladder_caption, sizeof view.ladder_caption);
  }
  cut(transposing ? "TRANSPOSE" : "PLAY VOICE", 15, view.keys_caption, sizeof view.keys_caption);
}

// "F1 CUTOFF", or "MORPH 1>2 50%" after a recall.
void PerformScreen::enc_text(app::Context& ctx, char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  if (!setup_ && morph_) {
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
  const PageKeys& pk = setup_ ? setup_keys_ : rig_->sound_keys;
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
}

// ROW 1 ACTIONS (SHIFT = LATCH) . ROW 2 SNAPSHOTS (SHIFT = STORE) . ROW 3 LADDER CUTOFF . ROW 4 KEYS  (WOBBLE on: ROW 3 WOBBLE RATE)
void PerformScreen::pads_line(app::Context& ctx, char* out, unsigned cap) const noexcept {
  out[0] = '\0';
  append(out, cap, "ROW 1 ACTIONS (SHIFT = LATCH) . ROW 2 SNAPSHOTS (SHIFT = STORE) . ROW 3 ");
  if (live_->wobble_on()) {
    append(out, cap, "WOBBLE RATE");
  } else {
    append(out, cap, "LADDER ");
    append(out, cap, controls_->label(ctx.engine(), rig_->sound_keys.focused()));
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

// The knob row: the sound knobs (PERFORM), the setup page's (PERFORM SETUP), or, with Shift held, the Shift row (REDO and UNDO).
void PerformScreen::draw_knob_row(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  static constexpr const char* kFKeys[kKeys] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
  view::Knob knobs[view::kKnobs];
  if (!setup_ && ctx.panel().shift()) {
    for (unsigned k = 0; k < kKeys; ++k) {
      knobs[k].fkey = kFKeys[k];
      knobs[k].blank = k < 6u;
      if (k == 6u || k == 7u) {
        const bool can = k == 6u ? rig_->bank->can_redo() : rig_->bank->can_undo();
        knobs[k].label = k == 6u ? "REDO" : "UNDO";
        cut(can ? (k == 6u ? "FWD" : "BACK") : "NONE", 7, knobs[k].value, sizeof knobs[k].value);
      }
    }
    view::draw_knobs(r, area, knobs);
    return;
  }
  const PageKeys& pk = keys();
  const Page& pg = pk.page();
  for (unsigned k = 0; k < kKeys; ++k) {
    view::Knob& kn = knobs[k];
    frame::fill_knob(ctx, *controls_, k, pg.entry[k], kn);
    if (kn.blank) {
      continue;
    }
    kn.focus = !(morph_ && !setup_) && k == pk.focus();
    kn.held = (pk.held() >> k & 1u) != 0u;
  }
  view::draw_knobs(r, area, knobs);
}

void PerformScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  frame::StatusView status;
  fill_status(ctx, status);
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);

  PerformView view;
  fill_view(ctx, view);
  const unsigned rows = setup_ ? 1u : 4u;  // PERFORM SETUP keeps a reminder of row 1 over an empty panel
  draw_view(r, area, view, rows);
  static constexpr const char* kRowNames[4] = {"ACTIONS", "SNAPS", "LADDER", "KEYS"};
  const char* captions[4] = {"", "", view.ladder_caption, view.keys_caption};
  for (unsigned row = 0; row < rows; ++row) {
    const int y = area.y + kGridTop + static_cast<int>(row) * kRowPitch;
    r.text(area.x + kLabelX, y + 8, kRowNames[row], Color::Text, Face::Label);
    r.text(area.x + kLabelX, y + 30, captions[row], Color::TextDim, Face::Micro);
  }
  if (setup_) {
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
