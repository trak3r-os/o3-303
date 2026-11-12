// SPDX-License-Identifier: GPL-3.0-or-later
// The level of the two vowel effects against their dry input (docs/PADS-TRACT-PLAN.md section B): the 303 voice's saw at C2 (rms 0.0676, peak
// 0.2557) is rendered once, run through TRACT and GROWL at Mix 50 % and 100 %, and the RMS of the output is read against the dry RMS in dB.
// Measured 2026-11-12 (the table is printed to stderr by the test case below; a ctest log shows it on a failure):
//
//   At GAIN 0 dB (dB against dry)          Mix 50 %   Mix 100 %      At the default GAIN             Mix 50 %   Mix 100 %
//   TRACT A res 0 (the factory sound)       -0.42      -0.39         (TRACT +0.5 dB)                  -0.17      +0.11
//   TRACT A res 1                           -1.08      -1.71                                          -0.85      -1.21
//   TRACT E res 0 / res 1                   -0.64 / -2.13   -0.77 / -2.72                             -0.40 / -1.92   -0.27 / -2.22
//   TRACT I res 0 / res 1                   -1.21 / -1.33   +0.19 / +0.36                             -0.95 / -1.06   +0.69 / +0.86
//   TRACT O res 0 / res 1                   -0.67 / -1.50   -0.31 / -2.13                             -0.42 / -1.28   +0.19 / -1.63
//   TRACT U res 0 / res 1                   -1.01 / -2.21   -0.00 / +0.27                             -0.76 / -1.95   +0.50 / +0.77
//   GROWL factory (A, comb mix 50 %)        -5.79      -14.87        (GROWL +15 dB, linear law)       -2.68      +0.13
//   GROWL comb mix 100 %                    -5.88      -16.79                                         -3.65      -1.79
//   GROWL vowel E / I / O / U               -5.4 / -3.6 / -5.5 / -4.1   -11.0 / -5.1 / -11.5 / -6.3   (vowels I and U stay up to +9.9 dB at the
//                                                                                                       default: the default matches the factory sound only)
//   GROWL at the default GAIN, equal-power law: factory +0.33 (Mix 50 %), +0.13 (Mix 100 %); comb mix 100 %: -0.64, -1.79
//
// Decisions:
//   - kTractGainDefaultDb = +0.5 and kGrowlGainDefaultDb = +15.0: the level match at Mix 100 % of the factory sound, to 0.5 dB.
//   - Mix law. TRACT: Mix 50 % sits -0.2 dB under the mean of its two ends (dry 0 dB, wet +0.1 dB): linear stays. GROWL: with the linear law
//     Mix 50 % sat -2.74 dB under the mean of its ends (the wet is a formant-shaped, comb-filtered copy, decorrelated from the dry one), more than
//     the 1.5 dB of the rule: GROWL crossfades at equal power, dry cos(pi m / 2) + wet sin(pi m / 2), which puts Mix 50 % at +0.20 dB over the mean.
//     Mix 0 keeps its exact bypass, Mix 100 % its wet-only loop.
//   - The peaks: at Mix 100 % with +15 dB a loud GROWL sound can pass 0 dBFS (the vowels I and U are the cases at +9 dB rms); the master limiter
//     is the safety, the knob the remedy (the default is a level match of the factory sound, not of every sound).
#include <doctest/doctest.h>

#include "../units/acid_voice/tests/acid_voice_rig.h"

#include <o3_303/units/growl/growl.h>
#include <o3_303/units/tract/tract.h>
#include <o3sdk/dsp/engine.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

namespace dsp = o3sdk::dsp;
using o3_303::Growl;
using o3_303::TractFx;

constexpr float kRate = 44100.0f;
constexpr std::size_t kFrom = 22050;   // the window read: 0.5 s to 1.0 s, the Mix ramp and the unit's settling long past
constexpr std::size_t kTo = 44100;

struct Ev {
  std::uint16_t id;
  float value;
};

// The voice: a saw at C2 (MIDI 36), the amplifier held (Sustain 100 %), Volume 0 dB, Overdrive at its stock 0 dB.
std::vector<float> dry_saw() {
  acid_voice_test::Rig rig;
  rig.set(o3_303::AcidVoice::Volume, 0.0f);
  rig.set(o3_303::AcidVoice::Sustain, 1.0f);
  rig.set(o3_303::AcidVoice::Wave, 0.0f);
  rig.script.on(0, 36);
  return rig.render(kTo + 4096);
}

// The unit driven in blocks of 64 on `in` (mono, to both channels); the parameter events are delivered at frame 0 of the first block.
template <class U>
std::vector<float> through(U& u, const std::vector<float>& in, const std::vector<Ev>& setup) {
  u.prepare(kRate);
  std::vector<float> l = in;
  std::vector<float> r = in;
  std::vector<dsp::Event> evs;
  dsp::Transport tr;
  for (std::size_t pos = 0; pos < l.size(); pos += 64) {
    const auto frames = static_cast<std::uint32_t>(std::min<std::size_t>(64, l.size() - pos));
    tr.frames = frames;
    evs.clear();
    if (pos == 0) {
      for (const Ev& e : setup) {
        evs.push_back(dsp::param_change(e.id, e.value, 0));
      }
    }
    const dsp::ProcessArgs a{evs.data(), static_cast<std::uint32_t>(evs.size()), l.data() + pos, r.data() + pos, l.data() + pos, r.data() + pos, frames, nullptr};
    u.process(a);
  }
  return l;
}

double db_against(const std::vector<float>& out, const std::vector<float>& dry) {
  return 20.0 * std::log10(acid_voice_test::rms(out, kFrom, kTo) / acid_voice_test::rms(dry, kFrom, kTo));
}

struct Row {
  const char* what;
  double mix50;
  double mix100;
};

// Every row at its default GAIN (the table of the header) unless gain_db is given.
std::vector<Row> tract_rows(const std::vector<float>& dry, const float* gain_db) {
  std::vector<Row> rows;
  static const char* const kVowel[] = {"A", "E", "I", "O", "U"};
  static char names[15][24];
  int k = 0;
  for (int v = 0; v < 5; ++v) {
    for (const float res : {0.0f, 0.5f, 1.0f}) {
      Row row{};
      std::snprintf(names[k], sizeof names[k], "TRACT %s res %.1f", kVowel[v], static_cast<double>(res));
      row.what = names[k++];
      for (const float mix : {0.5f, 1.0f}) {
        TractFx u;
        std::vector<Ev> set = {{TractFx::Vowel, static_cast<float>(v) * 0.25f}, {TractFx::Res, res}, {TractFx::Mix, mix}};
        if (gain_db != nullptr) {
          set.push_back({TractFx::Gain, *gain_db});
        }
        (mix < 0.75f ? row.mix50 : row.mix100) = db_against(through(u, dry, set), dry);
      }
      rows.push_back(row);
    }
  }
  return rows;
}

struct GrowlCase {
  const char* what;
  std::vector<Ev> set;
};

std::vector<GrowlCase> growl_cases() {
  return {
      {"GROWL factory", {{Growl::Note, 36.0f}}},
      {"GROWL comb mix 100", {{Growl::Note, 36.0f}, {Growl::CombMix, 1.0f}}},
      {"GROWL vowel E", {{Growl::Note, 36.0f}, {Growl::Vowel, 0.25f}}},
      {"GROWL vowel I", {{Growl::Note, 36.0f}, {Growl::Vowel, 0.5f}}},
      {"GROWL vowel O", {{Growl::Note, 36.0f}, {Growl::Vowel, 0.75f}}},
      {"GROWL vowel U", {{Growl::Note, 36.0f}, {Growl::Vowel, 1.0f}}},
  };
}

std::vector<Row> growl_rows(const std::vector<float>& dry, const float* gain_db) {
  std::vector<Row> rows;
  for (const GrowlCase& c : growl_cases()) {
    Row row{c.what, 0.0, 0.0};
    for (const float mix : {0.5f, 1.0f}) {
      Growl u;
      std::vector<Ev> set = c.set;
      set.push_back({Growl::Mix, mix});
      if (gain_db != nullptr) {
        set.push_back({Growl::Gain, *gain_db});
      }
      (mix < 0.75f ? row.mix50 : row.mix100) = db_against(through(u, dry, set), dry);
    }
    rows.push_back(row);
  }
  return rows;
}

void print(const char* title, const std::vector<Row>& rows) {
  std::fprintf(stderr, "%s\n", title);
  for (const Row& r : rows) {
    std::fprintf(stderr, "LEVEL %-22s mix50 %+6.2f dB  mix100 %+6.2f dB\n", r.what, r.mix50, r.mix100);
  }
}

}  // namespace

TEST_CASE("fx level: at the default GAIN the factory settings match the dry level at Mix 100 % and Mix 50 % has no dip") {
  const std::vector<float> dry = dry_saw();
  REQUIRE(acid_voice_test::rms(dry, kFrom, kTo) > 0.01);
  std::fprintf(stderr, "dry saw rms %.4f peak %.4f\n", acid_voice_test::rms(dry, kFrom, kTo), acid_voice_test::peak(dry, kFrom, kTo));
  const float zero = 0.0f;
  std::vector<Row> t_def = tract_rows(dry, nullptr);
  std::vector<Row> g_def = growl_rows(dry, nullptr);
  print("TRACT at GAIN 0 dB (the stage out of the way)", tract_rows(dry, &zero));
  print("GROWL at GAIN 0 dB", growl_rows(dry, &zero));
  print("TRACT at the default GAIN", t_def);
  print("GROWL at the default GAIN", g_def);
  // The factory row of each unit (TRACT: vowel A, res 0 is the first row; GROWL factory is the first).
  const Row& tf = t_def[0];
  const Row& gf = g_def[0];
  CHECK(std::fabs(tf.mix100) <= 0.75);  // the default is the level match to 0.5 dB, the measure to a few hundredths
  CHECK(std::fabs(gf.mix100) <= 0.75);
  // The Mix law: Mix 50 % within 1.5 dB of the mean of the two ends (the dry end is 0 dB by definition): TRACT linear, GROWL equal power.
  CHECK(tf.mix50 - 0.5 * tf.mix100 >= -1.5);
  CHECK(gf.mix50 - 0.5 * gf.mix100 >= -1.5);
  CHECK(gf.mix50 - 0.5 * gf.mix100 <= 1.5);  // and not a bump the other way
}
