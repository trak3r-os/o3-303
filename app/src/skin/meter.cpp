// SPDX-License-Identifier: GPL-3.0-or-later
#include "meter.h"

namespace o3_303::skin {

namespace {

constexpr int kMargin = 3;
constexpr int kGap = 2;

constexpr ui::Rgb kGreenDark{18, 40, 20};
constexpr ui::Rgb kAmberDark{50, 36, 12};

}  // namespace

unsigned segments_lit(unsigned level, unsigned n) noexcept {
  if (n == 0u) {
    return 0;
  }
  const unsigned l = level > kMeterMax ? kMeterMax : level;
  unsigned lit = 0;
  while (lit < n && l > lit * kMeterMax / n) {
    ++lit;
  }
  return lit;
}

bool segment_amber(unsigned i, unsigned n) noexcept { return i >= n * 3u / 4u; }

ui::Rect segment_box(const ui::Rect& win, unsigned n, unsigned i) noexcept {
  const int pitch = n != 0u ? (win.w - 2 * kMargin) / static_cast<int>(n) : 0;
  return ui::Rect{win.x + kMargin + pitch * static_cast<int>(i), win.y + kMargin, pitch - kGap, win.h - 2 * kMargin};
}

ui::Rgb segment_rgb(bool amber, bool lit) noexcept {
  if (lit) {
    return amber ? pal::kLedAmber : pal::kLedGreen;
  }
  return amber ? kAmberDark : kGreenDark;
}

bool segment_lit(unsigned i, unsigned n, unsigned level, unsigned peak) noexcept {
  const unsigned lit = segments_lit(level, n);
  const unsigned top = segments_lit(peak, n);
  return i < lit || (top > lit && i + 1u == top);
}

void queue_bar(Panel& p, const ui::Rect& win, unsigned n, unsigned level, unsigned peak, bool live) noexcept {
  window(p, win, 3);
  for (unsigned i = 0; i < n; ++i) {
    const bool lit = live && segment_lit(i, n, level, peak);
    p.rect(lit ? Layer::MeterOn : Layer::MeterOff, segment_box(win, n, i), segment_rgb(segment_amber(i, n), lit));
  }
}

}  // namespace o3_303::skin
