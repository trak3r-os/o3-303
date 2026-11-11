// SPDX-License-Identifier: GPL-3.0-or-later
// TRACT (units/tract) driven directly, block by block: the table, Mix 0 passing the input bit for bit (in place or not, and again after a
// ramp back to 0), the stereo behaviour (the wet path is the mono sum written to both channels, the dry path stays stereo), SIZE moving
// the formants and switching at its frame, the block size not mattering, and no NaN or runaway under random parameter motion.
#include <doctest/doctest.h>

#include "support/need.h"

#include <o3_303/units/tract/tract.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {

namespace dsp = o3sdk::dsp;
using o3_303::TractFx;

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
Stereo run(TractFx& u, const Stereo& in, const std::vector<Ev>& events, std::uint32_t block = 64, bool in_place = true) {
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
    u.process(a);
  }
  return in_place ? work : dst;
}

Stereo mono(std::vector<float> v) { return Stereo{v, v}; }

// A bass-like source: a saw of `f0` Hz (summed to 40 harmonics, so band-limited), peak about 0.5. f0 dividing the rate in whole cycles per
// second keeps a whole number of periods in the analysis windows.
std::vector<float> saw(std::size_t n, double f0, double amp = 0.3) {
  std::vector<float> v(n, 0.0f);
  for (std::size_t i = 0; i < n; ++i) {
    double acc = 0.0;
    for (int h = 1; h <= 40; ++h) {
      acc += std::sin(2.0 * kPi * f0 * h * static_cast<double>(i) / kRate) / h;
    }
    v[i] = static_cast<float>(amp * acc);
  }
  return v;
}

std::vector<float> noise(std::size_t n, std::uint32_t seed, float amp) {
  std::vector<float> v(n);
  std::uint32_t s = seed;
  for (auto& x : v) {
    s = s * 1664525u + 1013904223u;
    x = amp * (static_cast<float>(s >> 8) / 8388608.0f - 1.0f);
  }
  return v;
}

bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
  return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

bool all_finite(const Stereo& s) {
  for (std::size_t i = 0; i < s.l.size(); ++i) {
    if (!std::isfinite(s.l[i]) || !std::isfinite(s.r[i])) {
      return false;
    }
  }
  return true;
}

// The power of `v` at the harmonics k f0 of a whole-period window [from, to), for the harmonics between lo and hi Hz.
double band_power(const std::vector<float>& v, std::size_t from, std::size_t to, double f0, double lo, double hi) {
  double total = 0.0;
  for (double f = f0; f < 20000.0; f += f0) {
    if (f < lo || f > hi) {
      continue;
    }
    double re = 0.0;
    double im = 0.0;
    for (std::size_t i = from; i < to; ++i) {
      const double ph = 2.0 * kPi * f * static_cast<double>(i) / kRate;
      re += static_cast<double>(v[i]) * std::cos(ph);
      im += static_cast<double>(v[i]) * std::sin(ph);
    }
    total += re * re + im * im;
  }
  return total;
}

}  // namespace

TEST_CASE("tract: the table is valid, in the contract's order, and Mix defaults to 0") {
  TractFx u;
  const auto& info = u.info();
  CHECK(info.kind == dsp::UnitKind::Effect);
  CHECK(std::strcmp(info.name, "TRACT") == 0);
  NEED(info.param_count == TractFx::kParamCount);
  CHECK(TractFx::kParamCount == 7);
  CHECK(TractFx::Vowel == 0);
  CHECK(TractFx::Tongue == 1);
  CHECK(TractFx::Lips == 2);
  CHECK(TractFx::Res == 3);
  CHECK(TractFx::Glide == 4);
  CHECK(TractFx::Size == 5);
  CHECK(TractFx::Mix == 6);
  const char* const names[] = {"VOWEL", "TONGUE", "LIPS", "RES", "GLIDE", "SIZE", "MIX"};
  for (unsigned p = 0; p < info.param_count; ++p) {
    INFO(info.params[p].name);
    CHECK(o3sdk::core::param_desc_valid(info.params[p]));
    CHECK(std::strcmp(info.params[p].name, names[p]) == 0);
  }
  using o3sdk::core::ParamCurve;
  using o3sdk::core::ParamUnit;
  CHECK(info.params[TractFx::Vowel].min == 0.0f);
  CHECK(info.params[TractFx::Vowel].max == 1.0f);
  CHECK(info.params[TractFx::Vowel].def == 0.0f);
  CHECK(info.params[TractFx::Vowel].unit == ParamUnit::NormPct);
  CHECK((info.params[TractFx::Vowel].flags & o3sdk::core::kParamSmooth) != 0);
  // TONGUE and LIPS: -100..+100 % (the SDK has no signed percent display of a -1..+1 range)
  for (const auto id : {TractFx::Tongue, TractFx::Lips}) {
    CHECK(info.params[id].min == -100.0f);
    CHECK(info.params[id].max == 100.0f);
    CHECK(info.params[id].def == 0.0f);
    char text[o3sdk::core::kParamTextChars + 1];
    o3sdk::core::param_format(info.params[id], -100.0f, text, sizeof text);
    CHECK(std::strcmp(text, "-100") == 0);
    o3sdk::core::param_format(info.params[id], 100.0f, text, sizeof text);
    CHECK(std::strcmp(text, "100") == 0);
  }
  CHECK(info.params[TractFx::Res].def == 0.5f);
  CHECK(info.params[TractFx::Res].unit == ParamUnit::NormPct);
  CHECK(info.params[TractFx::Glide].min == 5.0f);
  CHECK(info.params[TractFx::Glide].max == 500.0f);
  CHECK(info.params[TractFx::Glide].def == 30.0f);
  CHECK(info.params[TractFx::Glide].curve == ParamCurve::Log);
  const auto& size = info.params[TractFx::Size];
  CHECK(size.unit == ParamUnit::Enum);
  NEED(size.list_count == 5);
  CHECK(size.min == 0.0f);
  CHECK(size.max == 4.0f);
  CHECK(size.def == 2.0f);
  const char* const sizes[] = {"BIG", "LARGE", "NORMAL", "SMALL", "TINY"};
  for (unsigned i = 0; i < 5; ++i) {
    CHECK(std::strcmp(size.names[i], sizes[i]) == 0);
  }
  CHECK(info.params[TractFx::Mix].def == 0.0f);  // wired and untouched is transparent
  CHECK(info.params[TractFx::Mix].unit == ParamUnit::NormPct);
  CHECK(u.idle());
}

TEST_CASE("tract: Mix 0 is the input bit for bit, in place or not, whatever the other parameters say") {
  const std::size_t n = 4096;
  Stereo in{noise(n, 11, 0.9f), noise(n, 12, 0.9f)};
  const std::vector<Ev> moves = {{0, TractFx::Vowel, 0.7f},  {0, TractFx::Tongue, 80.0f}, {0, TractFx::Lips, -60.0f},
                                 {100, TractFx::Res, 1.0f},  {200, TractFx::Glide, 5.0f},  {300, TractFx::Size, 0.0f},
                                 {400, TractFx::Vowel, 0.1f}};
  for (const bool in_place : {true, false}) {
    for (const std::uint32_t block : {64u, 5u}) {
      TractFx u;
      u.prepare(static_cast<float>(kRate));
      const Stereo out = run(u, in, moves, block, in_place);
      CHECK(same_bits(out.l, in.l));
      CHECK(same_bits(out.r, in.r));
      CHECK(u.idle());
    }
  }
  // A unit that was running and ramped back to 0 is exact again once the ramp has landed.
  TractFx u;
  u.prepare(static_cast<float>(kRate));
  const std::size_t m = 20000;
  Stereo in2{saw(m, 50.0), saw(m, 50.0)};
  const Stereo out = run(u, in2, {{0, TractFx::Mix, 1.0f}, {8000, TractFx::Mix, 0.0f}});
  CHECK(u.idle());
  const std::size_t from = 8000 + 400;  // the ramp is 5 ms (221 frames)
  const std::vector<float> a(out.l.begin() + static_cast<std::ptrdiff_t>(from), out.l.end());
  const std::vector<float> b(in2.l.begin() + static_cast<std::ptrdiff_t>(from), in2.l.end());
  CHECK(same_bits(a, b));
  // and the ramp itself (mix going down) ends on an exact input sample
  CHECK(out.l[8000 + 300] == in2.l[8000 + 300]);
}

TEST_CASE("tract: a fully wet unit writes the mono sum's tube to both channels, and the dry path stays stereo") {
  const std::size_t n = 12000;
  const Stereo in{saw(n, 50.0), noise(n, 5, 0.2f)};
  // Mix 1: both channels are the same signal, and it is the processor's output of the mono sum.
  {
    TractFx u;
    u.prepare(static_cast<float>(kRate));
    const Stereo out = run(u, in, {{0, TractFx::Vowel, 0.5f}, {0, TractFx::Mix, 1.0f}});
    NEED(all_finite(out));
    const auto tail = [](const std::vector<float>& v) { return std::vector<float>(v.begin() + 300, v.end()); };  // past the 5 ms Mix ramp
    CHECK(same_bits(tail(out.l), tail(out.r)));
    std::vector<float> sum(n);
    for (std::size_t i = 0; i < n; ++i) {
      sum[i] = 0.5f * (in.l[i] + in.r[i]);
    }
    TractFx v;
    v.prepare(static_cast<float>(kRate));
    const Stereo ref = run(v, mono(sum), {{0, TractFx::Vowel, 0.5f}, {0, TractFx::Mix, 1.0f}});
    // the same events at the same frames: the same wet path (the mono input of a mono source is the reference)
    CHECK(same_bits(tail(out.l), tail(ref.l)));
  }
  // A signal that cancels in the mono sum has no wet path.
  {
    std::vector<float> neg(n);
    const std::vector<float> s = saw(n, 50.0);
    for (std::size_t i = 0; i < n; ++i) {
      neg[i] = -s[i];
    }
    TractFx u;
    u.prepare(static_cast<float>(kRate));
    const Stereo out = run(u, Stereo{s, neg}, {{0, TractFx::Mix, 1.0f}});
    for (std::size_t i = 300; i < n; ++i) {   // after the 5 ms Mix ramp: the dry part is gone, the wet sum is zero
      REQUIRE(out.l[i] == 0.0f);
      REQUIRE(out.r[i] == 0.0f);
    }
  }
  // Half mixed, settled: left = dry L + 0.5 (wet - dry L), right likewise, so L - R = 0.5 (dry L - dry R): the image is in the dry part only.
  {
    TractFx u;
    u.prepare(static_cast<float>(kRate));
    const Stereo half = run(u, in, {{0, TractFx::Vowel, 0.5f}, {0, TractFx::Mix, 0.5f}});
    TractFx v;
    v.prepare(static_cast<float>(kRate));
    const Stereo wet = run(v, in, {{0, TractFx::Vowel, 0.5f}, {0, TractFx::Mix, 1.0f}});
    for (std::size_t i = 4000; i < n; ++i) {
      const float want_l = in.l[i] + (wet.l[i] - in.l[i]) * 0.5f;
      const float want_r = in.r[i] + (wet.r[i] - in.r[i]) * 0.5f;
      REQUIRE(half.l[i] == doctest::Approx(want_l).epsilon(1e-5));
      REQUIRE(half.r[i] == doctest::Approx(want_r).epsilon(1e-5));
    }
    CHECK(half.l[8000] != half.r[8000]);
  }
}

TEST_CASE("tract: SIZE moves the formants (BIG low, TINY high) and a switch at a frame keeps the signal finite and bounded") {
  const std::size_t n = 44100;
  const Stereo in = mono(saw(n, 50.0));
  auto band = [&](float size, double lo, double hi) {
    TractFx u;
    u.prepare(static_cast<float>(kRate));
    const Stereo out = run(u, in, {{0, TractFx::Vowel, 0.0f}, {0, TractFx::Size, size}, {0, TractFx::Mix, 1.0f}});
    return band_power(out.l, n / 2, n, 50.0, lo, hi);
  };
  // /a/'s F2 is about 1090 Hz at NORMAL: a BIG tract takes it down (x0.85), a TINY one up (x1.22).
  const double big_ratio = band(0.0f, 800.0, 1000.0) / band(0.0f, 1250.0, 1450.0);
  const double tiny_ratio = band(4.0f, 800.0, 1000.0) / band(4.0f, 1250.0, 1450.0);
  CHECK(big_ratio > 1.5 * tiny_ratio);

  // The switch: SIZE from NORMAL to TINY and back at frames inside a block, over a bass line, never leaves finite and bounded.
  TractFx u;
  u.prepare(static_cast<float>(kRate));
  const Stereo out = run(u, in, {{0, TractFx::Mix, 1.0f}, {10003, TractFx::Size, 4.0f}, {20011, TractFx::Size, 0.0f}, {30017, TractFx::Size, 2.0f}});
  NEED(all_finite(out));
  for (std::size_t i = 0; i < n; ++i) {
    REQUIRE(std::fabs(out.l[i]) <= 8.0f);
  }
  // After the dip (5 ms) the output at the new size is not the old one: the switch took effect, no glide of formants in between is needed.
  TractFx fixed;
  fixed.prepare(static_cast<float>(kRate));
  const Stereo ref = run(fixed, in, {{0, TractFx::Mix, 1.0f}});
  double diff = 0.0;
  for (std::size_t i = 15000; i < 20000; ++i) {
    diff += std::fabs(static_cast<double>(out.l[i]) - static_cast<double>(ref.l[i]));
  }
  CHECK(diff > 1.0);
}

TEST_CASE("tract: the block size does not change a sample") {
  const std::size_t n = 16000;
  const Stereo in{saw(n, 50.0, 0.4), noise(n, 3, 0.3f)};
  const std::vector<Ev> events = {{0, TractFx::Mix, 0.8f},      {0, TractFx::Vowel, 0.2f},     {1000, TractFx::Tongue, 70.0f},
                                  {2001, TractFx::Lips, -90.0f}, {3333, TractFx::Vowel, 0.9f},  {4000, TractFx::Size, 4.0f},
                                  {5555, TractFx::Res, 0.95f},  {6000, TractFx::Glide, 400.0f}, {7001, TractFx::Vowel, 0.0f},
                                  {9000, TractFx::Mix, 0.0f},   {12345, TractFx::Mix, 1.0f},    {13000, TractFx::Size, 1.0f}};
  TractFx ref_unit;
  ref_unit.prepare(static_cast<float>(kRate));
  const Stereo ref = run(ref_unit, in, events, 64);
  NEED(all_finite(ref));
  for (const std::uint32_t block : {1u, 7u, 16u, 33u, 63u}) {
    TractFx u;
    u.prepare(static_cast<float>(kRate));
    const Stereo out = run(u, in, events, block);
    INFO("block " << block);
    CHECK(same_bits(out.l, ref.l));
    CHECK(same_bits(out.r, ref.r));
  }
}

TEST_CASE("tract: random parameter motion, out-of-range and NaN values included, never makes a NaN or a runaway") {
  const std::size_t n = 3 * 44100;
  const Stereo in{noise(n, 21, 1.0f), noise(n, 22, 1.0f)};
  std::vector<Ev> events;
  std::uint32_t s = 7;
  auto next = [&]() {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
  };
  for (std::uint32_t at = 0; at < n; at += 1 + next() % 61u) {
    const auto id = static_cast<std::uint16_t>(next() % TractFx::kParamCount);
    float v = static_cast<float>(next() % 20001u) / 10000.0f - 1.0f;   // -1..+1
    const o3sdk::core::ParamDesc& d = TractFx(). info().params[id];
    v = d.min + (v * 0.5f + 0.5f) * (d.max - d.min);
    if (next() % 41u == 0) {
      v = std::numeric_limits<float>::quiet_NaN();
    } else if (next() % 37u == 0) {
      v = v * 10.0f;   // out of range: clamped
    }
    events.push_back({at, id, v});
  }
  events.push_back({0, TractFx::Mix, 1.0f});
  std::stable_sort(events.begin(), events.end(), [](const Ev& a, const Ev& b) { return a.at < b.at; });
  TractFx u;
  u.prepare(static_cast<float>(kRate));
  const Stereo out = run(u, in, events);
  NEED(all_finite(out));
  float peak = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    peak = std::max({peak, std::fabs(out.l[i]), std::fabs(out.r[i])});
  }
  CHECK(peak < 20.0f);   // the dry noise is under 1 and the wet path is clamped to 8
}
