// SPDX-License-Identifier: GPL-3.0-or-later
// DISTORTION against the engine's contract: the parameter table, an insert in a slot, stereo and in-place use, the bypass, the
// smoothing of Drive / Mix / Active (no click), the DC blocker, silence, and no NaN or denormals.
#include <doctest/doctest.h>

#include "support/need.h"
#include "support/rng.h"

#include <o3sdk/dsp/engine.h>

#include <o3_303/units/distortion/distortion.h>

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <vector>

using o3_303::Distortion;
namespace dsp = o3sdk::dsp;

namespace {

constexpr double kRate = 44100.0;

struct Ev {
  std::uint32_t at;
  std::uint16_t id;
  float value;
};

// Runs `d` over (in_l, in_r) in blocks of `block`, with parameter events at absolute samples, optionally in place.
void run(Distortion& d, const std::vector<float>& in_l, const std::vector<float>& in_r, std::vector<float>& out_l, std::vector<float>& out_r,
         const std::vector<Ev>& evs, std::uint32_t block = 64, bool in_place = false) {
  d.prepare(static_cast<float>(kRate));
  dsp::Transport tr;
  float zero[64] = {};
  float dummy[64];
  {
    dsp::Event start[16];
    unsigned n = 0;
    for (const Ev& e : evs) {
      if (e.at == 0 && n < 16) {
        start[n++] = dsp::param_change(e.id, e.value);
      }
    }
    tr.frames = 0;
    d.process(dsp::ProcessArgs{start, n, zero, zero, dummy, dummy, 0, &tr});
  }
  const std::size_t total = in_l.size();
  out_l.assign(total, 0.0f);
  out_r.assign(total, 0.0f);
  std::vector<float> wl = in_l;
  std::vector<float> wr = in_r;
  for (std::size_t pos = 0; pos < total; pos += block) {
    const auto n = static_cast<std::uint32_t>(total - pos < block ? total - pos : block);
    dsp::Event blk[16];
    unsigned k = 0;
    for (const Ev& e : evs) {
      if (e.at != 0 && e.at >= pos && e.at < pos + n && k < 16) {
        blk[k++] = dsp::param_change(e.id, e.value, static_cast<std::uint16_t>(e.at - pos));
      }
    }
    tr.frames = n;
    if (in_place) {
      d.process(dsp::ProcessArgs{blk, k, wl.data() + pos, wr.data() + pos, wl.data() + pos, wr.data() + pos, n, &tr});
    } else {
      d.process(dsp::ProcessArgs{blk, k, in_l.data() + pos, in_r.data() + pos, out_l.data() + pos, out_r.data() + pos, n, &tr});
    }
  }
  if (in_place) {
    out_l = wl;
    out_r = wr;
  }
}

std::vector<float> saw(double hz, float amp, std::size_t n) {
  std::vector<float> v(n);
  double ph = 0.0;
  for (auto& s : v) {
    ph += hz / kRate;
    ph -= std::floor(ph);
    s = amp * static_cast<float>(2.0 * ph - 1.0);
  }
  return v;
}

bool normal_finite(const std::vector<float>& v) {
  for (float s : v) {
    const int c = std::fpclassify(s);
    if (c == FP_NAN || c == FP_INFINITE || c == FP_SUBNORMAL) {
      return false;
    }
  }
  return true;
}

double max_step(const std::vector<float>& v, std::size_t from, std::size_t to) {
  double m = 0.0;
  for (std::size_t i = from; i < to; ++i) {
    m = std::max(m, static_cast<double>(std::fabs(v[i] - v[i - 1])));
  }
  return m;
}

}  // namespace

TEST_CASE("distortion: the parameter table is valid and in the contract's order") {
  Distortion d;
  const auto& info = d.info();
  CHECK(info.kind == dsp::UnitKind::Effect);
  REQUIRE(info.param_count == Distortion::kParamCount);
  for (unsigned p = 0; p < info.param_count; ++p) {
    INFO(info.params[p].name);
    CHECK(o3sdk::core::param_desc_valid(info.params[p]));
  }
  CHECK(Distortion::Active == 0);
  CHECK(Distortion::Type == 1);
  CHECK(Distortion::Drive == 2);
  CHECK(Distortion::Tone == 3);
  CHECK(Distortion::Mix == 4);
  CHECK(Distortion::kParamCount == 5);
  const float defaults[] = {1.0f, 0.0f, 0.5f, 0.6f, 0.85f};
  for (unsigned p = 0; p < 5; ++p) {
    CHECK(info.params[p].def == defaults[p]);
  }
  char text[16];
  (void)o3sdk::core::param_format(info.params[Distortion::Type], 0.0f, text, sizeof(text));
  CHECK(std::strcmp(text, "GRIT") == 0);
  (void)o3sdk::core::param_format(info.params[Distortion::Type], 4.0f, text, sizeof(text));
  CHECK(std::strcmp(text, "FOLD") == 0);
  (void)o3sdk::core::param_format(info.params[Distortion::Active], 1.0f, text, sizeof(text));
  CHECK(std::strcmp(text, "ON") == 0);
  (void)o3sdk::core::param_format(info.params[Distortion::Drive], 0.5f, text, sizeof(text));
  CHECK(std::strcmp(text, "50") == 0);
  (void)o3sdk::core::param_format(info.params[Distortion::Mix], 0.85f, text, sizeof(text));
  CHECK(std::strcmp(text, "85") == 0);
}

namespace {
// A mono instrument that plays a saw at 110 Hz from its first block: the source of the engine test.
class SawSource final : public dsp::Unit {
 public:
  const dsp::UnitInfo& info() const noexcept override { return info_; }
  void prepare(float) noexcept override { phase_ = 0.0f; }
  void process(const dsp::ProcessArgs& a) noexcept override {
    for (std::uint32_t i = 0; i < a.frames; ++i) {
      phase_ += 110.0f / 44100.0f;
      phase_ -= phase_ >= 1.0f ? 1.0f : 0.0f;
      a.out_l[i] = 0.7f * (2.0f * phase_ - 1.0f);
      a.out_r[i] = a.out_l[i];
    }
  }

 private:
  static constexpr dsp::UnitInfo info_{"SAW", dsp::UnitKind::Instrument, nullptr, 0, nullptr, 0};
  float phase_ = 0.0f;
};
}  // namespace

// What an inactive unit does to a signal: the DC blocker (y = x - x1 + R y1, R = exp(-2 pi 5 / fs)) and nothing else.
std::vector<float> blocked(const std::vector<float>& x) {
  const float r = std::exp(-2.0f * 3.14159265f * 5.0f * (1.0f / 44100.0f));
  std::vector<float> y(x.size());
  float x1 = 0.0f;
  float y1 = 0.0f;
  for (std::size_t i = 0; i < x.size(); ++i) {
    y1 = x[i] - x1 + r * y1;
    x1 = x[i];
    y[i] = y1;
  }
  return y;
}

TEST_CASE("distortion: an insert in the engine, parameters through the engine, each model changes the signal and stays bounded") {
  for (int m = 0; m < 5; ++m) {
    INFO("model " << m);
    dsp::Engine e;
    SawSource src;
    Distortion dist;
    const auto s = e.attach(src);
    const auto d = e.attach(dist);
    REQUIRE(e.set_source(0, s));
    REQUIRE(e.set_insert(0, 0, d));
    REQUIRE(e.set_param(d, Distortion::Type, static_cast<float>(m)));
    REQUIRE(e.set_param(d, Distortion::Drive, 0.8f));
    e.start();
    std::vector<float> out_l;
    std::vector<float> out_r;
    float l[64];
    float r[64];
    for (int b = 0; b < 400; ++b) {
      e.render(nullptr, nullptr, l, r, 64);
      out_l.insert(out_l.end(), l, l + 64);
      out_r.insert(out_r.end(), r, r + 64);
    }
    double pk = 0.0;
    for (std::size_t i = 0; i < out_l.size(); ++i) {
      pk = std::max(pk, static_cast<double>(std::fabs(out_l[i])));
      REQUIRE(out_l[i] == out_r[i]);  // a mono source stays mono
    }
    CHECK(pk < 1.0);  // the engine's soft clip is the bound
    CHECK(pk > 0.1);
    CHECK(normal_finite(out_l));

    // Active off: after the crossfade and the settling of the blocker (a second) the engine's output is the source through the DC
    // blocker, to within the slot's gain (cos(pi/4) x sqrt 2, which is 1 to a few ulps). The source is run again alongside for it.
    REQUIRE(e.set_param(d, Distortion::Active, 0.0f));
    const std::size_t extra = 44100;
    for (std::size_t done = 0; done < extra; done += 64) {
      e.render(nullptr, nullptr, l, r, 64);
      out_l.insert(out_l.end(), l, l + 64);
    }
    SawSource twin;
    twin.prepare(44100.0f);
    std::vector<float> plain(out_l.size());
    std::vector<float> plain_r(64);
    for (std::size_t pos = 0; pos < plain.size(); pos += 64) {
      const auto n = static_cast<std::uint32_t>(plain.size() - pos < 64 ? plain.size() - pos : 64);
      twin.process(dsp::ProcessArgs{nullptr, 0, nullptr, nullptr, plain.data() + pos, plain_r.data(), n, nullptr});
    }
    const auto expect = blocked(plain);
    double worst = 0.0;
    for (std::size_t i = out_l.size() - 4096; i < out_l.size(); ++i) {
      worst = std::max(worst, static_cast<double>(std::fabs(out_l[i] - expect[i])));
    }
    CHECK(worst < 1e-5);
  }
}


TEST_CASE("distortion: Active off and Mix 0 leave the signal alone but for the DC blocker, in place or not") {
  const auto in = saw(110.0, 0.8f, 8000);
  const auto expect = blocked(in);
  for (int variant = 0; variant < 2; ++variant) {
    Distortion d;
    std::vector<float> ol, orr;
    const std::vector<Ev> evs = {{0, Distortion::Active, variant == 0 ? 0.0f : 1.0f}, {0, Distortion::Mix, variant == 0 ? 0.85f : 0.0f}};
    run(d, in, in, ol, orr, evs);
    for (std::size_t i = 0; i < in.size(); ++i) {
      REQUIRE(ol[i] == expect[i]);
      REQUIRE(orr[i] == expect[i]);
    }
    Distortion d2;
    run(d2, in, in, ol, orr, evs, 64, true);
    for (std::size_t i = 0; i < in.size(); ++i) {
      REQUIRE(ol[i] == expect[i]);
    }
  }
}

TEST_CASE("distortion: stereo channels are independent, a mono signal is computed once, in place gives the same samples") {
  const auto a = saw(110.0, 0.8f, 20000);
  auto b = saw(147.0, 0.5f, 20000);
  for (std::size_t i = 0; i < b.size(); ++i) {
    b[i] = -b[i] * static_cast<float>(1.0 + 0.5 * std::sin(0.001 * static_cast<double>(i)));
  }
  for (int m = 0; m < 5; ++m) {
    INFO("model " << m);
    const std::vector<Ev> evs = {{0, Distortion::Type, static_cast<float>(m)}, {0, Distortion::Drive, 0.6f}};
    Distortion st, ma, mb;
    std::vector<float> sl, sr, al, ar, bl, br;
    run(st, a, b, sl, sr, evs);
    run(ma, a, a, al, ar, evs);
    run(mb, b, b, bl, br, evs);
    for (std::size_t i = 0; i < a.size(); ++i) {
      REQUIRE(al[i] == ar[i]);  // mono in, mono out
      REQUIRE(sl[i] == al[i]);  // the left channel of a stereo pair is what the mono left gives
      REQUIRE(sr[i] == bl[i]);  // and the right channel what the mono right gives
    }
    Distortion ip;
    std::vector<float> pl, pr;
    run(ip, a, b, pl, pr, evs, 64, true);
    for (std::size_t i = 0; i < a.size(); ++i) {
      REQUIRE(pl[i] == sl[i]);
      REQUIRE(pr[i] == sr[i]);
    }
    // A signal that is mono for a while and then not: the right channel follows its own input from the first different sample.
    auto c = a;
    for (std::size_t i = 10000; i < c.size(); ++i) {
      c[i] = b[i];
    }
    Distortion sw;
    std::vector<float> wl, wr;
    run(sw, a, c, wl, wr, evs);
    for (std::size_t i = 0; i < 10000; ++i) {
      REQUIRE(wl[i] == wr[i]);
    }
    CHECK(wr[15000] != wl[15000]);
    CHECK(normal_finite(wr));
  }
}

TEST_CASE("distortion: the block size does not matter, events at their frames") {
  const auto in = saw(98.0, 0.9f, 30000);
  const std::vector<Ev> evs = {{0, Distortion::Type, 3.0f}, {5000, Distortion::Drive, 1.0f}, {7777, Distortion::Tone, 0.1f},
                               {9000, Distortion::Mix, 0.4f}, {12000, Distortion::Active, 0.0f}, {20000, Distortion::Active, 1.0f}};
  std::vector<float> a, ar, b, br, c, cr;
  Distortion d1, d2, d3;
  run(d1, in, in, a, ar, evs, 64);
  run(d2, in, in, b, br, evs, 13);
  run(d3, in, in, c, cr, evs, 1);
  for (std::size_t i = 0; i < in.size(); ++i) {
    REQUIRE(a[i] == b[i]);
    REQUIRE(a[i] == c[i]);
  }
}

TEST_CASE("distortion: switching Active or jumping Drive does not click") {
  const auto in = saw(110.0, 0.6f, 44100);
  for (int m : {0, 3}) {
    INFO("model " << m);
    // Active: off at 20000, on again at 30000. The largest step across the switches against the largest step of the steady signal.
    {
      Distortion d;
      std::vector<float> ol, orr;
      const std::vector<Ev> evs = {{0, Distortion::Type, static_cast<float>(m)}, {0, Distortion::Drive, 0.9f}, {20000, Distortion::Active, 0.0f}, {30000, Distortion::Active, 1.0f}};
      run(d, in, in, ol, orr, evs);
      // The saw's own reset is a step of 1.2 in the dry signal; the distorted one has its own: take the steady maximum in a clean stretch.
      const double steady = max_step(ol, 14000, 19000);
      const double dry = max_step(in, 24000, 29000);
      const double bound = std::max(steady, dry) * 1.25 + 0.02;
      CHECK(max_step(ol, 20000, 21000) <= bound);
      CHECK(max_step(ol, 30000, 31000) <= bound);
      CHECK(normal_finite(ol));
    }
    // Drive: from 0.5 to 0.8 at 20000 (a coarse step of the encoder). The first samples after the jump are those of the old drive; the new
    // one arrives over a few ms. (From near 0 to full the gain changes by a factor of tens and the 5 ms glide is still audible as a swell.)
    {
      Distortion d, held;
      std::vector<float> ol, orr, hl, hr;
      const std::vector<Ev> start = {{0, Distortion::Type, static_cast<float>(m)}, {0, Distortion::Drive, 0.5f}};
      auto jump = start;
      jump.push_back({20000, Distortion::Drive, 0.8f});
      run(d, in, in, ol, orr, jump);
      run(held, in, in, hl, hr, start);
      for (std::size_t i = 20000; i < 20003; ++i) {
        CHECK(std::fabs(ol[i] - hl[i]) < 0.05);
      }
      double later = 0.0;
      for (std::size_t i = 20000 + 600; i < 20000 + 900; ++i) {
        later = std::max(later, static_cast<double>(std::fabs(ol[i] - hl[i])));
      }
      CHECK(later > 0.05);  // and it has arrived
    }
  }
}

TEST_CASE("distortion: the DC blocker takes a constant out, FUZZ included, and silence in gives silence out") {
  for (int m = 0; m < 5; ++m) {
    INFO("model " << m);
    for (float drive : {0.2f, 1.0f}) {
      // A constant input (what the voice never sends, but FUZZ's bias is one).
      std::vector<float> in(44100 * 2, 0.0f);
      for (std::size_t i = 4000; i < in.size(); ++i) {
        in[i] = 0.3f;
      }
      Distortion d;
      std::vector<float> ol, orr;
      run(d, in, in, ol, orr, {{0, Distortion::Type, static_cast<float>(m)}, {0, Distortion::Drive, drive}});
      CHECK(std::fabs(ol[in.size() - 1]) < 1e-3);
      CHECK(normal_finite(ol));
      // Silence from the start: FUZZ's output at zero input (a constant, up to 0.1) is taken off, so the unit is at rest at silence and
      // nothing comes out at all, in any model and at any drive.
      std::vector<float> z(44100 * 2, 0.0f);
      Distortion d2;
      run(d2, z, z, ol, orr, {{0, Distortion::Type, static_cast<float>(m)}, {0, Distortion::Drive, drive}});
      double bump = 0.0;
      for (float s : ol) {
        bump = std::max(bump, static_cast<double>(std::fabs(s)));
      }
      CHECK(bump < 1e-6);
      CHECK(normal_finite(ol));
    }
  }
}

TEST_CASE("distortion: switching on, or switching to FUZZ, over silence is silent at full drive; over a signal the transient is bounded") {
  const std::size_t n = 44100;
  const std::vector<float> z(n, 0.0f);
  for (int m = 0; m < 5; ++m) {
    INFO("model " << m);
    // Active: off, then on at 10000, silence all along.
    Distortion a;
    std::vector<float> ol, orr;
    run(a, z, z, ol, orr, {{0, Distortion::Type, static_cast<float>(m)}, {0, Distortion::Drive, 1.0f}, {0, Distortion::Mix, 1.0f}, {0, Distortion::Active, 0.0f}, {10000, Distortion::Active, 1.0f}});
    double pk = 0.0;
    for (float s : ol) {
      pk = std::max(pk, static_cast<double>(std::fabs(s)));
    }
    CHECK(pk < 1e-6);
    // Type: another model, then this one at 10000, silence all along.
    Distortion t;
    run(t, z, z, ol, orr, {{0, Distortion::Type, m == 0 ? 1.0f : 0.0f}, {0, Distortion::Drive, 1.0f}, {0, Distortion::Mix, 1.0f}, {10000, Distortion::Type, static_cast<float>(m)}});
    pk = 0.0;
    for (float s : ol) {
      pk = std::max(pk, static_cast<double>(std::fabs(s)));
    }
    CHECK(pk < 1e-6);
    // Over a signal: switched on at 10000 in the middle of a saw. The first 100 ms are within 25 % of the steady state that follows (the
    // models do not saturate at 1: Replicant's Pade tanh keeps a slope of 1/9, so FUZZ at full drive peaks near 5), and finite.
    auto saw_in = saw(110.0, 0.6f, n);
    Distortion s;
    run(s, saw_in, saw_in, ol, orr, {{0, Distortion::Type, static_cast<float>(m)}, {0, Distortion::Drive, 1.0f}, {0, Distortion::Mix, 1.0f}, {0, Distortion::Active, 0.0f}, {10000, Distortion::Active, 1.0f}});
    pk = 0.0;
    double steady = 0.0;
    for (std::size_t i = 10000; i < 10000 + 4410; ++i) {
      pk = std::max(pk, static_cast<double>(std::fabs(ol[i])));
    }
    for (std::size_t i = 30000; i < 30000 + 4410; ++i) {
      steady = std::max(steady, static_cast<double>(std::fabs(ol[i])));
    }
    CHECK(pk <= steady * 1.25);  // FUZZ at full drive: its mean follows the signal, and the blocker takes 30 ms to settle on it (+15 %)
    CHECK(normal_finite(ol));
  }
}

TEST_CASE("distortion: a signal that stops leaves no denormals, and the unit wakes up cleanly") {
  for (int m = 0; m < 5; ++m) {
    INFO("model " << m);
    std::vector<float> in(44100 * 4, 0.0f);
    const auto burst = saw(110.0, 0.9f, 8000);
    std::copy(burst.begin(), burst.end(), in.begin());
    std::copy(burst.begin(), burst.end(), in.begin() + 44100 * 3);
    Distortion d;
    std::vector<float> ol, orr;
    run(d, in, in, ol, orr, {{0, Distortion::Type, static_cast<float>(m)}, {0, Distortion::Drive, 0.9f}});
    CHECK(normal_finite(ol));
    CHECK(normal_finite(orr));
    // The burst after the silence comes out as the first one did (the states were at rest in between) for the models that rest at zero.
    if (m != 3) {
      for (std::size_t i = 0; i < 8000; ++i) {
        REQUIRE(ol[i] == ol[44100 * 3 + i]);
      }
    }
  }
}

TEST_CASE("distortion: a decaying tail raises no underflow, every model (no subnormal intermediates)") {
  // The voice flushes its envelopes at 1e-9, so the tail that reaches the unit is a signal decaying to 1e-9 and then exactly zero. The
  // host's SSE flags say whether any operation on the way produced a subnormal (FE_UNDERFLOW): a cube in TUBE's soft clip did for inputs
  // between 1e-15 and 2e-14, thousands of samples of every release. Nothing in o3-sdk sets flush-to-zero on the M4F.
  std::vector<float> in(80000, 0.0f);
  float amp = 0.9f;
  double ph = 0.0;
  for (std::size_t i = 0; i < 60000; ++i) {
    ph += 2.0 * 3.14159265358979 * 110.0 / kRate;
    in[i] = amp * static_cast<float>(std::sin(ph));
    amp *= 0.9995f;
    amp = amp < 1.0e-9f ? 0.0f : amp;
  }
  for (int m = 0; m < 5; ++m) {
    for (float drive : {0.5f, 1.0f}) {
      INFO("model " << m << " drive " << drive);
      Distortion d;
      std::vector<float> ol, orr;
      std::feclearexcept(FE_ALL_EXCEPT);
      run(d, in, in, ol, orr, {{0, Distortion::Type, static_cast<float>(m)}, {0, Distortion::Drive, drive}});
      const bool underflow = std::fetestexcept(FE_UNDERFLOW) != 0;
      CHECK_FALSE(underflow);
      CHECK(normal_finite(ol));
    }
  }
}

TEST_CASE("distortion: extreme levels and parameters stay finite and bounded; a NaN is flushed out within two control periods") {
  o3sdk::test::Rng rng(0xD15);
  for (int m = 0; m < 5; ++m) {
    INFO("model " << m);
    std::vector<float> in(30000);
    for (auto& s : in) {
      const float big = (static_cast<float>(rng.below(2001)) / 1000.0f - 1.0f);
      s = rng.below(50) == 0 ? big * 1000.0f : big;  // up to 1000x full scale
    }
    Distortion d;
    std::vector<float> ol, orr;
    run(d, in, in, ol, orr,
        {{0, Distortion::Type, static_cast<float>(m)}, {0, Distortion::Drive, 1.0f}, {0, Distortion::Tone, 1.0f}, {0, Distortion::Mix, 1.0f},
         {10000, Distortion::Drive, -5.0f}, {12000, Distortion::Tone, 9.0f}, {15000, Distortion::Type, 99.0f}, {16000, Distortion::Mix, 1e9f},
         {18000, Distortion::Drive, std::nanf("")}, {18001, Distortion::Tone, std::nanf("")}, {18002, Distortion::Type, std::nanf("")}, {18003, Distortion::Mix, std::nanf("")},
         {18004, Distortion::Active, std::nanf("")}});
    CHECK(normal_finite(ol));
    for (float s : ol) {
      REQUIRE(std::fabs(s) < 1.0e5f);  // the Pade tanh keeps a slope of 1/9 for huge arguments: 1000x full scale at a gain of 46 is 5000
    }
    // One NaN in the input.
    auto nan_in = saw(110.0, 0.5f, 6000);
    nan_in[1000] = std::nanf("");
    Distortion d2;
    run(d2, nan_in, nan_in, ol, orr, {{0, Distortion::Type, static_cast<float>(m)}});
    for (std::size_t i = 1000 + 2 * o3_303::Distortion::kControlPeriod + 64; i < ol.size(); ++i) {
      REQUIRE(std::isfinite(ol[i]));
    }
  }
}
