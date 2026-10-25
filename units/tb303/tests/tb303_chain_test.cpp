// SPDX-License-Identifier: GPL-3.0-or-later
// The voice and the distortion together in a dsp::Engine, as the app wires them (docs/M3-PLAN.md): slot 0 = TB303, insert 0 = DISTORTION,
// then the slot's level (the SOUND page's Volume, after the distortion as in Replicant). A short acid line is rendered; if the
// environment variable O3_303_WAV_DIR names a directory, the line is written there as tb303_line.wav (16-bit stereo, 44.1 kHz) for
// listening and for looking at its spectrum.
#include <doctest/doctest.h>

#include "metrics.h"
#include "ref_engine.h"
#include "support/need.h"
#include "tb303_rig.h"

#include <o3_303/units/distortion/distortion.h>

#include <array>
#include <cfenv>
#include <cstdio>

using namespace tb303_test;
using o3_303::Distortion;
using o3_303::Tb303;

namespace {

constexpr std::uint32_t kStep = 5292;  // a sixteenth at 125 BPM
constexpr std::uint32_t kSampleRate = 44100;

struct Line {
  int note;
  bool accent = false;
  bool slide = false;  // held into the next step
  bool rest = false;
};

// The line: two bars, in A minor, with accents, slides and rests.
const Line kLine[] = {
    {33}, {33}, {45, true}, {33, false, true}, {36}, {33}, {40, true, true}, {38}, {31}, {31, false, false, true}, {43, true}, {31},
    {33}, {0, false, false, true}, {36, true, true}, {40}, {33}, {33, true}, {45}, {33, false, true}, {36}, {0, false, false, true},
    {40, true}, {38}, {31, false, true}, {43}, {31, true}, {31}, {33}, {36, false, true}, {45, true}, {33},
};

void write_line(Script& s) {
  const std::uint32_t n = sizeof(kLine) / sizeof(kLine[0]);
  for (std::uint32_t i = 0; i < n; ++i) {  // all the NoteOns first: at one frame a slide's NoteOn goes before the old note's NoteOff
    const Line& l = kLine[i];
    if (l.rest) {
      continue;
    }
    const bool slid_in = i > 0 && kLine[i - 1].slide && !kLine[i - 1].rest;
    s.on(i * kStep, l.note, static_cast<std::uint8_t>((l.accent ? o3sdk::dsp::kNoteAccent : 0) | (slid_in ? o3sdk::dsp::kNoteSlide : 0)));
  }
  for (std::uint32_t i = 0; i < n; ++i) {
    const Line& l = kLine[i];
    if (l.rest) {
      continue;
    }
    const bool glide_next = l.slide && i + 1 < n && !kLine[i + 1].rest;
    s.off(glide_next ? (i + 1) * kStep : i * kStep + kStep / 2, l.note);
  }
}

std::array<double, kBands> band_shares(const std::vector<float>& x) {
  const auto e = band_energies(x, kSampleRate);
  std::array<double, kBands> band{};
  for (std::size_t i = 0; i < e.size(); ++i) {
    band[i % kBands] += e[i];
  }
  double total = 0.0;
  for (double b : band) {
    total += b;
  }
  for (double& b : band) {
    b = total > 0.0 ? b / total : 0.0;
  }
  return band;
}

struct Chain {
  o3sdk::dsp::Engine engine;
  Tb303 voice;
  Distortion dist;
  Script script;  // the voice's notes
  Script fx;      // the distortion's parameter changes
  o3sdk::dsp::UnitId v, d;
  Chain() : v(engine.attach(voice)), d(engine.attach(dist)) {
    script.set_unit(v);
    fx.set_unit(d);
    (void)engine.set_source(0, v);
    (void)engine.set_insert(0, 0, d);
    (void)engine.add_hook(script);
    (void)engine.add_hook(fx);
    (void)engine.set_param(v, Tb303::Volume, 0.0f);       // the app holds the voice at 0 dB ...
    (void)engine.set_mix(0, o3sdk::dsp::MixField::Level, 0.708f);  // ... and the SOUND page's Volume (-3 dB) is the slot's level
  }
  void render(std::uint32_t total, std::vector<float>& l, std::vector<float>& r) {
    engine.start();
    float bl[64];
    float br[64];
    for (std::uint32_t done = 0; done < total; done += 64) {
      const std::uint32_t n = (total - done) < 64 ? (total - done) : 64;
      engine.render(nullptr, nullptr, bl, br, n);
      l.insert(l.end(), bl, bl + n);
      r.insert(r.end(), br, br + n);
    }
  }
};

}  // namespace

TEST_CASE("tb303 chain: a 303 line through the voice, the distortion and the level; WAV and spectrum") {
  Chain c;
  write_line(c.script);
  std::vector<float> l, r;
  const std::uint32_t total = 32 * kStep + 3 * kSampleRate / 2;
  c.render(total, l, r);
  CHECK(l == r);  // mono through the chain
  const double pk = peak(l, 0, l.size());
  const double lvl = rms(l, 0, l.size());
  double mean = 0.0;
  for (float s : l) {
    mean += static_cast<double>(s);
  }
  mean /= static_cast<double>(l.size());
  std::printf("[chain] 32 steps at 125 BPM, GRIT drive 0.5 mix 0.85: peak %.3f, RMS %.3f, mean %.5f\n", pk, lvl, mean);
  CHECK(pk > 0.3);
  CHECK(pk < 1.0);
  CHECK(lvl > 0.05);
  CHECK(std::fabs(mean) < 0.01);  // the DC blocker: the voice's own offset and the distortion's do not reach the output
  for (float s : l) {
    REQUIRE(std::isfinite(s));
  }
  CHECK(peak(l, total - kSampleRate / 4, total) < 1e-3);  // and the tail ends in silence

  // The spectrum in octave bands (60 Hz upward): the distortion pushes the squelch into 1..4 kHz, the tone low-pass (4.8 kHz at 60 %)
  // leaves next to nothing above 3.8 kHz, and the bass is there.
  const auto share = band_shares(l);
  std::printf("[chain] octave bands from 60 Hz, share of energy:");
  for (double b : share) {
    std::printf(" %.1f%%", 100.0 * b);
  }
  std::printf("\n");
  CHECK((share[0] + share[1] + share[2] + share[3]) > 0.05);  // the bass and low mids
  CHECK((share[6] + share[7]) < 0.05);                         // above 3.8 kHz
  const bool wrote = write_wav("tb303_line.wav", l, r);
  const bool wanted = std::getenv("O3_303_WAV_DIR") != nullptr;
  CHECK((wrote || !wanted));
}

TEST_CASE("tb303 chain: every distortion type, switched on and off during the line, stays finite and under the clip") {
  for (int m = 0; m < 5; ++m) {
    INFO("model " << m);
    Chain c;
    write_line(c.script);
    (void)c.engine.set_param(c.d, Distortion::Type, static_cast<float>(m));
    (void)c.engine.set_param(c.d, Distortion::Drive, 1.0f);
    (void)c.engine.set_param(c.d, Distortion::Mix, 1.0f);
    c.fx.param(10 * kStep, Distortion::Active, 0.0f);
    c.fx.param(13 * kStep, Distortion::Active, 1.0f);
    std::vector<float> l, r;
    c.render(16 * kStep, l, r);
    for (float s : l) {
      REQUIRE(std::isfinite(s));
      REQUIRE(std::fabs(s) < 1.0f);
    }
    CHECK(rms(l, 2 * kStep, 8 * kStep) > 0.05);
  }
}

TEST_CASE("tb303 chain: the voice alone (distortion off) is bass and mids, and has the DC the blocker takes out") {
  Chain c;
  write_line(c.script);
  (void)c.engine.set_param(c.d, Distortion::Active, 0.0f);
  std::vector<float> l, r;
  c.render(32 * kStep + kSampleRate, l, r);
  const auto share = band_shares(l);
  std::printf("[chain] voice only, octave bands from 60 Hz, share of energy:");
  for (double b : share) {
    std::printf(" %.1f%%", 100.0 * b);
  }
  std::printf("\n");
  CHECK((share[0] + share[1] + share[2] + share[3] + share[4]) > 0.8);  // below 1.9 kHz
  (void)write_wav("tb303_line_dry.wav", l, r);

  // The same line with the blocker's job undone: the voice alone has an offset (its saw is bent), so a plain unit would not be silent of DC.
  Rig rig;
  write_line(rig.script);
  rig.set(Tb303::Volume, 0.0f);
  const auto raw = rig.render(32 * kStep);
  double mean = 0.0;
  for (float s : raw) {
    mean += static_cast<double>(s);
  }
  mean /= static_cast<double>(raw.size());
  std::printf("[chain] voice alone mean %.4f (the chain's output: below 0.01)\n", mean);
  CHECK(mean > 0.005);
}

TEST_CASE("tb303 chain: a release tail through every distortion type raises no underflow") {
  for (int m = 0; m < 5; ++m) {
    INFO("model " << m);
    Chain c;
    c.script.on(100, 36, o3sdk::dsp::kNoteAccent);
    c.script.off(kStep, 36);
    (void)c.engine.set_param(c.d, Distortion::Type, static_cast<float>(m));
    (void)c.engine.set_param(c.d, Distortion::Drive, 0.7f);
    c.engine.start();
    std::vector<float> l, r;
    std::feclearexcept(FE_ALL_EXCEPT);
    float bl[64];
    float br[64];
    for (std::uint32_t done = 0; done < 4 * kSampleRate; done += 64) {
      c.engine.render(nullptr, nullptr, bl, br, 64);
      l.insert(l.end(), bl, bl + 64);
    }
    const bool underflow = std::fetestexcept(FE_UNDERFLOW) != 0;
    CHECK_FALSE(underflow);
    CHECK(peak(l, 3 * kSampleRate, l.size()) == 0.0);
  }
}

// ---- the level of the five models ---------------------------------------------------------------------------------------------

namespace {

// The 303 line through the voice alone at Volume 0 dB (as the app holds it), SAW or SQR.
std::vector<float> voice_line(bool square) {
  std::vector<Step> steps;
  for (const Line& l : kLine) {
    steps.push_back({l.note, l.accent, l.slide, l.rest});
  }
  auto ev = defaults();
  ev.push_back(param(0, Tb303::Volume, 0.0f));
  ev.push_back(param(0, Tb303::Wave, square ? 1.0f : 0.0f));
  const auto notes = steps_to_events(steps, 125.0);
  ev.insert(ev.end(), notes.begin(), notes.end());
  Tb303 u;
  return render_unit(u, ev, 32 * kStep + kSampleRate * 3 / 2);
}

struct Level {
  double rms;
  double peak;
};

// The distortion alone on `in` (a mono signal) at the given Drive, the default Tone (0.6) and the given Mix.
Level through_distortion(const std::vector<float>& in, int model, float drive, float mix) {
  using namespace o3sdk::dsp;
  Distortion d;
  d.prepare(static_cast<float>(kSampleRate));
  Transport tr;
  float zero[64] = {};
  float dummy[64];
  Event start[5] = {param_change(Distortion::Type, static_cast<float>(model)), param_change(Distortion::Drive, drive),
                    param_change(Distortion::Tone, 0.6f), param_change(Distortion::Mix, mix), param_change(Distortion::Active, 1.0f)};
  tr.frames = 0;
  d.process(ProcessArgs{start, 5, zero, zero, dummy, dummy, 0, &tr});
  std::vector<float> out(in.size());
  std::vector<float> r(in.size());
  for (std::size_t pos = 0; pos < in.size(); pos += 64) {
    const auto n = static_cast<std::uint32_t>(in.size() - pos < 64 ? in.size() - pos : 64);
    tr.frames = n;
    d.process(ProcessArgs{nullptr, 0, in.data() + pos, in.data() + pos, out.data() + pos, r.data() + pos, n, &tr});
  }
  return Level{rms(out, 0, out.size()), peak(out, 0, out.size())};
}

double db(double ratio) { return 20.0 * std::log10(ratio); }

}  // namespace

TEST_CASE("distortion levels: the models match GRIT's level on a 303 line at the default Drive, and stay within stated bounds at full Drive") {
  // Distortion::kTrim is a fixed gain per model on its wet signal, measured so that the RMS of each model on this line at the default
  // Drive (0.5), Tone (0.6) and Mix 1 matches GRIT's, as the mean in dB of the SAW line and the SQR line: a square runs 4 to 6 dB hotter
  // through TUBE, HARD and FUZZ than a saw, so one trim cannot match both waves: each is within about 3 dB, FUZZ being the outlier.
  const char* names[] = {"GRIT", "TUBE", "HARD", "FUZZ", "FOLD"};
  const auto saw = voice_line(false);
  const auto sqr = voice_line(true);
  for (const float drive : {0.5f, 1.0f}) {
    const Level g_saw = through_distortion(saw, 0, drive, 1.0f);
    const Level g_sqr = through_distortion(sqr, 0, drive, 1.0f);
    const Level g_def_saw = through_distortion(saw, 0, drive, 0.85f);
    const Level g_def_sqr = through_distortion(sqr, 0, drive, 0.85f);
    for (int m = 0; m < 5; ++m) {
      const Level a = through_distortion(saw, m, drive, 1.0f);
      const Level b = through_distortion(sqr, m, drive, 1.0f);
      const double off_saw = db(a.rms / g_saw.rms);
      const double off_sqr = db(b.rms / g_sqr.rms);
      const double mean = 0.5 * (off_saw + off_sqr);
      // The unit's output at its default Mix (0.85), what the app hears: the dry fifth is the same for every model.
      const double mix_saw = db(through_distortion(saw, m, drive, 0.85f).rms / g_def_saw.rms);
      const double mix_sqr = db(through_distortion(sqr, m, drive, 0.85f).rms / g_def_sqr.rms);
      std::printf("[levels] drive %.1f %-4s trim %+5.1f dB: vs GRIT saw %+5.1f sqr %+5.1f dB (mean %+4.1f), at Mix 0.85 saw %+5.1f sqr %+5.1f; peaks %.2f %.2f\n",
                  static_cast<double>(drive), names[m], db(static_cast<double>(Distortion::kTrim[m])), off_saw, off_sqr, mean, mix_saw, mix_sqr,
                  a.peak, b.peak);
      if (drive == 0.5f) {
        CHECK(std::fabs(mean) < 0.3);       // matched in the mean of the two waves
        CHECK(std::fabs(off_saw) < 3.5);    // and each wave within 3.5 dB (FUZZ: about 3)
        CHECK(std::fabs(off_sqr) < 3.5);
        CHECK(std::fabs(mix_saw) < 3.5);
        CHECK(std::fabs(mix_sqr) < 3.5);
      } else {
        CHECK(off_saw > -2.5);              // at full Drive the models part again: FUZZ keeps growing with a square (+5.5 dB)
        CHECK(off_saw < 6.0);
        CHECK(off_sqr > -2.5);
        CHECK(off_sqr < 6.0);
      }
    }
  }
}
