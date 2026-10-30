// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Master gestures of the 303 screen (docs/M3B-PLAN.md R7), the second half of MainScreen:
//
//   tap Master                the encoder latches TEMPO (Shift: SWING), whatever page the F keys are on (R7)
//   hold Master + turn        a tempo nudge of 1 % a detent, at most 3 %, back at the release (R7)
//
// and the chips of what is latched or locked. The four assignable buttons (Note / Instr / Vol / Fx: held = momentary, Shift + tap latches, C4) are
// LiveActions' (live_actions.h), shared with PERFORM; the keyboard, the FILT ladders and the scale lock are the home map's (home_map.h). The pure parts
// (the nudge, the scale) are in o3_303/perform.h and tested alone; what is here puts them on the engine and the screen.

#include "main_screen.h"

#include <o3sdk/app/context.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>

namespace o3_303 {

namespace {

using namespace o3sdk;
using ui::Color;
using ui::Face;

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
    toast(ctx, enc_latch_ == EncLatch::Tempo ? "ENCODER: TEMPO" : (enc_latch_ == EncLatch::Swing ? "ENCODER: SWING" : "ENCODER: THE PAGE"),
          enc_latch_ == EncLatch::None);
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
    nudge_base_ = ctx.engine().own_tempo();  // not the MIDI master's: the release puts this back as the own tempo
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

// ---- What is latched or locked ------------------------------------------------------------------------------------------------------

// What is latched or locked and has no place of its own on the screen yet, as small chips on the right of the knob strip's tab line (the
// header's strip is full when REC, the chain and the four performance chips are all up): the encoder's TEMPO / SWING latch with its value,
// a tempo nudge (the scale the keyboard is locked to is on the info line).
void MainScreen::draw_state_chips(app::Context& ctx, ui::Renderer& r, const ui::Rect& strip) noexcept {
  struct Chip {
    char text[32];
    Color fill;
    Color ink;
  };
  Chip chips[2];
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
