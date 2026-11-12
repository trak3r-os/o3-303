#include "ladder_rows.h"

#include "live_actions.h"
#include <o3_303/perform.h>
#include <o3sdk/app/pad_leds.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/tract/tract.h>
#include <o3sdk/core/param.h>
#include <o3sdk/units/delay/delay.h>

namespace o3_303 {

namespace o3 = o3sdk;

// kKnobFactory (pages.h) names the units' parameters by number.
static_assert(AcidVoice::Cutoff == 0 && AcidVoice::Reso == 1 && AcidVoice::EnvMod == 2 && AcidVoice::Decay == 3 && AcidVoice::AccDecay == 8 && AcidVoice::Slide == 10 &&
                  AcidVoice::FilterFm == 13 && AcidVoice::Overdrive == 17,
              "kKnobFactory's voice ids");
static_assert(Growl::Vowel == 0 && Growl::FormantQ == 1 && Growl::CombTune == 3 && Growl::Mix == 7, "kKnobFactory's growl ids");
static_assert(TractFx::Vowel == 0 && TractFx::Tongue == 1 && TractFx::Lips == 2 && TractFx::Mix == 6, "kKnobFactory's tract ids");
static_assert(Distortion::Drive == 2 && o3sdk::units::Delay::Time == 0 && o3sdk::units::Delay::Feedback == 1, "kKnobFactory's distortion and delay ids");

namespace {

bool same(const LaneRef& a, const LaneRef& b) noexcept {
  return a.target == b.target && a.id == b.id;
}

void copy_cut(const char* s, char* out, unsigned cap, unsigned at) noexcept {
  unsigned n = at;
  for (; *s != '\0' && n + 1u < cap; ++s, ++n) {
    out[n] = *s;
  }
  out[n] = '\0';
}

}  // namespace

bool ladder_is_list(const o3::core::ParamDesc& d) noexcept {
  if (d.unit == o3::core::ParamUnit::Enum) {
    return d.list_count >= 1u && d.list_count <= kLadderSteps;
  }
  if (d.unit != o3::core::ParamUnit::Int || d.curve != o3::core::ParamCurve::Linear) {
    return false;
  }
  const float span = d.max - d.min;
  return span >= 1.0f && span < static_cast<float>(kLadderSteps) && span == static_cast<float>(static_cast<unsigned>(span)) && d.min == static_cast<float>(static_cast<int>(d.min));
}

namespace {

unsigned list_count(const o3::core::ParamDesc& d) noexcept {
  return d.unit == o3::core::ParamUnit::Enum ? d.list_count : static_cast<unsigned>(d.max - d.min) + 1u;
}

}  // namespace

unsigned ladder_places(const o3::core::ParamDesc& d) noexcept {
  return ladder_is_list(d) ? list_count(d) : kLadderSteps;
}

float ladder_value(const o3::core::ParamDesc& d, unsigned col, const KnobSpan* span) noexcept {
  if (!ladder_is_list(d)) {
    if (span != nullptr) {
      const float n0 = o3::core::param_norm(d, span->lo);
      const float n1 = o3::core::param_norm(d, span->hi);
      return o3::core::param_denorm(d, n0 + ladder_norm(col) * (n1 - n0));
    }
    return o3::core::param_denorm(d, ladder_norm(col));
  }
  const unsigned n = list_count(d);
  return d.min + static_cast<float>(col < n ? col : n - 1u);
}

unsigned ladder_pos(const o3::core::ParamDesc& d, float value, const KnobSpan* span) noexcept {
  if (!ladder_is_list(d)) {
    if (span != nullptr) {
      const float n0 = o3::core::param_norm(d, span->lo);
      const float n1 = o3::core::param_norm(d, span->hi);
      return ladder_col((o3::core::param_norm(d, value) - n0) / (n1 - n0));
    }
    return ladder_col(o3::core::param_norm(d, value));
  }
  const float at = value - d.min + 0.5f;
  if (!(at > 0.0f)) {
    return 0;
  }
  const unsigned n = list_count(d);
  const unsigned i = at >= static_cast<float>(n) ? n : static_cast<unsigned>(at);
  return i < n ? i : n - 1u;
}

void ladder_levels(const o3::core::ParamDesc* d, float value, bool blink, std::uint8_t (&out)[kLadderSteps], const KnobSpan* span) noexcept {
  const unsigned places = d != nullptr ? ladder_places(*d) : 0u;
  const bool list = d != nullptr && ladder_is_list(*d);
  const unsigned pos = d != nullptr ? ladder_pos(*d, value, span) : 0u;
  for (unsigned col = 0; col < kLadderSteps; ++col) {
    if (blink) {
      out[col] = o3::app::kLedOn;
    } else if (d == nullptr) {
      out[col] = o3::app::kLedOff;
    } else if (col == pos) {
      out[col] = o3::app::kLedOn;
    } else if (list) {
      out[col] = col < places ? o3::app::kLedDim : o3::app::kLedOff;
    } else {
      out[col] = col < pos ? o3::app::kLedMid : o3::app::kLedOff;
    }
  }
}

Entry ladder_entry(const Controls& controls, const Wiring& wiring, unsigned row) noexcept {
  if (row >= kLadderRows) {
    return Entry{};
  }
  return controls.row_entry(wiring.ladder[row]);
}

unsigned knob_row_pos(unsigned row) noexcept {
  for (unsigned i = 0; i < kKnobsRows; ++i) {
    if (kKnobsRowId[i] == row) {
      return i;
    }
  }
  return kKnobsRows;
}

LaneRef knob_factory(unsigned set, unsigned pos) noexcept {
  if (set >= kKnobSets || pos >= kKnobsPerSet) {
    return LaneRef{kLaneNone, 0};
  }
  return kKnobFactory.row[set][pos];
}

namespace {

// A row's knob changed: the stored set follows (the live copy and the set in use are the same four rows).
void store_row(Wiring& wiring, unsigned row) noexcept {
  const unsigned pos = knob_row_pos(row);
  if (pos < kKnobsRows) {
    wiring.knob_sets.row[wiring.knob_set < kKnobSets ? wiring.knob_set : 0u][pos] = wiring.ladder[row];
  }
}

}  // namespace

LadderAssign ladder_assign(Wiring& wiring, const Controls& controls, unsigned row, const Entry& knob) noexcept {
  LaneRef ref{kLaneNone, 0};
  if (row >= kLadderRows || !controls.row_ref(knob, ref)) {
    return LadderAssign::NoLane;
  }
  if (same(wiring.ladder[row], ref)) {
    wiring.ladder[row] = knob_factory(wiring.knob_set, knob_row_pos(row));
    store_row(wiring, row);
    return LadderAssign::Default;
  }
  wiring.ladder[row] = ref;
  store_row(wiring, row);
  return LadderAssign::Held;
}

void knob_set_use(Wiring& wiring, unsigned set) noexcept {
  wiring.knob_set = static_cast<std::uint8_t>(set < kKnobSets ? set : 0u);
  for (unsigned i = 0; i < kKnobsRows; ++i) {
    wiring.ladder[kKnobsRowId[i]] = wiring.knob_sets.row[wiring.knob_set][i];
  }
}

void knob_set_factory(Wiring& wiring, unsigned set) noexcept {
  if (set >= kKnobSets) {
    return;
  }
  for (unsigned i = 0; i < kKnobsRows; ++i) {
    wiring.knob_sets.row[set][i] = kKnobFactory.row[set][i];
  }
  if (set == wiring.knob_set) {
    knob_set_use(wiring, set);
  }
}

const char* knob_set_name(unsigned set) noexcept {
  static constexpr const char* kNames[kKnobSets] = {"FILTER", "MODDED", "GROWL", "WOBBLE", "TRACT"};
  return set < kKnobSets ? kNames[set] : "";
}

bool knob_set_is_factory(const Wiring& wiring, unsigned set) noexcept {
  if (set >= kKnobSets) {
    return false;
  }
  for (unsigned i = 0; i < kKnobsPerSet; ++i) {
    if (!same(wiring.knob_sets.row[set][i], kKnobFactory.row[set][i])) {
      return false;
    }
  }
  return true;
}

void ladder_reset_home(Wiring& wiring) noexcept {
  wiring.ladder[kLadderHome3] = knob_factory(wiring.knob_set, knob_row_pos(kLadderHome3));
  wiring.ladder[kLadderHome4] = knob_factory(wiring.knob_set, knob_row_pos(kLadderHome4));
  store_row(wiring, kLadderHome3);
  store_row(wiring, kLadderHome4);
}

namespace {

constexpr unsigned kWalkPageCount = 7;  // WOBBLE is the last (KN4; TRACT was before it, KN0): the walk's list only grows at its end
constexpr unsigned kWalkMax = 96;       // the list's most places the family jump looks at (the pages hold under 50 knobs)

const Page& walk_page(unsigned i) noexcept {
  switch (i) {
    case 0: return page_at(kPageSound);
    case 1: return kFxPage;
    case 2: return kNotesPage;
    case 3: return kTonePage;
    case 4: return kGrowlPage;
    case 5: return kTractPage;
    default: return kWobblePage;
  }
}

// The knob a lane can drive at place `index` of the walk (the entry of a page's key), or a blank past the end; `count` (if not null) gets the length. A knob that two pages show is the first one's.
Entry walk_at(const Controls& controls, unsigned index, unsigned* count, unsigned* page = nullptr) noexcept {
  Entry found;
  unsigned n = 0;
  for (unsigned p = 0; p < kWalkPageCount; ++p) {
    for (const Entry& en : walk_page(p).entry) {
      LaneRef ref{kLaneNone, 0};
      if (en.item == Item::Blank || !controls.row_ref(en, ref)) {
        continue;
      }
      bool seen = false;  // the same lane target on an earlier place
      for (unsigned q = 0; q < p && !seen; ++q) {
        for (const Entry& prev : walk_page(q).entry) {
          LaneRef pr{kLaneNone, 0};
          seen = seen || (prev.item != Item::Blank && controls.row_ref(prev, pr) && same(pr, ref));
        }
      }
      if (seen) {
        continue;
      }
      if (n == index) {
        found = en;
        if (page != nullptr) {
          *page = p;
        }
      }
      ++n;
    }
  }
  if (count != nullptr) {
    *count = n;
  }
  return found;
}

}  // namespace

unsigned ladder_knob_count(const Controls& controls) noexcept {
  unsigned n = 0;
  (void)walk_at(controls, 0xFFFFu, &n);
  return n;
}

Entry ladder_knob_at(const Controls& controls, unsigned index) noexcept { return walk_at(controls, index, nullptr); }

Entry ladder_walk(Wiring& wiring, const Controls& controls, unsigned row, int delta, const char** family) noexcept {
  const unsigned n = ladder_knob_count(controls);
  if (row >= kLadderRows || n == 0u) {
    return Entry{};
  }
  int at = -1;  // where the row's knob is in the list
  for (unsigned i = 0; i < n && at < 0; ++i) {
    LaneRef ref{kLaneNone, 0};
    if (controls.row_ref(ladder_knob_at(controls, i), ref) && same(ref, wiring.ladder[row])) {
      at = static_cast<int>(i);
    }
  }
  const int count = static_cast<int>(n);
  int next = at < 0 ? (delta < 0 ? count - 1 : 0) : at + delta;
  next = ((next % count) + count) % count;
  unsigned fam = 0;
  const Entry en = walk_at(controls, static_cast<unsigned>(next), nullptr, &fam);
  LaneRef ref{kLaneNone, 0};
  if (controls.row_ref(en, ref)) {
    wiring.ladder[row] = ref;
    store_row(wiring, row);
  }
  if (family != nullptr) {
    *family = walk_page(fam).name;
  }
  return en;
}

Entry ladder_walk_family(Wiring& wiring, const Controls& controls, unsigned row, int delta, const char** family) noexcept {
  const unsigned n = ladder_knob_count(controls);
  if (row >= kLadderRows || n == 0u) {
    return Entry{};
  }
  unsigned fam[kWalkMax] = {};  // the family (walk page) of each place of the list
  const unsigned count = n < kWalkMax ? n : kWalkMax;
  int at = -1;
  for (unsigned i = 0; i < count; ++i) {
    (void)walk_at(controls, i, nullptr, &fam[i]);
    LaneRef ref{kLaneNone, 0};
    if (at < 0 && controls.row_ref(ladder_knob_at(controls, i), ref) && same(ref, wiring.ladder[row])) {
      at = static_cast<int>(i);
    }
  }
  // The family to go to: the next (or previous) one that has a knob; from a knob the list does not hold, the first (or last) family.
  const int pages = static_cast<int>(kWalkPageCount);
  int want = at < 0 ? (delta < 0 ? pages : -1) : static_cast<int>(fam[at]);
  int target = -1;
  for (int step = 0; step < pages && target < 0; ++step) {
    want = ((want + (delta < 0 ? -1 : 1)) % pages + pages) % pages;
    for (unsigned i = 0; i < count && target < 0; ++i) {
      if (static_cast<int>(fam[i]) == want) {
        target = static_cast<int>(i);
      }
    }
  }
  if (target < 0) {
    return Entry{};
  }
  const Entry en = ladder_knob_at(controls, static_cast<unsigned>(target));
  LaneRef ref{kLaneNone, 0};
  if (controls.row_ref(en, ref)) {
    wiring.ladder[row] = ref;
    store_row(wiring, row);
  }
  if (family != nullptr) {
    *family = walk_page(fam[target]).name;
  }
  return en;
}

void ladder_caption(const o3::dsp::Engine& e, const Controls& controls, const Wiring& wiring, unsigned row, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return;
  }
  out[0] = '\0';
  if (row >= kLadderRows) {
    return;
  }
  copy_cut(controls.row_name(e, wiring.ladder[row]), out, cap, 0);
}

const char* ladder_short_name(const o3::dsp::Engine& e, const Controls& controls, const Wiring& wiring, unsigned row) noexcept {
  if (row >= kLadderRows) {
    return "";
  }
  return controls.row_name(e, wiring.ladder[row]);
}

LadderTap ladder_tap(o3::app::Context& ctx, Rig& rig, bool rec_motion, unsigned sel, const Entry& entry, unsigned col) noexcept {
  LadderTap r;
  o3::dsp::Engine& e = ctx.engine();
  const o3::core::ParamDesc* d = rig.controls.desc(e, entry);
  if (d == nullptr) {
    return r;
  }
  if (col >= ladder_places(*d)) {  // a list's dark pads
    return r;
  }
  KnobSpan span;
  const float value = ladder_value(*d, col, rig.controls.live_span(e, entry, span) ? &span : nullptr);
  if (rig.wiring.stock && is_mod(entry)) {  // a hand on a mod hears it (the encoder's rule, MODS-PLAN 5.3); the motion it records, played later, does not
    toggle_stock(rig, ctx);
  }
  LaneRef ref{kLaneNone, 0};
  const bool lane = rig.controls.lane_ref(entry, ref);
  if (!lane || d->unit == o3::core::ParamUnit::Enum) {
    rig.controls.set(e, entry, value);
  } else {
    (void)rig.seq->slew(ref.target, ref.id, rig.controls.to_lane(entry, value), rig.live->ladder_slew_ms());
  }
  if (!lane) {
    r.not_recorded = rec_motion;  // KN4: a row-only knob (the wobble, a switch) has no lane: the value is set, nothing is written
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
