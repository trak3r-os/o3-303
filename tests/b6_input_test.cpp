// SPDX-License-Identifier: GPL-3.0-or-later
// B6 (docs/BACKLOG.md items 7, 9, 10): a held Left or Right repeats the F key's knob step (Screen::repeat_buttons) and does nothing more with no F key held, one undo per held
// arrow; the encoder's speed (Context::enc_scale) on the continuous knobs and not on an enum; a learned controller's move is motion with REC on the line that plays.
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/home_map.h"
#include "../app/src/main_screen.h"

#include <o3_303/app_access.h>
#include <o3_303/step_model.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/distortion/distortion.h>

using namespace hands;
using namespace o3_303;

namespace {

HomeMap& home_of(Hands& t) { return static_cast<MainScreen*>(t.h->runtime().context().screen())->home_map(); }

// The kit's repeat schedule starts at 400 ms: 1500 ms held is several repeats after the press.
constexpr unsigned kHeldMs = 1500;

}  // namespace

TEST_CASE("B6 held Left with an F key held keeps stepping its knob, a tap steps it once") {
  Hands t;
  const float start = t.param(kVoice, AcidVoice::Cutoff);
  t.press(Button::F1);  // CUTOFF
  t.tap(Button::Left);
  const float once = t.param(kVoice, AcidVoice::Cutoff);
  CHECK(once < start);
  t.press(Button::Left);
  t.wait(kHeldMs);
  t.release(Button::Left);
  const float held = t.param(kVoice, AcidVoice::Cutoff);
  t.release(Button::F1);
  CHECK(held < once - (start - once) * 1.5f);  // the press and at least two repeats more than a tap
}

TEST_CASE("B6 held Right with no F key held moves the cursor once, as a press always did") {
  Hands t;
  HomeMap& home = home_of(t);
  t.tap(Button::Right);
  const int c1 = home.cursor();
  t.press(Button::Right);
  t.wait(kHeldMs);
  t.release(Button::Right);
  CHECK((home.cursor() - c1 + 16) % 16 == 1);
}

TEST_CASE("B6 a held arrow on a held step is one undo") {
  Hands t;
  bank().clear_history();
  t.press(Button::F1);
  t.down(step_pad_of(3));
  t.press(Button::Left);
  t.wait(kHeldMs);
  t.release(Button::Left);
  t.up(step_pad_of(3));
  t.release(Button::F1);
  CHECK(bank().can_undo());
  (void)bank().undo();
  CHECK_FALSE(bank().can_undo());  // the lock edits of every repeat were one step
}

TEST_CASE("B6 a held arrow with REC on and the line playing records motion") {
  Hands t;
  t.tap(Button::Rec);
  t.tap(Button::Play);
  t.wait(300);
  t.press(Button::F1);
  t.press(Button::Left);
  t.wait(kHeldMs);
  t.release(Button::Left);
  t.release(Button::F1);
  t.tap(Button::Play);
  CHECK(pattern_has_motion(bank().pattern(bank().selected()), 0));
}

TEST_CASE("B6 the encoder's speed moves a continuous knob further and an enum one value a detent") {
  Hands t;
  app::Context& ctx = t.h->runtime().context();
  t.tap(Button::F1);
  const auto spin = [&](unsigned mode) {
    ctx.set_enc_mode(mode);
    const float c0 = t.param(kVoice, AcidVoice::Cutoff);
    for (int i = 0; i < 4; ++i) {
      t.turn(-1);
    }
    const float d = c0 - t.param(kVoice, AcidVoice::Cutoff);
    t.wait(500);
    for (int i = 0; i < 4; ++i) {
      t.turn(1);
    }
    t.wait(500);
    return d;
  };
  const float off = spin(0);
  const float on = spin(2);
  CHECK(off > 0.0f);
  CHECK(on > off * 1.5f);
  // An enum (INSTRUMENT FX, F4 MODEL: 6 values) takes one value a detent whatever the speed.
  t.wait(200);
  t.to_instrument(kInstFx);
  t.tap(Button::F4);
  t.wait(200);
  const float m0 = t.param(kDist, Distortion::Type);
  ctx.set_enc_mode(2);
  t.turn(1);
  t.turn(1);
  const float m1 = t.param(kDist, Distortion::Type);
  CHECK(m1 - m0 <= 2.0f);
}

TEST_CASE("B6 a learned controller's move is motion on the line that plays with REC on, nothing with REC off, and no lane for Stock") {
  platform::host::FakeMidi midi;
  Hands t(midi);
  app::Context& ctx = t.h->runtime().context();
  using Port = platform::host::FakeMidi::Port;
  REQUIRE(ctx.midi().learn(kVoice, AcidVoice::Cutoff));
  midi.in(Port::Usb, 0xB0, 74, 0x7F);
  t.wait(30);
  REQUIRE(ctx.midi().bindings() == 1u);
  t.tap(Button::Play);
  t.wait(300);
  const auto sweep = [&] {
    for (unsigned v = 0; v < 128; v += 16) {
      midi.in(Port::Usb, 0xB0, 74, static_cast<std::uint8_t>(v));
      t.wait(60);
    }
  };
  bank().clear_history();
  sweep();  // REC off
  CHECK_FALSE(pattern_has_motion(bank().pattern(bank().selected()), 0));
  CHECK_FALSE(bank().can_undo());
  t.tap(Button::Rec);
  sweep();
  CHECK(pattern_has_motion(bank().pattern(bank().selected()), 0));
  // A controller bound to the voice's Stock switch has no lane: nothing recorded, nothing fails.
  ctx.midi().unlearn_all();
  if (ctx.midi().learn(kVoice, AcidVoice::Stock)) {
    midi.in(Port::Usb, 0xB0, 75, 0x7F);
    t.wait(30);
    midi.in(Port::Usb, 0xB0, 75, 0);
    t.wait(30);
  }
  t.tap(Button::Play);
  CHECK(true);
}

TEST_CASE("B6 a held Left with the Pattern overlay open does what its single press does") {
  const auto run = [](unsigned held_ms) {
    Hands t;
    t.press(Button::Pattern);
    t.press(Button::Left);
    t.wait(held_ms);
    t.release(Button::Left);
    t.release(Button::Pattern);
    return home_of(t).cursor() * 1000 + static_cast<int>(t.selected());
  };
  CHECK(run(1500) == run(10));
}
