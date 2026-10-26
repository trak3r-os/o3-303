// SPDX-License-Identifier: GPL-3.0-or-later
// FREEZE (units/freeze): the unit driven directly, block by block, against an oracle written as arrays (no ring, no delay line): the
// loop's content and length at several tempos, entry, exit and the loop point without a click, Mix, the bypass being bit-exact, and the
// rules the header states (latched at entry, the block size not mattering). The engine end to end is freeze_engine_test.cpp.
#include <doctest/doctest.h>

#include "support/need.h"

#include <o3_303/units/freeze/freeze.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace {

namespace dsp = o3sdk::dsp;
using o3_303::Freeze;

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kRate = 44100;
constexpr double kTol = 3e-6;  // float arithmetic against the oracle's double

struct Stereo {
  std::vector<float> l;
  std::vector<float> r;
};

// A signal of three sines a channel (peak 0.82, the steepest step about 0.07), none commensurate with a loop length, so that a loop point
// is a step of up to 1.6 unless it is crossfaded.
Stereo test_signal(std::size_t n) {
  Stereo s;
  s.l.resize(n);
  s.r.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / kRate;
    s.l[i] = static_cast<float>(0.45 * std::sin(2.0 * kPi * 261.63 * t) + 0.25 * std::sin(2.0 * kPi * 777.0 * t + 0.3) +
                                0.12 * std::sin(2.0 * kPi * 1503.0 * t + 1.1));
    s.r[i] = static_cast<float>(0.45 * std::sin(2.0 * kPi * 329.63 * t + 0.7) + 0.25 * std::sin(2.0 * kPi * 913.0 * t) +
                                0.12 * std::sin(2.0 * kPi * 1709.0 * t + 2.0));
  }
  return s;
}

double max_step(const std::vector<float>& v, std::size_t from, std::size_t to) {
  double m = 0.0;
  for (std::size_t i = from + 1; i < to && i < v.size(); ++i) {
    m = std::max(m, std::fabs(static_cast<double>(v[i]) - static_cast<double>(v[i - 1])));
  }
  return m;
}
double max_step(const Stereo& s, std::size_t from, std::size_t to) { return std::max(max_step(s.l, from, to), max_step(s.r, from, to)); }

double max_abs(const Stereo& s) {
  double m = 0.0;
  for (std::size_t i = 0; i < s.l.size(); ++i) {
    m = std::max({m, std::fabs(static_cast<double>(s.l[i])), std::fabs(static_cast<double>(s.r[i]))});
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

double max_diff(const Stereo& a, const Stereo& b, std::size_t from, std::size_t to) {
  double m = 0.0;
  for (std::size_t i = from; i < to && i < a.l.size() && i < b.l.size(); ++i) {
    m = std::max({m, std::fabs(static_cast<double>(a.l[i]) - static_cast<double>(b.l[i])),
                  std::fabs(static_cast<double>(a.r[i]) - static_cast<double>(b.r[i]))});
  }
  return m;
}

bool bit_equal(const Stereo& a, const Stereo& b, std::size_t from, std::size_t to) {
  return a.l.size() >= to && b.l.size() >= to &&
         std::memcmp(a.l.data() + from, b.l.data() + from, (to - from) * sizeof(float)) == 0 &&
         std::memcmp(a.r.data() + from, b.r.data() + from, (to - from) * sizeof(float)) == 0;
}

// The signal delayed by `lag` frames dropped from its front: s[i] of the result is in[i + lag].
Stereo ahead(const Stereo& in, std::size_t lag) {
  return {std::vector<float>(in.l.begin() + static_cast<std::ptrdiff_t>(lag), in.l.end()),
          std::vector<float>(in.r.begin() + static_cast<std::ptrdiff_t>(lag), in.r.end())};
}

struct Ev {
  std::uint32_t at;
  std::uint16_t id;
  float value;
};
struct TempoAt {
  std::uint32_t at;  // a block start
  std::uint32_t mbpm;
};

// The unit with a ring of garbage that prepare() must clear, driven block by block with events at their absolute frames.
class Rig {
 public:
  explicit Rig(std::uint32_t capacity = 65536) : ring_l_(capacity, 7.0f), ring_r_(capacity, -7.0f), fz(ring_l_.data(), ring_r_.data(), capacity) {
    fz.prepare(static_cast<float>(kRate));
  }

  Stereo run(const Stereo& in, const std::vector<Ev>& events, std::uint32_t mbpm = 120'000, std::uint32_t block = 64, bool in_place = true,
             const std::vector<TempoAt>& tempos = {}) {
    const std::size_t n = in.l.size();
    bake_log.clear();
    Stereo work = in;
    Stereo dst;
    if (!in_place) {
      dst.l.assign(n, std::numeric_limits<float>::quiet_NaN());  // every frame must be written
      dst.r.assign(n, std::numeric_limits<float>::quiet_NaN());
    }
    std::vector<dsp::Event> evs;
    dsp::Transport tr;
    tr.tempo_mbpm = mbpm;
    for (std::size_t pos = 0; pos < n; pos += block) {
      const auto frames = static_cast<std::uint32_t>(std::min<std::size_t>(block, n - pos));
      for (const TempoAt& t : tempos) {
        if (t.at <= pos) {
          tr.tempo_mbpm = t.mbpm;
        }
      }
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
      float* const or_ = in_place ? ir : dst.r.data() + pos;
      const dsp::ProcessArgs a{evs.data(), static_cast<std::uint32_t>(evs.size()), il, ir, ol, or_, frames, &tr};
      fz.process(a);
      bake_log.push_back({frames, fz.baked_last()});
    }
    return in_place ? work : dst;
  }

  // The frames of each process() call of the last run() and the tail frames the unit baked in it (its cost in that block).
  std::vector<std::pair<std::uint32_t, std::uint32_t>> bake_log;
  std::vector<float> ring_l_;
  std::vector<float> ring_r_;
  Freeze fz;
};

Ev on(std::uint32_t at) { return {at, Freeze::Active, 1.0f}; }
Ev off(std::uint32_t at) { return {at, Freeze::Active, 0.0f}; }

// The oracle: what a freeze at frame n0 (and out at n1, or never when n1 < 0) of loop `loop` and crossfade `fade` must output, written
// from the header's description with arrays and doubles. c is the loop: the `loop` frames before n0, the last `fade` of them replaced by
// a fade to the `fade` frames that precede the loop (the join of the loop point); before the signal's start there is silence.
struct Plan {
  std::uint32_t n0 = 0;
  std::int64_t n1 = -1;
  std::uint32_t loop = 0;
  std::uint32_t fade = 0;
  double mix = 1.0;
};

Stereo oracle(const Stereo& x, const Plan& p) {
  Stereo y = x;
  for (int ch = 0; ch < 2; ++ch) {
    const std::vector<float>& in = ch == 0 ? x.l : x.r;
    std::vector<float>& out = ch == 0 ? y.l : y.r;
    const auto at = [&](std::int64_t i) { return i < 0 ? 0.0 : static_cast<double>(in[static_cast<std::size_t>(i)]); };
    const std::int64_t L = p.loop;
    const std::int64_t X = p.fade;
    const std::int64_t n0 = p.n0;
    std::vector<double> c(p.loop);
    for (std::int64_t j = 0; j < L; ++j) {
      c[static_cast<std::size_t>(j)] = at(n0 - L + j);
    }
    for (std::int64_t i = 0; i < X; ++i) {
      const double a = static_cast<double>(i + 1) / static_cast<double>(X);
      c[static_cast<std::size_t>(L - X + i)] = at(n0 - X + i) * (1.0 - a) + at(n0 - L - X + i) * a;
    }
    // An exit asked for before the entry crossfade is complete waits for it.
    const std::int64_t k1 = p.n1 >= 0 ? std::max(p.n1 - n0, X) : std::numeric_limits<std::int64_t>::max();
    const std::int64_t fp0 = std::min(k1, X);
    for (std::int64_t n = n0; n < static_cast<std::int64_t>(in.size()); ++n) {
      const std::int64_t k = n - n0;
      double g = 0.0;
      if (k < k1) {
        g = static_cast<double>(std::min(k, X)) / static_cast<double>(X);
      } else if (k - k1 < fp0) {
        g = static_cast<double>(fp0 - (k - k1)) / static_cast<double>(X);
      } else {
        out[static_cast<std::size_t>(n)] = in[static_cast<std::size_t>(n)];
        continue;
      }
      const double lp = c[static_cast<std::size_t>(k % L)];
      const double s = at(n);
      out[static_cast<std::size_t>(n)] = static_cast<float>(s * (1.0 - p.mix * g) + lp * p.mix * g);
    }
  }
  return y;
}

constexpr double kSixteenths[3] = {4.0, 2.0, 1.0};

// The loop length from the tempo, written independently of the unit (rounded, in the engine's 44.1 kHz).
std::uint32_t expected_loop(unsigned length, std::uint32_t mbpm) {
  const double sixteenth = 15.0 * kRate * 1000.0 / static_cast<double>(mbpm);
  return static_cast<std::uint32_t>(std::floor(kSixteenths[length] * sixteenth + 0.5));
}

struct Case {
  std::uint32_t bpm;
  unsigned length;
};

// Every process() call baked at most half a frame a frame of its block (rounded up), the share the unit paces the baking by.
bool paced(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& log) {
  return std::all_of(log.begin(), log.end(), [](const auto& call) { return call.second <= (call.first + 1u) / 2u; });
}
std::uint32_t baked_total(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& log) {
  std::uint32_t total = 0;
  for (const auto& call : log) {
    total += call.second;
  }
  return total;
}

}  // namespace

TEST_CASE("freeze: the table is valid, in the contract's order, and the text is what the screen shows") {
  std::vector<float> a(8192);
  std::vector<float> b(8192);
  Freeze fz(a.data(), b.data(), 8192);
  const auto& info = fz.info();
  CHECK(info.kind == dsp::UnitKind::Effect);
  CHECK(std::strcmp(info.name, "FREEZE") == 0);
  NEED(info.param_count == Freeze::kParamCount);
  CHECK(Freeze::kParamCount == 7);  // the three of M3b, then the four of M3c (Mode ... Stop Time)
  CHECK(Freeze::Active == 0);
  CHECK(Freeze::Length == 1);
  CHECK(Freeze::Mix == 2);
  for (unsigned p = 0; p < info.param_count; ++p) {
    INFO(info.params[p].name);
    CHECK(o3sdk::core::param_desc_valid(info.params[p]));
  }
  CHECK(info.params[Freeze::Active].def == 0.0f);  // off
  CHECK(info.params[Freeze::Length].def == 1.0f);  // 1/8 of a bar
  CHECK(info.params[Freeze::Mix].def == 1.0f);
  char text[16];
  (void)o3sdk::core::param_format(info.params[Freeze::Active], 0.0f, text, sizeof text);
  CHECK(std::strcmp(text, "OFF") == 0);
  (void)o3sdk::core::param_format(info.params[Freeze::Active], 1.0f, text, sizeof text);
  CHECK(std::strcmp(text, "ON") == 0);
  const char* want[] = {"1/4", "1/8", "1/16"};
  for (int i = 0; i < 3; ++i) {
    (void)o3sdk::core::param_format(info.params[Freeze::Length], static_cast<float>(i), text, sizeof text);
    CHECK(std::strcmp(text, want[i]) == 0);
  }
  (void)o3sdk::core::param_format(info.params[Freeze::Mix], 0.5f, text, sizeof text);
  CHECK(std::strcmp(text, "50") == 0);
}

TEST_CASE("freeze: the loop is 1/4, 1/8 or 1/16 of a bar of the transport's tempo, kept inside the ring") {
  for (std::uint32_t bpm : {20u, 60u, 90u, 120u, 125u, 133u, 174u, 240u, 300u}) {
    for (unsigned length = 0; length < 3; ++length) {
      INFO(bpm << " BPM, length " << length);
      const std::uint32_t got = Freeze::loop_frames(length, bpm * 1000u, 131072);
      const std::uint32_t want = std::min(expected_loop(length, bpm * 1000u), 131072u - Freeze::kHeadroom);
      CHECK(got + 1u >= want);
      CHECK(got <= want + 1u);
    }
    // A bar is four beats: 1/4 of it is a beat; 1/4 is twice 1/8 and four times 1/16 (to a frame of rounding).
    const std::uint32_t q = Freeze::loop_frames(Freeze::Quarter, bpm * 1000u, 1u << 20);
    const std::uint32_t e = Freeze::loop_frames(Freeze::Eighth, bpm * 1000u, 1u << 20);
    const std::uint32_t s = Freeze::loop_frames(Freeze::Sixteenth, bpm * 1000u, 1u << 20);
    CHECK(q <= 2u * e + 1u);
    CHECK(2u * e <= q + 1u);
    CHECK(e <= 2u * s + 1u);
    CHECK(2u * s <= e + 1u);
  }
  // One beat at 125 BPM is 0.48 s, exactly 21,168 frames.
  CHECK(Freeze::loop_frames(Freeze::Quarter, 125'000, 131072) == 21168u);
  CHECK(Freeze::loop_frames(Freeze::Eighth, 125'000, 131072) == 10584u);
  CHECK(Freeze::loop_frames(Freeze::Sixteenth, 125'000, 131072) == 5292u);
  // The ring bounds it: a beat at 20 BPM would be 132,300 frames.
  CHECK(Freeze::loop_frames(Freeze::Quarter, 20'000, 131072) == 131072u - Freeze::kHeadroom);
  CHECK(Freeze::loop_frames(Freeze::Quarter, 20'000, 4096) == 4096u - Freeze::kHeadroom);
  // An index out of the table is the shortest; a tempo out of range is clamped by the transport's own maths.
  CHECK(Freeze::loop_frames(7, 125'000, 131072) == Freeze::loop_frames(Freeze::Sixteenth, 125'000, 131072));
  CHECK(Freeze::loop_frames(Freeze::Quarter, 0, 131072) <= 131072u - Freeze::kHeadroom);
  // The crossfade: 5 ms (221 frames at 44.1 kHz), at most kMaxFade, at most a quarter of a short loop, never 0.
  CHECK(Freeze::fade_frames(44100.0f, 20000) == 221u);
  CHECK(Freeze::fade_frames(96000.0f, 20000) == Freeze::kMaxFade);
  CHECK(Freeze::fade_frames(44100.0f, 400) == 100u);
  CHECK(Freeze::fade_frames(44100.0f, 2) == 1u);
}

TEST_CASE("freeze: off, the output is the input bit for bit, in place or not, whatever Length and Mix are") {
  const Stereo x = test_signal(30000);
  for (bool in_place : {true, false}) {
    for (std::uint32_t block : {64u, 7u, 1u}) {
      INFO("in_place " << in_place << ", block " << block);
      Rig r;
      const Stereo y = r.run(x, {{500, Freeze::Length, 2.0f}, {900, Freeze::Mix, 0.3f}, {9000, Freeze::Length, 0.0f}, {9001, Freeze::Active, 0.0f}},
                             125'000, block, in_place);
      CHECK(bit_equal(x, y, 0, x.l.size()));
      CHECK_FALSE(r.fz.frozen());
    }
  }
}

TEST_CASE("freeze: a ring that is too small, or missing, only passes the signal through, even when asked to freeze") {
  const Stereo x = test_signal(20000);
  std::vector<float> a(1000);
  std::vector<float> b(1000);
  Freeze small(a.data(), b.data(), 1000);
  small.prepare(44100.0f);
  Freeze none(nullptr, nullptr, 65536);
  none.prepare(44100.0f);
  for (Freeze* fz : {&small, &none}) {
    Stereo work = x;
    const dsp::Event ev = dsp::param_change(Freeze::Active, 1.0f, 100);
    dsp::Transport tr;
    tr.tempo_mbpm = 125'000;
    for (std::size_t pos = 0; pos < x.l.size(); pos += 64) {
      const auto frames = static_cast<std::uint32_t>(std::min<std::size_t>(64, x.l.size() - pos));
      tr.frames = frames;
      const dsp::Event* evs = pos == 0 ? &ev : nullptr;
      fz->process({evs, pos == 0 ? 1u : 0u, work.l.data() + pos, work.r.data() + pos, work.l.data() + pos, work.r.data() + pos, frames, &tr});
    }
    CHECK(bit_equal(x, work, 0, x.l.size()));
    CHECK_FALSE(fz->frozen());
  }
}

TEST_CASE("freeze: the loop is the last Length of the input, crossfaded at entry, at the loop point and at exit (oracle, tempos and lengths)") {
  const Stereo x = test_signal(330000);
  const Case cases[] = {{60, 0}, {60, 2}, {90, 1}, {125, 0}, {125, 1}, {125, 2}, {174, 0}, {174, 2}, {240, 1}, {300, 0}, {300, 1}, {300, 2}};
  for (const Case& c : cases) {
    for (std::uint32_t n0 : {30000u + 17u, 65536u + 23u, 65536u * 2u - 40u}) {
      for (std::uint32_t block : {64u, 29u}) {
        const std::uint32_t mbpm = c.bpm * 1000u;
        const std::uint32_t L = expected_loop(c.length, mbpm);
        INFO(c.bpm << " BPM, length " << c.length << ", entry " << n0 << ", block " << block);
        Rig r;
        const std::uint32_t exit_at = n0 + 3u * L + L / 3u + 5u;
        const std::vector<Ev> evs = {{0, Freeze::Length, static_cast<float>(c.length)}, on(n0), off(exit_at)};
        const Stereo y = r.run(x, evs, mbpm, block);
        const std::uint32_t got = r.fz.loop();
        CHECK(got + 1u >= L);
        CHECK(got <= L + 1u);
        Plan p;
        p.n0 = n0;
        p.n1 = exit_at;
        p.loop = got;
        p.fade = Freeze::fade_frames(44100.0f, got);
        const Stereo want = oracle(x, p);
        CHECK(max_diff(y, want, 0, x.l.size()) < kTol);
        // Before the entry and from the end of the exit crossfade, the input itself.
        CHECK(bit_equal(x, y, 0, n0));
        CHECK(bit_equal(x, y, exit_at + p.fade + 1u, x.l.size()));
        CHECK_FALSE(r.fz.frozen());
      }
    }
  }
}

TEST_CASE("freeze: the frozen output repeats at exactly the loop length and not at its neighbours (length at several tempos)") {
  const Stereo x = test_signal(200000);
  for (std::uint32_t bpm : {70u, 110u, 125u, 150u, 210u, 300u}) {
    for (unsigned length = 0; length < 3; ++length) {
      const std::uint32_t mbpm = bpm * 1000u;
      const std::uint32_t L = expected_loop(length, mbpm);
      if (L + Freeze::kHeadroom > 65536u) {
        continue;
      }
      INFO(bpm << " BPM, length " << length << ", loop " << L);
      Rig r;
      const std::uint32_t n0 = 40000;
      const Stereo y = r.run(x, {{0, Freeze::Length, static_cast<float>(length)}, on(n0)}, mbpm);
      const std::uint32_t got = r.fz.loop();
      NEED(got + 1u >= L);
      CHECK(got <= L + 1u);
      const std::size_t from = n0 + 400;  // after the entry crossfade
      const std::size_t to = n0 + 2u * got - 400;
      NEED(to + got + 2u < y.l.size());
      CHECK(max_diff(y, ahead(y, got), from, to) < 1e-6);
      // One frame either side it does not repeat (a loop of the right pitch but the wrong length would pass the check above).
      for (std::size_t lag : {static_cast<std::size_t>(got) - 1u, static_cast<std::size_t>(got) + 1u}) {
        CHECK(max_diff(y, ahead(y, lag), from, to) > 1e-3);
      }
    }
  }
}

TEST_CASE("freeze: entry, the loop point and exit do not click, and the output never leaves the input's range") {
  const Stereo x = test_signal(240000);
  const double s_in = max_step(x, 0, x.l.size());
  const double peak_in = max_abs(x);
  double worst_naive = 0.0;
  for (const Case& c : {Case{60, 0}, Case{90, 1}, Case{125, 0}, Case{125, 1}, Case{125, 2}, Case{174, 1}, Case{300, 2}}) {
    const std::uint32_t L0 = expected_loop(c.length, c.bpm * 1000u);
    // Entry after a whole loop and its fade have been recorded (the start of the signal is a step of its own in a shorter history), at
    // frames that fall in different places of a 64-frame block.
    for (std::uint32_t n0 : {L0 + 701u, L0 + 749u, L0 + 750u, L0 + 4033u}) {
      for (double mix : {1.0, 0.5}) {
        const std::uint32_t mbpm = c.bpm * 1000u;
        const std::uint32_t L = L0;
        INFO(c.bpm << " BPM, length " << c.length << ", entry " << n0 << ", mix " << mix);
        Rig r;
        const std::uint32_t exit_at = n0 + 3u * L + 77u;
        const Stereo y = r.run(x, {{0, Freeze::Length, static_cast<float>(c.length)}, {0, Freeze::Mix, static_cast<float>(mix)}, on(n0), off(exit_at)}, mbpm);
        const std::uint32_t fade = Freeze::fade_frames(44100.0f, r.fz.loop());
        // The signal's own steps, plus the slope of a crossfade between two values at most 2 * peak apart.
        const double bound = s_in + 2.0 * peak_in / static_cast<double>(fade) + 1e-4;
        CHECK(max_step(y, 0, y.l.size()) <= bound);
        CHECK(max_abs(y) <= peak_in + 1e-5);
        CHECK(all_finite(y));
        // The same loop without the crossfades would jump at the loop point by this much.
        for (int ch = 0; ch < 2; ++ch) {
          const std::vector<float>& v = ch == 0 ? x.l : x.r;
          worst_naive = std::max(worst_naive, std::fabs(static_cast<double>(v[n0 - 1]) - static_cast<double>(v[n0 - r.fz.loop()])));
        }
      }
    }
  }
  // The test has teeth: the bare loop point of this signal is a step of several times the crossfaded bound.
  CHECK(worst_naive > 5.0 * (s_in + 2.0 * peak_in / 221.0));
}

TEST_CASE("freeze: Mix is the loop against the input while frozen") {
  const Stereo x = test_signal(120000);
  const std::uint32_t mbpm = 125'000;
  const std::uint32_t n0 = 30000;
  // Mix 0: the input, bit for bit, though the unit freezes.
  {
    Rig r;
    const Stereo y = r.run(x, {{0, Freeze::Mix, 0.0f}, on(n0)}, mbpm);
    CHECK(r.fz.frozen());
    CHECK(bit_equal(x, y, 0, x.l.size()));
  }
  // Mix 0.25, 0.5, 1: the oracle with that share.
  for (double mix : {0.25, 0.5, 1.0}) {
    INFO("mix " << mix);
    Rig r;
    const Stereo y = r.run(x, {{0, Freeze::Mix, static_cast<float>(mix)}, on(n0)}, mbpm);
    Plan p;
    p.n0 = n0;
    p.loop = r.fz.loop();
    p.fade = Freeze::fade_frames(44100.0f, p.loop);
    p.mix = mix;
    CHECK(max_diff(y, oracle(x, p), 0, x.l.size()) < kTol);
  }
  // Mix moved while frozen is smoothed (5 ms): no step beyond the signal's, plus the slope of the gain, and it arrives.
  {
    Rig r;
    const Stereo y = r.run(x, {on(n0), {n0 + 20000, Freeze::Mix, 0.0f}, {n0 + 30000, Freeze::Mix, 1.0f}}, mbpm);
    const double bound = max_step(x, 0, x.l.size()) + 2.0 * max_abs(x) * 0.0046 + 1e-4;  // 1 - exp(-1 / (0.005 * 44100)) per frame
    CHECK(max_step(y, 0, y.l.size()) <= bound);
    CHECK(max_diff(y, x, n0 + 20000 + 4000, n0 + 30000) < 1e-4);  // Mix 0: the input
    Plan p;
    p.n0 = n0;
    p.loop = r.fz.loop();
    p.fade = Freeze::fade_frames(44100.0f, p.loop);
    const Stereo want = oracle(x, p);
    CHECK(max_diff(y, want, n0 + 30000 + 4000, x.l.size()) < 1e-4);  // Mix 1 again: the loop
  }
}

TEST_CASE("freeze: the loop is latched at entry: a new tempo or Length while frozen leaves it, the next entry uses them") {
  const Stereo x = test_signal(260000);
  Rig r;
  const std::uint32_t n0 = 30000;
  const std::uint32_t L1 = expected_loop(1, 125'000);  // 10,584
  const std::uint32_t second = n0 + 5u * L1;
  const Stereo y = r.run(x,
                         {{0, Freeze::Length, 1.0f}, on(n0), {n0 + 2u * L1 + 100u, Freeze::Length, 2.0f}, off(second - 3000u), on(second + 9000u)},
                         125'000, 64, true, {{(n0 + L1) / 64u * 64u, 200'000}});
  // The tempo went to 200 BPM and Length to 1/16 mid-hold: the first loop is the 125 BPM one throughout.
  Plan p;
  p.n0 = n0;
  p.n1 = second - 3000;
  p.loop = L1;
  p.fade = Freeze::fade_frames(44100.0f, L1);
  const Stereo want = oracle(x, p);
  CHECK(max_diff(y, want, 0, second - 3000u + p.fade + 1u) < kTol);
  // The second entry takes the tempo and the Length of that moment: 200 BPM, 1/16.
  const std::uint32_t L2 = expected_loop(2, 200'000);  // 3,308
  CHECK(r.fz.loop() + 1u >= L2);
  CHECK(r.fz.loop() <= L2 + 1u);
  CHECK(r.fz.frozen());
  const std::size_t from = second + 9000u + 400u;
  const std::size_t to = x.l.size() - r.fz.loop();
  CHECK(max_diff(y, ahead(y, r.fz.loop()), from, to) < 1e-6);
}

TEST_CASE("freeze: the first freeze after the start has silence for the history it does not have") {
  const Stereo x = test_signal(60000);
  Rig r;
  const std::uint32_t n0 = 300;
  const Stereo y = r.run(x, {{0, Freeze::Length, 0.0f}, on(n0)}, 125'000);  // 1/4: 21,168 frames, far more than was recorded
  Plan p;
  p.n0 = n0;
  p.loop = r.fz.loop();
  p.fade = Freeze::fade_frames(44100.0f, p.loop);
  CHECK(max_diff(y, oracle(x, p), 0, x.l.size()) < kTol);
  CHECK(all_finite(y));
}

TEST_CASE("freeze: the result does not depend on the block size (the baking of the loop's tail is paced by it)") {
  const Stereo x = test_signal(150000);
  const std::vector<Ev> evs = {{0, Freeze::Length, 2.0f}, on(40000 + 13), off(40000 + 13 + 4 * 5292 + 11), on(40000 + 13 + 4 * 5292 + 900)};
  Rig ref;
  const Stereo want = ref.run(x, evs, 125'000, 64);
  for (std::uint32_t block : {1u, 2u, 3u, 5u, 17u, 31u, 32u, 63u}) {
    INFO("block " << block);
    Rig r;
    const Stereo y = r.run(x, evs, 125'000, block);
    CHECK(bit_equal(want, y, 0, x.l.size()));
  }
  // Out of place as well.
  Rig r;
  CHECK(bit_equal(want, r.run(x, evs, 125'000, 64, false), 0, x.l.size()));
}

TEST_CASE("freeze: the ring is cleared by prepare and a second prepare starts over") {
  const Stereo x = test_signal(40000);
  Rig r;
  (void)r.run(x, {on(10000)}, 125'000);
  CHECK(r.fz.frozen());
  r.fz.prepare(44100.0f);
  CHECK_FALSE(r.fz.frozen());
  const auto zero = [](float v) { return v == 0.0f; };
  CHECK(std::all_of(r.ring_l_.begin(), r.ring_l_.end(), zero));
  CHECK(std::all_of(r.ring_r_.begin(), r.ring_r_.end(), zero));
  const Stereo y = r.run(x, {}, 125'000);
  CHECK(bit_equal(x, y, 0, x.l.size()));
}

TEST_CASE("freeze: a freeze that is released and pressed again at once stays continuous, and a frozen ring never leaks into a dry run") {
  const Stereo x = test_signal(200000);
  const double bound = max_step(x, 0, x.l.size()) + 2.0 * max_abs(x) / 221.0 + 1e-4;
  for (std::uint32_t gap : {0u, 1u, 50u, 200u, 222u, 500u, 5000u, 11000u}) {
    INFO("gap " << gap);
    Rig r;
    const std::uint32_t n0 = 50000;
    const std::uint32_t n1 = n0 + 9000;
    const Stereo y = r.run(x, {{0, Freeze::Length, 1.0f}, on(n0), off(n1), on(n1 + gap), off(n1 + gap + 14000)}, 125'000);
    CHECK(max_step(y, 0, y.l.size()) <= bound);
    CHECK(all_finite(y));
    CHECK(max_abs(y) <= max_abs(x) + 1e-5);
    CHECK(bit_equal(x, y, n1 + gap + 14000 + 300, x.l.size()));
  }
}

TEST_CASE("freeze: a tap shorter than the entry crossfade completes it (the exit waits), and a freeze soon after loops a ring without a step") {
  const Stereo x = test_signal(160000);
  const double bound = max_step(x, 0, x.l.size()) + 2.0 * max_abs(x) / 221.0 + 1e-4;
  const std::uint32_t n0 = 60000;
  for (std::uint32_t hold : {1u, 10u, 100u, 221u, 222u, 400u}) {
    INFO("hold " << hold);
    Rig r;
    const std::uint32_t second = n0 + hold + 700u;
    const Stereo y = r.run(x, {{0, Freeze::Length, 0.0f}, on(n0), off(n0 + hold), on(second), off(second + 5000u)}, 125'000);
    // The first tap: the oracle, whose exit is no sooner than the end of the entry crossfade.
    const std::uint32_t L = expected_loop(0, 125'000);
    Plan p;
    p.n0 = n0;
    p.n1 = n0 + hold;
    p.loop = L;
    p.fade = Freeze::fade_frames(44100.0f, L);
    CHECK(max_diff(y, oracle(x, p), 0, second) < kTol);
    // The second freeze, which takes the ring as the first one and its exit left it, does not click either.
    CHECK(max_step(y, 0, y.l.size()) <= bound);
    CHECK(all_finite(y));
    CHECK(bit_equal(x, y, second + 5000u + 300u, x.l.size()));
  }
}

TEST_CASE("freeze: the baking of the loop's tail is paced, a block's share at most, also through an exit; a quick re-entry waits for it") {
  const Stereo x = test_signal(120000);
  const double bound = max_step(x, 0, x.l.size()) + 2.0 * max_abs(x) / 221.0 + 1e-4;
  const std::uint32_t n0 = 64u * 700u;  // a block start at 64 frames
  const std::uint32_t fade = Freeze::fade_frames(44100.0f, Freeze::loop_frames(Freeze::Eighth, 125'000, 65536));
  NEED(fade == 221u);
  for (std::uint32_t block : {64u, 33u, 17u, 1u}) {
    INFO("block " << block);
    // The shortest tap: the exit comes as soon as the entry crossfade is over, with most of the baking still to do.
    {
      Rig r;
      const Stereo y = r.run(x, {on(n0), off(n0 + fade)}, 125'000, block);
      CHECK(paced(r.bake_log));  // no block carries more than half a frame a frame, the exit's included
      CHECK(baked_total(r.bake_log) == fade);  // the whole tail, once
      CHECK_FALSE(r.fz.baking());
      CHECK_FALSE(r.fz.frozen());
      Plan p;
      p.n0 = n0;
      p.n1 = n0 + fade;
      p.loop = r.fz.loop();
      p.fade = fade;
      CHECK(max_diff(y, oracle(x, p), 0, x.l.size()) < kTol);
    }
    // The same tap, and another freeze asked for the moment the exit ends: it takes the ring only when the tail is baked.
    {
      Rig r;
      const std::uint32_t again = n0 + 2u * fade;
      const Stereo y = r.run(x, {on(n0), off(n0 + fade), on(again), off(again + 4000u)}, 125'000, block);
      CHECK(paced(r.bake_log));
      CHECK(baked_total(r.bake_log) == 2u * fade);  // both tails, each once
      CHECK_FALSE(r.fz.baking());
      CHECK(max_step(y, 0, y.l.size()) <= bound);
      CHECK(all_finite(y));
      CHECK(bit_equal(x, y, again + 4000u + 300u, x.l.size()));
      if (block == 64u) {
        // The tail has 192 of its 221 frames baked when the exit ends (frame 442): the entry waits for the call at 448, which finishes it.
        CHECK(bit_equal(x, y, again, again + 7u));
        CHECK(max_diff(y, x, again + 7u, again + 8u) > 1e-5);
      }
    }
  }
}

TEST_CASE("freeze: gesture mashing (random on, off, Length, Mix, tempo and block size) stays finite, bounded and continuous") {
  const Stereo x = test_signal(400000);
  const double bound = max_step(x, 0, x.l.size()) + 2.0 * max_abs(x) / 221.0 + 1e-4;
  std::uint32_t seed = 12345u;
  const auto rnd = [&seed](std::uint32_t n) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % n;
  };
  for (int round = 0; round < 6; ++round) {
    Rig r;
    std::vector<Ev> evs;
    std::uint32_t at = 70000;  // a whole loop (at most 29,400 frames here) and more has been recorded
    bool active = false;
    while (at < x.l.size() - 20000u) {
      evs.push_back(active ? off(at) : on(at));
      active = !active;
      if (rnd(3) == 0) {
        evs.push_back({at + rnd(40), Freeze::Length, static_cast<float>(rnd(3))});
      }
      if (rnd(3) == 0) {
        evs.push_back({at + rnd(40), Freeze::Mix, static_cast<float>(rnd(101)) / 100.0f});
      }
      at += 1u + rnd(active ? 6000u : 900u);
    }
    evs.push_back(off(static_cast<std::uint32_t>(x.l.size()) - 15000u));
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.at < b.at; });
    const std::uint32_t block = 1u + rnd(64);
    INFO("round " << round << ", block " << block);
    const Stereo y = r.run(x, evs, 90'000u + 20'000u * static_cast<std::uint32_t>(round), block);
    CHECK(all_finite(y));
    CHECK(max_abs(y) <= max_abs(x) + 1e-5);
    CHECK(max_step(y, 0, y.l.size()) <= bound);
    CHECK(bit_equal(x, y, x.l.size() - 14000u, x.l.size()));  // released for good: the input again
  }
}

TEST_CASE("freeze: the slowest tempo's beat is clamped to the ring, and the loop is still the last of the input, in the real size") {
  constexpr std::uint32_t kCap = 131072;  // the app's ring: 1 MiB for both sides
  const Stereo x = test_signal(kCap * 2u + 40000u);
  Rig r(kCap);
  const std::uint32_t n0 = kCap + 5000u;
  const Stereo y = r.run(x, {{0, Freeze::Length, 0.0f}, on(n0)}, 20'000);
  CHECK(r.fz.loop() == kCap - Freeze::kHeadroom);
  Plan p;
  p.n0 = n0;
  p.loop = r.fz.loop();
  p.fade = Freeze::fade_frames(44100.0f, p.loop);
  CHECK(max_diff(y, oracle(x, p), 0, x.l.size()) < kTol);
}
