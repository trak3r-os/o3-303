// SPDX-License-Identifier: GPL-3.0-or-later
// The obsidian skin's PERFORM and PERFORM SETUP (docs/SKIN-PLAN.md 2.4, SK4): the pure rules (a lamp's tone from the pad's LED level, where the 48 caps sit) and the screen end to end in the
// simulator: with the skin on, the lamp of each pad is drawn in the tone of the pad's LED (four tones: the LED's levels low and mid share one blue), in every state PERFORM has (idle, an action
// held, latched, a snapshot and its morph, the wobble's rates, the ladder, the keys playing or transposing). That is checked on the view and on what the frame queued: the tint of each Lamp
// sprite against the pad's LED. PERFORM SETUP shows row 1 only, as classic does (the rest of its glass is its text), so its test is row 1. The colour buttons' chips say what each button
// does, and the frame is queued whole. The pictures are skin_perform.txt's.
#include <doctest/doctest.h>

#include "hands.h"
#include "../app/src/perform_screen.h"
#include "../app/src/skin/panel.h"
#include "../app/src/skin/perform.h"

#include <o3sdk/app/pad_leds.h>
#include <o3_303/app_access.h>
#include <o3_303/pad_map.h>
#include <o3_303/perform.h>

#include <cstdint>
#include <cstring>
#include <string>

using namespace hands;
using namespace o3_303;

namespace {

using o3sdk::app::kLedDim;
using o3sdk::app::kLedLow;
using o3sdk::app::kLedMid;
using o3sdk::app::kLedOff;
using o3sdk::app::kLedOn;

// The pad of an action, and the pads of the other roles, as perform_screen_test.cpp names them.
unsigned action_pad(PerfAction a) { return app::pad_at(0, perform_action_pad(a)); }

// PERFORM, on the skin, with the last frame's view: the LEDs follow the screen by a frame, so every check waits one out first.
struct OnPerform {
  Hands t;
  PerformScreen* s = nullptr;
  OnPerform() {
    t.skin_on();
    t.to_perform();
    t.wait(60);
    s = static_cast<PerformScreen*>(t.h->runtime().context().screen());
    REQUIRE(std::strcmp(t.title(), "PERFORM") == 0);
  }
  // Every lamp of the frame just drawn is its pad's LED (in the view), the sprite queued for it is tinted for that LED's tone, and the frame was queued whole. `setup`: PERFORM SETUP, whose
  // glass shows row 1 only.
  void mirrors(bool setup = false) {
    t.wait(60);
    const PerformView& v = s->last_view();
    for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
      CHECK_MESSAGE(v.lamp[pad].level == t.led(pad), "pad " << pad);
      CHECK(lamp_tone(v.lamp[pad].level) == lamp_tone(t.led(pad)));
    }
    // The queue: one Lamp sprite for each pad drawn, at the pad's cap box, in the tint of the tone of the pad's LED.
    const unsigned drawn = setup ? app::kPadCols : app::kPadCount;
    unsigned seen[app::kPadCount] = {};
    unsigned lamps = 0;
    const skin::Panel& q = skin::panel();
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::SpriteInfo sp;
      if (!q.sprite_at(i, sp) || sp.id != skin_assets::Id::Lamp) {
        continue;
      }
      ++lamps;
      bool found = false;
      for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
        const ui::Rect b = skin::lamp_box(app::pad_row(pad), app::pad_col(pad));
        if (b.x == sp.x && b.y == sp.y) {
          found = true;
          ++seen[pad];
          const ui::Rgb want = skin::lamp_tint(lamp_tone(t.led(pad)));
          CHECK_MESSAGE((sp.rgb.r == want.r && sp.rgb.g == want.g && sp.rgb.b == want.b), "pad " << pad << "'s lamp is not tinted for its LED");
        }
      }
      CHECK(found);
    }
    CHECK(lamps == drawn);
    for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
      CHECK_MESSAGE(seen[pad] == (pad < drawn ? 1u : 0u), "pad " << pad);
    }
    CHECK(skin::panel().dropped() == 0u);
    CHECK(skin::panel().shapes() < skin::Panel::kShapes);
    CHECK(skin::panel().texts() < skin::Panel::kTexts);
  }
};

}  // namespace

TEST_CASE("skin perform: a lamp's tone is the pad's LED level in four steps") {
  CHECK(lamp_tone(kLedOff) == LampTone::Off);
  CHECK(lamp_tone(kLedDim - 1) == LampTone::Off);
  CHECK(lamp_tone(kLedDim) == LampTone::Dim);
  CHECK(lamp_tone(kLedLow - 1) == LampTone::Dim);
  CHECK(lamp_tone(kLedLow) == LampTone::Mid);
  CHECK(lamp_tone(kLedMid) == LampTone::Mid);
  CHECK(lamp_tone(kLedOn - 1) == LampTone::Mid);
  CHECK(lamp_tone(kLedOn) == LampTone::On);
  // The tone never falls as the level rises, and four distinct tints say the four tones.
  unsigned last = 0;
  for (unsigned level = 0; level < 256; ++level) {
    const unsigned tone = static_cast<unsigned>(lamp_tone(static_cast<std::uint8_t>(level)));
    CHECK(tone >= last);
    last = tone;
  }
  for (unsigned a = 0; a < 4; ++a) {
    for (unsigned b = a + 1; b < 4; ++b) {
      const ui::Rgb ta = skin::lamp_tint(static_cast<LampTone>(a));
      const ui::Rgb tb = skin::lamp_tint(static_cast<LampTone>(b));
      CHECK((ta.r != tb.r || ta.g != tb.g || ta.b != tb.b));
    }
  }
}

TEST_CASE("skin perform: the 48 caps are one over each pad, in the glass, apart from each other, over the chips") {
  constexpr int kGlassTop = 66;
  constexpr int kGlassBottom = 384;
  constexpr int kChipsTop = 318;  // the colour buttons' line under them
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    const ui::Rect b = skin::lamp_box(app::pad_row(pad), app::pad_col(pad));
    CHECK(b.x >= 3);
    CHECK(b.x + b.w <= 800 - 3);
    CHECK(b.y >= kGlassTop + 3);
    CHECK(b.y + b.h <= kChipsTop);
    CHECK(b.y + b.h <= kGlassBottom);
    for (unsigned other = pad + 1; other < app::kPadCount; ++other) {
      const ui::Rect o = skin::lamp_box(app::pad_row(other), app::pad_col(other));
      // The cap's face (one pixel in from the sprite's box, 56 x 36) never touches another's.
      const bool apart = b.x + 57 <= o.x + 1 || o.x + 57 <= b.x + 1 || b.y + 37 <= o.y + 1 || o.y + 37 <= b.y + 1;
      CHECK_MESSAGE(apart, pad << " and " << other);
    }
  }
  // A column is one x for every row and a row one y for every column: the caps line up as the pads do.
  for (unsigned row = 1; row < 4; ++row) {
    CHECK(skin::lamp_box(row, 0).x == skin::lamp_box(0, 0).x);
    CHECK(skin::lamp_box(row, 5).y == skin::lamp_box(row, 0).y);
  }
}

TEST_CASE("skin perform: each lamp is drawn in the tone of its pad's LED in every state of PERFORM, and SETUP shows row 1") {
  OnPerform o;
  o.mirrors();  // idle: the ladder of F1 CUTOFF, the snapshots empty, the naturals dim

  // An action held on its pad: lit while it is down, not after.
  o.t.down(action_pad(PerfAction::Mute));
  o.mirrors();
  CHECK(o.s->last_view().lamp[action_pad(PerfAction::Mute)].level == kLedOn);
  o.t.up(action_pad(PerfAction::Mute));
  o.mirrors();

  // Latched with Shift: lit, with the ring (latched) in the view.
  o.t.press(Button::Shift);
  o.t.pad(action_pad(PerfAction::Freeze), 10);
  o.t.pad(action_pad(PerfAction::Roll), 10);
  o.t.release(Button::Shift);
  o.mirrors();
  CHECK(o.s->last_view().lamp[action_pad(PerfAction::Freeze)].latched);
  CHECK(o.s->last_view().lamp[action_pad(PerfAction::Roll)].latched);
  CHECK_FALSE(o.s->last_view().lamp[action_pad(PerfAction::Mute)].latched);
  o.t.pad(app::pad_at(0, 11), 10);  // ALL OFF
  o.mirrors();
  CHECK_FALSE(o.s->last_view().lamp[action_pad(PerfAction::Freeze)].latched);

  // Snapshots: stored (Shift + pad), recalled (the lit slot, the morph).
  o.t.press(Button::Shift);
  o.t.pad(app::pad_at(1, 0), 10);
  o.t.release(Button::Shift);
  o.t.turn(20);
  o.t.press(Button::Shift);
  o.t.pad(app::pad_at(1, 1), 10);
  o.t.release(Button::Shift);
  o.mirrors();
  o.t.pad(app::pad_at(1, 0), 10);
  o.mirrors();
  CHECK(o.s->morphing());
  o.t.turn(-8);
  o.mirrors();

  // The ladder follows the latched knob; a tap moves its place.
  o.t.tap(Button::F2, 10);
  o.t.pad(app::pad_at(2, 8), 10);
  o.t.wait(300);  // the jump is a slew
  o.mirrors();
  unsigned place = 12;  // a bar of mid lamps up to the knob's place, which is on, and off after it
  for (unsigned c = 0; c < 12; ++c) {
    if (o.s->last_view().lamp[app::pad_at(2, c)].level == kLedOn) {
      place = c;
    }
  }
  REQUIRE(place < 12);
  CHECK(place > 3);
  for (unsigned c = 0; c < 12; ++c) {
    const std::uint8_t level = o.s->last_view().lamp[app::pad_at(2, c)].level;
    CHECK(level == (c < place ? kLedMid : (c == place ? kLedOn : kLedOff)));
  }

  // WOBBLE: row 3 is the rate picker.
  o.t.press(Button::Shift);
  o.t.pad(action_pad(PerfAction::Wobble), 10);
  o.t.release(Button::Shift);
  o.t.pad(app::pad_at(2, 4), 10);
  o.mirrors();
  CHECK(std::strcmp(o.s->last_view().ladder_caption, "WOBBLE RATE") == 0);
  o.t.press(Button::Shift);
  o.t.pad(action_pad(PerfAction::Wobble), 10);
  o.t.release(Button::Shift);

  // The keys play the voice (a held key is lit), then transpose the running pattern.
  o.t.down(app::pad_at(3, 0));
  o.mirrors();
  CHECK(o.s->last_view().lamp[app::pad_at(3, 0)].level == kLedOn);
  o.t.up(app::pad_at(3, 0));
  o.t.to_home();
  o.t.toggle_trnsp();
  o.t.tap(Button::Play, 10);
  o.t.wait(100);
  o.t.to_perform();
  o.t.pad(app::pad_at(3, 7), 10);
  o.mirrors();
  CHECK(std::strcmp(o.s->last_view().keys_caption, "TRANSPOSE") == 0);
  CHECK(o.s->last_view().lamp[app::pad_at(3, 7)].level == kLedOn);

  // PERFORM SETUP: the pads stay PERFORM's, and so do the LEDs; the glass shows row 1 of them as a reminder.
  o.t.to_perform();  // KNOBS
  o.t.to_perform();
  o.t.wait(60);
  CHECK(std::strcmp(o.t.title(), "PERFORM SETUP") == 0);
  o.mirrors(true);
}

TEST_CASE("skin perform: the chips say what each colour button does, held and latched") {
  OnPerform o;
  o.t.wait(60);
  const PerformView& v = o.s->last_view();
  static constexpr const char* kNames[kPerfButtons] = {"NOTE", "INSTR", "VOL", "FX"};
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    CHECK(std::string(v.chip[b].name) == kNames[b]);
    CHECK(std::string(v.chip[b].action) == kPerfActionNames[kDefaultPerf[b]]);
    CHECK(v.chip[b].state == ChipState::Idle);
  }
  o.t.press(Button::Instr);
  o.t.wait(60);
  CHECK(v.chip[1].state == ChipState::Held);
  CHECK(v.chip[0].state == ChipState::Idle);
  o.t.release(Button::Instr);
  o.t.press(Button::Shift);
  o.t.tap(Button::Vol, 10);
  o.t.release(Button::Shift);
  o.t.wait(60);
  CHECK(v.chip[2].state == ChipState::Latched);
  CHECK(v.chip[1].state == ChipState::Idle);
  CHECK(o.s->last_view().lamp[action_pad(static_cast<PerfAction>(kDefaultPerf[2]))].level == kLedOn);
}

TEST_CASE("skin perform: the Shift row is REDO and UNDO as action keys, and SETUP has none") {
  OnPerform o;
  app::Context& ctx = o.t.h->runtime().context();
  CHECK(o.s->chrome(ctx) == app::Chrome::None);
  CHECK(o.s->theme(ctx) != nullptr);
  o.t.press(Button::Shift);
  o.t.wait(60);
  CHECK(skin::panel().dropped() == 0u);
  o.t.release(Button::Shift);
  o.t.to_perform();  // KNOBS
  o.t.to_perform();  // SETUP
  o.t.press(Button::Shift);
  o.t.wait(60);
  CHECK(skin::panel().dropped() == 0u);
  o.t.release(Button::Shift);
  o.t.skin_off();
  CHECK(o.s->chrome(ctx) == app::Chrome::Full);
  CHECK(o.s->theme(ctx) == nullptr);
}

TEST_CASE("skin perform: the worst frame (every action on, every button latched, a toast) fits the queue with room to spare") {
  OnPerform o;
  o.t.press(Button::Shift);
  for (Button b : {Button::Note, Button::Instr, Button::Vol, Button::Fx}) {  // the buttons first: Shift + a button on an action already latched lets it go
    o.t.tap(b, 10);
  }
  for (unsigned pad : {2u, 3u, 5u, 7u, 8u, 9u, 10u}) {
    o.t.pad(pad, 10);
  }
  o.t.release(Button::Shift);
  o.t.pad(app::pad_at(1, 2), 10);  // an empty snapshot slot: a toast
  o.mirrors();
  const PerformView& v = o.s->last_view();
  unsigned latched = 0;
  for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
    latched += v.lamp[pad].latched ? 1u : 0u;
  }
  CHECK(latched == 11u);  // a button's latch rings its action's lamp too
  for (unsigned pad = 0; pad < 11; ++pad) {
    CHECK_MESSAGE(v.lamp[pad].level == kLedOn, "action pad " << pad);
  }
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    CHECK(v.chip[b].state == ChipState::Latched);
  }
  MESSAGE("worst frame: " << skin::panel().shapes() << " shapes of " << skin::Panel::kShapes << ", " << skin::panel().texts() << " texts of " << skin::Panel::kTexts);
  CHECK(skin::panel().shapes() <= 210u);  // measured 202 of 256
  CHECK(skin::panel().texts() <= 120u);   // measured 108 of 160
}
