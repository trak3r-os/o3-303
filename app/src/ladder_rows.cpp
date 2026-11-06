#include "ladder_rows.h"

#include "live_actions.h"
#include <o3_303/perform.h>
#include <o3sdk/core/param.h>

namespace o3_303 {

namespace o3 = o3sdk;

namespace {

bool same(const LaneRef& a, const LaneRef& b) noexcept {
  return a.target == b.target && a.id == b.id;
}

bool follows(const Wiring& w, unsigned row) noexcept {
  return w.ladder[row].target == kLaneNone;
}

void copy_cut(const char* s, char* out, unsigned cap, unsigned at) noexcept {
  unsigned n = at;
  for (; *s != '\0' && n + 1u < cap; ++s, ++n) {
    out[n] = *s;
  }
  out[n] = '\0';
}

}  // namespace

Entry ladder_entry(const Controls& controls, const Wiring& wiring, unsigned row, const Entry& latched) noexcept {
  if (row >= kLadderRows || follows(wiring, row)) {
    return latched;
  }
  return controls.lane_entry(wiring.ladder[row]);
}

LadderAssign ladder_assign(Wiring& wiring, const Controls& controls, unsigned row, const Entry& knob) noexcept {
  LaneRef ref{kLaneNone, 0};
  if (row >= kLadderRows || !controls.lane_ref(knob, ref)) {
    return LadderAssign::NoLane;
  }
  if (same(wiring.ladder[row], ref)) {
    wiring.ladder[row] = kDefaultLadder[row];
    return LadderAssign::Default;
  }
  wiring.ladder[row] = ref;
  return LadderAssign::Held;
}

void ladder_reset_home(Wiring& wiring) noexcept {
  wiring.ladder[kLadderHome3] = kDefaultLadder[kLadderHome3];
  wiring.ladder[kLadderHome4] = kDefaultLadder[kLadderHome4];
}

void ladder_caption(const o3::dsp::Engine& e, const Controls& controls, const Wiring& wiring, unsigned row, const Entry& latched, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return;
  }
  out[0] = '\0';
  if (row >= kLadderRows) {
    return;
  }
  if (follows(wiring, row)) {
    copy_cut("LATCHED: ", out, cap, 0);
    unsigned n = 0;
    while (out[n] != '\0') {
      ++n;
    }
    copy_cut(controls.label(e, latched), out, cap, n);
    return;
  }
  copy_cut(controls.lane_name(e, wiring.ladder[row]), out, cap, 0);
}

const char* ladder_short_name(const o3::dsp::Engine& e, const Controls& controls, const Wiring& wiring, unsigned row) noexcept {
  if (row >= kLadderRows || follows(wiring, row)) {
    return "LATCHED";
  }
  return controls.lane_name(e, wiring.ladder[row]);
}

LadderTap ladder_tap(o3::app::Context& ctx, Rig& rig, bool rec_motion, unsigned sel, const Entry& entry, unsigned col) noexcept {
  LadderTap r;
  o3::dsp::Engine& e = ctx.engine();
  const o3::core::ParamDesc* d = rig.controls.desc(e, entry);
  if (d == nullptr) {
    return r;
  }
  const float value = o3::core::param_denorm(*d, ladder_norm(col));
  LaneRef ref{kLaneNone, 0};
  const bool lane = rig.controls.lane_ref(entry, ref);
  if (!lane || d->unit == o3::core::ParamUnit::Enum) {
    rig.controls.set(e, entry, value);
  } else {
    (void)rig.seq->slew(ref.target, ref.id, rig.controls.to_lane(entry, value), rig.live->ladder_slew_ms());
  }
  if (!lane) {
    return r;
  }
  const seq::Playhead ph = rig.seq->playhead();
  const unsigned pp = ph.playing && ph.pattern >= 0 ? static_cast<unsigned>(ph.pattern) : sel;
  if (rec_motion) {
    const int ln = rig.bank->claim_lane(pp, ref.target, ref.id);
    if (ln >= 0) {
      (void)rig.seq->record_motion(static_cast<unsigned>(ln), rig.controls.to_lane(entry, value));
    } else {
      r.lanes_full = true;
    }
  }
  r.lane = seq::find_lane(rig.bank->pattern(sel), ref.target, ref.id);
  return r;
}

}  // namespace o3_303
