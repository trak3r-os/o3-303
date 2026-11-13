// SPDX-License-Identifier: GPL-3.0-or-later
// The PERFORM screen (docs/UX-303.md 5.3 and 5.4, slice P1): the Perform button and its two pages, the 48 live pads (actions, snapshots and the
// morph, NEW / VARY / UNDO / REDO, the ladder, the keys), the colour buttons (the sounds' keys), the encoder, the Shift row, what leaving does, and PageKeys over a
// table of its own. The pure pad map is perform_test.cpp's; the 303 screen's own gestures are the older tests' (they run unchanged).
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/ladder_rows.h"
#include "../app/src/perform_screen.h"

#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/param.h>
#include <o3_303/app_access.h>
#include <o3_303/seq/ops.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cmath>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

using namespace hands;
using namespace o3_303;

namespace {

constexpr unsigned kRow1 = 0;
constexpr unsigned kRow2 = 1;
constexpr unsigned kRow3 = 2;
constexpr unsigned kRow4 = 3;

unsigned pp(unsigned row, unsigned col) { return app::pad_at(row, col); }
// The pad of an action: row 1's own, and for the freeze family and the wobble the pad of row 2 or 3 that is it at a rate (H1): FRZ, RPT 8, STP 1/2, REV, and the wobble's 1/4.
unsigned action_pad(PerfAction a) {
  switch (a) {
    case PerfAction::Freeze:
      return pp(kRow2, 0);
    case PerfAction::Roll:
      return pp(kRow2, 1);
    case PerfAction::Stop:
      return pp(kRow2, 7);
    case PerfAction::Reverse:
      return pp(kRow2, 9);
    case PerfAction::Wobble:
      return pp(kRow3, 0);
    default:
      return pp(kRow1, perform_action_pad(a));
  }
}
unsigned all_off_pad() { return pp(kRow1, 11); }

seq::Sequencer& seqr() { return app_sequencer(); }
float freeze_active(Hands& t) { return t.param(kFreeze, Freeze::Active); }
float freeze_mode(Hands& t) { return t.param(kFreeze, Freeze::Mode); }
float cutoff(Hands& t) { return t.param(kVoice, AcidVoice::Cutoff); }

// The pad's LED as the last frame drew it: the LEDs follow the screen by a frame.
std::uint8_t lamp(Hands& t, unsigned pad) {
  t.wait(40);
  return t.led(pad);
}

// Shift + a pad: the latch.
void shift_pad(Hands& t, unsigned pad) {
  t.press(Button::Shift);
  t.pad(pad, 10);
  t.release(Button::Shift);
}

double rms_from(Hands& t, std::size_t from) {
  const auto& a = t.h->audio();
  double sum = 0.0;
  std::size_t n = 0;
  for (std::size_t i = from; i < a.size(); ++i) {
    const double v = static_cast<double>(a[i]) / 32768.0;
    sum += v * v;
    ++n;
  }
  return n == 0 ? 0.0 : std::sqrt(sum / static_cast<double>(n));
}

bool same_pattern(const Pattern& a, const Pattern& b) { return std::memcmp(&a, &b, sizeof(Pattern)) == 0; }

}  // namespace

TEST_CASE("Perform opens PERFORM and flips it with PERFORM KNOBS; Shift + Perform is PERFORM SETUP and Perform leaves it; from any other screen it is page 1 (R8)") {
  Hands t;
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM KNOBS") == 0);
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  t.to_setup();
  CHECK(std::strcmp(t.title(), "PERFORM SETUP") == 0);
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  t.to_perform();  // KNOBS
  t.to_setup();    // SETUP, then away and back: page 1
  t.to_home();
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  t.tap(Button::File);  // File opens FILE PROJECTS from every screen (UX-303 2.1)
  CHECK(std::strcmp(t.title(), "FILE PROJECTS") == 0);
  t.to_home();
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "FILE PROJECTS") == 0);
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
}

TEST_CASE("row 1: a pad holds its action while it is down; the actions reach the sequencer, the send, the transpose and the freeze") {
  Hands t;
  t.to_perform();
  CHECK(seqr().perf() == 0);
  const struct {
    PerfAction a;
    std::uint8_t flag;
  } flags[] = {{PerfAction::Mute, seq::kPerfMute}, {PerfAction::AccAll, seq::kPerfAccAll}, {PerfAction::SlideAll, seq::kPerfSlideAll},
               {PerfAction::Fill, seq::kPerfFill}};
  for (const auto& f : flags) {
    t.down(action_pad(f.a));
    CHECK(seqr().perf() == f.flag);
    CHECK(lamp(t, action_pad(f.a)) == app::kLedOn);
    t.up(action_pad(f.a));
    CHECK(seqr().perf() == 0);
    CHECK(lamp(t, action_pad(f.a)) == app::kLedDim);
  }
  t.down(action_pad(PerfAction::OctUp));
  CHECK(seqr().live_transpose() == 12);
  t.up(action_pad(PerfAction::OctUp));
  CHECK(seqr().live_transpose() == 0);
  t.down(action_pad(PerfAction::Throw));
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(1.0f));
  t.up(action_pad(PerfAction::Throw));
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(0.0f));
  t.down(action_pad(PerfAction::Freeze));
  CHECK(freeze_active(t) == 1.0f);
  t.up(action_pad(PerfAction::Freeze));
  CHECK(freeze_active(t) == 0.0f);
}

TEST_CASE("row 1: Shift + a pad latches its action, a tap of a latched action's pad lets it go, ALL OFF lets every one go") {
  Hands t;
  t.to_perform();
  shift_pad(t, action_pad(PerfAction::Mute));
  shift_pad(t, action_pad(PerfAction::Freeze));
  CHECK(seqr().perf() == seq::kPerfMute);  // the pad's own release leaves it on
  CHECK(freeze_active(t) == 1.0f);
  CHECK(lamp(t, action_pad(PerfAction::Mute)) == app::kLedOn);
  t.pad(action_pad(PerfAction::Mute), 10);  // a plain tap on a latched action: it goes
  CHECK(seqr().perf() == 0);
  CHECK(lamp(t, action_pad(PerfAction::Mute)) == app::kLedDim);
  CHECK(freeze_active(t) == 1.0f);
  shift_pad(t, action_pad(PerfAction::Freeze));  // Shift + the pad again also lets it go
  CHECK(freeze_active(t) == 0.0f);
  // ALL OFF.
  shift_pad(t, action_pad(PerfAction::Mute));
  shift_pad(t, action_pad(PerfAction::AccAll));
  shift_pad(t, action_pad(PerfAction::Freeze));
  CHECK(seqr().perf() == (seq::kPerfMute | seq::kPerfAccAll));
  t.pad(all_off_pad(), 10);
  CHECK(seqr().perf() == 0);
  CHECK(freeze_active(t) == 0.0f);
  CHECK(lamp(t, all_off_pad()) == app::kLedDim);
}

TEST_CASE("FREEZE, ROLL, STOP and REV share the freeze: the one that went on first owns it") {
  Hands t;
  t.to_perform();
  shift_pad(t, action_pad(PerfAction::Freeze));
  CHECK(freeze_active(t) == 1.0f);
  CHECK(freeze_mode(t) == 0.0f);
  t.down(action_pad(PerfAction::Roll));  // FREEZE owns the unit: ROLL does nothing to it
  CHECK(freeze_mode(t) == 0.0f);
  CHECK(freeze_active(t) == 1.0f);
  t.up(action_pad(PerfAction::Roll));
  t.pad(all_off_pad(), 10);
  CHECK(freeze_active(t) == 0.0f);
  t.down(action_pad(PerfAction::Stop));
  CHECK(freeze_active(t) == 1.0f);
  CHECK(freeze_mode(t) == 2.0f);
  t.up(action_pad(PerfAction::Stop));
  CHECK(freeze_active(t) == 0.0f);
  t.down(action_pad(PerfAction::Reverse));
  CHECK(freeze_mode(t) == 3.0f);
  t.up(action_pad(PerfAction::Reverse));
  t.down(action_pad(PerfAction::Roll));
  CHECK(freeze_mode(t) == 1.0f);
  t.up(action_pad(PerfAction::Roll));
  CHECK(freeze_active(t) == 0.0f);
}

TEST_CASE("leaving PERFORM: the momentary actions end, the latched ones stay on, their chips and their pads are there on return") {
  Hands t;
  t.to_perform();
  shift_pad(t, action_pad(PerfAction::Freeze));
  t.down(action_pad(PerfAction::Mute));
  CHECK(seqr().perf() == seq::kPerfMute);
  t.to_home();  // Pattern while a pad is down: the screen goes, MUTE with it
  CHECK(seqr().perf() == 0);
  CHECK(freeze_active(t) == 1.0f);
  t.up(action_pad(PerfAction::Mute));  // the release reaches the 303 screen: harmless
  CHECK(seqr().perf() == 0);
  CHECK(freeze_active(t) == 1.0f);
  t.to_perform();
  CHECK(lamp(t, action_pad(PerfAction::Freeze)) == app::kLedOn);
  CHECK(lamp(t, action_pad(PerfAction::Mute)) == app::kLedDim);
  t.pad(action_pad(PerfAction::Freeze), 10);
  CHECK(freeze_active(t) == 0.0f);
  // A pad held on PERFORM ends when the screen goes (the colour buttons carry no action: PF1).
  t.action_press(PerfAction::Mute);
  CHECK(seqr().perf() == seq::kPerfMute);
  t.to_home();
  CHECK(seqr().perf() == 0);
  t.action_release(PerfAction::Mute);
  CHECK(seqr().perf() == 0);
}

TEST_CASE("the colour buttons on PERFORM flip the sounds and run no action (PF1); a pad latch stays until its pad or ALL OFF lets it go") {
  Hands t;
  t.to_perform();
  // Note, Instr, Vol and Fx are the sounds' keys, with or without Shift: no PERFORM action goes on or off.
  t.tap(Button::Note);
  t.shift_tap(Button::Note);
  t.tap(Button::Fx);
  t.shift_tap(Button::Fx);
  CHECK(seqr().perf() == 0);
  CHECK(freeze_active(t) == 0.0f);
  // A pad latch is not touched by a colour key.
  shift_pad(t, action_pad(PerfAction::Mute));
  CHECK(seqr().perf() == seq::kPerfMute);
  t.shift_tap(Button::Note);
  CHECK(seqr().perf() == seq::kPerfMute);
  t.pad(action_pad(PerfAction::Mute), 10);  // a tap on the latched pad lets it go
  CHECK(seqr().perf() == 0);
  shift_pad(t, action_pad(PerfAction::Freeze));
  CHECK(freeze_active(t) == 1.0f);
  t.tap(Button::Fx);
  CHECK(freeze_active(t) == 1.0f);
  t.pad(all_off_pad(), 10);  // D27
  CHECK(freeze_active(t) == 0.0f);
  // A latch stays on across the change of screen, and its pad is lit on return.
  shift_pad(t, action_pad(PerfAction::Mute));
  t.to_home();
  CHECK(seqr().perf() == seq::kPerfMute);
  t.to_perform();
  CHECK(seqr().perf() == seq::kPerfMute);
  CHECK(lamp(t, action_pad(PerfAction::Mute)) == app::kLedOn);
  t.pad(all_off_pad(), 10);
  CHECK(seqr().perf() == 0);
}

TEST_CASE("PERFORM SETUP: F1..F4 are blank (PF1), and the pads stay PERFORM's") {
  Hands t;
  t.to_perform();
  t.to_setup();    // SETUP
  CHECK(std::strcmp(t.title(), "PERFORM SETUP") == 0);
  t.tap(Button::F1);
  t.turn(1);  // nothing is behind a blank key
  CHECK(seqr().perf() == 0);
  t.tap(Button::Note);  // the colour key is the MODS flip here as everywhere
  CHECK(seqr().perf() == 0);
  t.down(action_pad(PerfAction::Mute));  // the pads are PERFORM's here too
  CHECK(seqr().perf() == seq::kPerfMute);
  t.up(action_pad(PerfAction::Mute));
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  const float before = cutoff(t);
  t.turn(1);
  CHECK(cutoff(t) != before);
}

TEST_CASE("PERFORM SETUP's F5..F8 (5.4): FILL, the freeze's length, ROLL as one list (rate and accelerate), STOP's time; no Shift row") {
  Hands t;
  t.to_perform();
  t.to_setup();    // SETUP
  CHECK(std::strcmp(t.title(), "PERFORM SETUP") == 0);
  // F5 FILL: what FILL plays, through the sequencer (the default is MUTATE).
  CHECK(seqr().fill_variant() == seq::FillVariant::Mutate);
  t.tap(Button::F5);
  t.turn(2);
  CHECK(seqr().fill_variant() == seq::FillVariant::Ratchet);
  // F6 FREEZE: the loop's length (1/4, 1/8, 1/16 of a bar; the default is 1/8).
  CHECK(t.param(kFreeze, Freeze::Length) == doctest::Approx(1.0f));
  t.tap(Button::F6);
  t.turn(1);
  CHECK(t.param(kFreeze, Freeze::Length) == doctest::Approx(2.0f));
  // F7 ROLL: 1/8, 1/16, 1/32, 1/64, BLD BEAT, BLD BAR; the default is 1/16. A rate switches the accelerate off; BUILD starts at 1/8.
  t.tap(Button::F7);
  CHECK(t.param(kFreeze, Freeze::RollRate) == doctest::Approx(1.0f));
  CHECK(t.param(kFreeze, Freeze::Accelerate) == doctest::Approx(0.0f));
  t.turn(1);  // 1/32
  CHECK(t.param(kFreeze, Freeze::RollRate) == doctest::Approx(2.0f));
  t.turn(2);  // 1/64, then BLD BEAT
  CHECK(t.param(kFreeze, Freeze::RollRate) == doctest::Approx(0.0f));
  CHECK(t.param(kFreeze, Freeze::Accelerate) == doctest::Approx(1.0f));  // BEAT
  t.turn(1);
  CHECK(t.param(kFreeze, Freeze::Accelerate) == doctest::Approx(2.0f));  // BAR
  CHECK(t.param(kFreeze, Freeze::RollRate) == doctest::Approx(0.0f));
  t.turn(5);  // clamped at the last place
  CHECK(t.param(kFreeze, Freeze::Accelerate) == doctest::Approx(2.0f));
  t.turn(-2);  // back to 1/64: the accelerate is off again and the rate is that one
  CHECK(t.param(kFreeze, Freeze::Accelerate) == doctest::Approx(0.0f));
  CHECK(t.param(kFreeze, Freeze::RollRate) == doctest::Approx(3.0f));
  // F8 STOP: the slow-down's time (1/8 bar .. 2 bars; the default is half a bar).
  CHECK(t.param(kFreeze, Freeze::StopTime) == doctest::Approx(2.0f));
  t.tap(Button::F8);
  t.turn(-9);
  CHECK(t.param(kFreeze, Freeze::StopTime) == doctest::Approx(0.0f));
  // No Shift row here: Shift + F7 and F8 are not REDO and UNDO (they are on PERFORM). NEW (a PERFORM pad, live here too) gives an UNDO step to not take.
  t.pad(pp(kRow2, 8), 10);
  const Pattern fresh = t.pat();
  t.press(Button::Shift);
  t.tap(Button::F8);
  t.tap(Button::F7);
  t.release(Button::Shift);
  CHECK(same_pattern(t.pat(), fresh));
  // Row 1 and the keys stay PERFORM's here (row 2 is the snapshots); the freeze rates of PERFORM's row 2 are what F6..F8 set.
  t.to_perform();
  t.down(action_pad(PerfAction::Roll));
  CHECK(t.param(kFreeze, Freeze::Mode) == doctest::Approx(1.0f));
  t.up(action_pad(PerfAction::Roll));
}

// A roll pad of row 2 (column 2: RPT 16, column 3: RPT 32) held for `hold_ms` with the pattern playing: the audio and the frames of the press and the release.
struct RollRun {
  std::vector<std::int16_t> audio;
  std::size_t press = 0;
  std::size_t release = 0;
};

RollRun roll_run(unsigned col, unsigned hold_ms) {
  Hands t;
  t.h->record(true);
  t.to_perform();
  t.tap(Button::Play);
  t.wait(1500);
  RollRun r;
  r.press = t.h->audio().size() / 2u;
  t.down(pp(kRow2, col));
  t.wait(hold_ms);
  r.release = t.h->audio().size() / 2u;
  t.up(pp(kRow2, col));
  t.wait(300);
  r.audio = t.h->audio();
  return r;
}

// True when frame i and frame i + lag are the same, on both channels, for every frame of [from, to).
bool repeats_at(const std::vector<std::int16_t>& a, std::size_t from, std::size_t to, std::size_t lag) {
  if (2u * (to + lag) > a.size()) {
    return false;
  }
  for (std::size_t i = 2u * from; i < 2u * to; ++i) {
    if (a[i] != a[i + 2u * lag]) {
      return false;
    }
  }
  return true;
}

TEST_CASE("ROLL repeats at the rate of its pad (H1): RPT 16 loops 1/16, RPT 32 loops 1/32, the loop each rate's note value") {
  // 125 BPM: the loop of a rate is Freeze::roll_frames (1/16 is 5,292 frames, 1/32 half of it). The loop starts after the entry crossfade; the
  // check is on the middle of the hold, where the audio of one loop is the audio of the next.
  const std::size_t l16 = Freeze::roll_frames(Freeze::kRoll16, 125'000, 131072);
  const std::size_t l32 = Freeze::roll_frames(Freeze::kRoll32, 125'000, 131072);
  REQUIRE(l32 * 2u == doctest::Approx(static_cast<double>(l16)).epsilon(0.001));
  const RollRun a = roll_run(2, 900);  // RPT 16
  const std::size_t from = a.press + 4000, to = a.release - l16 - 400;
  CHECK(repeats_at(a.audio, from, to, l16));
  CHECK_FALSE(repeats_at(a.audio, from, to, l32));
  const RollRun b = roll_run(3, 900);  // RPT 32
  CHECK(repeats_at(b.audio, b.press + 4000, b.release - l32 - 400, l32));
  CHECK_FALSE(repeats_at(b.audio, b.press + 4000, b.release - l32 - 400, l32 + 1u));
}

TEST_CASE("row 2: Shift + a snapshot pad stores the sound, a tap recalls it and the encoder becomes the morph until an F key is tapped") {
  Hands t;
  app_snapshots().clear();  // a new project starts with eight factory snapshots (REVIEW I4); this case wants empty pads
  t.to_perform();
  t.to_setup();  // H1: the snapshots are PERFORM SETUP's row 2
  CHECK(lamp(t, pp(kRow2, 0)) == app::kLedOff);  // empty
  const float c0 = cutoff(t);
  shift_pad(t, pp(kRow2, 0));  // store 1: the sound as it is
  CHECK(lamp(t, pp(kRow2, 0)) == app::kLedDim);
  t.to_perform();  // CUTOFF is the latched knob of PERFORM's first page
  t.turn(10);
  t.to_setup();
  const float c1 = cutoff(t);
  CHECK(c1 > c0);
  shift_pad(t, pp(kRow2, 1));  // store 2
  t.to_perform();
  t.turn(10);
  t.to_setup();
  const float live = cutoff(t);
  CHECK(live > c1);

  t.pad(pp(kRow2, 5), 10);  // an empty slot: nothing happens
  t.wait(120);
  CHECK(cutoff(t) == live);
  CHECK(lamp(t, pp(kRow2, 5)) == app::kLedOff);

  t.pad(pp(kRow2, 0), 10);  // recall 1: the glide takes 30 ms
  t.wait(150);
  CHECK(cutoff(t) == doctest::Approx(c0).epsilon(0.01));
  CHECK(lamp(t, pp(kRow2, 0)) == app::kLedOn);
  CHECK(lamp(t, pp(kRow2, 1)) == app::kLedDim);
  // The encoder is the morph, from the sound before the recall (LIVE) to snapshot 1: half way back is half way to `live`.
  t.turn(-16);
  t.wait(150);
  const float mid = cutoff(t);
  CHECK(mid > c0);
  CHECK(mid < live);
  t.turn(-16);
  t.wait(150);
  CHECK(cutoff(t) == doctest::Approx(live).epsilon(0.01));
  // An F key gives the encoder back: the next turn is the setup page's (F1 is blank), not a morph, so the cutoff stays.
  t.tap(Button::F1);
  const float before = cutoff(t);
  t.turn(-5);
  CHECK(cutoff(t) == before);
  // A recall of 2 and the morph from 1 to 2.
  t.pad(pp(kRow2, 1), 10);
  t.wait(150);
  CHECK(cutoff(t) == doctest::Approx(c1).epsilon(0.01));
}

TEST_CASE("row 2: NEW and VARY change the pattern shown and UNDO and REDO walk it back and forth; their LEDs say what there is to undo") {
  Hands t;
  t.to_perform();
  t.to_setup();
  const Pattern first = t.pat();
  CHECK(lamp(t, pp(kRow2, 8)) == app::kLedDim);   // NEW
  CHECK(lamp(t, pp(kRow2, 9)) == app::kLedDim);   // VARY
  CHECK(lamp(t, pp(kRow2, 10)) == app::kLedOff);  // UNDO: nothing to undo yet
  CHECK(lamp(t, pp(kRow2, 11)) == app::kLedOff);  // REDO

  t.pad(pp(kRow2, 8), 10);  // NEW
  const Pattern fresh = t.pat();
  CHECK_FALSE(same_pattern(fresh, first));
  CHECK(lamp(t, pp(kRow2, 10)) == app::kLedDim);
  t.pad(pp(kRow2, 9), 10);  // VARY: a tweak of the new line
  CHECK_FALSE(same_pattern(t.pat(), fresh));
  t.pad(pp(kRow2, 10), 10);  // UNDO the variation
  CHECK(same_pattern(t.pat(), fresh));
  CHECK(lamp(t, pp(kRow2, 11)) == app::kLedDim);
  t.pad(pp(kRow2, 10), 10);  // UNDO the new line
  CHECK(same_pattern(t.pat(), first));
  CHECK(lamp(t, pp(kRow2, 10)) == app::kLedOff);
  t.pad(pp(kRow2, 11), 10);  // REDO
  CHECK(same_pattern(t.pat(), fresh));
  // Nothing to undo: no change and no history entry.
  t.pad(pp(kRow2, 10), 10);
  t.pad(pp(kRow2, 10), 10);
  CHECK(same_pattern(t.pat(), first));
}

TEST_CASE("the Shift row: F8 is UNDO and F7 REDO, the other keys do nothing (no popup)") {
  Hands t;
  t.to_perform();
  t.to_setup();
  const Pattern first = t.pat();
  t.pad(pp(kRow2, 8), 10);  // NEW (on SETUP)
  const Pattern fresh = t.pat();
  CHECK_FALSE(same_pattern(fresh, first));
  t.to_perform();
  t.shift_tap(Button::F1);  // empty on PERFORM: today's RANDOM popup is the 303 screen's
  t.shift_tap(Button::F3);
  CHECK(same_pattern(t.pat(), fresh));
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  t.shift_tap(Button::F8);
  CHECK(same_pattern(t.pat(), first));
  t.shift_tap(Button::F7);
  CHECK(same_pattern(t.pat(), fresh));
  // The soft-key row carries the Shift row while Shift is held.
  t.press(Button::Shift);
  app::SoftKeys keys;
  t.h->runtime().context().screen()->soft_keys(t.h->runtime().context(), keys);
  CHECK(keys.label[0] == nullptr);
  CHECK(std::strcmp(keys.label[6], "REDO") == 0);
  CHECK(std::strcmp(keys.label[7], "UNDO") == 0);
  t.release(Button::Shift);
}

TEST_CASE("row 3 (H1): a pad runs the wobble at its rate, Shift latches it, the last pad pressed wins and the one below takes over; ALL OFF ends the override") {
  Hands t;
  t.to_perform();
  t.tap(Button::Play);
  t.wait(200);
  CHECK_FALSE(seqr().wobble().on);
  CHECK(seqr().wobble_rate_now() == seq::kWobbleRateNone);
  const float c = cutoff(t);
  t.down(pp(kRow3, 0));  // 1/4
  t.wait(40);
  CHECK(seqr().wobble().on);
  CHECK(seqr().wobble_rate_now() == seq::kWobble1_4);
  t.wait(100);
  CHECK(lamp(t, pp(kRow3, 0)) == app::kLedOn);
  CHECK(lamp(t, pp(kRow3, 1)) == app::kLedDim);
  CHECK(lamp(t, pp(kRow3, 11)) == app::kLedOff);  // the eleven rates only
  t.down(pp(kRow3, 3));  // 1/32 on top
  t.wait(30);
  CHECK(seqr().wobble_rate_now() == seq::kWobble1_32);
  t.up(pp(kRow3, 3));  // the one below takes over
  t.wait(30);
  CHECK(seqr().wobble_rate_now() == seq::kWobble1_4);
  t.up(pp(kRow3, 0));
  t.wait(30);
  CHECK_FALSE(seqr().wobble().on);
  CHECK(seqr().wobble().override_rate == seq::kWobbleRateNone);
  CHECK(cutoff(t) == c);  // not a ladder: the knob did not move
  // Shift + a pad latches; ALL OFF lets it go; row 4 is still the keys.
  shift_pad(t, pp(kRow3, 2));  // 1/16
  t.wait(30);
  CHECK(seqr().wobble().on);
  CHECK(seqr().wobble_rate_now() == seq::kWobble1_16);
  t.wait(100);
  CHECK(lamp(t, pp(kRow3, 2)) == app::kLedOn);
  CHECK(lamp(t, pp(kRow4, 3)) == app::kLedDim);  // Eb: in the default Penta Min (a scale shown lights its notes)
  t.pad(all_off_pad(), 10);
  t.wait(30);
  CHECK_FALSE(seqr().wobble().on);
  CHECK(seqr().wobble().override_rate == seq::kWobbleRateNone);
  t.tap(Button::Play);
}

TEST_CASE("row 1 (H2): DIST, CUT LO, CUT HI and RESO punch their targets while held, the knob comes back on the release, Shift latches, ALL OFF lets go") {
  Hands t;
  t.to_perform();
  t.tap(Button::Play);
  t.wait(100);
  CHECK(seqr().punches() == 0u);
  const float c = cutoff(t);
  const float r = t.param(kVoice, AcidVoice::Reso);
  t.down(action_pad(PerfAction::CutLo));
  t.wait(40);
  CHECK(seqr().punches() == 4u);  // slot 2: the cutoff
  CHECK(cutoff(t) == c);          // the knob is not moved: the punch is the sequencer's
  CHECK(lamp(t, action_pad(PerfAction::CutLo)) == app::kLedOn);
  t.down(action_pad(PerfAction::CutHi));  // the last pressed wins: the same slot, the other value
  t.wait(40);
  CHECK(seqr().punches() == 4u);
  t.up(action_pad(PerfAction::CutHi));  // CUT LO is still down: it takes over again
  t.wait(40);
  CHECK(seqr().punches() == 4u);
  t.up(action_pad(PerfAction::CutLo));
  t.wait(40);
  CHECK(seqr().punches() == 0u);
  CHECK(cutoff(t) == c);
  t.down(action_pad(PerfAction::Reso));
  t.down(action_pad(PerfAction::Dist));
  t.wait(40);
  CHECK(seqr().punches() == 0xBu);  // slots 0, 1 (DIST) and 3 (RESO)
  CHECK(t.param(kVoice, AcidVoice::Reso) == r);
  t.up(action_pad(PerfAction::Reso));
  t.up(action_pad(PerfAction::Dist));
  t.wait(40);
  CHECK(seqr().punches() == 0u);
  // Shift latches the punch; ALL OFF lets it go.
  shift_pad(t, action_pad(PerfAction::Dist));
  shift_pad(t, action_pad(PerfAction::CutHi));
  t.wait(40);
  CHECK(seqr().punches() == 0x7u);
  CHECK(lamp(t, action_pad(PerfAction::Dist)) == app::kLedOn);
  t.pad(all_off_pad(), 10);
  t.wait(40);
  CHECK(seqr().punches() == 0u);
  // Leaving the screen lets a held punch go, a latched one stays.
  t.down(action_pad(PerfAction::Reso));
  t.wait(40);
  CHECK(seqr().punches() == 8u);
  t.to_home();
  t.wait(40);
  CHECK(seqr().punches() == 0u);
  t.tap(Button::Play);
}

TEST_CASE("row 4: a key plays the voice; while the pattern runs and TRNSP is on, it transposes (the bottom left C is none)") {
  Hands t;
  t.to_perform();
  t.h->record(true);
  t.wait(200);
  CHECK(rms_from(t, 0) < 1e-6);
  // Stopped: the keys play the voice.
  const std::size_t mark = t.h->audio().size();
  t.down(pp(kRow4, 4));
  t.wait(300);
  CHECK(lamp(t, pp(kRow4, 4)) == app::kLedOn);  // held
  t.up(pp(kRow4, 4));
  t.wait(300);
  CHECK(rms_from(t, mark) > 0.01);
  CHECK(lamp(t, pp(kRow4, 0)) == app::kLedDim);  // naturals dim, sharps off
  CHECK(lamp(t, pp(kRow4, 1)) == app::kLedOff);
  // Playing, TRNSP off (the default): the keys play the voice, the pattern is not transposed, and no lamp shows a transposition.
  t.tap(Button::Play);
  t.wait(100);
  const std::size_t mark1 = t.h->audio().size();
  t.pad(pp(kRow4, 7), 100);
  t.wait(200);
  CHECK(seqr().live_transpose() == 0);
  CHECK(rms_from(t, mark1) > 0.01);
  CHECK(lamp(t, pp(kRow4, 0)) == app::kLedDim);
  // TRNSP on the 303 screen puts the key row on the pattern: a key transposes it.
  t.to_home();
  t.pad(role_pad(PadRole::Trnsp), 40);
  t.to_perform();
  t.pad(pp(kRow4, 7), 10);
  CHECK(seqr().live_transpose() == 7);
  t.wait(100);
  CHECK(lamp(t, pp(kRow4, 7)) == app::kLedOn);  // the pad of the transposition in force
  CHECK(lamp(t, pp(kRow4, 5)) == app::kLedDim);
  t.pad(pp(kRow4, 0), 10);
  CHECK(seqr().live_transpose() == 0);
  // The 303 screen's octave moves the keys: OCT+ there, and C here is an octave up.
  t.to_home();
  t.pad(role_pad(PadRole::OctUp), 10);
  t.to_perform();
  t.pad(pp(kRow4, 0), 10);
  CHECK(seqr().live_transpose() == 12);
  t.to_home();
  t.pad(role_pad(PadRole::OctDown), 10);
  t.to_perform();
  t.pad(pp(kRow4, 0), 10);
  CHECK(seqr().live_transpose() == 0);
  // TRNSP off on the 303 screen puts the keyboard on the voice: here too, and the pattern is not transposed.
  t.to_home();
  t.pad(role_pad(PadRole::Trnsp), 40);
  t.to_perform();
  t.wait(40);
  const std::size_t mark2 = t.h->audio().size();
  t.pad(pp(kRow4, 9), 100);
  t.wait(300);
  CHECK(seqr().live_transpose() == 0);
  CHECK(rms_from(t, mark2) > 0.01);
  t.tap(Button::Play);
  t.wait(50);
  CHECK(seqr().live_transpose() == 0);
}

TEST_CASE("the encoder is the latched sound knob; PERFORM's latch is its own until HOME joins it, and an F key held moves its knob while held") {
  Hands t;
  t.to_perform();
  const float c = cutoff(t);
  const float r = t.param(kVoice, AcidVoice::Reso);
  t.turn(3);
  CHECK(cutoff(t) > c);
  CHECK(t.param(kVoice, AcidVoice::Reso) == r);
  t.tap(Button::F2);
  t.turn(-3);
  CHECK(t.param(kVoice, AcidVoice::Reso) < r);
  // Hold F3 + turn: ENV MOD moves while held, then the latched RESO comes back.
  const float e = t.param(kVoice, AcidVoice::EnvMod);
  t.press(Button::F3);
  t.turn(2);
  t.release(Button::F3);
  CHECK(t.param(kVoice, AcidVoice::EnvMod) > e);
  const float r2 = t.param(kVoice, AcidVoice::Reso);
  t.turn(1);
  CHECK(t.param(kVoice, AcidVoice::Reso) > r2);
}

TEST_CASE("PERFORM's arrows (KN2): Left and Right move the latch along F1..F8, no wrap; Up and Down move the latched knob by 1/20, a switch flips") {
  Hands t;
  t.to_perform();
  const float r = t.param(kVoice, AcidVoice::Reso);
  t.tap(Button::Right);  // F1 (CUTOFF) -> F2 (RESO)
  t.turn(-3);
  CHECK(t.param(kVoice, AcidVoice::Reso) < r);
  t.tap(Button::Left);
  t.tap(Button::Left);  // at F1 already: stays, no wrap
  const float c = cutoff(t);
  t.turn(2);
  CHECK(cutoff(t) > c);
  for (unsigned i = 0; i < 12; ++i) {  // past F8: stays on F8 (VOLUME), no wrap
    t.tap(Button::Right);
  }
  // Up and Down on a continuous knob: the step an F key held + Right gives (1/20 of the range).
  t.tap(Button::Left);  // F7 (TUNE)
  t.tap(Button::Left);  // F6 (WAVE)
  t.tap(Button::Left);  // F5 (ACCENT)
  t.tap(Button::Left);  // F4 (DECAY)
  t.tap(Button::Left);  // F3 (ENV MOD)
  t.tap(Button::Left);  // F2 (RESO)
  const float r0 = t.param(kVoice, AcidVoice::Reso);
  t.tap(Button::Up);
  const float r1 = t.param(kVoice, AcidVoice::Reso);
  CHECK(r1 > r0);
  t.tap(Button::Down);
  CHECK(t.param(kVoice, AcidVoice::Reso) == doctest::Approx(r0).epsilon(0.001));
  t.press(Button::F2);
  t.tap(Button::Right);
  t.release(Button::F2);
  CHECK(t.param(kVoice, AcidVoice::Reso) == doctest::Approx(r1).epsilon(0.001));  // the same 1/20
  // A switch flips.
  t.tap(Button::Right);
  t.tap(Button::Right);
  t.tap(Button::Right);
  t.tap(Button::Right);  // F6 (WAVE)
  const float w0 = t.param(kVoice, AcidVoice::Wave);
  t.tap(Button::Up);
  const float w1 = t.param(kVoice, AcidVoice::Wave);
  CHECK(w1 != doctest::Approx(w0));
  t.tap(Button::Down);
  CHECK(t.param(kVoice, AcidVoice::Wave) == doctest::Approx(w0));
  t.tap(Button::Left);  // F5 (ACCENT): the latch moved, the wave stayed
  CHECK(t.param(kVoice, AcidVoice::Wave) == doctest::Approx(w0));
}

TEST_CASE("the keys of PageKeys run over a table of their own (set_table) and over the global one by default") {
  PageKeys keys;
  CHECK(keys.page_count() == kPageCount);
  CHECK(&keys.page() == &page_at(0));
  keys.set_page(kPagePerfSetup);
  CHECK(&keys.page() == &page_at(kPagePerfSetup));
  keys.set_page(kPageCount);  // past the table: nothing changes
  CHECK(keys.page_index() == kPagePerfSetup);

  static const Page kLocal[2] = {
      {"ONE", {Entry{Item::Tempo, 0, nullptr}, Entry{Item::Swing, 0, nullptr}, Entry{}, Entry{}, Entry{}, Entry{}, Entry{}, Entry{}}},
      {"TWO", {Entry{}, Entry{Item::Gate, 0, nullptr}, Entry{}, Entry{}, Entry{}, Entry{}, Entry{}, Entry{}}}};
  keys.set_table(kLocal, 2);
  CHECK(keys.page_count() == 2);
  CHECK(keys.page_index() == 0);
  CHECK(keys.focus() == 0);
  CHECK(&keys.page() == &kLocal[0]);
  CHECK(keys.focused().item == Item::Tempo);
  keys.next_page();
  CHECK(keys.page_index() == 1);
  CHECK(&keys.page() == &kLocal[1]);
  CHECK(keys.focused().item == Item::Blank);  // key 0 is blank on page 2 ... the latch fell back to key 0, which is blank: nothing is driven
  keys.next_page();  // wraps over the table's two pages, not the global table's
  CHECK(keys.page_index() == 0);
  keys.set_page(2);  // past the table
  CHECK(keys.page_index() == 0);
  keys.set_table(nullptr, 0);  // back to the global table
  CHECK(keys.page_count() == kPageCount);
  CHECK(&keys.page() == &page_at(0));
}

TEST_CASE("every frame of PERFORM, its setup page and the 303 screen fits well under the display list with every action latched, Shift held and a toast up") {
  Hands t;
  t.to_perform();
  t.tap(Button::Play);
  t.wait(100);
  t.tap(Button::Rec);  // REC joins the status line's chips
  for (unsigned col = 0; col < kLineActionCount; ++col) {  // every action of row 1 latched, then a rate of row 2 and one of row 3
    shift_pad(t, pp(kRow1, col));
  }
  shift_pad(t, pp(kRow2, 1));
  shift_pad(t, pp(kRow3, 0));
  t.press(Button::Shift);  // the Shift row
  t.wait(100);
  t.release(Button::Shift);
  t.to_setup();    // SETUP
  t.pad(pp(kRow2, 8), 10);  // NEW: a toast
  t.wait(100);
  t.to_perform();
  t.to_home();  // the 303 screen's strip with its chips and the "+N"
  t.wait(100);
  t.to_perform();
  t.wait(100);
  CHECK(t.h->runtime().frame_failures() == 0u);
  CHECK(t.h->display().render_failures() == 0u);
  CHECK(t.h->display().peak_frame_words() < 1800u);
  t.pad(all_off_pad(), 10);
  t.tap(Button::Rec);
  t.tap(Button::Play);
}

TEST_CASE("an F key held across the page change does not stay held: the next turn moves the latched knob") {
  Hands t;
  t.to_perform();
  const float e = t.param(kVoice, AcidVoice::EnvMod);
  t.press(Button::F3);
  t.turn(1);  // ENV MOD moves while held
  CHECK(t.param(kVoice, AcidVoice::EnvMod) > e);
  t.to_setup();    // SETUP, with F3 still down
  t.release(Button::F3);
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  const float e2 = t.param(kVoice, AcidVoice::EnvMod);
  const float c = cutoff(t);
  t.turn(1);
  CHECK(cutoff(t) > c);  // the latched CUTOFF came back
  CHECK(t.param(kVoice, AcidVoice::EnvMod) == e2);
  // The same on the way in from another screen.
  t.press(Button::F3);
  t.turn(1);
  t.to_home();
  t.to_perform();
  t.release(Button::F3);
  const float e3 = t.param(kVoice, AcidVoice::EnvMod);
  t.turn(1);
  CHECK(t.param(kVoice, AcidVoice::EnvMod) == e3);
}

TEST_CASE("PERFORM plays and does not record: the keys write no notes, with REC armed, stopped or playing") {
  Hands t;
  t.to_home();
  t.pad(role_pad(PadRole::Trnsp), 40);  // TRNSP on: playing, the keys would transpose
  t.to_perform();
  t.tap(Button::Rec);  // REC armed (the 303 screen's mode)
  const Pattern before = t.pat();
  t.pad(pp(kRow4, 2), 40);
  t.pad(pp(kRow4, 9), 40);
  CHECK(same_pattern(t.pat(), before));  // stopped: nothing written, the cursor does not advance
  t.tap(Button::Play);
  t.wait(300);
  t.pad(pp(kRow4, 4), 40);  // playing: transposes, writes nothing
  t.wait(300);
  CHECK(same_pattern(t.pat(), before));
  t.to_home();  // the keyboard on the voice: still nothing written from PERFORM
  t.pad(role_pad(PadRole::Trnsp), 40);
  t.to_perform();
  t.pad(pp(kRow4, 5), 40);
  t.wait(300);
  CHECK(same_pattern(t.pat(), before));
  t.tap(Button::Play);
  t.tap(Button::Rec);
}

// ---- KN3: four live sets of four knobs on KNOBS ------------------------------------------------------------------------------------------------------------------------------------------

namespace {

struct OnKnobs {
  Hands t;
  PerformScreen* s = nullptr;
  explicit OnKnobs(bool wobble_on = false) {
    t.skin_on();
    t.to_perform();
    if (wobble_on) {  // the sequencer's wobble() follows the settings only while the wobble runs
      t.tap(Button::Play);
      t.wait(200);
      shift_pad(t, action_pad(PerfAction::Wobble));
      t.wait(40);
      REQUIRE(seqr().wobble().on);
    }
    t.to_perform();
    t.wait(60);
    s = static_cast<PerformScreen*>(t.h->runtime().context().screen());
    REQUIRE(std::strcmp(t.title(), "PERFORM KNOBS") == 0);
  }
  std::string row(unsigned r) {
    t.wait(60);
    return s->last_view().row_knob[r];
  }
  std::string toast() const { return s->toast_text(); }
};

}  // namespace

TEST_CASE("KNOBS (KN3): F1..F4 are SET 1..4, factory-filled: FILTER, MODDED, GROWL, WOBBLE; a set change toasts and swaps the four rows") {
  OnKnobs o;
  CHECK(o.row(0) == "ENV MOD");
  CHECK(o.row(1) == "DECAY");
  CHECK(o.row(2) == "CUTOFF");
  CHECK(o.row(3) == "RESO");
  o.t.tap(Button::F2);
  CHECK(o.toast() == "SET 2 MODDED");
  CHECK(o.row(0) == "ACC DECAY");
  CHECK(o.row(1) == "SLIDE");
  CHECK(o.row(2) == "OVERDRIVE");
  CHECK(o.row(3) == "FILTER FM");
  o.t.tap(Button::F3);
  CHECK(o.toast() == "SET 3 GROWL");
  CHECK(o.row(0) == "VOWEL");
  CHECK(o.row(1) == "FORMANT Q");
  CHECK(o.row(2) == "COMB TUNE");
  CHECK(o.row(3) == "MIX");
  o.t.tap(Button::F4);
  CHECK(o.toast() == "SET 4 WOBBLE");
  CHECK(o.row(0) == "WOB RATE");
  CHECK(o.row(1) == "WOB DEPTH");
  CHECK(o.row(2) == "DRIVE");
  CHECK(o.row(3) == "DLY SEND");
  o.t.tap(Button::F1);
  CHECK(o.row(2) == "CUTOFF");
}

TEST_CASE("KNOBS (KN3): F5..F8 latch the encoder on their row's knob; a pad tap on a row moves the latch there (Q1-A); the set's knob is what the encoder moves") {
  OnKnobs o;
  o.t.tap(Button::F3);  // set 3 GROWL
  const float mix = o.t.param(kGrowl, Growl::Mix);
  o.t.tap(Button::F8);  // GRL MIX
  CHECK(o.s->knobs_keys().focus() == 7u);
  o.t.turn(3);
  CHECK(o.t.param(kGrowl, Growl::Mix) > mix);
  const float vowel = o.t.param(kGrowl, Growl::Vowel);
  o.t.pad(pp(kRow1, 5), 10);  // a tap on row 1 (VOWEL): the latch follows
  o.t.wait(100);
  CHECK(o.s->knobs_keys().focus() == 4u);
  CHECK(o.t.param(kGrowl, Growl::Vowel) != doctest::Approx(vowel));
  const float v1 = o.t.param(kGrowl, Growl::Vowel);
  o.t.turn(-2);
  CHECK(o.t.param(kGrowl, Growl::Vowel) < v1);
}

TEST_CASE("KNOBS (KN3): an F5..F8 key held + Up / Down re-points the row (toast ROW n: FAMILY / KNOB), Shift + Up / Down a family; the set keeps it, FACTORY puts it back") {
  OnKnobs o;
  const std::string first = o.row(1);
  o.t.press(Button::F6);
  o.t.tap(Button::Up);
  o.t.release(Button::F6);
  CHECK(o.row(1) != first);
  CHECK(o.toast().rfind("ROW 2: ", 0) == 0);
  CHECK(o.toast().find(" / ") != std::string::npos);
  CHECK(o.toast().find(o.row(1)) != std::string::npos);
  CHECK(o.s->knobs_keys().focus() == 5u);  // the latch stays on the held key
  const std::string walked = o.row(1);
  o.t.press(Button::F6);
  o.t.press(Button::Shift);
  o.t.tap(Button::Up);
  o.t.release(Button::Shift);
  o.t.release(Button::F6);
  CHECK(o.row(1) != walked);
  const std::string family = o.toast();
  // The set keeps it: another set and back.
  o.t.tap(Button::F2);
  o.t.tap(Button::F1);
  CHECK(o.row(1) == family.substr(family.rfind(" / ") + 3));
  // Shift + F1 puts set 1 back.
  o.t.shift_tap(Button::F1);
  CHECK(o.toast() == "SET 1 FACTORY: FILTER");
  CHECK(o.row(1) == "DECAY");
  CHECK(o.row(0) == "ENV MOD");
}

TEST_CASE("KNOBS (KN3): HOME's FILT rows are rows 3 and 4 of the set in use") {
  OnKnobs o;
  o.t.tap(Button::F2);  // set 2: row 3 OVERDRIVE, row 4 FILTER FM
  o.t.to_home();
  o.t.pad(role_pad(PadRole::Filt));
  o.t.wait(60);
  const float c0 = cutoff(o.t);
  const float od0 = o.t.param(kVoice, AcidVoice::Overdrive);
  const float fm0 = o.t.param(kVoice, AcidVoice::FilterFm);
  o.t.pad(pp(kRow3, 11), 10);
  o.t.wait(150);
  CHECK(o.t.param(kVoice, AcidVoice::Overdrive) != doctest::Approx(od0));
  CHECK(cutoff(o.t) == doctest::Approx(c0));
  o.t.pad(pp(kRow4, 11), 10);
  o.t.wait(150);
  CHECK(o.t.param(kVoice, AcidVoice::FilterFm) != doctest::Approx(fm0));
}

TEST_CASE("KNOBS (KN3): the Shift row is FACTORY 1..4 on F1..F4 (the set in use stays), REDO and UNDO on F7 and F8; a plain tap of F1..F4 after it is the set") {
  OnKnobs o;
  o.t.press(Button::F6);
  o.t.tap(Button::Up);
  o.t.release(Button::F6);
  const std::string moved = o.row(1);
  o.t.shift_tap(Button::F3);
  CHECK(o.toast() == "SET 3 FACTORY: GROWL");
  CHECK(o.row(1) == moved);  // set 1 still in use, not touched
  o.t.shift_tap(Button::F1);
  CHECK(o.row(1) == "DECAY");
  // UNDO and REDO act on the pattern as on PERFORM: a NEW, then UNDO gives the line back.
  const Pattern before = seqr().bank().pattern(seqr().bank().selected());
  o.t.pad(pp(kRow2, 8), 10);  // a knob pad of row 2: the pattern is not touched
  o.t.shift_tap(Button::F8);
  o.t.shift_tap(Button::F7);
  CHECK(same_pattern(before, seqr().bank().pattern(seqr().bank().selected())));
  o.t.tap(Button::F2);
  CHECK(o.toast() == "SET 2 MODDED");
}

TEST_CASE("KNOBS (KN4): set 4 WOBBLE's WOB RATE and WOB DEPTH set the wobble from a pad tap, and the encoder moves them after a latch") {
  OnKnobs o(true);
  o.t.tap(Button::F4);
  CHECK(o.toast() == "SET 4 WOBBLE");
  const unsigned rate0 = seqr().wobble().rate;
  const float depth0 = seqr().wobble().depth;
  o.t.pad(pp(kRow1, 0), 10);  // WOB RATE: the first place (a list: one pad a rate, the rest dark)
  o.t.wait(150);
  CHECK(seqr().wobble().rate != rate0);
  CHECK(o.s->knobs_keys().focus() == 4u);  // the latch followed the tap
  o.t.pad(pp(kRow2, 11), 10);  // WOB DEPTH: the top of the range
  o.t.wait(150);
  CHECK(seqr().wobble().depth > depth0);
  const float d1 = seqr().wobble().depth;
  o.t.turn(-3);
  o.t.wait(150);
  CHECK(seqr().wobble().depth < d1);
  CHECK(o.toast() != "REC DOES NOT RECORD IT");  // REC is off: nothing to say
}

TEST_CASE("KNOBS (KN4): with REC on and the pattern playing a tap of a row-only knob sets it and records nothing; a toast says so; a lane knob still records") {
  OnKnobs o(true);
  o.t.tap(Button::F4);
  o.t.wait(300);
  o.t.tap(Button::Rec);
  o.t.wait(100);
  const Pattern before = o.t.pat();
  const float depth0 = seqr().wobble().depth;
  o.t.pad(pp(kRow2, 11), 10);  // WOB DEPTH
  o.t.wait(300);
  CHECK(seqr().wobble().depth > depth0);  // set
  CHECK(o.toast() == "REC DOES NOT RECORD IT");
  CHECK(same_pattern(o.t.pat(), before));  // and nothing written
  o.t.pad(pp(kRow3, 10), 10);              // DRIVE has a lane: it records
  o.t.wait(300);
  const int lane = seq::find_lane(o.t.pat(), kLaneDist, 2);
  REQUIRE(lane >= 0);
  CHECK(seq::lane_has_motion(o.t.pat(), static_cast<unsigned>(lane)));
  o.t.tap(Button::Play);
  o.t.tap(Button::Rec);
}

TEST_CASE("KNOBS (KN4): hold + Up / Down walks a row on to the wobble's family: WOB RATE is on the last page, and the set keeps it") {
  OnKnobs o;
  bool wobble = false;
  o.t.press(Button::F5);
  for (unsigned i = 0; i < 400 && !wobble; ++i) {
    o.t.tap(Button::Up);
    wobble = o.row(0).rfind("WOB", 0) == 0;
  }
  o.t.release(Button::F5);
  CHECK(wobble);
  CHECK(o.toast().find("WOBBLE") != std::string::npos);
  const std::string name = o.row(0);
  o.t.tap(Button::F2);
  o.t.tap(Button::F1);
  CHECK(o.row(0) == name);
}

TEST_CASE("PERFORM (PF1): SEL3 and SEL4 step what rows 3 and 4 play, a tap forward, Shift + a tap back, a toast names it, the lamps follow") {
  Hands t;
  t.skin_on();
  t.to_perform();
  t.wait(60);
  auto* s = static_cast<PerformScreen*>(t.h->runtime().context().screen());
  REQUIRE(std::strcmp(t.title(), "PERFORM") == 0);
  const unsigned sel3 = pp(kRow2, 10);
  const unsigned sel4 = pp(kRow2, 11);
  CHECK(lamp(t, sel3) > 0);  // the selectors are lit
  CHECK(lamp(t, sel4) > 0);

  // Row 3 starts on the wobble rates and row 4 on the keys: the lamps differ (the keys light the scale, the rates the eleven pads).
  const std::uint8_t row3_rate = lamp(t, pp(2, 0));
  CHECK(row3_rate > 0);
  CHECK(lamp(t, pp(2, 11)) == 0);  // a twelfth pad past the eleven rates is dark

  // SEL3: the next content is KEYS; the same keys then light on both rows.
  t.pad(sel3, 10);
  CHECK(std::string(s->toast_text()) == "ROW 3: KEYS");
  for (unsigned col = 0; col < 12; ++col) {
    CHECK(lamp(t, pp(2, col)) == lamp(t, pp(3, col)));
  }
  // Once more: the first knob of the ladder list, a ladder of twelve places (every pad lit as a ladder).
  t.pad(sel3, 10);
  CHECK(std::string(s->toast_text()).rfind("ROW 3: ", 0) == 0);
  CHECK(std::string(s->toast_text()).find(" / ") != std::string::npos);
  unsigned lit = 0;  // a ladder lights the places of the knob (the value's pad at full, the places below it or a list's dimmed), not the pad past the eleven rates only
  for (unsigned col = 0; col < 12; ++col) {
    lit += lamp(t, pp(2, col)) > 0 ? 1u : 0u;
  }
  CHECK(lit > 0u);
  // Shift + a tap steps back, twice to WOB RATES, which wraps on the third to the last knob.
  shift_pad(t, sel3);
  CHECK(std::string(s->toast_text()) == "ROW 3: KEYS");
  shift_pad(t, sel3);
  CHECK(std::string(s->toast_text()) == "ROW 3: WOB RATES");
  CHECK(lamp(t, pp(2, 11)) == 0);
  shift_pad(t, sel3);
  CHECK(std::string(s->toast_text()).rfind("ROW 3: ", 0) == 0);
  CHECK(std::string(s->toast_text()).find(" / ") != std::string::npos);

  // SEL4 is row 4's own: it steps from KEYS to WOB RATES, and the two rows may play the same.
  t.pad(sel4, 10);
  CHECK(std::string(s->toast_text()).rfind("ROW 4: ", 0) == 0);
  shift_pad(t, sel4);
  CHECK(std::string(s->toast_text()) == "ROW 4: KEYS");
  shift_pad(t, sel4);
  CHECK(std::string(s->toast_text()) == "ROW 4: WOB RATES");  // both rows on the rates now: allowed
  t.pad(sel4, 10);
  CHECK(std::string(s->toast_text()) == "ROW 4: KEYS");
}
