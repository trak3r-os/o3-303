// SPDX-License-Identifier: GPL-3.0-or-later
// The sequencer's punches (H2, docs/PADS-TRACT-PLAN.md): set_punch() holds a unit's parameter at a value, at frame 0 of every block, over a p-lock,
// a slew, the wobble and the main loop's own write; clear_punch() brings back where the target would be without it; nothing is recorded and
// the engine's stored value is not touched. Recording stand-ins for the units see every event, the real descriptors of the voice are used.
#include <doctest/doctest.h>

#include <o3_303/seq/bank.h>
#include <o3_303/seq/ops.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

using namespace o3_303;
using namespace o3_303::seq;
namespace dsp = o3sdk::dsp;
namespace core = o3sdk::core;

namespace {

using core::ParamCurve;
using core::ParamDesc;
using core::ParamUnit;

const ParamDesc* const kVoiceParams = [] {
  static AcidVoice voice;
  return voice.info().params;
}();
const ParamDesc* const kGrowlParams = [] {
  static Growl growl;
  return growl.info().params;
}();

const char* const kTypeNames[] = {"A", "B", "C"};
const ParamDesc kDistParams[3] = {
    {"DRIVE", "", nullptr, nullptr, 0.0f, 1.0f, 0.5f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"TONE", "Hz", nullptr, nullptr, 100.0f, 8000.0f, 1000.0f, 100, 10, 0, ParamUnit::Hz, ParamCurve::Log, 0, 0},
    {"TYPE", "", kTypeNames, nullptr, 0.0f, 2.0f, 0.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
};
const ParamDesc kDelayParams[3] = {
    {"TIME", "ms", nullptr, nullptr, 10.0f, 2000.0f, 250.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Log, 0, 0},
    {"FEEDBACK", "%", nullptr, nullptr, 0.0f, 0.95f, 0.4f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
    {"MIX", "%", nullptr, nullptr, 0.0f, 1.0f, 0.3f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, 0, 0},
};

struct Rec {
  std::uint64_t frame;
  dsp::EventKind kind;
  std::uint8_t note;
  std::uint16_t param;
  float value;
};

class Probe final : public dsp::Unit {
 public:
  Probe(dsp::UnitKind kind, const ParamDesc* params, std::uint16_t count) : info_{"PROBE", kind, params, count, nullptr, 0} {}
  const dsp::UnitInfo& info() const noexcept override { return info_; }
  void prepare(float) noexcept override {}
  void process(const dsp::ProcessArgs& a) noexcept override {
    if (a.frames == 0u) {
      return;
    }
    for (std::uint32_t i = 0; i < a.event_count; ++i) {
      const dsp::Event& e = a.events[i];
      log.push_back(Rec{at + e.frame, e.kind, e.note, e.param, e.value});
    }
    for (std::uint32_t i = 0; i < a.frames; ++i) {
      if (info_.kind == dsp::UnitKind::Instrument) {
        a.out_l[i] = 0.5f;
        a.out_r[i] = 0.5f;
      } else if (a.out_l != a.in_l) {
        a.out_l[i] = a.in_l[i];
        a.out_r[i] = a.in_r[i];
      }
    }
    at += a.frames;
  }
  std::vector<Rec> log;
  std::uint64_t at = 0;

 private:
  dsp::UnitInfo info_;
};

class MixSpy final : public dsp::Clocked {
 public:
  void block(dsp::BlockContext& ctx) noexcept override { level.push_back(ctx.mix(0, dsp::MixField::Level)); }
  std::vector<float> level;
};

struct Rig {
  dsp::Engine engine;
  Probe voice{dsp::UnitKind::Instrument, kVoiceParams, AcidVoice::kParamCount};
  Probe dist{dsp::UnitKind::Effect, kDistParams, 3};
  Probe delay{dsp::UnitKind::Effect, kDelayParams, 3};
  Probe growl{dsp::UnitKind::Effect, kGrowlParams, Growl::kParamCount};
  MixSpy spy;
  std::unique_ptr<Bank> bank = std::make_unique<Bank>();
  Sequencer seq;
  dsp::UnitId vid = dsp::kNoUnit, did = dsp::kNoUnit, lid = dsp::kNoUnit, gid = dsp::kNoUnit;

  Rig() : seq(init(*bank)) {}
  static Bank& init(Bank& b) {
    b.init();
    return b;
  }

  // The growl is given to the sequencer only when `with_growl`. The wobble is set between this and the first run().
  void start(std::uint32_t tempo_mbpm = 120'000, bool with_growl = false) {
    vid = engine.attach(voice);
    did = engine.attach(dist);
    lid = engine.attach(delay);
    gid = engine.attach(growl);
    REQUIRE(engine.set_source(0, vid));
    REQUIRE(engine.set_insert(0, 0, did));
    REQUIRE(engine.set_insert(0, 1, gid));
    REQUIRE(engine.set_send_effect(0, lid));
    REQUIRE(seq.setup(engine, vid, did, lid));
    if (with_growl) {
      REQUIRE(seq.set_unit(kLaneGrowl, gid));
    }
    REQUIRE(engine.add_hook(spy));
    engine.start();
    engine.set_tempo(tempo_mbpm);
    tempo = tempo_mbpm;
  }

  void run(std::uint64_t frames, unsigned block = 64) {
    float l[dsp::kMaxBlockFrames];
    float r[dsp::kMaxBlockFrames];
    while (frames > 0) {
      const auto n = static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, block));
      engine.render(nullptr, nullptr, l, r, n);
      frames -= n;
    }
  }
  std::uint64_t now() const { return voice.at; }
  std::uint32_t tempo = 120'000;
};


constexpr float kCut = 1000.0f;

Pattern play_all(unsigned length) {
  Pattern p;
  clear(p);
  for (unsigned j = 0; j < kMaxSteps; ++j) {
    p.steps[j].flags = 0;
    p.steps[j].note = static_cast<std::uint8_t>(40 + j % 40u);
  }
  p.length = static_cast<std::uint8_t>(length);
  return p;
}

std::vector<Rec> events_of(const std::vector<Rec>& log, std::uint16_t id, std::size_t from = 0) {
  std::vector<Rec> out;
  for (std::size_t i = from; i < log.size(); ++i) {
    if (log[i].kind == dsp::EventKind::Param && log[i].param == id) {
      out.push_back(log[i]);
    }
  }
  return out;
}

// The value of the last Param event of `id` in each block of 64 frames (what the unit ends up with), from `from` on.
std::vector<float> block_ends(const std::vector<Rec>& log, std::uint16_t id, std::size_t from) {
  std::vector<float> out;
  std::uint64_t cur = ~std::uint64_t{0};
  for (const Rec& e : events_of(log, id, from)) {
    if (e.frame / 64u != cur) {
      out.push_back(e.value);
      cur = e.frame / 64u;
    } else {
      out.back() = e.value;
    }
  }
  return out;
}

float expect(float v) { return core::param_clamp(kVoiceParams[AcidVoice::Cutoff], v); }

}  // namespace

TEST_CASE("punch: a held target is written at frame 0 of every block, and the sequencer says so") {
  Rig rig;
  rig.start();
  CHECK(rig.seq.punches() == 0u);
  REQUIRE(rig.seq.set_punch(2, kLaneVoice, AcidVoice::Cutoff, kCut));
  rig.run(64 * 8);
  CHECK(rig.seq.punches() == 4u);
  const std::vector<Rec> ev = events_of(rig.voice.log, AcidVoice::Cutoff);
  REQUIRE(ev.size() >= 8u);
  for (const Rec& e : ev) {
    CHECK(e.value == expect(kCut));
    CHECK(e.frame % 64u == 0u);
  }
  CHECK(rig.seq.clear_punch(2));
  rig.run(64);
  CHECK(rig.seq.punches() == 0u);
}

TEST_CASE("punch: refused targets, slots and ids; a value is clamped to the range") {
  Rig rig;
  rig.start();
  CHECK_FALSE(rig.seq.set_punch(kPunchSlots, kLaneVoice, AcidVoice::Cutoff, 1.0f));
  CHECK_FALSE(rig.seq.set_punch(0, kLaneMixer, 0, 1.0f));
  CHECK_FALSE(rig.seq.set_punch(0, kLaneSeq, kSeqWobbleRate, 1.0f));
  CHECK_FALSE(rig.seq.set_punch(0, kLaneVoice, AcidVoice::Stock, 1.0f));
  CHECK_FALSE(rig.seq.set_punch(0, kLaneVoice, 200, 1.0f));
  CHECK_FALSE(rig.seq.set_punch(0, kLaneGrowl, 0, 1.0f));  // a unit that was not given to setup()
  CHECK_FALSE(rig.seq.clear_punch(kPunchSlots));
  REQUIRE(rig.seq.set_punch(0, kLaneVoice, AcidVoice::Cutoff, 1.0e9f));
  rig.run(64 * 2);
  const std::vector<Rec> ev = events_of(rig.voice.log, AcidVoice::Cutoff);
  REQUIRE_FALSE(ev.empty());
  CHECK(ev.back().value == expect(1.0e9f));
  CHECK(ev.back().value < 1.0e9f);
}

TEST_CASE("punch: it wins over the main loop's write and the knob comes back on release, and the engine never stores it") {
  Rig rig;
  rig.start();
  const float knob = core::param_denorm(kVoiceParams[AcidVoice::Cutoff], 0.3f);
  REQUIRE(rig.engine.set_param(rig.vid, AcidVoice::Cutoff, knob));
  rig.seq.update();
  rig.run(64);
  REQUIRE(rig.seq.set_punch(2, kLaneVoice, AcidVoice::Cutoff, kCut));
  rig.run(64 * 2);
  const float moved = core::param_denorm(kVoiceParams[AcidVoice::Cutoff], 0.6f);
  REQUIRE(rig.engine.set_param(rig.vid, AcidVoice::Cutoff, moved));  // the knob is turned while the pad is down
  rig.seq.update();
  std::size_t mark = rig.voice.log.size();
  rig.run(64 * 3);
  for (const float v : block_ends(rig.voice.log, AcidVoice::Cutoff, mark)) {
    CHECK(v == expect(kCut));
  }
  CHECK(rig.engine.param(rig.vid, AcidVoice::Cutoff) == doctest::Approx(moved));
  REQUIRE(rig.seq.clear_punch(2));
  mark = rig.voice.log.size();
  rig.run(64 * 2);
  const std::vector<float> after = block_ends(rig.voice.log, AcidVoice::Cutoff, mark);
  REQUIRE_FALSE(after.empty());
  CHECK(after.front() == doctest::Approx(moved));  // the knob's value as it stands now, not the one before the punch
}

TEST_CASE("punch: it wins over a slew, which goes on and ends where it was going") {
  Rig rig;
  rig.start();
  const float goal = core::param_denorm(kVoiceParams[AcidVoice::Cutoff], 0.9f);
  REQUIRE(rig.seq.slew(kLaneVoice, AcidVoice::Cutoff, goal, 100.0f));
  rig.run(64);
  REQUIRE(rig.seq.set_punch(2, kLaneVoice, AcidVoice::Cutoff, kCut));
  std::size_t mark = rig.voice.log.size();
  rig.run(64 * 12);  // longer than the slew's 100 ms (4,800 frames) is not reached: it is held in the middle
  for (const float v : block_ends(rig.voice.log, AcidVoice::Cutoff, mark)) {
    CHECK(v == expect(kCut));
  }
  REQUIRE(rig.seq.clear_punch(2));
  mark = rig.voice.log.size();
  rig.run(64 * 100);  // the slew ends (and says its goal) after the punch is gone
  const std::vector<float> after = block_ends(rig.voice.log, AcidVoice::Cutoff, mark);
  REQUIRE_FALSE(after.empty());
  CHECK(after.back() == doctest::Approx(goal).epsilon(1e-4));
  CHECK(after.front() != expect(kCut));
}

TEST_CASE("punch: it wins over the wobble, which starts again around its base") {
  Rig rig;
  rig.start();
  REQUIRE(rig.seq.set_wobble(true, kWobble1_16, kWobbleSine, 0.4f));  // the voice's cutoff
  rig.engine.play();
  rig.run(64 * 20);
  const std::size_t quiet = rig.voice.log.size();
  const std::vector<float> moving = block_ends(rig.voice.log, AcidVoice::Cutoff, 0);
  REQUIRE(moving.size() > 4u);
  CHECK(*std::max_element(moving.begin(), moving.end()) != *std::min_element(moving.begin(), moving.end()));
  REQUIRE(rig.seq.set_punch(2, kLaneVoice, AcidVoice::Cutoff, kCut));
  rig.run(64);  // the block the punch lands in
  const std::size_t mark = rig.voice.log.size();
  rig.run(64 * 40);
  for (const Rec& e : events_of(rig.voice.log, AcidVoice::Cutoff, mark)) {
    CHECK(e.value == expect(kCut));
  }
  REQUIRE(rig.seq.clear_punch(2));
  const std::size_t after = rig.voice.log.size();
  rig.run(64 * 40);
  const std::vector<float> again = block_ends(rig.voice.log, AcidVoice::Cutoff, after);
  REQUIRE(again.size() > 4u);
  CHECK(*std::max_element(again.begin(), again.end()) != *std::min_element(again.begin(), again.end()));
  CHECK(quiet > 0u);
}

TEST_CASE("punch: it wins over a p-lock, and the lock's value is back when the pad lets go inside the step") {
  Rig rig;
  rig.start();
  Pattern p = play_all(8);
  for (unsigned s = 0; s < 8; ++s) {
    for (unsigned q = 0; q < 4; ++q) {
      set_motion_point(p, 0, s, q, 203);
    }
  }
  rig.bank->replace(0, p);
  rig.engine.play();
  rig.run(64 * 40);
  const std::vector<Rec> locked = events_of(rig.voice.log, AcidVoice::Cutoff);
  REQUIRE_FALSE(locked.empty());
  const float lock_value = locked.back().value;
  CHECK(lock_value != expect(kCut));
  REQUIRE(rig.seq.set_punch(2, kLaneVoice, AcidVoice::Cutoff, kCut));
  rig.run(64);
  std::size_t mark = rig.voice.log.size();
  rig.run(64 * 40);  // across a step boundary: a step's lock would write at its first frame
  for (const Rec& e : events_of(rig.voice.log, AcidVoice::Cutoff, mark)) {
    CHECK(e.value == expect(kCut));
  }
  REQUIRE(rig.seq.clear_punch(2));
  mark = rig.voice.log.size();
  rig.run(64 * 2);
  const std::vector<float> back = block_ends(rig.voice.log, AcidVoice::Cutoff, mark);
  REQUIRE_FALSE(back.empty());
  CHECK(back.front() == doctest::Approx(lock_value).epsilon(1e-4));
}

TEST_CASE("punch: a slot given another target lets go of the first, and two slots hold two targets") {
  Rig rig;
  rig.start();
  const float knob = core::param_denorm(kVoiceParams[AcidVoice::Cutoff], 0.4f);
  REQUIRE(rig.engine.set_param(rig.vid, AcidVoice::Cutoff, knob));
  rig.seq.update();
  rig.run(64);
  REQUIRE(rig.seq.set_punch(2, kLaneVoice, AcidVoice::Cutoff, kCut));
  REQUIRE(rig.seq.set_punch(3, kLaneVoice, AcidVoice::Reso, 0.8f));
  rig.run(64 * 3);
  CHECK(rig.seq.punches() == 12u);
  REQUIRE_FALSE(events_of(rig.voice.log, AcidVoice::Reso).empty());
  // Slot 2 moves to the envelope: the cutoff is the knob's again.
  REQUIRE(rig.seq.set_punch(2, kLaneVoice, AcidVoice::EnvMod, 0.9f));
  const std::size_t mark = rig.voice.log.size();
  rig.run(64 * 2);
  const std::vector<float> cut = block_ends(rig.voice.log, AcidVoice::Cutoff, mark);
  REQUIRE_FALSE(cut.empty());
  CHECK(cut.front() == doctest::Approx(knob));
  CHECK_FALSE(events_of(rig.voice.log, AcidVoice::EnvMod, mark).empty());
  REQUIRE(rig.seq.clear_punch(2));
  REQUIRE(rig.seq.clear_punch(3));
  rig.run(64);
  CHECK(rig.seq.punches() == 0u);
}

TEST_CASE("punch: the distortion's two parameters are punched on the distortion unit") {
  Rig rig;
  rig.start();
  REQUIRE(rig.seq.set_punch(0, kLaneDist, 0, 1.0f));
  REQUIRE(rig.seq.set_punch(1, kLaneDist, 1, 4000.0f));
  rig.run(64 * 3);
  const std::vector<Rec> a = events_of(rig.dist.log, 0);
  const std::vector<Rec> b = events_of(rig.dist.log, 1);
  REQUIRE_FALSE(a.empty());
  REQUIRE_FALSE(b.empty());
  CHECK(a.back().value == 1.0f);
  CHECK(b.back().value == 4000.0f);
  CHECK(events_of(rig.voice.log, AcidVoice::Cutoff).empty());
}
