// SPDX-License-Identifier: GPL-3.0-or-later
// The sound of the whole app: silent until something plays, the factory line's headroom, VOLUME and MUTE.
// (Split from the former o3_303_app_test.cpp; the hands are in app_hands.h.)
#include "app_hands.h"

#include <o3_303/units/acid_voice/acid_voice.h>

TEST_CASE("VOLUME is slot 0's level: the voice's own volume stays at 0 dB") {
  Hands t;
  CHECK(t.param(kVoice, AcidVoice::Volume) == doctest::Approx(0.0f));
  t.tap(Button::F8);
  t.turn(-4);
  CHECK(t.param(kVoice, AcidVoice::Volume) == doctest::Approx(0.0f));
  CHECK(t.h->engine().mix(0, dsp::MixField::Level) < 0.7f);
}

TEST_CASE("sound: silent until something plays; a key pad plays the voice; Play plays the factory line") {
  Hands t;
  t.h->record(true);
  t.wait(200);
  CHECK(rms(t.h->audio(), 0) < 1e-6);  // nothing played yet: digital silence

  t.down(key_pad(48));
  t.wait(300);
  t.up(key_pad(48));
  t.wait(600);
  CHECK(rms(t.h->audio(), 0) > 0.01);

  t.wait(2000);  // let the delay's tail go
  const std::size_t mark = t.h->audio().size();
  t.tap(Button::Play);
  t.wait(2500);
  CHECK(rms(t.h->audio(), mark) > 0.05);
  t.tap(Button::Play);
}

TEST_CASE("headroom: the factory line at the default levels stays under full scale before the clipper") {
  Hands t;
  t.tap(Button::Play);
  const float peak = peak_over(t, 4500);
  t.tap(Button::Play);
  CHECK(peak < 1.0f);  // measured 0.86 (the master's soft clip starts at 0.8: the engine counts those blocks as `clipped`)
}

TEST_CASE("sound: MUTE holds the notes back while the pattern runs; VOLUME is the level of the whole voice") {
  Hands t;
  t.h->record(true);
  t.tap(Button::Play);
  t.wait(2200);
  const std::size_t a0 = t.h->audio().size();
  t.wait(1500);
  const double playing = rms(std::vector<std::int16_t>(t.h->audio().begin() + static_cast<std::ptrdiff_t>(a0), t.h->audio().end()), 0);
  REQUIRE(playing > 0.05);

  t.tap(Button::Performance);  // PERFORM: the colour buttons are the live actions there (on HOME they are the drawer's rows)
  t.press(Button::Note);       // MUTE
  t.wait(1500);                // the echoes of the last notes die away
  const std::size_t m0 = t.h->audio().size();
  t.wait(1500);
  const double muted = rms(std::vector<std::int16_t>(t.h->audio().begin() + static_cast<std::ptrdiff_t>(m0), t.h->audio().end()), 0);
  CHECK(muted < playing * 0.15);
  t.release(Button::Note);
  t.wait(2000);
  const std::size_t b0 = t.h->audio().size();
  t.wait(1500);
  const double back = rms(std::vector<std::int16_t>(t.h->audio().begin() + static_cast<std::ptrdiff_t>(b0), t.h->audio().end()), 0);
  CHECK(back > playing * 0.5);

  t.tap(Button::F8);  // VOLUME: -6 dB down to about -24 dB
  t.turn(-36);
  t.wait(1500);
  const std::size_t v0 = t.h->audio().size();
  t.wait(1500);
  const double quiet = rms(std::vector<std::int16_t>(t.h->audio().begin() + static_cast<std::ptrdiff_t>(v0), t.h->audio().end()), 0);
  CHECK(quiet < back * 0.5);  // 18 dB down, but the hot signal is clipped before: well under half
  t.tap(Button::Play);
}

// The rms of the factory line over 1.5 s after 2.5 s of playing, with MASTER's F4 turned `detents` first (0: left at the default).
static double line_rms_with_master(int detents) {
  Hands t;
  t.h->record(true);
  t.tap(Button::Master);  // MASTER (M1)
  if (detents != 0) {
    t.tap(Button::F4);
    t.turn(detents);
  }
  t.tap(Button::Play);
  t.wait(2500);
  const std::size_t mark = t.h->audio().size();
  t.wait(1500);
  const double r = rms(std::vector<std::int16_t>(t.h->audio().begin() + static_cast<std::ptrdiff_t>(mark), t.h->audio().end()), 0);
  t.tap(Button::Play);
  return r;
}

TEST_CASE("MASTER's F4 is the level of the whole output: turned down, the line is quieter; the level is the engine's master") {
  const double full = line_rms_with_master(0);
  const double down = line_rms_with_master(-70);
  REQUIRE(full > 0.05);
  CHECK(down < full * 0.5);  // 100 % - 70 detents of 1 %: about 30 %
  Hands t;
  t.tap(Button::Master);
  t.tap(Button::F4);
  t.turn(-10);
  CHECK(t.h->engine().master_level() < 1.0f);
}
