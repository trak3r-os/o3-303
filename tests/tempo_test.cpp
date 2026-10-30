// SPDX-License-Identifier: GPL-3.0-or-later
// The tempo's places after M1 (docs/UX-303.md 2.1, 2.6, 5.6, D5): a tap of Master opens MASTER at the release with the encoder on TEMPO; hold Master and turn
// for a tempo nudge of 1 % a detent, at most 3 %, that snaps back at the release, from any screen and without changing it; Shift + Master taps the tempo;
// Shift + Play restarts; the swing is PATTERN SETUP's F2. The screen's own keys and readout are master_screen_test.cpp's.
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/main_screen.h"
#include "../app/src/master_screen.h"

#include <o3_303/perform.h>
#include <o3_303/units/distortion/distortion.h>

#include <algorithm>
#include <cstring>
#include <string>

using namespace hands;
using namespace o3_303;

namespace {

std::uint32_t tempo(Hands& t) { return t.h->engine().tempo(); }
bool is_title(Hands& t, const char* title) { return std::strcmp(t.title(), title) == 0; }
std::string title_of(Hands& t) { return t.title(); }

}  // namespace

TEST_CASE("a tap of Master opens MASTER at the release, the encoder on TEMPO; F1 and the other keys are the page's own") {
  Hands t;
  const std::uint32_t t0 = tempo(t);
  const float cutoff0 = t.param(kVoice, 0);
  REQUIRE(is_title(t, "STEPS"));
  t.press(Button::Master);
  t.wait(40);
  CHECK(is_title(t, "STEPS"));  // not at the press: a hold + turn nudges and must not change the screen
  t.release(Button::Master);
  t.wait(5);
  CHECK(is_title(t, "MASTER"));
  t.turn(2);  // half a BPM a detent
  CHECK(tempo(t) == t0 + 1000);
  CHECK(t.param(kVoice, 0) == cutoff0);  // SOUND's CUTOFF was not the one turned
  t.turn(-2);
  CHECK(tempo(t) == t0);
  t.press(Button::Shift);  // Shift + turn is the coarse step (ten detents)
  t.turn(1);
  t.release(Button::Shift);
  CHECK(tempo(t) == t0 + 5000);
  // F4 is the master level, not the tempo.
  const float level0 = t.h->engine().master_level();
  t.tap(Button::F4);
  t.turn(-3);
  CHECK(t.h->engine().master_level() != level0);
  CHECK(tempo(t) == t0 + 5000);
  // A tap of Master on MASTER puts the encoder back on TEMPO and stays.
  t.tap(Button::Master);
  CHECK(is_title(t, "MASTER"));
  t.turn(2);
  CHECK(tempo(t) == t0 + 6000);
}

TEST_CASE("MASTER from any screen; Pattern comes back HOME; the encoder enters on TEMPO every time") {
  Hands t;
  t.to_instrument();
  REQUIRE(std::strncmp(t.title(), "INSTRUMENT", 10) == 0);
  t.tap(Button::Master);
  CHECK(is_title(t, "MASTER"));
  t.tap(Button::F4);  // the latch moves to MASTER ...
  t.to_home();
  CHECK(is_title(t, "STEPS"));
  t.tap(Button::Master);  // ... and enters on TEMPO again
  CHECK(is_title(t, "MASTER"));
  const std::uint32_t t0 = tempo(t);
  t.turn(2);
  CHECK(tempo(t) == t0 + 1000);
  t.to_perform();
  CHECK(is_title(t, "PERFORM"));
  t.tap(Button::Master);
  CHECK(is_title(t, "MASTER"));
  t.tap(Button::Params);  // Params from MASTER opens INSTRUMENT VOICE
  CHECK(std::strncmp(t.title(), "INSTRUMENT", 10) == 0);
}

TEST_CASE("a press longer than 300 ms, a turn, or another press with Master down: no MASTER") {
  Hands t;
  t.press(Button::Master);
  t.wait(400);
  t.release(Button::Master);
  t.wait(5);
  CHECK(is_title(t, "STEPS"));  // a long hold alone does nothing
  // Master down, another screen button taps: that screen stays, the release is no tap.
  t.press(Button::Master);
  t.tap(Button::Params, 10);
  t.release(Button::Master);
  t.wait(5);
  CHECK(std::strncmp(t.title(), "INSTRUMENT", 10) == 0);
  // A turn: nothing opens either.
  t.to_home();
  t.press(Button::Master);
  t.turn(1);
  t.release(Button::Master);
  t.wait(5);
  CHECK(is_title(t, "STEPS"));
}

TEST_CASE("swing is PATTERN SETUP's F2: Pattern twice, F2, turn; Shift + turn is coarse") {
  Hands t;
  const unsigned swing0 = t.pat().swing;
  t.tap(Button::Pattern);  // PATTERN SETUP (Pattern on HOME)
  REQUIRE(is_title(t, "PATTERN SETUP"));
  t.tap(Button::F2);
  const std::uint32_t t0 = tempo(t);
  t.turn(3);
  CHECK(t.pat().swing == swing0 + 3);
  t.press(Button::Shift);
  t.turn(1);  // coarse: 5 percent
  t.release(Button::Shift);
  CHECK(t.pat().swing == swing0 + 8);
  CHECK(tempo(t) == t0);
}

TEST_CASE("Shift + a tap of Master does not latch SWING: it is a tap of the tap tempo and opens nothing") {
  Hands t;
  const unsigned swing0 = t.pat().swing;
  t.shift_tap(Button::Master);
  t.wait(5);
  CHECK(is_title(t, "STEPS"));
  t.turn(3);  // the page's CUTOFF, not the swing
  CHECK(t.pat().swing == swing0);
}

TEST_CASE("hold Master and turn: the tempo is nudged one percent a detent, at most three, and the release gives it back, on the same screen") {
  Hands t;
  const std::uint32_t t0 = tempo(t);  // the factory line's own tempo, whatever it is: the nudge is relative
  const std::string home = title_of(t);
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
  CHECK(title_of(t) == home);
  t.release(Button::Master);
  t.wait(5);
  CHECK(tempo(t) == t0);  // snapped back
  CHECK(title_of(t) == home);
  // It was a nudge, not a tap: no latch afterwards, the encoder is the page's (CUTOFF).
  const float cutoff0 = t.param(kVoice, 0);
  t.turn(10);
  CHECK(tempo(t) == t0);
  CHECK(t.param(kVoice, 0) != cutoff0);
}

TEST_CASE("the nudge works the same, and never changes the screen, on HOME, SETUP, INSTRUMENT, PERFORM and MASTER") {
  Hands t;
  const std::uint32_t t0 = tempo(t);
  const auto nudge_here = [&] {
    const std::string here = title_of(t);
    t.press(Button::Master);
    t.turn(2);
    CHECK(tempo(t) == nudged_tempo(t0, 2));
    CHECK(title_of(t) == here);
    t.release(Button::Master);
    t.wait(5);
    CHECK(tempo(t) == t0);
    CHECK(title_of(t) == here);
  };
  nudge_here();  // HOME
  t.tap(Button::Pattern);
  nudge_here();  // PATTERN SETUP
  t.to_instrument();
  nudge_here();
  t.to_perform();
  nudge_here();
  t.tap(Button::Master);  // MASTER itself
  REQUIRE(is_title(t, "MASTER"));
  nudge_here();
}

TEST_CASE("a nudge is relative to the tempo it started from, also after the tempo changed between two nudges") {
  Hands t;
  t.tap(Button::Master);  // MASTER, TEMPO latched
  t.turn(40);             // +20 BPM
  const std::uint32_t t0 = tempo(t);
  t.press(Button::Master);
  t.turn(3);
  CHECK(tempo(t) == nudged_tempo(t0, 3));
  t.release(Button::Master);
  CHECK(tempo(t) == t0);
  CHECK(is_title(t, "MASTER"));
  // Master released after a nudge is no tap: the latch stayed on TEMPO.
  t.turn(2);
  CHECK(tempo(t) == t0 + 1000);
}

TEST_CASE("a nudge goes on across a screen change with Master held and ends at its release") {
  Hands t;
  const std::uint32_t t0 = tempo(t);
  t.press(Button::Master);
  t.turn(2);
  CHECK(tempo(t) != t0);
  t.tap(Button::File);  // another screen's button, with Master down (FILE PROJECTS)
  CHECK_FALSE(is_title(t, "STEPS"));
  CHECK_FALSE(is_title(t, "MASTER"));
  CHECK(tempo(t) == nudged_tempo(t0, 2));
  t.turn(1);  // still the nudge's
  CHECK(tempo(t) == nudged_tempo(t0, 3));
  t.release(Button::Master);
  t.wait(5);
  CHECK(tempo(t) == t0);
  CHECK_FALSE(is_title(t, "MASTER"));  // the chord: no MASTER at the release
}

TEST_CASE("Shift + Master taps the tempo from any screen, without changing it; a gap over two seconds starts the measure again") {
  Hands t;
  t.to_instrument();
  const std::string here = title_of(t);
  t.press(Button::Shift);
  for (unsigned i = 0; i < 5; ++i) {
    t.tap(Button::Master, 30);
    t.wait(470);  // 500 ms between the taps: 120 BPM
  }
  t.release(Button::Shift);
  CHECK(title_of(t) == here);
  CHECK(tempo(t) > 118'000u);
  CHECK(tempo(t) < 122'000u);
  CHECK_FALSE(t.h->engine().playing());
  // A gap over two seconds starts the measure again: two taps a second apart are 60 BPM.
  t.wait(2500);
  t.press(Button::Shift);
  t.tap(Button::Master, 30);
  t.wait(970);
  t.tap(Button::Master, 30);
  t.release(Button::Shift);
  CHECK(tempo(t) > 58'000u);
  CHECK(tempo(t) < 62'000u);
  CHECK(title_of(t) == here);
}

TEST_CASE("Shift + Play restarts the pattern from step 1 and no longer taps the tempo; Play alone plays and stops") {
  Hands t;
  const std::uint32_t t0 = tempo(t);
  t.tap(Button::Play);
  t.wait(1300);  // two beats and a bit at 125 BPM
  REQUIRE(t.h->engine().playing());
  const std::uint64_t before = t.h->engine().stats().pos;
  REQUIRE(before > (std::uint64_t{1} << 32));  // past the first tick
  t.press(Button::Shift);
  t.tap(Button::Play);
  t.release(Button::Shift);
  t.wait(20);
  CHECK(t.h->engine().playing());  // it runs on
  CHECK(t.h->engine().stats().pos < before);  // from the top again
  CHECK(tempo(t) == t0);
  // Taps with Shift + Play leave the tempo alone.
  for (unsigned i = 0; i < 4; ++i) {
    t.press(Button::Shift);
    t.tap(Button::Play, 30);
    t.release(Button::Shift);
    t.wait(400);
  }
  CHECK(tempo(t) == t0);
  CHECK(t.h->engine().playing());
  t.tap(Button::Play);
  CHECK_FALSE(t.h->engine().playing());
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
  t.tap(Button::Master);  // MASTER, TEMPO latched
  t.turn(2);              // half a BPM a detent, from the own tempo
  CHECK(t.h->engine().own_tempo() == own + 1000);
  CHECK(tempo(t) == 140'000u);
  // The nudge while a clock leads does nothing (the own tempo is silent) and the status line says why; the release puts nothing back.
  t.to_home();
  t.press(Button::Master);
  t.turn(2);
  CHECK(t.h->engine().own_tempo() == own + 1000);
  CHECK(tempo(t) == 140'000u);
  frame::StatusView v;
  static_cast<MainScreen*>(t.h->runtime().context().screen())->home_map().status(v);
  CHECK(std::strcmp(v.toast, "MIDI CLOCK LEADS") == 0);
  CHECK(v.toast_off);
  t.release(Button::Master);
  t.wait(5);
  CHECK(is_title(t, "STEPS"));
  CHECK(t.h->engine().own_tempo() == own + 1000);
  // F2 TAP does nothing either.
  t.tap(Button::Master);
  REQUIRE(is_title(t, "MASTER"));
  t.tap(Button::F2);
  t.wait(500);
  t.tap(Button::F2);
  CHECK(static_cast<MasterScreen*>(t.h->runtime().context().screen())->taps() == 0u);
  CHECK(t.h->engine().own_tempo() == own + 1000);
  midi.clock_off();
  t.wait(1200);  // 48 clocks of a gone master (0.86 s at 140), then the own tempo again
  CHECK_FALSE(t.h->engine().following());
  CHECK(tempo(t) == own + 1000);
}

TEST_CASE("a tap of the tempo under a nudge makes the tapped tempo the base: the release does not bring the old one back") {
  Hands t;
  t.tap(Button::Master);  // MASTER
  REQUIRE(is_title(t, "MASTER"));
  t.press(Button::F3);
  t.turn(2);
  t.tap(Button::F2, 30);  // two taps 500 ms apart: 120 BPM
  t.wait(470);
  t.tap(Button::F2, 30);
  const std::uint32_t tapped = tempo(t);  // the tapped tempo with the nudge on it
  CHECK(tapped > nudged_tempo(118'000, 2));
  CHECK(tapped < nudged_tempo(122'000, 2));
  t.release(Button::F3);
  t.wait(5);
  CHECK(tempo(t) > 118'000u);  // the tap survives the release
  CHECK(tempo(t) < 122'000u);
  CHECK(tempo(t) == nudged_tempo(tempo(t), 0));
}

TEST_CASE("a nudge after a tap is from the tapped tempo") {
  Hands t;
  t.tap(Button::Master);
  t.tap(Button::F2, 30);
  t.wait(470);
  t.tap(Button::F2, 30);
  const std::uint32_t base = tempo(t);
  t.press(Button::F3);
  t.turn(-3);
  CHECK(tempo(t) == nudged_tempo(base, -3));
  t.release(Button::F3);
  CHECK(tempo(t) == base);
}

TEST_CASE("a restore that the full command ring refuses is sent again: the render does not keep the nudged tempo") {
  Hands t;
  t.tap(Button::Master);  // MASTER
  REQUIRE(is_title(t, "MASTER"));
  MasterScreen& m = *static_cast<MasterScreen*>(t.h->runtime().context().screen());
  const std::uint32_t t0 = tempo(t);
  t.press(Button::Master);
  t.turn(3);
  const std::uint32_t nudged = nudged_tempo(t0, 3);
  REQUIRE(tempo(t) == nudged);
  // Fill the engine's command ring (512) with the nudged tempo, then let go of Master at once: the restore finds no room.
  dsp::Engine& e = t.h->engine();
  for (unsigned i = 0; i < 700; ++i) {
    (void)e.set_tempo(nudged);
  }
  const std::uint32_t dropped = e.dropped_commands();
  m.master_release(t.h->runtime().context());
  CHECK(e.dropped_commands() == dropped + 1u);  // the restore was refused
  CHECK(m.nudge() == 0);
  t.wait(100);  // the render drains the ring; the app's service retries
  CHECK(e.stats().tempo_mbpm == t0);
  CHECK(e.own_tempo() == t0);
  t.release(Button::Master);  // (the panel's own release: nothing left to do)
  t.wait(5);
  CHECK(e.stats().tempo_mbpm == t0);
}
