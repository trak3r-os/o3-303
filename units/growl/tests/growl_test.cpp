// SPDX-License-Identifier: GPL-3.0-or-later
// GROWL (units/growl) driven directly, block by block: the table, the formant peaks of each vowel against the header's table, the comb's
// resonance at the note, the block size not mattering, no click on a jump of Vowel, Note or Mix, Mix 0 being the input bit for bit, and a
// stable, finite, denormal-free feedback at the limits.
#include <doctest/doctest.h>

#include "support/need.h"

#include <o3_303/units/growl/growl.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

namespace dsp = o3sdk::dsp;
using o3_303::Growl;

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 44100.0;

struct Ev {
  std::uint32_t at;
  std::uint16_t id;
  float value;
};

struct Stereo {
  std::vector<float> l;
  std::vector<float> r;
};

// The unit driven block by block, events at their absolute frames.
Stereo run(Growl& g, const Stereo& in, const std::vector<Ev>& events, std::uint32_t block = 64, bool in_place = true) {
  const std::size_t n = in.l.size();
  Stereo work = in;
  Stereo dst;
  if (!in_place) {
    dst.l.assign(n, std::numeric_limits<float>::quiet_NaN());
    dst.r.assign(n, std::numeric_limits<float>::quiet_NaN());
  }
  std::vector<dsp::Event> evs;
  dsp::Transport tr;
  for (std::size_t pos = 0; pos < n; pos += block) {
    const auto frames = static_cast<std::uint32_t>(std::min<std::size_t>(block, n - pos));
    tr.frames = frames;
    evs.clear();
    for (const Ev& e : events) {
      if (e.at >= pos && e.at < pos + frames) {
        evs.push_back(dsp::param_change(e.id, e.value, static_cast<std::uint16_t>(e.at - pos)));
      }
    }
    float* const il = work.l.data() + pos;
    float* const ir = work.r.data() + pos;
    float* const ol = in_place ? il : dst.l.data() + pos;
    float* const orr = in_place ? ir : dst.r.data() + pos;
    const dsp::ProcessArgs a{evs.data(), static_cast<std::uint32_t>(evs.size()), il, ir, ol, orr, frames, &tr};
    g.process(a);
  }
  return in_place ? work : dst;
}

// The unit prepared with GAIN at 0 dB (its default is the level match, tests/fx_level_test.cpp): the tests below measure the filter and the
// comb as they were. The GAIN tests at the end use the defaults.
void prep(Growl& g) {
  g.prepare(static_cast<float>(kRate));
  // set() is private: the event path. Mix is 0, so this one silent frame only stores the parameter (and snaps it).
  const dsp::Event e = dsp::param_change(Growl::Gain, 0.0f, 0);
  float l = 0.0f, r = 0.0f;
  dsp::Transport tr;
  tr.frames = 1;
  g.process(dsp::ProcessArgs{&e, 1, &l, &r, &l, &r, 1, &tr});
}

Stereo mono(std::vector<float> v) { return Stereo{v, v}; }

std::vector<float> sine(std::size_t n, double hz, double amp = 0.5) {
  std::vector<float> v(n);
  for (std::size_t i = 0; i < n; ++i) {
    v[i] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
  }
  return v;
}

// A bass-like signal: a 55 Hz saw (bandlimited by summation to 40 harmonics) plus a little noise, peak about 0.6.
std::vector<float> bass(std::size_t n, double f0 = 55.0, std::uint32_t seed = 1) {
  std::vector<float> v(n, 0.0f);
  std::uint32_t s = seed;
  for (std::size_t i = 0; i < n; ++i) {
    double acc = 0.0;
    for (int h = 1; h <= 40; ++h) {
      acc += std::sin(2.0 * kPi * f0 * h * static_cast<double>(i) / kRate) / h;
    }
    s = s * 1664525u + 1013904223u;
    const double noise = (static_cast<double>(s >> 8) / 8388608.0 - 1.0) * 0.02;
    v[i] = static_cast<float>(0.35 * acc + noise);
  }
  return v;
}

double rms(const std::vector<float>& v, std::size_t from, std::size_t to) {
  double acc = 0.0;
  for (std::size_t i = from; i < to; ++i) {
    acc += static_cast<double>(v[i]) * static_cast<double>(v[i]);
  }
  return std::sqrt(acc / static_cast<double>(to - from));
}

double max_step(const std::vector<float>& v, std::size_t from, std::size_t to) {
  double m = 0.0;
  for (std::size_t i = from + 1; i < to && i < v.size(); ++i) {
    m = std::max(m, std::fabs(static_cast<double>(v[i]) - static_cast<double>(v[i - 1])));
  }
  return m;
}

bool all_finite(const Stereo& s) {
  for (std::size_t i = 0; i < s.l.size(); ++i) {
    if (!std::isfinite(s.l[i]) || !std::isfinite(s.r[i])) {
      return false;
    }
  }
  return true;
}

// The steady-state gain of the unit at a sine of `hz`: the events (Mix last, so the settings are snapped) then `settle` frames, then the
// RMS of the next `measure` frames against the input's.
double gain_at(const std::vector<Ev>& setup, double hz, std::size_t settle = 6000, std::size_t measure = 4410) {
  Growl g;
  prep(g);
  const Stereo in = mono(sine(settle + measure, hz));
  const Stereo out = run(g, in, setup);
  return rms(out.l, settle, settle + measure) / rms(in.l, settle, settle + measure);
}

std::vector<Ev> formant_only(float vowel, float q) {
  return {{0, Growl::Vowel, vowel},          {0, Growl::FormantQ, q}, {0, Growl::FormantMix, 1.0f}, {0, Growl::CombMix, 0.0f},
          {0, Growl::CombFeedback, 0.0f},    {0, Growl::Mix, 1.0f}};
}

std::vector<Ev> comb_only(float note, float tune, float fb) {
  return {{0, Growl::FormantMix, 0.0f}, {0, Growl::CombMix, 1.0f}, {0, Growl::CombFeedback, fb},
          {0, Growl::Note, note},       {0, Growl::CombTune, tune}, {0, Growl::Mix, 1.0f}};
}

}  // namespace

TEST_CASE("growl: the table is valid, in the contract's order, and Mix defaults to 0") {
  Growl g;
  const auto& info = g.info();
  CHECK(info.kind == dsp::UnitKind::Effect);
  CHECK(std::strcmp(info.name, "GROWL") == 0);
  NEED(info.param_count == Growl::kParamCount);
  CHECK(Growl::kParamCount == 9);
  CHECK(Growl::Vowel == 0);
  CHECK(Growl::FormantQ == 1);
  CHECK(Growl::FormantMix == 2);
  CHECK(Growl::CombTune == 3);
  CHECK(Growl::CombFeedback == 4);
  CHECK(Growl::CombMix == 5);
  CHECK(Growl::Note == 6);
  CHECK(Growl::Mix == 7);
  CHECK(Growl::Gain == 8);  // appended: a parameter id is its index
  for (unsigned p = 0; p < info.param_count; ++p) {
    INFO(info.params[p].name);
    CHECK(o3sdk::core::param_desc_valid(info.params[p]));
  }
  CHECK(info.params[Growl::Mix].def == 0.0f);  // wired and untouched is transparent
  CHECK(info.params[Growl::CombTune].min == -24.0f);
  CHECK(info.params[Growl::CombTune].max == 24.0f);
  CHECK(info.params[Growl::CombFeedback].min == doctest::Approx(-0.95f));
  CHECK(info.params[Growl::CombFeedback].max == doctest::Approx(0.95f));
  CHECK(info.params[Growl::Note].max == 127.0f);
  CHECK(info.params[Growl::Gain].min == -12.0f);
  CHECK(info.params[Growl::Gain].max == 18.0f);
  CHECK(info.params[Growl::Gain].def == o3_303::kGrowlGainDefaultDb);
}

TEST_CASE("growl: the vowel table is A E I O U at 0, 1/4, 1/2, 3/4, 1 and the in-betweens are linear") {
  const double want[Growl::kVowels][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};
  for (unsigned v = 0; v < Growl::kVowels; ++v) {
    float hz[3];
    float lv[3];
    Growl::formants(static_cast<float>(v) * 0.25f, hz, lv);
    for (unsigned k = 0; k < 3; ++k) {
      CHECK(static_cast<double>(hz[k]) == doctest::Approx(want[v][k]));
      CHECK(lv[k] > 0.0f);
      CHECK(lv[k] <= 1.0f);
    }
    CHECK(lv[0] == 1.0f);
  }
  float a[3], b[3], mid[3], l[3];
  Growl::formants(0.0f, a, l);
  Growl::formants(0.25f, b, l);
  Growl::formants(0.125f, mid, l);
  for (unsigned k = 0; k < 3; ++k) {
    CHECK(mid[k] == doctest::Approx(0.5f * (a[k] + b[k])));
  }
  float lo[3], hi[3];
  Growl::formants(-3.0f, lo, l);
  Growl::formants(0.0f, a, l);
  Growl::formants(9.0f, hi, l);
  Growl::formants(1.0f, b, l);
  for (unsigned k = 0; k < 3; ++k) {
    CHECK(lo[k] == a[k]);
    CHECK(hi[k] == b[k]);
  }
}

TEST_CASE("growl: the formant peaks land where the table says, for every vowel") {
  constexpr float kQ = 8.0f;
  for (unsigned v = 0; v < Growl::kVowels; ++v) {
    CAPTURE(v);
    const float vowel = static_cast<float>(v) * 0.25f;
    float hz[3];
    float lv[3];
    Growl::formants(vowel, hz, lv);
    // The response on a 1/24 octave grid from 150 Hz to 5 kHz.
    std::vector<double> f;
    std::vector<double> gain;
    const auto setup = formant_only(vowel, kQ);
    for (double x = 150.0; x < 5000.0; x *= std::pow(2.0, 1.0 / 24.0)) {
      f.push_back(x);
      gain.push_back(gain_at(setup, x));
    }
    for (unsigned k = 0; k < 3; ++k) {
      CAPTURE(k);
      // A local maximum of the response within 6 % of the formant (the grid is 2.9 % apart).
      bool found = false;
      for (std::size_t i = 1; i + 1 < f.size(); ++i) {
        if (gain[i] > gain[i - 1] && gain[i] >= gain[i + 1] && std::fabs(f[i] / static_cast<double>(hz[k]) - 1.0) < 0.06) {
          found = true;
        }
      }
      CHECK(found);
    }
    // The first formant's level: its centre passes Makeup x level, give or take the other bands' skirts.
    const double g1 = gain_at(setup, static_cast<double>(hz[0]));
    CHECK(g1 == doctest::Approx(static_cast<double>(Growl::kMakeup * lv[0])).epsilon(0.2));
  }
}

TEST_CASE("growl: Formant Q narrows the bands") {
  const float vowel = 0.0f;
  float hz[3];
  float lv[3];
  Growl::formants(vowel, hz, lv);
  const double off = static_cast<double>(hz[0]) * 1.25;
  const double wide = gain_at(formant_only(vowel, 2.0f), off);
  const double narrow = gain_at(formant_only(vowel, 16.0f), off);
  CHECK(wide > 1.5 * narrow);
  CHECK(gain_at(formant_only(vowel, 2.0f), static_cast<double>(hz[0])) == doctest::Approx(gain_at(formant_only(vowel, 16.0f), static_cast<double>(hz[0]))).epsilon(0.25));
}

TEST_CASE("growl: the comb resonates at the note's frequency and its harmonics, and Tune moves it by semitones") {
  const auto hz_of = [](double semitones) { return 440.0 * std::pow(2.0, (semitones - 69.0) / 12.0); };
  for (const double note : {45.0, 57.0, 38.0}) {
    CAPTURE(note);
    const double f0 = hz_of(note);
    const auto setup = comb_only(static_cast<float>(note), 0.0f, 0.8f);
    constexpr std::size_t kSettle = 30000;
    // The peak: scan +-2 % around f0 and take the best.
    double best = 0.0;
    double best_f = 0.0;
    for (int i = -10; i <= 10; ++i) {
      const double f = f0 * (1.0 + 0.002 * i);
      const double g = gain_at(setup, f, kSettle);
      if (g > best) {
        best = g;
        best_f = f;
      }
    }
    CHECK(best_f == doctest::Approx(f0).epsilon(0.006));
    CHECK(best > 0.9);                                       // peak-normalised: unity at the resonance
    CHECK(gain_at(setup, 2.0 * f0, kSettle) > 0.85);          // and its harmonics
    CHECK(gain_at(setup, 1.5 * f0, kSettle) < 0.2);           // (1 - g) / (1 + g) = 0.11 between them
    CHECK(gain_at(setup, 0.5 * f0, kSettle) < 0.2);
  }
  // Tune +12 on note 45 is note 57.
  const double f57 = hz_of(57.0);
  const auto tuned = comb_only(45.0f, 12.0f, 0.8f);
  CHECK(gain_at(tuned, f57, 30000) > 0.9);
  CHECK(gain_at(tuned, 1.5 * f57, 30000) < 0.2);
  // A negative Feedback resonates at the odd multiples of half the note.
  const double f45 = hz_of(45.0);
  const auto negative = comb_only(45.0f, 0.0f, -0.8f);
  CHECK(gain_at(negative, f45, 30000) < 0.2);
  CHECK(gain_at(negative, 0.5 * f45, 30000) > 0.85);
}

TEST_CASE("growl: the comb's delay is the period of the note, folded up an octave when the line is too short, and held at the top") {
  const float fs = 44100.0f;
  CHECK(Growl::comb_delay(69.0f, fs) == doctest::Approx(fs / 440.0f));
  CHECK(Growl::comb_delay(57.0f, fs) == doctest::Approx(2.0f * fs / 440.0f));
  CHECK(Growl::comb_delay(36.0f, fs) == doctest::Approx(fs / 65.4064f).epsilon(1e-4));
  const float low = Growl::comb_delay(0.0f, fs);  // 8.18 Hz: 5,391 frames, folded to 1,347 (32.7 Hz, 24 semitones up... two octaves)
  CHECK(low <= Growl::kMaxDelay);
  CHECK(low > 0.5f * Growl::kMaxDelay);
  CHECK(Growl::comb_delay(150.0f, fs) == Growl::kMinDelay);
  CHECK(Growl::comb_delay(std::nanf(""), fs) >= Growl::kMinDelay);
}

TEST_CASE("growl: the comb's delay is 2^x to the float's own accuracy over the whole pitch range") {
  const float fs = static_cast<float>(kRate);
  double worst = 0.0;
  for (double semis = -48.0; semis <= 160.0; semis += 0.173) {
    double d = static_cast<double>(fs) / 440.0 * std::exp2((69.0 - semis) / 12.0);
    for (unsigned i = 0; i < 8u && d > static_cast<double>(Growl::kMaxDelay); ++i) {
      d *= 0.5;
    }
    d = std::max(d, static_cast<double>(Growl::kMinDelay));
    const double got = static_cast<double>(Growl::comb_delay(static_cast<float>(semis), fs));
    worst = std::max(worst, std::fabs(got - d) / d);
  }
  CHECK(worst < 2.0e-6);  // the pitch is a float of up to 160 (an ulp of 1.5e-5 semitones, 1e-6 of a delay); the polynomial itself is 8e-8
}

TEST_CASE("growl: the loops of a settled Mix 100 % and of a Mix a hair under it give the same signal, whatever moves") {
  const std::size_t n = 30000;
  const Stereo in = mono(bass(n));
  const auto make = [](float mix) {
    return std::vector<Ev>{{0, Growl::Vowel, 0.4f},  {0, Growl::CombMix, 0.7f}, {0, Growl::Note, 40.0f}, {0, Growl::Mix, mix},
                           {9000, Growl::Note, 52.0f}, {20000, Growl::FormantMix, 0.6f}};  // a glide, then a ramp of the mixes
  };
  Growl a;
  prep(a);
  Growl b;
  prep(b);
  const Stereo full = run(a, in, make(1.0f));
  const Stereo near = run(b, in, make(0.9999f));
  double worst = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    worst = std::max(worst, std::fabs(static_cast<double>(full.l[i]) - static_cast<double>(near.l[i])));
  }
  CHECK(worst < 2.0e-4);  // 1e-4 of |wet - dry|, which stays under 2, and the rounding of the two forms
  CHECK(all_finite(full));
}

TEST_CASE("growl: the result does not depend on the block size") {
  const std::size_t n = 22050;
  const Stereo in = mono(bass(n));
  const std::vector<Ev> ev = {{0, Growl::Vowel, 0.1f},    {0, Growl::Mix, 0.8f},   {3001, Growl::Vowel, 0.9f},  {5000, Growl::Note, 48.0f},
                              {7123, Growl::CombTune, 7.0f}, {9000, Growl::FormantQ, 12.0f}, {12000, Growl::Mix, 0.0f}, {16000, Growl::Mix, 1.0f},
                              {19000, Growl::CombFeedback, -0.9f}};
  Growl ref;
  prep(ref);
  const Stereo a = run(ref, in, ev, 64);
  for (const std::uint32_t block : {1u, 7u, 17u, 33u, 63u}) {
    CAPTURE(block);
    Growl g;
    prep(g);
    const Stereo b = run(g, in, ev, block);
    double worst = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      worst = std::max(worst, std::fabs(static_cast<double>(a.l[i]) - static_cast<double>(b.l[i])));
    }
    CHECK(worst < 1e-6);
  }
  CHECK(all_finite(a));
}

TEST_CASE("growl: Mix 0 is the input bit for bit, in place or not, also after the unit has been on") {
  const std::size_t n = 30000;
  Stereo in{bass(n, 55.0, 3), bass(n, 61.0, 5)};  // different channels
  Growl untouched;
  untouched.prepare(static_cast<float>(kRate));
  for (const bool in_place : {true, false}) {
    CAPTURE(in_place);
    Growl g;
    prep(g);
    const Stereo out = run(g, in, {}, 64, in_place);
    CHECK(std::memcmp(out.l.data(), in.l.data(), n * sizeof(float)) == 0);
    CHECK(std::memcmp(out.r.data(), in.r.data(), n * sizeof(float)) == 0);
  }
  // On for a while (Mix 1 from 2,000 to 12,000), then off: once the 5 ms ramp is over the input comes through exactly.
  Growl g;
  prep(g);
  const Stereo out = run(g, in, {{2000, Growl::Mix, 1.0f}, {12000, Growl::Mix, 0.0f}}, 64, false);
  CHECK(std::memcmp(out.l.data(), in.l.data(), 2000 * sizeof(float)) == 0);
  CHECK(std::memcmp(out.l.data() + 12400, in.l.data() + 12400, (n - 12400) * sizeof(float)) == 0);
  CHECK(std::memcmp(out.r.data() + 12400, in.r.data() + 12400, (n - 12400) * sizeof(float)) == 0);
  CHECK(std::memcmp(out.l.data() + 4000, in.l.data() + 4000, 4000 * sizeof(float)) != 0);  // and it did something in between
  CHECK(g.idle());
  // A Mix that shows 0 % but is a float step's residue above 0 is off too: the unit goes idle (no cost) and passes the input exactly.
  Growl gh;
  prep(gh);
  const Stereo hair = run(gh, in, {{2000, Growl::Mix, 1.0f}, {12000, Growl::Mix, 2.2e-8f}}, 64, false);
  CHECK(std::memcmp(hair.l.data() + 12400, in.l.data() + 12400, (n - 12400) * sizeof(float)) == 0);
  CHECK(gh.idle());
  // The dry part keeps the stereo image: Mix 0.5 on different channels differs from the mono sum.
  Growl h;
  prep(h);
  const Stereo half = run(h, in, {{0, Growl::Mix, 0.5f}});
  double diff = 0.0;
  for (std::size_t i = 10000; i < 12000; ++i) {
    diff += std::fabs(static_cast<double>(half.l[i]) - static_cast<double>(half.r[i]));
  }
  CHECK(diff > 0.1);
}

TEST_CASE("growl: a jump of Vowel, Note or Mix does not click") {
  const std::size_t n = 30000;
  const Stereo in = mono(bass(n));
  const auto steady_step = [&](const std::vector<Ev>& ev) {
    Growl g;
    prep(g);
    const Stereo out = run(g, in, ev);
    return max_step(out.l, 12000, 24000);
  };
  const std::vector<Ev> base = {{0, Growl::Mix, 1.0f}, {0, Growl::CombMix, 0.7f}, {0, Growl::Note, 36.0f}};
  const auto with = [&](std::vector<Ev> extra) {
    std::vector<Ev> v = base;
    v.insert(v.end(), extra.begin(), extra.end());
    return v;
  };
  // Vowel A -> U (the far end of the table) and back, Note 36 -> 60 and 36 -> 24, each at frame 15,000.
  struct Case {
    const char* name;
    std::vector<Ev> before;  // the settings before the jump (steady state A)
    std::vector<Ev> jump;    // the event at 15,000
    std::vector<Ev> after;   // the same settings from the start (steady state B)
  };
  const Case cases[] = {
      {"vowel A to U", with({{0, Growl::Vowel, 0.0f}}), {{15000, Growl::Vowel, 1.0f}}, with({{0, Growl::Vowel, 1.0f}})},
      {"vowel U to A", with({{0, Growl::Vowel, 1.0f}}), {{15000, Growl::Vowel, 0.0f}}, with({{0, Growl::Vowel, 0.0f}})},
      {"note 36 to 60", with({{0, Growl::Vowel, 0.3f}}), {{15000, Growl::Note, 60.0f}}, with({{0, Growl::Vowel, 0.3f}, {0, Growl::Note, 60.0f}})},
      {"note 36 to 24", with({{0, Growl::Vowel, 0.3f}}), {{15000, Growl::Note, 24.0f}}, with({{0, Growl::Vowel, 0.3f}, {0, Growl::Note, 24.0f}})},
  };
  for (const Case& c : cases) {
    CAPTURE(c.name);
    const double bound = 1.6 * std::max(steady_step(c.before), steady_step(c.after));
    std::vector<Ev> ev = c.before;
    ev.insert(ev.end(), c.jump.begin(), c.jump.end());
    Growl g;
    prep(g);
    const Stereo out = run(g, in, ev);
    const double jump = max_step(out.l, 14900, 18000);
    CAPTURE(jump);
    CAPTURE(bound);
    CHECK(jump <= bound);
  }
  // Mix 0 -> 1 and 1 -> 0 ramp over 5 ms: no step beyond the dry or the wet signal's own.
  const double wet = steady_step(base);
  const double dry = max_step(in.l, 12000, 24000);
  for (const bool up : {true, false}) {
    CAPTURE(up);
    std::vector<Ev> ev = {{0, Growl::CombMix, 0.7f}, {0, Growl::Note, 36.0f}, {0, Growl::Mix, up ? 0.0f : 1.0f}, {15000, Growl::Mix, up ? 1.0f : 0.0f}};
    Growl g;
    prep(g);
    const Stereo out = run(g, in, ev);
    CHECK(max_step(out.l, 14900, 15600) <= 1.2 * std::max(dry, wet) + 0.02);
  }
}

TEST_CASE("growl: feedback at the limits stays finite and bounded, and its tail dies without denormals") {
  const std::size_t n = 44100 * 3;
  std::vector<float> burst(n, 0.0f);
  std::uint32_t s = 7;
  for (std::size_t i = 0; i < 4000; ++i) {  // a burst of noise at full scale, then silence
    s = s * 1664525u + 1013904223u;
    burst[i] = static_cast<float>(static_cast<double>(s >> 8) / 8388608.0 - 1.0);
  }
  struct Case {
    float fb, note, tune, q, vowel;
  };
  const Case cases[] = {{0.95f, 36.0f, 0.0f, 1.0f, 0.0f},  {-0.95f, 36.0f, 0.0f, 20.0f, 1.0f}, {0.95f, 127.0f, 24.0f, 20.0f, 0.5f},
                        {-0.95f, 127.0f, 24.0f, 1.0f, 0.5f}, {0.95f, 0.0f, -24.0f, 5.0f, 0.25f}, {-0.95f, 24.0f, -24.0f, 20.0f, 0.75f}};
  for (const Case& c : cases) {
    CAPTURE(c.fb);
    CAPTURE(c.note);
    Growl g;
    prep(g);
    const std::vector<Ev> ev = {{0, Growl::CombFeedback, c.fb}, {0, Growl::Note, c.note}, {0, Growl::CombTune, c.tune}, {0, Growl::FormantQ, c.q},
                                {0, Growl::Vowel, c.vowel},     {0, Growl::CombMix, 1.0f}, {0, Growl::Mix, 1.0f}};
    const Stereo out = run(g, mono(burst), ev);
    CHECK(all_finite(out));
    double peak = 0.0;
    bool subnormal = false;
    for (std::size_t i = 0; i < n; ++i) {
      peak = std::max(peak, std::fabs(static_cast<double>(out.l[i])));
      subnormal = subnormal || std::fpclassify(out.l[i]) == FP_SUBNORMAL || std::fpclassify(out.r[i]) == FP_SUBNORMAL;
    }
    CHECK(peak < 40.0);
    CHECK_FALSE(subnormal);
  }
  // At the highest pitch (a 4-frame loop) the tail of 0.95 reaches exact zero in the 3 s: the flush works.
  Growl g;
  prep(g);
  const Stereo out = run(g, mono(burst), {{0, Growl::CombFeedback, 0.95f}, {0, Growl::Note, 127.0f}, {0, Growl::CombTune, 24.0f}, {0, Growl::CombMix, 1.0f}, {0, Growl::Mix, 1.0f}});
  CHECK(rms(out.l, n - 4410, n) == 0.0);
}

TEST_CASE("growl: GAIN at 0 dB leaves the output as it was (golden hash)") {
  // The comb's output gains carry the gain, and at 0 dB they are the old values exactly. One hash over a busy run: Mix up, a vowel sweep, the
  // note and tune moving (the glide), feedback both signs, a half mix, and Mix back to 0. GAIN is set to 0 dB at frame 0: the hash was taken
  // before the stage existed.
  const std::size_t n = 16384;
  const Stereo in{bass(n, 55.0, 3), bass(n, 82.0, 4)};
  const std::vector<Ev> ev = {{0, Growl::Gain, 0.0f},
                              {0, Growl::Vowel, 0.2f},
                              {0, Growl::CombFeedback, 0.7f},
                              {0, Growl::Note, 36.0f},
                              {0, Growl::Mix, 1.0f},
                              {2000, Growl::Vowel, 0.8f},
                              {3000, Growl::Note, 43.0f},
                              {4000, Growl::CombTune, 7.0f},
                              {5000, Growl::CombFeedback, -0.6f},
                              {6000, Growl::FormantQ, 0.8f},
                              {7000, Growl::CombMix, 0.5f},
                              {8000, Growl::Mix, 0.5f},
                              {10000, Growl::FormantMix, 0.4f},
                              {12000, Growl::Note, 31.0f},
                              {14000, Growl::Mix, 0.0f}};
  // 953714623 on the tree before GAIN existed, and the same with GAIN in and the old crossfade (checked on 2026-11-12: the stage is
  // transparent at 0 dB). The Mix law then went to equal power (the half mix of this run and the ramps change by rounding and by law), and
  // the hash below is of that tree.
  constexpr std::uint32_t kGolden = 1369919415u;
  Growl g;
  g.prepare(static_cast<float>(kRate));
  const Stereo out = run(g, in, ev);
  std::uint32_t h = 2166136261u;
  for (const auto* v : {&out.l, &out.r}) {
    for (const float f : *v) {
      std::uint32_t u = 0;
      std::memcpy(&u, &f, sizeof u);
      for (int b = 0; b < 4; ++b) {
        h = (h ^ ((u >> (8 * b)) & 0xFFu)) * 16777619u;
      }
    }
  }
  CHECK_EQ(h, kGolden);
}

namespace {

// The wet signal of a full-wet unit on a bass line, GAIN as given (set at frame 0 while idle, so it snaps).
Stereo wet_with_gain(float db, std::uint32_t block = 64) {
  const std::size_t n = 12000;
  const Stereo in{bass(n, 55.0, 3), bass(n, 82.0, 4)};
  Growl g;
  prep(g);
  return run(g, in, {{0, Growl::Gain, db}, {0, Growl::Vowel, 0.3f}, {0, Growl::CombMix, 0.6f}, {0, Growl::CombFeedback, 0.5f}, {0, Growl::Mix, 1.0f}}, block);
}

}  // namespace

TEST_CASE("growl: GAIN +6 dB doubles the wet signal, -6 dB halves it, to the float's rounding") {
  const Stereo base = wet_with_gain(0.0f);
  const Stereo up = wet_with_gain(6.0f);
  const Stereo down = wet_with_gain(-6.0f);
  const double k = std::pow(10.0, 6.0 / 20.0);
  double worst_up = 0.0, worst_down = 0.0, peak = 0.0;
  for (std::size_t i = 600; i < base.l.size(); ++i) {  // past the 5 ms Mix ramp (the dry part is still fading out in its first frames)
    peak = std::max(peak, std::fabs(static_cast<double>(base.l[i])));
    worst_up = std::max(worst_up, std::fabs(static_cast<double>(up.l[i]) - k * static_cast<double>(base.l[i])));
    worst_down = std::max(worst_down, std::fabs(static_cast<double>(down.l[i]) - static_cast<double>(base.l[i]) / k));
  }
  CHECK(peak > 0.05);
  CHECK(worst_up < 1.0e-5 * (1.0 + peak));
  CHECK(worst_down < 1.0e-5 * (1.0 + peak));
}

TEST_CASE("growl: Mix 0 is the input bit for bit at any GAIN, also with GAIN moving") {
  const std::size_t n = 8192;
  const Stereo in{bass(n, 55.0, 3), bass(n, 82.0, 4)};
  for (const float db : {-12.0f, 0.0f, 15.0f, 18.0f}) {
    CAPTURE(db);
    Growl g;
    g.prepare(static_cast<float>(kRate));
    const Stereo out = run(g, in, {{0, Growl::Gain, db}, {3000, Growl::Gain, -db}});
    CHECK(std::memcmp(out.l.data(), in.l.data(), n * sizeof(float)) == 0);
    CHECK(std::memcmp(out.r.data(), in.r.data(), n * sizeof(float)) == 0);
  }
}

TEST_CASE("growl: a GAIN change ramps in about 5 ms (no step), then holds, and does not depend on the block size") {
  const std::size_t n = 12000;
  const Stereo in = mono(bass(n));
  const std::vector<Ev> ev = {{0, Growl::Gain, 0.0f}, {0, Growl::Mix, 1.0f}, {0, Growl::CombMix, 1.0f}, {0, Growl::FormantMix, 0.0f},
                              {6000, Growl::Gain, 12.0f}};
  Growl ref;
  prep(ref);
  const Stereo a = run(ref, in, ev, 64);
  // Before the change the gain is 1, after about 5 ms (220 frames, plus the period it lands in) it is 4: the ratio of two runs tells it.
  Growl flat;
  prep(flat);
  const Stereo b = run(flat, in, {{0, Growl::Gain, 0.0f}, {0, Growl::Mix, 1.0f}, {0, Growl::CombMix, 1.0f}, {0, Growl::FormantMix, 0.0f}}, 64);
  const double k = std::pow(10.0, 12.0 / 20.0);
  for (std::size_t i = 0; i < 6000; ++i) {
    CHECK(a.l[i] == b.l[i]);  // untouched before the change
  }
  double worst = 0.0;
  for (std::size_t i = 6000 + 300; i < n; ++i) {
    worst = std::max(worst, std::fabs(static_cast<double>(a.l[i]) - k * static_cast<double>(b.l[i])));
  }
  CHECK(worst < 2.0e-5);
  // A ramp, not a step: the first frames after the change are between the two levels, and the step between samples stays small.
  double ratio_mid = 0.0;
  for (std::size_t i = 6000; i < 6000 + 220; ++i) {
    if (std::fabs(b.l[i]) > 0.2f) {
      ratio_mid = static_cast<double>(a.l[i]) / static_cast<double>(b.l[i]);
      break;
    }
  }
  CHECK(ratio_mid < k);
  for (const std::uint32_t block : {1u, 7u, 33u}) {
    CAPTURE(block);
    Growl g;
    prep(g);
    const Stereo c = run(g, in, ev, block);
    double w = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      w = std::max(w, std::fabs(static_cast<double>(a.l[i]) - static_cast<double>(c.l[i])));
    }
    CHECK(w < 1e-6);
  }
  CHECK(all_finite(a));
}

TEST_CASE("growl: GAIN set while Mix is 0 is in force the moment Mix leaves 0 (no ramp from a stale value)") {
  const std::size_t n = 6000;
  const Stereo in = mono(bass(n));
  Growl g;
  prep(g);
  const Stereo a = run(g, in, {{0, Growl::Gain, 12.0f}, {0, Growl::CombMix, 1.0f}, {0, Growl::FormantMix, 0.0f}, {3000, Growl::Mix, 1.0f}});
  Growl h;
  prep(h);
  const Stereo b = run(h, in, {{0, Growl::Gain, 0.0f}, {0, Growl::CombMix, 1.0f}, {0, Growl::FormantMix, 0.0f}, {3000, Growl::Mix, 1.0f}});
  const double k = std::pow(10.0, 12.0 / 20.0);
  double worst = 0.0;
  for (std::size_t i = 3400; i < n; ++i) {  // past the 5 ms Mix ramp
    worst = std::max(worst, std::fabs(static_cast<double>(a.l[i]) - k * static_cast<double>(b.l[i])));
  }
  CHECK(worst < 2.0e-5);
}

TEST_CASE("growl: the Mix crossfades at equal power, dry cos(pi m / 2), wet sin(pi m / 2)") {
  const std::size_t n = 8000;
  const std::vector<float> sn = sine(n, 300.0, 0.5);
  std::vector<float> neg = sn;
  for (float& f : neg) {
    f = -f;
  }
  const std::vector<Ev> pass = {{0, Growl::FormantMix, 0.0f}, {0, Growl::CombMix, 0.0f}, {0, Growl::Mix, 0.5f}};  // wet = the mono sum
  {
    Growl g;
    prep(g);
    // Opposite channels: the mono sum (the wet) is 0, what is left is the dry part, cos(pi/4) of it.
    const Stereo out = run(g, Stereo{sn, neg}, pass);
    CHECK(rms(out.l, 4000, 8000) / rms(sn, 4000, 8000) == doctest::Approx(0.70710678).epsilon(1e-3));
  }
  {
    Growl g;
    prep(g);
    // Equal channels: wet and dry are the same signal, their gains add: cos + sin = sqrt(2).
    const Stereo out = run(g, mono(sn), pass);
    CHECK(rms(out.l, 4000, 8000) / rms(sn, 4000, 8000) == doctest::Approx(1.41421356).epsilon(1e-3));
  }
}
