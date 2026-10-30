// SPDX-License-Identifier: GPL-3.0-or-later
// The sequencer's M3b features (docs/M3B-PLAN.md) against the engine: ratchets (R4), trig conditions (R5), the momentary FILL (R3), the
// slew (R1, R2) and the motion lanes' targets (C2). Recording stand-ins for the voice, the distortion and the delay see every event with
// its absolute frame; a spy hook added after the sequencer reads what the mixer's targets stand at after it.
#include <doctest/doctest.h>

#include <o3_303/seq/bank.h>
#include <o3_303/seq/ops.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <vector>

using namespace o3_303;
using namespace o3_303::seq;
namespace dsp = o3sdk::dsp;
namespace core = o3sdk::core;

namespace {

using core::ParamCurve;
using core::ParamDesc;
using core::ParamUnit;

// ---- Stand-ins --------------------------------------------------------------------------------------------------------------

// The voice's real descriptors (Cutoff is a Table curve ...).
const ParamDesc* const kVoiceParams = [] {
  static AcidVoice voice;
  return voice.info().params;
}();
constexpr std::uint16_t kVoiceParamCount = AcidVoice::kParamCount;

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

// 32 linear parameters 0..100, for the tests that need many targets.
const std::array<ParamDesc, 32> kWide = [] {
  std::array<ParamDesc, 32> t{};
  for (auto& d : t) {
    d = ParamDesc{"P", "", nullptr, nullptr, 0.0f, 100.0f, 50.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
  }
  return t;
}();

struct Rec {
  std::uint64_t frame;  // absolute, since the first block
  dsp::EventKind kind;
  std::uint8_t note;
  std::uint8_t velocity;
  std::uint8_t flags;
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
      return;  // Engine::start() hands over the stored values: not music
    }
    for (std::uint32_t i = 0; i < a.event_count; ++i) {
      const dsp::Event& e = a.events[i];
      log.push_back(Rec{at + e.frame, e.kind, e.note, e.velocity, e.flags, e.param, e.value});
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

// Added after the sequencer: what each block left the slot 0 mixer's targets at.
class MixSpy final : public dsp::Clocked {
 public:
  void block(dsp::BlockContext& ctx) noexcept override {
    std::array<float, dsp::kMixFieldCount> v{};
    for (unsigned f = 0; f < dsp::kMixFieldCount; ++f) {
      v[f] = ctx.mix(0, static_cast<dsp::MixField>(f));
    }
    seen.push_back(v);
  }
  std::vector<std::array<float, dsp::kMixFieldCount>> seen;
};

struct Options {
  bool dist = true;   // the distortion is given to setup()
  bool delay = true;  // the delay is
  bool wide = false;  // all three units have 32 parameters
};

struct Rig {
  dsp::Engine engine;
  Probe voice;
  Probe dist;
  Probe delay;
  MixSpy spy;
  std::unique_ptr<Bank> bank;
  Sequencer seq;
  Options opt;
  dsp::UnitId vid = dsp::kNoUnit;
  dsp::UnitId did = dsp::kNoUnit;
  dsp::UnitId lid = dsp::kNoUnit;

  explicit Rig(Options o = Options{})
      : voice(dsp::UnitKind::Instrument, o.wide ? kWide.data() : kVoiceParams, o.wide ? 32 : kVoiceParamCount),
        dist(dsp::UnitKind::Effect, o.wide ? kWide.data() : kDistParams, o.wide ? 32 : 3),
        delay(dsp::UnitKind::Effect, o.wide ? kWide.data() : kDelayParams, o.wide ? 32 : 3),
        bank(std::make_unique<Bank>()),
        seq(init(*bank)),
        opt(o) {}

  static Bank& init(Bank& b) {
    b.init();
    return b;
  }

  void start(std::uint32_t tempo_mbpm = 120'000) {
    vid = engine.attach(voice);
    did = engine.attach(dist);
    lid = engine.attach(delay);
    REQUIRE(engine.set_source(0, vid));
    REQUIRE(engine.set_insert(0, 0, did));
    REQUIRE(engine.set_send_effect(0, lid));
    REQUIRE(seq.setup(engine, vid, opt.dist ? did : dsp::kNoUnit, opt.delay ? lid : dsp::kNoUnit));
    REQUIRE(engine.add_hook(spy));
    engine.start();
    engine.set_tempo(tempo_mbpm);
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
};

std::vector<Rec> only_notes(const std::vector<Rec>& log) {
  std::vector<Rec> out;
  for (const Rec& e : log) {
    if (e.kind == dsp::EventKind::NoteOn || e.kind == dsp::EventKind::NoteOff) {
      out.push_back(e);
    }
  }
  return out;
}

std::vector<Rec> only_params(const std::vector<Rec>& log, int id = -1) {
  std::vector<Rec> out;
  for (const Rec& e : log) {
    if (e.kind == dsp::EventKind::Param && (id < 0 || e.param == id)) {
      out.push_back(e);
    }
  }
  return out;
}

// A pattern whose steps all play: step j has note 40 + j % 40.
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

std::uint64_t frame_of(std::uint64_t tick, std::uint32_t tempo_mbpm) {
  const std::uint64_t inc = dsp::ticks_per_sample_q32(tempo_mbpm);
  return ((tick << 32) + inc - 1u) / inc;
}

constexpr std::uint32_t kStep = dsp::kTicksPerSixteenth;  // 240 ticks
constexpr std::uint32_t kTempo = 120'000;

// The note events of `log` between two ticks (from inclusive, to exclusive).
std::vector<Rec> between(const std::vector<Rec>& log, std::uint64_t from_tick, std::uint64_t to_tick, std::uint32_t tempo = kTempo) {
  std::vector<Rec> out;
  for (const Rec& e : only_notes(log)) {
    if (e.frame >= frame_of(from_tick, tempo) && e.frame < frame_of(to_tick, tempo)) {
      out.push_back(e);
    }
  }
  return out;
}

}  // namespace

// ==== R4: ratchets =============================================================================================================

TEST_CASE("ratchet: n hits at the starts of n equal parts of the step, each with the gate of its part") {
  struct Want {
    unsigned hits;
    std::vector<std::uint32_t> on;   // ticks after the step's start
    std::vector<std::uint32_t> off;
  };
  // The step is 240 ticks, the gate 50 % of each part: x2 parts of 120 (gate 60), x3 of 80 (40), x4 of 60 (30).
  const Want want[] = {{2, {0, 120}, {60, 180}}, {3, {0, 80, 160}, {40, 120, 200}}, {4, {0, 60, 120, 180}, {30, 90, 150, 210}}};
  for (const Want& w : want) {
    CAPTURE(w.hits);
    Rig rig;
    Pattern p = play_all(8);
    p.steps[2].flags = with_ratchet(0, w.hits - 1u);
    rig.bank->replace(0, p);
    rig.start();
    rig.engine.play();
    rig.run(frame_of(4 * kStep, kTempo));
    const auto ev = between(rig.voice.log, 2 * kStep, 3 * kStep);
    REQUIRE(ev.size() == 2u * w.hits);
    for (unsigned k = 0; k < w.hits; ++k) {
      CAPTURE(k);
      CHECK(ev[2 * k].kind == dsp::EventKind::NoteOn);
      CHECK(ev[2 * k].note == 42);
      CHECK(ev[2 * k].frame == frame_of(2 * kStep + w.on[k], kTempo));
      CHECK(ev[2 * k + 1].kind == dsp::EventKind::NoteOff);
      CHECK(ev[2 * k + 1].note == 42);
      CHECK(ev[2 * k + 1].frame == frame_of(2 * kStep + w.off[k], kTempo));
    }
    // The steps around it are plain and on their ticks.
    const auto before = between(rig.voice.log, kStep, 2 * kStep);
    REQUIRE(before.size() == 2);
    CHECK(before[0].frame == frame_of(kStep, kTempo));
    const auto after = between(rig.voice.log, 3 * kStep, 4 * kStep);
    REQUIRE(after.size() == 2);
    CHECK(after[0].frame == frame_of(3 * kStep, kTempo));
    CHECK(after[0].note == 43);
  }
}

TEST_CASE("ratchet: the hits fill the swung step, and the next step is where it was") {
  // Swing 50 %: step 0 is 300 ticks, step 1 starts at 300 and is 180 ticks long. x4 on step 1: parts of 45, gate 23 (50 % of 45, rounded).
  Rig rig;
  Pattern p = play_all(8);
  set_swing(p, 50);
  p.steps[1].flags = with_ratchet(0, 3);
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(3 * kStep, kTempo));
  const auto ev = between(rig.voice.log, 300, 480);
  REQUIRE(ev.size() == 8);
  const std::uint32_t on[] = {300, 345, 390, 435};
  const std::uint32_t off[] = {323, 368, 413, 458};
  for (unsigned k = 0; k < 4; ++k) {
    CHECK(ev[2 * k].frame == frame_of(on[k], kTempo));
    CHECK(ev[2 * k + 1].frame == frame_of(off[k], kTempo));
  }
  const auto next = between(rig.voice.log, 480, 481);
  REQUIRE(next.size() == 1);
  CHECK(next[0].kind == dsp::EventKind::NoteOn);
  CHECK(next[0].note == 42);
}

TEST_CASE("ratchet: the step's accent is on the first hit; ACC ALL accents them all") {
  for (const bool acc_all : {false, true}) {
    CAPTURE(acc_all);
    Rig rig;
    Pattern p = play_all(8);
    p.steps[1].flags = static_cast<std::uint8_t>(with_ratchet(0, 2) | kStepAccent);  // x3, accented
    rig.bank->replace(0, p);
    rig.seq.set_perf_flag(kPerfAccAll, acc_all);
    rig.start();
    rig.engine.play();
    rig.run(frame_of(3 * kStep, kTempo));
    std::vector<Rec> hits;
    for (const Rec& e : between(rig.voice.log, kStep, 2 * kStep)) {
      if (e.kind == dsp::EventKind::NoteOn) {
        hits.push_back(e);
      }
    }
    REQUIRE(hits.size() == 3);
    for (unsigned k = 0; k < 3; ++k) {
      const bool accented = k == 0u || acc_all;
      CAPTURE(k);
      CHECK(hits[k].flags == (accented ? dsp::kNoteAccent : 0));
      CHECK(hits[k].velocity == (accented ? 110 : 80));
    }
  }
}

TEST_CASE("ratchet: a full gate ends each hit at the start of the next, off before on") {
  Rig rig;
  Pattern p = play_all(8);
  set_gate(p, 100);
  p.steps[1].flags = with_ratchet(0, 1);  // x2
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(3 * kStep, kTempo));
  // From tick 240: step 0's off (a full gate), step 1's first hit, its off at 360 and the second hit's on at the same frame; the second
  // hit's off at 480 belongs to the next window.
  const auto ev = between(rig.voice.log, kStep, 2 * kStep);
  REQUIRE(ev.size() == 4);
  CHECK(ev[0].kind == dsp::EventKind::NoteOff);
  CHECK(ev[0].note == 40);
  CHECK(ev[1].kind == dsp::EventKind::NoteOn);
  CHECK(ev[1].note == 41);
  CHECK(ev[2].kind == dsp::EventKind::NoteOff);  // off at 360 ...
  CHECK(ev[3].kind == dsp::EventKind::NoteOn);   // ... then the second hit's on, at the same frame
  CHECK(ev[2].frame == ev[3].frame);
  CHECK(ev[2].frame == frame_of(kStep + 120, kTempo));
  CHECK_FALSE((ev[3].flags & dsp::kNoteSlide) != 0);
  const auto end = between(rig.voice.log, 2 * kStep, 2 * kStep + 1);
  REQUIRE(end.size() == 2);
  CHECK(end[0].kind == dsp::EventKind::NoteOff);  // the second hit ends with the step, before step 2's note
  CHECK(end[1].kind == dsp::EventKind::NoteOn);
}

TEST_CASE("ratchet and slide: a sliding step holds its LAST hit into the next step, the earlier hits are plain notes") {
  Rig rig;
  Pattern p = play_all(8);
  p.steps[1].flags = static_cast<std::uint8_t>(with_ratchet(0, 1) | kStepSlide);  // x2 and slide
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(4 * kStep, kTempo));
  const auto ev = between(rig.voice.log, kStep, 3 * kStep);
  // step 1: on 41 @240, off 41 @300 (gate of the first part), on 41 @360 (second hit: no off at 420, it is held) ; step 2 @480:
  // on 42 with the slide flag, then off 41 at the same frame; off 42 @600.
  REQUIRE(ev.size() == 6);
  CHECK(ev[0].kind == dsp::EventKind::NoteOn);
  CHECK(ev[0].frame == frame_of(kStep, kTempo));
  CHECK_FALSE((ev[0].flags & dsp::kNoteSlide) != 0);
  CHECK(ev[1].kind == dsp::EventKind::NoteOff);
  CHECK(ev[1].frame == frame_of(kStep + 60, kTempo));
  CHECK(ev[2].kind == dsp::EventKind::NoteOn);
  CHECK(ev[2].frame == frame_of(kStep + 120, kTempo));
  CHECK_FALSE((ev[2].flags & dsp::kNoteSlide) != 0);  // a hit does not glide from the hit before
  CHECK(ev[3].kind == dsp::EventKind::NoteOn);
  CHECK(ev[3].note == 42);
  CHECK(ev[3].frame == frame_of(2 * kStep, kTempo));
  CHECK((ev[3].flags & dsp::kNoteSlide) != 0);
  CHECK(ev[4].kind == dsp::EventKind::NoteOff);
  CHECK(ev[4].note == 41);
  CHECK(ev[4].frame == ev[3].frame);
  CHECK(ev[5].kind == dsp::EventKind::NoteOff);
  CHECK(ev[5].note == 42);
  CHECK(ev[5].frame == frame_of(2 * kStep + 120, kTempo));  // step 2 is a plain step: the gate is half of the whole step
}

TEST_CASE("ratchet after a slide: only the first hit glides in, the others retrigger") {
  Rig rig;
  Pattern p = play_all(8);
  p.steps[1].flags = kStepSlide;
  p.steps[2].flags = with_ratchet(0, 2);  // x3
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(4 * kStep, kTempo));
  std::vector<Rec> on;
  for (const Rec& e : between(rig.voice.log, 2 * kStep, 3 * kStep)) {
    if (e.kind == dsp::EventKind::NoteOn) {
      on.push_back(e);
    }
  }
  REQUIRE(on.size() == 3);
  CHECK((on[0].flags & dsp::kNoteSlide) != 0);
  CHECK_FALSE((on[1].flags & dsp::kNoteSlide) != 0);
  CHECK_FALSE((on[2].flags & dsp::kNoteSlide) != 0);
  // The held note 41 is released at the glide's frame, once.
  unsigned offs41 = 0;
  for (const Rec& e : between(rig.voice.log, 2 * kStep, 3 * kStep)) {
    if (e.kind == dsp::EventKind::NoteOff && e.note == 41) {
      ++offs41;
      CHECK(e.frame == frame_of(2 * kStep, kTempo));
    }
  }
  CHECK(offs41 == 1);
}

TEST_CASE("ratchet and tie: a tie behind a ratcheted step holds its last hit; a ratchet on a rest or a tie plays nothing extra") {
  Rig rig;
  Pattern p = play_all(8);
  p.steps[1].flags = with_ratchet(0, 1);                                // x2, then
  p.steps[2].flags = static_cast<std::uint8_t>(kStepTie | with_ratchet(0, 3));  // a tie (its ratchet is ignored)
  p.steps[4].flags = static_cast<std::uint8_t>(kStepRest | with_ratchet(0, 2));  // a rest with a ratchet
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(6 * kStep, kTempo));
  const auto ev = between(rig.voice.log, kStep, 3 * kStep);
  // 41: on @240, off @300, on @360, held through the tie, off with the tie step's gate (480 + 120 = 600) ; nothing at 420.
  REQUIRE(ev.size() == 4);
  CHECK(ev[0].kind == dsp::EventKind::NoteOn);
  CHECK(ev[1].kind == dsp::EventKind::NoteOff);
  CHECK(ev[2].kind == dsp::EventKind::NoteOn);
  CHECK(ev[2].frame == frame_of(kStep + 120, kTempo));
  CHECK(ev[3].kind == dsp::EventKind::NoteOff);
  CHECK(ev[3].note == 41);
  CHECK(ev[3].frame == frame_of(2 * kStep + 120, kTempo));
  CHECK(between(rig.voice.log, 4 * kStep, 5 * kStep).empty());  // the rest
}

TEST_CASE("ratchets: the same events whatever the block size, swing and tempo") {
  Pattern p = play_all(16);
  set_swing(p, 40);
  set_gate(p, 80);
  p.steps[1].flags = with_ratchet(0, 3);
  p.steps[2].flags = static_cast<std::uint8_t>(with_ratchet(0, 1) | kStepSlide);
  p.steps[3].flags = with_ratchet(0, 2);
  p.steps[5].flags = static_cast<std::uint8_t>(kStepTie);
  p.steps[6].flags = static_cast<std::uint8_t>(with_ratchet(0, 1) | kStepAccent);
  p.steps[9].flags = kStepRest;
  p.steps[12].flags = with_ratchet(0, 3);
  std::vector<Rec> reference;
  for (const std::uint32_t tempo : {60'000u, 133'000u, 300'000u}) {
    reference.clear();
    for (const unsigned block : {64u, 37u, 7u, 1u}) {
      CAPTURE(tempo);
      CAPTURE(block);
      Rig rig;
      rig.bank->replace(0, p);
      rig.start(tempo);
      rig.engine.play();
      rig.run(frame_of(34 * kStep, tempo), block);
      if (reference.empty()) {
        reference = rig.voice.log;
        CHECK(reference.size() > 60);
        continue;
      }
      const std::uint64_t horizon = std::min<std::uint64_t>(rig.voice.at, frame_of(34 * kStep, tempo));
      std::size_t i = 0;
      for (const Rec& e : reference) {
        if (e.frame >= horizon) {
          break;
        }
        REQUIRE(i < rig.voice.log.size());
        const Rec& g = rig.voice.log[i++];
        CHECK(g.frame == e.frame);
        CHECK(g.kind == e.kind);
        CHECK(g.note == e.note);
        CHECK(g.flags == e.flags);
      }
    }
  }
}

TEST_CASE("ratchets at 300 BPM on a short swung step: the hits stay strictly in order on their ticks") {
  Rig rig;
  Pattern p = play_all(8);
  set_swing(p, kMaxSwing);  // the odd step is 160 ticks
  for (unsigned j = 0; j < 8; ++j) {
    p.steps[j].flags = with_ratchet(0, 3);
  }
  rig.bank->replace(0, p);
  rig.start(300'000);
  rig.engine.play();
  rig.run(frame_of(10 * kStep, 300'000));
  const auto n = only_notes(rig.voice.log);
  REQUIRE(n.size() >= 64);
  std::uint64_t last = 0;
  unsigned ons = 0;
  for (const Rec& e : n) {
    CHECK(e.frame >= last);
    last = e.frame;
    ons += e.kind == dsp::EventKind::NoteOn ? 1u : 0u;
  }
  CHECK(ons >= 32);
  // Step 1 (odd, 160 ticks, starts at 240 + 79 = 319): hits at 319, 359, 399, 439.
  const auto ev = between(rig.voice.log, 319, 479, 300'000);
  REQUIRE(ev.size() == 8);
  const std::uint32_t on[] = {319, 359, 399, 439};
  for (unsigned k = 0; k < 4; ++k) {
    CHECK(ev[2 * k].frame == frame_of(on[k], 300'000));
  }
}

// ==== R5: trig conditions ======================================================================================================

namespace {

// The set of frames at which a note-on happened.
std::set<std::uint64_t> note_on_frames(const std::vector<Rec>& log) {
  std::set<std::uint64_t> out;
  for (const Rec& e : log) {
    if (e.kind == dsp::EventKind::NoteOn) {
      out.insert(e.frame);
    }
  }
  return out;
}

// Did step `step` of pass `pass` of a pattern of `length` steps (no swing) play?
bool played(const std::set<std::uint64_t>& on, unsigned pass, unsigned step, unsigned length, std::uint32_t tempo) {
  return on.count(frame_of(static_cast<std::uint64_t>(pass * length + step) * kStep, tempo)) != 0u;
}

}  // namespace

TEST_CASE("condition A:B plays on pass A of every B (pass % B == A - 1)") {
  const std::uint32_t tempo = 300'000;
  Rig rig;
  Pattern p = play_all(8);
  const unsigned a[8] = {1, 2, 1, 2, 3, 1, 4, 5};
  const unsigned b[8] = {2, 2, 3, 3, 3, 4, 4, 8};
  for (unsigned j = 0; j < 8; ++j) {
    p.steps[j].cond = cond_ratio(a[j], b[j]);
  }
  rig.bank->replace(0, p);
  rig.start(tempo);
  rig.engine.play();
  const unsigned passes = 24;  // the least common multiple of 2, 3, 4 and 8
  rig.run(frame_of(static_cast<std::uint64_t>(passes) * 8 * kStep, tempo));
  const auto on = note_on_frames(rig.voice.log);
  unsigned plays = 0;
  for (unsigned pass = 0; pass < passes; ++pass) {
    for (unsigned j = 0; j < 8; ++j) {
      const bool want = pass % b[j] == a[j] - 1u;
      CAPTURE(pass);
      CAPTURE(j);
      CHECK(played(on, pass, j, 8, tempo) == want);
      plays += want ? 1u : 0u;
    }
  }
  CHECK(plays == 12 + 12 + 8 + 8 + 8 + 6 + 6 + 3);  // 24/2, 24/2, 24/3 x3, 24/4 x2, 24/8
}

TEST_CASE("FIRST and NOT FIRST: the first pass after Play, after a jump and after the chain starts a pattern") {
  const std::uint32_t tempo = 300'000;
  Pattern p = play_all(8);
  p.steps[0].cond = kCondFirst;
  p.steps[1].cond = kCondNotFirst;
  const auto length_frames = [&](unsigned passes) { return frame_of(static_cast<std::uint64_t>(passes) * 8 * kStep, tempo); };
  {
    Rig rig;
    rig.bank->replace(0, p);
    rig.bank->replace(1, p);
    rig.start(tempo);
    rig.engine.play();
    rig.run(length_frames(3));
    auto on = note_on_frames(rig.voice.log);
    CHECK(played(on, 0, 0, 8, tempo));
    CHECK_FALSE(played(on, 0, 1, 8, tempo));
    for (unsigned pass = 1; pass < 3; ++pass) {
      CHECK_FALSE(played(on, pass, 0, 8, tempo));
      CHECK(played(on, pass, 1, 8, tempo));
    }
    // Stop and Play: the first pass again.
    rig.engine.stop();
    rig.run(64);
    rig.engine.play();
    const std::uint64_t restart = rig.voice.at;  // frame 0 of the first block of the new run
    rig.run(length_frames(2) + 64);
    const auto in_pass = [&](unsigned pass, std::uint8_t note) {
      unsigned n = 0;
      for (const Rec& e : rig.voice.log) {
        n += (e.kind == dsp::EventKind::NoteOn && e.note == note && e.frame >= restart + frame_of(pass * 8 * kStep, tempo) &&
              e.frame < restart + frame_of((pass + 1) * 8 * kStep, tempo))
                 ? 1u
                 : 0u;
      }
      return n;
    };
    CHECK(in_pass(0, 40) == 1);  // FIRST plays on the new first pass ...
    CHECK(in_pass(0, 41) == 0);  // ... and NOT FIRST does not
    CHECK(in_pass(1, 40) == 0);
    CHECK(in_pass(1, 41) == 1);
  }
  {
    // A jump to another pattern (here one with the same conditions) starts its first pass: FIRST plays again.
    Rig rig;
    rig.bank->replace(0, p);
    rig.bank->replace(1, p);
    rig.start(tempo);
    rig.engine.play();
    rig.run(length_frames(3));  // pattern 0 is in its third pass
    CHECK(rig.seq.jump(1));
    rig.run(length_frames(1));
    unsigned step0_plays = 0;
    for (const Rec& e : rig.voice.log) {
      step0_plays += (e.kind == dsp::EventKind::NoteOn && e.note == 40 && e.frame >= frame_of(24 * kStep, tempo)) ? 1u : 0u;
    }
    CHECK(step0_plays == 1);  // the jump starts at the next step; pattern 1's step 0 is FIRST
  }
  {
    // The chain: the pattern queued behind starts its own first pass; and the second pass of a repeating pattern is not "first".
    Rig rig;
    rig.bank->replace(0, p);
    rig.bank->replace(1, p);
    rig.start(tempo);
    CHECK(rig.seq.queue(1));
    rig.engine.play();
    rig.run(length_frames(4));
    const auto on = note_on_frames(rig.voice.log);
    CHECK(played(on, 0, 0, 8, tempo));       // pattern 0, pass 0
    CHECK_FALSE(played(on, 0, 1, 8, tempo));
    CHECK(played(on, 1, 0, 8, tempo));       // pattern 1 starts: its pass 0
    CHECK_FALSE(played(on, 1, 1, 8, tempo));
    CHECK_FALSE(played(on, 2, 0, 8, tempo));  // pattern 1 repeats (the chain is used up): pass 1
    CHECK(played(on, 2, 1, 8, tempo));
  }
}

namespace {

// Per (pass, step) whether a conditional step played, for a pattern of `length` steps run for `passes`.
std::vector<bool> outcomes(const Pattern& p, unsigned passes, std::uint32_t seed, bool set_the_seed, std::uint32_t tempo = 300'000) {
  Rig rig;
  rig.bank->replace(0, p);
  rig.start(tempo);
  if (set_the_seed) {
    rig.seq.set_seed(seed);
  }
  rig.engine.play();
  const unsigned length = p.length;
  rig.run(frame_of(static_cast<std::uint64_t>(passes) * length * kStep, tempo));
  const auto on = note_on_frames(rig.voice.log);
  std::vector<bool> out;
  for (unsigned pass = 0; pass < passes; ++pass) {
    for (unsigned j = 0; j < length; ++j) {
      out.push_back(played(on, pass, j, length, tempo));
    }
  }
  return out;
}

double fraction(const std::vector<bool>& v) {
  unsigned n = 0;
  for (const bool b : v) {
    n += b ? 1u : 0u;
  }
  return static_cast<double>(n) / static_cast<double>(v.size());
}

}  // namespace

TEST_CASE("probability: the share of steps that play follows the percent; the same seed gives the same notes") {
  for (const unsigned pct : {10u, 50u, 90u}) {
    CAPTURE(pct);
    Pattern p = play_all(16);
    for (unsigned j = 0; j < 16; ++j) {
      p.steps[j].cond = cond_probability(pct);
    }
    const auto a = outcomes(p, 40, 0, false);
    const auto b = outcomes(p, 40, 0, false);
    CHECK(a == b);  // reproducible: the construction seed is fixed
    const double f = fraction(a);  // 640 draws
    CHECK(f > static_cast<double>(pct) / 100.0 - 0.07);
    CHECK(f < static_cast<double>(pct) / 100.0 + 0.07);
  }
  Pattern p = play_all(16);
  for (unsigned j = 0; j < 16; ++j) {
    p.steps[j].cond = cond_probability(50);
  }
  const auto def = outcomes(p, 20, 0, false);
  const auto s1 = outcomes(p, 20, 1234, true);
  const auto s2 = outcomes(p, 20, 1234, true);
  const auto s3 = outcomes(p, 20, 99, true);
  CHECK(s1 == s2);
  CHECK(s1 != def);
  CHECK(s1 != s3);
}

TEST_CASE("probability: only steps with a probability draw, so other steps and conditions do not move the outcomes") {
  Pattern a = play_all(16);
  a.steps[1].cond = cond_probability(50);
  a.steps[6].cond = cond_probability(30);
  a.steps[11].cond = cond_probability(70);
  const auto base = outcomes(a, 30, 0, false);
  // The same probabilities, with other conditions and a rest with a probability around them.
  Pattern b = a;
  b.steps[0].cond = cond_ratio(1, 2);
  b.steps[2].cond = kCondNotFirst;
  b.steps[4].cond = kCondFill;
  b.steps[5].cond = kCondNotFill;
  set_flag(b, 8, kStepRest, true);
  b.steps[8].cond = cond_probability(50);  // a rest has no condition: no draw
  const auto other = outcomes(b, 30, 0, false);
  for (unsigned pass = 0; pass < 30; ++pass) {
    for (const unsigned j : {1u, 6u, 11u}) {
      CAPTURE(pass);
      CAPTURE(j);
      CHECK(base[pass * 16 + j] == other[pass * 16 + j]);
    }
  }
  CHECK(fraction(base) < 1.0);
}

TEST_CASE("a condition that does not pass makes the step a rest: the held note ends at its start and the slide is over") {
  const std::uint32_t tempo = 300'000;
  Rig rig;
  Pattern p = play_all(8);
  p.steps[0].flags = kStepSlide;  // holds its note into step 1
  p.steps[1].cond = kCondFirst;
  rig.bank->replace(0, p);
  rig.start(tempo);
  rig.engine.play();
  rig.run(frame_of(20 * kStep, tempo));
  // Pass 0: step 1 plays and glides in (kNoteSlide). Pass 1 (global step 9): it fails: 40 is released at step 1's start, no note-on
  // there, and step 2 is a plain note-on again.
  const auto first = between(rig.voice.log, kStep, kStep + 1, tempo);
  REQUIRE(first.size() == 2);
  CHECK(first[0].kind == dsp::EventKind::NoteOn);
  CHECK(first[0].note == 41);
  CHECK((first[0].flags & dsp::kNoteSlide) != 0);
  const auto second = between(rig.voice.log, 9 * kStep, 9 * kStep + 1, tempo);
  REQUIRE(second.size() == 1);
  CHECK(second[0].kind == dsp::EventKind::NoteOff);
  CHECK(second[0].note == 40);
  const auto step10 = between(rig.voice.log, 10 * kStep, 10 * kStep + 1, tempo);
  REQUIRE(step10.size() == 1);
  CHECK(step10[0].kind == dsp::EventKind::NoteOn);
  CHECK(step10[0].note == 42);
  CHECK_FALSE((step10[0].flags & dsp::kNoteSlide) != 0);
}

TEST_CASE("a note before a tie step whose condition fails ends with its own gate, not at the tie's start") {
  // Step 0 (gate 50 %), step 1 a tie that plays from the second pass on (NOT FIRST). Pass 0: the tie does not play, so step 0 ends at tick 120.
  // Pass 1: the tie plays: step 0's note is held through it and ends with the tie's gate, at 8 x 240 + 240 + 120.
  Rig rig;
  Pattern p = play_all(8);
  p.steps[1].flags = kStepTie;
  p.steps[1].cond = kCondNotFirst;
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(18 * kStep, kTempo));
  const auto pass0 = between(rig.voice.log, 0, 2 * kStep);
  REQUIRE(pass0.size() == 2);
  CHECK(pass0[0].kind == dsp::EventKind::NoteOn);
  CHECK(pass0[1].kind == dsp::EventKind::NoteOff);
  CHECK(pass0[1].note == 40);
  CHECK(pass0[1].frame == frame_of(120, kTempo));
  const auto pass1 = between(rig.voice.log, 8 * kStep, 10 * kStep);
  REQUIRE(pass1.size() == 2);
  CHECK(pass1[0].kind == dsp::EventKind::NoteOn);
  CHECK(pass1[1].kind == dsp::EventKind::NoteOff);
  CHECK(pass1[1].frame == frame_of(9 * kStep + 120, kTempo));
}

TEST_CASE("the look-ahead of a tie's probability is one draw a step: the tie follows the same seeded draws as a note step would") {
  // Pattern A: step 1 is a tie with a probability, pattern B: step 1 is a plain note with the same probability. Both have another probability
  // step further on. The draws come in the same order, so step 1 and step 5 pass in the same passes in both; in A a note before the tie ends with
  // its gate (tick 120) when the tie did not pass and with the tie's gate (360) when it did.
  const std::uint32_t tempo = 300'000;
  const unsigned passes = 40;
  Pattern a = play_all(8);
  a.steps[1].flags = kStepTie;
  a.steps[1].cond = cond_probability(50);
  a.steps[5].cond = cond_probability(50);
  Pattern b = a;
  b.steps[1].flags = 0;
  Rig ra;
  ra.bank->replace(0, a);
  ra.start(tempo);
  ra.engine.play();
  ra.run(frame_of(static_cast<std::uint64_t>(passes) * 8 * kStep, tempo));
  Rig rb;
  rb.bank->replace(0, b);
  rb.start(tempo);
  rb.engine.play();
  rb.run(frame_of(static_cast<std::uint64_t>(passes) * 8 * kStep, tempo));
  const auto on_b = note_on_frames(rb.voice.log);
  const auto on_a = note_on_frames(ra.voice.log);
  const auto off_at = [&](const Rig& r, std::uint8_t note, std::uint64_t tick) {
    for (const Rec& e : r.voice.log) {
      if (e.kind == dsp::EventKind::NoteOff && e.note == note && e.frame == frame_of(tick, tempo)) {
        return true;
      }
    }
    return false;
  };
  unsigned ties_played = 0;
  unsigned ties_failed = 0;
  for (unsigned pass = 0; pass < passes; ++pass) {
    CAPTURE(pass);
    const std::uint64_t base = static_cast<std::uint64_t>(pass) * 8 * kStep;
    const bool b1 = on_b.count(frame_of(base + kStep, tempo)) != 0u;  // step 1 passed, as a note
    if (b1) {
      CHECK(off_at(ra, 40, base + kStep + 120));  // as a tie: the note goes on through it
      CHECK_FALSE(off_at(ra, 40, base + 120));
      ++ties_played;
    } else {
      CHECK(off_at(ra, 40, base + 120));          // not played: the note ends with its own gate
      CHECK_FALSE(off_at(ra, 40, base + kStep));  // and nothing releases it at the tie's start
      ++ties_failed;
    }
    CHECK(on_a.count(frame_of(base + kStep, tempo)) == 0u);  // a tie never starts a note
    CHECK((on_a.count(frame_of(base + 5 * kStep, tempo)) != 0u) == (on_b.count(frame_of(base + 5 * kStep, tempo)) != 0u));
  }
  CHECK(ties_played > 5);
  CHECK(ties_failed > 5);
}

// ==== R3: the momentary FILL ===================================================================================================

TEST_CASE("FILL enters and leaves on a step boundary; the conditions FILL and NOT FILL follow it") {
  Rig rig;
  Pattern p = play_all(16);
  p.steps[8].cond = kCondFill;     // only in a fill
  p.steps[9].cond = kCondNotFill;  // only outside one
  rig.bank->replace(0, p);
  rig.seq.set_fill_variant(FillVariant::OctUp);
  rig.start();
  rig.engine.play();
  const auto until = [&](std::uint64_t tick) { rig.run(frame_of(tick, kTempo) - rig.now()); };
  until(3 * kStep + 100);  // inside step 3
  CHECK_FALSE(rig.seq.playhead().fill);
  rig.seq.set_fill(true);
  until(3 * kStep + 200);  // still inside step 3: the press has not been seen yet
  CHECK_FALSE(rig.seq.playhead().fill);
  until(6 * kStep + 50);   // step 4 started the fill
  CHECK(rig.seq.playhead().fill);
  rig.seq.set_fill(false);
  until(6 * kStep + 200);  // still inside step 6: the release has not been seen yet
  CHECK(rig.seq.playhead().fill);
  until(7 * kStep + 50);   // step 7 started: back to the pattern
  CHECK_FALSE(rig.seq.playhead().fill);
  until(23 * kStep + 100);  // inside step 7 of the second pass
  rig.seq.set_fill(true);
  until(26 * kStep);

  const auto note_at = [&](unsigned step) -> int {
    for (const Rec& e : rig.voice.log) {
      if (e.kind == dsp::EventKind::NoteOn && e.frame == frame_of(step * kStep, kTempo)) {
        return e.note;
      }
    }
    return -1;
  };
  for (unsigned s = 0; s <= 3; ++s) {
    CHECK(note_at(s) == 40 + static_cast<int>(s));  // before the fill
  }
  for (unsigned s = 4; s <= 6; ++s) {
    CHECK(note_at(s) == 52 + static_cast<int>(s));  // the fill: an octave up, from the step after the press
  }
  CHECK(note_at(7) == 47);  // released inside step 6: back at step 7
  CHECK(note_at(8) == -1);  // the FILL condition, outside a fill: silent
  CHECK(note_at(9) == 49);  // NOT FILL, outside a fill: plays
  CHECK(note_at(10) == 50);
  // Second pass: the fill began at step 24 (the press was inside step 23): the FILL step plays (an octave up), NOT FILL does not.
  CHECK(note_at(23) == 47);
  CHECK(note_at(24) == 48 + 12);
  CHECK(note_at(25) == -1);
  CHECK(std::memcmp(&rig.bank->pattern(0), &p, sizeof(Pattern)) == 0);  // the stored pattern never changed
}

TEST_CASE("FILL held at Play: the first step already plays the variant; the FILL condition plays and NOT FILL does not") {
  Rig rig;
  Pattern p = play_all(8);
  p.steps[0].cond = kCondFill;
  p.steps[1].cond = kCondNotFill;
  rig.bank->replace(0, p);
  rig.seq.set_fill_variant(FillVariant::OctUp);
  rig.seq.set_fill(true);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(3 * kStep, kTempo));
  const auto on = note_on_frames(rig.voice.log);
  CHECK(on.count(frame_of(0, kTempo)) == 1);   // FILL: plays
  CHECK(on.count(frame_of(kStep, kTempo)) == 0);  // NOT FILL: silent
  for (const Rec& e : rig.voice.log) {
    if (e.kind == dsp::EventKind::NoteOn && e.frame == frame_of(2 * kStep, kTempo)) {
      CHECK(e.note == 42 + 12);
    }
  }
  CHECK(rig.seq.playhead().fill);
  CHECK(rig.seq.fill_held());
}

TEST_CASE("fill OCT UP: every note an octave up, clamped at 127") {
  Rig rig;
  Pattern p = play_all(8);
  p.steps[2].note = 120;
  rig.bank->replace(0, p);
  rig.seq.set_fill_variant(FillVariant::OctUp);
  rig.seq.set_fill(true);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(4 * kStep, kTempo));
  std::vector<int> notes;
  for (const Rec& e : rig.voice.log) {
    if (e.kind == dsp::EventKind::NoteOn) {
      notes.push_back(e.note);
    }
  }
  REQUIRE(notes.size() == 4);
  CHECK(notes[0] == 52);
  CHECK(notes[1] == 53);
  CHECK(notes[2] == 127);
  CHECK(notes[3] == 55);
}

TEST_CASE("fill RATCHET: double hits, and four hits in the last quarter of the pattern") {
  Rig rig;
  Pattern p = play_all(16);
  set_flag(p, 5, kStepRest, true);  // a rest stays a rest
  rig.bank->replace(0, p);
  rig.seq.set_fill_variant(FillVariant::Ratchet);
  rig.seq.set_fill(true);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(16 * kStep, kTempo));
  for (unsigned s = 0; s < 16; ++s) {
    unsigned hits = 0;
    for (const Rec& e : between(rig.voice.log, s * kStep, (s + 1) * kStep)) {
      hits += e.kind == dsp::EventKind::NoteOn ? 1u : 0u;
    }
    CAPTURE(s);
    CHECK(hits == (s == 5 ? 0u : (s >= 12 ? 4u : 2u)));
  }
}

TEST_CASE("fill ACC ROTATE: the accents move one step later, the last one wraps to step 0") {
  Rig rig;
  Pattern p = play_all(8);
  p.steps[2].flags = kStepAccent;
  p.steps[7].flags = kStepAccent;
  rig.bank->replace(0, p);
  rig.seq.set_fill_variant(FillVariant::AccRotate);
  rig.seq.set_fill(true);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(8 * kStep, kTempo));
  std::vector<unsigned> accented;
  for (const Rec& e : rig.voice.log) {
    if (e.kind == dsp::EventKind::NoteOn && (e.flags & dsp::kNoteAccent) != 0) {
      unsigned step = 0;
      while (e.frame != frame_of(step * kStep, kTempo)) {
        ++step;
      }
      accented.push_back(step);
    }
  }
  REQUIRE(accented.size() == 2);
  CHECK(accented[0] == 0);
  CHECK(accented[1] == 3);
}

TEST_CASE("fill MUTATE: a variant of the pattern in its own notes, the same for a seed, another one at the next press; the stored pattern is untouched") {
  const auto fill_notes = [](std::uint32_t seed, unsigned presses) {
    Rig rig;
    Pattern p = play_all(16);
    for (unsigned j = 0; j < 16; ++j) {
      p.steps[j].note = static_cast<std::uint8_t>(36 + (j * 5u) % 12u);
      if (j % 3u == 2u) {
        p.steps[j].flags = kStepRest;
      }
    }
    rig.bank->replace(0, p);
    rig.seq.set_seed(seed);
    rig.seq.set_fill_variant(FillVariant::Mutate);
    rig.start();
    rig.engine.play();
    std::vector<int> last;
    for (unsigned press = 0; press < presses; ++press) {
      rig.seq.set_fill(true);
      const std::uint64_t from = rig.now();
      rig.run(frame_of(16 * kStep, kTempo) + 64);
      rig.seq.set_fill(false);
      rig.run(frame_of(16 * kStep, kTempo));
      last.clear();
      for (const Rec& e : rig.voice.log) {
        if (e.kind == dsp::EventKind::NoteOn && e.frame >= from + frame_of(kStep, kTempo) &&
            e.frame < from + frame_of(16 * kStep, kTempo)) {
          last.push_back(e.note * 1000 + e.flags);
        }
      }
    }
    CHECK(std::memcmp(&rig.bank->pattern(0), &p, sizeof(Pattern)) == 0);
    return last;
  };
  const auto a = fill_notes(5, 1);
  CHECK(a == fill_notes(5, 1));        // reproducible
  CHECK(a != fill_notes(6, 1));        // a seed of its own
  CHECK(fill_notes(5, 2) != a);        // the second press is another variant

  // The variant stays in the pattern's notes (a note borrowed from a step, or an octave away from one), keeps step 0, changes some steps but
  // not all, and turns some rests into notes.
  Pattern base = play_all(16);
  std::set<int> pool;
  for (unsigned j = 0; j < 16; ++j) {
    base.steps[j].note = static_cast<std::uint8_t>(36 + (j * 5u) % 12u);
    pool.insert(base.steps[j].note);
  }
  Pattern v = base;
  apply_fill(v, FillVariant::Mutate, 77);
  unsigned changed = 0;
  CHECK(std::memcmp(&v.steps[0], &base.steps[0], sizeof(Step)) == 0);
  for (unsigned j = 0; j < 16; ++j) {
    changed += std::memcmp(&v.steps[j], &base.steps[j], sizeof(Step)) != 0 ? 1u : 0u;
    const int n = v.steps[j].note;
    CHECK((pool.count(n) != 0u || pool.count(n - 12) != 0u || pool.count(n + 12) != 0u));
  }
  CHECK(changed > 0);
  CHECK(changed < 16);
  Pattern sparse = base;
  unsigned rests = 0;
  for (unsigned j = 1; j < 16; j += 2) {
    sparse.steps[j].flags = kStepRest;
    ++rests;
  }
  Pattern w = sparse;
  apply_fill(w, FillVariant::Mutate, 77);
  unsigned rests_after = 0;
  for (unsigned j = 0; j < 16; ++j) {
    rests_after += (w.steps[j].flags & kStepRest) != 0u ? 1u : 0u;
  }
  CHECK(rests_after < rests);
}

TEST_CASE("a fill follows a pattern change and an edit of the playing pattern") {
  Rig rig;
  Pattern a = play_all(8);
  Pattern b = play_all(8);
  for (unsigned j = 0; j < 8; ++j) {
    b.steps[j].note = static_cast<std::uint8_t>(60 + j);
  }
  rig.bank->replace(0, a);
  rig.bank->replace(1, b);
  rig.seq.set_fill_variant(FillVariant::OctUp);
  rig.seq.set_fill(true);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(2 * kStep + 100, kTempo));
  CHECK(rig.seq.jump(1));
  rig.run(frame_of(5 * kStep, kTempo) - rig.now());
  // Pattern 1's variant from the jump's step on (step 3): 72, 73 ...
  std::vector<int> notes;
  for (const Rec& e : rig.voice.log) {
    if (e.kind == dsp::EventKind::NoteOn) {
      notes.push_back(e.note);
    }
  }
  REQUIRE(notes.size() >= 5);
  CHECK(notes[0] == 52);
  CHECK(notes[1] == 53);
  CHECK(notes[2] == 54);
  CHECK(notes[3] == 72);
  CHECK(notes[4] == 73);
  // An edit of the playing pattern is in the variant the next time the step comes: pattern 1's step 6 is global step 9.
  CHECK(rig.bank->set_note(1, 6, 20));
  rig.run(frame_of(11 * kStep, kTempo) - rig.now());
  int at9 = -1;
  for (const Rec& e : rig.voice.log) {
    if (e.kind == dsp::EventKind::NoteOn && e.frame == frame_of(9 * kStep, kTempo)) {
      at9 = e.note;
    }
  }
  CHECK(at9 == 32);  // 20 + 12
}

// ==== The slew ================================================================================================================

namespace {

// The value the slew should have `frames` into its time: linear along the knob (the descriptor's curve).
float along(const ParamDesc& d, float from, float to, std::uint32_t frames, std::uint32_t total) {
  const float a = core::param_norm(d, from);
  const float b = core::param_norm(d, to);
  return core::param_denorm(d, a + (b - a) * (static_cast<float>(frames) / static_cast<float>(total)));
}

}  // namespace

TEST_CASE("a slew moves a parameter to its goal in the given time: events every 16 frames along the knob's curve, the goal at the exact frame") {
  Rig rig;
  rig.start();
  const ParamDesc& cutoff = kVoiceParams[AcidVoice::Cutoff];
  const float from = rig.engine.param(rig.vid, AcidVoice::Cutoff);
  CHECK(rig.seq.slew(kLaneVoice, AcidVoice::Cutoff, 3000.0f, 30.0f));
  CHECK(rig.engine.param(rig.vid, AcidVoice::Cutoff) == doctest::Approx(core::param_clamp(cutoff, 3000.0f)));  // stored at once
  rig.run(2000);
  const float goal = core::param_clamp(cutoff, 3000.0f);
  const std::uint32_t total = 1323;  // 30 ms at 44.1 kHz
  const auto ev = only_params(rig.voice.log, AcidVoice::Cutoff);
  REQUIRE(ev.size() >= 4);
  // The jump the stored value queued at frame 0 never sounds: the slew's own event follows it at the same frame, at the start value.
  CHECK(ev[0].frame == 0);
  CHECK(ev[0].value == doctest::Approx(goal));
  CHECK(ev[1].frame == 0);
  CHECK(ev[1].value == doctest::Approx(from));
  float prev = from;
  for (std::size_t i = 2; i + 1 < ev.size(); ++i) {
    CAPTURE(i);
    CHECK(ev[i].frame == 16 * (i - 1));
    CHECK(ev[i].value >= prev);
    CHECK(ev[i].value == doctest::Approx(along(cutoff, from, goal, static_cast<std::uint32_t>(ev[i].frame), total)).epsilon(1e-4));
    prev = ev[i].value;
  }
  CHECK(ev.back().frame == total);
  CHECK(ev.back().value == goal);
  CHECK(rig.seq.slewing() == 0);
  // And nothing more afterwards.
  const std::size_t n = only_params(rig.voice.log, AcidVoice::Cutoff).size();
  rig.run(1000);
  CHECK(only_params(rig.voice.log, AcidVoice::Cutoff).size() == n);
}

TEST_CASE("a slew started while stopped runs; a second slew on the same target goes on from where the first has got to") {
  Rig rig;
  rig.start();
  const ParamDesc& cutoff = kVoiceParams[AcidVoice::Cutoff];
  CHECK_FALSE(rig.engine.playing());
  CHECK(rig.seq.slew(kLaneVoice, AcidVoice::Cutoff, 3000.0f, 100.0f));
  rig.run(1024);  // 16 blocks: 23 ms into the 100 ms
  CHECK(rig.seq.slewing() == 1);
  const auto before = only_params(rig.voice.log, AcidVoice::Cutoff);
  const float at_retarget = before.back().value;
  CHECK(rig.seq.slew(kLaneVoice, AcidVoice::Cutoff, 500.0f, 50.0f));
  rig.run(64);
  const auto after = only_params(rig.voice.log, AcidVoice::Cutoff);
  // Frame 0 of the next block: the main loop's stored goal (500), then the new slew's first event, on the old trajectory.
  REQUIRE(after.size() >= before.size() + 2);
  const Rec& first = after[before.size() + 1];
  CHECK(first.frame == 1024);
  CHECK(first.value == doctest::Approx(at_retarget).epsilon(0.02));  // continuous: no jump to the old goal or the start
  rig.run(4000);
  const auto fin = only_params(rig.voice.log, AcidVoice::Cutoff);
  CHECK(fin.back().value == doctest::Approx(500.0f));
  CHECK(fin.back().frame == 1024 + 2205);  // 50 ms from the block where the second slew started
  CHECK(rig.seq.slewing() == 0);
  (void)cutoff;
}

TEST_CASE("slew_stop ends it where it is; a slew of no time, or of an enum, is a plain write") {
  Rig rig;
  rig.start();
  CHECK(rig.seq.slew(kLaneVoice, AcidVoice::Reso, 0.9f, 200.0f));
  rig.run(512);
  CHECK(rig.seq.slew_stop(kLaneVoice, AcidVoice::Reso));
  rig.run(64);
  const std::size_t n = only_params(rig.voice.log, AcidVoice::Reso).size();
  rig.run(20000);
  CHECK(only_params(rig.voice.log, AcidVoice::Reso).size() == n);
  CHECK(rig.seq.slewing() == 0);

  const std::uint64_t t0 = rig.now();
  CHECK(rig.seq.slew(kLaneVoice, AcidVoice::Decay, 0.5f, 0.0f));  // no time: at once
  CHECK(rig.seq.slew(kLaneDist, 2, 2.0f, 50.0f));             // an enum does not glide
  rig.run(64);
  const auto d = only_params(rig.voice.log, AcidVoice::Decay);
  REQUIRE(!d.empty());
  CHECK(d.back().value == doctest::Approx(core::param_clamp(kVoiceParams[AcidVoice::Decay], 0.5f)));
  CHECK(rig.seq.slewing() == 0);
  const auto e = only_params(rig.dist.log, 2);
  REQUIRE(e.size() >= 1);
  CHECK(e.back().value == 2.0f);
  CHECK(e.back().frame < t0 + 64);
  rig.run(5000);
  CHECK(only_params(rig.dist.log, 2).size() == e.size());
}

TEST_CASE("slew targets must exist: a bad target, an id past the table, a missing unit") {
  Options o;
  o.dist = false;
  Rig rig(o);
  rig.start();
  CHECK_FALSE(rig.seq.slew(kLaneNone, 0, 1.0f, 10.0f));
  CHECK_FALSE(rig.seq.slew(9, 0, 1.0f, 10.0f));
  CHECK_FALSE(rig.seq.slew(kLaneVoice, 200, 1.0f, 10.0f));
  CHECK_FALSE(rig.seq.slew(kLaneDist, 0, 1.0f, 10.0f));  // not given to setup()
  CHECK_FALSE(rig.seq.slew(kLaneDelay, 3, 1.0f, 10.0f));
  CHECK_FALSE(rig.seq.slew(kLaneMixer, 5, 1.0f, 10.0f));
  CHECK(rig.seq.slew(kLaneDelay, 2, 1.0f, 10.0f));
  CHECK(rig.seq.slew(kLaneMixer, kMixerPan, 0.5f, 10.0f));
}

TEST_CASE("a mixer slew writes one value a block toward its goal, from where the field stands, and reaches it in the block where the time ends") {
  Rig rig;
  rig.start();
  rig.run(64);
  const std::size_t blocks0 = rig.spy.seen.size();
  CHECK(rig.seq.slew(kLaneMixer, kMixerLevel, 0.2f, 30.0f));
  CHECK(rig.engine.mix(0, dsp::MixField::Level) == doctest::Approx(0.2f));  // the stored value is the goal at once
  rig.run(64 * 30);
  float prev = 1.0f;
  for (std::size_t b = blocks0; b < blocks0 + 20; ++b) {
    const float v = rig.spy.seen[b][kMixerLevel];
    CAPTURE(b);
    CHECK(v < prev);   // down, block by block ...
    CHECK(v > 0.2f);   // ... not there yet
    CHECK(v == doctest::Approx(1.0f + (0.2f - 1.0f) * (64.0f * static_cast<float>(b - blocks0 + 1)) / 1323.0f).epsilon(1e-3));
    prev = v;
  }
  CHECK(rig.spy.seen[blocks0 + 20][kMixerLevel] == 0.2f);  // frames 1280..1343 hold the end (1323)
  for (std::size_t b = blocks0 + 21; b < rig.spy.seen.size(); ++b) {
    CHECK(rig.spy.seen[b][kMixerLevel] == 0.2f);
  }
  CHECK(rig.seq.slewing() == 0);
  // The other fields did not move.
  CHECK(rig.spy.seen.back()[kMixerSendA] == 0.0f);
}

TEST_CASE("a slew is suppressed while a lock holds its target, and goes on along its line when the lock ends") {
  Rig rig;
  const std::uint32_t total = 44100;  // 1 s
  Pattern p = play_all(16);
  set_plock(p, 0, 2, 200);  // cutoff locked on step 2 (and that is all: step 3 has no point, so the lock ends at its start)
  rig.bank->replace(0, p);
  rig.start();
  const ParamDesc& cutoff = kVoiceParams[AcidVoice::Cutoff];
  const float from = rig.engine.param(rig.vid, AcidVoice::Cutoff);
  CHECK(rig.seq.slew(kLaneVoice, AcidVoice::Cutoff, 3000.0f, 1000.0f));
  rig.seq.update();  // the screen hands over the knob: the goal
  rig.engine.play();
  rig.run(frame_of(5 * kStep, kTempo));
  const float goal = core::param_clamp(cutoff, 3000.0f);
  const std::uint64_t lock_on = frame_of(2 * kStep, kTempo);
  const std::uint64_t lock_off = frame_of(3 * kStep, kTempo);
  unsigned during = 0;
  float restored = -1.0f;
  float lock_value = -1.0f;
  for (const Rec& e : only_params(rig.voice.log, AcidVoice::Cutoff)) {
    if (e.frame == lock_on) {
      lock_value = e.value;  // the last event of that frame is the lock's (it comes with the step)
    }
    if (e.frame > lock_on && e.frame < lock_off) {
      ++during;
    }
    if (e.frame == lock_off) {
      restored = e.value;
    }
  }
  CHECK(lock_value == doctest::Approx(point_to_value(cutoff, 200)));
  CHECK(during == 0);  // the slew said nothing for the whole step
  CHECK(restored > from);
  CHECK(restored < goal);
  CHECK(restored == doctest::Approx(along(cutoff, from, goal, static_cast<std::uint32_t>(lock_off), total)).epsilon(1e-3));
  rig.run(frame_of(12 * kStep, kTempo) - rig.now());
  const auto ev = only_params(rig.voice.log, AcidVoice::Cutoff);
  CHECK(ev.back().frame == total);
  CHECK(ev.back().value == goal);
}

TEST_CASE("a motion point on the slewed target also overrides the slew for its quarter, and the slew resumes after") {
  Rig rig;
  Pattern p = play_all(16);
  set_motion_point(p, 0, 1, 1, 100);  // one point: step 1, quarter 1
  rig.bank->replace(0, p);
  rig.start();
  const ParamDesc& cutoff = kVoiceParams[AcidVoice::Cutoff];
  const float from = rig.engine.param(rig.vid, AcidVoice::Cutoff);
  CHECK(rig.seq.slew(kLaneVoice, AcidVoice::Cutoff, 3000.0f, 1000.0f));
  rig.seq.update();
  rig.engine.play();
  rig.run(frame_of(2 * kStep, kTempo));
  const std::uint64_t on = frame_of(kStep + 60, kTempo);
  const std::uint64_t off = frame_of(kStep + 120, kTempo);
  unsigned during = 0;
  float lock_value = -1.0f;
  float resumed = -1.0f;
  for (const Rec& e : only_params(rig.voice.log, AcidVoice::Cutoff)) {
    if (e.frame == on) {
      lock_value = e.value;
    }
    if (e.frame > on && e.frame < off) {
      ++during;
    }
    if (e.frame == off) {
      resumed = e.value;
    }
  }
  CHECK(lock_value == doctest::Approx(point_to_value(cutoff, 100)));
  CHECK(during == 0);
  CHECK(resumed == doctest::Approx(along(cutoff, from, core::param_clamp(cutoff, 3000.0f), static_cast<std::uint32_t>(off), 44100)).epsilon(1e-3));
}

TEST_CASE("many slews at once: no event is lost, the notes are as without them, every slew lands on its goal") {
  const std::uint32_t tempo = 300'000;
  Pattern p = play_all(16);
  for (unsigned j = 0; j < 16; ++j) {
    p.steps[j].flags = with_ratchet(0, 3);  // busy: four hits a step
  }
  p.steps[3].flags = static_cast<std::uint8_t>(with_ratchet(0, 3) | kStepSlide);
  set_plock(p, 0, 4, 100);
  std::uint64_t stores_at = 0;
  const auto run = [&](bool slews) {
    Options o;
    o.wide = true;
    auto rig = std::make_unique<Rig>(o);
    rig->bank->replace(0, p);
    rig->start(tempo);
    if (slews) {
      for (unsigned id = 0; id < 20; ++id) {  // store = false: one event each in the first block, as a morph starts them
        CHECK(rig->seq.slew(kLaneVoice, static_cast<std::uint8_t>(id), 90.0f - static_cast<float>(id), 40.0f, false));
      }
      for (unsigned id = 0; id < 12; ++id) {
        CHECK(rig->seq.slew(kLaneDist, static_cast<std::uint8_t>(id), 10.0f + static_cast<float>(id), 40.0f, false));
      }
    }
    rig->engine.play();
    rig->run(frame_of(2 * kStep, tempo));
    if (slews) {
      CHECK(rig->seq.slewing() == 0);  // 40 ms are long over
      // The morph is done: it stores the values (these writes are the main loop's, one event each).
      stores_at = rig->now();
      for (unsigned id = 0; id < 20; ++id) {
        rig->engine.set_param(rig->vid, static_cast<std::uint16_t>(id), 90.0f - static_cast<float>(id));
      }
      rig->seq.update();
    }
    rig->run(frame_of(24 * kStep, tempo) - rig->now());
    return rig;
  };
  const auto without = run(false);
  const auto with = run(true);
  CHECK(with->seq.dropped_events() == 0);
  CHECK(with->engine.stats().dropped_events == 0);
  CHECK(with->seq.slew_overflow() == 0);
  // The notes: the same events, the same frames.
  const auto a = only_notes(without->voice.log);
  const auto b = only_notes(with->voice.log);
  REQUIRE(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    CHECK(a[i].frame == b[i].frame);
    CHECK(a[i].kind == b[i].kind);
    CHECK(a[i].note == b[i].note);
  }
  // The slews' share: at most kSlewEvents + kSlewExtraEvents (16) of them in a unit's list in a block while they run.
  std::vector<unsigned> per_block(1 + with->voice.at / 64, 0);
  std::vector<unsigned> dist_block(1 + with->voice.at / 64, 0);
  for (const Rec& e : with->voice.log) {
    if (e.kind == dsp::EventKind::Param && e.param != 0 && e.frame < stores_at) {
      ++per_block[e.frame / 64];
    }
  }
  for (const Rec& e : with->dist.log) {
    if (e.kind == dsp::EventKind::Param) {
      ++dist_block[e.frame / 64];
    }
  }
  for (const unsigned n : per_block) {
    CHECK(n <= 16);
  }
  for (const unsigned n : dist_block) {
    CHECK(n <= 16);
  }
  // Every slew's last event before the stores is its goal; the lock on id 0 later restores the stored goal as well.
  for (unsigned id = 0; id < 20; ++id) {
    const auto ev = only_params(with->voice.log, static_cast<int>(id));
    REQUIRE(!ev.empty());
    CHECK(ev.back().value == doctest::Approx(90.0f - static_cast<float>(id)));
  }
  for (unsigned id = 0; id < 12; ++id) {
    const auto ev = only_params(with->dist.log, static_cast<int>(id));
    REQUIRE(!ev.empty());
    CHECK(ev.back().value == doctest::Approx(10.0f + static_cast<float>(id)));
  }
}

TEST_CASE("more slews than the table holds: the extra ones jump to their goal, stored or not, and are counted") {
  Options o;
  o.wide = true;
  Rig rig(o);
  rig.start();
  for (unsigned id = 0; id < 32; ++id) {
    CHECK(rig.seq.slew(kLaneVoice, static_cast<std::uint8_t>(id), 80.0f, 50.0f, false));
  }
  for (unsigned id = 0; id < 8; ++id) {  // the 33rd .. 40th: store = false, nothing is written on the main side
    CHECK(rig.seq.slew(kLaneDist, static_cast<std::uint8_t>(id), 80.0f, 50.0f, false));
  }
  CHECK(rig.seq.slew(kLaneMixer, kMixerLevel, 0.3f, 50.0f, false));  // the 41st: a mixer field
  rig.run(64);
  CHECK(rig.seq.slewing() == kMaxSlews);
  CHECK(rig.seq.slew_overflow() == 9);
  for (unsigned id = 0; id < 8; ++id) {
    const auto ev = only_params(rig.dist.log, static_cast<int>(id));
    REQUIRE(ev.size() == 1);  // the hook wrote the goal itself, in the first block
    CHECK(ev[0].frame == 0);
    CHECK(ev[0].value == doctest::Approx(80.0f));
    CHECK(rig.engine.param(rig.did, static_cast<std::uint16_t>(id)) == doctest::Approx(50.0f));  // the main side was not touched
  }
  CHECK(rig.spy.seen[0][kMixerLevel] == doctest::Approx(0.3f));
  rig.run(4000);
  CHECK(rig.seq.slewing() == 0);
  CHECK(rig.seq.slew_stop_all());
  rig.run(64);
}

TEST_CASE("a full command ring: the value is set at once and the slew is refused") {
  Rig rig;
  rig.start();
  unsigned refused = 0;
  for (unsigned i = 0; i < 80; ++i) {
    refused += rig.seq.slew(kLaneVoice, AcidVoice::Reso, 0.1f + 0.01f * static_cast<float>(i), 100.0f) ? 0u : 1u;
  }
  CHECK(refused >= 80 - 64 - 1);
  CHECK(rig.engine.param(rig.vid, AcidVoice::Reso) == doctest::Approx(0.1f + 0.01f * 79.0f).epsilon(0.01));
}

// ==== C2: lanes drive their own targets ========================================================================================

TEST_CASE("lane i drives the target of the pattern's table: the voice, the distortion, the delay") {
  Rig rig;
  Pattern p = play_all(8);
  p.lanes[0] = LaneRef{kLaneDist, 1};   // the distortion's TONE
  p.lanes[1] = LaneRef{kLaneDelay, 2};  // the delay's MIX
  p.lanes[2] = LaneRef{kLaneVoice, 5};  // the voice's Wave, not "voice id 2"
  set_plock(p, 0, 1, 200);
  set_plock(p, 1, 1, 30);
  set_plock(p, 2, 1, 254);
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.set_param(rig.did, 1, 2500.0f);  // the knobs
  rig.engine.set_param(rig.lid, 2, 0.7f);
  rig.engine.set_param(rig.vid, 5, 0.0f);
  rig.seq.update();
  rig.engine.play();
  rig.run(frame_of(4 * kStep, kTempo));

  const auto d = only_params(rig.dist.log, 1);
  const auto l = only_params(rig.delay.log, 2);
  const auto v = only_params(rig.voice.log, 5);
  REQUIRE(d.size() >= 2);
  REQUIRE(l.size() >= 2);
  REQUIRE(v.size() >= 2);
  // Values go through each target's own descriptor.
  const auto find = [](const std::vector<Rec>& ev, std::uint64_t frame) {
    for (const Rec& e : ev) {
      if (e.frame == frame) {
        return e.value;
      }
    }
    return -999.0f;
  };
  CHECK(find(d, frame_of(kStep, kTempo)) == doctest::Approx(point_to_value(kDistParams[1], 200)));
  CHECK(find(l, frame_of(kStep, kTempo)) == doctest::Approx(point_to_value(kDelayParams[2], 30)));
  CHECK(find(v, frame_of(kStep, kTempo)) == doctest::Approx(point_to_value(kVoiceParams[5], 254)));
  // The lock ends with step 2 (no point): each target's own knob comes back, to its own unit.
  CHECK(find(d, frame_of(2 * kStep, kTempo)) == doctest::Approx(2500.0f));
  CHECK(find(l, frame_of(2 * kStep, kTempo)) == doctest::Approx(0.7f));
  CHECK(find(v, frame_of(2 * kStep, kTempo)) == doctest::Approx(0.0f));
  // Nothing went to the voice's ids 0..2 (what the lanes were in M3).
  for (unsigned id = 0; id < 5; ++id) {
    if (id != 5) {
      CHECK(only_params(rig.voice.log, static_cast<int>(id)).empty());
    }
  }
  // And nothing to the other units' other ids.
  CHECK(only_params(rig.dist.log, 0).empty());
  CHECK(only_params(rig.delay.log, 0).empty());
}

TEST_CASE("mixer lanes write slot 0's mixer through set_mix, over the fixed ranges; the knob comes back at the lock's end and at Stop") {
  Rig rig;
  Pattern p = play_all(8);
  p.lanes[0] = LaneRef{kLaneMixer, kMixerSendA};
  p.lanes[1] = LaneRef{kLaneMixer, kMixerPan};
  p.lanes[2] = LaneRef{kLaneMixer, kMixerMute};
  set_plock(p, 7, 1, 127);  // the default lane 7: slot 0's level
  set_plock(p, 0, 2, 254);
  set_plock(p, 1, 3, 0);
  set_plock(p, 2, 4, 254);
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.set_mix(0, dsp::MixField::Level, 0.5f);  // the knobs
  rig.engine.set_mix(0, dsp::MixField::SendA, 0.3f);
  rig.seq.update();
  rig.engine.play();
  const auto at = [&](std::uint64_t tick) {  // the mixer's targets in the block that holds `tick`
    return rig.spy.seen[frame_of(tick, kTempo) / 64];
  };
  rig.run(frame_of(6 * kStep, kTempo));
  CHECK(at(0)[kMixerLevel] == doctest::Approx(0.5f));
  CHECK(at(kStep)[kMixerLevel] == doctest::Approx(0.75f));       // point 127 over 0..1.5
  CHECK(at(2 * kStep)[kMixerLevel] == doctest::Approx(0.5f));    // the knob is back
  CHECK(at(2 * kStep)[kMixerSendA] == doctest::Approx(1.5f));    // point 254
  CHECK(at(3 * kStep)[kMixerSendA] == doctest::Approx(0.3f));
  CHECK(at(3 * kStep)[kMixerPan] == doctest::Approx(-1.0f));     // point 0 over -1..1
  CHECK(at(4 * kStep)[kMixerPan] == doctest::Approx(0.0f));      // the default knob
  CHECK(at(4 * kStep)[kMixerMute] == 1.0f);                      // point 254 over 0..1: muted
  CHECK(at(5 * kStep)[kMixerMute] == 0.0f);
  // Stop inside a lock hands the knobs back.
  Rig stop;
  stop.bank->replace(0, p);
  stop.start();
  stop.engine.set_mix(0, dsp::MixField::Level, 0.5f);
  stop.seq.update();
  stop.engine.play();
  stop.run(frame_of(kStep, kTempo) + 500);
  CHECK(stop.spy.seen.back()[kMixerLevel] == doctest::Approx(0.75f));
  stop.engine.stop();
  stop.run(64);
  CHECK(stop.spy.seen.back()[kMixerLevel] == doctest::Approx(0.5f));
}

TEST_CASE("the default lanes are the SOUND page: lane i is the voice's id i for 0..6, and the level on lane 7") {
  Rig rig;
  Pattern p = play_all(8);
  for (unsigned lane = 0; lane < 7; ++lane) {
    set_plock(p, lane, 1, 200);
  }
  set_plock(p, 7, 1, 100);
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(2 * kStep, kTempo));
  for (unsigned lane = 0; lane < 7; ++lane) {
    const auto ev = only_params(rig.voice.log, static_cast<int>(lane));
    REQUIRE(!ev.empty());
    CHECK(ev.front().value == doctest::Approx(point_to_value(kVoiceParams[lane], 200)));
  }
  CHECK(only_params(rig.voice.log, 7).empty());  // M3's lane 7 (Volume) is the mixer now
  CHECK(rig.spy.seen[frame_of(kStep, kTempo) / 64][kMixerLevel] == doctest::Approx(mixer_point_to_value(kMixerLevel, 100)));
}

TEST_CASE("a lane with no target, an unknown one, an id past the table, or a unit not given to setup() is silent") {
  Options o;
  o.dist = false;
  Rig rig(o);
  Pattern p = play_all(8);
  p.lanes[0] = LaneRef{kLaneNone, 0};
  p.lanes[1] = LaneRef{9, 0};
  p.lanes[2] = LaneRef{kLaneVoice, 200};
  p.lanes[3] = LaneRef{kLaneDist, 0};     // the distortion is not there
  p.lanes[4] = LaneRef{kLaneDelay, 9};    // past the delay's three parameters
  p.lanes[5] = LaneRef{kLaneMixer, 5};    // past the mixer's fields
  for (unsigned lane = 0; lane < 6; ++lane) {
    set_plock(p, lane, 1, 100);
  }
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(4 * kStep, kTempo));
  CHECK(only_params(rig.voice.log).empty());
  CHECK(only_params(rig.dist.log).empty());
  CHECK(only_params(rig.delay.log).empty());
  CHECK(rig.seq.dropped_events() == 0);
  CHECK(only_notes(rig.voice.log).size() >= 8);  // and the music plays
}

TEST_CASE("a lane re-pointed under a running lock gives the old target its knob back and writes the new one") {
  Rig rig;
  Pattern p = play_all(8);
  set_plock(p, 0, 1, 200);
  set_plock(p, 0, 2, 220);
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.set_param(rig.did, 0, 0.9f);
  rig.seq.update();
  rig.engine.play();
  rig.run(frame_of(kStep, kTempo) + 100);  // inside step 1, the lock is on the voice's cutoff
  CHECK(rig.bank->edit(0, [](Pattern& q) { q.lanes[0] = LaneRef{kLaneDist, 0}; }));
  rig.run(frame_of(3 * kStep, kTempo) - rig.now());
  // The next motion point (step 1, quarter 1) sees the new table: the voice's cutoff is released ...
  const auto v = only_params(rig.voice.log, AcidVoice::Cutoff);
  REQUIRE(v.size() >= 2);
  CHECK(v.back().frame == frame_of(kStep + 60, kTempo));
  CHECK(v.back().value == doctest::Approx(kVoiceParams[AcidVoice::Cutoff].def));
  // ... and the distortion gets the lock's point there, then step 2's, and its own knob back when the lock ends at step 3.
  std::vector<Rec> d;
  for (const Rec& e : only_params(rig.dist.log, 0)) {
    if (e.frame > 0) {
      d.push_back(e);  // (frame 0 is the knob written by the main loop above)
    }
  }
  REQUIRE(d.size() == 2);
  CHECK(d[0].frame == frame_of(kStep + 60, kTempo));
  CHECK(d[0].value == doctest::Approx(point_to_value(kDistParams[0], 200)));
  CHECK(d[1].frame == frame_of(2 * kStep, kTempo));
  CHECK(d[1].value == doctest::Approx(point_to_value(kDistParams[0], 220)));
  rig.run(frame_of(4 * kStep, kTempo) - rig.now());
  const auto after = only_params(rig.dist.log, 0);
  CHECK(after.back().frame == frame_of(3 * kStep, kTempo));
  CHECK(after.back().value == doctest::Approx(0.9f));
}

TEST_CASE("record_motion writes the point for the lane's own target") {
  Rig rig;
  Pattern p = play_all(16);
  p.lanes[1] = LaneRef{kLaneDist, 1};
  rig.bank->replace(0, p);
  rig.start();
  rig.engine.play();
  rig.run(frame_of(3 * kStep + 130, kTempo) + 64);  // step 3, quarter 2
  REQUIRE(rig.seq.playhead().step == 3);
  REQUIRE(rig.seq.playhead().point == 2);
  CHECK(rig.seq.record_motion(1, 3000.0f));  // the distortion's TONE in Hz
  CHECK(rig.bank->pattern(0).motion[1][3 * 4 + 2] == value_to_point(kDistParams[1], 3000.0f));
  CHECK(rig.seq.record_motion(7, 0.75f));    // the default lane 7: the mixer's level, linear
  CHECK(rig.bank->pattern(0).motion[7][3 * 4 + 2] == 127);
  CHECK(rig.bank->edit(0, [](Pattern& q) { q.lanes[2] = LaneRef{kLaneNone, 0}; }));
  CHECK_FALSE(rig.seq.record_motion(2, 1.0f));  // no target
}

TEST_CASE("setup() forgets what a previous run left: slews, the pass counter, the fill") {
  Rig rig;
  rig.start();
  CHECK(rig.seq.slew(kLaneVoice, AcidVoice::Reso, 0.9f, 500.0f));
  rig.run(256);
  CHECK(rig.seq.slewing() == 1);
  rig.seq.set_fill(true);
  // A new engine, as the simulator's tests do (the app object outlives a run).
  dsp::Engine second;
  Probe voice2(dsp::UnitKind::Instrument, kVoiceParams, kVoiceParamCount);
  const dsp::UnitId id = second.attach(voice2);
  REQUIRE(second.set_source(0, id));
  REQUIRE(rig.seq.setup(second, id));
  second.start();
  float l[64];
  float r[64];
  second.render(nullptr, nullptr, l, r, 64);
  CHECK(rig.seq.slewing() == 0);
  CHECK(only_params(voice2.log).empty());
}
