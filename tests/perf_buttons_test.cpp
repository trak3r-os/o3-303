// SPDX-License-Identifier: GPL-3.0-or-later
// The four assignable performance buttons (docs/M3B-PLAN.md C4): Note, Instr, Vol and Fx each take one of MUTE, ACC ALL, SLIDE ALL, OCT UP,
// FILL, THROW, FREEZE (the PERF page); the defaults are MUTE, ACC ALL, FILL, FREEZE; held is momentary, Shift latches. The actions reach the
// sequencer's flags, the delay send, the live transpose and the beat freeze.
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

}  // namespace

TEST_CASE("the defaults: Note is MUTE, Instr ACC ALL, Vol FILL and Fx FREEZE, each while held") {
  Hands t;
  o3_303::seq::Sequencer& s = app_sequencer();
  CHECK(s.perf() == 0);
  CHECK(freeze_active(t) == 0.0f);

  t.press(Button::Note);
  CHECK(s.perf() == o3_303::seq::kPerfMute);
  t.release(Button::Note);
  CHECK(s.perf() == 0);

  t.press(Button::Instr);
  CHECK(s.perf() == o3_303::seq::kPerfAccAll);
  t.release(Button::Instr);
  CHECK(s.perf() == 0);

  t.press(Button::Vol);
  CHECK(s.perf() == o3_303::seq::kPerfFill);
  CHECK(s.fill_held());
  t.release(Button::Vol);
  CHECK(s.perf() == 0);
  CHECK_FALSE(s.fill_held());

  t.press(Button::Fx);
  CHECK(freeze_active(t) == 1.0f);
  CHECK(s.perf() == 0);  // the freeze is a unit's parameter, not a sequencer flag
  t.release(Button::Fx);
  CHECK(freeze_active(t) == 0.0f);

  // Together, and the old SLIDE ALL and THROW are not on any button by default.
  t.press(Button::Note);
  t.press(Button::Vol);
  t.press(Button::Fx);
  CHECK(s.perf() == (o3_303::seq::kPerfMute | o3_303::seq::kPerfFill));
  CHECK(freeze_active(t) == 1.0f);
  t.release(Button::Fx);
  t.release(Button::Vol);
  t.release(Button::Note);
  CHECK(s.perf() == 0);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(0.3f));
}

TEST_CASE("Shift + the button latches its action, Shift + it again lets go, and the button's own release does not undo it") {
  Hands t;
  o3_303::seq::Sequencer& s = app_sequencer();
  t.shift_tap(Button::Vol);
  CHECK(s.fill_held());
  t.shift_tap(Button::Fx);
  CHECK(freeze_active(t) == 1.0f);
  t.shift_tap(Button::Note);
  CHECK(s.perf() == (o3_303::seq::kPerfFill | o3_303::seq::kPerfMute));
  // A held press of a latched button, and its release, leave the latch.
  t.tap(Button::Vol);
  CHECK(s.fill_held());
  t.shift_tap(Button::Vol);
  CHECK_FALSE(s.fill_held());
  t.shift_tap(Button::Fx);
  CHECK(freeze_active(t) == 0.0f);
  t.shift_tap(Button::Note);
  CHECK(s.perf() == 0);
}

TEST_CASE("an assignment on the PERF page gives a button another action; all seven can be reached") {
  Hands t;
  o3_303::seq::Sequencer& s = app_sequencer();
  // Vol = SLIDE ALL, Fx = THROW: the two the defaults left.
  t.assign_perf(Button::Vol, PerfAction::SlideAll);
  t.assign_perf(Button::Fx, PerfAction::Throw);
  t.press(Button::Vol);
  CHECK(s.perf() == o3_303::seq::kPerfSlideAll);
  CHECK_FALSE(s.fill_held());
  t.release(Button::Vol);
  t.press(Button::Fx);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(1.0f));
  CHECK(freeze_active(t) == 0.0f);  // Fx is not the freeze any more
  t.release(Button::Fx);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(0.3f));
  // Note = FREEZE, Instr = FILL: any action on any button.
  t.assign_perf(Button::Note, PerfAction::Freeze);
  t.assign_perf(Button::Instr, PerfAction::Fill);
  t.press(Button::Note);
  CHECK(freeze_active(t) == 1.0f);
  CHECK(s.perf() == 0);
  t.release(Button::Note);
  t.press(Button::Instr);
  CHECK(s.fill_held());
  t.release(Button::Instr);
  CHECK(s.perf() == 0);
  // Back to the defaults by the same page.
  t.assign_perf(Button::Note, PerfAction::Mute);
  t.press(Button::Note);
  CHECK(s.perf() == o3_303::seq::kPerfMute);
  t.release(Button::Note);
}

TEST_CASE("OCT UP is an octave on the running pattern while it is on, on top of the keyboard's transposition") {
  Hands t;
  o3_303::seq::Sequencer& s = app_sequencer();
  t.assign_perf(Button::Instr, PerfAction::OctUp);
  CHECK(s.live_transpose() == 0);
  t.press(Button::Instr);
  CHECK(s.live_transpose() == 12);
  t.release(Button::Instr);
  CHECK(s.live_transpose() == 0);
  // Latched while the keyboard transposes: 7 + 12.
  t.tap(Button::Play);
  t.wait(200);
  t.shift_tap(Button::Instr);
  CHECK(s.live_transpose() == 12);
  t.pad(key_pad(43));  // G2: seven semitones over the bottom left C
  t.wait(30);
  CHECK(s.live_transpose() == 19);
  t.shift_tap(Button::Instr);
  CHECK(s.live_transpose() == 7);
  // It ends with the run, as the keyboard's does; a latch that is still on starts the next run with its octave.
  t.shift_tap(Button::Instr);
  CHECK(s.live_transpose() == 19);
  t.tap(Button::Play);
  t.wait(50);
  CHECK(s.live_transpose() == 12);
  t.shift_tap(Button::Instr);
  CHECK(s.live_transpose() == 0);
}

TEST_CASE("two buttons with one action: it is on while either is; a button reassigned loses its latch; a held one takes the new action") {
  Hands t;
  o3_303::seq::Sequencer& s = app_sequencer();
  t.assign_perf(Button::Instr, PerfAction::Mute);  // Note and Instr are both MUTE
  t.press(Button::Note);
  t.press(Button::Instr);
  CHECK(s.perf() == o3_303::seq::kPerfMute);
  t.release(Button::Note);
  CHECK(s.perf() == o3_303::seq::kPerfMute);
  t.release(Button::Instr);
  CHECK(s.perf() == 0);

  // A latched MUTE on Note, then Note is reassigned to FREEZE: the latch is gone, nothing is muted or frozen under the hand.
  t.shift_tap(Button::Note);
  CHECK(s.perf() == o3_303::seq::kPerfMute);
  t.assign_perf(Button::Note, PerfAction::Freeze);
  t.wait(50);
  CHECK(s.perf() == 0);
  CHECK(freeze_active(t) == 0.0f);
  t.tap(Button::Note);
  t.wait(5);
  CHECK(freeze_active(t) == 0.0f);

  // Instr is still a latched MUTE after Note's reassignment (a latch belongs to its own button).
  t.shift_tap(Button::Instr);
  CHECK(s.perf() == o3_303::seq::kPerfMute);
  t.shift_tap(Button::Instr);
  CHECK(s.perf() == 0);
}

TEST_CASE("leaving the 303 screen lets go of the held buttons and keeps the latches") {
  Hands t;
  o3_303::seq::Sequencer& s = app_sequencer();
  t.shift_tap(Button::Note);  // MUTE latched
  t.press(Button::Vol);       // FILL held
  t.press(Button::Fx);        // FREEZE held
  CHECK(s.fill_held());
  CHECK(freeze_active(t) == 1.0f);
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "VAULT") == 0);
  t.wait(10);
  CHECK(s.perf() == o3_303::seq::kPerfMute);
  CHECK(freeze_active(t) == 0.0f);
  t.release(Button::Vol);
  t.release(Button::Fx);
  t.tap(Button::Pattern);
  CHECK(s.perf() == o3_303::seq::kPerfMute);
  t.shift_tap(Button::Note);
  CHECK(s.perf() == 0);
}

TEST_CASE("the Song key does nothing (M3b's provisional freeze key is gone), and a run starts with the defaults again") {
  Hands t;
  o3_303::seq::Sequencer& s = app_sequencer();
  t.press(Button::Song);
  CHECK(freeze_active(t) == 0.0f);
  t.release(Button::Song);
  t.assign_perf(Button::Fx, PerfAction::OctUp);  // leave a non-default assignment behind for the next run
  Hands again;
  CHECK(again.h->runtime().context().screen() != nullptr);
  again.press(Button::Fx);
  CHECK(freeze_active(again) == 1.0f);  // Fx is FREEZE again: setup() resets the app's wiring
  again.release(Button::Fx);
  CHECK(s.live_transpose() == 0);
}
