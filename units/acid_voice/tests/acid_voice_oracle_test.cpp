// SPDX-License-Identifier: GPL-3.0-or-later
// Oracle: the voice against Open303 itself (third_party/open303, MIT, pinned commit in its README), built on the host in double. What is
// compared, and what is not:
//  (a) the ladder: the voice's held note against TeeBeeFilter in its TB_303 mode at four times the rate, behind Open303's pre-filter
//      high-pass and in front of its output chain, on a grid of Cutoff and Reso: the spectrum of a sawtooth at 27.5 Hz line by line.
//      The voice's input level is its own, so one constant (the median difference in dB) is fitted per cell and must be the same over the
//      grid; what is checked is the shape: the peak's frequency and height, and every line within a tolerance.
//  (b) the cutoff law: the voice's envelope scale and offset against Open303::calculateEnvModScalerAndOffset, to float accuracy.
//  (c) the amplifier: the peak of an accented note over the peak of a plain one, against (1 + main + boost) / (1 + main) of the voice's own constants.
//  (d) the output's all-pass, high-pass and notch: the same comparison at 16.35 Hz, where they decide the shape.
//  (e) the ladder's coefficients: the voice's b, k and g (ladder_coeffs) against TeeBeeFilter's calculateCoefficientsApprox4 in double at twice the
//      sample rate, at 201 cutoffs from the floor to the top (10.4 kHz), Reso 0, 0.3, 0.7 and 1, with and without HI RES: to 1e-6 (2.6e-7 measured), and kB0Max
//      against the b of the top (0.2507).
// Not compared: the envelopes' shapes and the oscillator (the voice's are its own, tested in acid_voice_engine_test and the mods test).
//
// Deviations from Open303, each fitted to the Din Sync recording (docs/VOICE-303.md 2, "Deviations from Open303") and each carried into the
// reference of this file as a named constant, so that what is still Open303's is still compared exactly and nothing is loosened:
//  - the law's offset: 0.15 x c + 0.2943912 (c: where the knob sits between 314 and 2,394 Hz, 0..1) where Open303 has 0.048292930943553 x c +
//    0.2943912. (b) adds the difference to the reference's offset, and (a) settles the reference's filter at the voice's cutoff, not Open303's.
//    What (b) no longer proves is that the offset's slope in c is Open303's; the scale, and the offset at c = 0, still are.
//  - the amplifier's accent share: the filter envelope is in the amplifier at 0.10 (Open303: 0.45) and the accent adds max(0, 2.6 Accent - 0.6)
//    (Open303: 4 Accent). (c) takes both into its wanted ratio. What it no longer proves is Open303's 0.45 and 4; it still proves the structure:
//    the accent's share adds to the main input's at the same weight, at any knob position, to 4 %.
//  The start delay, the square's level, the accent and hold time constants are not in the paths compared here.
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

// The voice's two deviations from TeeBeeFilter in the ladder (docs/VOICE-303.md, section 2): the feedback high-pass at 15 Hz instead of 150 Hz, and
// the
// resonance scaled by s = fc / sqrt(fc^2 + 500^2) (kResRollHz) in both the feedback and the output gain. Both of Open303's k and g are linear in its
// skewed resonance r, so the reference follows by scaling r: this returns the setResonance() percent whose skew gives r x s.
constexpr double kFbHpHz = 15.0;
constexpr double kResRollHz = 500.0;
double roll(double fc) {
  return fc / std::sqrt(fc * fc + kResRollHz * kResRollHz);
}
double rolled_reso_percent(double reso_percent, double fc) {
  const double skewed = (1.0 - std::exp(-3.0 * 0.01 * reso_percent)) / (1.0 - std::exp(-3.0));
  const double r = skewed * roll(fc);
  return -100.0 * std::log(1.0 - r * (1.0 - std::exp(-3.0))) / 3.0;
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
  tee.setFeedbackHighpassCutoff(kFbHpHz);
  tee.setMode(rosic::TeeBeeFilter::TB_303);
  tee.setCutoff(cutoff);
  tee.setResonance(rolled_reso_percent(reso_percent, cutoff));
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


// Open303's own coefficients (TeeBeeFilter::calculateCoefficientsApprox4 in TB_303 mode, in double) at a cutoff and a Resonance: the step b0, the
// feedback k and the output gain g (without getSample's factor 2).
struct CoeffProbe : rosic::TeeBeeFilter {
  void at(double hz, double reso_percent, double& b, double& kk, double& gg) {
    setMode(TB_303);
    setSampleRate(2.0 * kFs);  // the voice runs two sub-steps per sample
    setResonance(reso_percent, false);
    cutoff = hz;  // set directly: setCutoff() floors at 200 Hz, the voice's floor is 25 Hz (a deviation of its own, docs/VOICE-303.md)
    calculateCoefficientsApprox4();
    b = b0;
    kk = k;
    gg = g;
  }
};

// The law's offset slope in c: the voice's 0.15 against Open303's 0.048292930943553 (Din Sync recording).
constexpr double kLawOffFDelta = 0.15 - 0.048292930943553;

// c of the law: where the knob sits over c0..c1 on a log scale, as the voice reads it (kLog2C0 and kLog2C1 are the voice's own rounded values).
double law_c(double clamped_cutoff) {
  const double c = (std::log2(clamped_cutoff) - 8.2938) / (11.2255 - 8.2938);
  return std::min(std::max(c, 0.0), 1.0);
}

// The cutoff the voice settles at on a plain note once its filter envelope has fallen to nothing: the knob, lowered by the law's offset
// (Open303: cutoff x 2^(scaler x (env - offset)), env = 0), held at the same floor.
double settled_cutoff(double knob, double env_mod) {
  constexpr double kC0 = 3.138152786059267e+002;
  constexpr double kC1 = 2.394411986817546e+003;
  LawProbe open;
  double scaler = 0.0, offset = 0.0;
  const double cl = std::min(std::max(knob, kC0), kC1);
  open.law(cl, 100.0 * env_mod, scaler, offset);
  offset += kLawOffFDelta * law_c(cl);  // the voice's offset slope, not Open303's (the filter itself is compared at the same cutoff)
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
      // 1.7 measured at most (cutoff 2400, Reso 0.9: the peak 0.7 % off on a steep peak between two lines 27.5 Hz apart); 1.5 before the resonance
      // scaling
      CHECK(m.worst_db < 2.0);
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
        // Open303's offset plus the one documented deviation (a slope in c, 0 at c = 0).
        const double want_off = offset + kLawOffFDelta * law_c(cl);
        const double eo = std::fabs(got_off - want_off);
        probe("law: scale err, offset err, scale", es, eo, want_scale);
        worst = std::max(worst, std::max(es, eo));
        CHECK(es < 2e-5 * (1.0 + std::fabs(want_scale)));
        CHECK(eo < 2e-5);
        if (cl <= kC0) {
          CHECK(std::fabs(got_off - offset) < 2e-5);  // at c = 0 the offset is Open303's own, undeviated
        }
      }
    }
  }
  probe("law: worst", worst);
}

TEST_CASE("oracle (e): the ladder's step, feedback and gain are Open303's, in double, from the floor to the 10.4 kHz top, and the step cap sits on the top's own step") {
  // The voice's float polynomials (acid_voice.cpp ladder_coeffs) against TeeBeeFilter::calculateCoefficientsApprox4 at 2 x 44.1 kHz (the voice's sub-step rate), TB_303 mode, in double, at the
  // same cutoff (2^x Hz, x the float the voice takes). They differ by the float rounding of the polynomials and by exp2_fast (relative error under 1e-7), so the
  // tolerance is small. The voice's top is 10.4 kHz because the approximations hold only up to a normalised cutoff of pi/4 (88.2 kHz: about 11 kHz), and Open303's
  // polynomials are the exact reference there, which a looser check (a bound on the response) could not be.
  constexpr int kN = 200;
  double worst_b = 0.0, worst_k = 0.0, worst_g = 0.0, top_b = 0.0;
  for (const float reso : {0.0f, 0.3f, 0.7f, 1.0f}) {
    for (const float acc_sweep : {0.0f, 2.0f}) {  // ACC SWEEP ON and HI RES (the feedback doubled)
      Rig r;
      r.set(AcidVoice::Reso, reso);
      r.set(AcidVoice::AccSweep, acc_sweep);
      (void)r.render(64);
      const double xmin = static_cast<double>(o3_303::AcidVoiceTestAccess::x_min(r.voice));
      const double xmax = static_cast<double>(o3_303::AcidVoiceTestAccess::x_max(r.voice));
      const double fb_scale = acc_sweep == 2.0f ? 2.0 : 1.0;
      CoeffProbe open;
      for (int i = 0; i <= kN; ++i) {
        const float x = static_cast<float>(xmin + (xmax - xmin) * static_cast<double>(i) / kN);
        float b = 0.0f, k = 0.0f, g = 0.0f;
        o3_303::AcidVoiceTestAccess::coeffs(r.voice, x, b, k, g);
        double ob = 0.0, ok = 0.0, og = 0.0;
        const double fc = std::exp2(static_cast<double>(x));
        open.at(fc, rolled_reso_percent(100.0 * static_cast<double>(reso), fc), ob, ok, og);
        worst_b = std::max(worst_b, std::fabs(static_cast<double>(b) - ob) / ob);
        worst_g = std::max(worst_g, std::fabs(static_cast<double>(g) - 2.0 * og) / (2.0 * og));
        if (reso > 0.0f) {
          worst_k = std::max(worst_k, std::fabs(static_cast<double>(k) - fb_scale * ok) / (fb_scale * ok));
        } else {
          CHECK(k == 0.0f);
        }
        if (i == kN) top_b = ob;
      }
      // Above the top the voice holds the top's coefficients.
      float b1 = 0.0f, k1 = 0.0f, g1 = 0.0f, b2 = 0.0f, k2 = 0.0f, g2 = 0.0f;
      o3_303::AcidVoiceTestAccess::coeffs(r.voice, static_cast<float>(xmax), b1, k1, g1);
      o3_303::AcidVoiceTestAccess::coeffs(r.voice, static_cast<float>(xmax) + 2.0f, b2, k2, g2);
      CHECK(b1 == b2);
      CHECK(k1 == k2);
      CHECK(g1 == g2);
    }
  }
  probe("coefficients: worst relative error b, k, g (x 1e6)", worst_b * 1e6, worst_k * 1e6, worst_g * 1e6);
  probe("coefficients: Open303's b at the top, kB0Max", top_b, static_cast<double>(o3_303::AcidVoiceTestAccess::kB0Max));
  CHECK(worst_b < 1e-6);  // 2.5e-7 measured: float rounding and exp2_fast
  CHECK(worst_k < 1e-6);  // 1.9e-7
  CHECK(worst_g < 1e-6);  // 2.6e-7
  // The step cap of Filter FM's modulated step (the modded loop's only) is the top's own step with a trace of room, so it never changes the stock loop's step.
  CHECK(static_cast<double>(o3_303::AcidVoiceTestAccess::kB0Max) >= top_b);
  CHECK(static_cast<double>(o3_303::AcidVoiceTestAccess::kB0Max) < top_b * 1.002);
}

TEST_CASE("oracle (c): an accented note peaks at (1 + 0.10 + boost) / (1 + 0.10) of a plain one, boost = max(0, 2.6 Accent - 0.6): Open303's amplifier with the Din Sync constants") {
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
    // Open303: (1 + 0.45 + 4 a) / (1.45). The voice's two constants are fitted to the Din Sync recording; the structure is Open303's.
    const double boost = std::max(0.0, 2.6 * static_cast<double>(a) - 0.6);
    const double want = (1.0 + 0.10 + boost) / (1.0 + 0.10);
    probe("amp: accent, ratio, wanted", static_cast<double>(a), acc / plain, want);
    REQUIRE(plain > 0.01);
    CHECK(acc / plain == doctest::Approx(want).epsilon(0.04));
  }
}
