// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Tap tempo (docs/UX-303.md 5.6): the tempo from the gaps between the last taps. Pure: the caller hands in the time of each tap and applies the tempo
// it gets back, so tests/tap_tempo_test.cpp checks it with no engine and no clock. Shift + Master (from any screen) and MASTER's F2 TAP use one.
//
// A measure is up to kMaxTaps taps; the tempo is the mean gap of the taps in it (n taps are n - 1 gaps, so one tap alone says nothing). A gap longer
// than kResetMs since the last tap starts a new measure with this tap. The result is in milli-BPM, clamped to the engine's range (20..300 BPM).
// A tap that does not move the clock forward (the same millisecond) is not counted.

#include <o3sdk/dsp/transport.h>

#include <cstdint>

namespace o3_303 {

class TapTempo {
 public:
  static constexpr unsigned kMaxTaps = 4;            // the measure: the last four taps (three gaps)
  static constexpr std::uint64_t kResetMs = 2000;    // a longer gap starts a new measure (30 BPM, the slowest a measure can say)

  // A tap at `now_ms` (a clock that does not wrap). true with `mbpm` set when the measure has a tempo (two taps or more); false with `mbpm`
  // untouched for the first tap of a measure.
  bool tap(std::uint64_t now_ms, std::uint32_t& mbpm) noexcept {
    if (n_ != 0u) {
      const std::uint64_t last = ms_[(head_ + kMaxTaps - 1u) % kMaxTaps];
      if (now_ms <= last) {  // the clock did not move (or went back): not a tap
        return false;
      }
      if (now_ms - last > kResetMs) {
        n_ = 0;
      }
    }
    ms_[head_] = now_ms;
    head_ = (head_ + 1u) % kMaxTaps;
    if (n_ < kMaxTaps) {
      ++n_;
    }
    if (n_ < 2u) {
      return false;
    }
    const std::uint64_t span = now_ms - ms_[(head_ + kMaxTaps - n_) % kMaxTaps];  // from the oldest tap of the measure
    // n taps are n - 1 gaps: a gap is span / (n - 1) ms and a tempo 60000 / gap BPM, in milli-BPM 60,000,000 * (n - 1) / span.
    std::uint64_t v = 60'000'000ull * (n_ - 1u) / span;
    v = v < o3sdk::dsp::kMinTempoMilliBpm ? o3sdk::dsp::kMinTempoMilliBpm : (v > o3sdk::dsp::kMaxTempoMilliBpm ? o3sdk::dsp::kMaxTempoMilliBpm : v);
    mbpm = static_cast<std::uint32_t>(v);
    return true;
  }

  // The measure starts afresh (the app's setup).
  void reset() noexcept { n_ = 0; }

  // The taps in the measure, 0..kMaxTaps; count_at() is what a screen shows at `now_ms`: 0 once the measure has gone stale (the next tap would start a new one).
  unsigned count() const noexcept { return n_; }
  unsigned count_at(std::uint64_t now_ms) const noexcept {
    return n_ != 0u && now_ms - ms_[(head_ + kMaxTaps - 1u) % kMaxTaps] > kResetMs ? 0u : n_;
  }

 private:
  std::uint64_t ms_[kMaxTaps] = {};
  unsigned head_ = 0;  // where the next tap is written
  unsigned n_ = 0;     // taps of the measure held in the ring, 0..kMaxTaps
};

}  // namespace o3_303
