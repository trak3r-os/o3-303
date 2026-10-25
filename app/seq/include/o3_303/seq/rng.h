// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A tiny deterministic generator: SplitMix32, a near-verbatim copy of FastRandom from Replicant 303's preset generator
// (third_party/replicant303/Presets/PresetBank.cpp, commit d5369be028399f5a403067b58da2936caa070c0e) by DanimanX. The same seed gives
// the same stream on every platform; the vault depends on it (gen.h), and so do the random / mutate tools (ops.h). Not for audio.
// Replicant 303 is distributed under the MIT License:
//
//   MIT License
//
//   Copyright (c) 2026 Daniel Rojas Grasset (DanimanX)
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.

#include <cstdint>

namespace o3_303::seq {

class Rng {
 public:
  explicit Rng(std::uint32_t seed) noexcept : state_(seed != 0u ? seed : 0x30303030u) {}

  std::uint32_t next() noexcept {
    state_ += 0x9e3779b9u;
    std::uint32_t z = state_;
    z = (z ^ (z >> 16)) * 0x85ebca6bu;
    z = (z ^ (z >> 13)) * 0xc2b2ae35u;
    return z ^ (z >> 16);
  }

  // [0, 1) with 24 bits.
  float next_float() noexcept { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }

  // low..high inclusive (low <= high).
  int next_int(int low, int high) noexcept {
    const std::uint32_t span = static_cast<std::uint32_t>(high - low + 1);
    return low + static_cast<int>(next() % span);
  }

  float between(float low, float high) noexcept { return low + (high - low) * next_float(); }

  bool chance(float probability) noexcept { return next_float() < probability; }

 private:
  std::uint32_t state_;
};

}  // namespace o3_303::seq
