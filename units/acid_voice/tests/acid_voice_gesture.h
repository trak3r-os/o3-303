// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support: a fixed 200,000-frame gesture on the voice (notes, accents, slides, ties, every SOUND parameter moved, a note-off, an
// all-notes-off) and a checksum of what it renders. The golden test pins today's voice with it; the mods test compares renderings of it.

#include "acid_voice_rig.h"

#include <cstdint>
#include <cstring>

namespace acid_voice_test {

constexpr std::uint32_t kGestureFrames = 200000;

// FNV-1a over the bit patterns of the samples (memcpy: no punning).
inline std::uint64_t fnv1a(const std::vector<float>& v) {
  std::uint64_t h = 1469598103934665603ull;
  for (const float s : v) {
    std::uint32_t u = 0;
    std::memcpy(&u, &s, sizeof(u));
    for (unsigned b = 0; b < 4; ++b) {
      h ^= (u >> (8u * b)) & 0xFFu;
      h *= 1099511628211ull;
    }
  }
  return h;
}

// What the gesture does with Wave: `Keep` leaves it at the value the rig has, `Toggle` flips it twice mid-way (both loops in one run).
enum class WaveMove { Keep, Toggle };

// 39 steps of 5000 frames. Step k plays a note from a 16-note table; every third is accented, every fifth (k % 5 == 2) slides: its NoteOn
// comes before the NoteOff of the note before it (a tie), the rest are gated 3500 frames. Every fourth step moves one SOUND parameter (the
// seven that are not Wave, in turn). Step 30 is cut by an all-notes-off while it sounds; the last frames are a rest. `offset` shifts every
// event later, for a run that needs a prelude (the 200,000 frames then end `offset` frames later).
inline void build_gesture(Script& s, WaveMove wave, std::uint32_t offset = 0) {
  constexpr std::uint32_t kStep = 5000;
  const int notes[16] = {36, 36, 48, 36, 39, 36, 51, 36, 34, 34, 46, 34, 37, 38, 41, 60};
  // The values each parameter is moved to (in its descriptor's range), the first on its first visit and the second on its second.
  const float moves[8][2] = {{900.0f, 120.0f}, {0.9f, 0.4f}, {0.3f, 1.0f},  {400.0f, 2000.0f},
                             {0.2f, 1.0f},     {0.0f, 0.0f}, {7.0f, -5.0f}, {0.0f, -9.0f}};
  int tied = -1;  // the note left gated for the next step's slide to take over
  for (std::uint32_t k = 0; k < 39; ++k) {
    const std::uint32_t at = offset + k * kStep;
    if (k % 4u == 3u) {
      const unsigned p = (k / 4u) % 8u;
      if (p != 5u) {
        s.param(at + 11, static_cast<std::uint16_t>(p), moves[p][(k / 16u) & 1u]);
      }
    }
    const int note = notes[k % 16u] + (k >= 20u && k < 26u ? 12 : 0);
    const bool slide = k % 5u == 2u;
    std::uint8_t flags = static_cast<std::uint8_t>(k % 3u == 0u ? dsp::kNoteAccent : 0u);
    flags = static_cast<std::uint8_t>(flags | (slide ? dsp::kNoteSlide : 0u));
    s.on(at, note, flags);
    if (tied >= 0) {
      s.off(at + 40, tied);
      tied = -1;
    }
    if (k == 30u) {
      s.all_off(at + 1500);
    }
    if ((k + 1u) % 5u == 2u) {
      tied = note;
    } else {
      s.off(at + 3500, note);
    }
  }
  if (wave == WaveMove::Toggle) {
    s.param(offset + 120000, AcidVoice::Wave, 1.0f);
    s.param(offset + 160000, AcidVoice::Wave, 0.0f);
  }
}

}  // namespace acid_voice_test
