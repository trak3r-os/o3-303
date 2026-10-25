// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/seq/ops.h>

#include <o3sdk/core/scales.h>
#include <o3_303/seq/rng.h>

namespace o3_303::seq {

namespace {

constexpr std::uint8_t kFlagMask = kStepAccent | kStepSlide | kStepTie | kStepRest;
constexpr unsigned kPointsPerLane = kMaxSteps * kMotionPointsPerStep;

// The length as the operations use it: one of the legal lengths, whatever the data says.
unsigned length_of(const Pattern& p) noexcept { return valid_length(p.length) ? p.length : kDefaultLength; }

unsigned clamp_step(unsigned step) noexcept { return step < kMaxSteps ? step : kMaxSteps - 1u; }

bool is_playing(const Step& s) noexcept { return (s.flags & (kStepRest | kStepTie)) == 0u; }

template <typename T>
void reverse_range(T* a, unsigned n) noexcept {
  for (unsigned i = 0, j = n; i + 1u < j; ++i, --j) {
    const T t = a[i];
    a[i] = a[j - 1u];
    a[j - 1u] = t;
  }
}

// Right rotation by r (0 < r < n) as three reversals: no temporary buffer.
template <typename T>
void rotate_right(T* a, unsigned n, unsigned r) noexcept {
  reverse_range(a, n);
  reverse_range(a, r);
  reverse_range(a + r, n - r);
}

void clear_points(Pattern& p, unsigned step) noexcept {
  for (auto& lane : p.motion) {
    for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
      lane[step * kMotionPointsPerStep + q] = kNoLock;
    }
  }
}

void copy_points(Pattern& p, unsigned to, unsigned from) noexcept {
  for (auto& lane : p.motion) {
    for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
      lane[to * kMotionPointsPerStep + q] = lane[from * kMotionPointsPerStep + q];
    }
  }
}

void make_rest(Pattern& p, unsigned step) noexcept {
  p.steps[step].note = kDefaultNote;
  p.steps[step].flags = kStepRest;
  clear_points(p, step);
}

// The notes the generators may pick: the range [low, high] filtered by the scale (the range itself when the scale leaves nothing).
unsigned scale_notes(const GenParams& g, std::uint8_t* out) noexcept {
  unsigned low = g.low;
  unsigned high = g.high > kMaxNote ? kMaxNote : g.high;
  if (low > high) {
    const unsigned t = low;
    low = high;
    high = t;
  }
  const unsigned scale = g.scale < o3sdk::core::kScaleCount ? g.scale : 0u;
  const unsigned mask = o3sdk::core::kScaleDef[scale];
  const unsigned root = g.root % 12u;
  unsigned n = 0;
  for (unsigned note = low; note <= high; ++note) {
    if (((mask >> ((note + 12u - root) % 12u)) & 1u) != 0u) {
      out[n++] = static_cast<std::uint8_t>(note);
    }
  }
  if (n == 0u) {
    out[n++] = static_cast<std::uint8_t>(low);
  }
  return n;
}

float percent(unsigned v) noexcept { return static_cast<float>(v > 100u ? 100u : v) * 0.01f; }

}  // namespace

// ---- One step ---------------------------------------------------------------------------------------------------------------

void set_note(Pattern& p, unsigned step, unsigned note) noexcept {
  p.steps[clamp_step(step)].note = static_cast<std::uint8_t>(note > kMaxNote ? kMaxNote : note);
}

void set_flag(Pattern& p, unsigned step, std::uint8_t flag, bool on) noexcept {
  std::uint8_t& f = p.steps[clamp_step(step)].flags;
  flag &= kFlagMask;
  if (!on) {
    f = static_cast<std::uint8_t>(f & ~flag);
    return;
  }
  f = static_cast<std::uint8_t>(f | flag);
  if ((flag & kStepRest) != 0u) {
    f = static_cast<std::uint8_t>(f & ~kStepTie);
  } else if ((flag & kStepTie) != 0u) {
    f = static_cast<std::uint8_t>(f & ~kStepRest);
  }
}

bool has_flag(const Pattern& p, unsigned step, std::uint8_t flag) noexcept {
  return (p.steps[clamp_step(step)].flags & flag) != 0u;
}

void toggle_flag(Pattern& p, unsigned step, std::uint8_t flag) noexcept {
  set_flag(p, step, flag, !has_flag(p, step, flag));
}

unsigned write_advance(Pattern& p, unsigned step, unsigned note, std::uint8_t flags) noexcept {
  const unsigned s = clamp_step(step);
  flags &= kFlagMask;
  if ((flags & kStepRest) != 0u) {
    flags = kStepRest;  // a rest has no other attribute, and rest beats tie
  }
  if ((flags & (kStepRest | kStepTie)) == 0u) {
    set_note(p, s, note);
  }
  p.steps[s].flags = flags;
  const unsigned next = s + 1u;
  return next >= length_of(p) ? 0u : next;
}

void insert_step(Pattern& p, unsigned at) noexcept {
  const unsigned n = length_of(p);
  if (at >= n) {
    return;
  }
  for (unsigned j = n - 1u; j > at; --j) {
    p.steps[j] = p.steps[j - 1u];
    copy_points(p, j, j - 1u);
  }
  make_rest(p, at);
}

void delete_step(Pattern& p, unsigned at) noexcept {
  const unsigned n = length_of(p);
  if (at >= n) {
    return;
  }
  for (unsigned j = at; j + 1u < n; ++j) {
    p.steps[j] = p.steps[j + 1u];
    copy_points(p, j, j + 1u);
  }
  make_rest(p, n - 1u);
}

StepClip copy_step(const Pattern& p, unsigned step) noexcept {
  const unsigned s = clamp_step(step);
  StepClip c{};
  c.step = p.steps[s];
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
      c.motion[lane][q] = p.motion[lane][s * kMotionPointsPerStep + q];
    }
  }
  return c;
}

void paste_step(Pattern& p, unsigned step, const StepClip& clip) noexcept {
  const unsigned s = clamp_step(step);
  p.steps[s] = clip.step;
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
      p.motion[lane][s * kMotionPointsPerStep + q] = clip.motion[lane][q];
    }
  }
}

// ---- The pattern ------------------------------------------------------------------------------------------------------------

void rotate(Pattern& p, int steps) noexcept {
  const int n = static_cast<int>(length_of(p));
  const int m = ((steps % n) + n) % n;
  if (m == 0) {
    return;
  }
  const auto r = static_cast<unsigned>(m);
  rotate_right(p.steps, static_cast<unsigned>(n), r);
  for (auto& lane : p.motion) {
    rotate_right(lane, static_cast<unsigned>(n) * kMotionPointsPerStep, r * kMotionPointsPerStep);
  }
}

bool transpose_notes(Pattern& p, int semitones) noexcept {
  const unsigned n = length_of(p);
  for (unsigned j = 0; j < n; ++j) {
    const int v = static_cast<int>(p.steps[j].note) + semitones;
    if (v < 0 || v > static_cast<int>(kMaxNote)) {
      return false;
    }
  }
  for (unsigned j = 0; j < n; ++j) {
    p.steps[j].note = static_cast<std::uint8_t>(static_cast<int>(p.steps[j].note) + semitones);
  }
  return true;
}

void reverse(Pattern& p) noexcept {
  const unsigned n = length_of(p);
  reverse_range(p.steps, n);
  for (auto& lane : p.motion) {
    reverse_range(lane, n * kMotionPointsPerStep);
  }
}

void invert(Pattern& p) noexcept {
  const unsigned n = length_of(p);
  int lo = 1000;
  int hi = -1;
  for (unsigned j = 0; j < n; ++j) {
    if (is_playing(p.steps[j])) {
      const int v = p.steps[j].note;
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
    }
  }
  if (hi < 0) {
    return;
  }
  for (unsigned j = 0; j < n; ++j) {
    if (is_playing(p.steps[j])) {
      p.steps[j].note = static_cast<std::uint8_t>(lo + hi - p.steps[j].note);
    }
  }
}

bool double_length(Pattern& p) noexcept {
  const unsigned n = length_of(p);
  if (n * 2u > kMaxSteps) {
    return false;
  }
  for (unsigned j = 0; j < n; ++j) {
    p.steps[n + j] = p.steps[j];
    copy_points(p, n + j, j);
  }
  p.length = static_cast<std::uint8_t>(n * 2u);
  return true;
}

void clear_pattern(Pattern& p) noexcept {
  for (unsigned j = 0; j < kMaxSteps; ++j) {
    make_rest(p, j);
  }
}

bool set_length(Pattern& p, unsigned length) noexcept {
  if (!valid_length(length)) {
    return false;
  }
  p.length = static_cast<std::uint8_t>(length);
  return true;
}

void set_swing(Pattern& p, int percent_) noexcept {
  p.swing = static_cast<std::uint8_t>(percent_ < 0 ? 0 : (percent_ > static_cast<int>(kMaxSwing) ? static_cast<int>(kMaxSwing) : percent_));
}

void set_gate(Pattern& p, int percent_) noexcept {
  p.gate = static_cast<std::uint8_t>(percent_ < 5 ? 5 : (percent_ > 100 ? 100 : percent_));
}

void set_transpose(Pattern& p, int semitones) noexcept {
  p.transpose = static_cast<std::int8_t>(semitones < -24 ? -24 : (semitones > 24 ? 24 : semitones));
}

// ---- Motion and parameter locks ---------------------------------------------------------------------------------------------

void set_motion_point(Pattern& p, unsigned lane, unsigned step, unsigned point, std::uint8_t value) noexcept {
  if (lane >= kMotionLanes || point >= kMotionPointsPerStep) {
    return;
  }
  p.motion[lane][clamp_step(step) * kMotionPointsPerStep + point] =
      (value == kNoLock || value <= kMaxPoint) ? value : kMaxPoint;
}

void set_plock(Pattern& p, unsigned lane, unsigned step, std::uint8_t value) noexcept {
  for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
    set_motion_point(p, lane, step, q, value);
  }
}

void clear_plock(Pattern& p, unsigned lane, unsigned step) noexcept { set_plock(p, lane, step, kNoLock); }

void clear_step_motion(Pattern& p, unsigned step) noexcept { clear_points(p, clamp_step(step)); }

void clear_lane_motion(Pattern& p, unsigned lane) noexcept {
  if (lane >= kMotionLanes) {
    return;
  }
  for (auto& point : p.motion[lane]) {
    point = kNoLock;
  }
}

void clear_motion(Pattern& p) noexcept {
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    clear_lane_motion(p, lane);
  }
}

bool step_has_motion(const Pattern& p, unsigned step) noexcept {
  const unsigned s = clamp_step(step);
  for (const auto& lane : p.motion) {
    for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
      if (lane[s * kMotionPointsPerStep + q] != kNoLock) {
        return true;
      }
    }
  }
  return false;
}

bool lane_has_motion(const Pattern& p, unsigned lane) noexcept {
  if (lane >= kMotionLanes) {
    return false;
  }
  for (unsigned i = 0; i < kPointsPerLane; ++i) {
    if (p.motion[lane][i] != kNoLock) {
      return true;
    }
  }
  return false;
}

std::uint8_t value_to_point(const o3sdk::core::ParamDesc& d, float value) noexcept {
  const float n = o3sdk::core::param_norm(d, value);
  const float x = n * static_cast<float>(kMaxPoint) + 0.5f;
  const int v = static_cast<int>(x);
  return static_cast<std::uint8_t>(v < 0 ? 0 : (v > static_cast<int>(kMaxPoint) ? static_cast<int>(kMaxPoint) : v));
}

float point_to_value(const o3sdk::core::ParamDesc& d, std::uint8_t point) noexcept {
  const std::uint8_t pt = point > kMaxPoint ? kMaxPoint : point;
  const float n = static_cast<float>(pt) * (1.0f / static_cast<float>(kMaxPoint));
  if (d.unit == o3sdk::core::ParamUnit::Enum) {
    const float idx = static_cast<float>(static_cast<int>(n * (d.max - d.min) + 0.5f));
    return d.min + idx;
  }
  return o3sdk::core::param_denorm(d, n);
}

// ---- Generators -------------------------------------------------------------------------------------------------------------

void random_fill(Pattern& p, const GenParams& g, std::uint32_t seed) noexcept {
  std::uint8_t notes[128];
  const unsigned count = scale_notes(g, notes);
  Rng rng(seed);
  const float density = percent(g.density);
  const float accent = percent(g.accent_pct);
  const float slide = percent(g.slide_pct);
  const unsigned n = length_of(p);
  int previous = notes[0];
  for (unsigned i = 0; i < n; ++i) {
    Step& s = p.steps[i];
    if (i != 0u && !rng.chance(density)) {
      s.flags = kStepRest;
      continue;
    }
    int note = notes[rng.next_int(0, static_cast<int>(count) - 1)];
    if (rng.chance(0.12f)) {  // a repeated note: the backbone of a 303 line, and something for a slide to glide from
      note = previous;
    }
    previous = note;
    s.note = static_cast<std::uint8_t>(note);
    std::uint8_t flags = 0;
    if (rng.chance(accent)) {
      flags |= kStepAccent;
    }
    if (rng.chance(slide)) {
      flags |= kStepSlide;
    }
    s.flags = flags;
  }
}

void mutate(Pattern& p, const GenParams& g, std::uint32_t seed, unsigned amount_pct) noexcept {
  std::uint8_t notes[128];
  const unsigned count = scale_notes(g, notes);
  Rng rng(seed);
  const float amount = percent(amount_pct);
  const float density = percent(g.density);
  const float accent = percent(g.accent_pct);
  const float slide = percent(g.slide_pct);
  const unsigned n = length_of(p);
  for (unsigned i = 0; i < n; ++i) {
    if (!rng.chance(amount)) {
      continue;
    }
    switch (rng.next_int(0, 3)) {
      case 0:
        p.steps[i].note = notes[rng.next_int(0, static_cast<int>(count) - 1)];
        break;
      case 1:
        set_flag(p, i, kStepRest, !rng.chance(density));
        break;
      case 2:
        set_flag(p, i, kStepAccent, rng.chance(accent));
        break;
      default:
        set_flag(p, i, kStepSlide, rng.chance(slide));
        break;
    }
  }
}

}  // namespace o3_303::seq
