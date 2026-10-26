// SPDX-License-Identifier: GPL-3.0-or-later
// FREEZE's modes of M3c (units/freeze, docs/M3C-PLAN.md R): ROLL (the loop at the Roll Rate, the change of rate at a loop boundary, Accelerate),
// STOP (the tape stop and its spin-up) and REVERSE, driven directly against oracles written as arrays. FREEZE itself (the default mode) is
// freeze_test.cpp and freeze_engine_test.cpp, unchanged; here Mode FREEZE is checked to be the same thing as no Mode at all.
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
#include <vector>

namespace {

namespace dsp = o3sdk::dsp;
using o3_303::Freeze;

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kRate = 44100;
constexpr double kTol = 3e-6;

struct Stereo {
  std::vector<float> l;
  std::vector<float> r;
};

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

Stereo pure_sine(std::size_t n, double hz, double amp) {
  Stereo s;
  s.l.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    s.l[i] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
  }
  s.r = s.l;
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

bool bit_equal(const Stereo& a, const Stereo& b, std::size_t from, std::size_t to) {
  return a.l.size() >= to && b.l.size() >= to && std::memcmp(a.l.data() + from, b.l.data() + from, (to - from) * sizeof(float)) == 0 &&
         std::memcmp(a.r.data() + from, b.r.data() + from, (to - from) * sizeof(float)) == 0;
}

double max_diff(const Stereo& a, const Stereo& b, std::size_t from, std::size_t to) {
  double m = 0.0;
  for (std::size_t i = from; i < to; ++i) {
    m = std::max({m, std::fabs(static_cast<double>(a.l[i]) - static_cast<double>(b.l[i])), std::fabs(static_cast<double>(a.r[i]) - static_cast<double>(b.r[i]))});
  }
  return m;
}

struct Ev {
  std::uint32_t at;
  std::uint16_t id;
  float value;
};

Ev on(std::uint32_t at) { return {at, Freeze::Active, 1.0f}; }
Ev off(std::uint32_t at) { return {at, Freeze::Active, 0.0f}; }
Ev mode(std::uint32_t at, Freeze::ModeValue m) { return {at, Freeze::Mode, static_cast<float>(m)}; }
Ev rate(std::uint32_t at, unsigned r) { return {at, Freeze::RollRate, static_cast<float>(r)}; }

// The unit with a ring of garbage that prepare() must clear. `probe` is called after each process() call with the frame count so far.
class Rig {
 public:
  explicit Rig(std::uint32_t capacity = 65536) : ring_l_(capacity, 7.0f), ring_r_(capacity, -7.0f), fz(ring_l_.data(), ring_r_.data(), capacity) {
    fz.prepare(static_cast<float>(kRate));
  }

  template <typename Probe>
  Stereo run(const Stereo& in, const std::vector<Ev>& events, std::uint32_t mbpm, std::uint32_t block, Probe&& probe) {
    const std::size_t n = in.l.size();
    Stereo work = in;
    std::vector<dsp::Event> evs;
    dsp::Transport tr;
    tr.tempo_mbpm = mbpm;
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
      const dsp::ProcessArgs a{evs.data(), static_cast<std::uint32_t>(evs.size()), il, ir, il, ir, frames, &tr};
      fz.process(a);
      probe(pos + frames);
    }
    return work;
  }
  Stereo run(const Stereo& in, const std::vector<Ev>& events, std::uint32_t mbpm = 120'000, std::uint32_t block = 64) {
    return run(in, events, mbpm, block, [](std::size_t) {});
  }

  std::vector<float> ring_l_;
  std::vector<float> ring_r_;
  Freeze fz;
};

// FREEZE's oracle (freeze_test.cpp), for the entry of a ROLL: the loop is the `loop` frames before n0 with the last `fade` replaced by a
// fade to the frames that precede the loop; it plays crossfaded in over `fade` frames (and out from n1, when n1 >= 0).
struct Plan {
  std::uint32_t n0 = 0;
  std::int64_t n1 = -1;
  std::uint32_t loop = 0;
  std::uint32_t fade = 0;
};

Stereo freeze_oracle(const Stereo& x, const Plan& p) {
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
      out[static_cast<std::size_t>(n)] = static_cast<float>(at(n) * (1.0 - g) + c[static_cast<std::size_t>(k % L)] * g);
    }
  }
  return y;
}

// The loop of a rate in frames, written independently of the unit: 1/8 note = 2 sixteenths, halved at each step; rounded; never below kMinRoll.
std::uint32_t expected_roll(unsigned r, std::uint32_t mbpm) {
  const double sixteenth = 15.0 * kRate * 1000.0 / static_cast<double>(mbpm);
  const double n = std::floor(2.0 / std::pow(2.0, r) * sixteenth + 0.5);
  return std::max(static_cast<std::uint32_t>(n), Freeze::kMinRoll);
}

}  // namespace

TEST_CASE("freeze modes: the table, the contract's order, the defaults and the helpers") {
  std::vector<float> a(8192);
  std::vector<float> b(8192);
  Freeze fz(a.data(), b.data(), 8192);
  const auto& info = fz.info();
  NEED(info.param_count == Freeze::kParamCount);
  CHECK(Freeze::kParamCount == 7);
  CHECK(Freeze::Mode == 3);
  CHECK(Freeze::RollRate == 4);
  CHECK(Freeze::Accelerate == 5);
  CHECK(Freeze::StopTime == 6);
  for (unsigned p = 0; p < info.param_count; ++p) {
    INFO(info.params[p].name);
    CHECK(o3sdk::core::param_desc_valid(info.params[p]));
  }
  CHECK(info.params[Freeze::Mode].def == 0.0f);        // FREEZE
  CHECK(info.params[Freeze::RollRate].def == 1.0f);    // 1/16
  CHECK(info.params[Freeze::Accelerate].def == 0.0f);  // OFF
  CHECK(info.params[Freeze::StopTime].def == 2.0f);    // 1/2 bar
  const struct {
    unsigned id;
    std::vector<const char*> names;
  } want[] = {{Freeze::Mode, {"FREEZE", "ROLL", "STOP", "REVERSE"}},
              {Freeze::RollRate, {"1/8", "1/16", "1/32", "1/64"}},
              {Freeze::Accelerate, {"OFF", "BEAT", "BAR"}},
              {Freeze::StopTime, {"1/8", "1/4", "1/2", "1", "2"}}};
  for (const auto& w : want) {
    for (std::size_t i = 0; i < w.names.size(); ++i) {
      char text[16];
      (void)o3sdk::core::param_format(info.params[w.id], static_cast<float>(i), text, sizeof text);
      CHECK(std::strcmp(text, w.names[i]) == 0);
    }
  }
  // The helpers against the independent formulas, at three tempos, and the clamps.
  for (const std::uint32_t mbpm : {60'000u, 120'000u, 174'000u, 300'000u}) {
    for (unsigned r = 0; r < Freeze::kRollRateCount; ++r) {
      CAPTURE(mbpm);
      CAPTURE(r);
      CHECK(Freeze::roll_frames(r, mbpm, 131072) == expected_roll(r, mbpm));
    }
  }
  CHECK(Freeze::roll_frames(3, 300'000, 131072) == 551);  // 1/64 at 300 BPM: above kMinRoll, so not clamped anywhere in the tempo range
  CHECK(Freeze::roll_frames(3, 300'000, 131072) >= Freeze::kMinRoll);
  CHECK(Freeze::roll_frames(0, 20'000, 131072) == 66150);  // 1/8 note at 20 BPM fits the real ring; a 4096 one clamps it
  CHECK(Freeze::roll_frames(0, 20'000, 4096) == 4096 - Freeze::kHeadroom);
  CHECK(Freeze::accel_frames(Freeze::kAccelOff, 120'000) == 0);
  CHECK(Freeze::accel_frames(Freeze::kAccelBeat, 120'000) == 22050);
  CHECK(Freeze::accel_frames(Freeze::kAccelBar, 120'000) == 88200);
  CHECK(Freeze::stop_frames(Freeze::kStop4th, 120'000, 131072) == 22050);
  CHECK(Freeze::stop_frames(Freeze::kStopTwo, 120'000, 131072) == 176400);
  CHECK(Freeze::stop_frames(Freeze::kStopTwo, 60'000, 131072) == 2u * (131072 - Freeze::kHeadroom));  // the lag at the end must fit the ring
  CHECK(Freeze::reverse_span(131072) == (131072 - Freeze::kHeadroom) / 2);
  CHECK(Freeze::reverse_span(1024) == 0);
}

TEST_CASE("freeze modes: Mode FREEZE, set or not, is the same unit, and the other parameters change nothing of it") {
  const Stereo x = test_signal(150000);
  Rig plain;
  const Stereo a = plain.run(x, {{0, Freeze::Length, 0.0f}, on(40000), off(90000), on(110000)});
  Rig set;
  const Stereo b = set.run(x, {mode(0, Freeze::kModeFreeze), {0, Freeze::RollRate, 3.0f}, {0, Freeze::Accelerate, 2.0f}, {0, Freeze::StopTime, 4.0f}, {0, Freeze::Length, 0.0f}, on(40000), off(90000), on(110000)});
  CHECK(bit_equal(a, b, 0, x.l.size()));
}

TEST_CASE("freeze modes: ROLL loops the last Roll Rate of the input, crossfaded as FREEZE is, at every rate and tempo (oracle, periods)") {
  const Stereo x = test_signal(260000);
  for (const std::uint32_t mbpm : {90'000u, 120'000u, 174'000u, 300'000u}) {
    for (unsigned r = 0; r < Freeze::kRollRateCount; ++r) {
      CAPTURE(mbpm);
      CAPTURE(r);
      Rig rig;
      const std::uint32_t n0 = 100000;
      const Stereo y = rig.run(x, {mode(0, Freeze::kModeRoll), rate(0, r), on(n0)}, mbpm);
      const std::uint32_t loop = expected_roll(r, mbpm);
      CHECK(rig.fz.loop() == loop);
      CHECK(rig.fz.rolling());
      Plan p;
      p.n0 = n0;
      p.loop = loop;
      p.fade = Freeze::fade_frames(static_cast<float>(kRate), loop);
      CHECK(max_diff(y, freeze_oracle(x, p), 0, x.l.size()) < kTol);
      // The period: the output repeats at exactly the loop and not at its neighbours.
      const std::size_t a = n0 + 4u * loop;
      bool same = true;
      bool near = false;
      for (std::size_t i = a; i < a + loop; ++i) {
        same = same && y.l[i] == y.l[i + loop] && y.r[i] == y.r[i + loop];
        near = near || y.l[i] == y.l[i + loop + 1u];
      }
      CHECK(same);
      CHECK_FALSE(near);
    }
  }
}

TEST_CASE("freeze modes: ROLL released leaves as FREEZE does, and the input is bit for bit back") {
  const Stereo x = test_signal(220000);
  Rig rig;
  const std::uint32_t n0 = 80000;
  const std::uint32_t n1 = 130000;
  const Stereo y = rig.run(x, {mode(0, Freeze::kModeRoll), rate(0, Freeze::kRoll16), on(n0), off(n1)});
  const std::uint32_t loop = expected_roll(Freeze::kRoll16, 120'000);
  Plan p;
  p.n0 = n0;
  p.n1 = n1;
  p.loop = loop;
  p.fade = Freeze::fade_frames(static_cast<float>(kRate), loop);
  CHECK(max_diff(y, freeze_oracle(x, p), 0, x.l.size()) < kTol);
  CHECK(bit_equal(x, y, n1 + 2u * p.fade, x.l.size()));
  CHECK_FALSE(rig.fz.frozen());
  const double bound = max_step(x, 0, x.l.size()) + 2.0 * max_abs(x) / (p.fade + 1.0) + 1e-4;
  CHECK(max_step(y, 0, y.l.size()) <= bound);
}

TEST_CASE("freeze modes: Accelerate halves the roll each beat or bar down to 1/64, at the loop boundary, with no click and the head of the loop") {
  struct Case {
    Freeze::AccelerateValue accel;
    std::uint32_t period;  // frames between steps at 120 BPM
  };
  const Case cases[] = {{Freeze::kAccelBeat, 22050}, {Freeze::kAccelBar, 88200}};
  for (const Case& c : cases) {
    CAPTURE(c.period);
    const std::uint32_t n0 = 60000;
    const std::size_t total = n0 + 4u * c.period + 30000u;
    const Stereo x = test_signal(total);
    Rig rig;
    // One frame a block: the frame where the loop changes is seen exactly.
    std::vector<std::pair<std::size_t, std::uint32_t>> changes;  // the first frame the unit reports a new loop
    std::uint32_t last = 0;
    const Stereo y = rig.run(x, {mode(0, Freeze::kModeRoll), rate(0, Freeze::kRoll8), {0, Freeze::Accelerate, static_cast<float>(c.accel)}, on(n0)}, 120'000, 1,
                             [&](std::size_t done) {
                               if (done > n0 && rig.fz.loop() != last) {
                                 changes.emplace_back(done - 1u, rig.fz.loop());
                                 last = rig.fz.loop();
                               }
                             });
    // The model: repeats of the current loop; at each end of a repeat the request counted so far is applied.
    std::vector<std::pair<std::size_t, std::uint32_t>> want;
    {
      unsigned cur = 0;
      unsigned req = 0;
      std::uint64_t e = n0;
      std::uint64_t next_step = static_cast<std::uint64_t>(n0) + c.period;
      want.emplace_back(n0, expected_roll(0, 120'000));
      while (e < total) {
        e += expected_roll(cur, 120'000);
        while (next_step <= e) {
          req = std::min(req + 1u, 3u);
          next_step += c.period;
        }
        if (req != cur && e < total) {
          cur = req;
          want.emplace_back(e, expected_roll(cur, 120'000));
        }
      }
    }
    NEED(changes.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
      CAPTURE(i);
      CHECK(changes[i].first == want[i].first);
      CHECK(changes[i].second == want[i].second);
    }
    CHECK(changes.back().second == expected_roll(3, 120'000));  // it ends at 1/64 and stays
    // Each stage's loop is the head of the first one: the first L - fade frames of its first repeat are the first frames of the loop.
    const std::size_t first_loop = n0 + want[0].second;  // the second repeat of the first loop: its head
    for (std::size_t i = 1; i < want.size(); ++i) {
      CAPTURE(i);
      const std::uint32_t len = want[i].second;
      const std::uint32_t fade = Freeze::fade_frames(static_cast<float>(kRate), len);
      const std::size_t s = want[i].first;
      NEED(s + len + len <= total);
      for (std::size_t j = 0; j + fade < len; j += 7) {
        NEED(y.l[s + j] == y.l[first_loop + j]);
        NEED(y.r[s + j] == y.r[first_loop + j]);
        // and the next repeat (the stage's own loop, tail included) is the same as the first
        NEED(y.l[s + len + j] == y.l[s + j]);
      }
      // The tail is a fade into the end of the previous loop: the join at the new loop point is the old one.
      for (std::uint32_t t = 0; t < fade; ++t) {
        const double a = static_cast<double>(t + 1u) / static_cast<double>(fade);
        const double head = static_cast<double>(y.l[first_loop + len - fade + t]);
        const double tail = static_cast<double>(y.l[s - fade + t]);
        NEED(std::fabs(static_cast<double>(y.l[s + len - fade + t]) - (head * (1.0 - a) + tail * a)) < kTol);
      }
      // No click across the relatch or the new loop point.
      const double bound = max_step(x, 0, x.l.size()) + 2.0 * max_abs(x) / (static_cast<double>(fade) + 1.0) + 1e-4;
      CHECK(max_step(y.l, s - 10u, s + 2u * len + 10u) <= bound);
      CHECK(max_step(y.r, s - 10u, s + 2u * len + 10u) <= bound);
    }
  }
}

TEST_CASE("freeze modes: a Roll Rate set while rolling applies at the next loop boundary: shorter without a join, longer by starting over") {
  const Stereo x = test_signal(300000);
  const std::uint32_t n0 = 80000;
  const std::uint32_t mbpm = 120'000;
  for (const bool shorter : {true, false}) {
    CAPTURE(shorter);
    Rig rig;
    const unsigned r0 = shorter ? Freeze::kRoll16 : Freeze::kRoll64;
    const unsigned r1 = shorter ? Freeze::kRoll32 : Freeze::kRoll8;
    const std::uint32_t l0 = expected_roll(r0, mbpm);
    const std::uint32_t l1 = expected_roll(r1, mbpm);
    const std::uint32_t change = n0 + 3u * l0 + l0 / 3u;  // in the middle of the fourth repeat
    std::vector<std::pair<std::size_t, std::uint32_t>> changes;
    std::uint32_t last = 0;
    const Stereo y = rig.run(x, {mode(0, Freeze::kModeRoll), rate(0, r0), on(n0), rate(change, r1)}, mbpm, 1, [&](std::size_t done) {
      if (done > n0 && rig.fz.loop() != last) {
        changes.emplace_back(done - 1u, rig.fz.loop());
        last = rig.fz.loop();
      }
    });
    NEED(changes.size() >= 2);
    CHECK(changes[0].second == l0);
    if (shorter) {
      NEED(changes.size() == 2);
      CHECK(changes[1].first == n0 + 4u * l0);  // the end of the repeat the change came in
      CHECK(changes[1].second == l1);
      CHECK(rig.fz.roll_rate() == r1);
    } else {
      // The roll leaves (5 ms) and enters again with the new rate: a new capture, so the loop is the longer one from the entry on.
      CHECK(changes[1].second == l1);
      CHECK(changes[1].first >= n0 + 4u * l0);
      CHECK(changes[1].first <= n0 + 4u * l0 + 2u * Freeze::fade_frames(static_cast<float>(kRate), l0) + 1u);
      CHECK(rig.fz.roll_rate() == r1);
    }
    const double bound = max_step(x, 0, x.l.size()) + 2.0 * max_abs(x) / 131.0 + 1e-4;
    CHECK(max_step(y, 0, y.l.size()) <= bound);
    CHECK(all_finite(y));
  }
}

TEST_CASE("freeze modes: STOP slows the read to a halt, monotonically, in the time asked, and ends in silence") {
  const std::uint32_t mbpm = 120'000;
  const std::uint32_t t_stop = Freeze::stop_frames(Freeze::kStop4th, mbpm, 65536);  // 22,050
  const std::uint32_t n0 = 20000;
  const std::uint32_t n1 = n0 + t_stop + 20000;
  const Stereo x = pure_sine(n1 + 30000, 1000.0, 0.5);
  Rig rig;
  std::vector<float> speed;
  std::vector<std::uint32_t> lag;
  const Stereo y = rig.run(x, {mode(0, Freeze::kModeStop), {0, Freeze::StopTime, static_cast<float>(Freeze::kStop4th)}, on(n0), off(n1)}, mbpm, 64,
                           [&](std::size_t) {
                             speed.push_back(rig.fz.stop_speed());
                             lag.push_back(rig.fz.stop_lag());
                           });
  // The speed: 1 until the entry, falling to 0 at n0 + t_stop, 0 while held, then rising to 1, never turning back.
  std::size_t reach0 = 0;
  std::size_t back1 = 0;
  for (std::size_t i = 1; i < speed.size(); ++i) {
    const std::size_t f = (i + 1) * 64;
    if (f <= n0 + 64) {
      continue;
    }
    if (f <= n1) {
      CHECK(speed[i] <= speed[i - 1]);
      if (speed[i] == 0.0f && reach0 == 0) {
        reach0 = f;
      }
    } else if (f > n1 + 64) {
      CHECK(speed[i] >= speed[i - 1]);
      if (speed[i] == 1.0f && back1 == 0) {
        back1 = f;
      }
    }
  }
  CHECK(reach0 >= n0 + t_stop - 64);
  CHECK(reach0 <= n0 + t_stop + 64);
  // The spin-up from 0 takes a quarter of the stop time (5,512 frames) and then the 5 ms crossfade.
  const std::uint32_t spin = t_stop / 4u;
  CHECK(back1 >= n1 + spin - 64);
  CHECK(back1 <= n1 + spin + 128);
  // The lag at the end of the slow-down is half of it (the area under the speed curve) and keeps growing while it is held.
  const std::size_t i_end = (n0 + t_stop) / 64u;
  CHECK(static_cast<double>(lag[i_end]) == doctest::Approx(t_stop / 2.0).epsilon(0.01));
  CHECK(lag[(n1 - 1) / 64u] > lag[i_end]);
  // The pitch follows the speed: the rate of the up-crossings in windows falls with it, and tracks 1 kHz x speed.
  const auto crossings = [&](std::size_t from, std::size_t to) {
    unsigned n = 0;
    for (std::size_t i = from + 1; i < to; ++i) {
      n += (y.l[i - 1] < 0.0f && y.l[i] >= 0.0f) ? 1u : 0u;
    }
    return n;
  };
  double previous = 1e9;
  for (std::size_t w = n0 + 2000; w + 2205 < n0 + t_stop - 6000; w += 2205) {
    const double hz = crossings(w, w + 2205) * (kRate / 2205.0);
    const double mid = static_cast<double>(w + 1102 - n0);
    const double expect = 1000.0 * (1.0 - mid / t_stop);
    CAPTURE(w);
    CHECK(hz <= previous + 25.0);
    CHECK(hz == doctest::Approx(expect).epsilon(0.12));
    previous = hz;
  }
  // Held at 0 the output is silent (Mix 1), exactly.
  bool silent = true;
  for (std::size_t i = n0 + t_stop + 200; i < n1; ++i) {
    silent = silent && y.l[i] == 0.0f && y.r[i] == 0.0f;
  }
  CHECK(silent);
}

TEST_CASE("freeze modes: STOP released spins up and crossfades back; the input is bit for bit again; no click at entry or exit") {
  const std::uint32_t mbpm = 120'000;
  const std::uint32_t t_stop = Freeze::stop_frames(Freeze::kStop4th, mbpm, 65536);
  const std::uint32_t n0 = 20000;
  struct Case {
    const char* what;
    std::uint32_t release;  // frames after the entry
  };
  const Case cases[] = {{"released in the entry's first frames", 3}, {"released mid slow-down", t_stop / 2}, {"released at the stop", t_stop}, {"released long after", t_stop + 20000}};
  for (const Case& c : cases) {
    CAPTURE(c.what);
    const std::uint32_t n1 = n0 + c.release;
    const Stereo x = pure_sine(n1 + 20000 + t_stop, 1000.0, 0.5);
    Rig rig;
    const Stereo y = rig.run(x, {mode(0, Freeze::kModeStop), {0, Freeze::StopTime, static_cast<float>(Freeze::kStop4th)}, on(n0), off(n1)}, mbpm);
    // Done by n1 + spin-up + fade; from there on, the input itself.
    const std::uint32_t fade = Freeze::fade_frames(static_cast<float>(kRate), t_stop);
    CHECK(bit_equal(x, y, n1 + t_stop / 4u + fade + 128u, x.l.size()));
    CHECK(bit_equal(x, y, 0, n0));
    CHECK_FALSE(rig.fz.frozen());
    // The sine's own steepest step is 0.0712; the crossfade of two unrelated phases adds at most 1 / fade.
    const double bound = max_step(x, 0, x.l.size()) + 1.0 / fade + 1e-4;
    CHECK(max_step(y, 0, y.l.size()) <= bound);
    CHECK(all_finite(y));
  }
}

TEST_CASE("freeze modes: STOP records the input all along, so a freeze right after loops real material") {
  const Stereo x = test_signal(260000);
  const std::uint32_t mbpm = 120'000;
  Rig rig;
  const std::uint32_t n_stop = 60000;
  const std::uint32_t n_stop_off = 80000;
  const std::uint32_t n_freeze = 120000;
  const Stereo y = rig.run(x, {mode(0, Freeze::kModeStop), {0, Freeze::StopTime, 0.0f}, on(n_stop), off(n_stop_off), mode(100000, Freeze::kModeFreeze), {100000, Freeze::Length, 0.0f}, on(n_freeze)}, mbpm);
  Plan p;
  p.n0 = n_freeze;
  p.loop = Freeze::loop_frames(0, mbpm, 65536);
  p.fade = Freeze::fade_frames(static_cast<float>(kRate), p.loop);
  CHECK(max_diff(y, freeze_oracle(x, p), n_freeze, x.l.size()) < kTol);
}

TEST_CASE("freeze modes: REVERSE plays the input backwards from the entry, crossfaded in and out (oracle)") {
  const Stereo x = test_signal(120000);
  struct Case {
    std::uint32_t n0;
    std::uint32_t k1;  // frames of reversal before the release
  };
  const Case cases[] = {{50000, 3000}, {50000, 100}, {50000, 220}, {50000, 221}, {20000, 9000}, {1000, 800}};
  for (const Case& c : cases) {
    CAPTURE(c.n0);
    CAPTURE(c.k1);
    Rig rig;
    const Stereo y = rig.run(x, {mode(0, Freeze::kModeReverse), on(c.n0), off(c.n0 + c.k1)});
    const std::uint32_t X = Freeze::fade_frames(static_cast<float>(kRate), Freeze::reverse_span(65536));
    CHECK(X == 221);
    const std::uint32_t xp1 = std::min(c.k1, X);
    Stereo want = x;
    for (int ch = 0; ch < 2; ++ch) {
      const std::vector<float>& in = ch == 0 ? x.l : x.r;
      std::vector<float>& out = ch == 0 ? want.l : want.r;
      const auto at = [&](std::int64_t i) { return i < 0 ? 0.0 : static_cast<double>(in[static_cast<std::size_t>(i)]); };
      for (std::int64_t k = 0; k < static_cast<std::int64_t>(c.k1) + xp1; ++k) {
        const double share = k < c.k1 ? static_cast<double>(std::min<std::int64_t>(k, X)) / X : static_cast<double>(xp1 - (k - c.k1)) / X;
        const std::int64_t n = static_cast<std::int64_t>(c.n0) + k;
        out[static_cast<std::size_t>(n)] = static_cast<float>(at(n) * (1.0 - share) + at(c.n0 - k) * share);
      }
    }
    CHECK(max_diff(y, want, 0, x.l.size()) < kTol);
    CHECK(bit_equal(x, y, c.n0 + c.k1 + xp1, x.l.size()));
    const double bound = max_step(x, 0, x.l.size()) + 2.0 * max_abs(x) / (X + 1.0) + 1e-4;
    CHECK(max_step(y, 0, y.l.size()) <= bound);
  }
}

TEST_CASE("freeze modes: a REVERSE held past its span leaves and starts again from the new now, without a click; the ring stays a true record") {
  constexpr std::uint32_t kCap = 4096;  // the smallest ring: the span is 1,536
  const Stereo x = test_signal(30000);
  Rig rig(kCap);
  const std::uint32_t n0 = 8000;
  const std::uint32_t n1 = 22000;
  const Stereo y = rig.run(x, {mode(0, Freeze::kModeReverse), on(n0), off(n1)});
  const std::uint32_t span = Freeze::reverse_span(kCap);
  CHECK(span == 1536);
  const std::uint32_t X = Freeze::fade_frames(static_cast<float>(kRate), span);
  // The oracle: cycles of `span` frames from n0; in each, the share rises over X, is full until span - X, and falls over X; the read is
  // x[start - k]. After the last full cycle the release (n1) finds the unit in a cycle: it falls from the share it has.
  Stereo want = x;
  for (int ch = 0; ch < 2; ++ch) {
    const std::vector<float>& in = ch == 0 ? x.l : x.r;
    std::vector<float>& out = ch == 0 ? want.l : want.r;
    const auto at = [&](std::int64_t i) { return i < 0 ? 0.0 : static_cast<double>(in[static_cast<std::size_t>(i)]); };
    std::int64_t start = n0;
    std::int64_t n = n0;
    bool released = false;
    while (n < static_cast<std::int64_t>(x.l.size()) && !(released && n >= n1 + 2 * X + span)) {
      // one cycle
      std::int64_t xp = 0;
      bool forced = false;
      for (std::int64_t k = 0; n < static_cast<std::int64_t>(x.l.size()); ++k, ++n) {
        if (k >= static_cast<std::int64_t>(span) - X) {
          forced = true;
        }
        const bool down = forced || n >= n1;
        if (down && xp == 0) {
          break;  // the cycle is over (Idle); a held Active starts the next one at this very frame
        }
        const double share = xp >= X ? 1.0 : static_cast<double>(xp) / X;
        xp += down ? -1 : (xp < X ? 1 : 0);
        out[static_cast<std::size_t>(n)] = static_cast<float>(at(n) * (1.0 - share) + at(start - k) * share);
      }
      if (n >= n1) {
        released = true;
        break;
      }
      start = n;
    }
  }
  CHECK(max_diff(y, want, 0, x.l.size()) < kTol);
  const double bound = max_step(x, 0, x.l.size()) + 2.0 * max_abs(x) / (X + 1.0) + 1e-4;
  CHECK(max_step(y, 0, y.l.size()) <= bound);
  CHECK(bit_equal(x, y, n1 + 2u * X + span, x.l.size()));
}

TEST_CASE("freeze modes: every mode gives the same output at any block size") {
  const Stereo x = test_signal(120000);
  const std::vector<std::vector<Ev>> gestures = {
      {mode(0, Freeze::kModeRoll), rate(0, Freeze::kRoll8), {0, Freeze::Accelerate, 1.0f}, on(20000), rate(41000, Freeze::kRoll16), off(95000)},
      {mode(0, Freeze::kModeStop), {0, Freeze::StopTime, 1.0f}, on(20000), off(60000), on(75000), off(76000)},
      {mode(0, Freeze::kModeReverse), on(20000), off(26000), on(30000), off(90000)},
  };
  for (std::size_t g = 0; g < gestures.size(); ++g) {
    CAPTURE(g);
    Rig ref;
    const Stereo a = ref.run(x, gestures[g], 100'000, 64);
    for (const std::uint32_t block : {1u, 7u, 33u, 100u}) {
      CAPTURE(block);
      Rig other;
      const Stereo b = other.run(x, gestures[g], 100'000, block);
      CHECK(max_diff(a, b, 0, x.l.size()) < 1e-6);
    }
    CHECK(all_finite(a));
  }
}

TEST_CASE("freeze modes: gesture mashing across the modes stays finite, bounded and continuous, and ends at the input") {
  const Stereo x = test_signal(500000);
  const double bound = max_step(x, 0, x.l.size()) + 2.0 * max_abs(x) / 130.0 + 1e-4;
  std::uint32_t seed = 777u;
  const auto rnd = [&seed](std::uint32_t n) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % n;
  };
  for (int round = 0; round < 8; ++round) {
    Rig r(round % 3 == 0 ? 4096u : 131072u);
    std::vector<Ev> evs;
    std::uint32_t at = 70000;
    bool active = false;
    while (at < x.l.size() - 60000u) {
      evs.push_back(active ? off(at) : on(at));
      active = !active;
      if (rnd(2) == 0) {
        evs.push_back(mode(at > 3 ? at - 3 : 0, static_cast<Freeze::ModeValue>(rnd(4))));
      }
      if (rnd(3) == 0) {
        evs.push_back({at + rnd(40), Freeze::RollRate, static_cast<float>(rnd(4))});
      }
      if (rnd(4) == 0) {
        evs.push_back({at + rnd(40), Freeze::Accelerate, static_cast<float>(rnd(3))});
      }
      if (rnd(4) == 0) {
        evs.push_back({at + rnd(40), Freeze::StopTime, static_cast<float>(rnd(5))});
      }
      if (rnd(3) == 0) {
        evs.push_back({at + rnd(40), Freeze::Mix, static_cast<float>(rnd(101)) / 100.0f});
      }
      at += 1u + rnd(active ? 20000u : 1500u);
    }
    evs.push_back(off(static_cast<std::uint32_t>(x.l.size()) - 55000u));
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.at < b.at; });
    const std::uint32_t block = 1u + rnd(64);
    INFO("round " << round << ", block " << block);
    const Stereo y = r.run(x, evs, 40'000u + 30'000u * static_cast<std::uint32_t>(round), block);
    CHECK(all_finite(y));
    CHECK(max_abs(y) <= max_abs(x) + 1e-5);
    CHECK(max_step(y, 0, y.l.size()) <= bound);
    CHECK(bit_equal(x, y, x.l.size() - 30000u, x.l.size()));
  }
}

// FREEZE stays what was verified on the unit on 2026-10-26 (o3-303 f3c08e6). The hash was recorded when the M3c modes were added, from code
// that a 1.5 M-frame run had found bit for bit equal to the verified one. A change of this value is a change to FREEZE.
TEST_CASE("freeze: the FREEZE mode's output is bit for bit the unit-verified one (golden checksum)") {
  constexpr std::size_t kN = 200'000;
  Stereo in;
  in.l.resize(kN);
  in.r.resize(kN);
  std::uint32_t lcg = 12345u;
  for (std::size_t i = 0; i < kN; ++i) {
    lcg = lcg * 1664525u + 1013904223u;
    in.l[i] = static_cast<float>(static_cast<std::int32_t>(lcg) >> 8) * (0.5f / 8388608.0f);
    lcg = lcg * 1664525u + 1013904223u;
    in.r[i] = static_cast<float>(static_cast<std::int32_t>(lcg) >> 8) * (0.5f / 8388608.0f);
  }
  const std::vector<Ev> gesture = {
      on(20'000),  off(41'000), {50'000, Freeze::Length, 2.0f}, on(60'003), off(64'500), on(70'000), off(70'900),
      {80'000, Freeze::Length, 0.0f}, {81'000, Freeze::Mix, 0.5f}, on(90'017), off(130'000), {131'000, Freeze::Mix, 1.0f},
      on(150'000), {160'000, Freeze::Length, 1.0f}, off(175'000),
  };
  Rig rig(131'072);
  const Stereo out = rig.run(in, gesture, 125'000, 64);
  std::uint32_t h = 2166136261u;
  for (std::size_t i = 0; i < kN; ++i) {
    for (const float v : {out.l[i], out.r[i]}) {
      std::uint32_t bits;
      std::memcpy(&bits, &v, sizeof bits);
      h = (h ^ bits) * 16777619u;
    }
  }
  CHECK(h == 425045298u);
}
