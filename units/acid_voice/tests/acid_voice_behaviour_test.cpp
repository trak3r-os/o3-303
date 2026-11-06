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
