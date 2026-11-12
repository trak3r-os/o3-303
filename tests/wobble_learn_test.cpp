// SPDX-License-Identifier: GPL-3.0-or-later
// WOBBLE's MIDI learn (docs/PADS-TRACT-PLAN.md D1): the proxy unit WobbleLearn (engine id 6, never routed) holds RATE, SHAPE, DEPTH, RESET, TARGET and WOB as parameters so the kit's Midi
// learn can bind a controller to them; the app writes a learned move to the settings (Controls::set) or flips the wobble latch, and the mirror keeps the engine's values on the panel's.
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/frame.h"
#include "../app/src/instrument_screen.h"
#include "../app/src/wobble_learn.h"

#include <o3sdk/core/param.h>
#include <o3sdk/platform/host/fake_midi.h>
#include <o3_303/app_access.h>

#include <cmath>
#include <cstring>
#include <string>

using namespace hands;
using namespace o3_303;
using Port = platform::host::FakeMidi::Port;

namespace {

constexpr dsp::UnitId kWobLearn = 6;  // attached after the tract (o3_303_app.cpp)

InstrumentScreen& screen_of(Hands& t) { return *static_cast<InstrumentScreen*>(t.h->runtime().context().screen()); }

view::Knob knob_of(Hands& t, unsigned k) {
  InstrumentView v;
  screen_of(t).fill_view(t.h->runtime().context(), v);
  return v.knob[k];
}

}  // namespace

TEST_CASE("the proxy's table is the five settings in WobbleField's order, then WOB, and nothing after") {
  const char* names[] = {"RATE", "SHAPE", "DEPTH", "RESET", "TARGET", "WOB"};
  for (std::uint16_t i = 0; i < 6; ++i) {
    const core::ParamDesc* d = wobble_learn_desc(i);
    REQUIRE(d != nullptr);
    CHECK(std::string(d->name) == names[i]);
  }
  CHECK(wobble_learn_desc(6) == nullptr);
  CHECK(WobbleLearn::kParamCount == 6u);
  CHECK(WobbleLearn::kWob == 5u);
  // The defaults are WobbleSettings'.
  const WobbleSettings ws;
  CHECK(wobble_learn_desc(kWobbleRate)->def == doctest::Approx(static_cast<float>(ws.rate)));
  CHECK(wobble_learn_desc(kWobbleShape)->def == doctest::Approx(static_cast<float>(ws.shape)));
  CHECK(wobble_learn_desc(kWobbleDepth)->def == doctest::Approx(static_cast<float>(ws.depth_pct)));
  CHECK(wobble_learn_desc(kWobbleReset)->def == doctest::Approx(ws.reset ? 1.0f : 0.0f));
  CHECK(wobble_learn_desc(kWobbleTarget)->def == doctest::Approx(static_cast<float>(ws.target)));
}

TEST_CASE("the engine has the proxy as unit 6 with six parameters, in no chain") {
  Hands t;
  dsp::Engine& e = t.h->engine();
  REQUIRE(e.unit_count() == 7u);
  REQUIRE(e.unit_info(kWobLearn) != nullptr);
  CHECK(std::string(e.unit_info(kWobLearn)->name) == "WOBBLE");
  CHECK(e.unit_info(kWobLearn)->param_count == 6u);
  CHECK(t.param(kWobLearn, kWobbleDepth) == doctest::Approx(50.0f));
}

TEST_CASE("a controller learned on WOBBLE's DEPTH moves the setting and the knob once it has caught the value (Pickup), and the running wobble takes it") {
  platform::host::FakeMidi midi;
  Hands t(midi);
  app::Context& ctx = t.h->runtime().context();
  t.to_wobble();
  t.tap(Button::Fx);  // WOB on: a running wobble takes a change of the settings at once
  t.wait(30);
  REQUIRE(app_sequencer().wobble().on);
  t.shift_tap(Button::F3);  // DEPTH
  REQUIRE(ctx.midi().learning());
  midi.in(Port::Usb, 0xB0, 74, 0x7F);
  t.wait(30);
  CHECK_FALSE(ctx.midi().learning());
  REQUIRE(ctx.midi().bindings() == 1u);
  midi.in(Port::Usb, 0xB0, 74, 0);  // far from the value (50): nothing yet
  t.wait(60);
  CHECK(t.param(kWobLearn, kWobbleDepth) == doctest::Approx(50.0f));
  midi.in(Port::Usb, 0xB0, 74, 0x7F);  // it went through 50 on the way: caught, and the controller's position is the value
  t.wait(60);
  CHECK(std::strcmp(knob_of(t, 2).label, "DEPTH") == 0);
  CHECK(t.param(kWobLearn, kWobbleDepth) == doctest::Approx(100.0f));
  CHECK(knob_of(t, 2).norm == doctest::Approx(1.0f));
  CHECK(app_sequencer().wobble().depth == doctest::Approx(1.0f));
  midi.in(Port::Usb, 0xB0, 74, 0);
  t.wait(60);
  CHECK(t.param(kWobLearn, kWobbleDepth) == doctest::Approx(0.0f));
  CHECK(app_sequencer().wobble().depth == doctest::Approx(0.0f));
  // The panel moves it too, and the mirror brings the engine's value along (Pickup reads it).
  t.tap(Button::F3);
  t.turn(5);
  t.wait(60);
  CHECK(t.param(kWobLearn, kWobbleDepth) > 0.0f);
  CHECK(app_sequencer().wobble().depth == doctest::Approx(t.param(kWobLearn, kWobbleDepth) / 100.0f).epsilon(0.02));
}

TEST_CASE("a controller learned on WOBBLE's RATE steps the rate through the list") {
  platform::host::FakeMidi midi;
  Hands t(midi);
  t.to_wobble();
  t.shift_tap(Button::F1);
  midi.in(Port::Usb, 0xB0, 74, 0x40);
  t.wait(30);
  REQUIRE(t.h->runtime().context().midi().bindings() == 1u);
  CHECK(std::strcmp(knob_of(t, 0).value, "1/8") == 0);
  midi.in(Port::Usb, 0xB0, 74, 0);  // the first message is only compared with the value (index 4 of 10)
  midi.in(Port::Usb, 0xB0, 74, 0x7F);  // ...the second crossed it
  t.wait(60);
  CHECK(t.param(kWobLearn, kWobbleRate) == doctest::Approx(10.0f));
  CHECK(std::strcmp(knob_of(t, 0).value, "1/16D") == 0);
  midi.in(Port::Usb, 0xB0, 74, 0);
  t.wait(60);
  CHECK(t.param(kWobLearn, kWobbleRate) == doctest::Approx(0.0f));
  CHECK(std::strcmp(knob_of(t, 0).value, "1/1") == 0);
}

TEST_CASE("a controller learned on WOB latches and releases the wobble action like F8 and the FX key") {
  platform::host::FakeMidi midi;
  Hands t(midi);
  seq::Sequencer& s = app_sequencer();
  t.to_wobble();
  t.shift_tap(Button::F8);  // the learn well on the WOBBLE page (E1b kept it: the maintainer asked for the learn here)
  REQUIRE(t.h->runtime().context().midi().learning());
  midi.in(Port::Usb, 0xB0, 75, 0x7F);
  t.wait(30);
  REQUIRE(t.h->runtime().context().midi().bindings() == 1u);
  midi.in(Port::Usb, 0xB0, 75, 0);
  t.wait(60);
  CHECK_FALSE(s.wobble().on);
  midi.in(Port::Usb, 0xB0, 75, 0x7F);
  t.wait(60);
  CHECK(s.wobble().on);
  CHECK(t.param(kWobLearn, WobbleLearn::kWob) == doctest::Approx(1.0f));
  t.tap(Button::F8);  // the panel's F8 lets it go (the FX key does the same); the engine's value follows
  t.wait(60);
  CHECK_FALSE(s.wobble().on);
  CHECK(t.param(kWobLearn, WobbleLearn::kWob) == doctest::Approx(0.0f));
}
