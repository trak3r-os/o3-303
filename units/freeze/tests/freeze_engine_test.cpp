// SPDX-License-Identifier: GPL-3.0-or-later
// FREEZE as master insert 0 of a dsp::Engine (the app's wiring): off it changes nothing, bit for bit; turned on from the main loop
// (Engine::set_param, the way the app's FREEZE action will) it loops the master at the transport's tempo, and turned off the output is
// the unfrozen engine's again.
#include <doctest/doctest.h>

#include "support/need.h"

#include <o3_303/units/freeze/freeze.h>
#include <o3sdk/dsp/engine.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

namespace dsp = o3sdk::dsp;
using o3_303::Freeze;

// An instrument that plays a fixed stereo signal whatever the events: the same in every engine.
class Tone final : public dsp::Unit {
 public:
  const dsp::UnitInfo& info() const noexcept override {
    static constexpr dsp::UnitInfo kInfo{"TONE", dsp::UnitKind::Instrument, nullptr, 0, nullptr, 0};
    return kInfo;
  }
  void prepare(float) noexcept override { n_ = 0; }
  void process(const dsp::ProcessArgs& args) noexcept override {
    for (std::uint32_t i = 0; i < args.frames; ++i) {
      const float t = static_cast<float>(n_ % 441000u) / 44100.0f;
      args.out_l[i] = 0.30f * std::sin(6.2831853f * 261.63f * t) + 0.20f * std::sin(6.2831853f * 777.0f * t + 0.3f);
      args.out_r[i] = 0.30f * std::sin(6.2831853f * 329.63f * t + 0.7f) + 0.20f * std::sin(6.2831853f * 913.0f * t);
      ++n_;
    }
  }

 private:
  std::uint32_t n_ = 0;
};

constexpr std::uint32_t kCapacity = 131072;  // the app's ring

struct Rig {
  std::vector<float> ring_l = std::vector<float>(kCapacity);
  std::vector<float> ring_r = std::vector<float>(kCapacity);
  Tone tone;
  Freeze freeze{ring_l.data(), ring_r.data(), kCapacity};
  dsp::Engine engine;
  dsp::UnitId freeze_id = dsp::kNoUnit;

  explicit Rig(bool with_freeze) {
    const dsp::UnitId t = engine.attach(tone);
    (void)engine.set_source(0, t);
    if (with_freeze) {
      freeze_id = engine.attach(freeze);
      (void)engine.set_master_insert(0, freeze_id);
    }
    (void)engine.set_tempo(120'000);
    engine.start();
  }

  // `blocks` blocks of 64 frames appended to the output (left, right interleaved apart: two vectors).
  void render(std::uint32_t blocks, std::vector<float>& l, std::vector<float>& r) {
    float bl[dsp::kMaxBlockFrames];
    float br[dsp::kMaxBlockFrames];
    for (std::uint32_t b = 0; b < blocks; ++b) {
      engine.render(nullptr, nullptr, bl, br, dsp::kMaxBlockFrames);
      l.insert(l.end(), bl, bl + dsp::kMaxBlockFrames);
      r.insert(r.end(), br, br + dsp::kMaxBlockFrames);
    }
  }
};

double max_diff(const std::vector<float>& a, const std::vector<float>& b, std::size_t from, std::size_t to) {
  double m = 0.0;
  for (std::size_t i = from; i < to && i < a.size() && i < b.size(); ++i) {
    m = std::max(m, std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i])));
  }
  return m;
}

}  // namespace

TEST_CASE("freeze in the engine: master insert 0, off, is invisible: the output is the engine's without it, bit for bit") {
  Rig plain(false);
  Rig with(true);
  std::vector<float> pl, pr, wl, wr;
  plain.render(400, pl, pr);
  with.render(400, wl, wr);
  NEED(pl.size() == wl.size());
  CHECK(std::memcmp(pl.data(), wl.data(), pl.size() * sizeof(float)) == 0);
  CHECK(std::memcmp(pr.data(), wr.data(), pr.size() * sizeof(float)) == 0);
  CHECK_FALSE(with.freeze.frozen());
  CHECK(with.engine.unit_info(with.freeze_id)->param_count == Freeze::kParamCount);
}

TEST_CASE("freeze in the engine: on from the main loop it loops the master at the tempo, off it is the unfrozen output again") {
  Rig plain(false);
  Rig with(true);
  std::vector<float> pl, pr, wl, wr;
  const std::uint32_t kBlocksBefore = 300;  // 19,200 frames: more than a loop of recording
  plain.render(kBlocksBefore, pl, pr);
  with.render(kBlocksBefore, wl, wr);
  // 1/8 of a bar at 120 BPM: a quarter of a second, 11,025 frames.
  CHECK(with.engine.set_param(with.freeze_id, Freeze::Length, 1.0f));
  CHECK(with.engine.set_param(with.freeze_id, Freeze::Active, 1.0f));
  plain.render(600, pl, pr);
  with.render(600, wl, wr);
  const std::size_t n0 = kBlocksBefore * 64u;  // the command lands at frame 0 of the next block
  CHECK(with.freeze.frozen());
  CHECK(with.freeze.loop() == 11025u);
  // Periodic at the loop, on both sides, after the entry crossfade; and not the unfrozen output.
  const std::size_t from = n0 + 400;
  const std::size_t to = n0 + 2u * 11025u - 100u;
  NEED(to + 11025u < wl.size());
  std::vector<float> shifted_l(wl.begin() + 11025, wl.end());
  std::vector<float> shifted_r(wr.begin() + 11025, wr.end());
  CHECK(max_diff(wl, shifted_l, from, to) < 1e-6);
  CHECK(max_diff(wr, shifted_r, from, to) < 1e-6);
  CHECK(max_diff(wl, pl, from, to) > 0.05);
  // The loop is the last 11,025 frames before the entry: the same samples, after the master's own gain and clip.
  double back = 0.0;
  for (std::size_t k = 400; k < 3000; ++k) {
    back = std::max(back, std::fabs(static_cast<double>(wl[n0 + k]) - static_cast<double>(pl[n0 + k - 11025])));
  }
  CHECK(back < 1e-5);

  // Off again: after the exit crossfade the engine is the unfrozen one, bit for bit.
  CHECK(with.engine.set_param(with.freeze_id, Freeze::Active, 0.0f));
  plain.render(200, pl, pr);
  with.render(200, wl, wr);
  CHECK_FALSE(with.freeze.frozen());
  const std::size_t exit_at = (kBlocksBefore + 600u) * 64u;
  NEED(wl.size() == pl.size());
  CHECK(std::memcmp(wl.data() + exit_at + 300, pl.data() + exit_at + 300, (wl.size() - exit_at - 300) * sizeof(float)) == 0);
  CHECK(std::memcmp(wr.data() + exit_at + 300, pr.data() + exit_at + 300, (wr.size() - exit_at - 300) * sizeof(float)) == 0);
}

TEST_CASE("freeze in the engine: the loop is taken at the tempo of the entry and stays when the tempo moves") {
  Rig with(true);
  std::vector<float> wl, wr;
  with.render(300, wl, wr);
  CHECK(with.engine.set_param(with.freeze_id, Freeze::Length, 2.0f));  // 1/16 of a bar
  CHECK(with.engine.set_tempo(150'000));                              // 4,410 frames
  CHECK(with.engine.set_param(with.freeze_id, Freeze::Active, 1.0f));
  with.render(100, wl, wr);
  CHECK(with.freeze.loop() == 4410u);
  CHECK(with.engine.set_tempo(90'000));
  with.render(100, wl, wr);
  CHECK(with.freeze.loop() == 4410u);  // latched
  CHECK(with.engine.set_param(with.freeze_id, Freeze::Active, 0.0f));
  with.render(20, wl, wr);
  CHECK(with.engine.set_param(with.freeze_id, Freeze::Active, 1.0f));
  with.render(20, wl, wr);
  CHECK(with.freeze.loop() == 7350u);  // 1/16 of a bar at 90 BPM
}
