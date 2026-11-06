// SPDX-License-Identifier: GPL-3.0-or-later
// The golden pin of the voice (docs/MODS-PLAN.md 3.3 point 6): FNV-1a over 200,000 frames of a gesture (notes, accents, slides, ties, every
// SOUND parameter moved, a note-off, an all-notes-off) for SAW, SQR and a run that toggles Wave. The checksums pin the voice as it is (they
// were first taken before any mod existed, and the voice was then rewritten on Open303, 2026-11): they change only with the sound, and then
// they are retaken on purpose.
//
// This is a host pin, and it says so. prepare() and set() call libm's std::exp, so the bits depend on the host's glibc: the test checks only
// on an x86-64 glibc of the recorded version, in Debug and Release alike (x86-64 GCC emits no FMA without -march, and float SSE arithmetic
// is the same at -O0 and -O2). Anywhere else it reports, by name, that it did not check. The MK66 is not covered by this pin: GCC contracts
// a * b + c there, so the cross build is covered by an objdump diff of the stock instantiations of run() (docs/MODS-PLAN.md 3.3 point 6).
#include <doctest/doctest.h>

#include "acid_voice_gesture.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__GLIBC__)
#include <gnu/libc-version.h>
#endif

using namespace acid_voice_test;

namespace {

// Recorded on 2026-11-06 on x86-64 with glibc 2.39 from the Open303-based voice (the coupled TB_303 ladder at two sub-steps, the stock loop in
// four passes), in Debug and in Release. A hash cannot say how far the bits moved: the stock and modded loops are compared sample by sample in
// acid_voice_behaviour_test.cpp.
constexpr const char* kGlibc = "2.39";
constexpr std::uint64_t kGoldenSaw = 0x7b86e03248ad82b4ull;
constexpr std::uint64_t kGoldenSqr = 0x87275e3afb2d33d6ull;
constexpr std::uint64_t kGoldenToggle = 0xfbadaf4bbe8037c3ull;

bool host_is_recorded(const char*& why) {
#if defined(__x86_64__) && defined(__GLIBC__)
  if (std::strcmp(gnu_get_libc_version(), kGlibc) == 0) {
    return true;
  }
  why = "glibc is not the recorded version";
#else
  why = "not an x86-64 glibc host";
#endif
  return false;
}

std::vector<float> render_gesture(bool square, WaveMove wave) {
  Rig r;
  r.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
  build_gesture(r.script, wave);
  return r.render(kGestureFrames);
}

}  // namespace

TEST_CASE("golden: the gesture sounds, and is finite") {
  for (const bool square : {false, true}) {
    const std::vector<float> v = render_gesture(square, WaveMove::Keep);
    REQUIRE(v.size() == kGestureFrames);
    bool finite = true;
    for (const float s : v) {
      finite = finite && std::isfinite(s);
    }
    CHECK(finite);
    CHECK(peak(v, 0, v.size()) > 0.1);
    CHECK(rms(v, 199000, 200000) < rms(v, 190000, 193500));  // the last step's note ends and releases
  }
}

TEST_CASE("golden: today's voice, bit for bit (host pin)") {
  const std::uint64_t saw = fnv1a(render_gesture(false, WaveMove::Keep));
  const std::uint64_t sqr = fnv1a(render_gesture(true, WaveMove::Keep));
  const std::uint64_t tog = fnv1a(render_gesture(false, WaveMove::Toggle));
  if (std::getenv("O3_303_PRINT_GOLDEN") != nullptr) {
    std::printf("golden: SAW 0x%016llxull SQR 0x%016llxull TOGGLE 0x%016llxull\n", static_cast<unsigned long long>(saw),
                static_cast<unsigned long long>(sqr), static_cast<unsigned long long>(tog));
  }
  const char* why = "";
  if (!host_is_recorded(why)) {
    MESSAGE("golden pin NOT CHECKED: " << why << " (recorded on x86-64 glibc " << kGlibc << ")");
    return;
  }
  CHECK(saw == kGoldenSaw);
  CHECK(sqr == kGoldenSqr);
  CHECK(tog == kGoldenToggle);
}
