// SPDX-License-Identifier: GPL-3.0-or-later
// Oracle: the voice against Open303 itself (third_party/open303, MIT, pinned commit in its README), built on the host in double. What is
// compared, and what is not:
//  (a) the ladder: the voice's held note against TeeBeeFilter in its TB_303 mode at four times the rate, behind Open303's pre-filter
//      high-pass and in front of its output chain, on a grid of Cutoff and Reso: the spectrum of a sawtooth at 27.5 Hz line by line.
//      The voice's input level is its own, so one constant (the median difference in dB) is fitted per cell and must be the same over the
//      grid; what is checked is the shape: the peak's frequency and height, and every line within a tolerance.
//  (b) the cutoff law: the voice's envelope scale and offset against Open303::calculateEnvModScalerAndOffset, to float accuracy.
//  (c) the amplifier: the peak of an accented note over the peak of a plain one, against 0.45 and 4 x Accent.
//  (d) the output's all-pass, high-pass and notch: the same comparison at 16.35 Hz, where they decide the shape.
// Not compared: the envelopes' shapes and the oscillator (the voice's are its own, tested in acid_voice_engine_test and the mods test).
#include <doctest/doctest.h>

#include "acid_voice_access.h"
#include "acid_voice_rig.h"

#include <climits>  // the vendored headers lean on these two
#include <cstring>

#include <rosic_Open303.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace acid_voice_test;
using o3_303::AcidVoice;

namespace {

constexpr double kFs = 44100.0;
constexpr double kPi = 3.14159265358979323846;

void probe(const char* what, double a, double b = 0.0, double c = 0.0) {
  if (std::getenv("O3_303_PROBE") != nullptr) {
    std::printf("probe: %-40s %12.5f %12.5f %12.5f\n", what, a, b, c);
  }
}

// Open303's signal path after the oscillator, as getSample() runs it, from a sawtooth of frequency f0 (double): the pre-filter high-pass
// and the filter at four times the rate (the anti-aliasing decimator is left out: the lines compared are far below it), then the
// all-pass, the high-pass and the notch at the base rate. Returns the base-rate samples.
std::vector<double> reference_saw(double f0, double cutoff, double reso_percent, std::size_t frames) {
  constexpr int kOver = 4;
  rosic::TeeBeeFilter tee;
  rosic::OnePoleFilter hp1, hp2, ap;
  rosic::BiquadFilter notch;
  hp1.setMode(rosic::OnePoleFilter::HIGHPASS);
  hp2.setMode(rosic::OnePoleFilter::HIGHPASS);
  ap.setMode(rosic::OnePoleFilter::ALLPASS);
  notch.setMode(rosic::BiquadFilter::BANDREJECT);
  hp2.setSampleRate(kFs);
  ap.setSampleRate(kFs);
  notch.setSampleRate(kFs);
  hp1.setSampleRate(kOver * kFs);
  tee.setSampleRate(kOver * kFs);
  hp1.setCutoff(44.486);
  hp2.setCutoff(24.167);
  ap.setCutoff(14.008);
  notch.setFrequency(7.5164);
  notch.setBandwidth(4.7);
  tee.setFeedbackHighpassCutoff(150.0);
  tee.setMode(rosic::TeeBeeFilter::TB_303);
  tee.setCutoff(cutoff);
  tee.setResonance(reso_percent);
  std::vector<double> out(frames);
  double phase = 0.0;
  const double inc = f0 / (kOver * kFs);
  for (std::size_t i = 0; i < frames; ++i) {
    double y = 0.0;
    for (int j = 0; j < kOver; ++j) {
      double x = 2.0 * phase - 1.0;
      phase += inc;
      phase -= std::floor(phase);
      x = hp1.getSample(-x);
      y = tee.getSample(x);
    }
    y = ap.getSample(y);
    y = hp2.getSample(y);
    y = notch.getSample(y);
    out[i] = y;
  }
  return out;
}

// Open303's law, through a subclass (the function and the two results are protected).
struct LawProbe : rosic::Open303 {
  void law(double cutoff_hz, double env_mod_percent, double& scaler, double& offset) {
    setCutoff(cutoff_hz);
    setEnvMod(env_mod_percent);
    scaler = envScaler;
    offset = envOffset;
  }
};


// The cutoff the voice settles at on a plain note once its filter envelope has fallen to nothing: the knob, lowered by the law's offset
// (Open303: cutoff x 2^(scaler x (env - offset)), env = 0), held at the same floor.
double settled_cutoff(double knob, double env_mod) {
  constexpr double kC0 = 3.138152786059267e+002;
  constexpr double kC1 = 2.394411986817546e+003;
  LawProbe open;
  double scaler = 0.0, offset = 0.0;
  open.law(std::min(std::max(knob, kC0), kC1), 100.0 * env_mod, scaler, offset);
  return std::max(knob * std::pow(2.0, -scaler * offset), std::min(knob, 200.0));
}

// The voice's held note at `midi` through the engine: a plain, steady note (Env Mod 0 and a Decay of 30 ms leave the cutoff at settled_cutoff() once the envelope has fallen,
// Sustain 1 holds the amplifier, Decay at its shortest ends the lift of the filter envelope before the part measured), the oscillator
// scaled down by Overdrive -18 dB so that the amplifier's ceiling stays out of the way.
std::vector<float> voice_saw(int midi, float cutoff, float reso, std::size_t frames) {
  Rig r;
  r.set(AcidVoice::Wave, 0.0f);
  r.set(AcidVoice::Cutoff, cutoff);
  r.set(AcidVoice::Reso, reso);
  r.set(AcidVoice::EnvMod, 0.0f);
  r.set(AcidVoice::Decay, 30.0f);
  r.set(AcidVoice::Sustain, 1.0f);
  r.set(AcidVoice::Overdrive, -18.0f);
  r.set(AcidVoice::Volume, 0.0f);
  r.script.on(0, midi);
  return r.render(static_cast<std::uint32_t>(frames));
}

// The magnitude of the Hann-windowed DTFT of x[from, from + len) at f Hz.
template <typename T>
double line(const std::vector<T>& x, std::size_t from, std::size_t len, double f) {
  std::complex<double> acc(0.0, 0.0);
  const double w = 2.0 * kPi * f / kFs;
  for (std::size_t i = 0; i < len; ++i) {
    const double h = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(len));
    acc += static_cast<double>(x[from + i]) * h * std::polar(1.0, -w * static_cast<double>(from + i));
  }
  return std::abs(acc);
}

struct Compared {
  double k_db = 0.0;      // the median of the voice's line over the reference's, dB
  double worst_db = 0.0;  // the largest |difference - k| over the lines compared
  double pk_hz_err = 0.0; // relative error of the peak's frequency (parabolic over three lines)
  double pk_db_err = 0.0; // the peak's height over the reference's, less k
  double pk_hz = 0.0;
  bool has_peak = false;
  double out_peak = 0.0;
};

// Compares the voice's lines with the reference's at the harmonics of f0 between lo and hi Hz.
Compared compare_lines(int midi, double f0, float cutoff, float reso, double lo, double hi, std::size_t periods) {
  const std::size_t warm = static_cast<std::size_t>(kFs * 1.5);
  const std::size_t len = static_cast<std::size_t>(std::lround(kFs / f0 * static_cast<double>(periods)));
  const auto u = voice_saw(midi, cutoff, reso, warm + len);
  const auto r = reference_saw(f0, settled_cutoff(static_cast<double>(cutoff), 0.0), 100.0 * static_cast<double>(reso), warm + len);
  Compared c;
  c.out_peak = peak(u, warm, warm + len);
  std::vector<double> fr, du, dr;
  for (unsigned n = 1; static_cast<double>(n) * f0 <= hi; ++n) {
    const double f = static_cast<double>(n) * f0;
    if (f < lo) {
      continue;
    }
    fr.push_back(f);
    // The line's level plus the sawtooth's 1/n fall, so that the filter's own peak shows (a flat input would put it at the top).
    const double tilt = 20.0 * std::log10(static_cast<double>(n));
    du.push_back(20.0 * std::log10(line(u, warm, len, f) + 1e-12) + tilt);
    dr.push_back(20.0 * std::log10(line(r, warm, len, f) + 1e-12) + tilt);
    if (std::getenv("O3_303_DUMP") != nullptr) {
      std::printf("line %8.1f Hz  unit %8.2f  ref %8.2f  diff %7.2f\n", f, du.back(), dr.back(), du.back() - dr.back());
    }
  }
  // The lines more than 50 dB under the reference's loudest are noise of the float arithmetic: left out.
  const double top = *std::max_element(dr.begin(), dr.end());
  std::vector<double> diff;
  std::vector<std::size_t> kept;
  for (std::size_t i = 0; i < fr.size(); ++i) {
    if (dr[i] > top - 50.0) {
      diff.push_back(du[i] - dr[i]);
      kept.push_back(i);
    }
  }
  std::vector<double> sorted = diff;
  std::sort(sorted.begin(), sorted.end());
  c.k_db = sorted[sorted.size() / 2];
  for (const double d : diff) {
    c.worst_db = std::max(c.worst_db, std::fabs(d - c.k_db));
  }
  // The peak: the loudest line of the reference above 4 f0, refined over three lines.
  const auto peak_of = [&](const std::vector<double>& d, double& hz, double& db) {
    std::size_t m = 0;
    for (std::size_t i = 1; i + 1 < d.size(); ++i) {
      if (d[i] > d[m] || m == 0) {
        m = i;
      }
    }
    const double a = d[m - 1], b = d[m], g = d[m + 1];
    const double den = a - 2.0 * b + g;
    const double off = den != 0.0 ? 0.5 * (a - g) / den : 0.0;
    hz = fr[m] + off * (fr[1] - fr[0]);
    db = b - 0.25 * (a - g) * off;
  };
  double hu = 0.0, du_pk = 0.0, hr = 0.0, dr_pk = 0.0;
  peak_of(du, hu, du_pk);
  peak_of(dr, hr, dr_pk);
  c.has_peak = true;
  c.pk_hz = hr;
  c.pk_hz_err = std::fabs(hu - hr) / hr;
  c.pk_db_err = std::fabs((du_pk - dr_pk) - c.k_db);
  return c;
}

}  // namespace

TEST_CASE("oracle (a): the ladder's response is TeeBeeFilter's TB_303 in peak frequency and peak height, on a grid of Cutoff and Reso") {
  const float cutoffs[] = {350.0f, 700.0f, 1400.0f, 2400.0f};
  const float resos[] = {0.0f, 0.4f, 0.65f, 0.9f};
  double k_lo = 1e9, k_hi = -1e9;
  for (const float c : cutoffs) {
    for (const float rs : resos) {
      const Compared m = compare_lines(21, 27.5, c, rs, 60.0, 6000.0, 16);
      probe("ladder cell: k dB, worst dB, peak hz err", m.k_db, m.worst_db, m.pk_hz_err);
      probe("ladder cell: peak dB err, peak hz, out peak", m.pk_db_err, m.pk_hz, m.out_peak);
      CHECK(m.out_peak < 0.6);  // the amplifier's ceiling is not in play
      k_lo = std::min(k_lo, m.k_db);
      k_hi = std::max(k_hi, m.k_db);
      CHECK(m.worst_db < 1.5);
      // A resonance peak that the lines (27.5 Hz apart) can place: from Reso 0.65, and a cutoff whose settled value is above the first lines.
      if (rs >= 0.65f && c >= 700.0f) {
        CHECK(m.pk_hz_err < 0.03);
        CHECK(m.pk_db_err < 1.5);
      }
    }
  }
  probe("ladder: k over the grid, lo hi", k_lo, k_hi);
  CHECK(k_hi - k_lo < 1.5);
}

TEST_CASE("oracle (d): the output's all-pass, high-pass and notch are Open303's, at 16.35 Hz and up") {
  // MIDI 12 is 16.35 Hz: the lines below 200 Hz are shaped by the high-passes and the notch; the ladder is flat there.
  const double f0 = 440.0 * std::pow(2.0, -57.0 / 12.0);
  const Compared m = compare_lines(12, f0, 1400.0f, 0.0f, 8.0, 300.0, 24);
  probe("chain: k dB, worst dB", m.k_db, m.worst_db);
  CHECK(m.worst_db < 1.0);
}

TEST_CASE("oracle (b): the cutoff law is Open303's, inside c0..c1 and clamped outside, stock and FISH, to float accuracy") {
  constexpr double kC0 = 3.138152786059267e+002;
  constexpr double kC1 = 2.394411986817546e+003;
  LawProbe open;
  const float cutoffs[] = {25.0f, 200.0f, 314.0f, 350.0f, 700.0f, 1000.0f, 1500.0f, 2394.0f, 3000.0f, 4500.0f};
  const float env_mods[] = {0.0f, 0.2f, 0.6f, 1.0f};
  double worst = 0.0;
  for (const float c : cutoffs) {
    for (const float e : env_mods) {
      for (const float fish : {0.0f, 1.0f}) {
        Rig r;
        r.set(AcidVoice::Cutoff, c);
        r.set(AcidVoice::EnvMod, e);
        r.set(AcidVoice::Range, fish);
        (void)r.render(64);
        const double cl = std::min(std::max(static_cast<double>(c), kC0), kC1);  // the law holds its ends outside
        double scaler = 0.0, offset = 0.0, scaler0 = 0.0, offset0 = 0.0;
        open.law(cl, 100.0 * static_cast<double>(e), scaler, offset);
        open.law(cl, 0.0, scaler0, offset0);
        // FISH: three times the part of the scaler that grows with Env Mod, and none of the constant.
        const double want_scale = fish != 0.0f ? 3.0 * (scaler - scaler0) : scaler;
        const double got_scale = static_cast<double>(o3_303::AcidVoiceTestAccess::env_scale(r.voice));
        const double got_off = static_cast<double>(o3_303::AcidVoiceTestAccess::env_off(r.voice));
        const double es = std::fabs(got_scale - want_scale);
        const double eo = std::fabs(got_off - offset);
        probe("law: scale err, offset err, scale", es, eo, want_scale);
        worst = std::max(worst, std::max(es, eo));
        CHECK(es < 2e-5 * (1.0 + std::fabs(want_scale)));
        CHECK(eo < 2e-5);
      }
    }
  }
  probe("law: worst", worst);
}

TEST_CASE("oracle (c): an accented note peaks at (1 + 0.45 + 4 Accent) / (1 + 0.45) of a plain one, the amplifier of Open303") {
  const auto peak_of_note = [](bool accent, float accent_amt) {
    Rig r;
    r.set(AcidVoice::Wave, 0.0f);
    r.set(AcidVoice::Reso, 0.0f);
    r.set(AcidVoice::EnvMod, 0.0f);     // with FISH: the filter does not move
    r.set(AcidVoice::Range, 1.0f);
    r.set(AcidVoice::Decay, 3000.0f);   // the envelopes hardly fall within the 20 ms measured
    r.set(AcidVoice::AccDecay, 3000.0f);
    r.set(AcidVoice::AmpDecay, 4000.0f);
    r.set(AcidVoice::AccSweep, 0.0f);   // the sweep's lift of the cutoff is another matter
    r.set(AcidVoice::Overdrive, -18.0f);
    r.set(AcidVoice::Volume, 0.0f);
    r.set(AcidVoice::Accent, accent_amt);
    r.script.on(0, 33, accent ? dsp::kNoteAccent : 0);
    const auto o = r.render(4410);
    return peak(o, 441, 1323);  // 10 to 30 ms: the de-clicker has settled, the envelope has not moved
  };
  for (const float a : {0.0f, 0.35f, 0.7f, 1.0f}) {
    const double plain = peak_of_note(false, a);
    const double acc = peak_of_note(true, a);
    const double want = (1.0 + 0.45 + 4.0 * static_cast<double>(a)) / (1.0 + 0.45);
    probe("amp: accent, ratio, wanted", static_cast<double>(a), acc / plain, want);
    REQUIRE(plain > 0.01);
    CHECK(acc / plain == doctest::Approx(want).epsilon(0.04));
  }
}
