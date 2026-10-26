// SPDX-License-Identifier: GPL-3.0-or-later
//
// The performance gestures of the 303 screen (docs/M3B-PLAN.md C4, R1, R7, R8), the second half of MainScreen:
//
//   Note / Instr / Vol / Fx   four assignable buttons (the PERF page), held = momentary, Shift + tap latches (C4)
//   hold KEYS + a pad         rows 2 and 3 are the Cutoff and Reso ladders (Shift: Env Mod), through a 30 ms slew; the LEDs are the bar (R1)
//   tap Master                the encoder latches TEMPO (Shift: SWING), whatever page the F keys are on (R7)
//   hold Master + turn        a tempo nudge of 1 % a detent, at most 3 %, back at the release (R7)
//   a scale chosen            the keyboard pads out of it are dark and do nothing (R8)
//
// The pure parts (which actions are on, the ladder positions, the nudge, the scale) are in o3_303/perform.h and tested alone; what is
// here puts them on the engine, the sequencer and the pads.

#include "main_screen.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>
#include <o3_303/units/tb303/tb303.h>

namespace o3_303 {

namespace {

using namespace o3sdk;
using ui::Color;
using ui::Face;

constexpr int kLiveTransposeMax = 48;  // the sequencer's limit
constexpr int kOctave = 12;

// `s` appended to the text in `out` (cap bytes with the terminator).
void cat(char* out, unsigned cap, const char* s) noexcept {
  unsigned n = 0;
  while (n < cap && out[n] != '\0') {
    ++n;
  }
  for (; *s != '\0' && n + 1u < cap; ++s) {
    out[n++] = *s;
  }
  if (n < cap) {
    out[n] = '\0';
  }
}

}  // namespace

// ---- The four performance buttons (C4) ------------------------------------------------------------------------------------------

void MainScreen::perf_press(app::Context& ctx, unsigned button, bool shift) noexcept {
  if (button >= kPerfButtons) {
    return;
  }
  if (shift) {
    perf_latched_ ^= 1u << button;
  } else {
    perf_held_ |= 1u << button;
  }
  apply_perf(ctx);
}

void MainScreen::perf_release(app::Context& ctx, unsigned button) noexcept {
  if (button >= kPerfButtons) {
    return;
  }
  perf_held_ &= ~(1u << button);
  apply_perf(ctx);
}

// What the buttons held or latched say, put on the sequencer's flags, the delay send, the live transpose and the freeze. An action two
// buttons carry is on while either is. The freeze is only written when it changes (a command to the engine each time would be noise).
void MainScreen::apply_perf(app::Context& ctx) noexcept {
  const std::uint8_t on = perf_actions_on(rig_->wiring.perf, perf_held_, perf_latched_);
  const auto is_on = [&](PerfAction a) { return (on & perf_bit(a)) != 0u; };
  seq::Sequencer& sequencer = *rig_->seq;
  sequencer.set_perf_flag(seq::kPerfMute, is_on(PerfAction::Mute));
  sequencer.set_perf_flag(seq::kPerfAccAll, is_on(PerfAction::AccAll));
  sequencer.set_perf_flag(seq::kPerfSlideAll, is_on(PerfAction::SlideAll));
  sequencer.set_fill(is_on(PerfAction::Fill));
  rig_->wiring.throw_on = is_on(PerfAction::Throw);
  controls_->apply_send(ctx.engine());
  const std::uint8_t changed = static_cast<std::uint8_t>(on ^ perf_applied_);
  perf_applied_ = on;
  if ((changed & perf_bit(PerfAction::OctUp)) != 0u) {
    push_live_transpose();
  }
  if ((changed & perf_bit(PerfAction::Freeze)) != 0u) {
    set_freeze(ctx.engine(), *rig_, is_on(PerfAction::Freeze));
  }
  ctx.redraw();
}

// A button the PERF page gave another action lets go of its latch (a latched MUTE must not turn into a latched FREEZE under the hand); a
// held one goes on, now with the new action.
void MainScreen::sync_perf_assignments(app::Context& ctx) noexcept {
  bool changed = false;
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    if (rig_->wiring.perf[b] != perf_seen_[b]) {
      perf_seen_[b] = rig_->wiring.perf[b];
      perf_latched_ &= ~(1u << b);
      changed = true;
    }
  }
  if (changed) {
    apply_perf(ctx);
  }
}

// The live transposition the sequencer plays: the keyboard's (live_trn_) and an octave up while an OCT UP action is on.
void MainScreen::push_live_transpose() noexcept {
  int t = live_trn_ + ((perf_applied_ & perf_bit(PerfAction::OctUp)) != 0u ? kOctave : 0);
  t = t < -kLiveTransposeMax ? -kLiveTransposeMax : (t > kLiveTransposeMax ? kLiveTransposeMax : t);
  rig_->seq->set_live_transpose(t);
}

// ---- Master: the tempo (R7) --------------------------------------------------------------------------------------------------------

void MainScreen::master_press(app::Context& ctx, bool shift) noexcept {
  master_down_ = true;
  master_used_ = false;
  master_shift_ = shift;
  master_down_ms_ = ctx.uptime_ms();
}

void MainScreen::master_release(app::Context& ctx) noexcept {
  if (!master_down_) {
    return;
  }
  master_down_ = false;
  nudge_end(ctx);
  if (!master_used_ && ctx.uptime_ms() - master_down_ms_ < kHoldMs) {  // a tap: TEMPO (Shift: SWING) on the encoder; again, back to the page
    const EncLatch want = master_shift_ ? EncLatch::Swing : EncLatch::Tempo;
    enc_latch_ = enc_latch_ == want ? EncLatch::None : want;
    toast(ctx, enc_latch_ == EncLatch::Tempo ? "ENCODER: TEMPO" : (enc_latch_ == EncLatch::Swing ? "ENCODER: SWING" : "ENCODER: THE PAGE"));
  }
  ctx.redraw();
}

void MainScreen::master_turn(app::Context& ctx, int detents) noexcept {
  master_used_ = true;
  const int next = clamp_nudge(nudge_pct_ + detents);
  if (next == nudge_pct_) {
    return;
  }
  if (nudge_pct_ == 0) {
    nudge_base_ = ctx.engine().tempo();
  }
  nudge_pct_ = next;
  std::uint32_t mbpm = nudged_tempo(nudge_base_, nudge_pct_);
  mbpm = mbpm < dsp::kMinTempoMilliBpm ? static_cast<std::uint32_t>(dsp::kMinTempoMilliBpm)
                                       : (mbpm > dsp::kMaxTempoMilliBpm ? static_cast<std::uint32_t>(dsp::kMaxTempoMilliBpm) : mbpm);
  (void)ctx.engine().set_tempo(mbpm);  // the header shows the tempo, the chip the nudge
  ctx.redraw();
}

// The base tempo back (the release of Master, or the screen going away with it held).
void MainScreen::nudge_end(app::Context& ctx) noexcept {
  if (nudge_pct_ != 0) {
    (void)ctx.engine().set_tempo(nudge_base_);
    nudge_pct_ = 0;
    ctx.redraw();
  }
}

// The encoder while a tap of Master has latched it: the tempo or the swing, as their F keys would (fine, Shift: coarse).
void MainScreen::latch_turn(app::Context& ctx, int detents, bool coarse) noexcept {
  const Entry entry{enc_latch_ == EncLatch::Tempo ? Item::Tempo : Item::Swing, 0, nullptr};
  controls_->step(ctx.engine(), entry, detents, coarse);
  ctx.redraw();
}

// ---- The KEYS ladders (R1) ---------------------------------------------------------------------------------------------------------

// Row 2 is Cutoff, row 3 Reso (Env Mod with Shift): 12 places on the knob, the first pad its minimum and the last its maximum. A tap
// jumps there through a 30 ms slew (so a jump does not click and a p-lock or a motion point on the step wins over it).
void MainScreen::ladder_press(app::Context& ctx, unsigned pad, bool shift) noexcept {
  const unsigned row = app::pad_row(pad);
  const unsigned col = app::pad_col(pad);
  if (row < 2u) {
    return;
  }
  const std::uint8_t id = row == 2u ? static_cast<std::uint8_t>(Tb303::Cutoff) : static_cast<std::uint8_t>(shift ? Tb303::EnvMod : Tb303::Reso);
  const core::ParamDesc* d = ctx.engine().param_desc(rig_->wiring.voice, id);
  if (d == nullptr) {
    return;
  }
  const float value = core::param_denorm(*d, ladder_norm(col));
  (void)rig_->seq->slew(kLaneVoice, id, value, kLadderSlewMs);
  if (recording_motion()) {  // REC or the MOTION latch while the pattern plays: the jump is written under the playhead like a turn
    const seq::Playhead ph = rig_->seq->playhead();
    const int lane = seq::find_lane(rig_->bank->pattern(ph.playing && ph.pattern >= 0 ? static_cast<unsigned>(ph.pattern) : sel()), kLaneVoice, id);
    if (lane >= 0) {
      (void)rig_->seq->record_motion(static_cast<unsigned>(lane), value);
    }
  }
  const int shown = seq::find_lane(pat(), kLaneVoice, id);
  if (shown >= 0) {
    lane_ = static_cast<unsigned>(shown);  // the motion lane under the step view follows the parameter the hand is on
  }
  ctx.redraw();
}

// KEYS held: rows 2 and 3 draw where Cutoff and Reso (Env Mod with Shift) stand, a bar from the first pad to the position, the position
// itself at full level.
void MainScreen::ladder_leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  const bool shift = ctx.panel().shift();
  for (unsigned row = 2; row < app::kPadRows; ++row) {
    const std::uint16_t id = row == 2u ? static_cast<std::uint16_t>(Tb303::Cutoff) : static_cast<std::uint16_t>(shift ? Tb303::EnvMod : Tb303::Reso);
    const core::ParamDesc* d = ctx.engine().param_desc(rig_->wiring.voice, id);
    const unsigned pos = d != nullptr ? ladder_col(core::param_norm(*d, ctx.engine().param(rig_->wiring.voice, id))) : 0u;
    for (unsigned col = 0; col < kLadderSteps; ++col) {
      leds.set(app::pad_at(row, col), col < pos ? app::kLedMid : (col == pos ? app::kLedOn : app::kLedOff));
    }
  }
}

// ---- The scale lock (R8) -----------------------------------------------------------------------------------------------------------

// With a scale chosen on the SEQ page the keyboard keeps to it: the other pads are dark and do nothing. Until the player chooses one
// (the generator's default scale is not a choice) the keyboard is chromatic as it always was; "No scale" and "Chromatic" have every note.
bool MainScreen::scale_locked() const noexcept {
  const Wiring& w = rig_->wiring;
  return w.scale_set && w.gen.scale < o3sdk::core::kScaleCount && o3sdk::core::kScaleDef[w.gen.scale] != 0xFFFu;
}

bool MainScreen::scale_locked_out(int note) const noexcept {
  return scale_locked() && !in_scale(note, rig_->wiring.gen.scale, rig_->wiring.gen.root);
}

// ---- What is latched or locked ------------------------------------------------------------------------------------------------------

// What is latched or locked and has no place of its own on the screen yet, as small chips on the right of the knob strip's tab line (the
// header's strip is full when REC, the chain and the four performance chips are all up): the encoder's TEMPO / SWING latch with its value,
// a tempo nudge, the scale the keyboard is locked to.
void MainScreen::draw_state_chips(app::Context& ctx, ui::Renderer& r, const ui::Rect& strip) noexcept {
  struct Chip {
    char text[32];
    Color fill;
    Color ink;
  };
  Chip chips[3];
  unsigned count = 0;
  if (enc_latch_ != EncLatch::None) {  // the latch, with the value it has
    const bool tempo = enc_latch_ == EncLatch::Tempo;
    const Entry entry{tempo ? Item::Tempo : Item::Swing, 0, nullptr};
    Chip& c = chips[count++];
    c.text[0] = '\0';
    cat(c.text, sizeof c.text, tempo ? "ENCODER: TEMPO " : "ENCODER: SWING ");
    char value[16];
    (void)controls_->format(ctx.engine(), entry, value, sizeof value);
    cat(c.text, sizeof c.text, value);
    if (!tempo) {
      cat(c.text, sizeof c.text, controls_->unit_text(ctx.engine(), entry));  // "5%"; the tempo's unit is in the header
    }
    c.fill = Color::Sel;
    c.ink = Color::SelText;
  }
  if (nudge_pct_ != 0) {
    Chip& c = chips[count++];
    c.text[0] = '\0';
    cat(c.text, sizeof c.text, "NUDGE ");
    const char sign[2] = {nudge_pct_ < 0 ? '-' : '+', '\0'};
    const char digit[3] = {static_cast<char>('0' + (nudge_pct_ < 0 ? -nudge_pct_ : nudge_pct_)), '%', '\0'};
    cat(c.text, sizeof c.text, sign);
    cat(c.text, sizeof c.text, digit);
    c.fill = Color::Accent;
    c.ink = Color::AccentText;
  }
  if (scale_locked()) {
    const Wiring& w = rig_->wiring;
    Chip& c = chips[count++];
    c.text[0] = '\0';
    cat(c.text, sizeof c.text, "SCALE ");
    char name[o3sdk::core::kScaleNameChars + 1];
    unsigned n = 0;
    for (; n < o3sdk::core::kScaleNameChars && o3sdk::core::kScaleNames[w.gen.scale][n] != '\0'; ++n) {
      name[n] = o3sdk::core::kScaleNames[w.gen.scale][n];
    }
    while (n != 0u && name[n - 1u] == ' ') {  // the table pads a name to its field ("Phrygian ")
      --n;
    }
    name[n] = '\0';
    cat(c.text, sizeof c.text, name);
    c.fill = Color::Empty;
    c.ink = Color::Text;
  }
  int x = strip.x + strip.w - 6;
  for (unsigned i = 0; i < count; ++i) {
    const int w = r.text_width(chips[i].text, Face::Micro) + 10;
    x -= w;
    r.panel(ui::Rect{x, strip.y + 1, w, 13}, chips[i].fill);
    r.text(x + 5, strip.y + 3, chips[i].text, chips[i].ink, Face::Micro);
    x -= 6;
  }
}

}  // namespace o3_303
