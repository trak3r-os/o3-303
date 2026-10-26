// SPDX-License-Identifier: GPL-3.0-or-later
// The sequencer's M3c features (docs/M3C-PLAN.md): the wobble (W) and the growl's note (G). Recording stand-ins for the voice, the distortion, the
// delay and the growl see every event with its absolute frame; the real descriptors of the voice and the growl are used. The wobble's values
// are checked against a model written here in doubles (the phase from the transport's ticks, the shapes spelled out).
#include <doctest/doctest.h>

#include <o3_303/seq/bank.h>
#include <o3_303/seq/ops.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/tb303/tb303.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace o3_303;
using namespace o3_303::seq;
namespace dsp = o3sdk::dsp;
namespace core = o3sdk::core;

namespace {

using core::ParamCurve;
using core::ParamDesc;
using core::ParamUnit;

constexpr double kPi = 3.14159265358979323846;

const ParamDesc* const kVoiceParams = [] {
  static Tb303 voice;
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
  Probe voice{dsp::UnitKind::Instrument, kVoiceParams, Tb303::kParamCount};
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

std::vector<Rec> params_of(const std::vector<Rec>& log, int id) {
  std::vector<Rec> out;
  for (const Rec& e : log) {
    if (e.kind == dsp::EventKind::Param && e.param == id) {
      out.push_back(e);
    }
  }
  return out;
}
std::vector<Rec> notes_on(const std::vector<Rec>& log) {
  std::vector<Rec> out;
  for (const Rec& e : log) {
    if (e.kind == dsp::EventKind::NoteOn) {
      out.push_back(e);
    }
  }
  return out;
}

// The Param events of the voice's cutoff; where two share a frame (the knob's own write, queued by set_param() for frame 0 of the first block,
// and the wobble's, which follows it in the list and wins), only the later one.
std::vector<Rec> wob_events(const std::vector<Rec>& log, int id = 0) {
  std::vector<Rec> out;
  for (const Rec& e : params_of(log, id)) {
    if (!out.empty() && out.back().frame == e.frame) {
      out.back() = e;
    } else {
      out.push_back(e);
    }
  }
  return out;
}

double cutoff_norm(float v) { return static_cast<double>(core::param_norm(kVoiceParams[0], v)); }

std::uint64_t frame_of(std::uint64_t tick, std::uint32_t tempo_mbpm) {
  const std::uint64_t inc = dsp::ticks_per_sample_q32(tempo_mbpm);
  return ((tick << 32) + inc - 1u) / inc;
}

// The transport's tick at an absolute frame, in doubles.
double ticks_at(std::uint64_t frame, std::uint32_t tempo_mbpm) {
  return static_cast<double>(frame) * static_cast<double>(dsp::ticks_per_sample_q32(tempo_mbpm)) / 4294967296.0;
}

// The shapes, spelled out; u in [0, 1).
double wave(unsigned shape, double u) {
  switch (shape) {
    case kWobbleSine:
      return std::sin(2.0 * kPi * u);
    case kWobbleTriangle:
      return u < 0.25 ? 4.0 * u : (u < 0.75 ? 2.0 - 4.0 * u : 4.0 * u - 4.0);
    case kWobbleSawDown:
      return 1.0 - 2.0 * u;
    case kWobbleSawUp:
      return 2.0 * u - 1.0;
    default:
      return u < 0.5 ? 1.0 : -1.0;
  }
}

Pattern play_all(unsigned length = 16) {
  Pattern p;
  clear(p);
  for (unsigned j = 0; j < kMaxSteps; ++j) {
    p.steps[j].flags = 0;
    p.steps[j].note = static_cast<std::uint8_t>(40 + j % 40u);
  }
  p.length = static_cast<std::uint8_t>(length);
  return p;
}

constexpr std::uint32_t kStep = dsp::kTicksPerSixteenth;  // 240 ticks

// The cutoff knob at 0.5 (a Table curve: the normalised position is another matter), handed to the hook.
void set_knob(Rig& rig, float norm) {
  const float v = core::param_denorm(kVoiceParams[0], norm);
  REQUIRE(rig.engine.set_param(rig.vid, 0, v));
  rig.seq.update();
}

}  // namespace

TEST_CASE("wobble: the tables are the brief's: eleven rates, six shapes, ticks of the note value, a motion lane over kLaneSeq") {
  static_assert(kWobbleRateCount == 11 && kWobbleShapeCount == 6);
  const double whole = 3840.0;
  const double want[kWobbleRateCount] = {1.0, 1.0 / 2, 1.0 / 4, 1.0 / 4 * 2 / 3, 1.0 / 8, 1.0 / 8 * 2 / 3, 1.0 / 16, 1.0 / 16 * 2 / 3, 1.0 / 32, 1.0 / 8 * 1.5, 1.0 / 16 * 1.5};
  for (unsigned r = 0; r < kWobbleRateCount; ++r) {
    CAPTURE(r);
    CHECK(static_cast<double>(kWobbleTicks[r]) == doctest::Approx(whole * want[r]));
  }
  CHECK(static_cast<unsigned>(kWobbleTicks[kWobble1_4]) == dsp::kPpqn);
  CHECK(kGrowlNoteParam == Growl::Note);
  CHECK(std::string(kWobbleRateNames[kWobble1_16D]) == "1/16D");
  // The shapes at four phases, against the spelled-out ones.
  for (unsigned shape = 0; shape < kWobbleSquare + 1u; ++shape) {
    for (const double u : {0.0, 0.1, 0.3, 0.6, 0.9}) {
      CHECK(static_cast<double>(wobble_wave(shape, static_cast<std::uint32_t>(u * 4294967296.0), 0.0f)) == doctest::Approx(wave(shape, u)).epsilon(1e-5).scale(1.0));
    }
  }
  CHECK(wobble_wave(kWobbleSampleHold, 12345u, 0.37f) == 0.37f);
  // The lane over kLaneSeq has its two descriptors, and a Pattern stays what it was.
  Rig rig;
  rig.start();
  Pattern p = play_all();
  p.lanes[0] = LaneRef{kLaneSeq, kSeqWobbleRate};
  p.lanes[1] = LaneRef{kLaneSeq, kSeqWobbleDepth};
  rig.bank->replace(0, p);
  CHECK(rig.seq.slew(kLaneSeq, kSeqWobbleRate, 3.0f, 10.0f) == false);  // not a slew target
  CHECK_FALSE(rig.seq.set_wobble(true, 0, 0, 0.5f, LaneRef{kLaneSeq, kSeqWobbleRate}));  // nor a wobble target
}

TEST_CASE("wobble: the rate and the phase are the transport's, at several tempos, rates and shapes (swing off, no reset)") {
  struct Case {
    std::uint32_t tempo;
    unsigned rate;
    unsigned shape;
  };
  const Case cases[] = {{120'000, kWobble1_4, kWobbleSine},   {120'000, kWobble1_8, kWobbleTriangle}, {90'000, kWobble1_8T, kWobbleSine},
                        {174'000, kWobble1_16, kWobbleSine},  {140'000, kWobble1_16D, kWobbleTriangle}, {75'000, kWobble1_1, kWobbleSine},
                        {200'000, kWobble1_32, kWobbleSine},  {120'000, kWobble1_4, kWobbleSawDown},    {120'000, kWobble1_4, kWobbleSawUp},
                        {120'000, kWobble1_4, kWobbleSquare}, {299'000, kWobble1_16T, kWobbleTriangle}, {20'000, kWobble1_2, kWobbleSine}};
  for (const Case& c : cases) {
    CAPTURE(c.tempo);
    CAPTURE(c.rate);
    CAPTURE(c.shape);
    Rig rig;
    rig.start(c.tempo);
    set_knob(rig, 0.5f);
    const double base = cutoff_norm(rig.engine.param(rig.vid, 0));
    CHECK(rig.seq.set_wobble(true, c.rate, c.shape, 0.6f));
    rig.seq.set_wobble_reset(false);
    rig.engine.play();
    const std::uint64_t frames = std::min<std::uint64_t>(frame_of(2u * 3840u, c.tempo), 300000u);
    rig.run(frames);
    const std::vector<Rec> ev = wob_events(rig.voice.log);
    // Four events a block; a square holds its value between flips, and an unchanged value is not repeated (frame 0 of each block always speaks).
    const bool flat = c.shape == kWobbleSquare;
    REQUIRE(ev.size() >= (flat ? frames / 64u : frames / 64u * 4u - 8u));
    unsigned checked = 0;
    for (const Rec& e : ev) {
      const double u = std::fmod(ticks_at(e.frame, c.tempo) / kWobbleTicks[c.rate], 1.0);
      // The jumps of the saws and the square: a tick landing on one is a coin toss between the two sides.
      if (u < 1e-4 || u > 1.0 - 1e-4 || std::fabs(u - 0.5) < 1e-4) {
        continue;
      }
      const double want = std::clamp(base + 0.5 * 0.6 * wave(c.shape, u), 0.0, 1.0);
      CHECK(cutoff_norm(e.value) == doctest::Approx(want).epsilon(1e-4).scale(1.0));
      ++checked;
    }
    CHECK(checked + 40 >= ev.size());
    // 16 frames apart.
    for (std::size_t i = 1; i < ev.size() && i < 64 && !flat; ++i) {
      CHECK(ev[i].frame - ev[i - 1].frame == 16u);
    }
  }
}

TEST_CASE("wobble: the phase restarts with each note by default, at the note's frame, ratchet hits and slides included; off, it does not") {
  for (const bool reset : {true, false}) {
    CAPTURE(reset);
    Rig rig;
    rig.start();
    set_knob(rig, 0.5f);
    const double base = cutoff_norm(rig.engine.param(rig.vid, 0));
    Pattern p = play_all(8);
    p.steps[2].flags = with_ratchet(0, 3);  // four hits
    p.steps[4].flags = kStepSlide;
    rig.bank->replace(0, p);
    REQUIRE(rig.seq.set_wobble(true, kWobble1_1, kWobbleSawDown, 1.0f));  // a slow saw that starts at +1: after a reset it is at its top
    rig.seq.set_wobble_reset(reset);
    rig.engine.play();
    rig.run(frame_of(8 * kStep, rig.tempo));
    const std::vector<Rec> notes = notes_on(rig.voice.log);
    const std::vector<Rec> ev = wob_events(rig.voice.log);
    REQUIRE(notes.size() == 8u + 3u);
    unsigned reset_events = 0;
    for (const Rec& n : notes) {
      const auto at = std::find_if(ev.begin(), ev.end(), [&](const Rec& e) { return e.frame == n.frame; });
      if (reset) {
        REQUIRE(at != ev.end());  // one event at the note's own frame, even where it is no multiple of 16
        // Phase 0 of a saw down is +1: the top of the swing (base + depth / 2), clamped to the range.
        CHECK(cutoff_norm(at->value) == doctest::Approx(std::min(1.0, base + 0.5)).epsilon(1e-4).scale(1.0));
        ++reset_events;
        // and it goes on from there: 16 frames later the phase is 16 frames, not the transport's.
        const auto next = std::find_if(ev.begin(), ev.end(), [&](const Rec& e) { return e.frame == n.frame + 16u; });
        if (next != ev.end() && (n.frame % 16u) == 0u) {
          const double u = 16.0 * static_cast<double>(dsp::ticks_per_sample_q32(rig.tempo)) / 4294967296.0 / 3840.0;
          CHECK(cutoff_norm(next->value) == doctest::Approx(std::clamp(base + 0.5 * (1.0 - 2.0 * u), 0.0, 1.0)).epsilon(1e-4).scale(1.0));
        }
      }
    }
    if (!reset) {
      // Free: the saw falls through the whole run, so no event but the transport's first is at its top.
      CHECK(cutoff_norm(ev.back().value) < cutoff_norm(ev.front().value));
    } else {
      CHECK(reset_events == notes.size());
    }
  }
}

TEST_CASE("wobble: the target is any lane target: the voice, the distortion, the delay, the mixer and the growl, and the others stay quiet") {
  struct Case {
    LaneRef target;
    int unit;  // 0 voice, 1 dist, 2 delay, 3 growl
    std::uint16_t id;
  };
  const Case cases[] = {{{kLaneVoice, 3}, 0, 3}, {{kLaneDist, 1}, 1, 1}, {{kLaneDelay, 2}, 2, 2}, {{kLaneGrowl, Growl::Vowel}, 3, Growl::Vowel}};
  for (const Case& c : cases) {
    CAPTURE(static_cast<int>(c.target.target));
    Rig rig;
    rig.start(120'000, true);
    if (c.unit == 3) {
      REQUIRE(rig.engine.set_param(rig.gid, Growl::Vowel, 0.5f));  // mid-range: the swing is not clamped away
      rig.seq.update();
    }
    CHECK(rig.seq.set_wobble(true, kWobble1_8, kWobbleSine, 0.5f, c.target));
    rig.engine.play();
    rig.run(20000);
    Probe* probes[4] = {&rig.voice, &rig.dist, &rig.delay, &rig.growl};
    for (int u = 0; u < 4; ++u) {
      std::size_t n = 0;
      for (const Rec& e : probes[u]->log) {
        n += (e.kind == dsp::EventKind::Param) ? 1u : 0u;
      }
      if (u == c.unit) {
        CHECK(n >= 20000u / 64u * 4u - 8u);
        for (const Rec& e : probes[u]->log) {
          CHECK(e.param == c.id);
        }
      } else {
        CHECK(n == 0u);
      }
    }
  }
  // A mixer field: the level breathes around the knob's 1.0 (clamped at the engine's 1.5), one write a block.
  {
    Rig rig;
    rig.start();
    CHECK(rig.seq.set_wobble(true, kWobble1_4, kWobbleTriangle, 0.4f, LaneRef{kLaneMixer, kMixerLevel}));
    rig.engine.play();
    rig.run(frame_of(960, rig.tempo));
    const auto [lo, hi] = std::minmax_element(rig.spy.level.begin(), rig.spy.level.end());
    CHECK(*lo < 0.8f);
    CHECK(*hi > 1.15f);
    CHECK(*hi <= 1.5f);
  }
  // What cannot be wobbled is refused and changes nothing: an Enum, the mute, the sequencer's own, an unknown id or unit, a bad rate or shape.
  {
    Rig rig;
    rig.start();
    CHECK_FALSE(rig.seq.set_wobble(true, kWobble1_4, kWobbleSine, 0.5f, LaneRef{kLaneDist, 2}));  // TYPE is an Enum
    CHECK_FALSE(rig.seq.set_wobble(true, kWobble1_4, kWobbleSine, 0.5f, LaneRef{kLaneMixer, kMixerMute}));
    CHECK_FALSE(rig.seq.set_wobble(true, kWobble1_4, kWobbleSine, 0.5f, LaneRef{kLaneSeq, 0}));
    CHECK_FALSE(rig.seq.set_wobble(true, kWobble1_4, kWobbleSine, 0.5f, LaneRef{kLaneGrowl, 0}));  // the growl was not given
    CHECK_FALSE(rig.seq.set_wobble(true, kWobble1_4, kWobbleSine, 0.5f, LaneRef{kLaneDist, 9}));
    CHECK_FALSE(rig.seq.set_wobble(true, kWobbleRateCount, kWobbleSine, 0.5f));
    CHECK_FALSE(rig.seq.set_wobble(true, kWobble1_4, kWobbleShapeCount, 0.5f));
    CHECK_FALSE(rig.seq.wobble().on);
    CHECK(rig.seq.set_wobble(true, kWobble1_32, kWobbleSquare, 2.0f));
    const Wobble w = rig.seq.wobble();
    CHECK(w.on);
    CHECK(w.rate == kWobble1_32);
    CHECK(w.shape == kWobbleSquare);
    CHECK(w.depth == 1.0f);  // clamped
    CHECK(w.target.target == kLaneVoice);
    CHECK(w.reset_on_note);
    CHECK(w.override_rate == kWobbleRateNone);
    CHECK(rig.seq.set_wobble(false, 0, 0, 0.0f));
    CHECK_FALSE(rig.seq.wobble().on);
  }
}

TEST_CASE("growl: with the unit given, the hook sets its Note at every note start: one event a note, the note as played") {
  Rig rig;
  rig.start(120'000, true);
  Pattern p = play_all(8);
  p.steps[1].flags = with_ratchet(0, 1);       // two hits
  p.steps[3].flags = kStepSlide;               // a glide into step 4
  p.steps[5].flags = kStepRest;                // no note
  p.steps[6].flags = kStepTie;                 // none either
  p.transpose = 5;
  rig.bank->replace(0, p);
  rig.seq.set_live_transpose(-2);
  rig.engine.play();
  rig.run(frame_of(8 * kStep, rig.tempo));
  const std::vector<Rec> notes = notes_on(rig.voice.log);
  const std::vector<Rec> got = params_of(rig.growl.log, Growl::Note);
  REQUIRE(notes.size() >= 7u);
  REQUIRE(got.size() == notes.size());
  for (std::size_t i = 0; i < notes.size(); ++i) {
    CAPTURE(i);
    CHECK(got[i].frame == notes[i].frame);
    CHECK(got[i].value == static_cast<float>(notes[i].note));
  }
  // Nothing else goes to the growl, and without set_unit() nothing does.
  CHECK(rig.growl.log.size() == got.size());
  Rig bare;
  bare.start();
  bare.bank->replace(0, p);
  bare.engine.play();
  bare.run(frame_of(8 * kStep, bare.tempo));
  CHECK(bare.growl.log.empty());
  // set_unit() is for the setup: not for a target that has no unit, and not once the engine runs.
  CHECK_FALSE(rig.seq.set_unit(kLaneGrowl, rig.gid));
  Rig other;
  CHECK_FALSE(other.seq.set_unit(kLaneGrowl, 0));  // before setup()
  other.start();
  CHECK_FALSE(other.seq.set_unit(kLaneVoice, other.did));
  CHECK_FALSE(other.seq.set_unit(kLaneMixer, other.did));
}

TEST_CASE("growl: a motion lane and a slew drive its parameters like any unit's, and its Note stays the hook's") {
  Rig rig;
  rig.start(120'000, true);
  Pattern p = play_all(8);
  p.lanes[0] = LaneRef{kLaneGrowl, Growl::Vowel};
  set_motion_point(p, 0, 2, 0, value_to_point(kGrowlParams[Growl::Vowel], 0.75f));
  rig.bank->replace(0, p);
  CHECK(rig.seq.slew(kLaneGrowl, Growl::CombFeedback, 0.9f, 20.0f));
  rig.engine.play();
  rig.run(frame_of(4 * kStep, rig.tempo));
  const auto vowel = params_of(rig.growl.log, Growl::Vowel);
  REQUIRE_FALSE(vowel.empty());
  CHECK(vowel.front().frame == frame_of(2 * kStep, rig.tempo));
  CHECK(vowel.front().value == doctest::Approx(0.75f).epsilon(0.005));
  const auto fb = params_of(rig.growl.log, Growl::CombFeedback);
  REQUIRE_FALSE(fb.empty());
  CHECK(fb.back().value == doctest::Approx(0.9f));
}

TEST_CASE("wobble: a motion lane on kLaneSeq sets the rate and the depth per step; the pad override beats it; both give way to set_wobble") {
  Rig rig;
  rig.start();
  Pattern p = play_all(8);
  p.lanes[0] = LaneRef{kLaneSeq, kSeqWobbleRate};
  p.lanes[1] = LaneRef{kLaneSeq, kSeqWobbleDepth};
  const auto point_of_rate = [](unsigned r) { return static_cast<std::uint8_t>(static_cast<double>(r) * 254.0 / 10.0 + 0.5); };
  for (unsigned j = 0; j < 8; ++j) {
    for (unsigned q = 0; q < 4; ++q) {
      set_motion_point(p, 0, j, q, j < 4 ? point_of_rate(kWobble1_16) : point_of_rate(kWobble1_32));
    }
  }
  for (unsigned q = 0; q < 4; ++q) {
    set_motion_point(p, 1, 6, q, 0);  // depth 0 for the whole of step 6
  }
  rig.bank->replace(0, p);
  REQUIRE(rig.seq.set_wobble(true, kWobble1_4, kWobbleSine, 1.0f));
  CHECK(rig.seq.wobble_rate_now() == -1);  // nothing has run
  rig.engine.play();
  std::map<unsigned, int> rate_at_step;
  for (unsigned step = 0; step < 8; ++step) {
    rig.run(frame_of((step + 1) * kStep, rig.tempo) - rig.now() - 64);  // up to the last block of the step
    rate_at_step[step] = rig.seq.wobble_rate_now();
    rig.run(64);
  }
  for (unsigned step = 0; step < 8; ++step) {
    CAPTURE(step);
    CHECK(rate_at_step[step] == (step < 4 ? static_cast<int>(kWobble1_16) : static_cast<int>(kWobble1_32)));
  }
  // The depth of step 6 is 0: the wobble is at its base there (one value), and swings elsewhere. Rates and depth are steps; nothing else jumps.
  const std::vector<Rec> ev = wob_events(rig.voice.log);
  const auto range = [&](unsigned from_step, unsigned to_step) {
    double lo = 2.0;
    double hi = -1.0;
    for (const Rec& e : ev) {
      if (e.frame >= frame_of(from_step * kStep, rig.tempo) + 64 && e.frame < frame_of(to_step * kStep, rig.tempo)) {
        lo = std::min(lo, cutoff_norm(e.value));
        hi = std::max(hi, cutoff_norm(e.value));
      }
    }
    return hi - lo;
  };
  CHECK(range(6, 7) < 1e-5);
  CHECK(range(4, 5) > 0.2);
  CHECK(range(7, 8) > 0.2);
  for (std::size_t i = 1; i < ev.size(); ++i) {
    const bool near_depth_step = ev[i].frame + 128 >= frame_of(6 * kStep, rig.tempo) && ev[i - 1].frame <= frame_of(7 * kStep, rig.tempo) + 128;
    if (!near_depth_step && ev[i].frame - ev[i - 1].frame == 16u) {
      CHECK(std::fabs(cutoff_norm(ev[i].value) - cutoff_norm(ev[i - 1].value)) < 0.06);
    }
  }
  // The override: while it holds, its rate runs whatever the lane says; released, the lane's rate is back.
  rig.seq.wobble_rate_override(kWobble1_8T);
  rig.run(256);
  CHECK(rig.seq.wobble_rate_now() == static_cast<int>(kWobble1_8T));
  CHECK(rig.seq.wobble().override_rate == static_cast<int>(kWobble1_8T));
  rig.seq.wobble_rate_override(kWobbleRateNone);
  rig.run(256);
  CHECK(rig.seq.wobble_rate_now() != static_cast<int>(kWobble1_8T));
  // Off, the readback goes quiet.
  rig.seq.set_wobble(false, 0, 0, 0.0f);
  rig.run(256);
  CHECK(rig.seq.wobble_rate_now() == -1);
}

TEST_CASE("wobble: the override changes the rate under the fingers: the period of the swing follows, from the same phase") {
  Rig rig;
  rig.start();
  set_knob(rig, 0.5f);
  REQUIRE(rig.seq.set_wobble(true, kWobble1_4, kWobbleSine, 0.6f));
  rig.seq.set_wobble_reset(false);
  rig.engine.play();
  const std::uint64_t quarter = frame_of(960, rig.tempo);  // 22,050 frames
  rig.run(quarter * 2u);
  const std::size_t before = rig.voice.log.size();
  rig.seq.wobble_rate_override(kWobble1_16);  // four times as fast
  rig.run(quarter * 2u);
  rig.seq.wobble_rate_override(kWobbleRateNone);
  rig.run(quarter * 2u);
  const std::vector<Rec> ev = wob_events(rig.voice.log);
  const double centre = cutoff_norm(rig.engine.param(rig.vid, 0));
  // Sign flips of the swing about its centre, with a dead band, inside a window of frames.
  const auto flips = [&](std::uint64_t from, std::uint64_t to) {
    int last = 0;
    unsigned n = 0;
    for (const Rec& e : ev) {
      if (e.frame >= from && e.frame < to) {
        const double d = cutoff_norm(e.value) - centre;
        const int sign = d > 1e-3 ? 1 : (d < -1e-3 ? -1 : 0);
        if (sign != 0) {
          n += (last != 0 && sign != last) ? 1u : 0u;
          last = sign;
        }
      }
    }
    return n;
  };
  (void)before;
  CHECK(flips(0, quarter * 2u) >= 2u);            // two cycles of 1/4: four half cycles, three flips
  CHECK(flips(0, quarter * 2u) <= 4u);
  CHECK(flips(quarter * 2u, quarter * 4u) >= 14u);  // eight cycles of 1/16: sixteen half cycles
  CHECK(flips(quarter * 4u, quarter * 6u) >= 2u);   // and the 1/4 again
  CHECK(flips(quarter * 4u, quarter * 6u) <= 4u);
}

TEST_CASE("wobble: the base is the p-lock or motion point on the same target for its steps, the knob elsewhere, and a moved knob moves it") {
  Rig rig;
  rig.start();
  set_knob(rig, 0.3f);
  Pattern p = play_all(8);
  set_motion_point(p, 0, 3, 0, 203);  // 0.8 of the cutoff's range ... on steps 3 and 4 (quarters 0..3 of both)
  for (unsigned q = 0; q < 4; ++q) {
    set_motion_point(p, 0, 3, q, 203);
    set_motion_point(p, 0, 4, q, 203);
  }
  rig.bank->replace(0, p);
  REQUIRE(rig.seq.set_wobble(true, kWobble1_16, kWobbleSine, 0.2f));  // one cycle a step: a whole number of them in a step
  rig.seq.set_wobble_reset(false);
  rig.engine.play();
  rig.run(frame_of(8 * kStep, rig.tempo));
  const std::vector<Rec> ev = wob_events(rig.voice.log);
  const auto mean = [&](unsigned from_step, unsigned to_step) {
    double acc = 0.0;
    unsigned n = 0;
    for (const Rec& e : ev) {
      if (e.frame >= frame_of(from_step * kStep, rig.tempo) + 128 && e.frame < frame_of(to_step * kStep, rig.tempo)) {
        acc += cutoff_norm(e.value);
        ++n;
      }
    }
    return n != 0u ? acc / n : -1.0;
  };
  const double knob = cutoff_norm(rig.engine.param(rig.vid, 0));
  const double lock = static_cast<double>(core::param_norm(kVoiceParams[0], core::param_denorm(kVoiceParams[0], 203.0f / 254.0f)));
  CHECK(mean(0, 3) == doctest::Approx(knob).epsilon(0.02).scale(1.0));
  CHECK(mean(3, 5) == doctest::Approx(lock).epsilon(0.02).scale(1.0));
  CHECK(mean(5, 8) == doctest::Approx(knob).epsilon(0.02).scale(1.0));
  // The knob moved while the wobble runs: the base follows, at the next update().
  rig.bank->replace(0, play_all(8));  // without the locks
  rig.run(frame_of(8 * kStep, rig.tempo));
  set_knob(rig, 0.6f);
  const std::size_t mark = rig.voice.log.size();
  rig.run(frame_of(960, rig.tempo));
  double acc = 0.0;
  unsigned n = 0;
  const std::vector<Rec> after = params_of(std::vector<Rec>(rig.voice.log.begin() + static_cast<std::ptrdiff_t>(mark), rig.voice.log.end()), 0);
  for (const Rec& e : after) {
    acc += cutoff_norm(e.value);
    ++n;
  }
  REQUIRE(n > 0u);
  CHECK(acc / n == doctest::Approx(cutoff_norm(rig.engine.param(rig.vid, 0))).epsilon(0.03).scale(1.0));
}

TEST_CASE("wobble: switched off, or moved to another target, the old target gets its base back once, and a slew on the target waits") {
  Rig rig;
  rig.start();
  set_knob(rig, 0.4f);
  REQUIRE(rig.seq.set_wobble(true, kWobble1_8, kWobbleTriangle, 1.0f));
  rig.engine.play();
  rig.run(8000);
  const float knob = rig.engine.param(rig.vid, 0);
  REQUIRE(rig.seq.set_wobble(false, 0, 0, 0.0f));
  const std::size_t mark = rig.voice.log.size();
  rig.run(256);
  std::vector<Rec> after = params_of(std::vector<Rec>(rig.voice.log.begin() + static_cast<std::ptrdiff_t>(mark), rig.voice.log.end()), 0);
  REQUIRE(after.size() == 1u);
  CHECK(after[0].value == knob);
  CHECK(after[0].frame % 64u == 0u);
  // Moved: the cutoff gets its base, the delay's mix starts to move.
  REQUIRE(rig.seq.set_wobble(true, kWobble1_8, kWobbleTriangle, 1.0f));
  rig.run(256);
  const std::size_t mark2 = rig.voice.log.size();
  const std::size_t mark_delay = rig.delay.log.size();
  REQUIRE(rig.seq.set_wobble(true, kWobble1_8, kWobbleTriangle, 1.0f, LaneRef{kLaneDelay, 2}));
  rig.run(256);
  after = params_of(std::vector<Rec>(rig.voice.log.begin() + static_cast<std::ptrdiff_t>(mark2), rig.voice.log.end()), 0);
  REQUIRE(after.size() == 1u);
  CHECK(after[0].value == knob);
  CHECK(rig.delay.log.size() - mark_delay >= 4u);
  // A slew to a goal on the wobbled target: the wobble moves around where the slew has got to, and the slew's goal is the base after it.
  REQUIRE(rig.seq.set_wobble(true, kWobble1_8, kWobbleSine, 0.2f));
  rig.run(256);
  const std::size_t mark3 = rig.voice.log.size();
  const float goal = core::param_denorm(kVoiceParams[0], 0.9f);
  CHECK(rig.seq.slew(kLaneVoice, 0, goal, 100.0f));
  rig.run(frame_of(960, rig.tempo));
  const std::vector<Rec> sl = params_of(std::vector<Rec>(rig.voice.log.begin() + static_cast<std::ptrdiff_t>(mark3), rig.voice.log.end()), 0);
  REQUIRE(sl.size() > 8u);
  // Four events a block all along (the slew adds none), and the swing's centre ends at the goal.
  std::map<std::uint64_t, unsigned> per_block;
  for (const Rec& e : sl) {
    ++per_block[e.frame / 64u];
  }
  for (const auto& kv : per_block) {
    CHECK(kv.second <= kWobbleEvents);
  }
  double acc = 0.0;
  unsigned n = 0;
  for (std::size_t i = sl.size() - 688; i < sl.size(); ++i) {  // 688 events 16 frames apart: one cycle of 1/8 (11,025 frames)
    acc += cutoff_norm(sl[i].value);
    ++n;
  }
  CHECK(acc / n == doctest::Approx(0.9).epsilon(0.03).scale(1.0));
  CHECK(rig.seq.slewing() == 1u);  // it is still counted: it waits for the wobble to let go of the target
  REQUIRE(rig.seq.set_wobble(false, 0, 0, 0.0f));
  rig.run(256);
  CHECK(rig.seq.slewing() == 0u);
}

TEST_CASE("wobble: the events per block stay inside the budget, ratchets at 300 BPM and a crowd of slews on the same unit included") {
  Rig rig;
  rig.start(300'000);
  Pattern p = play_all(8);
  for (unsigned j = 0; j < 8; ++j) {
    p.steps[j].flags = with_ratchet(0, 3);
  }
  rig.bank->replace(0, p);
  REQUIRE(rig.seq.set_wobble(true, kWobble1_32, kWobbleSine, 1.0f));
  for (std::uint8_t id = 1; id < 7; ++id) {
    CHECK(rig.seq.slew(kLaneVoice, id, 0.9f * static_cast<float>(id) / 7.0f, 400.0f, false));
  }
  rig.engine.play();
  rig.run(frame_of(8 * kStep, rig.tempo) * 3u);
  std::map<std::uint64_t, unsigned> per_block;
  for (const Rec& e : wob_events(rig.voice.log)) {
    ++per_block[e.frame / 64u];
  }
  unsigned worst = 0;
  for (const auto& kv : per_block) {
    worst = std::max(worst, kv.second);
  }
  CHECK(worst <= kWobbleEvents);
  CHECK(worst >= 5u);  // the notes did restart it, not just the ticks
  CHECK(kWobbleEvents <= kSlewEvents);
  CHECK(rig.seq.dropped_events() == 0u);
  CHECK(rig.engine.stats().dropped_events == 0u);
}

TEST_CASE("wobble: the sample and hold draws once a cycle from the seed: the same seed gives the same values, another does not") {
  const auto run_with = [](std::uint32_t seed) {
    Rig rig;
    rig.start();
    rig.seq.set_seed(seed);
    set_knob(rig, 0.5f);
    REQUIRE(rig.seq.set_wobble(true, kWobble1_16, kWobbleSampleHold, 1.0f));
    rig.seq.set_wobble_reset(false);
    rig.engine.play();
    rig.run(frame_of(16 * kStep, rig.tempo));
    std::vector<float> v;
    for (const Rec& e : wob_events(rig.voice.log)) {
      v.push_back(e.value);
    }
    return v;
  };
  const std::vector<float> a = run_with(77);
  const std::vector<float> b = run_with(77);
  const std::vector<float> c = run_with(78);
  REQUIRE(a.size() > 100u);
  CHECK(a == b);
  CHECK(a != c);
  // Held within a cycle: one cycle is 5,512 frames (about 344 events at 16 frames), the values change about once in it.
  unsigned changes = 0;
  for (std::size_t i = 1; i < a.size(); ++i) {
    changes += a[i] != a[i - 1] ? 1u : 0u;
  }
  CHECK(changes >= 12u);
  CHECK(changes <= 20u);  // 16 steps = 16 cycles
}

TEST_CASE("wobble: it runs while the transport is stopped, free at the transport's tempo, and Play brings it onto the grid") {
  Rig rig;
  rig.start();
  set_knob(rig, 0.5f);
  REQUIRE(rig.seq.set_wobble(true, kWobble1_4, kWobbleSine, 0.6f));
  rig.seq.set_wobble_reset(false);
  rig.run(frame_of(960, rig.tempo) + 1000u);  // stopped for a beat and a bit
  CHECK(wob_events(rig.voice.log).size() > 1000u);
  const double base = cutoff_norm(rig.engine.param(rig.vid, 0));
  // Free from 0: the first value is the sine's 0, a quarter of a cycle later its top.
  CHECK(cutoff_norm(rig.voice.log.front().value) == doctest::Approx(base).epsilon(1e-4).scale(1.0));
  rig.engine.play();
  const std::uint64_t play_frame = rig.now();
  rig.run(4096);
  const std::vector<Rec> ev = wob_events(rig.voice.log);
  const auto first = std::find_if(ev.begin(), ev.end(), [&](const Rec& e) { return e.frame >= play_frame; });
  REQUIRE(first != ev.end());
  CHECK(first->frame == play_frame);
  CHECK(cutoff_norm(first->value) == doctest::Approx(base).epsilon(1e-4).scale(1.0));  // phase 0 again with the transport
}

TEST_CASE("slew: one that does not store starts the next from its goal, and the wobble's base is that goal (the snapshot morph)") {
  Rig rig;
  rig.start();
  set_knob(rig, 0.2f);
  const auto at = [](float norm) { return core::param_denorm(kVoiceParams[0], norm); };
  REQUIRE(rig.seq.slew(kLaneVoice, 0, at(0.4f), 10.0f, false));
  rig.run(4096);  // over: the engine still holds 0.2
  rig.seq.update();
  rig.voice.log.clear();
  REQUIRE(rig.seq.slew(kLaneVoice, 0, at(0.6f), 10.0f, false));
  rig.run(4096);
  const std::vector<Rec> ev = params_of(rig.voice.log, 0);
  REQUIRE_FALSE(ev.empty());
  CHECK(std::fabs(ev.front().value - at(0.4f)) < std::fabs(ev.front().value - at(0.2f)));  // no jump back to 0.2
  CHECK(ev.back().value == doctest::Approx(at(0.6f)));

  rig.seq.update();
  rig.voice.log.clear();
  REQUIRE(rig.seq.set_wobble(true, kWobble1_16, kWobbleSine, 0.1f));
  rig.run(frame_of(4 * kStep, rig.tempo));
  const std::vector<Rec> wob = wob_events(rig.voice.log);
  REQUIRE_FALSE(wob.empty());
  for (const Rec& e : wob) {
    CHECK(e.value >= at(0.54f));
    CHECK(e.value <= at(0.66f));
  }
}
