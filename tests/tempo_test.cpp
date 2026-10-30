// SPDX-License-Identifier: GPL-3.0-or-later
// Tempo and swing get permanent places (docs/M3B-PLAN.md R7): a tap of Master latches TEMPO on the encoder, Shift + a tap of Master latches
// SWING, whatever page the F keys are on; hold Master and turn for a tempo nudge of 1 % a detent, at most 3 %, that snaps back at the
// release.
#include <doctest/doctest.h>

#include "hands.h"

#include <o3_303/perform.h>
#include <o3_303/units/distortion/distortion.h>

#include <algorithm>
#include <cstring>

using namespace hands;
using namespace o3_303;

namespace {

std::uint32_t tempo(Hands& t) { return t.h->engine().tempo(); }

}  // namespace

TEST_CASE("a tap of Master latches TEMPO on the encoder, until an F key or another tap of Master") {
  Hands t;
  const std::uint32_t t0 = tempo(t);
  const float cutoff0 = t.param(kVoice, 0);
  t.tap(Button::Master);
  t.turn(2);  // half a BPM a detent
  CHECK(tempo(t) == t0 + 1000);
  CHECK(t.param(kVoice, 0) == cutoff0);  // SOUND's CUTOFF, the page's own latched key, was not the one turned
  // (INSTRUMENT has no Master gesture until MASTER, M1: the latch is the 303 screen's.)
  t.turn(-2);
  CHECK(tempo(t) == t0);
  // Shift + turn is the coarse step (ten detents).
  t.press(Button::Shift);
  t.turn(1);
  t.release(Button::Shift);
  CHECK(tempo(t) == t0 + 5000);
  // A second tap of Master gives the encoder back to the page: F1 (CUTOFF) is the latched key.
  const float drive0 = t.param(kVoice, 0);
  t.tap(Button::Master);
  const std::uint32_t t1 = tempo(t);
  t.tap(Button::F1);
  t.turn(-5);
  CHECK(tempo(t) == t1);
  CHECK(t.param(kVoice, 0) != drive0);
  // An F key's tap also gives it back, whatever latched it: Master, then F2.
  t.tap(Button::Master);
  t.turn(2);
  CHECK(tempo(t) == t1 + 1000);
  const float reso0 = t.param(kVoice, 1);
  t.tap(Button::F2);
  t.turn(3);
  CHECK(tempo(t) == t1 + 1000);
  CHECK(t.param(kVoice, 1) != reso0);
}

TEST_CASE("Shift + a tap of Master latches SWING on the encoder; a tap of Master switches to TEMPO and back") {
  Hands t;
  const std::uint32_t t0 = tempo(t);
  const unsigned swing0 = t.pat().swing;
  t.shift_tap(Button::Master);
  t.turn(3);
  CHECK(t.pat().swing == swing0 + 3);
  CHECK(tempo(t) == t0);
  t.press(Button::Shift);
  t.turn(1);  // coarse: 5 percent
  t.release(Button::Shift);
  CHECK(t.pat().swing == swing0 + 8);
  // A plain tap latches the tempo (the swing latch is not the one tapped); a plain tap again lets go.
  t.tap(Button::Master);
  t.turn(2);
  CHECK(tempo(t) == t0 + 1000);
  CHECK(t.pat().swing == swing0 + 8);
  t.shift_tap(Button::Master);  // Shift + tap: SWING again
  t.turn(-1);
  CHECK(t.pat().swing == swing0 + 7);
  t.shift_tap(Button::Master);  // the same latch again: back to the page
  t.turn(1);
  CHECK(t.pat().swing == swing0 + 7);
  CHECK(tempo(t) == t0 + 1000);
}

TEST_CASE("hold Master and turn: the tempo is nudged one percent a detent, at most three, and the release gives it back") {
  Hands t;
  const std::uint32_t t0 = tempo(t);  // 125.0 BPM after the factory line's own tempo, whatever it is: the nudge is relative
  t.press(Button::Master);
  t.turn(1);
  CHECK(tempo(t) == nudged_tempo(t0, 1));
  t.turn(1);
  CHECK(tempo(t) == nudged_tempo(t0, 2));
  t.turn(5);  // clamped at +3 %
  CHECK(tempo(t) == nudged_tempo(t0, 3));
  CHECK(tempo(t) > t0);
  t.turn(-4);  // 3 - 4: -1 %
  CHECK(tempo(t) == nudged_tempo(t0, -1));
  t.turn(-9);
  CHECK(tempo(t) == nudged_tempo(t0, -3));
  t.turn(3);  // back to zero: the base, exactly
  CHECK(tempo(t) == t0);
  t.turn(2);
  CHECK(tempo(t) == nudged_tempo(t0, 2));
  t.release(Button::Master);
  CHECK(tempo(t) == t0);  // snapped back
  // It was a nudge, not a tap: no latch afterwards, the encoder is the page's (CUTOFF).
  const float cutoff0 = t.param(kVoice, 0);
  t.turn(10);
  CHECK(tempo(t) == t0);
  CHECK(t.param(kVoice, 0) != cutoff0);
}

TEST_CASE("a nudge is relative to the tempo it started from, also after the tempo changed between two nudges") {
  Hands t;
  t.tap(Button::Master);  // TEMPO latched
  t.turn(40);             // +20 BPM
  const std::uint32_t t0 = tempo(t);
  t.press(Button::Master);
  t.turn(3);
  CHECK(tempo(t) == nudged_tempo(t0, 3));
  t.release(Button::Master);
  CHECK(tempo(t) == t0);
  // The latch is still the tempo's: Master released after a nudge is no tap, so the latch stayed as it was (on).
  t.turn(2);
  CHECK(tempo(t) == t0 + 1000);
}

TEST_CASE("a nudge ends when the screen is left; the vault does not keep the tempo moved") {
  Hands t;
  const std::uint32_t t0 = tempo(t);
  t.press(Button::Master);
  t.turn(2);
  CHECK(tempo(t) != t0);
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "VAULT") == 0);
  CHECK(tempo(t) == t0);
  t.release(Button::Master);
  t.tap(Button::Pattern);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  CHECK(tempo(t) == t0);
}

TEST_CASE("MIDI: setup routes the voice; while a master's clock leads, TEMPO and the nudge step the own tempo, not the master's") {
  platform::host::FakeMidi midi;
  Hands t(midi);
  app::Midi& m = t.h->runtime().context().midi();
  CHECK(m.routes() == 1u);
  // A note on the user's channel (omni by default) plays the voice.
  t.h->record(true);
  midi.in(platform::host::FakeMidi::Port::Usb, 0x90, 45, 110);
  t.wait(120);
  int peak = 0;
  for (const std::int16_t s : t.h->audio()) {
    peak = std::max(peak, s < 0 ? -static_cast<int>(s) : static_cast<int>(s));
  }
  CHECK(peak > 1000);
  midi.in(platform::host::FakeMidi::Port::Usb, 0x80, 45, 0);
  t.h->record(false);

  const std::uint32_t own = t.h->engine().own_tempo();
  hal::midi::Config c = m.config();
  c.follow = true;
  m.set_config(c);
  midi.clock_tempo(140'000);
  t.wait(20);
  REQUIRE(t.h->engine().following());
  CHECK(tempo(t) == 140'000u);
  t.tap(Button::Master);
  t.turn(2);  // half a BPM a detent, from the own tempo
  CHECK(t.h->engine().own_tempo() == own + 1000);
  CHECK(tempo(t) == 140'000u);
  t.tap(Button::Master);
  // The nudge: hold Master and turn; the release puts the own tempo back, not the master's.
  t.press(Button::Master);
  t.turn(2);
  t.release(Button::Master);
  CHECK(t.h->engine().own_tempo() == own + 1000);
  midi.clock_off();
  t.wait(1200);  // 48 clocks of a gone master (0.86 s at 140), then the own tempo again
  CHECK_FALSE(t.h->engine().following());
  CHECK(tempo(t) == own + 1000);
}
