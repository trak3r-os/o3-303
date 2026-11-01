// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The sample on which the engine's transport reaches a whole tick, in exact rational arithmetic (the same as o3-sdk's tests/support/exact_grid.h, which is not on this tree's include
// path): sample n reaches tick t when n * mbpm * 960 >= t * 60 * rate_mhz, so it is the ceiling of that division, with nothing rounded before it. The kit's transport lands a step there
// ("Exact landing", o3sdk/dsp/transport.h); the floored Q32.32 increment these tests used before put every whole-sample step one sample late.

#include <o3sdk/core/audio_clock.h>
#include <o3sdk/dsp/transport.h>

#include <cstdint>

namespace o3_303::seq_test {

inline std::uint64_t exact_frame(std::uint64_t tick, std::uint32_t tempo_mbpm) {
  namespace core = o3sdk::core;
  namespace dsp = o3sdk::dsp;
  constexpr std::uint64_t rate_mhz =
      (static_cast<std::uint64_t>(core::kCpuHz) * 1000u * core::kRefCyclesPerSample.den + core::kRefCyclesPerSample.num / 2u) / core::kRefCyclesPerSample.num;
  const std::uint64_t t = tempo_mbpm < dsp::kMinTempoMilliBpm ? dsp::kMinTempoMilliBpm : (tempo_mbpm > dsp::kMaxTempoMilliBpm ? dsp::kMaxTempoMilliBpm : tempo_mbpm);
  const std::uint64_t num = tick * 60u * rate_mhz;
  const std::uint64_t den = t * dsp::kPpqn;
  return (num + den - 1u) / den;
}

// The sample on which `tick` is reached when the tempo became `to` at the block boundary after `frame0` samples at `from` since Play: the position at frame0 is frame0 * from's rate, and each
// sample after it adds `to`'s (the engine applies a tempo at frame 0 of a block). Exact, as above.
inline std::uint64_t exact_frame_after_change(std::uint64_t tick, std::uint64_t frame0, std::uint32_t from, std::uint32_t to) {
  namespace core = o3sdk::core;
  namespace dsp = o3sdk::dsp;
  constexpr std::uint64_t rate_mhz =
      (static_cast<std::uint64_t>(core::kCpuHz) * 1000u * core::kRefCyclesPerSample.den + core::kRefCyclesPerSample.num / 2u) / core::kRefCyclesPerSample.num;
  const auto clamp = [](std::uint32_t m) -> std::uint64_t { return m < dsp::kMinTempoMilliBpm ? dsp::kMinTempoMilliBpm : (m > dsp::kMaxTempoMilliBpm ? dsp::kMaxTempoMilliBpm : m); };
  const std::uint64_t pos0 = frame0 * clamp(from) * dsp::kPpqn;  // in units of 1 / (60 * rate_mhz) ticks
  const std::uint64_t target = tick * 60u * rate_mhz;
  if (target <= pos0) {
    return frame0;
  }
  const std::uint64_t slope = clamp(to) * dsp::kPpqn;
  return frame0 + (target - pos0 + slope - 1u) / slope;
}

}  // namespace o3_303::seq_test
