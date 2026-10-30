// SPDX-License-Identifier: GPL-3.0-or-later
//
// The live actions and the snapshots, moved out of MainScreen (main_screen_perf.cpp and main_screen_snap.cpp held them as its second and third
// part) so that PERFORM runs the same state. The pure parts (which actions are on, the freeze's owner, the snapshots' blend) are in
// o3_303/perform.h and o3_303/snapshot.h and tested alone; what is here puts them on the engine and the sequencer.

#include "live_actions.h"

#include "step_view.h"

#include <o3sdk/dsp/engine.h>
#include <o3_303/units/freeze/freeze.h>

namespace o3_303 {

namespace {

using namespace o3sdk;

constexpr int kLiveTransposeMax = 48;  // the sequencer's limit
constexpr int kOctave = 12;
constexpr float kRecallSlewMs = 30.0f;  // a recall jumps with a short glide (the pad ladders' time), so it does not click
constexpr float kMorphSlewMs = 50.0f;   // an encoder detent of the morph: a few blocks of glide, so a turn is smooth
constexpr std::uint64_t kSnapSettleMs = 40;  // after the slews' time: the blocks the hook needs to reach the goal

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

}  // namespace

void LiveActions::init(Rig& rig) noexcept {
  rig_ = &rig;
  controls_ = &rig.controls;
  button_held_ = 0;
  button_latched_ = 0;
  pad_held_ = 0;
  pad_latched_ = 0;
  applied_ = 0;
  freeze_owner_.reset();
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    seen_[b] = rig.wiring.perf[b];
  }
  keys_live_ = false;
  live_trn_ = 0;
  was_playing_ = false;
  seed_ = 0x303u;
  snap_pending_ = false;
  snap_deadline_ms_ = 0;
  rig.wiring.snaps.clear();
}

void LiveActions::update(app::Context& ctx) noexcept {
  const bool playing = ctx.engine().playing();
  if (was_playing_ && !playing) {
    live_trn_ = 0;  // the keyboard's transposition ends with the run
    push_live_transpose();
  }
  was_playing_ = playing;
  if (snap_pending_ && ctx.uptime_ms() >= snap_deadline_ms_) {
    snap_finish(ctx, false);
  }
  sync_assignments(ctx);
}

void LiveActions::leave(app::Context& ctx) noexcept {
  button_held_ = 0;
  pad_held_ = 0;
  apply(ctx);
}

// A seed that differs from press to press and from boot to boot: the sequence of the last one and the time of this press.
std::uint32_t LiveActions::next_seed(app::Context& ctx) noexcept {
  seed_ = seed_ * 1664525u + 1013904223u + static_cast<std::uint32_t>(ctx.uptime_ms()) * 2654435761u;
  return seed_;
}

// ---- The four colour buttons (C4) -----------------------------------------------------------------------------------------------

void LiveActions::button_press(app::Context& ctx, unsigned button, bool shift) noexcept {
  if (button >= kPerfButtons) {
    return;
  }
  if (shift) {
    // Shift + a button whose action is latched on a PERFORM pad lets the action go (as that pad would), whatever screen this is; else it
    // latches or unlatches the button itself.
    const unsigned a = rig_->wiring.perf[button];
    if (a < kPerfActionCount && (pad_latched_ & perf_bit(static_cast<PerfAction>(a))) != 0u) {
      unlatch(static_cast<PerfAction>(a));
    } else {
      button_latched_ ^= 1u << button;
    }
  } else {
    button_held_ |= 1u << button;
  }
  apply(ctx);
}

void LiveActions::button_release(app::Context& ctx, unsigned button) noexcept {
  if (button >= kPerfButtons) {
    return;
  }
  button_held_ &= ~(1u << button);
  apply(ctx);
}

// ---- PERFORM's pads -------------------------------------------------------------------------------------------------------------

bool LiveActions::latched(PerfAction a) const noexcept {
  if ((pad_latched_ & perf_bit(a)) != 0u) {
    return true;
  }
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    if ((button_latched_ >> b & 1u) != 0u && rig_->wiring.perf[b] == static_cast<std::uint8_t>(a)) {
      return true;
    }
  }
  return false;
}

void LiveActions::unlatch(PerfAction a) noexcept {
  pad_latched_ = static_cast<std::uint16_t>(pad_latched_ & ~perf_bit(a));
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    if (rig_->wiring.perf[b] == static_cast<std::uint8_t>(a)) {
      button_latched_ &= ~(1u << b);
    }
  }
}

void LiveActions::action_press(app::Context& ctx, PerfAction a, bool shift) noexcept {
  if (static_cast<unsigned>(a) >= kPerfActionCount) {
    return;
  }
  if (latched(a)) {  // a latched action's pad, tapped again (with Shift or without), lets it go
    unlatch(a);
  } else if (shift) {
    pad_latched_ = static_cast<std::uint16_t>(pad_latched_ | perf_bit(a));
  } else {
    pad_held_ = static_cast<std::uint16_t>(pad_held_ | perf_bit(a));
  }
  apply(ctx);
}

void LiveActions::action_release(app::Context& ctx, PerfAction a) noexcept {
  if (static_cast<unsigned>(a) >= kPerfActionCount) {
    return;
  }
  pad_held_ = static_cast<std::uint16_t>(pad_held_ & ~perf_bit(a));
  apply(ctx);
}

void LiveActions::all_off(app::Context& ctx) noexcept {
  pad_latched_ = 0;
  button_latched_ = 0;
  rig_->seq->wobble_rate_override(seq::kWobbleRateNone);
  apply(ctx);
}

void LiveActions::wobble_rate(app::Context& ctx, int rate) noexcept {
  if (rate >= 0) {
    rig_->seq->wobble_rate_override(rate);
  }
  ctx.redraw();
}

// What the buttons and pads held or latched say, put on the sequencer's flags, the delay send, the live transpose and the freeze. An action two
// of them carry is on while either is. The freeze is only written when it changes (a command to the engine each time would be noise).
void LiveActions::apply(app::Context& ctx) noexcept {
  const std::uint16_t on = live_actions_on(rig_->wiring.perf, button_held_, button_latched_, pad_held_, pad_latched_);
  const auto is_set = [&](PerfAction a) { return (on & perf_bit(a)) != 0u; };
  seq::Sequencer& sequencer = *rig_->seq;
  sequencer.set_perf_flag(seq::kPerfMute, is_set(PerfAction::Mute));
  sequencer.set_perf_flag(seq::kPerfAccAll, is_set(PerfAction::AccAll));
  sequencer.set_perf_flag(seq::kPerfSlideAll, is_set(PerfAction::SlideAll));
  sequencer.set_fill(is_set(PerfAction::Fill));
  rig_->wiring.throw_on = is_set(PerfAction::Throw);
  controls_->apply_send(ctx.engine());
  const std::uint16_t changed = static_cast<std::uint16_t>(on ^ applied_);
  applied_ = on;
  if ((changed & perf_bit(PerfAction::OctUp)) != 0u) {
    push_live_transpose();
  }
  // FREEZE, ROLL, STOP and REV are the modes of the one freeze unit: the one that went on first has it until it goes off (perform.h FreezeOwner). It
  // writes the Mode, then Active; the release writes Active only and leaves the Mode: nothing touches the unit while it leaves, so nothing can click,
  // and the next entry writes its own Mode first. FREEZE alone is what it was (its Mode write is FREEZE, the unit's default).
  const FreezeCmd fc = freeze_owner_.step(on, changed);
  if (fc.set_mode) {
    set_freeze_mode(ctx.engine(), *rig_, static_cast<Freeze::ModeValue>(fc.mode));
  }
  if (fc.set_active) {
    set_freeze(ctx.engine(), *rig_, fc.active);
  }
  // WOBBLE: the LFO runs while the action is on, at the WOBBLE page's settings; a pad picks its rate; the release switches it off and gives the
  // pattern's rate back.
  if ((changed & perf_bit(PerfAction::Wobble)) != 0u) {
    controls_->apply_wobble(is_set(PerfAction::Wobble));
    if (!is_set(PerfAction::Wobble)) {
      sequencer.wobble_rate_override(seq::kWobbleRateNone);
    }
  }
  ctx.redraw();
}

// A button the PERF page gave another action lets go of its latch (a latched MUTE must not turn into a latched FREEZE under the hand); a
// held one goes on, now with the new action.
void LiveActions::sync_assignments(app::Context& ctx) noexcept {
  bool changed = false;
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    if (rig_->wiring.perf[b] != seen_[b]) {
      seen_[b] = rig_->wiring.perf[b];
      button_latched_ &= ~(1u << b);
      changed = true;
    }
  }
  if (changed) {
    apply(ctx);
  }
}

// ---- The keyboard's transposition -----------------------------------------------------------------------------------------------

void LiveActions::set_live_transpose(int semitones) noexcept {
  live_trn_ = semitones;
  push_live_transpose();
}

// The live transposition the sequencer plays: the keyboard's (live_trn_) and an octave up while an OCT UP action is on.
void LiveActions::push_live_transpose() noexcept {
  int t = live_trn_ + (is_on(PerfAction::OctUp) ? kOctave : 0);
  t = t < -kLiveTransposeMax ? -kLiveTransposeMax : (t > kLiveTransposeMax ? kLiveTransposeMax : t);
  rig_->seq->set_live_transpose(t);
}

// ---- Snapshots (R2) -------------------------------------------------------------------------------------------------------------

void LiveActions::snap_press(app::Context& ctx, unsigned slot, bool shift, Message& msg) noexcept {
  SnapBank& snaps = rig_->wiring.snaps;
  msg.off = false;
  msg.text[0] = 'S';
  msg.text[1] = 'N';
  msg.text[2] = 'A';
  msg.text[3] = 'P';
  msg.text[4] = ' ';
  msg.text[5] = '\0';
  char num[4];
  view::fmt_uint(slot + 1u, num);
  append(msg.text, sizeof msg.text, num);
  float now[kSnapValues];
  if (shift) {
    snap_finish(ctx, true);  // what is stored is what the morph was heading for, in the engine
    controls_->snap_capture(ctx.engine(), now);
    snaps.store(slot, now);
    append(msg.text, sizeof msg.text, " STORED");
    return;
  }
  if (!snaps.used(slot)) {
    append(msg.text, sizeof msg.text, " IS EMPTY");
    msg.off = true;
    return;
  }
  snap_finish(ctx, true);
  controls_->snap_capture(ctx.engine(), now);
  (void)snaps.recall(slot, now);
  snap_apply(ctx, kRecallSlewMs);
  append(msg.text, sizeof msg.text, " RECALLED");
}

bool LiveActions::snap_turn(app::Context& ctx, int detents, bool coarse, Message& msg) noexcept {
  SnapBank& snaps = rig_->wiring.snaps;
  if (!snaps.morphing()) {
    msg.text[0] = '\0';
    append(msg.text, sizeof msg.text, "RECALL A SNAPSHOT FIRST");
    msg.off = true;
    return false;
  }
  (void)snaps.turn(detents, coarse);
  snap_apply(ctx, kMorphSlewMs);
  ctx.redraw();
  return true;
}

// The blend at the morph's position on the sequencer's slews: a gliding target (a voice, distortion or delay parameter, the level, the send)
// is `slew(..., store = false)`; a discrete one (a wave, the distortion's model, the delay's mode) is written when the blend changes it.
void LiveActions::snap_apply(app::Context& ctx, float ms) noexcept {
  dsp::Engine& e = ctx.engine();
  const SnapBank& snaps = rig_->wiring.snaps;
  for (unsigned i = 0; i < kSnapTargetCount; ++i) {
    const Entry& en = snap_target(i);
    LaneRef ref{kLaneNone, 0};
    if (!controls_->lane_ref(en, ref)) {
      continue;
    }
    const bool discrete = controls_->snap_discrete(e, i);
    const float v = snaps.value(i, discrete);
    if (discrete) {
      if (controls_->get(e, en) != v) {
        (void)rig_->seq->slew_stop(ref.target, ref.id);
        controls_->set(e, en, v);
      }
    } else {
      (void)rig_->seq->slew(ref.target, ref.id, controls_->to_lane(en, v), ms, false);
    }
  }
  snap_pending_ = true;
  snap_deadline_ms_ = ctx.uptime_ms() + static_cast<std::uint32_t>(ms) + kSnapSettleMs;  // (a float to 64 bits would pull in soft doubles)
}

// The morph's blend is stored in the engine (what the pages show, what the next lock gives back): once the slews are over, or at once (the
// slews are stopped where they are and the targets set to where they were going).
void LiveActions::snap_finish(app::Context& ctx, bool at_once) noexcept {
  if (!snap_pending_) {
    return;
  }
  snap_pending_ = false;
  dsp::Engine& e = ctx.engine();
  const SnapBank& snaps = rig_->wiring.snaps;
  for (unsigned i = 0; i < kSnapTargetCount; ++i) {
    const Entry& en = snap_target(i);
    LaneRef ref{kLaneNone, 0};
    if (!controls_->lane_ref(en, ref) || controls_->snap_discrete(e, i)) {
      continue;
    }
    if (at_once) {
      (void)rig_->seq->slew_stop(ref.target, ref.id);
    }
    controls_->set(e, en, snaps.value(i, false));
  }
  ctx.redraw();
}

}  // namespace o3_303
