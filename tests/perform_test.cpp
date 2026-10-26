// SPDX-License-Identifier: GPL-3.0-or-later
// The pure parts of the performance gestures (app/include/o3_303/perform.h, docs/M3B-PLAN.md C4, R1, R7, R8, R5's text): which actions
// the four buttons have on, the ladder positions, the tempo nudge, the scale lock, how a trig condition reads.
#include <doctest/doctest.h>

#include <o3_303/pattern.h>
#include <o3_303/perform.h>

#include <cstdint>
#include <cstring>
#include <string>

using namespace o3_303;

namespace {

std::string text_of(std::uint8_t cond) {
  char buf[16];
  const unsigned n = cond_text(cond, buf, sizeof buf);
  CHECK(n == std::strlen(buf));
  return std::string(buf);
}

}  // namespace

TEST_CASE("perform: the defaults are Note = MUTE, Instr = ACC ALL, Vol = FILL, Fx = FREEZE (C4)") {
  CHECK(kPerfButtons == 4);
  CHECK(kPerfActionCount == 7);
  CHECK(kDefaultPerf[0] == static_cast<std::uint8_t>(PerfAction::Mute));
  CHECK(kDefaultPerf[1] == static_cast<std::uint8_t>(PerfAction::AccAll));
  CHECK(kDefaultPerf[2] == static_cast<std::uint8_t>(PerfAction::Fill));
  CHECK(kDefaultPerf[3] == static_cast<std::uint8_t>(PerfAction::Freeze));
  // The seven actions of the plan, in this order, with names the F-key strip (eight characters) and the header can show.
  CHECK(std::strcmp(kPerfActionNames[static_cast<unsigned>(PerfAction::SlideAll)], "SLD ALL") == 0);
  CHECK(std::strcmp(kPerfActionNames[static_cast<unsigned>(PerfAction::OctUp)], "OCT UP") == 0);
  for (unsigned a = 0; a < kPerfActionCount; ++a) {
    CHECK(std::strlen(kPerfActionNames[a]) <= 8);
    CHECK(std::strlen(kPerfActionChips[a]) <= std::strlen(kPerfActionNames[a]));
  }
}

TEST_CASE("perform: an action is on while a button that carries it is held or latched") {
  std::uint8_t assign[kPerfButtons] = {kDefaultPerf[0], kDefaultPerf[1], kDefaultPerf[2], kDefaultPerf[3]};
  CHECK(perf_actions_on(assign, 0, 0) == 0);
  CHECK(perf_actions_on(assign, 1u << 0, 0) == perf_bit(PerfAction::Mute));
  CHECK(perf_actions_on(assign, 0, 1u << 3) == perf_bit(PerfAction::Freeze));
  CHECK(perf_actions_on(assign, 1u << 1, 1u << 2) == (perf_bit(PerfAction::AccAll) | perf_bit(PerfAction::Fill)));
  // Held and latched on one button: one action.
  CHECK(perf_actions_on(assign, 1u << 0, 1u << 0) == perf_bit(PerfAction::Mute));
  // Two buttons with one action: on while either is.
  assign[1] = static_cast<std::uint8_t>(PerfAction::Mute);
  CHECK(perf_actions_on(assign, 1u << 1, 0) == perf_bit(PerfAction::Mute));
  CHECK(perf_actions_on(assign, 1u << 0, 1u << 1) == perf_bit(PerfAction::Mute));
  // An assignment outside the table does nothing.
  assign[2] = 200;
  CHECK(perf_actions_on(assign, 1u << 2, 0) == 0);
  // A bit above the four buttons is no button.
  CHECK(perf_actions_on(assign, 1u << 5, 1u << 6) == 0);
  // Every action has its own bit.
  std::uint8_t all = 0;
  for (unsigned a = 0; a < kPerfActionCount; ++a) {
    const std::uint8_t b = perf_bit(static_cast<PerfAction>(a));
    CHECK((all & b) == 0);
    all = static_cast<std::uint8_t>(all | b);
  }
  CHECK(all == 0x7F);
}

TEST_CASE("perform: the ladder has 12 places from the knob's minimum to its maximum, and a position finds its pad") {
  CHECK(kLadderSteps == 12);
  CHECK(ladder_norm(0) == 0.0f);
  CHECK(ladder_norm(11) == 1.0f);
  CHECK(ladder_norm(40) == 1.0f);  // past the row: the last
  float last = -1.0f;
  for (unsigned c = 0; c < kLadderSteps; ++c) {
    CHECK(ladder_norm(c) > last);
    last = ladder_norm(c);
    CHECK(ladder_col(ladder_norm(c)) == c);  // the pad draws the position it sets
  }
  CHECK(ladder_col(-0.5f) == 0);
  CHECK(ladder_col(0.0f) == 0);
  CHECK(ladder_col(1.0f) == 11);
  CHECK(ladder_col(7.0f) == 11);
  CHECK(ladder_col(0.5f) == 6);  // 5.5 rounds up
  CHECK(ladder_col(0.04f) == 0);
  CHECK(ladder_col(0.05f) == 1);
  CHECK(kLadderSlewMs == 30.0f);
}

TEST_CASE("perform: the tempo nudge is one percent a detent, at most three, and exact at zero") {
  CHECK(kNudgeMaxPct == 3);
  CHECK(clamp_nudge(0) == 0);
  CHECK(clamp_nudge(2) == 2);
  CHECK(clamp_nudge(9) == 3);
  CHECK(clamp_nudge(-9) == -3);
  CHECK(nudged_tempo(125'000, 0) == 125'000);
  CHECK(nudged_tempo(125'000, 3) == 128'750);
  CHECK(nudged_tempo(125'000, -3) == 121'250);
  CHECK(nudged_tempo(125'000, 1) == 126'250);
  CHECK(nudged_tempo(125'000, 50) == 128'750);  // clamped to the nudge's range
  CHECK(nudged_tempo(125'000, -50) == 121'250);
  CHECK(nudged_tempo(300'000, 3) == 309'000);   // the engine clamps its own range; the nudge itself does not overflow
  CHECK(nudged_tempo(4'000'000'000u, 3) == 4'120'000'000u);
  CHECK(nudged_tempo(0, 3) == 0);
}

TEST_CASE("perform: a scale holds the notes of its degrees over the root, and the two 'no scale' entries hold them all") {
  // C major: 0 2 4 5 7 9 11.
  const unsigned major = 3;
  const bool want_major[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
  for (int pc = 0; pc < 12; ++pc) {
    CHECK(in_scale(36 + pc, major, 0) == want_major[pc]);
    CHECK(in_scale(60 + pc, major, 0) == want_major[pc]);  // every octave
  }
  // Penta Min over C: C Eb F G Bb; over D the same shape moved up two.
  const unsigned penta_min = 18;
  CHECK(in_scale(36, penta_min, 0));
  CHECK(in_scale(39, penta_min, 0));
  CHECK(in_scale(41, penta_min, 0));
  CHECK(in_scale(43, penta_min, 0));
  CHECK(in_scale(46, penta_min, 0));
  CHECK_FALSE(in_scale(38, penta_min, 0));
  CHECK_FALSE(in_scale(40, penta_min, 0));
  CHECK(in_scale(38, penta_min, 2));  // D is the root now
  CHECK(in_scale(41, penta_min, 2));
  CHECK(in_scale(36, penta_min, 2));  // C is the flat seventh of D
  CHECK_FALSE(in_scale(37, penta_min, 2));
  // The notes under the root's first octave and the lowest MIDI notes are still in their pitch class.
  CHECK(in_scale(0, major, 0));
  CHECK_FALSE(in_scale(1, major, 0));
  CHECK(in_scale(1, major, 1));
  CHECK(in_scale(0, major, 5));  // C is the 5th degree of F major
  // No scale (0) and Chromatic (1) hold every note; so does an index past the table.
  for (int n = 0; n < 128; ++n) {
    CHECK(in_scale(n, 0, 0));
    CHECK(in_scale(n, 1, 7));
    CHECK(in_scale(n, 40, 0));
    CHECK(in_scale(n, 255, 3));
  }
}

TEST_CASE("perform: a trig condition reads as ALWAYS, a percent, A:B, FILL, !FILL, FIRST or !FIRST") {
  CHECK(text_of(kCondAlways) == "ALWAYS");
  CHECK(text_of(cond_probability(50)) == "50%");
  CHECK(text_of(cond_probability(7)) == "7%");
  CHECK(text_of(cond_probability(99)) == "99%");
  CHECK(text_of(cond_ratio(1, 2)) == "1:2");
  CHECK(text_of(cond_ratio(3, 8)) == "3:8");
  CHECK(text_of(cond_ratio(8, 8)) == "8:8");
  CHECK(text_of(kCondFill) == "FILL");
  CHECK(text_of(kCondNotFill) == "!FILL");
  CHECK(text_of(kCondFirst) == "FIRST");
  CHECK(text_of(kCondNotFirst) == "!FIRST");
  // A reserved value plays as always, and reads so.
  CHECK(text_of(100) == "ALWAYS");
  CHECK(text_of(0xC4) == "ALWAYS");
  CHECK(text_of(0xFF) == "ALWAYS");
  CHECK(text_of(static_cast<std::uint8_t>(kCondRatio | (4u << 3) | 1u)) == "ALWAYS");  // 5:2 is not a ratio
  // The longest text fits the strip's eight characters.
  for (unsigned c = 0; c < 256; ++c) {
    CHECK(text_of(static_cast<std::uint8_t>(c)).size() <= 8);
  }
  // A small buffer cuts the text and keeps its terminator; none writes nothing.
  char tiny[4];
  CHECK(cond_text(kCondNotFirst, tiny, sizeof tiny) == 3);
  CHECK(std::strcmp(tiny, "!FI") == 0);
  CHECK(cond_text(kCondFill, nullptr, 8) == 0);
  CHECK(cond_text(kCondFill, tiny, 0) == 0);
}
