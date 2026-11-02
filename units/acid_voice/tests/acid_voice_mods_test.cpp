// SPDX-License-Identifier: GPL-3.0-or-later
// The mods of the voice (docs/MODS-PLAN.md section 3, ids 8..19): that each is exactly the stock voice at its stock value, that Stock
// is exactly the stock voice, that nothing clicks at a toggle, that each does what the public descriptions say (3.2), and that the guards
// hold (3.5): non-finite values read as stock, range ends, the 576 corners with the mods at their extremes, flushed states.
#include <doctest/doctest.h>

#include "acid_voice_gesture.h"
#include "metrics.h"

#include <cfenv>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

using namespace acid_voice_test;
using o3_303::AcidVoice;

// The voice's state, read through the friend the header names. Limits (3.5): the ladder's four states inside the original's +-4 clamp
// (kStateLimit) and the tanh of each inside +-1; the feedback high-pass and Filter FM's blocker inside twice the output bound; the
// envelopes and the soft attack in 0..1; the output of the last sample, the muffler's low-pass and the blocker's input inside the output
// bound (2.0 before the volume: 1.4 at the gesture's -3 dB); each mod's own state inside its range.
namespace o3_303 {
struct AcidVoiceTestAccess {
  static constexpr float kLadder = 4.0f;       // the original's clamp of each ladder state
  static constexpr float kTanh = 1.0001f;      // the tanh of a state
  static constexpr float kOut = 2.0f;          // the output bound before the volume
  static constexpr float kHp = 2.0f * kOut;    // feedback high-pass and Filter FM's blocker
  static constexpr float kSubMax = 0.8f;       // Sub Osc at its highest step
  static constexpr float kFmMax = 2.0f;        // Filter FM's depth k at 100 %
  static constexpr float kOdMin = 0.0630f;     // -24 dB, a little under
  static constexpr float kOdMax = 64.5f;       // +36 dB, a little over
  static constexpr float kSgMax = 0.4751f;     // half of the ladder's g ceiling (0.95), with the float slack of 16 accumulated steps
  static constexpr float kSwpMax = 2.0f;       // the follower of SLOW is twice the accent pulse

  static bool modded(const AcidVoice& v) { return v.modded_; }

  // Returns null when every state is inside its limit, else the name of the first that is not; `worst` takes the largest |s| over the ladder.
  static const char* violation(const AcidVoice& v, float& worst) {
    const auto bad = [](float x, float lo, float hi) { return !(x >= lo && x <= hi) || std::fpclassify(x) == FP_SUBNORMAL; };
    for (unsigned i = 0; i < 4; ++i) {
      worst = std::max(worst, std::fabs(v.s_[i]));
      if (bad(v.s_[i], -kLadder, kLadder)) return "ladder state";
      if (bad(v.t_[i], -kTanh, kTanh)) return "ladder tanh";
    }
    if (bad(v.hp_state_, -kHp, kHp)) return "feedback high-pass";
    if (bad(v.sub_g_, 0.0f, kSgMax)) return "filter g";
    if (bad(v.vcf_, 0.0f, 1.0f)) return "filter envelope";
    if (bad(v.vca_, 0.0f, 1.0f)) return "amplifier envelope";
    if (bad(v.acc_, 0.0f, 1.0f)) return "accent envelope";
    if (bad(v.att_, 0.0f, 1.0f)) return "soft attack";
    if (bad(v.fm_xm1_, -kOut, kOut)) return "FM blocker input";
    if (bad(v.fm_ym1_, -kHp, kHp)) return "FM blocker output";
    if (bad(v.fm_y_, -kOut, kOut)) return "FM last output";
    if (bad(v.muf_lp_, -kOut, kOut)) return "muffler low-pass";
    if (bad(v.last_out_, -kOut * 2.0f, kOut * 2.0f)) return "last output";  // after the volume: up to +6 dB
    if (bad(v.sub_.v, 0.0f, kSubMax)) return "sub level";
    if (bad(v.subq_, -1.0f, 1.0f)) return "sub sign";
    if (bad(v.fm_.v, 0.0f, kFmMax)) return "FM depth";
    if (bad(v.mix_.v, 0.0f, 1.0f)) return "muffler mix";
    if (bad(v.od_g_, kOdMin, kOdMax)) return "overdrive gain";
    if (bad(v.swp_, 0.0f, kSwpMax)) return "sweep follower";
    if (bad(v.swp_m_, 0.0f, kSwpMax)) return "sweep follower (slow)";
    if (bad(v.phase_, 0.0f, 1.0f)) return "oscillator phase";
    if (!std::isfinite(v.pitch_hz_) || v.pitch_hz_ <= 0.0f) return "pitch";
    return nullptr;
  }
};
}  // namespace o3_303

namespace {

constexpr double kRate = 44100.0;
constexpr std::uint32_t ms(float t) { return static_cast<std::uint32_t>(t * 44.1f); }

constexpr unsigned kFirst = AcidVoice::AccDecay;  // 8
constexpr unsigned kLast = AcidVoice::Stock;      // 19

const o3sdk::core::ParamDesc& desc(unsigned id) {
  static AcidVoice v;
  return v.info().params[id];
}

bool same_bits(const std::vector<float>& a, const std::vector<float>& b, std::size_t from = 0) {
  if (a.size() != b.size() || from > a.size()) {
    return false;
  }
  return std::memcmp(a.data() + from, b.data() + from, (a.size() - from) * sizeof(float)) == 0;
}

bool all_finite(const std::vector<float>& v) {
  for (const float s : v) {
    if (!std::isfinite(s)) {
      return false;
    }
  }
  return true;
}

bool none_subnormal(const std::vector<float>& v) {
  for (const float s : v) {
    if (std::fpclassify(s) == FP_SUBNORMAL) {
      return false;
    }
  }
  return true;
}

std::size_t first_diff(const std::vector<float>& a, const std::vector<float>& b) {
  const std::size_t n = a.size() < b.size() ? a.size() : b.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) {
      return i;
    }
  }
  return n;
}

// The extremes of a mod's range (an Enum's ends are its first and last names; the Log and linear rows' are min and max).
float lo_of(unsigned id) { return desc(id).min; }
float hi_of(unsigned id) { return desc(id).max; }

// Prints a measurement when $O3_303_PROBE is set (to calibrate the thresholds below by running the test with it).
void probe(const char* what, double a, double b = 0.0, double c = 0.0) {
  if (std::getenv("O3_303_PROBE") != nullptr) {
    std::printf("probe: %-40s %12.5f %12.5f %12.5f\n", what, a, b, c);
  }
}

// The voice as the gesture's reference: a fresh one.
std::vector<float> fresh(bool square, std::uint32_t offset = 0) {
  Rig r;
  r.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
  build_gesture(r.script, WaveMove::Keep, offset);
  return r.render(kGestureFrames + offset);
}

}  // namespace

// ---- exactness --------------------------------------------------------------------------------------------------------------------

TEST_CASE("mods: the table is valid and follows the plan (ids, names, ranges, defaults)") {
  AcidVoice v;
  const auto& info = v.info();
  REQUIRE(info.param_count == 20);
  const char* names[12] = {"ACC DECAY", "TRACKING", "SLIDE", "SOFT ATK", "SUB OSC", "FILTER FM", "ACC SWEEP", "MUFFLER", "SWP SPEED", "OVERDRIVE", "AMP DECAY", "STOCK"};
  const float lo[12] = {30.0f, 0.0f, 60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -24.0f, 16.0f, 0.0f};
  const float hi[12] = {3000.0f, 150.0f, 360.0f, 5.0f, 3.0f, 1.0f, 2.0f, 2.0f, 2.0f, 36.0f, 4000.0f, 1.0f};
  const float def[12] = {180.0f, 0.0f, 60.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 4000.0f, 0.0f};
  for (unsigned i = 0; i < 12; ++i) {
    const auto& d = info.params[kFirst + i];
    INFO("mod " << names[i]);
    CHECK(o3sdk::core::param_desc_valid(d));
    CHECK(std::strcmp(d.name, names[i]) == 0);
    CHECK(std::strlen(d.name) <= 9);  // the knob strip's nine characters (kParamNameChars is 12)
    CHECK(d.min == lo[i]);
    CHECK(d.max == hi[i]);
    CHECK(d.def == def[i]);
    if (d.curve == o3sdk::core::ParamCurve::Log) {
      CHECK(d.min > 0.0f);
    }
    if (d.unit == o3sdk::core::ParamUnit::Enum) {
      REQUIRE(d.names != nullptr);
      for (unsigned n = 0; n < d.list_count; ++n) {
        CHECK(std::strlen(d.names[n]) <= 6);  // the skin's caption cut (kCaptionChars)
      }
    }
  }
  CHECK(AcidVoice::Stock == 19);
  CHECK(AcidVoice::Cutoff == 0);  // the ids of the 303's own knobs did not move
}

TEST_CASE("mods: each mod set to its stock value is the stock voice, bit for bit") {
  for (const bool square : {false, true}) {
    const std::vector<float> ref = fresh(square);
    for (unsigned id = kFirst; id <= kLast; ++id) {
      INFO("mod " << std::string(desc(id).name));
      Rig r;
      r.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
      r.set(static_cast<std::uint16_t>(id), desc(id).def);
      build_gesture(r.script, WaveMove::Keep);
      CHECK(same_bits(r.render(kGestureFrames), ref));
    }
  }
}

// Back at stock the voice runs the stock loop at once (the ramp lands within a few control periods); its bits equal a voice that was never
// modded only once the voice state has decayed (MODS-PLAN 3.3 point 9), so the first test looks at the loop and at a note soon after the
// return, the second at a note after the state has settled.
TEST_CASE("mods: a mod set to an extreme and back runs the stock loop at once, and sounds as the stock voice after a note, not yet bit for bit") {
  const auto play = [&](bool modded_then_back, unsigned id, float extreme, bool& stock_loop) {
    Rig r;
    if (modded_then_back) {
      r.script.param(100, static_cast<std::uint16_t>(id), extreme);
      r.script.param(2000, static_cast<std::uint16_t>(id), desc(id).def);
    }
    r.script.on(3000, 36, 0);
    r.script.off(6000, 36);
    std::vector<float> v = r.render(2600);  // 600 frames after the return: every ramp (about 5 ms) has landed
    stock_loop = !o3_303::AcidVoiceTestAccess::modded(r.voice);
    const std::vector<float> rest = r.render(4400);
    v.insert(v.end(), rest.begin(), rest.end());
    return v;
  };
  bool ignore = true;
  const std::vector<float> ref = play(false, 0, 0.0f, ignore);
  for (unsigned id = kFirst; id < kLast; ++id) {
    for (const float extreme : {lo_of(id), hi_of(id)}) {
      INFO("mod " << std::string(desc(id).name) << " at " << extreme);
      bool stock_loop = false;
      const std::vector<float> got = play(true, id, extreme, stock_loop);
      CHECK(stock_loop);
      CHECK(all_finite(got));
      // The note starts 1,000 frames after the return: the traces of the modded run are still in the state, and small.
      double worst = 0.0;
      for (std::size_t i = 3000; i < got.size(); ++i) {
        worst = std::max(worst, std::fabs(static_cast<double>(got[i]) - static_cast<double>(ref[i])));
      }
      probe(desc(id).name, extreme, worst);
      CHECK(worst < 0.25);  // measured up to 0.14 (OVERDRIVE at +36 dB), against a note that peaks near 0.5: a trace, not a different sound
    }
  }
}

TEST_CASE("mods: a mod set to an extreme and back, once the voice state has decayed, is the stock voice again, bit for bit") {
  constexpr std::uint32_t kPre = 100000;  // silence before the gesture: the extreme is set at 100, stock again at 2000, and the free-running filter settles
  const std::vector<float> ref = fresh(false, kPre);
  for (unsigned id = kFirst; id < kLast; ++id) {
    for (const float extreme : {lo_of(id), hi_of(id)}) {
      INFO("mod " << std::string(desc(id).name) << " at " << extreme);
      Rig r;
      r.script.param(100, static_cast<std::uint16_t>(id), extreme);
      r.script.param(2000, static_cast<std::uint16_t>(id), desc(id).def);
      build_gesture(r.script, WaveMove::Keep, kPre);
      const std::vector<float> got = r.render(kGestureFrames + kPre);
      CHECK(all_finite(got));
      CHECK(same_bits(got, ref, kPre));
      if (!same_bits(got, ref, kPre)) {
        MESSAGE("first difference at frame " << first_diff(got, ref) << " (the gesture starts at " << kPre << ")");
      }
    }
  }
}

TEST_CASE("mods: Stock with every mod at an extreme is the stock voice, bit for bit, and gives the mods back when it goes off") {
  const std::vector<float> ref = fresh(false);
  for (const bool hi : {false, true}) {
    Rig r;
    for (unsigned id = kFirst; id < kLast; ++id) {
      r.set(static_cast<std::uint16_t>(id), hi ? hi_of(id) : lo_of(id));
    }
    r.set(AcidVoice::Stock, 1.0f);
    build_gesture(r.script, WaveMove::Keep);
    CHECK(same_bits(r.render(kGestureFrames), ref));
  }
  // Toggled while the voice is idle, with the mods already ramped in: on, then off, then the stored values are heard again.
  constexpr std::uint32_t kPre = 100000;  // a silent prelude: long enough for the free-running filter to settle again
  const std::vector<float> ref_pre = fresh(false, kPre);
  Rig on;
  for (unsigned id = kFirst; id < kLast; ++id) {
    on.script.param(10, static_cast<std::uint16_t>(id), hi_of(id));
  }
  on.script.param(2000, AcidVoice::Stock, 1.0f);
  build_gesture(on.script, WaveMove::Keep, kPre);
  const std::vector<float> got = on.render(kGestureFrames + kPre);
  CHECK(all_finite(got));
  CHECK(same_bits(got, ref_pre, kPre));

  // Off again: the stored extremes sound (the render differs from the stock voice).
  Rig off;
  for (unsigned id = kFirst; id < kLast; ++id) {
    off.script.param(10, static_cast<std::uint16_t>(id), hi_of(id));
  }
  off.script.param(1000, AcidVoice::Stock, 1.0f);
  off.script.param(3000, AcidVoice::Stock, 0.0f);
  build_gesture(off.script, WaveMove::Keep, kPre);
  const std::vector<float> heard = off.render(kGestureFrames + kPre);
  CHECK(all_finite(heard));
  CHECK_FALSE(same_bits(heard, ref_pre, kPre));
}

// ---- helpers of the behaviour and guard tests -----------------------------------------------------------------------------------------

namespace {

using P = std::pair<std::uint16_t, float>;

double hz_of(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// Magnitudes of a Hann-windowed FFT of x[from, from + n) (n a power of two): bin k is at k * rate / n.
std::vector<double> spectrum(const std::vector<float>& x, std::size_t from, std::size_t n) {
  std::vector<std::complex<double>> a(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(n));
    a[i] = static_cast<double>(x[from + i]) * w;
  }
  fft(a);
  std::vector<double> m(n / 2);
  for (std::size_t i = 0; i < n / 2; ++i) {
    m[i] = std::abs(a[i]);
  }
  return m;
}

// The energy of the bins between lo and hi Hz.
double band(const std::vector<double>& m, std::size_t n, double lo, double hi) {
  double e = 0.0;
  for (std::size_t k = 0; k < m.size(); ++k) {
    const double f = static_cast<double>(k) * kRate / static_cast<double>(n);
    if (f >= lo && f < hi) {
      e += m[k] * m[k];
    }
  }
  return e;
}

double centroid(const std::vector<double>& m, std::size_t n, double lo, double hi) {
  double num = 0.0;
  double den = 0.0;
  for (std::size_t k = 1; k < m.size(); ++k) {
    const double f = static_cast<double>(k) * kRate / static_cast<double>(n);
    if (f >= lo && f < hi) {
      num += f * m[k] * m[k];
      den += m[k] * m[k];
    }
  }
  return den > 0.0 ? num / den : 0.0;
}

double db(double ratio) { return 20.0 * std::log10(ratio); }

// a and b within `rel` of the larger of them. (doctest::Approx adds 1.0 to its scale: on values of a few hundredths it accepts anything.)
bool near(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::max(std::fabs(a), std::fabs(b)); }

// The largest step between two neighbouring samples in [from, to).
double max_step(const std::vector<float>& v, std::size_t from, std::size_t to) {
  double m = 0.0;
  for (std::size_t i = from + 1; i < to && i < v.size(); ++i) {
    m = std::max(m, std::fabs(static_cast<double>(v[i]) - static_cast<double>(v[i - 1])));
  }
  return m;
}

}  // namespace

// ---- no click ---------------------------------------------------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kChange = 10000;

// The note of the click tests: unaccented, 20,000 frames, the mods in `before` from the start, `change` applied at kChange. Returns the
// largest step within 10 ms of the change relative to the larger of the largest steps of the old state (the 110 ms before the change) and of
// the new state (settled, 90 to 200 ms later): the plan's "1.25 times" with the new state's own steps counted, since a change that adds
// a signal (a sub, a drive) has larger steps of its own.
double click_ratio(std::initializer_list<P> before, std::initializer_list<P> change, bool square = false, std::uint32_t at = kChange) {
  Rig r;
  r.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
  r.set(AcidVoice::Cutoff, 1500.0f);
  r.set(AcidVoice::Reso, 0.5f);
  r.set(AcidVoice::EnvMod, 0.3f);
  r.set(AcidVoice::Decay, 2500.0f);
  r.set(AcidVoice::Volume, 0.0f);
  for (const P& p : before) {
    r.set(p.first, p.second);
  }
  r.script.on(0, 48);
  for (const P& p : change) {
    r.script.param(at, p.first, p.second);
  }
  const std::vector<float> v = r.render(20000);
  const double old_steps = max_step(v, at - 5000, at);
  const double new_steps = max_step(v, at + 4000, at + 9000);
  const double ref = std::max(old_steps, new_steps);
  const double got = max_step(v, at, at + ms(10.0f));
  probe("click: old, new, got", old_steps, new_steps, got);
  REQUIRE(ref > 0.0);
  return got / ref;
}

// Every mod at an end of its range (AMP DECAY excepted at its short end: 16 ms would leave nothing sounding to click).
std::vector<P> all_at(bool hi) {
  std::vector<P> v;
  for (unsigned id = kFirst; id < kLast; ++id) {
    const bool amp_short = id == AcidVoice::AmpDecay && !hi;
    v.push_back({static_cast<std::uint16_t>(id), amp_short ? desc(id).def : (hi ? hi_of(id) : lo_of(id))});
  }
  return v;
}

}  // namespace

TEST_CASE("mods: no click when Stock is toggled mid-note, either way, with every mod at an extreme") {
  for (const bool square : {false, true}) {
    for (const bool hi : {false, true}) {
      const std::vector<P> ex = all_at(hi);
      auto run = [&](bool stock_before, float stock_after) {
        Rig r;
        r.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
        r.set(AcidVoice::Cutoff, 1500.0f);
        r.set(AcidVoice::Reso, 0.5f);
        r.set(AcidVoice::EnvMod, 0.3f);
        r.set(AcidVoice::Decay, 2500.0f);
        r.set(AcidVoice::Volume, 0.0f);
        for (const P& p : ex) {
          r.set(p.first, p.second);
        }
        r.set(AcidVoice::Stock, stock_before ? 1.0f : 0.0f);
        r.script.on(0, 48);
        r.script.param(kChange, AcidVoice::Stock, stock_after);
        const std::vector<float> v = r.render(20000);
        REQUIRE(all_finite(v));
        const double ref = std::max(max_step(v, kChange - 5000, kChange), max_step(v, kChange + 4000, kChange + 9000));
        REQUIRE(ref > 0.0);
        const double got = max_step(v, kChange, kChange + ms(10.0f));
        probe(hi ? "stock toggle (hi): ref, got" : "stock toggle (lo): ref, got", ref, got, stock_after);
        return got / ref;
      };
      INFO((square ? "SQR" : "SAW") << (hi ? " all max" : " all min"));
      CHECK(run(false, 1.0f) <= 1.25);
      CHECK(run(true, 0.0f) <= 1.25);
    }
  }
}

TEST_CASE("mods: no click when a switch mod, a ramped mod or Stock-free mod changes mid-note") {
  struct Case {
    const char* name;
    std::uint16_t id;
    float from;
    float to;
  };
  const Case cases[] = {
      {"SUB OSC off to HIGH", AcidVoice::SubOsc, 0.0f, 3.0f},   {"SUB OSC HIGH to off", AcidVoice::SubOsc, 3.0f, 0.0f},
      {"SUB OSC LOW to HIGH", AcidVoice::SubOsc, 1.0f, 3.0f},   {"MUFFLER off to SOFT", AcidVoice::Muffler, 0.0f, 1.0f},
      {"MUFFLER off to HARD", AcidVoice::Muffler, 0.0f, 2.0f},  {"MUFFLER SOFT to HARD", AcidVoice::Muffler, 1.0f, 2.0f},
      {"MUFFLER HARD to off", AcidVoice::Muffler, 2.0f, 0.0f},  {"FILTER FM off to full", AcidVoice::FilterFm, 0.0f, 1.0f},
      {"FILTER FM full to off", AcidVoice::FilterFm, 1.0f, 0.0f}, {"OVERDRIVE 0 to +36", AcidVoice::Overdrive, 0.0f, 36.0f},
      {"OVERDRIVE 0 to -24", AcidVoice::Overdrive, 0.0f, -24.0f}, {"OVERDRIVE +36 to 0", AcidVoice::Overdrive, 36.0f, 0.0f},
      {"ACC SWEEP NORMAL to OFF", AcidVoice::AccSweep, 1.0f, 0.0f}, {"ACC SWEEP NORMAL to HI RES", AcidVoice::AccSweep, 1.0f, 2.0f},
      {"ACC SWEEP HI RES to NORMAL", AcidVoice::AccSweep, 2.0f, 1.0f}, {"SWP SPEED NORMAL to SLOW", AcidVoice::SwpSpeed, 1.0f, 2.0f},
      {"SWP SPEED NORMAL to FAST", AcidVoice::SwpSpeed, 1.0f, 0.0f}, {"SWP SPEED SLOW to NORMAL", AcidVoice::SwpSpeed, 2.0f, 1.0f},
      {"SOFT ATK off to 30 MS", AcidVoice::SoftAtk, 0.0f, 5.0f},
  };
  for (const bool square : {false, true}) {
    for (const Case& c : cases) {
      INFO((square ? "SQR " : "SAW ") << c.name);
      const double ratio = click_ratio({{c.id, c.from}}, {{c.id, c.to}}, square);
      probe(c.name, ratio);
      CHECK(ratio <= 1.25);
    }
  }
}

// The step metric above cannot see a ramp: a level that jumps adds a step no larger than the sound's own edges. What a ramp shows is that the
// difference to the same note rendered without the change starts at nothing: its RMS over the first half millisecond, and its largest value in
// the first millisecond, are a fraction of what they settle at once the ramp (5 ms) has landed. The two catch different jumps (the ladder
// smooths a level that steps at its input over a few samples, a mix at its output not at all). A depth that steps, as FILTER FM's would,
// shows in neither: the filter integrates it.
TEST_CASE("mods: a switch or a level that changes mid-note comes in over a ramp, it does not jump") {
  struct Case {
    const char* name;
    std::uint16_t id;
    float to;
  };
  const Case cases[] = {
      {"SUB OSC HIGH", AcidVoice::SubOsc, 3.0f},   {"MUFFLER SOFT", AcidVoice::Muffler, 1.0f}, {"MUFFLER HARD", AcidVoice::Muffler, 2.0f},
      {"FILTER FM", AcidVoice::FilterFm, 1.0f},    {"OVERDRIVE -24", AcidVoice::Overdrive, -24.0f}, {"OVERDRIVE +36", AcidVoice::Overdrive, 36.0f},
  };
  for (const bool square : {false, true}) {
    for (const Case& c : cases) {
      const auto render = [&](bool change) {
        Rig r;
        r.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
        r.set(AcidVoice::Cutoff, 1500.0f);
        r.set(AcidVoice::Reso, 0.5f);
        r.set(AcidVoice::EnvMod, 0.3f);
        r.set(AcidVoice::Decay, 2500.0f);
        r.set(AcidVoice::Volume, 0.0f);
        r.script.on(0, 48);
        if (change) {
          r.script.param(kChange, c.id, c.to);
        }
        return r.render(kChange + 3000);
      };
      const std::vector<float> base = render(false);
      const std::vector<float> got = render(true);
      const auto rms_diff = [&](std::size_t from, std::size_t to) {
        double m = 0.0;
        for (std::size_t i = from; i < to; ++i) {
          const double d = static_cast<double>(got[i]) - static_cast<double>(base[i]);
          m += d * d;
        }
        return std::sqrt(m / static_cast<double>(to - from));
      };
      const auto max_diff = [&](std::size_t from, std::size_t to) {
        double m = 0.0;
        for (std::size_t i = from; i < to; ++i) {
          m = std::max(m, std::fabs(static_cast<double>(got[i]) - static_cast<double>(base[i])));
        }
        return m;
      };
      const double first = rms_diff(kChange, kChange + 22);
      const double settled = rms_diff(kChange + 441, kChange + 2500);
      const double first_max = max_diff(kChange, kChange + 44);
      const double settled_max = max_diff(kChange + 441, kChange + 2500);
      probe(c.name, first / settled, first_max / settled_max);
      INFO((square ? "SQR " : "SAW ") << c.name << ": first half ms " << first << " of " << settled << ", first ms " << first_max << " of " << settled_max);
      REQUIRE(settled > 0.005);
      CHECK(first < 0.5 * settled);
      CHECK(first_max < 0.5 * settled_max);
    }
  }
}

// ---- behaviour, one mod at a time (docs/MODS-PLAN.md 3.2) --------------------------------------------------------------------------

namespace {

// A flat, open voice: the output is the oscillator times the amplifier, so the amplifier's own movements can be read from it.
constexpr std::initializer_list<P> kOpen = {{AcidVoice::Cutoff, 4500.0f}, {AcidVoice::Reso, 0.0f},   {AcidVoice::EnvMod, 0.0f},
                                            {AcidVoice::Decay, 2500.0f},  {AcidVoice::Volume, 0.0f}, {AcidVoice::Accent, 0.0f}};

std::vector<P> with(std::initializer_list<P> base, std::initializer_list<P> more) {
  std::vector<P> v(base);
  v.insert(v.end(), more.begin(), more.end());
  return v;
}

std::vector<float> hold_v(const std::vector<P>& params, int note, std::uint8_t flags, std::uint32_t len, std::uint32_t total, bool square = false) {
  Rig r;
  r.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
  for (const P& p : params) {
    r.set(p.first, p.second);
  }
  r.script.on(0, note, flags);
  r.script.off(len, note);
  return r.render(total);
}

// RMS over a sliding window of `w` samples ending at i.
double win_rms(const std::vector<float>& v, std::size_t i, std::size_t w) { return rms(v, i + 1 - w, i + 1); }

}  // namespace

TEST_CASE("mods: SOFT ATK rises in the time it says, accented, tied and sliding notes stay instant") {
  // Note 96 (2.1 kHz) through an open filter: the output's RMS over one period follows the soft attack.
  const double want_ms[6] = {0.0, 0.3, 1.0, 3.0, 10.0, 30.0};
  for (unsigned step = 3; step <= 5; ++step) {
    const std::vector<float> v = hold_v(with(kOpen, {{AcidVoice::SoftAtk, static_cast<float>(step)}}), 96, 0, 6000, 8000);
    const double fin = win_rms(v, 3000, 441);
    REQUIRE(fin > 0.01);
    std::size_t t10 = 0;
    std::size_t t90 = 0;
    for (std::size_t i = 21; i < 3000; ++i) {
      const double r = win_rms(v, i, 21);
      if (t10 == 0 && r >= 0.1 * fin) {
        t10 = i;
      }
      if (t90 == 0 && r >= 0.9 * fin) {
        t90 = i;
        break;
      }
    }
    const double rise = static_cast<double>(t90 - t10) / 44.1;
    probe("soft atk: want ms, measured ms", want_ms[step], rise);
    INFO("SOFT ATK " << want_ms[step] << " ms measured " << rise << " ms");
    CHECK(rise > want_ms[step] * 0.8);
    CHECK(rise < want_ms[step] * 1.2);
  }
  // An accented note keeps the stock loop: bit for bit what it is with SOFT ATK off.
  const std::uint8_t acc = dsp::kNoteAccent;
  CHECK(same_bits(hold_v(with(kOpen, {{AcidVoice::SoftAtk, 5.0f}}), 60, acc, 3000, 4000), hold_v(with(kOpen, {{AcidVoice::SoftAtk, 0.0f}}), 60, acc, 3000, 4000)));

  // A note that takes over a held one (a tie), or glides from a sounding one (a slide), does not fade in: the level at its start is the level
  // before it. The first note is long past its own attack.
  for (const std::uint8_t flags : {static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(dsp::kNoteSlide)}) {
    Rig r;
    for (const P& p : kOpen) {
      r.set(p.first, p.second);
    }
    r.set(AcidVoice::SoftAtk, 5.0f);
    r.script.on(0, 96);
    r.script.on(8000, 100, flags);  // a tie (the first note is still held) in the first run, a slide in the second
    r.script.off(8040, 96);
    r.script.off(12000, 100);
    const std::vector<float> v = r.render(13000);
    const double before = win_rms(v, 7999, 441);
    const double after = win_rms(v, 8000 + 441, 441);
    probe("soft atk: level before a tie/slide, after", before, after);
    INFO("flags " << static_cast<int>(flags));
    CHECK(after > before * 0.9);
  }
}

TEST_CASE("mods: ACC DECAY scales the time the accented sweep takes, AMP DECAY the level's") {
  // An accented note with the whole opening on the accent (ENV MOD 1): the brightness falls as the cutoff does, at the accent's time. The
  // level is the same for every run (30 % of the way down from the first run's start to its floor), so the time it is crossed at is
  // proportional to tau: a and b in t = a + b * tau are the delay of the measurement and the slope; the slope is what is checked.
  constexpr std::size_t kW = 169;  // one period of note 60
  const auto render = [&](float acc_decay) {
    return hold_v({{AcidVoice::Cutoff, 80.0f}, {AcidVoice::Reso, 0.0f}, {AcidVoice::EnvMod, 1.0f}, {AcidVoice::Decay, 2500.0f}, {AcidVoice::Accent, 1.0f},
                   {AcidVoice::Volume, 0.0f}, {AcidVoice::AccDecay, acc_decay}},
                  60, dsp::kNoteAccent, 60000, 70000);
  };
  const std::vector<float> fast = render(60.0f);
  const double top = bright(fast, 100, 100 + kW);
  const double floor = bright(fast, 60000 - kW, 60000);
  const double level = floor + 0.3 * (top - floor);
  const auto fall_ms = [&](const std::vector<float>& v) {
    for (std::size_t i = 100; i + kW < 60000; i += 22) {
      if (bright(v, i, i + kW) < level) {
        return (static_cast<double>(i) + 0.5 * static_cast<double>(kW)) / 44.1;
      }
    }
    return 1e9;
  };
  const double t60 = fall_ms(fast);
  const double t180 = fall_ms(render(180.0f));
  const double t600 = fall_ms(render(600.0f));
  probe("acc decay: 60, 180, 600 ms crossing", t60, t180, t600);
  CHECK(t180 > t60);
  CHECK(t600 > t180);
  const double slope_a = (t180 - t60) / 120.0;
  const double slope_b = (t600 - t180) / 420.0;
  probe("acc decay: slopes", slope_a, slope_b);
  CHECK(near(slope_b, slope_a, 0.2));  // linear in tau: the time constant is the one that was set
  CHECK(t600 / t60 > 5.0);                                   // and not nothing: ten times the tau is several times the time

  // AMP DECAY: the level of a held note with the filter at rest falls as exp(-t / tau).
  for (const float tau : {300.0f, 1000.0f}) {
    const std::vector<float> v = hold_v(with(kOpen, {{AcidVoice::AmpDecay, tau}}), 48, 0, 60000, 66000);
    const double r = rms(v, 26460, 27460) / rms(v, 4410, 5410);  // 600 ms against 100 ms
    const double want = std::exp(-0.5 / (static_cast<double>(tau) * 0.001));
    probe("amp decay: tau, ratio, want", tau, r, want);
    INFO("AMP DECAY " << tau << " ms");
    CHECK(r > want * 0.85);
    CHECK(r < want * 1.15);
  }
}

TEST_CASE("mods: SLIDE sets the glide's time constant") {
  // Note 36, then a slide to 48: the pitch moves a one pole in Hz; the time to cover 63 % of the way is tau.
  const auto t63_ms = [&](float tau) {
    Rig r;
    for (const P& p : kOpen) {
      r.set(p.first, p.second);
    }
    r.set(AcidVoice::Slide, tau);
    r.script.on(0, 36);
    r.script.on(20000, 48, dsp::kNoteSlide);
    r.script.off(60000, 48);
    const std::vector<float> v = r.render(60000);
    const double f0 = hz_of(36);
    const double f1 = hz_of(48);
    const double goal = f0 + 0.632 * (f1 - f0);
    // The upward zero crossings, placed by linear interpolation; the pitch is the inverse of their spacing.
    double prev = -1.0;
    for (std::size_t i = 20001; i < v.size(); ++i) {
      if (v[i - 1] < 0.0f && v[i] >= 0.0f) {
        const double t = static_cast<double>(i - 1) + static_cast<double>(-v[i - 1]) / static_cast<double>(v[i] - v[i - 1]);
        if (prev >= 0.0 && kRate / (t - prev) >= goal) {
          return (t - 20000.0) / 44.1;
        }
        prev = t;
      }
    }
    return 1e9;
  };
  const double t60 = t63_ms(60.0f);
  const double t240 = t63_ms(240.0f);
  probe("slide: 60 ms, 240 ms (63 % time)", t60, t240);
  // The pitch is read from the spacing of crossings, a period late (15 ms at the start): the delay is the same for both, the slope is not.
  CHECK((t240 - t60) / 180.0 > 0.85);
  CHECK((t240 - t60) / 180.0 < 1.15);
  CHECK(t60 > 60.0 * 0.8);
  CHECK(t60 < 60.0 * 1.35);
}

TEST_CASE("mods: TRACKING moves the cutoff with the note, C2 stays put") {
  constexpr std::size_t kN = 16384;
  const auto render = [&](float track, int note) {
    return hold_v({{AcidVoice::Cutoff, 500.0f}, {AcidVoice::Reso, 0.0f}, {AcidVoice::EnvMod, 0.0f}, {AcidVoice::Decay, 2500.0f}, {AcidVoice::Volume, 0.0f},
                   {AcidVoice::Tracking, track}},
                  note, 0, 30000, 30000);
  };
  const auto cent = [&](float track, int note) { return centroid(spectrum(render(track, note), 8000, kN), kN, 20.0, 20000.0); };
  // The harmonic balance relative to the note: the energy from 1.5 to 12 times its pitch over the energy around it. With the cutoff tracking
  // the note, C4 and C2 have the same balance; with it fixed, C4 (a harmonic series spread two octaves wider) has lost most of its upper part.
  const auto balance = [&](float track, int note) {
    const std::vector<double> m = spectrum(render(track, note), 8000, kN);
    const double f = hz_of(note);
    return band(m, kN, 1.5 * f, 12.0 * f) / band(m, kN, 0.5 * f, 1.5 * f);
  };
  const double c2_0 = cent(0.0f, 36);
  const double c2_100 = cent(100.0f, 36);
  const double c4_100 = cent(100.0f, 60);
  const double c4_150 = cent(150.0f, 60);
  const double bal_0 = balance(0.0f, 60) / balance(0.0f, 36);
  const double bal_100 = balance(100.0f, 60) / balance(100.0f, 36);
  probe("tracking: C4/C2 centroid at 100 %, balance ratio at 0 %, at 100 %", c4_100 / c2_100, bal_0, bal_100);
  CHECK(near(c2_100, c2_0, 0.001));  // C2 is the reference: unchanged at any setting
  CHECK(c4_100 / c2_100 > 4.0 * 0.8);                     // two octaves of centroid for two octaves of note
  CHECK(c4_100 / c2_100 < 4.0 * 1.25);
  CHECK(c4_150 > c4_100 * 0.99);                          // over-tracking: the filter is further open, so the centroid is not lower
  CHECK(bal_100 > 0.7);                                   // tracked: the same balance...
  CHECK(bal_100 < 1.4);
  CHECK(bal_0 < 0.5 * bal_100);                           // ...untracked: the upper part is gone
}

TEST_CASE("mods: FILTER FM changes the sound and stays bounded") {
  const std::initializer_list<P> base = {{AcidVoice::Cutoff, 700.0f}, {AcidVoice::Reso, 0.8f}, {AcidVoice::EnvMod, 0.3f}, {AcidVoice::Decay, 2500.0f}, {AcidVoice::Volume, 0.0f}};
  const std::vector<float> off = hold_v(with(base, {}), 36, 0, 30000, 30000);
  const std::vector<float> half = hold_v(with(base, {{AcidVoice::FilterFm, 0.5f}}), 36, 0, 30000, 30000);
  const std::vector<float> full = hold_v(with(base, {{AcidVoice::FilterFm, 1.0f}}), 36, 0, 30000, 30000);
  CHECK(all_finite(full));
  CHECK(peak(full, 0, full.size()) < 2.0);
  const auto diff = [&](const std::vector<float>& a, const std::vector<float>& b) {
    double s = 0.0;
    for (std::size_t i = 8000; i < a.size(); ++i) {
      s += std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
    }
    return s / static_cast<double>(a.size() - 8000) / rms(a, 8000, a.size());
  };
  probe("filter fm: diff half, full; bright off, half, full", diff(off, half), diff(off, full), bright(full, 8000, 30000) / bright(off, 8000, 30000));
  CHECK(diff(off, half) > 0.02);
  CHECK(diff(off, full) > diff(off, half));
  constexpr std::size_t kN = 16384;
  const std::vector<double> so = spectrum(off, 8000, kN);
  const std::vector<double> sf = spectrum(full, 8000, kN);
  CHECK(std::fabs(db(band(sf, kN, 300.0, 3000.0) / band(so, kN, 300.0, 3000.0))) > 0.5);  // the spectrum moved
}

TEST_CASE("mods: MUFFLER lowers the peak, HARD lets the bass through") {
  // A loud accented bass, driven into the ladder so that the peaks are well past the muffler's thresholds.
  const std::initializer_list<P> base = {{AcidVoice::Cutoff, 600.0f}, {AcidVoice::Reso, 0.9f},    {AcidVoice::EnvMod, 0.5f},
                                         {AcidVoice::Decay, 2500.0f}, {AcidVoice::Volume, 0.0f}, {AcidVoice::Accent, 1.0f}};
  constexpr std::size_t kN = 16384;
  const auto render = [&](float type) {
    return hold_v(with(base, {{AcidVoice::Muffler, type}}), 36, dsp::kNoteAccent, 30000, 30000);
  };
  const std::vector<float> off = render(0.0f);
  const std::vector<float> soft = render(1.0f);
  const std::vector<float> hard = render(2.0f);
  const double p_off = peak(off, 8000, 30000);
  const double p_soft = peak(soft, 8000, 30000);
  const double p_hard = peak(hard, 8000, 30000);
  probe("muffler peak off, soft, hard", p_off, p_soft, p_hard);
  CHECK(all_finite(soft));
  CHECK(all_finite(hard));
  CHECK(p_soft < p_off * 0.9);
  CHECK(p_hard < p_off * 0.97);
  const std::vector<double> so = spectrum(off, 8000, kN);
  const std::vector<double> sh = spectrum(hard, 8000, kN);
  const double bass = db(band(sh, kN, 20.0, 100.0) / band(so, kN, 20.0, 100.0));
  probe("muffler HARD: bass change dB, highs change dB", bass, db(band(sh, kN, 300.0, 5000.0) / band(so, kN, 300.0, 5000.0)));
  CHECK(std::fabs(bass) < 1.0);
  CHECK(band(sh, kN, 300.0, 5000.0) < band(so, kN, 300.0, 5000.0));
}

TEST_CASE("mods: SUB OSC adds energy an octave down, more at each step") {
  constexpr std::size_t kN = 16384;
  const double f = hz_of(48) / 2.0;
  const auto sub_energy = [&](float level, bool square) {
    Rig r;
    r.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
    for (const P& p : kOpen) {
      r.set(p.first, p.second);
    }
    r.set(AcidVoice::SubOsc, level);
    r.script.on(0, 48);
    const std::vector<float> v = r.render(30000);
    return band(spectrum(v, 8000, kN), kN, f - 4.0, f + 4.0);
  };
  for (const bool square : {false, true}) {
    const double e0 = sub_energy(0.0f, square);
    const double e1 = sub_energy(1.0f, square);
    const double e2 = sub_energy(2.0f, square);
    const double e3 = sub_energy(3.0f, square);
    probe("sub: energy at f/2 off, low, mid, high (dB over low)", db(std::sqrt(e0 / e1)), 0.0, db(std::sqrt(e3 / e1)));
    INFO((square ? "SQR" : "SAW"));
    CHECK(e1 > e0 * 100.0);  // at least 20 dB over what a saw or a square has there (nothing)
    CHECK(e2 > e1 * 1.5);
    CHECK(e3 > e2 * 1.5);
  }
}

TEST_CASE("mods: OVERDRIVE adds harmonics, and -24 dB all but silences the oscillator") {
  // Resonance 0: the ladder is then a plain saturating low-pass, and the level at its input is all there is to read.
  constexpr std::size_t kN = 16384;
  const std::initializer_list<P> base = {{AcidVoice::Cutoff, 1200.0f}, {AcidVoice::Reso, 0.0f}, {AcidVoice::EnvMod, 0.0f}, {AcidVoice::Decay, 2500.0f}, {AcidVoice::Volume, 0.0f}};
  const auto render = [&](float db_in) { return hold_v(with(base, {{AcidVoice::Overdrive, db_in}}), 36, 0, 30000, 30000); };
  const std::vector<float> d0 = render(0.0f);
  const std::vector<float> d24 = render(24.0f);
  const std::vector<float> dm24 = render(-24.0f);
  // The share of the energy above 500 Hz (the saw's fundamental is 65 Hz): a hard-driven ladder squares the wave off.
  const auto share = [&](const std::vector<float>& v) {
    const std::vector<double> m = spectrum(v, 8000, kN);
    return band(m, kN, 500.0, 20000.0) / band(m, kN, 20.0, 20000.0);
  };
  probe("overdrive: hi share at 0, +24; rms -24 over 0", share(d0), share(d24), rms(dm24, 8000, 30000) / rms(d0, 8000, 30000));
  CHECK(all_finite(d24));
  CHECK(share(d24) > share(d0) * 1.5);
  CHECK(rms(dm24, 8000, 30000) < 0.12 * rms(d0, 8000, 30000));
  CHECK(rms(dm24, 8000, 30000) > 0.0);
}

TEST_CASE("mods: SWP SPEED, SLOW builds up over close accents and FAST gives less to the second") {
  // Accents every 100 ms, the whole sweep from the accent (ENV MOD 0): the brightness of the first 8 ms after each onset.
  const auto onsets = [&](float speed) {
    Rig r;
    for (const P& p : {P{AcidVoice::Cutoff, 300.0f}, P{AcidVoice::Reso, 0.3f}, P{AcidVoice::EnvMod, 0.0f}, P{AcidVoice::Decay, 2500.0f}, P{AcidVoice::Volume, 0.0f},
                       P{AcidVoice::Accent, 1.0f}}) {
      r.set(p.first, p.second);
    }
    r.set(AcidVoice::SwpSpeed, speed);
    for (std::uint32_t k = 0; k < 4; ++k) {
      r.script.on(k * 4410u, 36, dsp::kNoteAccent);
      r.script.off(k * 4410u + 3000u, 36);
    }
    const std::vector<float> v = r.render(4 * 4410);
    std::vector<double> b;
    for (std::uint32_t k = 0; k < 4; ++k) {
      const std::size_t from = k * 4410u + 100u;
      b.push_back(bright(v, from, from + 352) / rms(v, from, from + 352));
    }
    return b;
  };
  const std::vector<double> normal = onsets(1.0f);
  const std::vector<double> slow = onsets(2.0f);
  const std::vector<double> fast = onsets(0.0f);
  probe("swp normal 1..4", normal[0], normal[1], normal[3]);
  probe("swp slow 1..4", slow[0], slow[1], slow[3]);
  probe("swp fast 1..4", fast[0], fast[1], fast[3]);
  // SLOW takes longer to rise and can rise higher: its first accent opens less than NORMAL's, its fourth, with the charge of the three before it,
  // more than NORMAL's (and, in itself, several times its own first).
  CHECK(slow[0] < 0.6 * normal[0]);
  CHECK(slow[3] > 1.15 * normal[3]);
  CHECK(slow[3] > 2.0 * slow[0]);
  // The first accent of a cold voice reads lower than the later ones in every mode (the accent's lift is smoothed from zero), so FAST is read
  // against NORMAL, which has no memory: what the second accent gets, over what it would get in NORMAL, is less than for the first.
  CHECK(fast[1] / normal[1] < 0.95 * (fast[0] / normal[0]));
  CHECK(fast[3] / normal[3] < fast[1] / normal[1]);
  CHECK(near(normal[3], normal[1], 0.05));  // today's: every accent after the first the same
}

TEST_CASE("mods: ACC SWEEP OFF lifts nothing, HI RES resonates more") {
  const std::initializer_list<P> base = {{AcidVoice::Cutoff, 300.0f}, {AcidVoice::Reso, 0.3f},    {AcidVoice::EnvMod, 0.0f},
                                         {AcidVoice::Decay, 2500.0f}, {AcidVoice::Volume, 0.0f}, {AcidVoice::Accent, 1.0f}};
  const auto tilt = [&](float sweep, std::uint8_t flags) {
    const std::vector<float> v = hold_v(with(base, {{AcidVoice::AccSweep, sweep}}), 36, flags, 30000, 30000);
    return bright(v, 2000, 4000) / rms(v, 2000, 4000);
  };
  const double plain_off = tilt(0.0f, 0);
  const double acc_off = tilt(0.0f, dsp::kNoteAccent);
  const double acc_norm = tilt(1.0f, dsp::kNoteAccent);
  probe("acc sweep: tilt plain/off, accent/off, accent/normal", plain_off, acc_off, acc_norm);
  CHECK(acc_norm > plain_off * 1.3);                           // the sweep lifts the cutoff on an accent...
  CHECK(near(acc_off, plain_off, 0.05));  // ...and OFF has none of it

  // HI RES: the resonant peak around the cutoff stands higher (the energy from 0.6 to 1.5 kHz of a note whose harmonics are 33 Hz apart).
  constexpr std::size_t kN = 16384;
  // (RESO 0.3: doubled it is 0.6, past the knee where the ladder rings; at 0.7 it is saturated either way and the two are alike.)
  const auto peak_band = [&](float sweep) {
    const std::vector<float> v = hold_v({{AcidVoice::Cutoff, 1000.0f}, {AcidVoice::Reso, 0.3f}, {AcidVoice::EnvMod, 0.0f}, {AcidVoice::Decay, 2500.0f}, {AcidVoice::Volume, 0.0f},
                                         {AcidVoice::AccSweep, sweep}},
                                        24, 0, 30000, 30000);
    return band(spectrum(v, 8000, kN), kN, 600.0, 1500.0);
  };
  probe("hi res: band energy ratio", peak_band(2.0f) / peak_band(1.0f));
  CHECK(peak_band(2.0f) > peak_band(1.0f) * 2.0);
}

// ---- a mod set live reaches the loop that computes it ---------------------------------------------------------------------------------

// The mods that live in the modded loop (Sub Osc, Filter FM, HI RES, Muffler, Overdrive) are chosen per control period by control(): a value
// that arrives while the voice runs, with no note and no ramp going, must make that choice too. A mod set live between notes is, once the
// voice has settled, heard exactly as if it had been set before the voice started (the same bits).
TEST_CASE("mods: a mod set live, between notes, is heard exactly as if it had been set before the voice started, every mod at both ends") {
  constexpr std::uint32_t kPre = 40000;   // silence before the gesture: the live set is at 2000, and the free-running filter settles after it
  constexpr std::uint32_t kLen = 60000;   // the first twelve steps of the gesture: accents, two slides, three parameter moves
  for (const bool square : {false, true}) {
    for (unsigned id = kFirst; id < kLast; ++id) {
      for (const float extreme : {lo_of(id), hi_of(id)}) {
        INFO((square ? "SQR " : "SAW ") << "mod " << std::string(desc(id).name) << " at " << extreme);
        Rig before;
        before.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
        before.set(static_cast<std::uint16_t>(id), extreme);
        build_gesture(before.script, WaveMove::Keep, kPre);
        Rig live;
        live.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
        live.script.param(2000, static_cast<std::uint16_t>(id), extreme);
        build_gesture(live.script, WaveMove::Keep, kPre);
        const std::vector<float> a = before.render(kPre + kLen);
        const std::vector<float> b = live.render(kPre + kLen);
        double d2 = 0.0;
        for (std::size_t i = kPre; i < a.size(); ++i) {
          d2 += (static_cast<double>(a[i]) - static_cast<double>(b[i])) * (static_cast<double>(a[i]) - static_cast<double>(b[i]));
        }
        const double rel = std::sqrt(d2 / static_cast<double>(a.size() - kPre)) / rms(a, kPre, a.size());
        probe("live vs before: first diff frame, relative rms of the difference", static_cast<double>(first_diff(a, b)), rel);
        if (id == AcidVoice::Overdrive && extreme < 0.0f) {
          // -24 dB leaves the ladder almost undriven: it rings by its own feedback, a limit cycle whose phase is that of the moment the
          // level came in. The two voices ring alike (the same power), not in step.
          CHECK(near(rms(a, kPre, a.size()), rms(b, kPre, b.size()), 0.1));
          CHECK(near(peak(a, kPre, a.size()), peak(b, kPre, b.size()), 0.2));
        } else {
          CHECK(same_bits(a, b, kPre));
          if (!same_bits(a, b, kPre)) {
            MESSAGE("first difference at frame " << first_diff(a, b) << " (the gesture starts at " << kPre << ")");
          }
        }
      }
    }
  }
}

// HI RES set live in the middle of a held note, at the top of RESO: the saturation that bounds the doubled feedback must be the modded
// loop's. Without it the unclamped rational saturator runs the loop away from where a voice that had HI RES from the start settles.
TEST_CASE("mods: HI RES set live mid-note, at RESO 1.0, settles where HI RES from the start does, bounded") {
  const auto note = [&](bool live) {
    Rig r;
    r.set(AcidVoice::Cutoff, 1200.0f);
    r.set(AcidVoice::Reso, 1.0f);
    r.set(AcidVoice::EnvMod, 0.0f);
    r.set(AcidVoice::Decay, 2500.0f);
    r.set(AcidVoice::Volume, 0.0f);
    if (live) {
      r.script.param(2000, AcidVoice::AccSweep, 2.0f);
    } else {
      r.set(AcidVoice::AccSweep, 2.0f);
    }
    r.script.on(100, 36, 0);
    return r.render(40000);
  };
  const std::vector<float> from_start = note(false);
  const std::vector<float> live = note(true);
  const double p_start = peak(from_start, 20000, 40000);
  const double p_live = peak(live, 20000, 40000);
  const double r_start = rms(from_start, 20000, 40000);
  const double r_live = rms(live, 20000, 40000);
  probe("hi res live: peak start/live, rms start/live", p_start, p_live, r_start);
  probe("hi res live: rms live", r_live);
  CHECK(p_live <= 1.2);
  CHECK(near(p_live, p_start, 0.05));
  CHECK(near(r_live, r_start, 0.05));
}

// ---- guards (docs/MODS-PLAN.md 3.5) ---------------------------------------------------------------------------------------------------

TEST_CASE("mods: a value that is not a number reads as the stock value, for every mod") {
  constexpr std::uint32_t kPre = 100000;  // a silent prelude: long enough for the free-running filter to settle again
  const std::vector<float> ref = fresh(false, kPre);
  const float bad[3] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
  for (unsigned id = kFirst; id <= kLast; ++id) {
    for (const float v : bad) {
      INFO("mod " << std::string(desc(id).name) << " at " << v);
      Rig r;
      r.script.param(100, static_cast<std::uint16_t>(id), v);
      build_gesture(r.script, WaveMove::Keep, kPre);
      const std::vector<float> got = r.render(kGestureFrames + kPre);
      CHECK(all_finite(got));
      CHECK(same_bits(got, ref, kPre));
    }
  }
  // After a real value: back to stock, once the ramp has landed.
  for (unsigned id = kFirst; id <= kLast; ++id) {
    Rig r;
    r.script.param(100, static_cast<std::uint16_t>(id), hi_of(id));
    r.script.param(1000, static_cast<std::uint16_t>(id), bad[0]);
    build_gesture(r.script, WaveMove::Keep, kPre);
    INFO("mod " << std::string(desc(id).name) << " NaN after a value");
    CHECK(same_bits(r.render(kGestureFrames + kPre), ref, kPre));
  }
}

TEST_CASE("mods: every mod at each end of its range stays finite and bounded over the gesture") {
  for (const bool square : {false, true}) {
    for (unsigned id = kFirst; id < kLast; ++id) {
      for (const float extreme : {lo_of(id), hi_of(id)}) {
        INFO((square ? "SQR " : "SAW ") << "mod " << std::string(desc(id).name) << " at " << extreme);
        Rig r;
        r.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
        r.set(static_cast<std::uint16_t>(id), extreme);
        build_gesture(r.script, WaveMove::Keep);
        const std::vector<float> v = r.render(kGestureFrames);
        CHECK(all_finite(v));
        CHECK(none_subnormal(v));
        const double p = peak(v, 0, v.size());
        probe(desc(id).name, extreme, p);
        CHECK(p <= 2.0);  // the volume is -3 dB: 2.0 before it is 1.4 here, a bound with room
      }
    }
  }
}

namespace {

struct Config {
  const char* name;
  float v[kLast - kFirst];  // ids 8..18
};

// The stock values, the two ends, and two mixes meant to hurt: everything that makes the signal hot or the filter wild together.
const Config kConfigs[] = {
    {"stock", {180.0f, 0.0f, 60.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 4000.0f}},
    {"all min", {30.0f, 0.0f, 60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -24.0f, 16.0f}},
    {"all max", {3000.0f, 150.0f, 360.0f, 5.0f, 3.0f, 1.0f, 2.0f, 2.0f, 2.0f, 36.0f, 4000.0f}},
    {"hostile 1", {30.0f, 150.0f, 360.0f, 0.0f, 3.0f, 1.0f, 2.0f, 2.0f, 2.0f, 36.0f, 4000.0f}},
    {"hostile 2", {3000.0f, 150.0f, 60.0f, 5.0f, 3.0f, 1.0f, 2.0f, 0.0f, 0.0f, 36.0f, 16.0f}},
    // The one mod that only the modded loop's saturator and doubled feedback carry, alone: nothing else asks for that loop.
    {"hi res only", {180.0f, 0.0f, 60.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 1.0f, 0.0f, 4000.0f}},
};
constexpr unsigned kConfigCount = sizeof(kConfigs) / sizeof(kConfigs[0]);

}  // namespace

namespace {

// Renders `total` frames in blocks of 64, reading the state after every block; `why` names the first state out of its limit.
std::vector<float> render_watched(Rig& r, std::uint32_t total, const char*& why, float& worst_ladder) {
  std::vector<float> out;
  out.reserve(total);
  why = nullptr;
  for (std::uint32_t done = 0; done < total; done += 64) {
    const std::vector<float> chunk = r.render(std::min<std::uint32_t>(64, total - done));
    out.insert(out.end(), chunk.begin(), chunk.end());
    if (why == nullptr) {
      why = o3_303::AcidVoiceTestAccess::violation(r.voice, worst_ladder);
    }
  }
  return out;
}

}  // namespace

// `live` 0: the mods are set before the voice starts. 1: set live at frame 50, between notes (the first note is at 100). 2: set live at
// frame 1200, in the middle of the first note. The second and third take the ramp-pending path, the first the start() path.
TEST_CASE("mods: the 576 corners of the voice, with the mods at their stock values, their ends, two hostile mixes and HI RES alone, set before the start and live") {
  const float waves[2] = {0.0f, 1.0f};
  const float resos[2] = {0.65f, 1.0f};
  const float cutoffs[3] = {25.0f, 350.0f, 4500.0f};
  const float envmods[2] = {0.0f, 1.0f};
  const float accents[2] = {0.0f, 1.0f};
  const float tunes[3] = {-12.0f, 0.0f, 12.0f};
  const int notes[4] = {0, 24, 60, 127};
  std::uint32_t corners = 0;
  std::uint32_t live_corners = 0;
  double worst = 0.0;
  float worst_ladder = 0.0f;
  for (unsigned pass = 0; pass < 2; ++pass) {
    for (const Config& cfg : kConfigs) {
      for (const float wave : waves) {
        for (const float reso : resos) {
          for (const float cutoff : cutoffs) {
            for (const float envmod : envmods) {
              for (const float accent : accents) {
                for (const float tune : tunes) {
                  for (const int note : notes) {
                    Rig r;
                    r.set(AcidVoice::Wave, wave);
                    r.set(AcidVoice::Reso, reso);
                    r.set(AcidVoice::Cutoff, cutoff);
                    r.set(AcidVoice::EnvMod, envmod);
                    r.set(AcidVoice::Accent, accent);
                    r.set(AcidVoice::Tune, tune);
                    r.set(AcidVoice::Volume, 6.0f);
                    r.set(AcidVoice::Decay, 2500.0f);
                    const unsigned live = pass == 0 ? 0u : 1u + (corners & 1u);
                    const std::uint32_t at = live == 1u ? 50u : 1200u;
                    for (unsigned i = 0; i < kLast - kFirst; ++i) {
                      if (live == 0u) {
                        r.set(static_cast<std::uint16_t>(kFirst + i), cfg.v[i]);
                      } else if (cfg.v[i] != desc(kFirst + i).def) {  // only what changes: a neighbour's set must not stand in for this one's
                        r.script.param(at, static_cast<std::uint16_t>(kFirst + i), cfg.v[i]);
                      }
                    }
                    const int other = note < 100 ? note + 7 : note - 7;
                    r.script.on(100, note, accent > 0.5f ? dsp::kNoteAccent : 0);
                    r.script.on(2500, other, dsp::kNoteSlide);
                    r.script.off(4000, other);
                    const char* why = nullptr;
                    const std::vector<float> v = render_watched(r, 5000, why, worst_ladder);
                    ++corners;
                    live_corners += live != 0u ? 1u : 0u;
                    const double p = peak(v, 0, v.size());
                    worst = std::max(worst, p);
                    // Whichever way the mods came in, a mod that is not at its stock value (Sub Osc, Filter FM, HI RES, Muffler, Overdrive: ids 12, 13, 14,
                    // 15, 17 of the table) has the voice in the modded loop once the ramps have landed.
                    const bool needs = cfg.v[AcidVoice::SubOsc - kFirst] != 0.0f || cfg.v[AcidVoice::FilterFm - kFirst] != 0.0f || cfg.v[AcidVoice::AccSweep - kFirst] == 2.0f ||
                                       cfg.v[AcidVoice::Muffler - kFirst] != 0.0f || cfg.v[AcidVoice::Overdrive - kFirst] != 0.0f;
                    const bool wrong_loop = o3_303::AcidVoiceTestAccess::modded(r.voice) != needs;
                    if (!all_finite(v) || !none_subnormal(v) || p > 2.0 * 1.9953 || why != nullptr || wrong_loop) {  // 2.0 before the volume (+6 dB)
                      FAIL("pass " << pass << " live " << live << " corner " << std::string(cfg.name) << " wave " << wave << " reso " << reso << " cutoff " << cutoff << " envmod " << envmod
                                   << " accent " << accent << " tune " << tune << " note " << note << ": peak " << p << ", state out of its limit: " << std::string(why != nullptr ? why : "none") << ", the modded loop is " << std::string(wrong_loop ? "not what the values need" : "right"));
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  probe("corners, worst peak (after a volume of +6 dB), worst ladder state", static_cast<double>(corners), worst, static_cast<double>(worst_ladder));
  CHECK(corners == 2u * kConfigCount * 576u);
  CHECK(live_corners == kConfigCount * 576u);
}

TEST_CASE("mods: every state flushes to exactly zero after silence, with the mods at their ends") {
  for (const Config& cfg : kConfigs) {
    for (const bool square : {false, true}) {
      Rig r;
      r.set(AcidVoice::Wave, square ? 1.0f : 0.0f);
      r.set(AcidVoice::Reso, 0.9f);
      r.set(AcidVoice::Volume, 6.0f);
      for (unsigned i = 0; i < kLast - kFirst; ++i) {
        r.set(static_cast<std::uint16_t>(kFirst + i), cfg.v[i]);
      }
      r.script.on(0, 36, dsp::kNoteAccent);
      r.script.on(3000, 48, dsp::kNoteSlide);
      r.script.off(6000, 36);
      r.script.off(6000, 48);
      const std::vector<float> v = r.render(6000 + 88200);
      INFO("config " << cfg.name << (square ? " SQR" : " SAW"));
      CHECK(all_finite(v));
      CHECK(none_subnormal(v));
      bool zero = true;
      for (std::size_t i = v.size() - 10000; i < v.size(); ++i) {
        zero = zero && v[i] == 0.0f;
      }
      CHECK(zero);
    }
  }
}
