// SPDX-License-Identifier: GPL-3.0-or-later
//
// The second-hand gestures of the 303 screen that are not performance buttons, the third part of MainScreen:
//
//   SNAP page (R2)    eight snapshots of SOUND + the distortion + the delay + the level and the send on its F keys. A tap recalls, Shift + a key
//                     stores, the encoder morphs from the previous snapshot to the one recalled last. RAM only.
//   RANDOM popup (C3) Shift + F1 opens it: Scale, Density and Acc / Slide % on F1..F3, F8 makes a new line, F7 (or Shift + F1) closes it.
//
// The morph is the sequencer's slew with store = false (sequencer.h): about eighteen targets glide in one event each, and the blend is stored
// in the engine once the slews are over, because the hook puts back the stored value when a lock ends. The pure part (the snapshots, the
// position, the blend) is SnapBank (o3_303/snapshot.h), tested alone.

#include "main_screen.h"

#include "step_view.h"

#include <o3sdk/app/context.h>
#include <o3sdk/dsp/engine.h>

namespace o3_303 {

namespace {

using namespace o3sdk;
using ui::Color;
using ui::Rect;

constexpr float kRecallSlewMs = 30.0f;  // a recall jumps with a short glide (the pad ladders' time), so it does not click
constexpr float kMorphSlewMs = 50.0f;   // an encoder detent of the morph: a few blocks of glide, so a turn is smooth
constexpr std::uint64_t kSnapSettleMs = 40;  // after the slews' time: the blocks the hook needs to reach the goal

// `s` cut to `max` characters into `out` (cap > max); `s` appended to the text in `out`.
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

}  // namespace

// ---- Snapshots (R2) ---------------------------------------------------------------------------------------------------------------

void MainScreen::snap_press(app::Context& ctx, unsigned slot, bool shift) noexcept {
  SnapBank& snaps = rig_->wiring.snaps;
  char text[24] = "SNAP ";
  char num[4];
  view::fmt_uint(slot + 1u, num);
  append(text, sizeof text, num);
  float now[kSnapValues];
  if (shift) {
    snap_finish(ctx, true);  // what is stored is what the morph was heading for, in the engine
    controls_->snap_capture(ctx.engine(), now);
    snaps.store(slot, now);
    append(text, sizeof text, " STORED");
    toast(ctx, text);
    return;
  }
  if (!snaps.used(slot)) {
    append(text, sizeof text, " IS EMPTY");
    toast(ctx, text, true);
    return;
  }
  snap_finish(ctx, true);
  controls_->snap_capture(ctx.engine(), now);
  (void)snaps.recall(slot, now);
  snap_apply(ctx, kRecallSlewMs);
  append(text, sizeof text, " RECALLED");
  toast(ctx, text);
}

void MainScreen::snap_turn(app::Context& ctx, int detents, bool coarse) noexcept {
  SnapBank& snaps = rig_->wiring.snaps;
  if (!snaps.morphing()) {
    toast(ctx, "RECALL A SNAPSHOT FIRST", true);
    return;
  }
  (void)snaps.turn(detents, coarse);
  snap_apply(ctx, kMorphSlewMs);
  ctx.redraw();
}

// The blend at the morph's position on the sequencer's slews: a gliding target (a voice, distortion or delay parameter, the level, the send)
// is `slew(..., store = false)`; a discrete one (a wave, the distortion's model, the delay's mode) is written when the blend changes it.
void MainScreen::snap_apply(app::Context& ctx, float ms) noexcept {
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
void MainScreen::snap_finish(app::Context& ctx, bool at_once) noexcept {
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

// ---- The RANDOM popup (C3) --------------------------------------------------------------------------------------------------------

void MainScreen::random_toggle(app::Context& ctx) noexcept {
  random_open_ = !random_open_;
  enc_latch_ = EncLatch::None;  // the encoder is the popup's
  ctx.redraw();
}

void MainScreen::random_press(app::Context& ctx, unsigned key) noexcept {
  if (key < kRandomEntries) {
    random_focus_ = key;
  } else if (key == 6u) {
    random_open_ = false;
  } else if (key == 7u) {
    random_generate(ctx);
  }
  ctx.redraw();
}

void MainScreen::random_generate(app::Context& ctx) noexcept {
  (void)rig_->bank->random_fill(sel(), rig_->wiring.gen, next_seed(ctx));
  toast(ctx, "RANDOM LINE");
  clamp_cursor();
  view_page_ = cursor_ / 16;
}

void MainScreen::draw_random(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  draw_tabs(ctx, r, area, "RANDOM   F1-F3 SET UP   F8 GENERATE   F7 OR SHIFT + F1 CLOSE");
  view::Knob knobs[view::kKnobs];
  for (unsigned k = 0; k < app::kFKeyCount; ++k) {
    if (k < kRandomEntries) {
      fill_knob(ctx, k, random_entry(k), knobs[k]);
      knobs[k].focus = k == random_focus_;
    } else {
      static constexpr const char* kFKeys[kKeys] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
      knobs[k].fkey = kFKeys[k];
      knobs[k].blank = k < 6u;
      if (k == 6u) {
        knobs[k].label = "CLOSE";
        cut("DONE", 7, knobs[k].value, sizeof knobs[k].value);
      } else if (k == 7u) {
        knobs[k].label = "GENERATE";
        cut("NEW", 7, knobs[k].value, sizeof knobs[k].value);
        knobs[k].norm = 1.0f;
      }
    }
  }
  view::draw_knobs(r, area, knobs);
}

// ---- The SNAP page's strip ---------------------------------------------------------------------------------------------------------

void MainScreen::draw_snap(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  const SnapBank& snaps = rig_->wiring.snaps;
  char hint[64] = "TAP RECALLS   SHIFT + F STORES   ENCODER MORPHS";
  if (snaps.morphing()) {
    char num[4];
    append(hint, sizeof hint, "   FROM ");
    if (snaps.previous() >= 0) {
      view::fmt_uint(static_cast<unsigned>(snaps.previous()) + 1u, num);
      append(hint, sizeof hint, num);
    } else {
      append(hint, sizeof hint, "LIVE");
    }
    append(hint, sizeof hint, " TO ");
    view::fmt_uint(static_cast<unsigned>(snaps.last()) + 1u, num);
    append(hint, sizeof hint, num);
  }
  draw_tabs(ctx, r, area, hint);
  static constexpr const char* kFKeys[kKeys] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
  view::Knob knobs[view::kKnobs];
  for (unsigned k = 0; k < kSnapSlots; ++k) {
    view::Knob& kn = knobs[k];
    kn.fkey = kFKeys[k];
    kn.label = page_at(kPageSnap).entry[k].label;
    const bool used = snaps.used(k);
    kn.norm = used ? 1.0f : -1.0f;
    cut(used ? "SET" : "---", 7, kn.value, sizeof kn.value);
    if (snaps.last() == static_cast<int>(k)) {  // the end of the morph: its value is how far the encoder has taken it
      kn.focus = true;
      kn.norm = static_cast<float>(snaps.position()) / static_cast<float>(kMorphSteps);
      char pct[8];
      const unsigned n = view::fmt_uint(snaps.position() * 100u / kMorphSteps, pct);
      pct[n] = '%';
      pct[n + 1u] = '\0';
      cut(pct, 7, kn.value, sizeof kn.value);
    }
    kn.previous = snaps.previous() == static_cast<int>(k);
  }
  view::draw_knobs(r, area, knobs);
}

}  // namespace o3_303
