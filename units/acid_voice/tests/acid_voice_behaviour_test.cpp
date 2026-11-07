// SPDX-License-Identifier: GPL-3.0-or-later
// What the Open303-based voice does as a player hears it (docs/VOICE-303.md): the ladder does not ring on its own at stock and does with
// HI RES, the end of a note, RANGE FISH, SUSTAIN, the output ceiling, and the two loops of the voice agreeing. The shape of the sweep, the
// slide and the oracle against Open303 are in acid_voice_mods_test.cpp, acid_voice_engine_test.cpp and acid_voice_oracle_test.cpp.
#include <doctest/doctest.h>

#include "acid_voice_access.h"
#include "acid_voice_rig.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

using namespace acid_voice_test;
using o3_303::AcidVoice;
using o3_303::AcidVoiceTestAccess;

namespace {

using P = std::pair<std::uint16_t, float>;

bool probing() { return std::getenv("O3_303_PROBE") != nullptr; }
void probe(const char* what, double a, double b = 0.0, double c = 0.0) {
  if (probing()) std::printf("probe: %-48s %12.5f %12.5f %12.5f\n", what, a, b, c);
}

bool all_finite(const std::vector<float>& v) {
  for (const float x : v) {
    if (!std::isfinite(x)) return false;
  }
  return true;
}

// A note from 0 to `len`, the given parameters set before the start.
std::vector<float> note(const std::vector<P>& params, int n, std::uint8_t flags, std::uint32_t len, std::uint32_t total) {
  Rig r;
  for (const P& p : params) r.set(p.first, p.second);
  r.script.on(0, n, flags);
  r.script.off(len, n);
  return r.render(total);
}

double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

// A dominant frequency from the spacing of upward crossings over [from, to).
double hz(const std::vector<float>& v, std::size_t from, std::size_t to) { return crossings_hz(v, from, to); }

}  // namespace

TEST_CASE("behaviour: a stock ladder never rings on its own: with the oscillator taken away it is dead two seconds later, over Cutoff x Reso up to Reso 1") {
  // The oscillator feeds the ladder all the time (the amplifier is after it), so the ladder is excited by a note, then OVERDRIVE goes to its
  // bottom (no oscillator at all) with the gate still open: a filter that is not self-oscillating dies, one that is (HI RES, below) does not.
  double worst = 0.0;
  for (const float cutoff : {25.0f, 300.0f, 1500.0f, 4500.0f}) {
    for (const float reso : {0.0f, 0.5f, 0.9f, 1.0f}) {
      Rig r;
      r.set(AcidVoice::Cutoff, cutoff);
      r.set(AcidVoice::Reso, reso);
      r.set(AcidVoice::EnvMod, 0.0f);
      r.script.on(0, 36, dsp::kNoteAccent);
      r.script.param(8000, AcidVoice::Overdrive, -24.0f);
      const std::vector<float> v = r.render(8000 + 2 * 44100);
      INFO("cutoff " << cutoff << " reso " << reso);
      CHECK(all_finite(v));
      const double ring = static_cast<double>(AcidVoiceTestAccess::ladder_peak(r.voice));
      worst = std::max(worst, ring);
      CHECK(ring < 1e-4);
      CHECK(peak(v, 8000 + 44100 + 22050, v.size()) < 1e-4);
    }
  }
  probe("stock ladder after 2 s without input: worst", worst);
}

TEST_CASE("behaviour: HI RES with OVERDRIVE at the bottom rings at the cutoff at Reso 1, 1 kHz and up, bounded") {
  for (const float cutoff : {1000.0f, 2000.0f, 3500.0f}) {
    const std::vector<float> v = note({{AcidVoice::Cutoff, cutoff}, {AcidVoice::Reso, 1.0f}, {AcidVoice::EnvMod, 0.0f}, {AcidVoice::AccSweep, 2.0f}, {AcidVoice::Overdrive, -24.0f}, {AcidVoice::Volume, 0.0f}},
                                      48, 0, 60000, 60000);
    const double level = rms(v, 30000, 60000);
    const double f = hz(v, 30000, 60000);
    probe("hi res ring: cutoff, rms, crossings hz", static_cast<double>(cutoff), level, f);
    INFO("cutoff " << cutoff);
    CHECK(all_finite(v));
    CHECK(peak(v, 0, v.size()) < 1.0);
    CHECK(level > 0.01);                      // it rings, with nothing at the input
    CHECK(f > 0.5 * static_cast<double>(cutoff));
    CHECK(f < 2.0 * static_cast<double>(cutoff));
  }
}

TEST_CASE("behaviour: HI RES: where the ladder rings on its own, per Reso and Cutoff, bounded, finite and without a DC offset") {
  // The feedback is scaled by fc / sqrt(fc^2 + 500^2) (the resonance runs out of steam at low cutoffs), so the doubled feedback of HI RES needs
  // a higher cutoff to oscillate than before. The ring is what is left at the output 0.7 to 1.4 s after the oscillator is taken away (OVERDRIVE at
  // its bottom) with the gate open; "rings" is an RMS above 0.01, as in the test above. The figures are read with O3_303_PROBE=1.
  // The sweep starts at the bottom of the knob (RANGE FISH, 25 Hz = kMinCutoff), where the ladder must stay bounded and dead.
  const float cutoffs[] = {25.0f,  50.0f,  100.0f, 150.0f,  200.0f,  250.0f,  300.0f,  325.0f,  350.0f,  375.0f,  400.0f,
                           500.0f, 600.0f, 700.0f, 850.0f,  1000.0f, 1250.0f, 1500.0f, 2000.0f, 3500.0f, 4800.0f};
  for (const float reso : {0.65f, 1.0f}) {
    float first_ring = 0.0f;
    for (const float cutoff : cutoffs) {
      const std::vector<float> v = note({{AcidVoice::Cutoff, cutoff}, {AcidVoice::Reso, reso}, {AcidVoice::EnvMod, 0.0f}, {AcidVoice::AccSweep, 2.0f},
                                         {AcidVoice::Overdrive, -24.0f}, {AcidVoice::Volume, 0.0f}, {AcidVoice::Range, 1.0f}},
                                        48, 0, 60000, 60000);
      const double level = rms(v, 30000, 60000);
      double mean = 0.0;
      for (std::size_t i = 30000; i < 60000; ++i) mean += static_cast<double>(v[i]);
      mean /= 30000.0;
      probe("hi res ring: reso x 100, cutoff, rms", static_cast<double>(reso) * 100.0, static_cast<double>(cutoff), level);
      INFO("reso " << reso << " cutoff " << cutoff);
      CHECK(all_finite(v));
      CHECK(peak(v, 0, v.size()) < 1.0);
      CHECK(std::fabs(mean) < 0.002);  // the output's high-pass blocks DC
      if (level > 0.01 && first_ring == 0.0f) first_ring = cutoff;
    }
    probe("hi res ring: first cutoff that rings at reso x 100", static_cast<double>(reso) * 100.0, static_cast<double>(first_ring));
    // Read: by the RMS > 0.01 above it rings from 350 Hz at full RESO (0.006 at 325 Hz, a faint start; dead at 300 and below, down to 25 Hz) and
    // from 375 Hz at 65 %; docs/VOICE-303.md says so. (The feedback high-pass at 150 Hz and no scaling put them at about 350 Hz and 1 kHz.)
    if (reso == 1.0f) {
      CHECK(first_ring >= 325.0f);
      CHECK(first_ring <= 375.0f);
    } else {
      CHECK(first_ring >= 350.0f);
      CHECK(first_ring <= 425.0f);
    }
  }
}

TEST_CASE("behaviour: HI RES at full RESO with ENV MOD and Filter FM at 100 % at a low cutoff (25 to 200 Hz): accented notes stay finite and bounded, with a bounded offset") {
  // The resonance roll-off leaves the loop weak at a low cutoff, and ENV MOD and Filter FM push the cutoff up from there while the notes retrigger:
  // nothing may blow up below 200 Hz (RANGE FISH, the whole knob). Figures with O3_303_PROBE=1: peak 0.89 at every cutoff, the ladder's peak
  // 0.08 to 1.2. Unlike the sustained ring above, the mean is not zero: 0.017 to 0.022 over the run, 0.08 to 0.12 inside an accented note (the
  // output there is nearly all offset, a subsonic ladder excursion the 14 Hz high-pass lets through), gone 0.1 s after the gate closes. It needs
  // ENV MOD and Filter FM together (either alone: mean under 0.001), and it is as large without the 500 Hz roll-off (0.012 to 0.015), so it is
  // the FM loop's, not the fold's. Bounded and finite, so the check is 0.05; a follow-up for the unit's ear (a sub-bass wobble on a hot patch).
  for (const float cutoff : {25.0f, 60.0f, 100.0f, 150.0f, 200.0f}) {
    Rig r;
    for (const P& p : {P{AcidVoice::Cutoff, cutoff}, P{AcidVoice::Reso, 1.0f}, P{AcidVoice::EnvMod, 1.0f}, P{AcidVoice::FilterFm, 1.0f}, P{AcidVoice::AccSweep, 2.0f},
                       P{AcidVoice::Range, 1.0f}, P{AcidVoice::Decay, 400.0f}, P{AcidVoice::Volume, 0.0f}}) {
      r.set(p.first, p.second);
    }
    for (std::uint32_t k = 0; k < 8; ++k) {
      r.script.on(k * 10000u, 36 + static_cast<int>(k % 3u) * 12, (k % 2u) != 0u ? dsp::kNoteAccent : std::uint8_t{0});
      r.script.off(k * 10000u + 6000u, 36 + static_cast<int>(k % 3u) * 12);
    }
    const std::vector<float> v = r.render(120000);
    double mean = 0.0;
    for (std::size_t i = 0; i < v.size(); ++i) mean += static_cast<double>(v[i]);
    mean /= static_cast<double>(v.size());
    probe("hi res low cutoff + env mod + fm: cutoff, peak, mean", static_cast<double>(cutoff), peak(v, 0, v.size()), mean);
    probe("hi res low cutoff + env mod + fm: ladder peak", static_cast<double>(AcidVoiceTestAccess::ladder_peak(r.voice)));
    INFO("cutoff " << cutoff);
    CHECK(all_finite(v));
    CHECK(peak(v, 0, v.size()) < 1.0);
    CHECK(std::fabs(mean) < 0.05);
    CHECK(AcidVoiceTestAccess::ladder_peak(r.voice) < AcidVoiceTestAccess::kLadder);
  }
}

TEST_CASE("behaviour: HI RES at Reso 1 with Filter FM at 100 % over a sweep of the cutoff stays finite and bounded") {
  Rig r;
  for (const P& p : {P{AcidVoice::Reso, 1.0f}, P{AcidVoice::AccSweep, 2.0f}, P{AcidVoice::FilterFm, 1.0f}, P{AcidVoice::Overdrive, -24.0f}, P{AcidVoice::Volume, 0.0f}}) r.set(p.first, p.second);
  r.script.on(0, 48, 0);
  for (std::uint32_t k = 0; k < 60; ++k) {
    r.script.param(k * 1000u, AcidVoice::Cutoff, 25.0f + 4475.0f * static_cast<float>(k % 30u) / 29.0f);
  }
  const std::vector<float> v = r.render(60000);
  CHECK(all_finite(v));
  CHECK(peak(v, 0, v.size()) < 1.0);
  CHECK(AcidVoiceTestAccess::ladder_peak(r.voice) < AcidVoiceTestAccess::kLadder);
}

TEST_CASE("behaviour: OVERDRIVE -24 dB is exactly no oscillator") {
  const std::vector<float> v = note({{AcidVoice::Overdrive, -24.0f}, {AcidVoice::Reso, 0.6f}, {AcidVoice::Volume, 0.0f}}, 48, dsp::kNoteAccent, 8000, 12000);
  CHECK(peak(v, 2000, v.size()) < 1e-5);  // measured 2e-7: the ladder's last state running out; the input is exactly zero
  probe("overdrive -24: peak over the first 2000", peak(v, 0, 2000));
}

TEST_CASE("behaviour: the end of a note: unaccented holds, then falls 40 dB by 30 ms; accented has a 50 ms time constant; FISH starts at the gate") {
  constexpr std::uint32_t kGate = 20000;
  const std::vector<P> base = {{AcidVoice::Cutoff, 4500.0f}, {AcidVoice::Reso, 0.0f}, {AcidVoice::Decay, 3000.0f}, {AcidVoice::Volume, 0.0f}, {AcidVoice::AmpDecay, 3000.0f}};
  const auto level = [&](const std::vector<float>& v, double from_ms, double to_ms) {
    return rms(v, kGate + static_cast<std::size_t>(from_ms * 44.1), kGate + static_cast<std::size_t>(to_ms * 44.1));
  };
  const std::vector<float> plain = note(base, 72, 0, kGate, kGate + 8000);
  const double before = rms(plain, kGate - 441, kGate);
  probe("plain: before, 0-4 ms, 4-8 ms", before, level(plain, 0, 4) / before, level(plain, 4, 8) / before);
  probe("plain: 8-12 ms, 20-30 ms, 30-40 ms", level(plain, 8, 12) / before, level(plain, 20, 30) / before, level(plain, 30, 40) / before);
  CHECK(level(plain, 0, 6) > 0.8 * before);                 // about 8 ms at the level it had
  CHECK(db(level(plain, 30, 40) / before) < -40.0);         // and down 40 dB by 30 ms
  const std::vector<float> acc = note(base, 72, dsp::kNoteAccent, kGate, kGate + 12000);
  const double before_a = rms(acc, kGate - 441, kGate);
  const double at50 = level(acc, 45, 55) / before_a;
  probe("accent: ratio at 50 ms (e^-1 = 0.368)", at50, level(acc, 95, 105) / before_a);
  CHECK(at50 > 0.25);
  CHECK(at50 < 0.5);
  std::vector<P> fish = base;
  fish.push_back({AcidVoice::Range, 1.0f});
  const std::vector<float> f = note(fish, 72, 0, kGate, kGate + 8000);
  const double before_f = rms(f, kGate - 441, kGate);
  probe("fish: 0-4 ms, 4-8 ms", level(f, 0, 4) / before_f, level(f, 4, 8) / before_f);
  CHECK(level(f, 4, 8) / before_f < 0.7 * (level(plain, 4, 8) / before));  // it is already falling where the 303 still holds
}

TEST_CASE("behaviour: RANGE FISH with ENV MOD 0 has no sweep; the 303 range with ENV MOD 0 still sweeps; the bass at 32 Hz") {
  const auto tilt = [&](float range, double from_ms, double to_ms) {
    const std::vector<float> v = note({{AcidVoice::Cutoff, 400.0f}, {AcidVoice::Reso, 0.5f}, {AcidVoice::EnvMod, 0.0f}, {AcidVoice::Decay, 600.0f}, {AcidVoice::Range, range}, {AcidVoice::Volume, 0.0f}},
                                      36, 0, 40000, 40000);
    const std::size_t a = static_cast<std::size_t>(from_ms * 44.1), b = static_cast<std::size_t>(to_ms * 44.1);
    return bright(v, a, b) / rms(v, a, b);
  };
  const double s_early = tilt(0.0f, 5, 50), s_late = tilt(0.0f, 600, 700);
  const double f_early = tilt(1.0f, 5, 50), f_late = tilt(1.0f, 600, 700);
  probe("stock early/late tilt, ratio", s_early, s_late, s_early / s_late);
  probe("fish early/late tilt, ratio", f_early, f_late, f_early / f_late);
  CHECK(s_early / s_late > 1.15);
  CHECK(std::fabs(f_early / f_late - 1.0) < 0.1);

  // The bass: a note at 32 Hz (C1) through the open filter, against one two octaves up, in both ranges.
  const auto bass = [&](float range) {
    const std::vector<P> p = {{AcidVoice::Cutoff, 4500.0f}, {AcidVoice::Reso, 0.0f}, {AcidVoice::EnvMod, 0.0f}, {AcidVoice::Decay, 3000.0f}, {AcidVoice::Range, range}, {AcidVoice::Volume, 0.0f}};
    const std::vector<float> lo = note(p, 24, 0, 40000, 40000), hi = note(p, 48, 0, 40000, 40000);
    return rms(lo, 10000, 30000) / rms(hi, 10000, 30000);
  };
  const double b303 = bass(0.0f), bfish = bass(1.0f);
  probe("bass 32 Hz over 131 Hz: 303, fish, fish in dB", b303, bfish, db(bfish / b303));
  CHECK(bfish > b303);                      // FISH does not cut the bass as the 303 range does
  CHECK(db(bfish / b303) < 6.0);
}

TEST_CASE("behaviour: SUSTAIN holds the amplifier at its level while the gate is open, and the note ends at the gate") {
  constexpr std::uint32_t kGate = 60000;
  const auto run = [&](float sustain) {
    return note({{AcidVoice::Cutoff, 4500.0f}, {AcidVoice::Reso, 0.0f}, {AcidVoice::Decay, 300.0f}, {AcidVoice::EnvMod, 0.0f}, {AcidVoice::AmpDecay, 300.0f}, {AcidVoice::Sustain, sustain}, {AcidVoice::Volume, 0.0f}},
                60, 0, kGate, kGate + 6000);
  };
  const std::vector<float> s0 = run(0.0f), s6 = run(0.6f);
  const double a = rms(s6, 40000, 42000), b = rms(s6, 56000, 58000);
  probe("sustain 0.6: 0.45 s, 1.13 s", a, b, b / a);
  probe("sustain 0 at 1.13 s over sustain 0.6", rms(s0, 56000, 58000) / b);
  CHECK(b > 0.0);
  CHECK(std::fabs(b / a - 1.0) < 0.1);                  // held
  CHECK(rms(s0, 56000, 58000) < 0.1 * b);                // against a decayed note without it
  CHECK(rms(s6, kGate + 2000, kGate + 4000) < 0.05 * b); // and it ends at the gate
}

TEST_CASE("behaviour: the output stays under 1 before Volume on a grid of extreme knobs with OVERDRIVE +36 dB") {
  double worst = 0.0;
  for (const float cutoff : {25.0f, 4500.0f}) {
    for (const float reso : {0.0f, 1.0f}) {
      for (const float envmod : {0.0f, 1.0f}) {
        for (const float accent : {0.0f, 1.0f}) {
          for (const float hires : {1.0f, 2.0f}) {
            for (const float tune : {-12.0f, 12.0f}) {
              for (const float fm : {0.0f, 1.0f}) {
                Rig r;
                for (const P& p : {P{AcidVoice::Cutoff, cutoff}, P{AcidVoice::Reso, reso}, P{AcidVoice::EnvMod, envmod}, P{AcidVoice::Accent, accent}, P{AcidVoice::AccSweep, hires}, P{AcidVoice::Tune, tune},
                                   P{AcidVoice::FilterFm, fm}, P{AcidVoice::Overdrive, 36.0f}, P{AcidVoice::Volume, 0.0f}}) {
                  r.set(p.first, p.second);
                }
                r.script.on(0, 36, dsp::kNoteAccent);
                r.script.on(3000, 43, dsp::kNoteSlide);
                r.script.off(6000, 43);
                const std::vector<float> v = r.render(9000);
                REQUIRE(all_finite(v));
                worst = std::max(worst, peak(v, 0, v.size()));
              }
            }
          }
        }
      }
    }
  }
  probe("worst peak over the grid", worst);
  CHECK(worst < 1.0);
}

TEST_CASE("behaviour: the stock loop and the modded loop agree on the same input, to a small tolerance") {
  const std::vector<P> base = {{AcidVoice::Cutoff, 600.0f}, {AcidVoice::Reso, 0.7f}, {AcidVoice::EnvMod, 0.8f}, {AcidVoice::Volume, 0.0f}};
  const auto run = [&](float fm, bool& modded) {
    Rig r;
    for (const P& p : base) r.set(p.first, p.second);
    r.set(AcidVoice::FilterFm, fm);
    r.script.on(0, 36, dsp::kNoteAccent);
    r.script.on(6000, 43, dsp::kNoteSlide);
    r.script.off(12000, 43);
    const std::vector<float> v = r.render(16000);
    modded = AcidVoiceTestAccess::modded(r.voice);
    return v;
  };
  bool m0 = true, m1 = false;
  const std::vector<float> stock = run(0.0f, m0), mod = run(1.0e-6f, m1);  // a Filter FM that does nothing audible, only puts the voice in the modded loop
  CHECK_FALSE(m0);
  CHECK(m1);
  double err = 0.0;
  for (std::size_t i = 0; i < stock.size(); ++i) err = std::max(err, static_cast<double>(std::fabs(stock[i] - mod[i])));
  probe("loops: max difference, stock peak", err, peak(stock, 0, stock.size()));
  CHECK(err < 1e-3 * peak(stock, 0, stock.size()));
}

TEST_CASE("behaviour: at the top of a sweep, with the ladder pinned at its top cutoff, the stock loop and the modded loop still agree") {
  // The ladder's top is 10.4 kHz and Filter FM's step ceiling (kB0Max) sits on its step, so the modded loop (which caps the modulated step) takes the same step
  // as the stock loop (which has no cap) up to it. With a ceiling below the top's step the modded loop would run darker above it: this is the test of that.
  struct Case { float reso; float wave; float tracking; };
  for (const Case c : {Case{0.0f, 0.0f, 1.0f}, Case{0.7f, 0.0f, 1.0f}, Case{1.0f, 1.0f, 1.0f}}) {
    const auto run = [&](float fm, float& b_top, float& at_top) {
      Rig r;
      r.set(AcidVoice::Range, 1.0f);  // RANGE FISH: the knob goes to 4,800 Hz, and the envelope has three times the reach
      r.set(AcidVoice::Cutoff, 4800.0f);
      r.set(AcidVoice::Reso, c.reso);
      r.set(AcidVoice::EnvMod, 1.0f);
      r.set(AcidVoice::Decay, 3000.0f);
      r.set(AcidVoice::Tracking, c.tracking);
      r.set(AcidVoice::Wave, c.wave);
      r.set(AcidVoice::Volume, 0.0f);
      r.set(AcidVoice::FilterFm, fm);
      r.script.on(0, 84, dsp::kNoteAccent);
      std::vector<float> out;
      b_top = 0.0f;
      at_top = 0.0f;
      for (int blk = 0; blk < 600; ++blk) {
        const std::vector<float> part = r.render(64);
        out.insert(out.end(), part.begin(), part.end());
        const float b = AcidVoiceTestAccess::lad_b(r.voice);
        b_top = std::max(b_top, b);
        at_top += b >= AcidVoiceTestAccess::kB0Max * 0.9995f ? 1.0f : 0.0f;  // blocks spent within 0.05 % of the cap
      }
      return out;
    };
    float b0 = 0.0f, n0 = 0.0f, b1 = 0.0f, n1 = 0.0f;
    const std::vector<float> stock = run(0.0f, b0, n0);
    const std::vector<float> mod = run(1.0e-6f, b1, n1);
    INFO("reso " << c.reso << " wave " << c.wave);
    double err = 0.0;
    for (std::size_t i = 0; i < stock.size(); ++i) err = std::max(err, static_cast<double>(std::fabs(stock[i] - mod[i])));
    probe("top of a sweep: step reached, blocks at the cap, max difference x 1e6", static_cast<double>(b0), static_cast<double>(n0), err * 1e6);
    CHECK(b0 >= AcidVoiceTestAccess::kB0Max * 0.9995f);  // the sweep did reach the top (the test would pass for nothing otherwise)
    CHECK(n0 >= 30.0f);                                   // and stayed there for a while (39 to 41 of the 600 blocks measured)
    CHECK(b1 == b0);                                      // the two loops set the same step
    CHECK(err < 3e-5);  // the oscillator seed's trace (2.8e-6 measured; with the old 16 kHz top the modded loop ran 0.2 to 0.9 away)
  }
}

TEST_CASE("behaviour: STOCK reads RANGE and SUSTAIN as stock, and they come back when STOCK goes off") {
  const auto run = [&](std::vector<P> p, float stock_at_start, float stock_later) {
    Rig r;
    for (const P& q : p) r.set(q.first, q.second);
    r.set(AcidVoice::Stock, stock_at_start);
    r.script.on(0, 36, 0);
    r.script.off(30000, 36);
    r.script.param(30100, AcidVoice::Stock, stock_later);
    r.script.on(36000, 36, 0);
    r.script.off(60000, 36);
    return r.render(66000);
  };
  const std::vector<P> plain = {{AcidVoice::Decay, 300.0f}, {AcidVoice::AmpDecay, 300.0f}, {AcidVoice::EnvMod, 0.0f}, {AcidVoice::Volume, 0.0f}};
  std::vector<P> mods = plain;
  mods.push_back({AcidVoice::Range, 1.0f});
  mods.push_back({AcidVoice::Sustain, 0.7f});
  const std::vector<float> ref = run(plain, 1.0f, 1.0f);
  const std::vector<float> ref_off = run(plain, 0.0f, 0.0f);
  const std::vector<float> on = run(mods, 1.0f, 1.0f);
  const std::vector<float> back = run(mods, 1.0f, 0.0f);
  // With STOCK on throughout the voice is the stock voice, sample for sample.
  {
    double d = 0.0;
    std::size_t first = ref.size();
    for (std::size_t i = 0; i < ref.size(); ++i) {
      if (ref[i] != on[i]) { first = std::min(first, i); d = std::max(d, static_cast<double>(std::fabs(ref[i] - on[i]))); }
    }
    probe("stock on vs default: first differing sample, max diff", static_cast<double>(first), d);
  }
  CHECK(std::equal(ref.begin(), ref.end(), on.begin()));
  // The first note (STOCK on) is the stock note; the second (STOCK off) holds at SUSTAIN and has the FISH range.
  CHECK(std::equal(ref.begin(), ref.begin() + 30000, back.begin()));
  const double held = rms(back, 52000, 54000), stock_level = rms(ref_off, 52000, 54000);
  probe("after STOCK off: sustained level over stock", held, stock_level);
  CHECK(held > 2.5 * std::max(stock_level, 1e-9));
}

TEST_CASE("behaviour: STOCK over mods at their defaults changes nothing, sample for sample") {
  const auto run = [](float stock) {
    Rig r;
    r.set(AcidVoice::Stock, stock);
    r.script.on(0, 36, dsp::kNoteAccent);
    r.script.on(6000, 43, dsp::kNoteSlide);
    r.script.off(12000, 43);
    return r.render(16000);
  };
  const std::vector<float> off = run(0.0f), on = run(1.0f);
  double err = 0.0;
  for (std::size_t i = 0; i < off.size(); ++i) err = std::max(err, static_cast<double>(std::fabs(off[i] - on[i])));
  probe("stock on against off at the defaults: max difference, peak", err, peak(on, 0, on.size()));
  CHECK(err == 0.0);
}

// A motion lane or a morph writes a parameter every block, and each write runs control() early: what goes by time must not hurry with it.
TEST_CASE("behaviour: a parameter written every block does not hurry the accent's sweep or the note's end") {
  const auto run = [](bool stream) {
    Rig r;
    r.set(AcidVoice::Reso, 0.9f);
    r.set(AcidVoice::Volume, 0.0f);
    for (std::uint32_t k = 0; k < 6u; ++k) {
      r.script.on(k * 4000u, 36, dsp::kNoteAccent);
      r.script.off(k * 4000u + 2000u, 36);
    }
    if (stream) {
      for (std::uint32_t t = 0; t < 26000u; t += 40u) {
        r.script.param(t, AcidVoice::Cutoff, 350.0f);  // the value it has: only the early control() differs
      }
    }
    return r.render(26000);
  };
  const std::vector<float> plain = run(false), streamed = run(true);
  double err = 0.0;
  for (std::size_t i = 0; i < plain.size(); ++i) err = std::max(err, static_cast<double>(std::fabs(plain[i] - streamed[i])));
  probe("streamed against plain: max difference, peak", err, peak(plain, 0, plain.size()));
  CHECK(err < 0.02 * peak(plain, 0, plain.size()));
}

// ---- the amplifier's start delay, the cutoff's range on the 303 and the square's edges (docs/VOICE-303.md) ----

namespace {

constexpr std::uint32_t kMs1 = 44;  // about 1 ms at 44.1 kHz
constexpr std::uint32_t kDelay = 194;  // kAmpDelay, 4.4 ms, in samples at 44.1 kHz
std::uint32_t ms(double t) { return static_cast<std::uint32_t>(t * 44.1 + 0.5); }

// A flat voice: the filter open, no resonance, no sweep, a long decay, so the amplifier is all there is to the start of a note.
std::vector<P> flat() {
  return {{AcidVoice::Cutoff, 2394.0f}, {AcidVoice::Reso, 0.0f}, {AcidVoice::EnvMod, 0.0f}, {AcidVoice::Accent, 0.0f},
          {AcidVoice::Decay, 3000.0f}, {AcidVoice::AmpDecay, 3000.0f}, {AcidVoice::Volume, 0.0f}};
}
std::vector<P> with_more(std::vector<P> p, std::initializer_list<P> more) {
  for (const P& q : more) p.push_back(q);
  return p;
}
// The largest |x| in [from, to) samples.
double pk(const std::vector<float>& v, std::uint32_t from, std::uint32_t to) { return peak(v, from, to); }
// The first sample at or after `from` whose magnitude exceeds `level`; v.size() when none.
std::size_t first_above(const std::vector<float>& v, double level, std::size_t from = 0) {
  for (std::size_t i = from; i < v.size(); ++i) {
    if (std::fabs(static_cast<double>(v[i])) > level) return i;
  }
  return v.size();
}


// The amplitude of harmonic `k` of `hz` in v[from, from + n): a Hann-windowed Goertzel at the exact frequency.
double harmonic(const std::vector<float>& v, std::size_t from, std::size_t n, double hz, int k) {
  const double w = 2.0 * 3.14159265358979323846 * hz * k / 44100.0;
  double re = 0.0, im = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double win = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(n));
    const double x = static_cast<double>(v[from + i]) * win;
    re += x * std::cos(w * static_cast<double>(from + i));
    im += x * std::sin(w * static_cast<double>(from + i));
  }
  return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(n);
}

// The largest |x[i] - x[i-1]| in [from, to).
double max_step(const std::vector<float>& v, std::size_t from, std::size_t to) {
  double m = 0.0;
  for (std::size_t i = from + 1; i < to && i < v.size(); ++i) {
    m = std::max(m, std::fabs(static_cast<double>(v[i]) - static_cast<double>(v[i - 1])));
  }
  return m;
}
bool bit_equal(const std::vector<float>& a, const std::vector<float>& b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](float x, float y) { return std::memcmp(&x, &y, sizeof x) == 0; });
}
// The delay, in samples, left after the first block of a note's start: read from the voice after `frames` samples.
unsigned wait_after(std::vector<P> params, std::uint32_t frames, std::uint8_t flags = 0, std::uint32_t second_at = 0, std::uint8_t second_flags = 0) {
  Rig r;
  for (const P& q : params) r.set(q.first, q.second);
  r.script.on(0, 48, flags);
  if (second_at != 0) r.script.on(second_at, 52, second_flags);
  (void)r.render(frames);
  return AcidVoiceTestAccess::start_wait(r.voice);
}

}  // namespace

TEST_CASE("start: a plain note is silent for 4.4 ms, then the amplifier's de-clicker rises") {
  // Mutation (kAmpDelay = 0), run: the first sample above 1e-7 is 1, not 195; the first and second milliseconds after the delay read 0.99 and 0.97 of
  // the plateau (bounds below); start_wait_n reads 0; many checks fail across the start tests.
  const std::vector<float> v = note(flat(), 84, 0, 6000, 6000);  // high, so the de-clicker's rise is not hidden by the note's own period
  const double plateau = pk(v, ms(14), ms(20));
  REQUIRE(plateau > 0.05);
  bool silent = true;
  for (std::uint32_t i = 0; i < kDelay; ++i) silent = silent && v[i] == 0.0f;
  CHECK(silent);
  const std::size_t first = first_above(v, 1e-7);
  probe("plain: first sample above 1e-7", static_cast<double>(first));
  CHECK(first == kDelay + 1u);  // the sample after the delay: the de-clicker's first non-zero output
  const double r56 = pk(v, kDelay, kDelay + kMs1) / plateau, r67 = pk(v, kDelay + kMs1, kDelay + 2u * kMs1) / plateau,
               r710 = pk(v, kDelay + 2u * kMs1, kDelay + 5u * kMs1) / plateau;
  probe("plain: peak 1st, 2nd, 3rd-5th ms after the delay over the plateau", r56, r67, r710);
  CHECK(r56 > 0.1);
  CHECK(r56 < 0.5);
  CHECK(r67 > 0.5);
  CHECK(r67 < 0.95);
  CHECK(r710 > 0.9);
  CHECK(r710 < 1.2);
  // The delay is counted in samples, one block at a time.
  Rig r;
  for (const P& q : flat()) r.set(q.first, q.second);
  r.script.on(0, 60);
  (void)r.render(64);
  CHECK(AcidVoiceTestAccess::start_wait_n(r.voice) == kDelay);
  CHECK(AcidVoiceTestAccess::start_wait(r.voice) == kDelay - 64u);
}

TEST_CASE("start: the delay does not depend on early control() calls") {
  // A parameter written every 10 samples runs control() early each time; the delay counts samples, so the start is where it was.
  const std::vector<float> plain = note(flat(), 60, 0, 6000, 6000);
  Rig r;
  for (const P& q : flat()) r.set(q.first, q.second);
  r.script.on(0, 60);
  r.script.off(6000, 60);
  for (std::uint32_t t = 0; t < 600u; t += 10u) r.script.param(t, AcidVoice::Cutoff, 2394.0f);
  const std::vector<float> streamed = r.render(6000);
  CHECK(first_above(streamed, 1e-7) == first_above(plain, 1e-7));
  bool silent = true;
  for (std::uint32_t i = 0; i < kDelay; ++i) silent = silent && streamed[i] == 0.0f;
  CHECK(silent);
}

TEST_CASE("start: an accented note at ACCENT 0 starts as a plain one; with ACCENT up its own part is there before 4.4 ms") {
  // Mutation (the whole input delayed, accent term included), run: the accented notes below are exactly 0 before the delay's end (pre/post 0.0, first
  // sound at 4,400 us against about 130 us with the fix); the two CHECKs on pre and first_above fail.
  // The filter envelope of an accented note decays at ACC DECAY, a plain one's at DECAY (flat() has EnvMod 0, but the law keeps a little of it): with
  // both at 3000 ms the two notes are the same signal, to the bit, so what is compared is the amplifier's start and nothing else.
  const std::vector<P> same = with_more(flat(), {{AcidVoice::AccDecay, 3000.0f}});
  const std::vector<float> plain = note(same, 84, 0, 6000, 6000);
  const std::vector<float> acc0 = note(same, 84, dsp::kNoteAccent, 6000, 6000);
  bool silent = true;
  for (std::uint32_t i = 0; i < kDelay; ++i) silent = silent && acc0[i] == 0.0f;
  CHECK(silent);
  CHECK(bit_equal(acc0, plain));
  for (const float a : {0.5f, 1.0f}) {
    const std::vector<float> v = note(with_more(flat(), {{AcidVoice::Accent, a}}), 84, dsp::kNoteAccent, 6000, 6000);
    const double post = pk(v, ms(14), ms(20));
    const double pre = pk(v, 0, kDelay - 2u);
    probe("accent knob: peak before 5 ms over the plateau, first above 1e-4 (us)", a, pre / post, static_cast<double>(first_above(v, 1e-4)) / 44.1 * 1000.0);
    CHECK(pre > 0.3 * post);  // the accent's share is there: measured 0.4 to 0.5 of the level after 5 ms at ACCENT 50 % on a recording
    CHECK(pre < 1.05 * post);
    CHECK(first_above(v, 1e-4) < ms(1.5));
    // and a plain note at the same ACCENT is still silent
    const std::vector<float> p = note(with_more(flat(), {{AcidVoice::Accent, a}}), 84, 0, 6000, 6000);
    CHECK(pk(p, 0, kDelay) == 0.0);
  }
}

TEST_CASE("start: a legato, tied, sliding or RANGE FISH note has no delay, and no dip") {
  const std::uint8_t kSlide = dsp::kNoteSlide;
  // 52 starts at 6000 while 48 is held (a tie), or with the slide flag after its end (still sounding).
  CHECK(wait_after(flat(), 64) == kDelay - 64u);                      // the first note starts late
  CHECK(wait_after(flat(), 6064, 0, 6000, 0) == 0u);                  // held: a tie
  CHECK(wait_after(flat(), 6064, 0, 6000, kSlide) == 0u);             // a slide
  CHECK(wait_after(with_more(flat(), {{AcidVoice::Range, 1.0f}}), 64) == 0u);  // RANGE FISH starts at once
  // Fish: first signal within 1 ms.
  const std::vector<float> fish = note(with_more(flat(), {{AcidVoice::Range, 1.0f}}), 60, 0, 6000, 6000);
  CHECK(first_above(fish, 1e-4) < ms(1));
  // No dip at a tie: the level just after the new note is not under the one before it.
  Rig r;
  for (const P& q : flat()) r.set(q.first, q.second);
  r.script.on(0, 48);
  r.script.on(6000, 52);
  r.script.off(12000, 52);
  r.script.off(12000, 48);
  const std::vector<float> v = r.render(12000);
  const double before = rms(v, 6000 - ms(5), 6000), after = rms(v, 6000, 6000 + ms(5));
  probe("tie: rms before, after", before, after);
  CHECK(after > 0.8 * before);
}

TEST_CASE("start: Soft Atk values are the attack with no delay; the delay is OFF's; an accented note keeps it") {
  // Mutation (the delay applies whatever Soft Atk says): the wait left is 130, not 0, for the values, and the first sound is at 195.
  for (const float idx : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f}) {
    CHECK(wait_after(with_more(flat(), {{AcidVoice::SoftAtk, idx}}), 64) == 0u);
    const std::vector<float> v = note(with_more(flat(), {{AcidVoice::SoftAtk, idx}}), 60, 0, 6000, 6000);
    CHECK(first_above(v, 1e-4) < ms(1));
    // An accented note has no Soft Atk: it starts late like OFF.
    CHECK(wait_after(with_more(flat(), {{AcidVoice::SoftAtk, idx}}), 64, dsp::kNoteAccent) == kDelay - 64u);
  }
  // MODS off (Stock) with Soft Atk 5 is Soft Atk OFF (Stock also resets the decays, so both renders are Stock).
  const std::vector<float> off = note(with_more(flat(), {{AcidVoice::Stock, 1.0f}}), 60, 0, 6000, 6000);
  const std::vector<float> stock = note(with_more(flat(), {{AcidVoice::SoftAtk, 5.0f}, {AcidVoice::Stock, 1.0f}}), 60, 0, 6000, 6000);
  CHECK(bit_equal(off, stock));
  CHECK(first_above(stock, 1e-7) == kDelay + 1u);
}

TEST_CASE("start: a restart while the last note still sounds does not click or dip") {
  // Mutation (g0 forced to 0 on a restart: the input drops to 0 and climbs), run: on the plain note the rms after the restart over the one before
  // is 0.57, not 1.57 (bound 0.7), so the dip is caught. The largest step stays 1.05 of the note's own steps, so that bound is not what catches it,
  // and the accented restart reads 1.29 (the accent's share is not held back), so it passes: the plain restart is the one this guards.
  // Both ways a restart arrives: 1.5 ms after the NoteOff, and in the same frame as it (the NoteOff first), which is how the sequencer ends a
  // note at a full gate (seq_sequencer_test: "a full gate ends the note before the next one starts, at the same frame, without a glide").
  for (const std::uint8_t flags : {std::uint8_t{0}, dsp::kNoteAccent}) {
   for (const std::uint32_t gap : {std::uint32_t{0}, ms(1.5)}) {
    Rig r;
    for (const P& q : with_more(flat(), {{AcidVoice::Accent, 0.5f}})) r.set(q.first, q.second);
    const std::uint32_t t0 = 8000;
    r.script.on(0, 48, flags);
    r.script.off(t0, 48);
    r.script.on(t0 + gap, 48, flags);  // while the gate hold still sounds
    r.script.off(t0 + ms(60), 48);
    const std::vector<float> v = r.render(t0 + ms(80));
    const double own = max_step(v, 0, ms(15));  // the note's own start: the filter's envelope is at its brightest (an accented body is darker and quieter)
    const std::uint32_t at = t0 + gap;
    const double around = max_step(v, at - 20, at + ms(10));
    const double before = rms(v, at - ms(5), at), after = rms(v, at, at + ms(5));
    probe("restart: max step around / own steps; rms after / before", around / own, after / before, static_cast<double>(flags));
    CHECK(around < 1.25 * own);
    CHECK(after > 0.7 * before);
    CHECK(all_finite(v));
   }
  }
}

namespace {
// The delay left one sample after a second note's start, the first having been released `gap` samples before it (at 6000): what the voice
// is waiting for when it restarts. The whole delay less that sample (kDelay - 1) is a start from quiet.
unsigned restart_wait(std::uint32_t gap, std::uint8_t flags) {
  Rig r;
  for (const P& q : with_more(flat(), {{AcidVoice::Accent, 0.5f}})) r.set(q.first, q.second);
  r.script.on(0, 48, flags);
  r.script.off(6000, 48);
  r.script.on(6000 + gap, 48, flags);
  (void)r.render(6000 + gap + 1u);  // the last block ends one frame after the restart
  return AcidVoiceTestAccess::start_wait(r.voice);
}
}  // namespace

TEST_CASE("start: the delay is for a note that starts from quiet; on a tail only what avoids a dip, in the same frame as the NoteOff too") {
  // The rule: the amplifier's start waits for the part of the input the output has not got yet. From quiet that is all of it (194 samples); on a
  // tail it is the share the tail does not cover, so there is no dip (the gain would fall to the tail's level and climb) and no extra silence.
  // A NoteOff and a NoteOn in the same frame (a full gate in the sequencer: the NoteOff first, no slide flag) are a restart on a plain note's
  // whole body: the wait left is 7 samples (0.16 ms) one sample in, 0 on an accented note, whose own share is not delayed.
  // Mutation (g0 forced to 0 on a restart, the whole delay every time): 193 in every row; the dip test above reads 0.57 as well.
  const unsigned quiet = kDelay - 1u;
  const unsigned same_plain = restart_wait(0, 0), same_acc = restart_wait(0, dsp::kNoteAccent);
  probe("restart wait: same frame plain, accent", static_cast<double>(same_plain), static_cast<double>(same_acc));
  CHECK(same_plain > 0u);
  CHECK(same_plain <= 10u);
  CHECK(same_acc == 0u);
  unsigned last = same_plain;
  for (const std::uint32_t gap : {ms(1.5), ms(10), ms(20), ms(50), ms(500)}) {
    const unsigned w = restart_wait(gap, 0);
    probe("restart wait: gap in samples, plain", static_cast<double>(gap), static_cast<double>(w));
    CHECK(w >= last);  // the quieter the tail, the more of the delay is needed
    CHECK(w <= quiet);
    last = w;
  }
  CHECK(restart_wait(ms(10), 0) > 30u);
  CHECK(restart_wait(ms(10), 0) < 120u);
  CHECK(restart_wait(ms(20), 0) > 150u);
  CHECK(last == quiet);  // 500 ms later the tail is gone: a start from quiet
  CHECK(restart_wait(ms(500), dsp::kNoteAccent) >= quiet - 2u);  // an accented tail is a hair above quiet after 500 ms
  // A tie or a slide never waits (the held or still sounding note covers it): checked above.
}

TEST_CASE("start: all notes off during the delay leaves no blip of the delayed input") {
  // The first note's input is held for 194 samples; the all-off at 100 ends it. Without the reset the held input is let through at the end of the
  // wait (or at once) and the note is heard after the silence it was asked for. An accented note's own share is not delayed, so it is heard up
  // to the all-off and rings out through the de-clicker and the filter for a few ms: the window for it starts at 400 (9 ms).
  // Mutation (`vca_ = 0; vcf_ = 0;` removed from all_notes_off()): peaks 0.31 (plain) and 0.46 (accent).
  for (const std::uint8_t flags : {std::uint8_t{0}, dsp::kNoteAccent}) {
    Rig r;
    for (const P& q : with_more(flat(), {{AcidVoice::Accent, 0.5f}})) r.set(q.first, q.second);
    r.script.on(0, 60, flags);
    r.script.all_off(100);  // inside the delay
    r.script.on(6000, 60, flags);  // and the next note starts as from quiet
    const std::vector<float> v = r.render(7000);
    const double after = pk(v, flags == 0u ? 128u : 400u, 6000);
    probe("all notes off in the delay: peak after it (flags)", after, static_cast<double>(flags));
    CHECK(after < 1e-3);
    CHECK(AcidVoiceTestAccess::start_wait(r.voice) <= kDelay);
    const std::vector<float> w(v.begin() + 6000, v.end());
    CHECK(first_above(w, 1e-7) <= kDelay + 2u);
    CHECK(pk(w, 0, 1000) > 0.05);
  }
}

TEST_CASE("start: a burst of restarts every millisecond stays bounded, plain and accented") {
  for (const std::uint8_t flags : {std::uint8_t{0}, dsp::kNoteAccent}) {
    Rig r;
    for (const P& q : with_more(flat(), {{AcidVoice::Reso, 0.6f}, {AcidVoice::Accent, 0.8f}})) r.set(q.first, q.second);
    for (std::uint32_t k = 0; k < 200u; ++k) {
      r.script.on(k * ms(1), 36 + static_cast<int>(k % 7u), flags);
      r.script.off(k * ms(1) + ms(0.5), 36 + static_cast<int>(k % 7u));
    }
    std::vector<float> all;
    const char* bad = nullptr;
    float worst = 0.0f;
    for (std::uint32_t t = 0; t < ms(220) && bad == nullptr; t += 64u) {
      const std::vector<float> b = r.render(64);
      all.insert(all.end(), b.begin(), b.end());
      bad = AcidVoiceTestAccess::violation(r.voice, worst);
    }
    CHECK(bad == nullptr);
    CHECK(all_finite(all));
    CHECK(peak(all, 0, all.size()) < 2.0);
  }
}

TEST_CASE("cutoff: on RANGE 303 the knob is clamped to 200..2394 Hz at both ends; on FISH it is not") {
  // Mutation (clamp removed), run: six of the equal pairs below fail (25 against 200, 100 against 200, 3000 against 4500, 2400 against 4500 on 303,
  // and the same two under Stock). With the floor at 314 instead of 200, 200 and 300 would read the same and CHECK_FALSE(300 against 200) would fail.
  const auto at = [](float hz_knob, float range, float stock = 0.0f) {
    return note(with_more(flat(), {{AcidVoice::Cutoff, hz_knob}, {AcidVoice::Range, range}, {AcidVoice::Stock, stock}, {AcidVoice::Wave, 1.0f}}), 48, 0, 8000, 8000);
  };
  CHECK(bit_equal(at(25.0f, 0.0f), at(200.0f, 0.0f)));
  CHECK(bit_equal(at(100.0f, 0.0f), at(200.0f, 0.0f)));
  CHECK(bit_equal(at(3000.0f, 0.0f), at(4500.0f, 0.0f)));
  CHECK(bit_equal(at(2400.0f, 0.0f), at(4500.0f, 0.0f)));  // 2,394.41 Hz is the top: 2400 is already over it
  CHECK_FALSE(bit_equal(at(300.0f, 0.0f), at(200.0f, 0.0f)));
  CHECK_FALSE(bit_equal(at(2000.0f, 0.0f), at(2394.0f, 0.0f)));
  // FISH: the whole 25..4500 Hz
  CHECK_FALSE(bit_equal(at(25.0f, 1.0f), at(200.0f, 1.0f)));
  CHECK_FALSE(bit_equal(at(3000.0f, 1.0f), at(4500.0f, 1.0f)));
  // MODS off means RANGE 303, whatever Range holds (Stock also puts the decays back to their stock values, so the pairs are both Stock).
  CHECK(bit_equal(at(25.0f, 1.0f, 1.0f), at(25.0f, 0.0f, 1.0f)));
  CHECK(bit_equal(at(25.0f, 1.0f, 1.0f), at(200.0f, 1.0f, 1.0f)));
  CHECK(bit_equal(at(4500.0f, 1.0f, 1.0f), at(2400.0f, 1.0f, 1.0f)));
  CHECK_FALSE(bit_equal(at(300.0f, 1.0f, 1.0f), at(200.0f, 1.0f, 1.0f)));
  // The levels of the two ends differ by the filter: a note at 25 on FISH is darker than at 200.
  const double dark = bright(at(25.0f, 1.0f), 3000, 8000), mid = bright(at(200.0f, 1.0f), 3000, 8000);
  probe("FISH: brightness at 25 Hz, at 200 Hz", dark, mid);
  CHECK(dark < mid);
}

TEST_CASE("cutoff: RANGE and MODS switched while a note plays: the clamp adds no click, at both ends of the knob") {
  // Switching RANGE also changes the output chain (high-pass 44 and 24 Hz to 12 Hz, notch off, de-clicker 200 to 1500 Hz) with its state kept, and
  // that alone steps the output by up to 27 % of the peak: eb9a684 (v0.5.1) reads 0.2748 at knob 200 for the same switch and 0.189 and 0.200 at
  // knobs 25 and 100 (measured on the unmodified tree). The bound is that worst reading, 0.275: no knob position steps worse than before. Here the
  // 303 -> FISH switch reads 0.2695 at knobs 25, 100 and 200 alike: the clamp's ends glide to the new range over 30 ms, so the 303 side is the
  // same sound as knob 200 and the chain's own step is all there is (it is unavoidable without ramping the chain's switch; knobs 25 and 100
  // read higher than before for that reason, not the clamp's). Mutation, run: `fresh_ = fresh;` removed from update_range() (the filter jumps
  // instead of ramping) moves the largest reading to 0.286 and fails here; the clamp's own glide is pinned by the test below.
  for (const float knob : {25.0f, 100.0f, 200.0f, 1000.0f, 2394.0f, 4500.0f}) {
    for (const int what : {0, 1}) {  // 0: RANGE 303 -> FISH, 1: FISH -> STOCK (MODS off)
      Rig r;
      for (const P& q : with_more(flat(), {{AcidVoice::Cutoff, knob}, {AcidVoice::Range, what == 0 ? 0.0f : 1.0f}, {AcidVoice::Wave, 1.0f}})) r.set(q.first, q.second);
      r.script.on(0, 48);
      r.script.off(30000, 48);
      const std::uint32_t t = 10000;
      if (what == 0) {
        r.script.param(t, AcidVoice::Range, 1.0f);
      } else {
        r.script.param(t, AcidVoice::Stock, 1.0f);
      }
      const std::vector<float> v = r.render(14000);
      const double over = max_step(v, t - 64, t + ms(20));
      const double level = std::max(pk(v, t - ms(20), t - 64), pk(v, t + ms(30), t + ms(80)));
      probe("switch: knob, what, max step over the switch / the louder level", knob, static_cast<double>(what), over / level);
      CHECK(over < 0.275 * level);
      CHECK(all_finite(v));
    }
  }
}

TEST_CASE("cutoff: the clamp's ends glide to the new range at 0.04 octave per control period, and a new note starts from the targets") {
  // 303 -> FISH moves the floor from 200 Hz (7.644 log2) to 0 (7.6 octaves, 69 ms) and the ceiling from 2394 Hz (11.2255) to 14 (2.8 octaves, 25 ms).
  // Mutation (the ends snapped at once, as with no slew): the first reading below is the target at once and the rate check fails.
  Rig r;
  for (const P& q : with_more(flat(), {{AcidVoice::Cutoff, 1000.0f}, {AcidVoice::Wave, 1.0f}})) r.set(q.first, q.second);
  r.script.on(0, 48);
  r.script.param(4000, AcidVoice::Range, 1.0f);
  r.script.off(30000, 48);
  float prev_lo = 0.0f, prev_hi = 0.0f;
  float max_rate = 0.0f;
  (void)r.render(4000);
  const float lo0 = AcidVoiceTestAccess::clamp_lo(r.voice), hi0 = AcidVoiceTestAccess::clamp_hi(r.voice);
  CHECK(lo0 == doctest::Approx(7.6438562f));
  CHECK(hi0 == doctest::Approx(11.2255f));
  prev_lo = lo0;
  prev_hi = hi0;
  bool mid = false;
  for (int i = 0; i < 60; ++i) {  // 3,840 samples: the floor's 7.6 octaves take 3,056
    (void)r.render(64);  // the switch is at 4000, the first of these blocks
    const float lo = AcidVoiceTestAccess::clamp_lo(r.voice), hi = AcidVoiceTestAccess::clamp_hi(r.voice);
    max_rate = std::max(max_rate, std::max(prev_lo - lo, hi - prev_hi));
    mid = mid || (lo < lo0 - 0.1f && lo > 0.1f);
    prev_lo = lo;
    prev_hi = hi;
  }
  probe("clamp glide: largest octave step over 64 samples, floor, ceiling at the end", static_cast<double>(max_rate), static_cast<double>(prev_lo), static_cast<double>(prev_hi));
  CHECK(mid);  // it passed through intermediate places
  CHECK(max_rate <= 0.04f * 4.0f * 1.01f);  // 64 samples are four control periods
  CHECK(prev_lo == doctest::Approx(0.0f).epsilon(1e-6).scale(1.0));
  CHECK(prev_hi == doctest::Approx(14.0f));
}

TEST_CASE("square: the duty is Open303's 46.88 %, at low and high pitch") {
  // The second harmonic of a pulse is |sin(2 pi d)| / 2 against |sin(pi d)| for the first: 0.098 at 46.88 %, 0.047 at the old 48.5 %. The
  // output chain bends both, the same way in either voice, so the readings below are the voice's own and the bounds sit between the two duties.
  // Mutation (kSqrBias for 48.5 %: 2.0995): note 36 reads 0.041, note 60 reads 0.029, both under their bounds.
  const auto h21 = [](int n) {
    const std::vector<float> v = note(with_more(flat(), {{AcidVoice::Cutoff, 4500.0f}, {AcidVoice::Range, 1.0f}, {AcidVoice::Wave, 1.0f}}), n, 0, 30000, 30000);
    const double f = 440.0 * std::pow(2.0, (n - 69) / 12.0);
    return harmonic(v, 8000, 16384, f, 2) / harmonic(v, 8000, 16384, f, 1);
  };
  const double low = h21(36), high = h21(60);
  probe("square h2/h1 at note 36, 60", low, high);
  // 0.098 is the pulse alone; the chain's bend adds up to a few percent at the low note (0.1013 now, 0.0997 before the amplifier delay moved
  // the level), so the upper bound is 0.105: still far from the old duty's 0.047.
  CHECK(low > 0.065);
  CHECK(low < 0.105);
  CHECK(high > 0.047);
  CHECK(high < 0.072);
}

TEST_CASE("square: the highest notes stay finite and bounded") {
  for (const int n : {96, 108, 120}) {
    for (const float range : {0.0f, 1.0f}) {
      const std::vector<float> v = note(with_more(flat(), {{AcidVoice::Cutoff, 4500.0f}, {AcidVoice::Range, range}, {AcidVoice::Wave, 1.0f}, {AcidVoice::Reso, 0.5f}}), n, dsp::kNoteAccent, 8000, 8000);
      CHECK(all_finite(v));
      CHECK(peak(v, 0, v.size()) < 2.0);
    }
  }
}

// ---- the slide: the 303's one pole on the log (octave) pitch -------------------------------------------------------------------------

namespace {
// A note `from`, held, then a slide to `to` at sample kAt; the voice's pitch state after `ms` of the glide, as the fraction of the interval covered
// in octaves (log2 of the pitch, so the same measure whatever the interval and its direction). `knob_every`: a Cutoff event every that many
// samples through the glide (control() then runs early, each time: the glide must go by the samples that ran, not by the calls).
double glide_fraction(int from, int to, float slide_ms, double ms, std::uint32_t knob_every = 0) {
  constexpr std::uint32_t kAt = 4000;
  Rig r;
  r.set(AcidVoice::Slide, slide_ms);
  r.script.on(0, from);
  r.script.on(kAt, to, dsp::kNoteSlide);  // before the old note's NoteOff, as the sequencer sends it
  r.script.off(kAt, from);
  r.script.off(kAt + 60000, to);
  const std::uint32_t n = static_cast<std::uint32_t>(ms * 44.1 + 0.5);
  for (std::uint32_t k = knob_every; knob_every != 0 && k < n; k += knob_every) {
    r.script.param(kAt + k, AcidVoice::Cutoff, 600.0f + static_cast<float>(k % 5u) * 20.0f);
  }
  (void)r.render(kAt + n);
  const double f0 = 440.0 * std::pow(2.0, (from - 69) / 12.0);
  return std::log2(static_cast<double>(AcidVoiceTestAccess::pitch_hz(r.voice)) / f0) / (static_cast<double>(to - from) / 12.0);
}
}  // namespace

TEST_CASE("slide: at SLIDE 60 the glide is half way, in octaves, after 22 ms x ln 2, up or down, for any interval") {
  // The 303: 100 k into 0.22 uF before the exponential converter, tau 22 ms, in the octave domain (docs/VOICE-303.md 2). At t = tau ln 2 = 15.25 ms
  // the
  // pitch has covered half the interval in octaves: 0.5 for one octave up and down, and for two octaves (a Hz one pole would read 0.29 up, 0.63
  // down).
  const double half = 22.0 * std::log(2.0);
  for (const auto& iv : {std::pair<int, int>{48, 60}, {60, 48}, {48, 72}, {72, 48}, {48, 53}, {55, 48}}) {
    const double x = glide_fraction(iv.first, iv.second, 60.0f, half);
    probe("slide half way: from, to, fraction", iv.first, iv.second, x);
    CHECK(x > 0.45);
    CHECK(x < 0.55);
  }
}

TEST_CASE("slide: tau is 22/60 of the SLIDE time, and the glide ends on the note") {
  for (const float knob : {60.0f, 120.0f, 240.0f}) {
    const double tau = static_cast<double>(knob) * 22.0 / 60.0;
    const double x = glide_fraction(48, 60, knob, tau);
    probe("slide 63 % time: SLIDE, fraction", knob, x);
    CHECK(x > 0.632 - 0.03);
    CHECK(x < 0.632 + 0.03);
    CHECK(glide_fraction(48, 72, knob, tau * 3.0) > 0.93);          // 3 tau: "about 60 ms" at the stock setting
    CHECK(glide_fraction(72, 48, knob, tau * 20.0) == doctest::Approx(1.0).epsilon(1e-4));
  }
}

TEST_CASE("slide: control() running early (a knob moving through the glide) does not speed it up") {
  const double half = 22.0 * std::log(2.0);
  for (const std::uint32_t every : {5u, 7u, 16u, 23u}) {
    const double x = glide_fraction(48, 72, 60.0f, half, every);
    probe("slide half way with knob events every", every, x);
    CHECK(x > 0.45);
    CHECK(x < 0.55);
  }
}

TEST_CASE("slide: a note that starts the voice jumps at once, and a slid note arrives exactly on its pitch") {
  Rig r;
  r.script.on(0, 48);
  r.script.off(2000, 48);
  r.script.on(4000, 60);  // the voice is silent by now: no slide flag, no held note
  r.script.off(4400, 60);
  (void)r.render(4001);
  CHECK(AcidVoiceTestAccess::pitch_hz(r.voice) == AcidVoiceTestAccess::target_hz(r.voice));
  Rig s;
  s.script.on(0, 48);
  s.script.on(4000, 71, dsp::kNoteSlide);
  s.script.off(4000, 48);
  s.script.off(30000, 71);
  (void)s.render(20000);
  CHECK(AcidVoiceTestAccess::pitch_hz(s.voice) == AcidVoiceTestAccess::target_hz(s.voice));
}
