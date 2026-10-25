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

// A setting of the voice other than Replicant's defaults (Cutoff 350 Hz, Decay 750 ms, Tune 0 stay): Reso, Env Mod and Accent as 0..1, and
// the voice's own Volume in dB (the app holds it at 0).
struct Setting {
  float reso = 0.65f;
  float env_mod = 0.6f;
  float accent = 0.7f;
  float volume_db = 0.0f;
};

// The 303 line through the voice alone at Volume 0 dB (as the app holds it), SAW or SQR.
std::vector<float> voice_line(bool square, const Setting& set = Setting{}) {
  std::vector<Step> steps;
  for (const Line& l : kLine) {
    steps.push_back({l.note, l.accent, l.slide, l.rest});
  }
  auto ev = defaults();
  ev.push_back(param(0, Tb303::Volume, set.volume_db));
  ev.push_back(param(0, Tb303::Reso, set.reso));
  ev.push_back(param(0, Tb303::EnvMod, set.env_mod));
  ev.push_back(param(0, Tb303::Accent, set.accent));
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
  // Distortion::trim (a gain per model on its wet signal, a function of Drive) is measured so that the RMS of each model on this line at
  // the default Drive (0.5), Tone (0.6) and Mix 1 matches GRIT's, as the mean in dB of the SAW line and the SQR line: relative to GRIT a
  // square runs 2 to 3 dB hotter than a saw through TUBE, HARD, FUZZ and FOLD, so one trim cannot match both waves (each is within 1.6 dB
  // of GRIT at the default). The next cases check the whole of Drive and two other voices.
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
                  static_cast<double>(drive), names[m], db(static_cast<double>(Distortion::trim(static_cast<unsigned>(m), drive))), off_saw, off_sqr, mean, mix_saw, mix_sqr,
                  a.peak, b.peak);
      if (drive == 0.5f) {
        CHECK(std::fabs(mean) < 0.3);       // matched in the mean of the two waves
        CHECK(std::fabs(off_saw) < 2.5);    // and each wave within 2.5 dB
        CHECK(std::fabs(off_sqr) < 2.5);
        CHECK(std::fabs(mix_saw) < 2.5);
        CHECK(std::fabs(mix_sqr) < 2.5);
      } else {
        CHECK(std::fabs(mean) < 0.3);       // and at full Drive too (M3's fixed trims let FUZZ grow by 5.5 dB on a square)
        CHECK(std::fabs(off_saw) < 2.5);
        CHECK(std::fabs(off_sqr) < 2.5);
      }
    }
  }
}

TEST_CASE("distortion levels: across the whole of Drive the models keep GRIT's level, and none runs hot") {
  // Distortion::trim is measured offline at 21 values of Drive (0, 0.05 .. 1) on this line (src/distortion.cpp): from Drive 0.25 up, the
  // mean in dB of the SAW and SQR lines of each model is GRIT's to a fraction of a dB, at the knots and between them. Below 0.25 the
  // correction fades out (the models are near-transparent there and GRIT's 450 Hz high-pass makes it the odd one). GRIT's own trim holds
  // its wet peak, which grows by 2.5 dB over the default's by Drive 0.8, at the default's.
  const char* names[] = {"GRIT", "TUBE", "HARD", "FUZZ", "FOLD"};
  const auto saw = voice_line(false);
  const auto sqr = voice_line(true);
  double grit_default_peak = 0.0;
  {
    const Level a = through_distortion(saw, 0, 0.5f, 1.0f);
    const Level b = through_distortion(sqr, 0, 0.5f, 1.0f);
    grit_default_peak = std::max(a.peak, b.peak);
  }
  double spread_max = 0.0;
  for (int step = 5; step <= 20; ++step) {
    const float drive = static_cast<float>(step) * 0.05f + ((step & 1) != 0 ? 0.012f : 0.0f);  // half of the values between knots
    const Level g_saw = through_distortion(saw, 0, drive, 1.0f);
    const Level g_sqr = through_distortion(sqr, 0, drive, 1.0f);
    if (drive >= 0.5f) {
      CHECK(std::max(g_saw.peak, g_sqr.peak) < grit_default_peak * 1.03);
    }
    for (int m = 1; m < 5; ++m) {
      const Level a = through_distortion(saw, m, drive, 1.0f);
      const Level b = through_distortion(sqr, m, drive, 1.0f);
      const double mean = 0.5 * (db(a.rms / g_saw.rms) + db(b.rms / g_sqr.rms));
      spread_max = std::max(spread_max, std::fabs(mean));
      INFO(names[m] << " drive " << drive);
      CHECK(std::fabs(mean) < 0.6);
      CHECK(std::fabs(db(a.rms / g_saw.rms)) < 3.0);  // each wave alone: up to 2.6 dB off at Drive 0.25, 1.6 dB from the default up
      CHECK(std::fabs(db(b.rms / g_sqr.rms)) < 3.0);
      CHECK(std::max(a.peak, b.peak) < (m == 4 ? 1.3 : 0.9));  // the wet peak, before the slot's level: FOLD folds up to 1.25, the others sit lower
    }
  }
  std::printf("[levels] across Drive 0.25..1 the worst mean offset of a model from GRIT is %.2f dB\n", spread_max);
}

TEST_CASE("distortion levels: Drive 0 .. 0.2, where the correction fades out to M3's trims, the models stay at or above GRIT, by at most 8 dB") {
  // GRIT's 450 Hz high-pass makes it far quieter than the others at low Drive, where they are near-transparent; the table does not pull
  // them down to it there (the correction is faded in over Drive 0 .. 0.25). Measured on the reference line, the mean of the two waves
  // against GRIT: +0.3 .. +1.9 dB for TUBE, HARD and FUZZ at 0 and 0.2, up to +5.6 dB at 0.05; FOLD +7.5 dB at 0, falling to +1.2 at 0.2.
  const char* names[] = {"GRIT", "TUBE", "HARD", "FUZZ", "FOLD"};
  const auto saw = voice_line(false);
  const auto sqr = voice_line(true);
  for (int step = 0; step <= 4; ++step) {
    const float drive = static_cast<float>(step) * 0.05f;
    const Level g_saw = through_distortion(saw, 0, drive, 1.0f);
    const Level g_sqr = through_distortion(sqr, 0, drive, 1.0f);
    for (int m = 1; m < 5; ++m) {
      const Level a = through_distortion(saw, m, drive, 1.0f);
      const Level b = through_distortion(sqr, m, drive, 1.0f);
      const double mean = 0.5 * (db(a.rms / g_saw.rms) + db(b.rms / g_sqr.rms));
      INFO(names[m] << " drive " << drive);
      CHECK(mean > -0.5);
      CHECK(mean < 8.0);
      CHECK(std::max(a.peak, b.peak) < 1.3);  // FOLD at Drive 0 folds up to 1.2
    }
  }
}

TEST_CASE("distortion levels: the table was fitted on one line; on a resonant, accented, deep-sweep voice the models sit a few dB above GRIT and none runs hot") {
  // Reso, Accent and Env Mod at 100 %: the same notes, a very different spectrum. GRIT's high-pass reads this line lower than the
  // other models read it, so from Drive 0.25 up each wave is +0.5 .. +4.2 dB above GRIT's (measured; on the reference line they are
  // within 2.6 dB either way, and the mean of the two waves within 0.1 dB). The wet peaks, which are what the headroom is about, stay
  // where they are on the reference line: GRIT 1.24 (at Drive 0.5), FOLD 1.28, TUBE .84, HARD .81, FUZZ .77.
  const char* names[] = {"GRIT", "TUBE", "HARD", "FUZZ", "FOLD"};
  const Setting hot{1.0f, 1.0f, 1.0f, 0.0f};
  const auto saw = voice_line(false, hot);
  const auto sqr = voice_line(true, hot);
  for (int step = 5; step <= 20; ++step) {
    const float drive = static_cast<float>(step) * 0.05f;
    const Level g_saw = through_distortion(saw, 0, drive, 1.0f);
    const Level g_sqr = through_distortion(sqr, 0, drive, 1.0f);
    CHECK(std::max(g_saw.peak, g_sqr.peak) < 1.35);
    for (int m = 1; m < 5; ++m) {
      const Level a = through_distortion(saw, m, drive, 1.0f);
      const Level b = through_distortion(sqr, m, drive, 1.0f);
      INFO(names[m] << " drive " << drive);
      CHECK(db(a.rms / g_saw.rms) > -1.0);
      CHECK(db(a.rms / g_saw.rms) < 5.0);
      CHECK(db(b.rms / g_sqr.rms) > -1.0);
      CHECK(db(b.rms / g_sqr.rms) < 5.0);
      CHECK(std::max(a.peak, b.peak) < (m == 4 ? 1.4 : 1.0));
    }
  }
}

TEST_CASE("distortion levels: a voice at twice the reference line (+6 dB) - GRIT's wet peak is recorded, the others stay under full scale") {
  // GRIT keeps the original's Pade tanh, which does not saturate beyond |x| = 3 (the default sound is unchanged by leaving it; see
  // Distortion's header): on a voice twice as loud its wet peak reaches 1.71 (Drive 0.55 .. 0.6; 1.24 on the reference line). That is
  // the number to watch: when GRIT is bounded some day, or the table changes, this case moves and says so. The others do not follow
  // the voice's level into the peak: TUBE .88, HARD .84, FUZZ .79, FOLD 1.36.
  const Setting loud{0.65f, 0.6f, 0.7f, 6.0f};
  const auto saw = voice_line(false, loud);
  const auto sqr = voice_line(true, loud);
  double grit_peak = 0.0;
  double worst[5] = {};
  for (int step = 0; step <= 20; ++step) {
    const float drive = static_cast<float>(step) * 0.05f;
    for (int m = 0; m < 5; ++m) {
      const Level a = through_distortion(saw, m, drive, 1.0f);
      const Level b = through_distortion(sqr, m, drive, 1.0f);
      worst[m] = std::max(worst[m], std::max(a.peak, b.peak));
    }
  }
  grit_peak = worst[0];
  std::printf("[levels] +6 dB voice, wet peak over Drive: GRIT %.2f TUBE %.2f HARD %.2f FUZZ %.2f FOLD %.2f\n", worst[0], worst[1], worst[2], worst[3], worst[4]);
  CHECK(grit_peak > 1.5);
  CHECK(grit_peak < 1.85);
  CHECK(worst[1] < 1.0);
  CHECK(worst[2] < 1.0);
  CHECK(worst[3] < 1.0);
  CHECK(worst[4] < 1.5);
}
