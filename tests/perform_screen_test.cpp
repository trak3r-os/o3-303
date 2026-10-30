// SPDX-License-Identifier: GPL-3.0-or-later
// The PERFORM screen (docs/UX-303.md 5.3 and 5.4, slice P1): the Perform button and its two pages, the 48 live pads (actions, snapshots and the
// morph, NEW / VARY / UNDO / REDO, the ladder, the keys), the four colour buttons, the encoder, the Shift row, what leaving does, and PageKeys over a
// table of its own. The pure pad map is perform_test.cpp's; the 303 screen's own gestures are the older tests' (they run unchanged).
#include <doctest/doctest.h>

#include "hands.h"

#include <o3sdk/app/pad_leds.h>
#include <o3sdk/core/param.h>
#include <o3_303/app_access.h>
#include <o3_303/seq/ops.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cmath>
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
unsigned action_pad(PerfAction a) { return pp(kRow1, perform_action_pad(a)); }
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

TEST_CASE("Perform opens PERFORM, again PERFORM SETUP, again PERFORM; from any other screen it is page 1") {
  Hands t;
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM SETUP") == 0);
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  t.to_perform();  // SETUP, then away and back: page 1
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
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(0.3f));
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
  // A colour button held on PERFORM ends when the screen goes.
  t.press(Button::Note);
  CHECK(seqr().perf() == seq::kPerfMute);
  t.to_home();
  CHECK(seqr().perf() == 0);
  t.release(Button::Note);
  CHECK(seqr().perf() == 0);
  // On the 303 screen the same buttons are the drawer's rows (H3): no action starts, held or with Shift.
  t.press(Button::Note);
  CHECK(seqr().perf() == 0);
  t.release(Button::Note);
  t.shift_tap(Button::Fx);
  CHECK(freeze_active(t) == 0.0f);
}

TEST_CASE("the colour buttons on PERFORM are the four assigned actions, Shift latches, and ALL OFF unlatches them too") {
  Hands t;
  t.to_perform();
  t.press(Button::Note);
  CHECK(seqr().perf() == seq::kPerfMute);
  CHECK(lamp(t, action_pad(PerfAction::Mute)) == app::kLedOn);  // the pad of the action mirrors the button
  t.release(Button::Note);
  CHECK(seqr().perf() == 0);
  t.shift_tap(Button::Fx);
  CHECK(freeze_active(t) == 1.0f);
  CHECK(lamp(t, action_pad(PerfAction::Freeze)) == app::kLedOn);
  t.pad(all_off_pad(), 10);  // D27: also the colour buttons' latches
  CHECK(freeze_active(t) == 0.0f);
  t.shift_tap(Button::Fx);
  CHECK(freeze_active(t) == 1.0f);
  t.pad(action_pad(PerfAction::Freeze), 10);  // the pad of a latched action lets it go whoever latched it
  CHECK(freeze_active(t) == 0.0f);
  // A latch stays on across the change of screen, and its pad is lit on return.
  t.shift_tap(Button::Note);
  t.to_home();
  CHECK(seqr().perf() == seq::kPerfMute);
  t.to_perform();
  CHECK(seqr().perf() == seq::kPerfMute);
  CHECK(lamp(t, action_pad(PerfAction::Mute)) == app::kLedOn);
  t.pad(all_off_pad(), 10);
  CHECK(seqr().perf() == 0);
}

TEST_CASE("PERFORM SETUP is the PERF page: F1 assigns the Note button, and the pads stay PERFORM's") {
  Hands t;
  t.to_perform();
  t.to_perform();  // SETUP
  CHECK(std::strcmp(t.title(), "PERFORM SETUP") == 0);
  t.tap(Button::F1);
  t.turn(1);  // MUTE -> ACC ALL
  t.press(Button::Note);
  CHECK(seqr().perf() == seq::kPerfAccAll);
  t.release(Button::Note);
  t.down(action_pad(PerfAction::Mute));  // the pads are PERFORM's here too
  CHECK(seqr().perf() == seq::kPerfMute);
  t.up(action_pad(PerfAction::Mute));
  // The latch of the setup page is its own: the sound knobs' latch is still F1.
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  const float before = cutoff(t);
  t.turn(1);
  CHECK(cutoff(t) != before);
  t.to_perform();  // SETUP again: it comes back as it was left, latch and all
  t.turn(1);
  t.press(Button::Note);
  CHECK(seqr().perf() == seq::kPerfSlideAll);  // ACC ALL -> SLD ALL
}

TEST_CASE("row 2: Shift + a snapshot pad stores the sound, a tap recalls it and the encoder becomes the morph until an F key is tapped") {
  Hands t;
  t.to_perform();
  CHECK(lamp(t, pp(kRow2, 0)) == app::kLedOff);  // empty
  const float c0 = cutoff(t);
  shift_pad(t, pp(kRow2, 0));  // store 1: the sound as it is
  CHECK(lamp(t, pp(kRow2, 0)) == app::kLedDim);
  t.turn(10);  // CUTOFF is the latched knob
  const float c1 = cutoff(t);
  CHECK(c1 > c0);
  shift_pad(t, pp(kRow2, 1));  // store 2
  t.turn(10);
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
  // An F key gives the encoder back to the knob: the next turn moves the cutoff itself, not a morph.
  t.tap(Button::F1);
  const float before = cutoff(t);
  t.turn(-5);
  CHECK(cutoff(t) < before);
  // A recall of 2 and the morph from 1 to 2.
  t.pad(pp(kRow2, 1), 10);
  t.wait(150);
  CHECK(cutoff(t) == doctest::Approx(c1).epsilon(0.01));
}

TEST_CASE("row 2: NEW and VARY change the pattern shown and UNDO and REDO walk it back and forth; their LEDs say what there is to undo") {
  Hands t;
  t.to_perform();
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
  const Pattern first = t.pat();
  t.pad(pp(kRow2, 8), 10);  // NEW
  const Pattern fresh = t.pat();
  CHECK_FALSE(same_pattern(fresh, first));
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

TEST_CASE("row 3: twelve places of the latched sound knob through a slew; F2 makes it RESO; the LEDs are a bar to where it stands") {
  Hands t;
  t.to_perform();
  const core::ParamDesc* dc = t.h->engine().param_desc(kVoice, AcidVoice::Cutoff);
  REQUIRE(dc != nullptr);
  t.pad(pp(kRow3, 11), 10);
  t.wait(100);
  CHECK(cutoff(t) == doctest::Approx(dc->max).epsilon(0.001));
  CHECK(lamp(t, pp(kRow3, 11)) == app::kLedOn);
  CHECK(lamp(t, pp(kRow3, 10)) == app::kLedMid);
  CHECK(lamp(t, pp(kRow3, 0)) == app::kLedMid);
  t.pad(pp(kRow3, 0), 10);
  t.wait(100);
  CHECK(cutoff(t) == doctest::Approx(dc->min).epsilon(0.001));
  CHECK(lamp(t, pp(kRow3, 0)) == app::kLedOn);
  CHECK(lamp(t, pp(kRow3, 1)) == app::kLedOff);
  // A tap of a ladder pad is a slew, not a jump, and the knob reads the goal at once.
  t.pad(pp(kRow3, 7), 1);
  t.wait(5);
  CHECK(seqr().slewing() >= 1);
  t.wait(80);
  CHECK(seqr().slewing() == 0);
  CHECK(cutoff(t) == doctest::Approx(core::param_denorm(*dc, ladder_norm(7))).epsilon(0.001));
  // F2: RESO.
  t.tap(Button::F2);
  const core::ParamDesc* dr = t.h->engine().param_desc(kVoice, AcidVoice::Reso);
  REQUIRE(dr != nullptr);
  t.pad(pp(kRow3, 9), 10);
  t.wait(100);
  CHECK(t.param(kVoice, AcidVoice::Reso) == doctest::Approx(core::param_denorm(*dr, ladder_norm(9))).epsilon(0.001));
  CHECK(cutoff(t) == doctest::Approx(core::param_denorm(*dc, ladder_norm(7))).epsilon(0.001));  // the cutoff stayed
  CHECK(lamp(t, pp(kRow3, 9)) == app::kLedOn);
  // A switch (F6, the wave) is set at once to the pad's place.
  t.tap(Button::F6);
  const core::ParamDesc* dw = t.h->engine().param_desc(kVoice, AcidVoice::Wave);
  REQUIRE(dw != nullptr);
  t.pad(pp(kRow3, 11), 10);
  t.wait(20);
  CHECK(t.param(kVoice, AcidVoice::Wave) == doctest::Approx(dw->max));
  t.pad(pp(kRow3, 0), 10);
  t.wait(20);
  CHECK(t.param(kVoice, AcidVoice::Wave) == doctest::Approx(dw->min));
}

TEST_CASE("row 3 while the pattern plays and Rec is on: a ladder jump is written as motion on the knob's lane") {
  Hands t;
  t.to_perform();
  CHECK_FALSE(seq::lane_has_motion(t.pat(), 0));
  t.tap(Button::Play);
  t.wait(300);
  t.pad(pp(kRow3, 7), 10);  // not recording: nothing written
  t.wait(300);
  CHECK_FALSE(seq::lane_has_motion(t.pat(), 0));
  t.tap(Button::Rec);  // Rec is the 303 screen's mode, and the same here
  t.pad(pp(kRow3, 3), 10);
  t.wait(300);
  CHECK(seq::lane_has_motion(t.pat(), 0));
  CHECK_FALSE(seq::lane_has_motion(t.pat(), 1));
  t.tap(Button::F2);
  t.pad(pp(kRow3, 11), 10);  // RESO is lane 1
  t.wait(300);
  CHECK(seq::lane_has_motion(t.pat(), 1));
  t.tap(Button::Play);
}

TEST_CASE("WOBBLE on: row 3 is the wobble's rates (columns 1..11), the keys stay; ALL OFF ends the rate override with the action") {
  Hands t;
  t.to_perform();
  t.tap(Button::Play);
  t.wait(200);
  shift_pad(t, action_pad(PerfAction::Wobble));
  t.wait(40);
  CHECK(seqr().wobble().on);
  CHECK(seqr().wobble_rate_now() == seq::kWobble1_8);
  t.wait(100);
  CHECK(lamp(t, pp(kRow3, seq::kWobble1_8)) == app::kLedOn);
  CHECK(lamp(t, pp(kRow3, 0)) == app::kLedDim);
  CHECK(lamp(t, pp(kRow3, 11)) == app::kLedOff);  // the eleven rates only
  const float c = cutoff(t);
  t.pad(pp(kRow3, 2), 10);  // 1/4
  t.wait(30);
  CHECK(seqr().wobble().override_rate == seq::kWobble1_4);
  CHECK(seqr().wobble_rate_now() == seq::kWobble1_4);
  CHECK(cutoff(t) == c);  // not a ladder: the knob did not move
  t.pad(pp(kRow3, 11), 10);  // no rate there
  t.wait(30);
  CHECK(seqr().wobble_rate_now() == seq::kWobble1_4);
  t.wait(100);
  CHECK(lamp(t, pp(kRow3, 2)) == app::kLedOn);
  // Row 4 is still the keys.
  CHECK(lamp(t, pp(kRow4, 2)) == app::kLedDim);
  t.pad(all_off_pad(), 10);
  t.wait(30);
  CHECK_FALSE(seqr().wobble().on);
  CHECK(seqr().wobble().override_rate == seq::kWobbleRateNone);
  t.wait(100);
  CHECK(lamp(t, pp(kRow3, 2)) != app::kLedOn);  // the ladder again (the bar is at the cutoff's place)
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
  CHECK(lamp(t, pp(kRow4, 4)) == app::kLedDim);
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

TEST_CASE("the keys of PageKeys run over a table of their own (set_table) and over the global one by default") {
  PageKeys keys;
  CHECK(keys.page_count() == kPageCount);
  CHECK(&keys.page() == &page_at(0));
  keys.set_page(kPagePerf);
  CHECK(&keys.page() == &page_at(kPagePerf));
  keys.set_page(kPageCount);  // past the table: nothing changes
  CHECK(keys.page_index() == kPagePerf);

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
  keys.next_page();  // wraps over the table's two pages, not the global seven
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
  for (unsigned col = 0; col < kPerfActionCount; ++col) {  // all eleven actions latched
    shift_pad(t, pp(kRow1, col));
  }
  t.pad(pp(kRow2, 8), 10);  // NEW: a toast
  shift_pad(t, pp(kRow2, 0));
  t.pad(pp(kRow2, 0), 10);
  t.press(Button::Shift);  // the Shift row, with the toast still up
  t.wait(100);
  t.release(Button::Shift);
  t.pad(pp(kRow2, 8), 10);
  t.wait(100);
  t.to_perform();  // SETUP
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

TEST_CASE("Shift + a colour button lets go of an action latched on a PERFORM pad") {
  Hands t;
  t.to_perform();
  shift_pad(t, action_pad(PerfAction::Mute));  // Note carries MUTE
  CHECK(seqr().perf() == seq::kPerfMute);
  t.shift_tap(Button::Note);  // not a second latch on the button: the action goes
  CHECK(seqr().perf() == 0);
  t.shift_tap(Button::Note);  // now it is the button's own latch
  CHECK(seqr().perf() == seq::kPerfMute);
  t.shift_tap(Button::Note);
  CHECK(seqr().perf() == 0);
  // The same with a pad latch and a button latch on Freeze: both go.
  shift_pad(t, action_pad(PerfAction::Freeze));
  CHECK(freeze_active(t) == 1.0f);
  t.shift_tap(Button::Fx);
  CHECK(freeze_active(t) == 0.0f);
  t.shift_tap(Button::Fx);
  CHECK(freeze_active(t) == 1.0f);
  t.pad(all_off_pad(), 10);
  CHECK(freeze_active(t) == 0.0f);
}

TEST_CASE("an F key held across the page change does not stay held: the next turn moves the latched knob") {
  Hands t;
  t.to_perform();
  const float e = t.param(kVoice, AcidVoice::EnvMod);
  t.press(Button::F3);
  t.turn(1);  // ENV MOD moves while held
  CHECK(t.param(kVoice, AcidVoice::EnvMod) > e);
  t.to_perform();  // SETUP, with F3 still down
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
