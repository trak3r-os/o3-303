// SPDX-License-Identifier: GPL-3.0-or-later
// TapTempo (app/include/o3_303/tap_tempo.h, docs/UX-303.md 5.6): pure, with the clock handed in. The mean gap of the last four taps, a reset after a gap
// over two seconds, the engine's range, and the taps that are not taps.
#include <doctest/doctest.h>

#include <o3_303/tap_tempo.h>
#include <o3sdk/dsp/transport.h>

using o3_303::TapTempo;

TEST_CASE("one tap says nothing; the second gives the tempo of the gap") {
  TapTempo t;
  std::uint32_t mbpm = 7;
  CHECK_FALSE(t.tap(1000, mbpm));
  CHECK(mbpm == 7u);  // untouched
  CHECK(t.count() == 1u);
  CHECK(t.tap(1500, mbpm));
  CHECK(mbpm == 120'000u);  // 500 ms a beat
  CHECK(t.count() == 2u);
}

TEST_CASE("four taps use three gaps, and the fifth drops the first: the mean of the last four taps") {
  TapTempo t;
  std::uint32_t mbpm = 0;
  // Gaps of 400, 500, 600 ms: 3 gaps over 1500 ms, a mean gap of 500 ms.
  CHECK_FALSE(t.tap(0, mbpm));
  CHECK(t.tap(400, mbpm));
  CHECK(mbpm == 150'000u);
  CHECK(t.tap(900, mbpm));
  CHECK(mbpm == 133'333u);  // 2 gaps over 900 ms
  CHECK(t.tap(1500, mbpm));
  CHECK(mbpm == 120'000u);
  CHECK(t.count() == 4u);
  // The fifth tap at 2000 ms: the window is the taps at 400, 900, 1500, 2000: 3 gaps over 1600 ms.
  CHECK(t.tap(2000, mbpm));
  CHECK(mbpm == 112'500u);
  CHECK(t.count() == 4u);
  // The sixth at 2500: 900, 1500, 2000, 2500: 3 gaps over 1600 ms again.
  CHECK(t.tap(2500, mbpm));
  CHECK(mbpm == 112'500u);
}

TEST_CASE("a gap over two seconds starts a new measure with that tap; two seconds exactly does not") {
  TapTempo t;
  std::uint32_t mbpm = 0;
  CHECK_FALSE(t.tap(0, mbpm));
  CHECK(t.tap(500, mbpm));
  CHECK(mbpm == 120'000u);
  CHECK_FALSE(t.tap(500 + 2001, mbpm));  // too late: this is the first tap of a new measure
  CHECK(t.count() == 1u);
  CHECK(mbpm == 120'000u);
  CHECK(t.tap(500 + 2001 + 1000, mbpm));
  CHECK(mbpm == 60'000u);  // only the new measure's gap counts
  // Exactly kResetMs is still the same measure (30 BPM).
  TapTempo u;
  CHECK_FALSE(u.tap(10, mbpm));
  CHECK(u.tap(10 + TapTempo::kResetMs, mbpm));
  CHECK(mbpm == 30'000u);
  CHECK(u.count() == 2u);
}

TEST_CASE("the tempo is clamped to the engine's range") {
  TapTempo fast;
  std::uint32_t mbpm = 0;
  CHECK_FALSE(fast.tap(100, mbpm));
  CHECK(fast.tap(110, mbpm));  // 10 ms: 6000 BPM
  CHECK(mbpm == o3sdk::dsp::kMaxTempoMilliBpm);
  CHECK(fast.tap(115, mbpm));
  CHECK(mbpm == o3sdk::dsp::kMaxTempoMilliBpm);
  // The slowest a measure can say is 30 BPM (a longer gap resets), inside the range, so it is not clamped: the floor is 20.
  static_assert(60'000'000ull / TapTempo::kResetMs >= o3sdk::dsp::kMinTempoMilliBpm, "a measure never needs the lower clamp");
  // A clamp that does fire: a tap 1 ms after the last is a gap of 1 ms.
  TapTempo f2;
  CHECK_FALSE(f2.tap(5000, mbpm));
  CHECK(f2.tap(5001, mbpm));
  CHECK(mbpm == o3sdk::dsp::kMaxTempoMilliBpm);
}

TEST_CASE("a tap on the same millisecond or from the past is not a tap") {
  TapTempo t;
  std::uint32_t mbpm = 0;
  CHECK_FALSE(t.tap(1000, mbpm));
  CHECK_FALSE(t.tap(1000, mbpm));  // the clock did not move
  CHECK(t.count() == 1u);
  CHECK_FALSE(t.tap(900, mbpm));   // the clock went back
  CHECK(t.count() == 1u);
  CHECK(t.tap(1500, mbpm));
  CHECK(mbpm == 120'000u);
}

TEST_CASE("reset forgets the measure; the ring survives many measures and taps") {
  TapTempo t;
  std::uint32_t mbpm = 0;
  CHECK_FALSE(t.tap(0, mbpm));
  CHECK(t.tap(500, mbpm));
  t.reset();
  CHECK(t.count() == 0u);
  CHECK_FALSE(t.tap(600, mbpm));  // the first of a new measure, whatever was before
  CHECK(t.tap(1200, mbpm));
  CHECK(mbpm == 100'000u);
  // Many taps at a steady 400 ms: always 150 BPM, however far the ring has turned.
  TapTempo s;
  std::uint64_t now = 100;
  for (unsigned i = 0; i < 40; ++i, now += 400) {
    const bool has = s.tap(now, mbpm);
    CHECK(has == (i != 0u));
    if (has) {
      CHECK(mbpm == 150'000u);
    }
  }
  CHECK(s.count() == TapTempo::kMaxTaps);
}

TEST_CASE("count_at: the measure shows nothing once it is stale, and exactly two seconds is not yet") {
  TapTempo t;
  std::uint32_t mbpm = 0;
  CHECK(t.count_at(0) == 0u);
  CHECK_FALSE(t.tap(1000, mbpm));
  CHECK(t.tap(1500, mbpm));
  CHECK(t.count_at(1500) == 2u);
  CHECK(t.count_at(1500 + TapTempo::kResetMs) == 2u);
  CHECK(t.count_at(1500 + TapTempo::kResetMs + 1) == 0u);
  CHECK(t.count() == 2u);  // the raw count is still there; the next tap starts afresh
  CHECK_FALSE(t.tap(1500 + TapTempo::kResetMs + 1, mbpm));
  CHECK(t.count_at(1500 + TapTempo::kResetMs + 1) == 1u);
}
