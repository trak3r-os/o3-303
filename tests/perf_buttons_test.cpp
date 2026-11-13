// SPDX-License-Identifier: GPL-3.0-or-later
// PERFORM's action pads on row 1 (docs/M3B-PLAN.md C4, UX-303 5.3): MUTE, ACC ALL, SLIDE ALL, OCT DOWN, OCT UP, FILL, THROW and the punches; a press holds the action
// while the pad is down, Shift + a press latches it, a tap on a latched pad lets it go. (The colour buttons carried these actions until PF1; they are the
// sounds' keys now, sound_buttons_test.cpp.) The actions reach the sequencer's flags, the delay send, the live transpose and the beat freeze.
#include <doctest/doctest.h>

#include "hands.h"

#include <o3sdk/dsp/engine.h>
#include <o3_303/app_access.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/freeze/freeze.h>

#include <cstring>

using namespace hands;
using o3_303::Freeze;
using o3_303::PerfAction;
using o3_303::app_sequencer;

namespace {

float freeze_active(Hands& t) { return t.param(kFreeze, Freeze::Active); }
void tap_pad(Hands& t, PerfAction a) { t.pad(perform_pad_of(a), 10); }

}  // namespace

TEST_CASE("the pads: MUTE, ACC ALL, FILL and FREEZE each while held; the actions reach the sequencer and the freeze unit") {
  Hands t;
  t.to_perform();
  o3_303::seq::Sequencer& s = app_sequencer();
  CHECK(s.perf() == 0);
  CHECK(freeze_active(t) == 0.0f);

  t.action_press(PerfAction::Mute);
  CHECK(s.perf() == o3_303::seq::kPerfMute);
  t.action_release(PerfAction::Mute);
  CHECK(s.perf() == 0);

  t.action_press(PerfAction::AccAll);
  CHECK(s.perf() == o3_303::seq::kPerfAccAll);
  t.action_release(PerfAction::AccAll);
  CHECK(s.perf() == 0);

  t.action_press(PerfAction::Fill);
  CHECK(s.perf() == o3_303::seq::kPerfFill);
  CHECK(s.fill_held());
  t.action_release(PerfAction::Fill);
  CHECK(s.perf() == 0);
  CHECK_FALSE(s.fill_held());

  t.action_press(PerfAction::Freeze);
  CHECK(freeze_active(t) == 1.0f);
  CHECK(s.perf() == 0);  // the freeze is a unit's parameter, not a sequencer flag
  t.action_release(PerfAction::Freeze);
  CHECK(freeze_active(t) == 0.0f);

  // Together.
  t.action_press(PerfAction::Mute);
  t.action_press(PerfAction::Fill);
  t.action_press(PerfAction::Freeze);
  CHECK(s.perf() == (o3_303::seq::kPerfMute | o3_303::seq::kPerfFill));
  CHECK(freeze_active(t) == 1.0f);
  t.action_release(PerfAction::Freeze);
  t.action_release(PerfAction::Fill);
  t.action_release(PerfAction::Mute);
  CHECK(s.perf() == 0);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(0.0f));
}

TEST_CASE("Shift + a pad latches its action, a tap on it lets go, the pad's own release does not undo it, and the colour keys leave it alone") {
  Hands t;
  t.to_perform();
  o3_303::seq::Sequencer& s = app_sequencer();
  t.latch(PerfAction::Fill);
  CHECK(s.fill_held());
  t.latch(PerfAction::Freeze);
  CHECK(freeze_active(t) == 1.0f);
  t.latch(PerfAction::Mute);
  CHECK(s.perf() == (o3_303::seq::kPerfFill | o3_303::seq::kPerfMute));
  // The colour keys are the sounds' (PF1): Vol is TRACT, with or without Shift, and the latched actions stay.
  t.tap(Button::Vol);
  t.shift_tap(Button::Vol);
  CHECK(s.fill_held());
  tap_pad(t, PerfAction::Fill);
  CHECK_FALSE(s.fill_held());
  tap_pad(t, PerfAction::Freeze);
  CHECK(freeze_active(t) == 0.0f);
  tap_pad(t, PerfAction::Mute);
  CHECK(s.perf() == 0);
}

TEST_CASE("SLIDE ALL and THROW are pads of row 1 too") {
  Hands t;
  t.to_perform();
  o3_303::seq::Sequencer& s = app_sequencer();
  t.action_press(PerfAction::SlideAll);
  CHECK(s.perf() == o3_303::seq::kPerfSlideAll);
  CHECK_FALSE(s.fill_held());
  t.action_release(PerfAction::SlideAll);
  t.action_press(PerfAction::Throw);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(1.0f));
  CHECK(freeze_active(t) == 0.0f);
  t.action_release(PerfAction::Throw);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(0.0f));
}

TEST_CASE("OCT UP is an octave on the running pattern while it is on, on top of the keyboard's transposition") {
  Hands t;
  o3_303::seq::Sequencer& s = app_sequencer();
  t.to_perform();
  CHECK(s.live_transpose() == 0);
  t.action_press(PerfAction::OctUp);
  CHECK(s.live_transpose() == 12);
  t.action_release(PerfAction::OctUp);
  CHECK(s.live_transpose() == 0);
  // Latched while the keyboard transposes (TRNSP on, HOME's pad; PERFORM's row 4 is the keyboard here): 7 + 12.
  t.to_home();
  t.toggle_trnsp();
  t.to_perform();
  t.tap(Button::Play);
  t.wait(200);
  t.latch(PerfAction::OctUp);
  CHECK(s.live_transpose() == 12);
  t.pad(key_pad(43));  // G2: seven semitones over the bottom left C
  t.wait(30);
  CHECK(s.live_transpose() == 19);
  tap_pad(t, PerfAction::OctUp);
  CHECK(s.live_transpose() == 7);
  // It ends with the run, as the keyboard's does; a latch that is still on starts the next run with its octave.
  t.latch(PerfAction::OctUp);
  CHECK(s.live_transpose() == 19);
  t.tap(Button::Play);
  t.wait(50);
  CHECK(s.live_transpose() == 12);
  tap_pad(t, PerfAction::OctUp);
  CHECK(s.live_transpose() == 0);
}

TEST_CASE("leaving PERFORM lets go of the held pads and keeps the latches") {
  Hands t;
  t.to_perform();
  o3_303::seq::Sequencer& s = app_sequencer();
  t.latch(PerfAction::Mute);                // MUTE latched
  t.action_press(PerfAction::Fill);         // FILL held
  t.action_press(PerfAction::Freeze);       // FREEZE held
  CHECK(s.fill_held());
  CHECK(freeze_active(t) == 1.0f);
  t.tap(Button::Params);  // INSTRUMENT
  CHECK(std::strcmp(t.title(), "INSTRUMENT FX") == 0);
  t.wait(10);
  CHECK(s.perf() == o3_303::seq::kPerfMute);
  CHECK(freeze_active(t) == 0.0f);
  t.action_release(PerfAction::Fill);
  t.action_release(PerfAction::Freeze);
  t.to_perform();
  CHECK(s.perf() == o3_303::seq::kPerfMute);
  tap_pad(t, PerfAction::Mute);
  CHECK(s.perf() == 0);
}

TEST_CASE("the Song key does nothing (M3b's provisional freeze key is gone), and a run starts afresh") {
  Hands t;
  o3_303::seq::Sequencer& s = app_sequencer();
  t.press(Button::Song);
  CHECK(freeze_active(t) == 0.0f);
  t.release(Button::Song);
  t.to_perform();
  t.latch(PerfAction::Mute);  // leave a latch behind for the next run
  Hands again;
  CHECK(again.h->runtime().context().screen() != nullptr);
  again.to_perform();
  again.action_press(PerfAction::Freeze);
  CHECK(freeze_active(again) == 1.0f);
  again.action_release(PerfAction::Freeze);
  CHECK(s.live_transpose() == 0);
  CHECK(s.perf() == 0);  // setup() resets the app's state: the old run's latch is gone
}
