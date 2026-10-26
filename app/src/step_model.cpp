// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/step_model.h>

namespace o3_303 {

namespace {

constexpr bool is_rest(const Step& s) noexcept { return (s.flags & kStepRest) != 0u; }
constexpr bool is_tie(const Step& s) noexcept { return (s.flags & kStepTie) != 0u && !is_rest(s); }
constexpr bool is_note(const Step& s) noexcept { return !is_rest(s) && !is_tie(s); }

}  // namespace

StepInfo describe(const Pattern& p, unsigned step) noexcept {
  StepInfo info;
  const unsigned len = p.length != 0u ? p.length : kDefaultLength;
  if (step >= len) {
    return info;
  }
  const Step& s = p.steps[step];
  const Step& next = p.steps[(step + 1u) % len];
  const bool next_tie = is_tie(next);
  if (is_rest(s)) {
    return info;
  }
  info.slide = (s.flags & kStepSlide) != 0u;
  info.to_next = info.slide || next_tie;
  if (is_note(s)) {
    info.kind = Cell::Note;
    info.note = s.note;
    info.accent = (s.flags & kStepAccent) != 0u;
    info.ramp_to = info.slide && is_note(next) ? static_cast<int>(next.note) : -1;
    return info;
  }
  // A tie: the note it carries is the nearest step before it that is not a tie; a rest in between means nothing sounds.
  for (unsigned back = 1; back < len; ++back) {
    const Step& before = p.steps[(step + len - back) % len];
    if (is_tie(before)) {
      continue;
    }
    if (is_rest(before)) {
      return StepInfo{};
    }
    info.kind = Cell::Tie;
    info.note = before.note;
    info.accent = (before.flags & kStepAccent) != 0u;  // the joined bar keeps the colour of the note it carries
    info.from_prev = true;
    return info;
  }
  return StepInfo{};  // a pattern of ties only: nothing starts the note
}

bool pattern_is_empty(const Pattern& p) noexcept {
  for (const Step& s : p.steps) {
    if (!is_rest(s)) {
      return false;
    }
  }
  return true;
}

bool pitch_range(const Pattern& p, int& lowest, int& highest) noexcept {
  const unsigned len = p.length != 0u ? p.length : kDefaultLength;
  bool any = false;
  for (unsigned i = 0; i < len && i < kMaxSteps; ++i) {
    if (!is_note(p.steps[i])) {
      continue;
    }
    const int n = p.steps[i].note;
    if (!any || n < lowest) {
      lowest = n;
    }
    if (!any || n > highest) {
      highest = n;
    }
    any = true;
  }
  return any;
}

bool step_has_motion(const Pattern& p, unsigned lane, unsigned step) noexcept {
  if (lane >= kMotionLanes || step >= kMaxSteps) {
    return false;
  }
  for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
    if (p.motion[lane][step * kMotionPointsPerStep + q] != kNoLock) {
      return true;
    }
  }
  return false;
}

bool pattern_has_motion(const Pattern& p, unsigned lane) noexcept {
  const unsigned len = p.length != 0u ? p.length : kDefaultLength;
  for (unsigned l = 0; l < kMotionLanes; ++l) {
    if (lane < kMotionLanes && l != lane) {
      continue;
    }
    for (unsigned s = 0; s < len && s < kMaxSteps; ++s) {
      if (step_has_motion(p, l, s)) {
        return true;
      }
    }
  }
  return false;
}

PitchWindow fit_window(int lowest, int highest, int lane_h, int top_px) noexcept {
  PitchWindow w;
  if (highest < lowest) {
    const int t = lowest;
    lowest = highest;
    highest = t;
  }
  const int span = highest - lowest + 1;
  int px = 16;
  int rows = lane_h / px;
  int top = (top_px + px - 1) / px;
  for (; px > 5; --px) {  // the tallest rows that leave the notes, a margin above them for a name and one row below
    rows = lane_h / px;
    top = (top_px + px - 1) / px;
    if (rows >= span + top + 1) {
      break;
    }
  }
  rows = lane_h / px;
  top = (top_px + px - 1) / px;
  rows = rows < 1 ? 1 : (rows > 128 ? 128 : rows);
  const int free_rows = rows - span;
  int t = top;
  if (free_rows - top - 1 > 0) {
    t += (free_rows - top - 1) / 2;
  }
  int b = free_rows - t;
  b = b < 0 ? 0 : b;
  int lo = lowest - b;
  lo = lo < 0 ? 0 : lo;
  if (lo + rows - 1 > 127) {
    lo = 128 - rows;
  }
  w.lo = lo;
  w.rows = rows;
  w.px = px;
  return w;
}

PitchWindow range_window(int lowest, int highest, int lane_h, int pad) noexcept {
  PitchWindow w;
  if (highest < lowest) {
    const int t = lowest;
    lowest = highest;
    highest = t;
  }
  pad = pad < 0 ? 0 : pad;
  const int span = highest - lowest + 1;
  const int want = span + 2 * pad;
  int px = 16;
  while (px > 5 && lane_h / px < want) {
    --px;
  }
  int rows = lane_h / px;
  rows = rows < 1 ? 1 : (rows > 128 ? 128 : rows);
  int lo;
  if (rows >= want) {
    rows = want;
    lo = lowest - pad;
  } else {
    lo = lowest - (rows - span) / 2;  // too wide for the lane: the rows that fit, round the middle of the range
  }
  lo = lo < 0 ? 0 : lo;
  if (lo + rows - 1 > 127) {
    lo = 128 - rows;
  }
  w.lo = lo;
  w.rows = rows;
  w.px = px;
  return w;
}

unsigned note_name(unsigned note, char* out, unsigned cap) noexcept {
  static constexpr char kNames[12][3] = {{'C', ' ', 0}, {'C', '#', 0}, {'D', ' ', 0}, {'D', '#', 0}, {'E', ' ', 0}, {'F', ' ', 0},
                                         {'F', '#', 0}, {'G', ' ', 0}, {'G', '#', 0}, {'A', ' ', 0}, {'A', '#', 0}, {'B', ' ', 0}};
  if (cap < 5u) {
    if (cap != 0u) {
      out[0] = '\0';
    }
    return 0;
  }
  unsigned n = 0;
  out[n++] = kNames[note % 12u][0];
  if (kNames[note % 12u][1] == '#') {
    out[n++] = '#';
  }
  const int octave = static_cast<int>(note / 12u) - 1;
  if (octave < 0) {
    out[n++] = '-';
  }
  out[n++] = static_cast<char>('0' + (octave < 0 ? -octave : octave) % 10);
  out[n] = '\0';
  return n;
}

void pattern_name(unsigned index, char* out, unsigned cap) noexcept {
  if (cap < 4u) {
    if (cap != 0u) {
      out[0] = '\0';
    }
    return;
  }
  const unsigned i = index < kPatternCount ? index : kPatternCount - 1u;
  out[0] = static_cast<char>('A' + i / 12u);
  out[1] = static_cast<char>('0' + (i % 12u + 1u) / 10u);
  out[2] = static_cast<char>('0' + (i % 12u + 1u) % 10u);
  out[3] = '\0';
}

}  // namespace o3_303
