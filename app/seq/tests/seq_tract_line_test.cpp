// SPDX-License-Identifier: GPL-3.0-or-later
// The TRACT line (docs/TRACT-KNOBS-PLAN.md, A1b): its data (tract_line.h), the Bank's accessors, and the sequencer's hook. A recording stand-in for the
// tract unit (with its real descriptors) sees every event with its absolute frame; the hook's timing is checked against the exact landing frame of
// the step (exact_frame.h), the glide against the tempo.
#include <doctest/doctest.h>

#include "exact_frame.h"

#include <o3_303/seq/bank.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/tract_line.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/tract/tract.h>
#include <o3sdk/core/param.h>
#include <o3sdk/dsp/engine.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
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
const ParamDesc* const kTractParams = [] {
  static TractFx tract;
  return tract.info().params;
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
      log.push_back(Rec{at + e.frame, e.kind, e.param, e.value});
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

struct Rig {
  dsp::Engine engine;
  Probe voice{dsp::UnitKind::Instrument, kVoiceParams, AcidVoice::kParamCount};
  Probe dist{dsp::UnitKind::Effect, kDistParams, 3};
  Probe delay{dsp::UnitKind::Effect, kDelayParams, 3};
  Probe tract{dsp::UnitKind::Effect, kTractParams, TractFx::kParamCount};
  std::unique_ptr<Bank> bank = std::make_unique<Bank>();
  Sequencer seq;
  dsp::UnitId vid = dsp::kNoUnit, did = dsp::kNoUnit, lid = dsp::kNoUnit, tid = dsp::kNoUnit;

  Rig() : seq(init(*bank)) {}
  static Bank& init(Bank& b) {
    b.init();
    return b;
  }

  void start(bool with_tract = true, std::uint32_t tempo_mbpm = 120'000) {
    vid = engine.attach(voice);
    did = engine.attach(dist);
    lid = engine.attach(delay);
    tid = engine.attach(tract);
    REQUIRE(engine.set_source(0, vid));
    REQUIRE(engine.set_insert(0, 0, did));
    REQUIRE(engine.set_insert(0, 1, tid));
    REQUIRE(engine.set_send_effect(0, lid));
    REQUIRE(seq.setup(engine, vid, did, lid));
    if (with_tract) {
      REQUIRE(seq.set_unit(kLaneTract, tid));
    }
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
  // A knob of the tract, handed to the hook (the sequencer reads the engine's value in update()).
  void knob(std::uint16_t id, float v) {
    REQUIRE(engine.set_param(tid, id, v));
    seq.update();
  }
  std::uint32_t tempo = 120'000;
};

constexpr std::uint32_t kStep = dsp::kTicksPerSixteenth;  // 240 ticks
constexpr std::uint16_t kVowel = TractFx::Vowel;
constexpr std::uint16_t kGlide = TractFx::Glide;

std::uint64_t frame_of(std::uint64_t tick, std::uint32_t tempo_mbpm = 120'000) { return seq_test::exact_frame(tick, tempo_mbpm); }

// The Param events of one parameter; where two share a frame (the knob's own write, queued for frame 0 of the first block, and the line's), the later one.
std::vector<Rec> events_of(const std::vector<Rec>& log, std::uint16_t id) {
  std::vector<Rec> out;
  for (const Rec& e : log) {
    if (e.kind == dsp::EventKind::Param && e.param == id) {
      if (!out.empty() && out.back().frame == e.frame) {
        out.back() = e;
      } else {
        out.push_back(e);
      }
    }
  }
  return out;
}

// Only the events before `frame`.
std::vector<Rec> before(std::vector<Rec> v, std::uint64_t frame) {
  v.erase(std::remove_if(v.begin(), v.end(), [&](const Rec& e) { return e.frame >= frame; }), v.end());
  return v;
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

// A line from a string: A E I O U a vowel, a e i o u the same with GLD, '-' follows the knob. The rest of the 64 steps stay "-".
void put_line(TractLine& t, const std::string& s, unsigned len = kTractSame) {
  tract_clear(t);
  for (std::size_t i = 0; i < s.size() && i < kTractSteps; ++i) {
    const char c = s[i];
    const bool low = c >= 'a' && c <= 'z';
    const char up = low ? static_cast<char>(c - 'a' + 'A') : c;
    std::uint8_t v = kTractFollow;
    switch (up) {
      case 'A': v = kTractA; break;
      case 'E': v = kTractE; break;
      case 'I': v = kTractI; break;
      case 'O': v = kTractO; break;
      case 'U': v = kTractU; break;
      default: break;
    }
    t.step[i] = tract_make(v, low && v != kTractFollow);
  }
  t.len = static_cast<std::uint8_t>(len);
}

float vowel_value(char c) {
  switch (c) {
    case 'A': return 0.0f;
    case 'E': return 0.25f;
    case 'I': return 0.5f;
    case 'O': return 0.75f;
    default: return 1.0f;
  }
}

void check_event(const Rec& e, std::uint64_t frame, float value) {
  CHECK(e.frame == frame);
  CHECK(e.value == doctest::Approx(value).epsilon(1e-6));
}

}  // namespace

TEST_CASE("the tract line: the ids match the unit, the data helpers") {
  static_assert(kTractVowelParam == TractFx::Vowel && kTractGlideParam == TractFx::Glide);
  CHECK(kTractParams[kVowel].min == 0.0f);
  CHECK(kTractParams[kVowel].max == 1.0f);
  CHECK(kTractParams[kGlide].min == 5.0f);
  CHECK(kTractParams[kGlide].max == 500.0f);
  // The vowel positions are the unit's: A = 0 .. U = 1.
  CHECK(tract_vowel_value(kTractA) == 0.0f);
  CHECK(tract_vowel_value(kTractE) == 0.25f);
  CHECK(tract_vowel_value(kTractU) == 1.0f);
  CHECK(tract_vowel_value(kTractFollow) == 0.0f);
  CHECK(tract_make(kTractE, true) == (kTractE | kTractGlide));
  CHECK(tract_make(9, true) == kTractGlide);  // a vowel above U is "-"
  CHECK(tract_length(TractLine{}, 12) == 12u);
  TractLine t;
  put_line(t, "A-e", 8);
  CHECK(tract_length(t, 12) == 8u);
  CHECK(tract_has_vowels(t));
  put_line(t, "---");
  t.step[2] = kTractGlide;  // GLD without a vowel is not a vowel
  CHECK_FALSE(tract_has_vowels(t));
  // sanitize
  t.step[0] = 0x07u;
  t.step[1] = 0x46u;
  t.len = 12;
  t.reserved[1] = 3;
  CHECK(tract_sanitize(t));
  CHECK(t.step[0] == kTractFollow);
  CHECK(t.step[1] == kTractFollow);
  CHECK(t.len == kTractSame);
  CHECK(t.reserved[1] == 0);
  CHECK_FALSE(tract_sanitize(t));
  // the line index wraps on its length
  CHECK(tract_index(70, 64) == 6u);
  CHECK(tract_index(5, 4) == 1u);
}

TEST_CASE("the tract line: the Bank's accessors, undo coalescing and carry") {
  std::unique_ptr<Bank> bank = std::make_unique<Bank>();
  bank->init();
  // Fresh: every step "-", SAME.
  CHECK(bank->tract_line(3).len == kTractSame);
  CHECK_FALSE(tract_has_vowels(bank->tract_line(3)));
  // Set, read, reject.
  CHECK(bank->set_tract_step(3, 5, kTractO, true));
  CHECK(bank->tract_step(3, 5) == (kTractO | kTractGlide));
  CHECK_FALSE(bank->set_tract_step(3, 5, kTractO, true));  // unchanged
  CHECK_FALSE(bank->set_tract_step(3, 64, kTractO, false));
  CHECK_FALSE(bank->set_tract_step(48, 0, kTractO, false));
  CHECK(bank->tract_step(3, 64) == kTractFollow);
  CHECK(bank->set_tract_length(3, 8));
  CHECK(bank->tract_line(3).len == 8u);
  CHECK_FALSE(bank->set_tract_length(3, 12));
  CHECK_FALSE(bank->set_tract_length(3, 8));
  CHECK(bank->tract_line(3).len == 8u);
  // Turning one step is one undo step.
  bank->clear_history();
  CHECK(bank->set_tract_step(3, 1, kTractA, false));
  CHECK(bank->set_tract_step(3, 1, kTractE, false));
  CHECK(bank->set_tract_step(3, 1, kTractI, false));
  CHECK(bank->undo() == 3);
  CHECK(bank->tract_step(3, 1) == kTractFollow);
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->redo() == 3);
  CHECK(bank->tract_step(3, 1) == kTractI);
  // Two steps are two undo steps.
  bank->clear_history();
  CHECK(bank->set_tract_step(3, 2, kTractU, false));
  CHECK(bank->set_tract_step(3, 3, kTractU, false));
  CHECK(bank->undo() == 3);
  CHECK(bank->tract_step(3, 3) == kTractFollow);
  CHECK(bank->tract_step(3, 2) == kTractU);
  CHECK(bank->undo() == 3);
  CHECK(bank->tract_step(3, 2) == kTractFollow);
  CHECK_FALSE(bank->can_undo());
  // CLEAR is one undo step and leaves the line canonical.
  bank->clear_history();
  CHECK(bank->set_tract_step(3, 7, kTractE, true));
  CHECK(bank->set_tract_length(3, 16));
  CHECK(bank->clear_tract(3));
  CHECK_FALSE(bank->clear_tract(3));
  CHECK_FALSE(tract_has_vowels(bank->tract_line(3)));
  CHECK(bank->tract_line(3).len == kTractSame);
  CHECK(bank->undo() == 3);
  CHECK(bank->tract_line(3).len == 16u);
  CHECK(bank->tract_step(3, 7) == (kTractE | kTractGlide));

  // paint_tract: one edit over a mask of steps (the TRACT page's pads), one undo step; -1 keeps the vowel or the GLD; no vowel, no GLD.
  bank->clear_history();
  CHECK(bank->clear_tract(3));
  bank->clear_history();
  const std::uint64_t mask = (1ull << 2) | (1ull << 5) | (1ull << 40);
  CHECK(bank->paint_tract(3, mask, kTractE, -1));
  CHECK(bank->tract_step(3, 2) == kTractE);
  CHECK(bank->tract_step(3, 40) == kTractE);
  CHECK(bank->tract_step(3, 3) == kTractFollow);
  CHECK(bank->paint_tract(3, 1ull << 5, -1, 1));  // GLD on a vowel step keeps the vowel
  CHECK(bank->tract_step(3, 5) == (kTractE | kTractGlide));
  CHECK(bank->paint_tract(3, 1ull << 5, kTractU, -1));  // a new vowel keeps the GLD
  CHECK(bank->tract_step(3, 5) == (kTractU | kTractGlide));
  CHECK(bank->paint_tract(3, mask, -1, 1));
  CHECK(bank->tract_step(3, 2) == (kTractE | kTractGlide));
  CHECK_FALSE(bank->paint_tract(3, mask, -1, 1));  // unchanged
  CHECK(bank->paint_tract(3, mask | (1ull << 9), kTractFollow, -1));  // "-" takes the GLD with it
  CHECK(bank->tract_step(3, 2) == kTractFollow);
  CHECK(bank->tract_step(3, 9) == kTractFollow);
  CHECK(bank->paint_tract(3, 1ull << 9, -1, 1) == false);  // GLD on a "-" step means nothing
  CHECK(bank->tract_step(3, 9) == kTractFollow);
  CHECK_FALSE(bank->paint_tract(3, 0, kTractA, -1));
  CHECK_FALSE(bank->paint_tract(3, mask, 6, -1));
  CHECK_FALSE(bank->paint_tract(48, mask, kTractA, -1));
  // Several steps at once are an undo step of their own; a lone step joins its turns.
  bank->clear_history();
  CHECK(bank->paint_tract(3, mask, kTractA, 0));
  CHECK(bank->paint_tract(3, mask, kTractI, -1));
  CHECK(bank->undo() == 3);
  CHECK(bank->tract_step(3, 40) == kTractA);
  CHECK(bank->undo() == 3);
  CHECK(bank->tract_step(3, 40) == kTractFollow);
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->paint_tract(3, 1ull << 7, kTractA, -1));
  CHECK(bank->paint_tract(3, 1ull << 7, kTractO, -1));
  CHECK(bank->undo() == 3);
  CHECK(bank->tract_step(3, 7) == kTractFollow);
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->set_tract_length(3, 16));  // the line the next block starts from: SAME is 16 here, step 7 E with GLD
  CHECK(bank->set_tract_step(3, 7, kTractE, true));

  // The line rides with the pattern: COPY PAT and PASTE, DUP>NEXT (replace), undo of a paste.
  bank->clear_history();
  CHECK(bank->clear_tract(3));
  Pattern src = play_all(16);
  put_line(src.tract, "AEIOUaeiou", 32);
  bank->replace(3, src);
  bank->copy_pattern(3);
  CHECK(bank->paste_pattern(5));
  CHECK(std::memcmp(&bank->tract_line(5), &src.tract, sizeof(TractLine)) == 0);
  CHECK(bank->undo() == 5);
  CHECK_FALSE(tract_has_vowels(bank->tract_line(5)));
  CHECK(bank->tract_line(5).len == kTractSame);
  CHECK(bank->redo() == 5);
  CHECK(bank->tract_step(5, 6) == (kTractE | kTractGlide));
  CHECK(bank->tract_line(5).len == 32u);
  bank->replace(6, bank->pattern(3));  // DUP>NEXT
  CHECK(bank->tract_step(6, 9) == (kTractU | kTractGlide));
  CHECK(bank->tract_line(6).len == 32u);
  // The sequencer's snapshot sees it too.
  Pattern snap;
  std::uint32_t ver = 0;
  REQUIRE(bank->snapshot(6, snap, &ver));
  CHECK(std::memcmp(&snap.tract, &src.tract, sizeof(TractLine)) == 0);
  // CLEAR PAT clears the notes, as it does not touch the motion: the line has its own CLEAR (clear_tract); a fresh pattern (clear()) has none.
  CHECK(bank->clear_pattern(6));
  CHECK(tract_has_vowels(bank->tract_line(6)));
  CHECK(bank->tract_line(6).len == 32u);
  CHECK(bank->clear_tract(6));
  Pattern fresh = play_all(16);
  put_line(fresh.tract, "AEIOU", 8);
  clear(fresh);
  CHECK_FALSE(tract_has_vowels(fresh.tract));
  CHECK(fresh.tract.len == kTractSame);
}

TEST_CASE("the tract line: random_tract draws the same line from the same seed, over the line's length only, with one undo step") {
  std::unique_ptr<Bank> bank = std::make_unique<Bank>();
  std::unique_ptr<Bank> other = std::make_unique<Bank>();
  bank->clear_history();
  other->clear_history();
  const unsigned pat = 2;
  REQUIRE(bank->set_tract_length(pat, 8));
  REQUIRE(other->set_tract_length(pat, 8));
  bank->clear_history();
  other->clear_history();
  CHECK(bank->set_tract_step(pat, 40, kTractU, true));  // a step above the length: it must stay
  CHECK(other->set_tract_step(pat, 40, kTractU, true));
  bank->clear_history();
  other->clear_history();
  CHECK(bank->random_tract(pat, 0x1234u));
  CHECK(other->random_tract(pat, 0x1234u));
  for (unsigned n = 0; n < kTractSteps; ++n) {
    CHECK(bank->tract_step(pat, n) == other->tract_step(pat, n));  // the same seed, the same line, bit for bit
  }
  CHECK(bank->tract_step(pat, 40) == (kTractU | kTractGlide));
  CHECK(bank->tract_line(pat).len == 8u);
  CHECK(tract_vowel(bank->tract_step(pat, 0)) != kTractFollow);  // step 0 always has a vowel
  unsigned vowels = 0;
  for (unsigned n = 0; n < 8; ++n) {
    const std::uint8_t v = bank->tract_step(pat, n);
    CHECK(tract_vowel(v) <= kTractVowels);
    CHECK((v & ~(kTractGlide | 0x07u)) == 0u);
    CHECK((tract_vowel(v) != kTractFollow || !tract_glide(v)));  // a "-" has no GLD
    vowels += tract_vowel(v) != kTractFollow ? 1u : 0u;
  }
  CHECK(vowels >= 1u);
  // Another seed draws another line (over the whole line of 64: 64 steps leave no doubt), and a seed over SAME follows the pattern's length.
  CHECK(other->set_tract_length(pat, 64));
  CHECK(bank->set_tract_length(pat, 64));
  CHECK(bank->random_tract(pat, 1u));
  CHECK(other->random_tract(pat, 2u));
  bool differs = false;
  unsigned seen[kTractVowels + 1] = {};
  unsigned glides = 0;
  for (unsigned n = 0; n < kTractSteps; ++n) {
    differs = differs || bank->tract_step(pat, n) != other->tract_step(pat, n);
    ++seen[tract_vowel(bank->tract_step(pat, n))];
    glides += tract_glide(bank->tract_step(pat, n)) ? 1u : 0u;
  }
  CHECK(differs);
  for (unsigned v = 0; v <= kTractVowels; ++v) {
    CHECK(seen[v] > 0u);  // "-" and every vowel turn up in 64 steps
  }
  CHECK(glides > 0u);
  // One undo step, back to what the line held.
  bank->clear_history();
  const TractLine before = bank->tract_line(pat);
  CHECK(bank->random_tract(pat, 77u));
  CHECK(bank->undo() == pat);
  CHECK(std::memcmp(&bank->tract_line(pat), &before, sizeof(TractLine)) == 0);
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->set_tract_length(pat, kTractSame));
  CHECK(bank->random_tract(pat, 5u));
  const unsigned plen = bank->pattern(pat).length != 0u ? bank->pattern(pat).length : kDefaultLength;
  for (unsigned n = plen; n < kTractSteps; ++n) {
    CHECK(bank->tract_step(pat, n) == before.step[n]);  // SAME is the pattern's length: 16 steps drawn, the rest untouched
  }
}

TEST_CASE("the tract line: the Bank is in the SDRAM as a plain trivial object, and the line is inside the pattern") {
  // O3SDK_BIG puts project_bank() in the SDRAM (bank.cpp); the line is a member of the Pattern, so it lives there too. A trivially constructible
  // Bank is what a zero-initialised SDRAM object needs; the sizes keep the chunk and the records the project file has.
  static_assert(std::is_trivially_default_constructible<Bank>::value);
  static_assert(std::is_trivial<Pattern>::value && std::is_trivial<TractLine>::value);
  static_assert(offsetof(Pattern, tract) == kPatBytes);
  static_assert(sizeof(Pattern) - kPatBytes == sizeof(TractLine));
  CHECK(sizeof(TractLine) == 68u);
  CHECK(kPatBytes == 2260u);
  Bank& b = project_bank();
  b.init();
  CHECK(b.pattern(0).tract.len == kTractSame);
  CHECK(reinterpret_cast<std::uintptr_t>(&b.pattern(0)) % 4u == 0u);
}

// ---- The hook ------------------------------------------------------------------------------------------------------------------

TEST_CASE("the tract line: a vowel step writes VOWEL at the step's frame, a '-' step gives the knob back, LINE off writes nothing") {
  const std::string line = "AEIOU--A";
  for (const bool on : {true, false}) {
    CAPTURE(on);
    Rig rig;
    rig.start();
    rig.knob(kVowel, 0.3f);
    Pattern p = play_all(8);
    put_line(p.tract, line);
    rig.bank->replace(0, p);
    rig.seq.set_tract_line(on);
    rig.engine.play();
    const std::uint64_t end = frame_of(8 * kStep);
    rig.run(end + 512);
    const std::vector<Rec> ev = before(events_of(rig.tract.log, kVowel), end);
    if (!on) {
      REQUIRE(ev.size() == 1u);  // only the knob's own write at frame 0
      check_event(ev[0], 0, 0.3f);
      CHECK(events_of(rig.tract.log, kGlide).empty());
      continue;
    }
    // A E I O U, then "-" gives the knob (0.3) back at its frame, the second "-" says nothing, A again at step 7.
    REQUIRE(ev.size() == 7u);
    const unsigned at[7] = {0, 1, 2, 3, 4, 5, 7};
    const float want[7] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 0.3f, 0.0f};
    for (unsigned i = 0; i < 7; ++i) {
      CAPTURE(i);
      check_event(ev[i], frame_of(at[i] * kStep), want[i]);
    }
    CHECK(events_of(rig.tract.log, kGlide).empty());  // no GLD, no glide
  }
}

TEST_CASE("the tract line: with no tract unit given to the sequencer the line does nothing") {
  Rig rig;
  rig.start(false);
  Pattern p = play_all(8);
  put_line(p.tract, "AEIOUAEI");
  rig.bank->replace(0, p);
  rig.seq.set_tract_line(true);
  rig.engine.play();
  rig.run(frame_of(8 * kStep));
  CHECK(rig.tract.log.empty());
}

TEST_CASE("the tract line: GLD sets the glide to a quarter of the step and the vowel to the next step's, at the step's first quarter") {
  Rig rig;
  rig.start();
  rig.knob(kVowel, 0.6f);
  rig.knob(kGlide, 80.0f);
  Pattern p = play_all(8);
  put_line(p.tract, "aUe-AO--");  // a(GLD) -> U; U; e(GLD) -> "-" = the knob; "-"
  rig.bank->replace(0, p);
  rig.seq.set_tract_line(true);
  rig.engine.play();
  const std::uint64_t end = frame_of(8 * kStep);
  rig.run(end + 512);
  const std::vector<Rec> v = before(events_of(rig.tract.log, kVowel), end);
  const std::vector<Rec> g = before(events_of(rig.tract.log, kGlide), end);
  // 120 bpm: a sixteenth is 125 ms, a quarter of it 31.25 ms.
  const float glide_ms = 31.25f;
  // VOWEL: 0 | q1: 1 | step 1 (U): 1 | step 2 e: .25 | q1: the knob 0.6 | step 3 "-": 0.6 (given back) | step 4 A: 0 | step 5 O: .75 | 6 "-": .6
  struct Ev {
    std::uint64_t frame;
    float value;
  };
  const Ev wv[] = {{frame_of(0), 0.0f},           {frame_of(0 * kStep + kStep / 4), 1.0f}, {frame_of(1 * kStep), 1.0f},
                   {frame_of(2 * kStep), 0.25f},  {frame_of(2 * kStep + kStep / 4), 0.6f}, {frame_of(3 * kStep), 0.6f},
                   {frame_of(4 * kStep), 0.0f},   {frame_of(5 * kStep), 0.75f},            {frame_of(6 * kStep), 0.6f}};
  REQUIRE(v.size() == sizeof(wv) / sizeof(wv[0]));
  for (std::size_t i = 0; i < v.size(); ++i) {
    CAPTURE(i);
    check_event(v[i], wv[i].frame, wv[i].value);
  }
  // GLIDE (the knob's own write at frame 0 first): the step's quarter sets it, the next (non-GLD) step gives the knob's back, the same after the second GLD step.
  const Ev wg[] = {{frame_of(0), 80.0f}, {frame_of(0 * kStep + kStep / 4), glide_ms}, {frame_of(1 * kStep), 80.0f}, {frame_of(2 * kStep + kStep / 4), glide_ms}, {frame_of(3 * kStep), 80.0f}};
  REQUIRE(g.size() == sizeof(wg) / sizeof(wg[0]));
  for (std::size_t i = 0; i < g.size(); ++i) {
    CAPTURE(i);
    check_event(g[i], wg[i].frame, wg[i].value);
  }
  // Within a step the glide arrives before the vowel (the unit's one-pole time is set first).
  const auto first_glide = std::find_if(rig.tract.log.begin(), rig.tract.log.end(), [&](const Rec& e) { return e.param == kGlide && e.frame == wg[0].frame; });
  const auto first_vowel = std::find_if(rig.tract.log.begin(), rig.tract.log.end(), [&](const Rec& e) { return e.param == kVowel && e.frame == wv[1].frame; });
  REQUIRE(first_glide != rig.tract.log.end());
  REQUIRE(first_vowel != rig.tract.log.end());
  CHECK(first_glide < first_vowel);
}

TEST_CASE("the tract line: the glide is a quarter of the step at other tempos and held to 5..500 ms") {
  struct Case {
    std::uint32_t tempo;
    float want_ms;
  };
  // 62.5 / bpm * 240 / 240 ms: 15.625 * 240 / bpm. 60 bpm 62.5; 240 bpm 15.625; 299 bpm 12.54; 20 bpm 187.5.
  const Case cases[] = {{60'000, 62.5f}, {240'000, 15.625f}, {20'000, 187.5f}};
  for (const Case& c : cases) {
    CAPTURE(c.tempo);
    Rig rig;
    rig.start(true, c.tempo);
    Pattern p = play_all(4);
    put_line(p.tract, "aE--");
    rig.bank->replace(0, p);
    rig.seq.set_tract_line(true);
    rig.engine.play();
    rig.run(frame_of(2 * kStep, c.tempo));
    const std::vector<Rec> g = events_of(rig.tract.log, kGlide);
    REQUIRE(!g.empty());
    CHECK(g[0].value == doctest::Approx(c.want_ms).epsilon(1e-4));
    CHECK(g[0].frame == frame_of(kStep / 4, c.tempo));
  }
}

TEST_CASE("the tract line: polymetric - a line of 4 under a pattern of 16, 64 under 16, 16 and 32 under 8, SAME follows the pattern") {
  struct Case {
    const char* name;
    unsigned pattern_len;
    unsigned line_len;  // kTractSame or a length
    unsigned steps;     // how many steps to play
  };
  const Case cases[] = {{"4 under 16", 16, 4, 40}, {"64 under 16", 16, 64, 70}, {"8 under 16", 16, 8, 36}, {"16 under 8", 8, 16, 40}, {"32 under 8", 8, 32, 70}, {"SAME under 8", 8, kTractSame, 30}, {"SAME under 16", 16, kTractSame, 34}};
  for (const Case& c : cases) {
    CAPTURE(c.name);
    Rig rig;
    rig.start();
    Pattern p = play_all(c.pattern_len);
    // A line that tells every step from its neighbours and from the wrap: the five vowels in a rotating order.
    std::string s;
    const unsigned fill = c.line_len == kTractSame ? c.pattern_len : c.line_len;
    for (unsigned i = 0; i < fill; ++i) {
      s += "AEIOU"[(i * 2u + i / 5u) % 5u];
    }
    put_line(p.tract, s, c.line_len);
    rig.bank->replace(0, p);
    rig.seq.set_tract_line(true);
    rig.engine.play();
    const std::uint64_t end = frame_of(static_cast<std::uint64_t>(c.steps) * kStep);
    rig.run(end + 256);
    const std::vector<Rec> ev = before(events_of(rig.tract.log, kVowel), end);
    REQUIRE(ev.size() == c.steps);
    for (unsigned n = 0; n < c.steps; ++n) {
      CAPTURE(n);
      // SAME follows the pattern's step (n wrapped to its length); a fixed length counts steps since Play.
      const unsigned idx = c.line_len == kTractSame ? n % c.pattern_len : n % c.line_len;
      check_event(ev[n], frame_of(static_cast<std::uint64_t>(n) * kStep), vowel_value(s[idx]));
    }
  }
}

TEST_CASE("the tract line: the playhead is the line's step, published with LINE off too, and -1 when stopped") {
  Rig rig;
  rig.start();
  Pattern p = play_all(16);
  put_line(p.tract, "AEIO-", 8);
  rig.bank->replace(0, p);
  CHECK(rig.seq.tract_playhead() == -1);
  for (const bool on : {true, false}) {
    CAPTURE(on);
    rig.seq.set_tract_line(on);
    CHECK(rig.seq.tract_line_on() == on);
    rig.engine.play();
    rig.run(frame_of(3 * kStep) + 40);
    CHECK(rig.seq.tract_playhead() == 3);
    rig.run(frame_of(11 * kStep) - frame_of(3 * kStep));  // step 11 under a line of 8 is its step 3
    CHECK(rig.seq.tract_playhead() == 3);
    rig.run(frame_of(12 * kStep) - frame_of(11 * kStep));
    CHECK(rig.seq.tract_playhead() == 4);
    rig.engine.stop();
    rig.run(512);
    CHECK(rig.seq.tract_playhead() == -1);
    rig.run(frame_of(2 * kStep));
    rig.seq.set_tract_line(false);
  }
}

TEST_CASE("the tract line: a pattern change (a song row) switches the line, and the new pattern's line starts at its own first step") {
  Rig rig;
  rig.start();
  Pattern a = play_all(8);
  put_line(a.tract, "AEIO", 4);  // 8 steps: A E I O A E I O
  Pattern b = play_all(8);
  put_line(b.tract, "UOIEUOIEAAAAAAAA", 16);  // 8 steps: U O I E U O I E; its second half (A) would show if the count went on from pattern 0
  rig.bank->replace(0, a);
  rig.bank->replace(1, b);
  REQUIRE(rig.bank->song_append(SongRow{0, 1, 0, 0}));
  REQUIRE(rig.bank->song_append(SongRow{1, 1, 0, 0}));
  rig.seq.sync_song();
  rig.seq.set_song_loop(false);
  rig.seq.set_play_song(true);
  rig.seq.set_tract_line(true);
  rig.engine.play();
  const std::uint64_t end = frame_of(16 * kStep);
  rig.run(end + 256);
  const std::vector<Rec> ev = before(events_of(rig.tract.log, kVowel), end);
  const char want[16] = {'A', 'E', 'I', 'O', 'A', 'E', 'I', 'O', 'U', 'O', 'I', 'E', 'U', 'O', 'I', 'E'};
  REQUIRE(ev.size() == 16u);
  for (unsigned n = 0; n < 16; ++n) {
    CAPTURE(n);
    check_event(ev[n], frame_of(static_cast<std::uint64_t>(n) * kStep), vowel_value(want[n]));
  }
}

TEST_CASE("the tract line: LINE turned off in the middle gives the knob back at the next step; turned on, it starts at the next step") {
  Rig rig;
  rig.start();
  rig.knob(kVowel, 0.3f);
  Pattern p = play_all(8);
  put_line(p.tract, "UUUUUUUU");
  rig.bank->replace(0, p);
  rig.seq.set_tract_line(true);
  rig.engine.play();
  rig.run(frame_of(2 * kStep) + 300);  // inside step 2
  rig.seq.set_tract_line(false);
  rig.run(frame_of(6 * kStep) - frame_of(2 * kStep) - 300);
  rig.seq.set_tract_line(true);
  rig.run(frame_of(8 * kStep) - frame_of(6 * kStep) + 300);
  const std::vector<Rec> ev = before(events_of(rig.tract.log, kVowel), frame_of(8 * kStep));
  // Steps 0, 1, 2: U; step 3: the knob back (once); steps 4, 5 nothing; step 6, 7: U.
  REQUIRE(ev.size() == 6u);
  check_event(ev[0], frame_of(0), 1.0f);
  check_event(ev[1], frame_of(1 * kStep), 1.0f);
  check_event(ev[2], frame_of(2 * kStep), 1.0f);
  check_event(ev[3], frame_of(3 * kStep), 0.3f);
  check_event(ev[4], frame_of(6 * kStep), 1.0f);
  check_event(ev[5], frame_of(7 * kStep), 1.0f);
}

TEST_CASE("the tract line: a motion lane on VOWEL wins over the line") {
  Rig rig;
  rig.start();
  Pattern p = play_all(8);
  put_line(p.tract, "AAAAAAAA");
  p.lanes[0] = LaneRef{kLaneTract, kTractVowelParam};
  for (unsigned j = 0; j < 8; ++j) {
    for (unsigned q = 0; q < kMotionPointsPerStep; ++q) {
      p.motion[0][j * kMotionPointsPerStep + q] = 127;  // point 127 of 254: the middle of VOWEL's range
    }
  }
  rig.bank->replace(0, p);
  rig.seq.set_tract_line(true);
  rig.engine.play();
  const std::uint64_t end = frame_of(8 * kStep);
  rig.run(end + 256);
  for (const Rec& e : before(events_of(rig.tract.log, kVowel), end)) {
    CAPTURE(e.frame);
    CHECK(e.value == doctest::Approx(0.5f));  // never the line's A
  }
}
