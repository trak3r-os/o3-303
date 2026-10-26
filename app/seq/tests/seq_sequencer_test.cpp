// SPDX-License-Identifier: GPL-3.0-or-later
// The sequencer (o3_303/seq/sequencer.h) against the engine: a recording stand-in for the voice sees every event with its absolute
// frame. Directed tests for each semantic (lengths, swing, gate, slide, tie, rest, accent, transposes, chain, motion ...) and a
// property test of random patterns against an independent model of the same rules.
#include <doctest/doctest.h>

#include <o3_303/seq/bank.h>
#include <o3_303/seq/ops.h>
#include <o3_303/seq/rng.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/tb303/tb303.h>
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

// ---- The voice stand-in -----------------------------------------------------------------------------------------------------

using core::ParamCurve;
using core::ParamDesc;
using core::ParamUnit;

// The voice's real descriptors (units/tb303: Cutoff is a Table curve, Decay a log, Wave a choice, ...): what the motion lanes map onto.
const ParamDesc* const kVoiceParams = [] {
  static Tb303 voice;
  return voice.info().params;
}();

struct Rec {
  std::uint64_t frame;  // absolute, since Play
  dsp::EventKind kind;
  std::uint8_t note;
  std::uint8_t velocity;
  std::uint8_t flags;
  std::uint16_t param;
  float value;
};

class Voice final : public dsp::Unit {
 public:
  explicit Voice(std::uint16_t params = 8) : info_{"VOICE", dsp::UnitKind::Instrument, kVoiceParams, params, nullptr, 0} {}
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
      a.out_l[i] = 0.0f;
      a.out_r[i] = 0.0f;
    }
    at += a.frames;
  }
  std::vector<Rec> log;
  std::uint64_t at = 0;

 private:
  dsp::UnitInfo info_;
};

// Everything an app has: an engine with the voice on slot 0, a bank and the sequencer hook.
struct Rig {
  dsp::Engine engine;
  Voice voice;
  std::unique_ptr<Bank> bank;
  Sequencer seq;
  dsp::UnitId id = dsp::kNoUnit;

  Rig() : bank(std::make_unique<Bank>()), seq(init(*bank)) {}

  static Bank& init(Bank& b) {
    b.init();
    return b;
  }

  void start(std::uint32_t tempo_mbpm = 120'000) {
    id = engine.attach(voice);
    REQUIRE(engine.set_source(0, id));
    REQUIRE(seq.setup(engine, id));
    engine.start();
    engine.set_tempo(tempo_mbpm);
  }

  // Renders `frames` frames in blocks of at most `block` frames.
  void run(std::uint64_t frames, unsigned block = 64) {
    float l[dsp::kMaxBlockFrames];
    float r[dsp::kMaxBlockFrames];
    while (frames > 0) {
      const auto n = static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, block));
      engine.render(nullptr, nullptr, l, r, n);
      frames -= n;
    }
  }

  // Note events only.
  std::vector<Rec> notes() const {
    std::vector<Rec> out;
    for (const Rec& e : voice.log) {
      if (e.kind == dsp::EventKind::NoteOn || e.kind == dsp::EventKind::NoteOff) {
        out.push_back(e);
      }
    }
    return out;
  }
  std::vector<Rec> params() const {
    std::vector<Rec> out;
    for (const Rec& e : voice.log) {
      if (e.kind == dsp::EventKind::Param) {
        out.push_back(e);
      }
    }
    return out;
  }
};

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

// The frame at which the transport reaches whole tick `tick` (frame f owns the positions up to f x inc).
std::uint64_t frame_of(std::uint64_t tick, std::uint32_t tempo_mbpm) {
  const std::uint64_t inc = dsp::ticks_per_sample_q32(tempo_mbpm);
  return ((tick << 32) + inc - 1u) / inc;
}

constexpr std::uint32_t kStep = dsp::kTicksPerSixteenth;  // 240 ticks

}  // namespace

// ---- Directed tests -----------------------------------------------------------------------------------------------------------

TEST_CASE("setup needs a voice with the eight lanes and adds the hook") {
  Rig few;
  Voice small(4);
  const dsp::UnitId id = few.engine.attach(small);
  CHECK_FALSE(few.seq.setup(few.engine, id));
  CHECK_FALSE(few.seq.setup(few.engine, dsp::kNoUnit));
  Rig rig;
  rig.start();
  CHECK(rig.id == 0);
}

TEST_CASE("it plays the selected pattern from step 0 at the press of Play; nothing before") {
  Rig rig;
  rig.bank->replace(3, play_all(16));
  rig.bank->select(3);
  rig.start();
  rig.run(20000);
  CHECK(rig.voice.log.empty());
  CHECK_FALSE(rig.seq.playhead().playing);
  CHECK(rig.seq.playhead().step == -1);
  const std::uint64_t pressed = rig.voice.at;
  rig.engine.play();
  rig.run(64);
  const auto n = rig.notes();
  REQUIRE(n.size() >= 1);
  CHECK(n[0].kind == dsp::EventKind::NoteOn);
  CHECK(n[0].note == 40);
  CHECK(n[0].frame == pressed);  // frame 0 of the block after the press
  const Playhead ph = rig.seq.playhead();
  CHECK(ph.playing);
  CHECK(ph.pattern == 3);
  CHECK(ph.step == 0);
  CHECK(ph.sounding);
}

TEST_CASE("steps and gates land on their ticks, and the pattern wraps at 8, 16, 32 and 64 steps") {
  for (const unsigned length : {8u, 16u, 32u, 64u}) {
    CAPTURE(length);
    Rig rig;
    rig.bank->replace(0, play_all(length));
    rig.start(120'000);
    rig.engine.play();
    const unsigned steps = 2 * length + 2;
    rig.run(frame_of(static_cast<std::uint64_t>(steps) * kStep, 120'000));
    const auto n = rig.notes();
    REQUIRE(n.size() >= 2u * (2 * length + 1));
    for (unsigned k = 0; k < 2 * length + 1; ++k) {
      const Rec& on = n[2 * k];
      const Rec& off = n[2 * k + 1];
      CHECK(on.kind == dsp::EventKind::NoteOn);
      CHECK(on.note == 40 + (k % length) % 40);  // the loop starts over at step `length`
      CHECK(on.frame == frame_of(static_cast<std::uint64_t>(k) * kStep, 120'000));
      CHECK(off.kind == dsp::EventKind::NoteOff);
      CHECK(off.note == on.note);
      CHECK(off.frame == frame_of(static_cast<std::uint64_t>(k) * kStep + kStep / 2, 120'000));  // gate 50 %
    }
    CHECK(rig.seq.playhead().step == static_cast<int>((steps - 1) % length));
  }
}

TEST_CASE("at 120 BPM a sixteenth is 5512.5 frames: the frames are exact, not rounded to the block") {
  Rig rig;
  rig.bank->replace(0, play_all(8));
  rig.start(120'000);
  rig.engine.play();
  rig.run(30000);
  const auto n = rig.notes();
  REQUIRE(n.size() >= 8);
  // Exact values in frames (the transport's increment is an integer, so the frame is the exact one rounded up, give or take one).
  const auto near = [](std::uint64_t got, std::uint64_t exact) { return got >= exact && got <= exact + 1; };
  CHECK(n[0].frame == 0);
  CHECK(near(n[2].frame, 5513));   // 5512.5
  CHECK(near(n[4].frame, 11025));
  CHECK(near(n[6].frame, 16538));  // 16537.5
  CHECK(near(n[1].frame, 2757));   // the gate: 120 ticks = 2756.25 frames
  CHECK(n[2].frame - n[0].frame > 5510);
  CHECK(n[2].frame - n[0].frame < 5515);
}

TEST_CASE("swing delays the odd steps and shortens them: the pair still adds up to two sixteenths") {
  Rig rig;
  Pattern p = play_all(16);
  set_swing(p, 50);  // half a step: the odd step starts 60 ticks late
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(8 * kStep, 120'000));
  const auto n = rig.notes();
  REQUIRE(n.size() >= 16);
  // sw = 50 % x 120 ticks = 60: even steps 300 ticks long, odd ones 180; gate 50 % of each.
  const std::uint64_t starts[] = {0, 300, 480, 780, 960, 1260, 1440, 1740};
  const std::uint64_t gates[] = {150, 90, 150, 90, 150, 90, 150, 90};
  for (unsigned k = 0; k < 8; ++k) {
    CAPTURE(k);
    CHECK(n[2 * k].kind == dsp::EventKind::NoteOn);
    CHECK(n[2 * k].frame == frame_of(starts[k], 120'000));
    CHECK(n[2 * k + 1].kind == dsp::EventKind::NoteOff);
    CHECK(n[2 * k + 1].frame == frame_of(starts[k] + gates[k], 120'000));
  }
  // The even steps are on the grid again.
  CHECK(n[4].frame - n[0].frame == frame_of(480, 120'000));

  // The maximum swing (66 % -> 79 ticks) and none.
  Rig full;
  Pattern q = play_all(16);
  set_swing(q, kMaxSwing);
  full.bank->replace(0, q);
  full.start(120'000);
  full.engine.play();
  full.run(frame_of(4 * kStep, 120'000));
  const auto m = full.notes();
  REQUIRE(m.size() >= 4);
  CHECK(m[2].frame == frame_of(kStep + 79, 120'000));
}

TEST_CASE("the gate is a percent of the step's own length") {
  struct Case { unsigned gate; unsigned swing; std::uint64_t off_tick; };
  const Case cases[] = {
      {50, 0, 120}, {80, 0, 192}, {5, 0, 12}, {100, 0, 240},
      {50, 50, 150},   // step 0 is even: 300 ticks long, half of it
  };
  for (const Case& c : cases) {
    CAPTURE(c.gate);
    CAPTURE(c.swing);
    Rig rig;
    Pattern p = play_all(8);
    set_gate(p, static_cast<int>(c.gate));
    set_swing(p, static_cast<int>(c.swing));
    rig.bank->replace(0, p);
    rig.start(120'000);
    rig.engine.play();
    rig.run(frame_of(kStep, 120'000) + 64);
    const auto n = rig.notes();
    REQUIRE(n.size() >= 2);
    CHECK(n[0].kind == dsp::EventKind::NoteOn);
    CHECK(n[1].kind == dsp::EventKind::NoteOff);
    CHECK(n[1].frame == frame_of(c.off_tick, 120'000));
  }
}

TEST_CASE("a full gate ends the note before the next one starts, at the same frame, without a glide") {
  Rig rig;
  Pattern p = play_all(8);
  set_gate(p, 100);
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(3 * kStep, 120'000));
  const auto n = rig.notes();
  REQUIRE(n.size() >= 5);
  CHECK(n[0].kind == dsp::EventKind::NoteOn);
  CHECK(n[1].kind == dsp::EventKind::NoteOff);  // the off first ...
  CHECK(n[2].kind == dsp::EventKind::NoteOn);   // ... then the next on, same frame
  CHECK(n[1].frame == n[2].frame);
  CHECK(n[2].flags == 0);  // no kNoteSlide
}

TEST_CASE("a slide: the next note-on carries kNoteSlide and comes before the held note's note-off, at the same frame") {
  Rig rig;
  Pattern p = play_all(8);
  p.steps[0].note = 40;
  set_flag(p, 0, kStepSlide, true);
  p.steps[1].note = 47;
  set_flag(p, 1, kStepAccent, true);
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(3 * kStep, 120'000));
  const auto n = rig.notes();
  REQUIRE(n.size() >= 6);
  // Step 0: on, and no gate-off of its own: it is held into step 1.
  CHECK(n[0].kind == dsp::EventKind::NoteOn);
  CHECK(n[0].note == 40);
  CHECK(n[0].flags == 0);
  CHECK(n[1].kind == dsp::EventKind::NoteOn);  // step 1 (the glide target), straight after
  CHECK(n[1].note == 47);
  CHECK(n[1].frame == frame_of(kStep, 120'000));
  CHECK(n[1].flags == (dsp::kNoteSlide | dsp::kNoteAccent));
  CHECK(n[1].velocity == 110);
  CHECK(n[2].kind == dsp::EventKind::NoteOff);
  CHECK(n[2].note == 40);
  CHECK(n[2].frame == n[1].frame);  // the same frame, after the on
  CHECK(n[3].kind == dsp::EventKind::NoteOff);  // step 1's own gate end
  CHECK(n[3].note == 47);
  CHECK(n[3].frame == frame_of(kStep + kStep / 2, 120'000));
  CHECK(n[4].kind == dsp::EventKind::NoteOn);  // step 2 is a plain note again
  CHECK(n[4].flags == 0);
}

TEST_CASE("a slide to the same note does not release it") {
  Rig rig;
  Pattern p = play_all(8);
  p.steps[0].note = 45;
  p.steps[1].note = 45;
  set_flag(p, 0, kStepSlide, true);
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(2 * kStep, 120'000) - 1);
  const auto n = rig.notes();
  REQUIRE(n.size() == 3);
  CHECK(n[0].kind == dsp::EventKind::NoteOn);
  CHECK(n[1].kind == dsp::EventKind::NoteOn);
  CHECK(n[1].flags == dsp::kNoteSlide);
  CHECK(n[2].kind == dsp::EventKind::NoteOff);  // only the gate of step 1
  CHECK(n[2].frame == frame_of(kStep + kStep / 2, 120'000));
}

TEST_CASE("a slide into a rest releases at the rest; a slide on the last step glides into step 0 of the loop") {
  Rig rig;
  Pattern p = play_all(8);
  set_flag(p, 2, kStepSlide, true);
  set_flag(p, 3, kStepRest, true);
  set_flag(p, 7, kStepSlide, true);
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(9 * kStep, 120'000));
  const auto n = rig.notes();
  // Find the release of step 2's note: at step 3's start.
  bool found = false;
  for (const Rec& e : n) {
    if (e.kind == dsp::EventKind::NoteOff && e.note == p.steps[2].note) {
      CHECK(e.frame == frame_of(3 * kStep, 120'000));
      found = true;
    }
  }
  CHECK(found);
  // Step 7 slides: the loop's step 0 comes with kNoteSlide.
  for (std::size_t i = 0; i < n.size(); ++i) {
    if (n[i].kind == dsp::EventKind::NoteOn && n[i].frame == frame_of(8 * kStep, 120'000)) {
      CHECK(n[i].flags == dsp::kNoteSlide);
      CHECK(n[i].note == p.steps[0].note);
      REQUIRE(i + 1 < n.size());
      CHECK(n[i + 1].kind == dsp::EventKind::NoteOff);
      CHECK(n[i + 1].note == p.steps[7].note);
    }
  }
}

TEST_CASE("a rest plays nothing and leaves the previous note to its own gate") {
  Rig rig;
  Pattern p = play_all(8);
  set_flag(p, 1, kStepRest, true);
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(3 * kStep, 120'000));
  const auto n = rig.notes();
  REQUIRE(n.size() >= 4);
  CHECK(n[0].kind == dsp::EventKind::NoteOn);   // step 0
  CHECK(n[1].kind == dsp::EventKind::NoteOff);  // its gate
  CHECK(n[2].kind == dsp::EventKind::NoteOn);   // step 2: nothing for step 1
  CHECK(n[2].frame == frame_of(2 * kStep, 120'000));
}

TEST_CASE("a tie carries the note through the step and ends with the gate of the last tied step") {
  Rig rig;
  Pattern p = play_all(8);
  set_flag(p, 1, kStepTie, true);
  set_flag(p, 2, kStepTie, true);
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(4 * kStep, 120'000));
  const auto n = rig.notes();
  REQUIRE(n.size() >= 4);
  CHECK(n[0].kind == dsp::EventKind::NoteOn);
  CHECK(n[0].frame == 0);
  CHECK(n[1].kind == dsp::EventKind::NoteOff);  // after step 2's gate: 3 steps - half a step
  CHECK(n[1].note == p.steps[0].note);
  CHECK(n[1].frame == frame_of(2 * kStep + kStep / 2, 120'000));
  CHECK(n[2].kind == dsp::EventKind::NoteOn);  // step 3
  CHECK(n[2].note == p.steps[3].note);
  CHECK(n[2].frame == frame_of(3 * kStep, 120'000));
  // A tie with nothing before it is silent; a slide flag on a tie holds the note into the next step.
  Rig quiet;
  Pattern q = play_all(8);
  set_flag(q, 0, kStepRest, true);
  set_flag(q, 1, kStepTie, true);
  quiet.bank->replace(0, q);
  quiet.start(120'000);
  quiet.engine.play();
  quiet.run(frame_of(2 * kStep, 120'000) - 1);
  CHECK(quiet.notes().empty());

  Rig glide;
  Pattern g = play_all(8);
  set_flag(g, 1, kStepTie, true);
  set_flag(g, 1, kStepSlide, true);
  glide.bank->replace(0, g);
  glide.start(120'000);
  glide.engine.play();
  glide.run(frame_of(3 * kStep, 120'000));
  const auto m = glide.notes();
  REQUIRE(m.size() >= 3);
  CHECK(m[0].kind == dsp::EventKind::NoteOn);
  CHECK(m[1].kind == dsp::EventKind::NoteOn);  // step 2 glides from the held note
  CHECK(m[1].flags == dsp::kNoteSlide);
  CHECK(m[2].kind == dsp::EventKind::NoteOff);
  CHECK(m[2].note == g.steps[0].note);
}

TEST_CASE("a tie across the loop: the last step ties into step 0") {
  Rig rig;
  Pattern p = play_all(8);
  set_flag(p, 0, kStepTie, true);
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(9 * kStep, 120'000));
  const auto n = rig.notes();
  // Step 7 holds on (a tie follows it, around the loop) so the second pass has no note-on at step 0 and the note goes on.
  unsigned ons_at_8 = 0;
  for (const Rec& e : n) {
    ons_at_8 += (e.kind == dsp::EventKind::NoteOn && e.frame == frame_of(8 * kStep, 120'000)) ? 1u : 0u;
  }
  CHECK(ons_at_8 == 0);
}

TEST_CASE("accent: velocity 110 and kNoteAccent; the others 80 and no flag") {
  Rig rig;
  Pattern p = play_all(8);
  set_flag(p, 1, kStepAccent, true);
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(3 * kStep, 120'000));
  const auto n = rig.notes();
  REQUIRE(n.size() >= 5);
  CHECK(n[0].velocity == 80);
  CHECK(n[0].flags == 0);
  CHECK(n[2].kind == dsp::EventKind::NoteOn);
  CHECK(n[2].velocity == 110);
  CHECK(n[2].flags == dsp::kNoteAccent);
}

TEST_CASE("the pattern's transpose and the live transpose add to the notes; the note-off matches the note-on") {
  Rig rig;
  Pattern p = play_all(8);
  p.transpose = 5;
  p.steps[2].note = 125;  // 125 + 5 - 12 = 118
  p.steps[3].note = 2;    // 2 + 5 - 12 < 0 clamps to 0
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(kStep / 4, 120'000));  // inside step 0's note (its gate ends at 120 ticks)
  rig.seq.set_live_transpose(-12);
  CHECK(rig.seq.live_transpose() == -12);
  rig.run(frame_of(4 * kStep, 120'000) - frame_of(kStep / 4, 120'000));
  const auto n = rig.notes();
  REQUIRE(n.size() >= 8);
  CHECK(n[0].kind == dsp::EventKind::NoteOn);
  CHECK(n[0].note == 40 + 5);  // step 0 sounds with the pattern transpose only
  CHECK(n[1].kind == dsp::EventKind::NoteOff);
  CHECK(n[1].note == 45);  // and ends the note it started, not the transposed number
  CHECK(n[2].note == 41 + 5 - 12);
  CHECK(n[3].note == 41 + 5 - 12);
  CHECK(n[4].note == 118);
  CHECK(n[6].note == 0);
  CHECK(n[7].note == 0);
}

TEST_CASE("the live transpose clamps to the MIDI range and to +-48") {
  Rig rig;
  Pattern p = play_all(8);
  p.steps[0].note = 120;
  p.steps[1].note = 5;
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.seq.set_live_transpose(500);
  CHECK(rig.seq.live_transpose() == 48);
  rig.engine.play();
  rig.run(frame_of(2 * kStep, 120'000) - 1);
  auto n = rig.notes();
  CHECK(n[0].note == 127);  // 120 + 48 clamps
  rig.seq.set_live_transpose(-500);
  CHECK(rig.seq.live_transpose() == -48);
}

TEST_CASE("a queued pattern starts when the playing one ends; then it repeats; the chain follows the queue") {
  Rig rig;
  Pattern a = play_all(8);
  Pattern b = play_all(8);
  Pattern c = play_all(8);
  for (unsigned j = 0; j < 8; ++j) {
    b.steps[j].note = static_cast<std::uint8_t>(80 + j);
    c.steps[j].note = static_cast<std::uint8_t>(100 + j);
  }
  rig.bank->replace(0, a);
  rig.bank->replace(1, b);
  rig.bank->replace(2, c);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(3 * kStep, 120'000));
  CHECK(rig.seq.playhead().pattern == 0);
  CHECK(rig.seq.playhead().queued == -1);
  CHECK(rig.seq.queue(1));
  CHECK(rig.seq.queue(2));
  rig.run(64);
  CHECK(rig.seq.playhead().queued == 1);
  CHECK(rig.seq.playhead().queue_length == 2);
  ChainView v = rig.seq.chain();
  CHECK(v.length == 2);
  CHECK(v.pattern[0] == 1);
  CHECK(v.pattern[1] == 2);
  CHECK(v.position == -1);  // the playing pattern is not from the chain
  CHECK(v.next == 1);
  CHECK(v.next_entry == 0);
  CHECK_FALSE(v.loop);
  // Still pattern 0 until step 8.
  rig.run(frame_of(8 * kStep, 120'000) - frame_of(3 * kStep, 120'000) - 64 - 1);
  CHECK(rig.seq.playhead().pattern == 0);
  rig.run(8);
  CHECK(rig.seq.playhead().pattern == 1);
  CHECK(rig.seq.playhead().step == 0);
  CHECK(rig.seq.playhead().queued == 2);
  CHECK(rig.seq.playhead().queue_length == 1);
  v = rig.seq.chain();
  CHECK(v.position == 0);
  CHECK(v.next == 2);
  CHECK(v.next_entry == 1);
  rig.run(frame_of(16 * kStep, 120'000) - frame_of(8 * kStep, 120'000));
  CHECK(rig.seq.playhead().pattern == 2);
  CHECK(rig.seq.playhead().queued == -1);
  CHECK(rig.seq.playhead().queue_length == 0);
  v = rig.seq.chain();
  CHECK(v.position == 1);  // the chain is used up: pattern 2 repeats
  CHECK(v.next == -1);
  rig.run(frame_of(40 * kStep, 120'000) - frame_of(16 * kStep, 120'000));
  CHECK(rig.seq.playhead().pattern == 2);

  // The notes: pattern 0 steps 0-7, pattern 1 steps 0-7, then pattern 2 for ever, no gap in the grid.
  const auto n = rig.notes();
  unsigned k = 0;
  for (const Rec& e : n) {
    if (e.kind != dsp::EventKind::NoteOn) {
      continue;
    }
    CHECK(e.frame == frame_of(static_cast<std::uint64_t>(k) * kStep, 120'000));
    const unsigned expect = k < 8 ? 40 + k : (k < 16 ? 80 + (k - 8) : 100 + (k - 16) % 8);
    CHECK(e.note == expect);
    ++k;
  }
  CHECK(k >= 40);
}

TEST_CASE("the chain loops when asked, and clear_queue empties it") {
  Rig rig;
  for (unsigned i = 0; i < 3; ++i) {
    Pattern p = play_all(8);
    for (unsigned j = 0; j < 8; ++j) {
      p.steps[j].note = static_cast<std::uint8_t>(30 + 10 * i + j);
    }
    rig.bank->replace(i, p);
  }
  rig.start(120'000);
  rig.engine.play();
  rig.seq.queue(1);
  rig.seq.queue(2);
  rig.seq.set_chain_loop(true);
  rig.run(frame_of(8 * kStep, 120'000) + 64);
  CHECK(rig.seq.chain().loop);
  CHECK(rig.seq.playhead().pattern == 1);
  rig.run(frame_of(16 * kStep, 120'000) - frame_of(8 * kStep, 120'000));
  CHECK(rig.seq.playhead().pattern == 2);
  CHECK(rig.seq.playhead().queued == 1);  // the wrap
  CHECK(rig.seq.chain().next_entry == 0);
  rig.run(frame_of(24 * kStep, 120'000) - frame_of(16 * kStep, 120'000));
  CHECK(rig.seq.playhead().pattern == 1);
  CHECK(rig.seq.chain().position == 0);
  rig.run(frame_of(32 * kStep, 120'000) - frame_of(24 * kStep, 120'000));
  CHECK(rig.seq.playhead().pattern == 2);

  rig.seq.clear_queue();
  rig.run(64);
  CHECK(rig.seq.chain().length == 0);
  CHECK(rig.seq.playhead().queued == -1);
  rig.run(frame_of(48 * kStep, 120'000) - frame_of(32 * kStep, 120'000));
  CHECK(rig.seq.playhead().pattern == 2);  // nothing queued: it repeats
}

TEST_CASE("the chain holds 16 entries; a full chain drops what has been played") {
  Rig rig;
  for (unsigned i = 0; i < 20; ++i) {
    rig.bank->replace(i, play_all(8));
  }
  rig.start(120'000);
  rig.engine.play();
  for (unsigned i = 1; i <= 17; ++i) {
    CHECK(rig.seq.queue(i));  // the command is accepted; the 17th entry does not fit
  }
  rig.run(64);
  ChainView v = rig.seq.chain();
  CHECK(v.length == kMaxQueue);
  CHECK(v.pattern[15] == 16);  // 17 was refused
  // Play three patterns of it, then there is room for the played ones' places.
  rig.run(frame_of(3 * 8 * kStep, 120'000) + 64);
  v = rig.seq.chain();
  CHECK(v.position == 2);
  rig.seq.queue(17);
  rig.run(64);
  v = rig.seq.chain();
  CHECK(v.length == kMaxQueue - 2 + 1);  // 16 - the two played ones + the new entry
  CHECK(v.pattern[v.length - 1] == 17);
  CHECK(v.pattern[0] == 3);  // the first two (done) went; the playing one (entry 2 = pattern 3) stays
  CHECK(v.position == 0);
  CHECK_FALSE(rig.seq.queue(48));
  CHECK_FALSE(rig.seq.queue(1000));
}

TEST_CASE("jump starts a pattern at the next step; the chain goes on after it; stopped it only selects") {
  Rig rig;
  for (unsigned i = 0; i < 4; ++i) {
    Pattern p = play_all(16);
    for (unsigned j = 0; j < 16; ++j) {
      p.steps[j].note = static_cast<std::uint8_t>(30 + 10 * i + j);
    }
    rig.bank->replace(i, p);
  }
  rig.start(120'000);
  CHECK(rig.seq.jump(2));  // stopped: only selects
  CHECK(rig.bank->selected() == 2);
  rig.engine.play();
  rig.run(frame_of(3 * kStep, 120'000));
  CHECK(rig.seq.playhead().pattern == 2);
  rig.seq.queue(3);
  CHECK(rig.seq.jump(1));
  rig.run(frame_of(5 * kStep, 120'000) - frame_of(3 * kStep, 120'000));
  CHECK(rig.seq.playhead().pattern == 1);
  // The jump took effect at the next step (3): pattern 1's step 0, 1, ...
  const auto n = rig.notes();
  unsigned seen_jump = 0;
  for (const Rec& e : n) {
    if (e.kind == dsp::EventKind::NoteOn && e.frame >= frame_of(3 * kStep, 120'000)) {
      CHECK(e.note == 40 + seen_jump);  // pattern 1: notes 40..
      ++seen_jump;
    }
  }
  CHECK(seen_jump >= 2);
  // At the end of pattern 1 the chain (3) follows.
  rig.run(frame_of(3 * kStep + 16 * kStep, 120'000) - frame_of(5 * kStep, 120'000) + 64);
  CHECK(rig.seq.playhead().pattern == 3);
  CHECK(rig.seq.chain().position == 0);
}

TEST_CASE("an edit of the playing pattern is heard the next time its step comes; other patterns are not read") {
  Rig rig;
  rig.bank->replace(0, play_all(8));
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(7 * kStep, 120'000) + 64);  // step 6 has played
  rig.bank->set_note(0, 6, 99);
  rig.bank->set_note(1, 6, 98);  // another pattern
  rig.run(frame_of(16 * kStep, 120'000) - frame_of(7 * kStep, 120'000));
  unsigned k = 0;
  for (const Rec& e : rig.notes()) {
    if (e.kind != dsp::EventKind::NoteOn) {
      continue;
    }
    if (k == 6) {
      CHECK(e.note == 46);  // the first pass: the original
    }
    if (k == 14) {
      CHECK(e.note == 99);  // the second pass: the edit
    }
    ++k;
  }
  CHECK(k >= 16);
  CHECK(rig.seq.playhead().pattern == 0);
}

TEST_CASE("shrinking the length while playing wraps to step 0 at once") {
  Rig rig;
  rig.bank->replace(0, play_all(16));
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(10 * kStep, 120'000) + 64);  // inside step 10
  rig.bank->set_length(0, 8);
  rig.run(frame_of(12 * kStep, 120'000) - frame_of(10 * kStep, 120'000));
  const auto n = rig.notes();
  std::vector<Rec> ons;
  for (const Rec& e : n) {
    if (e.kind == dsp::EventKind::NoteOn) {
      ons.push_back(e);
    }
  }
  REQUIRE(ons.size() >= 12);
  CHECK(ons[10].note == 40 + 10);  // step 10 had started
  CHECK(ons[11].note == 40);       // step 11 is step 0 of the shorter loop
  CHECK(ons[11].frame == frame_of(11 * kStep, 120'000));
}

TEST_CASE("motion: a p-lock goes to the voice before the step's note, and the knob's value comes back after it") {
  Rig rig;
  Pattern p = play_all(8);
  set_plock(p, 0, 2, 200);  // cutoff locked on step 2
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(5 * kStep, 120'000));
  const auto pr = rig.params();
  REQUIRE(pr.size() == 2);
  CHECK(pr[0].param == 0);
  CHECK(pr[0].frame == frame_of(2 * kStep, 120'000));
  CHECK(pr[0].value == doctest::Approx(point_to_value(kVoiceParams[0], 200)));
  CHECK(pr[1].param == 0);
  CHECK(pr[1].frame == frame_of(3 * kStep, 120'000));
  CHECK(pr[1].value == doctest::Approx(350.0f));  // the knob (its default)
  // Order inside the block of step 2: the param, then the note-on.
  std::size_t at_param = 0;
  std::size_t at_note = 0;
  for (std::size_t i = 0; i < rig.voice.log.size(); ++i) {
    const Rec& e = rig.voice.log[i];
    if (e.frame == frame_of(2 * kStep, 120'000) && e.kind == dsp::EventKind::Param) {
      at_param = i;
    }
    if (e.frame == frame_of(2 * kStep, 120'000) && e.kind == dsp::EventKind::NoteOn) {
      at_note = i;
    }
  }
  CHECK(at_param < at_note);
}

TEST_CASE("motion: four points a step, each at its quarter of the step, only sent when they change") {
  Rig rig;
  Pattern p = play_all(8);
  set_swing(p, 50);
  for (unsigned q = 0; q < 4; ++q) {
    set_motion_point(p, 4, 1, q, static_cast<std::uint8_t>(40 + 60 * q));  // accent lane across step 1 (odd: 180 ticks long)
  }
  set_motion_point(p, 1, 0, 2, 100);  // a single point on lane 1: step 0 quarter 2 (even step: 300 ticks long)
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(4 * kStep, 120'000));
  const auto pr = rig.params();
  // Step 1 starts at 300 and is 180 ticks long: its quarters are at 300, 345, 390, 435. Step 0 (300 long): quarters 0, 75, 150, 225.
  struct Want { std::uint64_t tick; unsigned lane; };
  const Want want[] = {{150, 1}, {225, 1}, {300, 4}, {345, 4}, {390, 4}, {435, 4}, {480, 4}};
  REQUIRE(pr.size() == 7);
  for (std::size_t i = 0; i < 7; ++i) {
    CAPTURE(i);
    CHECK(pr[i].frame == frame_of(want[i].tick, 120'000));
    CHECK(pr[i].param == want[i].lane);
  }
  CHECK(pr[0].value == doctest::Approx(point_to_value(kVoiceParams[1], 100)));
  CHECK(pr[1].value == doctest::Approx(kVoiceParams[1].def));  // the lock ends at step 0's quarter 3: the knob
  for (unsigned q = 0; q < 4; ++q) {
    CHECK(pr[2 + q].value == doctest::Approx(point_to_value(kVoiceParams[4], static_cast<std::uint8_t>(40 + 60 * q))));
  }
  CHECK(pr[6].value == doctest::Approx(kVoiceParams[4].def));  // step 2 quarter 0: back to the knob
}

TEST_CASE("motion: the knob's current value comes back, as handed over by update(); stop restores the lanes") {
  Rig rig;
  Pattern p = play_all(8);
  set_plock(p, 0, 1, 10);
  set_plock(p, 3, 1, 250);
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(kStep, 120'000) + 200);
  CHECK(rig.engine.set_param(rig.id, 0, 900.0f));  // the user turns CUTOFF while it is locked on step 1 ...
  rig.seq.update();                                // ... and the screen hands the knobs over
  rig.run(frame_of(3 * kStep, 120'000) - frame_of(kStep, 120'000) - 200);
  float last0 = -1.0f;
  float last3 = -1.0f;
  for (const Rec& e : rig.params()) {
    if (e.frame >= frame_of(2 * kStep, 120'000) && e.param == 0) last0 = e.value;
    if (e.frame >= frame_of(2 * kStep, 120'000) && e.param == 3) last3 = e.value;
  }
  CHECK(last0 == doctest::Approx(900.0f));
  CHECK(last3 == doctest::Approx(750.0f));  // lane 3's knob was never touched: its default

  // Stop inside a lock: the knob values return at once.
  Rig stop;
  stop.bank->replace(0, p);
  stop.start(120'000);
  stop.engine.play();
  stop.run(frame_of(kStep, 120'000) + 500);
  const std::size_t before = stop.params().size();
  stop.engine.stop();
  stop.run(64);
  const auto after = stop.params();
  REQUIRE(after.size() == before + 2);
  CHECK(after[before].value == doctest::Approx(350.0f));
  CHECK(after[before + 1].value == doctest::Approx(750.0f));
  CHECK_FALSE(stop.seq.playhead().playing);
}

TEST_CASE("Stop releases the held note at once; Play starts over; Play while playing restarts") {
  Rig rig;
  Pattern p = play_all(8);
  set_flag(p, 0, kStepSlide, true);  // step 0's note is held to the end of the step
  rig.bank->replace(0, p);
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(kStep / 2, 120'000));
  CHECK(rig.seq.playhead().sounding);
  const std::uint64_t played = rig.voice.at;
  rig.engine.stop();
  rig.run(64);
  auto n = rig.notes();
  REQUIRE(n.size() >= 2);
  CHECK(n.back().kind == dsp::EventKind::NoteOff);
  CHECK(n.back().note == 40);
  CHECK(n.back().frame == played);  // frame 0 of the first block after the stop
  CHECK_FALSE(rig.seq.playhead().sounding);
  CHECK(rig.seq.playhead().pattern == -1);
  CHECK(rig.seq.playhead().step == -1);
  const std::size_t count = n.size();
  rig.run(20000);
  CHECK(rig.notes().size() == count);  // silence while stopped

  rig.bank->select(0);
  rig.engine.play();
  rig.run(64);
  n = rig.notes();
  CHECK(n.size() == count + 1);
  CHECK(n.back().kind == dsp::EventKind::NoteOn);
  CHECK(n.back().note == 40);
  CHECK(n.back().flags == 0);  // not a glide: the run started clean

  // Play while playing: the held note ends at frame 0 of the restart block, then step 0 starts again.
  rig.run(frame_of(kStep / 2, 120'000));  // step 0 slides: its note is held
  const std::uint64_t at = rig.voice.at;
  rig.engine.play();
  rig.run(64);
  n = rig.notes();
  REQUIRE(n.size() >= 2);
  CHECK(n[n.size() - 2].kind == dsp::EventKind::NoteOff);
  CHECK(n[n.size() - 2].frame == at);
  CHECK(n.back().kind == dsp::EventKind::NoteOn);
  CHECK(n.back().frame == at);
  CHECK(n.back().note == 40);
  CHECK(n.back().flags == 0);
}

TEST_CASE("performance flags: mute, accent all and slide all") {
  Rig rig;
  rig.bank->replace(0, play_all(8));
  rig.start(120'000);
  rig.engine.play();
  rig.seq.set_perf_flag(kPerfAccAll, true);
  CHECK(rig.seq.perf() == kPerfAccAll);
  rig.run(frame_of(2 * kStep, 120'000));
  auto n = rig.notes();
  CHECK(n[0].flags == dsp::kNoteAccent);
  CHECK(n[0].velocity == 110);
  rig.seq.set_perf(0);
  CHECK(rig.seq.perf() == 0);

  // Slide all: every note from the second on glides from the one before; gates do not end the notes.
  Rig sl;
  sl.bank->replace(0, play_all(8));
  sl.start(120'000);
  sl.engine.play();
  sl.seq.set_perf_flag(kPerfSlideAll, true);
  sl.run(frame_of(3 * kStep, 120'000));
  n = sl.notes();
  REQUIRE(n.size() >= 5);
  CHECK(n[0].kind == dsp::EventKind::NoteOn);
  CHECK(n[1].kind == dsp::EventKind::NoteOn);
  CHECK(n[1].flags == dsp::kNoteSlide);
  CHECK(n[2].kind == dsp::EventKind::NoteOff);
  CHECK(n[3].kind == dsp::EventKind::NoteOn);

  // Mute: no new notes; the playhead goes on; the held note is released.
  Rig mu;
  mu.bank->replace(0, play_all(8));
  mu.start(120'000);
  mu.engine.play();
  mu.run(frame_of(kStep / 2, 120'000) - 2);
  mu.seq.set_perf_flag(kPerfMute, true);
  mu.run(frame_of(5 * kStep, 120'000));
  n = mu.notes();
  unsigned ons = 0;
  for (const Rec& e : n) {
    ons += e.kind == dsp::EventKind::NoteOn ? 1u : 0u;
  }
  CHECK(ons == 1);
  CHECK(mu.seq.playhead().step >= 4);
  CHECK_FALSE(mu.seq.playhead().sounding);
  mu.seq.set_perf_flag(kPerfMute, false);
  mu.run(frame_of(8 * kStep, 120'000) - frame_of(5 * kStep, 120'000));
  ons = 0;
  for (const Rec& e : mu.notes()) {
    ons += e.kind == dsp::EventKind::NoteOn ? 1u : 0u;
  }
  CHECK(ons > 1);
}

TEST_CASE("the playhead follows the steps and the quarters") {
  Rig rig;
  rig.bank->replace(0, play_all(16));
  rig.start(120'000);
  rig.engine.play();
  for (unsigned s = 0; s < 20; ++s) {
    // Just after step s has started.
    const std::uint64_t target = frame_of(static_cast<std::uint64_t>(s) * kStep, 120'000) + 70;
    rig.run(target - rig.voice.at);
    CHECK(rig.seq.playhead().step == static_cast<int>(s % 16));
    CHECK(rig.seq.playhead().pattern == 0);
    CHECK(rig.seq.playhead().playing);
  }
  // The quarter: step 3 starts at tick 720; its quarters are 60 ticks (1378 frames) apart.
  Rig q;
  q.bank->replace(0, play_all(16));
  q.start(120'000);
  q.engine.play();
  q.run(frame_of(3 * kStep + 130, 120'000) + 64);
  CHECK(q.seq.playhead().step == 3);
  CHECK(q.seq.playhead().point == 2);
}

TEST_CASE("record_motion writes the point under the playhead, one undo step per lane") {
  Rig rig;
  rig.bank->replace(0, play_all(16));
  rig.start(120'000);
  CHECK_FALSE(rig.seq.record_motion(0, 1000.0f));  // not playing
  rig.engine.play();
  rig.run(frame_of(3 * kStep + 130, 120'000) + 64);  // step 3, quarter 2
  const Playhead ph = rig.seq.playhead();
  REQUIRE(ph.step == 3);
  REQUIRE(ph.point == 2);
  CHECK(rig.seq.record_motion(0, 1000.0f));
  CHECK(rig.bank->pattern(0).motion[0][3 * 4 + 2] == value_to_point(kVoiceParams[0], 1000.0f));
  CHECK(rig.bank->pattern(0).motion[0][3 * 4 + 1] == kNoLock);
  CHECK_FALSE(rig.seq.record_motion(8, 1.0f));
  rig.run(frame_of(5 * kStep + 130, 120'000) + 64 - rig.voice.at);  // step 5, quarter 2
  const Playhead later = rig.seq.playhead();
  REQUIRE(later.step == 5);
  REQUIRE(later.point == 2);
  CHECK(rig.seq.record_motion(0, 2000.0f));  // later, same lane: still the same undo step
  CHECK(rig.bank->pattern(0).motion[0][5 * 4 + 2] == value_to_point(kVoiceParams[0], 2000.0f));
  CHECK(rig.bank->undo() == 0);
  CHECK(rig.bank->pattern(0).motion[0][3 * 4 + 2] == kNoLock);  // both recordings went in one undo
  CHECK(rig.bank->pattern(0).motion[0][5 * 4 + 2] == kNoLock);
  CHECK(rig.bank->redo() == 0);
  CHECK(rig.bank->pattern(0).motion[0][3 * 4 + 2] == value_to_point(kVoiceParams[0], 1000.0f));
  // The recorded locks play in the next loop (steps 19 and 21).
  rig.run(frame_of(22 * kStep, 120'000) - rig.voice.at);
  unsigned sent = 0;
  for (const Rec& e : rig.params()) {
    sent += (e.param == 0 && e.frame >= frame_of(16 * kStep, 120'000)) ? 1u : 0u;
  }
  CHECK(sent >= 2);
}

TEST_CASE("tempo extremes: the steps stay on their ticks at 20 and 300 BPM, and a tempo change mid-song keeps the grid") {
  for (const std::uint32_t tempo : {20'000u, 300'000u, 77'777u}) {
    CAPTURE(tempo);
    Rig rig;
    rig.bank->replace(0, play_all(8));
    rig.start(tempo);
    rig.engine.play();
    rig.run(frame_of(18 * kStep, tempo));
    unsigned k = 0;
    for (const Rec& e : rig.notes()) {
      if (e.kind == dsp::EventKind::NoteOn) {
        CHECK(e.frame == frame_of(static_cast<std::uint64_t>(k) * kStep, tempo));
        ++k;
      }
    }
    CHECK(k >= 17);
  }
  Rig rig;
  rig.bank->replace(0, play_all(8));
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(4 * kStep, 120'000));
  rig.engine.set_tempo(240'000);  // twice as fast from the next block
  const std::uint64_t switched = rig.voice.at;
  rig.run(20000);
  unsigned late = 0;
  std::uint64_t previous = 0;
  for (const Rec& e : rig.notes()) {
    if (e.kind == dsp::EventKind::NoteOn && e.frame > switched + 64) {
      if (late > 0) {
        const std::uint64_t gap = e.frame - previous;
        CHECK(gap >= 2755);  // 2756.25 frames a step at 240 BPM
        CHECK(gap <= 2758);
      }
      previous = e.frame;
      ++late;
    }
  }
  CHECK(late > 3);
}

TEST_CASE("the same events whatever the block size") {
  Pattern p = play_all(16);
  set_swing(p, 33);
  set_gate(p, 70);
  set_flag(p, 3, kStepSlide, true);
  set_flag(p, 5, kStepTie, true);
  set_flag(p, 9, kStepRest, true);
  set_plock(p, 2, 6, 77);
  set_motion_point(p, 0, 10, 1, 5);
  std::vector<Rec> reference;
  for (const unsigned block : {64u, 37u, 7u, 1u}) {
    CAPTURE(block);
    Rig rig;
    rig.bank->replace(0, p);
    rig.start(133'000);
    rig.engine.play();
    rig.run(frame_of(34 * kStep, 133'000), block);
    if (reference.empty()) {
      reference = rig.voice.log;
      CHECK(reference.size() > 40);
      continue;
    }
    // Compare up to the frame all runs reached.
    const std::uint64_t horizon = std::min<std::uint64_t>(rig.voice.at, frame_of(34 * kStep, 133'000));
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
      CHECK(g.param == e.param);
    }
  }
}

// The frame at which `tick` is reached when the tempo became `tempo` at a block boundary (the transport at `pos0`, Q32.32, after
// `frame0` frames).
std::uint64_t frame_after_change(std::uint64_t tick, std::uint64_t pos0, std::uint64_t frame0, std::uint32_t tempo) {
  const std::uint64_t inc = dsp::ticks_per_sample_q32(tempo);
  return frame0 + (((tick << 32) - pos0) + inc - 1u) / inc;
}

TEST_CASE("a tempo change in the middle of a step keeps every later step and gate on its tick") {
  struct Change { std::uint32_t from; std::uint32_t to; };
  const Change changes[] = {{120'000, 200'000}, {120'000, 20'000}, {20'000, 300'000}, {300'000, 20'000}};
  for (const Change& c : changes) {
    CAPTURE(c.from);
    CAPTURE(c.to);
    Rig rig;
    rig.bank->replace(0, play_all(8));
    rig.start(c.from);
    rig.engine.play();
    rig.run(frame_of(100, c.from));  // inside step 0 (its gate ends at tick 120)
    const std::uint64_t pos0 = rig.engine.stats().pos;
    const std::uint64_t frame0 = rig.voice.at;
    rig.engine.set_tempo(c.to);
    rig.run(frame_after_change(6 * kStep + kStep / 2 + 5, pos0, frame0, c.to) - frame0);
    const auto n = rig.notes();
    REQUIRE(n.size() >= 14);
    CHECK(n[0].frame == 0);
    CHECK(n[1].kind == dsp::EventKind::NoteOff);
    CHECK(n[1].frame == frame_after_change(kStep / 2, pos0, frame0, c.to));
    for (unsigned k = 1; k <= 6; ++k) {
      CAPTURE(k);
      CHECK(n[2 * k].kind == dsp::EventKind::NoteOn);
      CHECK(n[2 * k].frame == frame_after_change(static_cast<std::uint64_t>(k) * kStep, pos0, frame0, c.to));
      CHECK(n[2 * k + 1].kind == dsp::EventKind::NoteOff);
      CHECK(n[2 * k + 1].frame == frame_after_change(static_cast<std::uint64_t>(k) * kStep + kStep / 2, pos0, frame0, c.to));
    }
  }
}

TEST_CASE("a swing change while playing moves the odd steps only: the even steps never leave the grid") {
  Rig rig;
  rig.bank->replace(0, play_all(16));
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(100, 120'000));  // inside step 0, swing 0
  CHECK(rig.bank->set_swing(0, 50));
  rig.run(frame_of(800, 120'000) - rig.voice.at);  // inside step 3 (it started at 780)
  CHECK(rig.bank->set_swing(0, 0));
  rig.run(frame_of(1750, 120'000) - rig.voice.at);
  // Step 1 was scheduled with swing 0 (240); step 2 is on the grid (480); with swing 50 % step 3 is 60 ticks late (780); step 4 is on
  // the grid again (960) and, with swing 0, step 5 is on time (1200).
  const std::uint64_t starts[] = {0, 240, 480, 780, 960, 1200, 1440, 1680};
  unsigned k = 0;
  for (const Rec& e : rig.notes()) {
    if (e.kind != dsp::EventKind::NoteOn) {
      continue;
    }
    REQUIRE(k < 8);
    CAPTURE(k);
    CHECK(e.frame == frame_of(starts[k], 120'000));
    ++k;
  }
  CHECK(k == 8);
  // Step 3 is odd, started at 780 and ends at the pair's end (960): a gate of half of 180 ticks.
  bool found = false;
  for (const Rec& e : rig.notes()) {
    if (e.kind == dsp::EventKind::NoteOff && e.note == play_all(16).steps[3].note) {
      CHECK(e.frame == frame_of(780 + 90, 120'000));
      found = true;
    }
  }
  CHECK(found);
}

TEST_CASE("the hook never plays a half-written pattern: a render inside an edit keeps the pattern it had") {
  Rig rig;
  rig.bank->replace(0, play_all(8));
  rig.start(120'000);
  rig.engine.play();
  rig.run(frame_of(3 * kStep, 120'000) - 10);  // 10 frames before step 3
  std::uint32_t inside = 0;
  rig.bank->edit(0, [&](Pattern& p) {
    for (unsigned j = 0; j < 4; ++j) {  // the first half of the edit is written ...
      p.steps[j].note = 5;
      p.steps[j].flags = kStepAccent;
    }
    inside = rig.bank->version(0);
    rig.run(200);  // ... when the render comes in (it is an interrupt): step 3 starts in this block
    for (unsigned j = 4; j < 8; ++j) {
      p.steps[j].note = 5;
      p.steps[j].flags = kStepAccent;
    }
  });
  CHECK((inside & 1u) == 1u);
  rig.run(frame_of(6 * kStep, 120'000) - rig.voice.at);
  unsigned k = 0;
  for (const Rec& e : rig.notes()) {
    if (e.kind != dsp::EventKind::NoteOn) {
      continue;
    }
    if (k == 3) {  // the step that started during the edit: the old pattern's, not the half-written one's
      CHECK(e.note == 43);
      CHECK(e.flags == 0);
    }
    if (k == 4 || k == 5) {  // the next steps: the finished edit
      CHECK(e.note == 5);
      CHECK(e.flags == dsp::kNoteAccent);
    }
    ++k;
  }
  CHECK(k >= 6);

  // The same at a pattern change: the new pattern is being edited when the old one ends. The hook plays the step from the shadow it
  // has (the old pattern's), not from a torn copy, and has the new pattern at the next block.
  Rig sw;
  sw.bank->replace(0, play_all(8));
  Pattern b = play_all(8);
  for (unsigned j = 0; j < 8; ++j) {
    b.steps[j].note = static_cast<std::uint8_t>(80 + j);
  }
  sw.bank->replace(1, b);
  sw.start(120'000);
  sw.engine.play();
  sw.seq.queue(1);
  sw.run(frame_of(8 * kStep, 120'000) - 10);
  sw.bank->edit(1, [&](Pattern& p) {
    for (unsigned j = 0; j < 4; ++j) {
      p.steps[j].note = 5;
    }
    sw.run(200);  // step 8 (the new pattern's step 0) starts here
  });
  sw.run(frame_of(11 * kStep, 120'000) - sw.voice.at);
  k = 0;
  for (const Rec& e : sw.notes()) {
    if (e.kind != dsp::EventKind::NoteOn) {
      continue;
    }
    if (k == 8) {
      CHECK(e.note == 40);  // the old shadow's step 0: nothing torn
    }
    if (k == 9 || k == 10) {
      CHECK(e.note == 5);  // pattern 1, as edited
    }
    CHECK(e.note != 0);
    ++k;
  }
  CHECK(k >= 11);
  CHECK(sw.seq.playhead().pattern == 1);
}

// ---- The property test: random patterns against an independent model ---------------------------------------------------------

namespace {

struct Ev {
  std::uint64_t tick;
  int cls;  // 0: a gate end, 1: motion, 2: a step's notes (the order the hook serves events that share a tick)
  dsp::EventKind kind;
  int note;
  int flags;
  unsigned lane;
  float value;
};

// What the hook should emit for `steps` steps of pattern `p` from tick 0 (knob values at the descriptors' defaults), by the rules in
// sequencer.h, written without sharing the hook's structure: the steps in order, with each note's end scheduled and sorted in.
std::vector<Ev> model(const Pattern& p, unsigned steps, std::uint64_t* end_tick) {
  std::vector<Ev> out;
  const unsigned n = p.length;
  const unsigned sw = (static_cast<unsigned>(p.swing) * 6u + 2u) / 5u;
  int held = -1;
  bool prev_slide = false;
  std::uint64_t start = 0;
  std::uint8_t overridden = 0;
  float last[kMotionLanes] = {};
  for (unsigned k = 0; k < steps; ++k) {
    const unsigned j = k % n;
    const Step s = p.steps[j];
    const unsigned len = kStep + (j % 2 == 0 ? sw : 0u) - (j % 2 == 1 ? sw : 0u);
    const unsigned gate = (len * p.gate + 50u) / 100u;
    // The motion points of this step.
    for (unsigned q = 0; q < 4; ++q) {
      for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
        const std::uint8_t pt = p.motion[lane][j * 4 + q];
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << lane);
        if (pt == kNoLock) {
          if ((overridden & bit) != 0) {
            out.push_back(Ev{start + q * len / 4, 1, dsp::EventKind::Param, 0, 0, lane, kVoiceParams[lane].def});
            overridden = static_cast<std::uint8_t>(overridden & ~bit);
          }
        } else {
          const float v = point_to_value(kVoiceParams[lane], pt);
          if ((overridden & bit) == 0 || v != last[lane]) {
            out.push_back(Ev{start + q * len / 4, 1, dsp::EventKind::Param, 0, 0, lane, v});
            last[lane] = v;
            overridden = static_cast<std::uint8_t>(overridden | bit);
          }
        }
      }
    }
    const bool rest = (s.flags & kStepRest) != 0;
    const bool tie = (s.flags & kStepTie) != 0 && !rest;
    const bool slide = (s.flags & kStepSlide) != 0;
    const unsigned after = (j + 1) % n;
    const bool tie_follows = (p.steps[after].flags & kStepTie) != 0 && (p.steps[after].flags & kStepRest) == 0;
    const bool hold = slide || tie_follows;
    auto end_note = [&](std::uint64_t tick) {
      out.push_back(Ev{tick, 0, dsp::EventKind::NoteOff, held, 0, 0, 0.0f});
      held = -1;
    };
    if (rest) {
      if (held >= 0) {
        out.push_back(Ev{start, 2, dsp::EventKind::NoteOff, held, 0, 0, 0.0f});
        held = -1;
      }
      prev_slide = false;
    } else if (tie) {
      if (held < 0) {
        prev_slide = false;
      } else {
        if (!hold) {
          end_note(start + gate);
        }
        prev_slide = slide;
      }
    } else {
      const int note = std::clamp(static_cast<int>(s.note) + p.transpose, 0, 127);
      const int flags = (s.flags & kStepAccent) != 0 ? static_cast<int>(dsp::kNoteAccent) : 0;
      if (held >= 0 && prev_slide) {
        out.push_back(Ev{start, 2, dsp::EventKind::NoteOn, note, flags | dsp::kNoteSlide, 0, 0.0f});
        if (held != note) {
          out.push_back(Ev{start, 2, dsp::EventKind::NoteOff, held, 0, 0, 0.0f});
        }
      } else {
        if (held >= 0) {
          out.push_back(Ev{start, 2, dsp::EventKind::NoteOff, held, 0, 0, 0.0f});
        }
        out.push_back(Ev{start, 2, dsp::EventKind::NoteOn, note, flags, 0, 0.0f});
      }
      held = note;
      if (!hold) {
        end_note(start + gate);
      }
      prev_slide = slide;
    }
    start += len;
  }
  *end_tick = start;
  std::stable_sort(out.begin(), out.end(), [](const Ev& a, const Ev& b) {
    return a.tick != b.tick ? a.tick < b.tick : a.cls < b.cls;
  });
  return out;
}

}  // namespace

TEST_CASE("random patterns play exactly as the model says, at any tempo and block size") {
  unsigned total_events = 0;
  for (std::uint32_t seed = 1; seed <= 60; ++seed) {
    CAPTURE(seed);
    Rng r(seed * 7919u);
    Pattern p;
    clear(p);
    p.length = kLengths[r.next_int(0, 3)];
    GenParams g;
    g.density = static_cast<std::uint8_t>(r.next_int(30, 100));
    g.accent_pct = static_cast<std::uint8_t>(r.next_int(0, 60));
    g.slide_pct = static_cast<std::uint8_t>(r.next_int(0, 60));
    g.low = 24;
    g.high = 72;
    random_fill(p, g, seed);
    for (unsigned j = 0; j < p.length; ++j) {
      if (r.chance(0.2f)) {
        set_flag(p, j, kStepTie, true);
      }
      if (r.chance(0.08f)) {
        set_flag(p, j, kStepRest, true);
      }
      if (r.chance(0.05f)) {
        set_flag(p, j, kStepSlide, true);  // a slide on a tie / rest step too
      }
    }
    set_swing(p, r.next_int(0, 66));
    set_gate(p, r.next_int(5, 100));
    set_transpose(p, r.next_int(-24, 24));
    p.lanes[7] = LaneRef{kLaneVoice, 7};  // the default lane 7 is the mixer's level (M3b); this model has the voice's eight ids as lanes
    for (unsigned m = 0; m < 6; ++m) {  // some locks and motion points
      set_motion_point(p, static_cast<unsigned>(r.next_int(0, 7)), static_cast<unsigned>(r.next_int(0, static_cast<int>(p.length) - 1)),
                       static_cast<unsigned>(r.next_int(0, 3)), static_cast<std::uint8_t>(r.next_int(0, 254)));
    }
    const auto tempo = static_cast<std::uint32_t>(r.next_int(60, 300)) * 1000u;
    const unsigned block = static_cast<unsigned>(r.next_int(1, 64));
    const unsigned steps = std::min(2u * p.length + 3u, 100u);

    Rig rig;
    rig.bank->replace(0, p);
    rig.start(tempo);
    rig.engine.play();
    std::uint64_t end_tick = 0;
    const std::vector<Ev> want = model(p, steps, &end_tick);
    rig.run(frame_of(end_tick, tempo), block);

    // The hook's events up to the end tick (those at the end tick belong to the next step).
    std::vector<Rec> got;
    for (const Rec& e : rig.voice.log) {
      if (e.frame < frame_of(end_tick, tempo)) {
        got.push_back(e);
      }
    }
    std::vector<Ev> expect;
    for (const Ev& e : want) {
      if (e.tick < end_tick) {
        expect.push_back(e);
      }
    }
    REQUIRE(got.size() == expect.size());
    total_events += static_cast<unsigned>(got.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
      CAPTURE(i);
      CHECK(got[i].frame == frame_of(expect[i].tick, tempo));
      CHECK(got[i].kind == expect[i].kind);
      if (expect[i].kind == dsp::EventKind::Param) {
        CHECK(got[i].param == expect[i].lane);
        CHECK(got[i].value == doctest::Approx(expect[i].value));
      } else {
        CHECK(got[i].note == expect[i].note);
        if (expect[i].kind == dsp::EventKind::NoteOn) {
          CHECK(got[i].flags == expect[i].flags);
        }
      }
    }
  }
  CHECK(total_events > 3000);
}
