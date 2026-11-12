// SPDX-License-Identifier: GPL-3.0-or-later
// The TRACT page (TRACT-KNOBS-PLAN A2, PADS-TRACT): Sample Editor opens it, the VOL key is the on / off key that remembers the last mix, the pads are the pattern's 16 steps with RANDOMIZE, PLAY TRACT
// and VOWEL MODE beside them and the vowel keys, nudges or pickers below (one page: the old LINE layer is gone), the strip says each step's vowel and its TONGUE and LIPS values, the wobble's MOUTH
// target swings VOWEL, and the TARGET refusal with the tract off.
// The pure parts (the read-out, the vowel table, the page entries) are in pages_test.cpp.
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/frame.h"
#include "../app/src/home_map.h"
#include "../app/src/instrument_screen.h"
#include "../app/src/main_screen.h"

#include <o3sdk/dsp/engine.h>
#include <o3_303/app_access.h>
#include <o3_303/seq/ops.h>
#include <o3_303/step_model.h>
#include <o3_303/tract_line.h>
#include <o3_303/tract_vowels.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/tract/tract.h>

#include <o3sdk/app/app.h>
#include <o3sdk/sim/harness.h>
#include <o3sdk/sim/scenario.h>

#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using namespace hands;
using o3_303::TractFx;
using o3_303::app_sequencer;

namespace {

o3_303::HomeMap& home_of(Hands&) { return o3_303::app_home_map(); }  // the shared home map: valid on every screen of the HOME class (a cast of screen() to MainScreen is only so on HOME)

}  // namespace

TEST_CASE("tract page: Sample Editor opens TRACT (one page, again nothing); the other Sample buttons are each their own screen (E1a)") {
  Hands t;
  CHECK(std::strcmp(t.title(), "TRACT") != 0);
  t.tap(Button::SampleEdit);
  CHECK(std::strcmp(t.title(), "TRACT") == 0);
  t.tap(Button::SampleEdit);
  CHECK(std::strcmp(t.title(), "TRACT") == 0);
  t.tap(Button::SamplePlay);
  CHECK(std::strcmp(t.title(), "GROWL") == 0);
  t.to_tract();
  CHECK(std::strcmp(t.title(), "TRACT") == 0);
  t.to_wobble();
  CHECK(std::strcmp(t.title(), "WOBBLE") == 0);
  t.to_tract();
  CHECK(std::strcmp(t.title(), "TRACT") == 0);
  t.to_home();
  t.to_growl();
  CHECK(std::strcmp(t.title(), "GROWL") == 0);
}

TEST_CASE("tract page: the VOL key (Fx1) is the on / off key (Mix 0 off, the last mix on), F7 is the mix, MIDI-learnable knobs F1 to F7") {
  Hands t;
  t.to_tract();
  CHECK(t.param(kTract, TractFx::Mix) == 0.0f);  // silent at a boot
  t.tap(Button::Vol);
  CHECK(t.param(kTract, TractFx::Mix) == doctest::Approx(0.5f));
  t.tap(Button::F7);  // F7 focuses the mix
  t.turn(4);
  const float mix = t.param(kTract, TractFx::Mix);
  CHECK(mix > 0.5f);
  t.tap(Button::Vol);  // off: Mix 0, the unit is a bit-exact bypass
  CHECK(t.param(kTract, TractFx::Mix) == 0.0f);
  t.tap(Button::Vol);  // on: the mix it had
  CHECK(t.param(kTract, TractFx::Mix) == doctest::Approx(mix));
  t.tap(Button::F1);
  t.turn(3);
  CHECK(t.param(kTract, TractFx::Vowel) > 0.0f);  // the knob moves the unit
}

namespace {

// The TRACT page's pads (PADS-TRACT; 0-based rows and columns, row 0 at the top): rows 0 and 1, columns 0-7 are the pattern's 16 steps, columns 9 and 11 the keys (0,9 RANDOMIZE ALL; 0,11 PLAY TRACT;
// 1,9 RANDOMIZE VOWELS; 1,11 VOWEL MODE). Rows 2 and 3 are the NORMAL mode's A E I / O U - and the TONGUE and LIPS nudges (columns 4 and 6), or VOWEL MODE's pickers.
unsigned key_pad(unsigned row, unsigned col) { return app::pad_at(row, col); }
unsigned pal_pad(unsigned col) { return app::pad_at(2, col); }
unsigned low_pad(unsigned col) { return app::pad_at(3, col); }
std::uint8_t step_of(unsigned s) { return bank().tract_step(bank().selected(), s); }
unsigned vowel_of(unsigned s) { return o3_303::tract_vowel(step_of(s)); }
unsigned line_len_of(Hands& t) { return o3_303::tract_length(t.pat().tract, t.pat().length != 0u ? t.pat().length : o3_303::kDefaultLength); }

// The view the screen on show draws from (fill_view): the strip's cells, the tongue and lips texts.
o3_303::InstrumentView view_now(Hands& t) {
  app::Context& ctx = t.h->runtime().context();
  o3_303::InstrumentView v;
  static_cast<o3_303::InstrumentScreen*>(ctx.screen())->fill_view(ctx, v);
  return v;
}

// The levels a pad shows over 1.2 s (a blinking one is seen at two).
struct Seen {
  bool off = false, low = false, on = false;
};
Seen watch(Hands& t, unsigned pad) {
  Seen s;
  for (unsigned i = 0; i < 12; ++i) {
    t.wait(100);
    const std::uint8_t l = t.led(pad);
    s.off = s.off || l == app::kLedOff;
    s.low = s.low || l == app::kLedLow;
    s.on = s.on || l == app::kLedOn;
  }
  return s;
}

// How many of the pattern's lanes drive the tract.
unsigned tract_lanes(Hands& t) {
  unsigned n = 0;
  for (unsigned lane = 0; lane < o3_303::kMotionLanes; ++lane) {
    if (t.pat().lanes[lane].target != o3_303::kLaneTract) {
      continue;
    }
    bool used = false;  // a lane whose points are gone keeps its target: it is free, not in use
    for (unsigned s = 0; s < o3_303::kMaxSteps; ++s) {
      used = used || o3_303::step_lock_point(t.pat(), lane, s) != o3_303::kNoLock;
    }
    n += used ? 1u : 0u;
  }
  return n;
}

std::string toast_now(Hands& t) {
  o3_303::frame::StatusView sv;
  home_of(t).status(sv);  // the home map the screens share: the toast an INSTRUMENT-class screen raised
  return sv.toast;
}

}  // namespace

TEST_CASE("tract page pads: rows 1 and 2 are the pattern's steps with the four keys beside them, the columns between are dark, normal mode lights A E I O U - and the nudges") {
  Hands t;
  t.to_tract();
  t.wait(60);
  REQUIRE(line_len_of(t) == 16u);
  for (unsigned r = 0; r < 2u; ++r) {
    CHECK(t.led(key_pad(r, 8)) == app::kLedOff);
    CHECK(t.led(key_pad(r, 10)) == app::kLedOff);
  }
  CHECK(t.led(key_pad(0, 9)) == app::kLedLow);
  CHECK(t.led(key_pad(1, 9)) == app::kLedLow);
  CHECK(t.led(key_pad(1, 11)) == app::kLedLow);  // VOWEL MODE off
  // PLAY TRACT blinks: high while the tract pattern plays (the default), low once the knobs rule.
  Seen on = watch(t, key_pad(0, 11));
  CHECK((on.on && on.off && !on.low));
  t.pad(key_pad(0, 11));
  Seen off = watch(t, key_pad(0, 11));
  CHECK((off.low && off.off && !off.on));
  t.pad(key_pad(0, 11));  // back on: the sequencer outlives the Hands, the next test starts from the default
  // Normal mode: the six vowel keys and the four nudges lit, the rest of rows 3 and 4 dark.
  for (unsigned r = 2; r < 4u; ++r) {
    for (unsigned c = 0; c < app::kPadCols; ++c) {
      const bool key = c < 3u || c == 4u || c == 6u;
      CAPTURE(r);
      CAPTURE(c);
      CHECK((t.led(app::pad_at(r, c)) != app::kLedOff) == key);
    }
  }
}

TEST_CASE("tract page pads, normal mode: A E I and O U - write the vowel under the held steps, else the cursor's and go on; the dark pads do nothing; one undo each") {
  Hands t;
  t.to_tract();
  REQUIRE(line_len_of(t) == 16u);
  bank().clear_history();
  t.pad(2);
  CHECK(home_of(t).cursor() == 2);
  t.pad(low_pad(0));  // O on step 2, the cursor goes on to 3
  CHECK(vowel_of(2) == 4u);
  CHECK(home_of(t).cursor() == 3);
  t.pad(pal_pad(0));  // A
  t.pad(pal_pad(1));  // E
  CHECK(vowel_of(3) == 1u);
  CHECK(vowel_of(4) == 2u);
  t.pad(pal_pad(2));  // I on step 5
  CHECK(vowel_of(5) == 3u);
  // Held steps: one write on both, the cursor stays. The second row is steps 8 to 15.
  t.down(7);
  t.down(app::pad_at(1, 2));  // step 10
  t.pad(low_pad(1));          // U
  t.up(app::pad_at(1, 2));
  t.up(7);
  CHECK(vowel_of(7) == 5u);
  CHECK(vowel_of(10) == 5u);
  // "-" clears.
  CHECK(bank().set_tract_step(t.selected(), 12, 2, false));
  bank().clear_history();  // the direct write above shares the paint's undo key: start the undo stack after it
  t.down(app::pad_at(1, 4));  // step 12
  t.pad(low_pad(2));
  t.up(app::pad_at(1, 4));
  CHECK(step_of(12) == 0u);
  // The dark pads (the rest of the key rows, the gaps between the keys, the columns past 9) change nothing: no step, no lock, no knob.
  const auto before = t.pat();
  const float vowel = t.param(kTract, TractFx::Vowel);
  for (const unsigned p : {key_pad(2, 3), key_pad(2, 5), key_pad(2, 7), key_pad(2, 11), key_pad(3, 3), key_pad(3, 5), key_pad(3, 11), key_pad(0, 8), key_pad(1, 10)}) {
    t.pad(p);
  }
  t.wait(20);
  CHECK(std::memcmp(&before, &t.pat(), sizeof before) == 0);
  CHECK(t.param(kTract, TractFx::Vowel) == vowel);
  CHECK(std::strcmp(t.title(), "TRACT") == 0);
  // One undo for the "-": step 12 is a vowel again (HOME's Shift + F8); the notes are not touched by any of it.
  t.to_home();
  t.shift_tap(Button::F8);
  CHECK(vowel_of(12) == 2u);
  for (unsigned n = 0; n < 16; ++n) {
    CHECK(t.pat().steps[n].flags == before.steps[n].flags);
  }
}

TEST_CASE("tract page pads, normal mode: the TONGUE and LIPS nudges move the lock of the held steps (else the cursor's) by 10 from its own value or the knob's, clamped at 100") {
  Hands t;
  t.to_tract();
  REQUIRE(tract_lanes(t) == 0u);
  REQUIRE(t.param(kTract, TractFx::Tongue) == 0.0f);
  t.pad(3);
  t.pad(pal_pad(4));  // TONGUE + on the cursor's step, from the knob (0)
  CHECK(std::strcmp(view_now(t).cell[3].tongue_txt, "+10") == 0);
  t.pad(pal_pad(4));
  CHECK(std::strcmp(view_now(t).cell[3].tongue_txt, "+20") == 0);
  t.pad(pal_pad(6));  // LIPS +
  t.pad(low_pad(6));  // LIPS -: back to 0
  CHECK(std::strcmp(view_now(t).cell[3].lips_txt, "+0") == 0);
  CHECK(tract_lanes(t) == 2u);
  for (unsigned i = 0; i < 13u; ++i) {
    t.pad(low_pad(4));  // TONGUE - to the floor
  }
  CHECK(std::strcmp(view_now(t).cell[3].tongue_txt, "-100") == 0);
  // Held: each step moves from its own lock (step 3 has -100, step 4 none: the knob's 0).
  t.down(3);
  t.down(4);
  t.pad(pal_pad(4));
  t.up(4);
  t.up(3);
  const o3_303::InstrumentView v = view_now(t);
  CHECK(std::strcmp(v.cell[3].tongue_txt, "-90") == 0);
  CHECK(std::strcmp(v.cell[4].tongue_txt, "+10") == 0);
  CHECK(std::strcmp(v.cell[5].tongue_txt, "") == 0);
  CHECK(t.param(kTract, TractFx::Tongue) == 0.0f);  // the knob is untouched: these are locks
  CHECK(t.param(kTract, TractFx::Lips) == 0.0f);
  CHECK(tract_lanes(t) == 2u);
  // Shift + F5 CLEAR takes the locks away with the vowels.
  t.shift_tap(Button::F5);
  CHECK(tract_lanes(t) == 0u);
}

TEST_CASE("tract page pads, VOWEL MODE: the vowel, SIZE, GLIDE, TONGUE and LIPS pickers set the knobs, or the held steps' values; the LEDs follow; the key toggles back") {
  Hands t;
  t.to_tract();
  t.pad(key_pad(1, 11));
  CHECK(toast_now(t) == "VOWEL MODE ON");
  t.wait(60);
  CHECK(t.led(key_pad(1, 11)) == app::kLedOn);
  const auto count = [&](unsigned row, unsigned first, unsigned last, std::uint8_t level) {
    unsigned n = 0;
    for (unsigned c = first; c <= last; ++c) {
      n += t.led(app::pad_at(row, c)) == level ? 1u : 0u;
    }
    return n;
  };
  CHECK(count(2, 0, 4, app::kLedOn) == 1u);   // the vowel the knob says
  CHECK(count(2, 0, 4, app::kLedLow) == 4u);
  CHECK(t.led(pal_pad(5)) == app::kLedOff);   // SIZE: light off
  CHECK(t.led(pal_pad(6)) == app::kLedOff);
  CHECK(count(2, 7, 11, app::kLedOn) == 1u);  // the glide's nearest
  CHECK(count(2, 7, 11, app::kLedLow) == 4u);
  CHECK(count(3, 0, 5, app::kLedOn) == 1u);   // the tongue
  CHECK(count(3, 0, 5, app::kLedLow) == 5u);
  CHECK(count(3, 6, 11, app::kLedOn) == 1u);  // the lips
  CHECK(count(3, 6, 11, app::kLedLow) == 5u);
  const auto pattern = t.pat();
  // The vowel pads write the VOWEL knob.
  t.pad(pal_pad(3));  // O
  t.wait(20);
  CHECK(t.param(kTract, TractFx::Vowel) == doctest::Approx(0.75f));
  CHECK(t.led(pal_pad(3)) == app::kLedOn);
  CHECK(count(2, 0, 4, app::kLedOn) == 1u);
  // GLIDE: 5, 16, 50, 158, 500 ms.
  const float glide[5] = {5.0f, 16.0f, 50.0f, 158.0f, 500.0f};
  for (unsigned i = 0; i < 5u; ++i) {
    t.pad(pal_pad(7 + i));
    t.wait(20);
    CHECK(t.param(kTract, TractFx::Glide) == doctest::Approx(glide[i]));
    CHECK(t.led(pal_pad(7 + i)) == app::kLedOn);
  }
  // SIZE: each tap the next of the five, round (the knob starts at 2).
  const float size[5] = {3.0f, 4.0f, 0.0f, 1.0f, 2.0f};
  for (const float want : size) {
    t.pad(pal_pad(5));
    CHECK(t.param(kTract, TractFx::Size) == doctest::Approx(want));
    CHECK(t.led(pal_pad(5)) == app::kLedOff);
  }
  // TONGUE and LIPS at -100, -60, -20, +20, +60, +100.
  const float bend[6] = {-100.0f, -60.0f, -20.0f, 20.0f, 60.0f, 100.0f};
  for (unsigned i = 0; i < 6u; ++i) {
    t.pad(low_pad(i));
    t.pad(low_pad(6 + (5u - i)));
    t.wait(20);
    CHECK(t.param(kTract, TractFx::Tongue) == doctest::Approx(bend[i]));
    CHECK(t.param(kTract, TractFx::Lips) == doctest::Approx(bend[5u - i]));
    CHECK(t.led(low_pad(i)) == app::kLedOn);
    CHECK(t.led(low_pad(6 + (5u - i))) == app::kLedOn);
  }
  CHECK(tract_lanes(t) == 0u);  // no step held: knobs only
  CHECK(std::memcmp(&pattern, &t.pat(), sizeof pattern) == 0);
  // With a step held the pickers write that step: the vowel on the line, the bends as locks; the knobs stay.
  t.pad(low_pad(0));  // TONGUE knob -100 (it is -100 and LIPS -100 after the loop above: reset them apart)
  t.down(2);
  t.pad(pal_pad(1));            // E on step 2
  t.pad(low_pad(5));            // TONGUE +100
  t.pad(low_pad(6));            // LIPS -100
  t.up(2);
  CHECK(vowel_of(2) == 2u);
  const o3_303::InstrumentView v = view_now(t);
  CHECK(std::strcmp(v.cell[2].tongue_txt, "+100") == 0);
  CHECK(std::strcmp(v.cell[2].lips_txt, "-100") == 0);
  CHECK(t.param(kTract, TractFx::Tongue) == doctest::Approx(-100.0f));
  CHECK(t.param(kTract, TractFx::Vowel) == doctest::Approx(0.75f));
  // The key again: the normal pads are back.
  t.pad(key_pad(1, 11));
  CHECK(toast_now(t) == "VOWEL MODE OFF");
  t.wait(60);
  CHECK(t.led(key_pad(1, 11)) == app::kLedLow);
  CHECK(t.led(pal_pad(0)) != app::kLedOff);
  CHECK(t.led(pal_pad(7)) == app::kLedOff);
}

TEST_CASE("tract page pads: RANDOMIZE ALL draws the vowels and a TONGUE and a LIPS lock on every step, RANDOMIZE VOWELS only the vowels, each one undo step and a new draw each tap") {
  Hands t;
  t.to_tract();
  REQUIRE(tract_lanes(t) == 0u);
  bank().clear_history();
  const auto vowels = [&] {
    unsigned n = 0;
    for (unsigned s = 0; s < 16; ++s) {
      n += vowel_of(s) != 0u ? 1u : 0u;
    }
    return n;
  };
  t.pad(key_pad(1, 9));  // vowels
  CHECK(vowels() > 0u);
  CHECK(tract_lanes(t) == 0u);
  CHECK(toast_now(t) == "NEW TRACT VOWELS");
  CHECK(bank().undo() == t.selected());
  CHECK(vowels() == 0u);
  CHECK_FALSE(bank().can_undo());
  t.wait(7);
  t.pad(key_pad(0, 9));  // all
  CHECK(toast_now(t) == "NEW TRACT PATTERN");
  CHECK(vowels() > 0u);
  CHECK(tract_lanes(t) == 2u);
  const int tongue = o3_303::seq::find_lane(t.pat(), o3_303::kLaneTract, TractFx::Tongue);
  const int lips = o3_303::seq::find_lane(t.pat(), o3_303::kLaneTract, TractFx::Lips);
  REQUIRE(tongue >= 0);
  REQUIRE(lips >= 0);
  for (unsigned s = 0; s < 16; ++s) {
    CHECK(o3_303::step_lock_point(t.pat(), static_cast<unsigned>(tongue), s) != o3_303::kNoLock);
    CHECK(o3_303::step_lock_point(t.pat(), static_cast<unsigned>(lips), s) != o3_303::kNoLock);
  }
  CHECK(o3_303::step_lock_point(t.pat(), static_cast<unsigned>(tongue), 16) == o3_303::kNoLock);
  const auto first = t.pat();
  t.wait(7);
  t.pad(key_pad(0, 9));  // a new seed each tap
  CHECK(std::memcmp(&first, &t.pat(), sizeof first) != 0);
  CHECK(bank().undo() == t.selected());  // one step back: the first draw
  CHECK(std::memcmp(&first, &t.pat(), sizeof first) == 0);
  CHECK(bank().undo() == t.selected());  // and the second: nothing
  CHECK(vowels() == 0u);
  CHECK(tract_lanes(t) == 0u);
  CHECK_FALSE(bank().can_undo());
  // The same seed draws the same pattern (Bank::random_tract_all).
  CHECK(bank().random_tract_all(t.selected(), 77u, TractFx::Tongue, TractFx::Lips));
  const auto a = t.pat();
  CHECK(bank().undo() == t.selected());
  CHECK(bank().random_tract_all(t.selected(), 77u, TractFx::Tongue, TractFx::Lips));
  CHECK(std::memcmp(&a, &t.pat(), sizeof a) == 0);
}

TEST_CASE("tract page pads: PLAY TRACT toggles the sequencer's tract pattern (on by default), the toast says which, the project keeps it") {
  Hands t;
  t.to_tract();
  CHECK(app_sequencer().tract_line_on());
  t.pad(key_pad(0, 11));
  CHECK_FALSE(app_sequencer().tract_line_on());
  CHECK(toast_now(t) == "PLAY TRACT: KNOBS");
  t.pad(key_pad(0, 11));
  CHECK(app_sequencer().tract_line_on());
  CHECK(toast_now(t) == "PLAY TRACT: PATTERN");
}

TEST_CASE("tract page: Left and Right move the cursor, Shift + F4 steps the length of the vowel line (on STEPS too), Shift + F5 clears the line") {
  Hands t;
  t.to_tract();
  t.pad(4);
  t.shift_tap(Button::Right);
  t.pad(pal_pad(0));  // A on step 5
  CHECK(vowel_of(5) == 1u);
  t.shift_tap(Button::Left);  // from the cursor (6) back to 5
  t.shift_tap(Button::Left);
  t.pad(pal_pad(1));  // E on step 4
  CHECK(vowel_of(4) == 2u);
  // LEN: SAME (16) -> 4 -> 8 -> 16 -> 32 -> 64 -> SAME.
  CHECK(t.pat().tract.len == o3_303::kTractSame);
  const unsigned want[] = {4, 8, 16, 32, 64, 0};
  for (unsigned w : want) {
    t.shift_tap(Button::F4);
    CHECK(t.pat().tract.len == w);
    CHECK(std::strcmp(t.title(), "TRACT") == 0);
  }
  // CLEAR.
  t.shift_tap(Button::F5);
  for (unsigned n = 0; n < 16; ++n) {
    CHECK(step_of(n) == 0u);
  }
  CHECK(t.pat().tract.len == o3_303::kTractSame);
}

TEST_CASE("tract page: F8 is GAIN (B2), the wet level: it turns the unit's id 7 and is MIDI-learnable") {
  Hands t;
  t.to_tract();
  app::Context& ctx = t.h->runtime().context();
  CHECK(t.param(kTract, TractFx::Gain) == doctest::Approx(o3_303::kTractGainDefaultDb));
  t.tap(Button::F8);
  t.turn(4);
  CHECK(t.param(kTract, TractFx::Gain) > o3_303::kTractGainDefaultDb);
  t.shift_tap(Button::F8);  // Shift + F8 arms MIDI learn on it
  CHECK(ctx.midi().learning());
}

TEST_CASE("tract page: the Shift row shows LEN, CLEAR and NEW on F4 to F6 and LEARN on the other unit knobs") {
  Hands t;
  t.to_tract();
  app::Context& ctx = t.h->runtime().context();
  const auto row = [&](unsigned k) {
    app::SoftKeys keys;
    ctx.screen()->soft_keys(ctx, keys);
    return std::string(keys.label[k] != nullptr ? keys.label[k] : "");
  };
  CHECK(row(3).empty());
  t.press(Button::Shift);
  CHECK(row(0) == "LEARN");
  CHECK(row(3) == "LEN");
  CHECK(row(4) == "CLEAR");
  CHECK(row(5) == "NEW");
  CHECK(row(6) == "LEARN");
  CHECK(row(7) == "LEARN");
  t.release(Button::Shift);
}

TEST_CASE("tract page: Shift + F6 NEW draws a line over the line's length, one undo step, the other steps stay") {
  Hands t;
  t.to_tract();
  REQUIRE(line_len_of(t) == 16u);
  for (unsigned n = 0; n < 64; ++n) {
    REQUIRE(step_of(n) == 0u);
  }
  bank().clear_history();
  CHECK(bank().set_tract_step(t.selected(), 30, 5, true));  // above the length: it stays
  bank().clear_history();
  t.shift_tap(Button::F6);
  CHECK(std::strcmp(t.title(), "TRACT") == 0);
  unsigned vowels = 0;
  for (unsigned n = 0; n < 16; ++n) {
    vowels += vowel_of(n) != 0u ? 1u : 0u;
  }
  CHECK(vowels > 0u);
  CHECK(vowel_of(0) != 0u);
  CHECK(step_of(30) == (5u | o3_303::kTractGlide));
  CHECK(bank().can_undo());
  CHECK(bank().undo() == t.selected());
  for (unsigned n = 0; n < 16; ++n) {
    CHECK(step_of(n) == 0u);
  }
  CHECK_FALSE(bank().can_undo());
  t.shift_tap(Button::F6);  // a new seed each press
  std::uint8_t first[16];
  for (unsigned n = 0; n < 16; ++n) {
    first[n] = step_of(n);
  }
  t.wait(7);
  t.shift_tap(Button::F6);
  bool differs = false;
  for (unsigned n = 0; n < 16; ++n) {
    differs = differs || first[n] != step_of(n);
  }
  CHECK(differs);
}

TEST_CASE("tract page: the wobble's TARGET stops before MOUTH while the tract is off (TRACT IS OFF), and goes on with it on") {
  Hands t;
  (void)t.h->engine().set_param(kGrowl, o3_303::Growl::Mix, 0.5f);  // VOWEL needs the growl
  t.to_wobble();
  t.tap(Button::Fx);  // WOB on: the sequencer takes the target at once
  t.tap(Button::F4);  // the TARGET key
  t.turn(1);
  CHECK(app_sequencer().wobble().target.target == o3_303::kLaneGrowl);
  t.turn(1);  // MOUTH: refused, the tract is off
  CHECK(app_sequencer().wobble().target.target == o3_303::kLaneGrowl);
  t.to_home();
  o3_303::frame::StatusView sv;
  home_of(t).status(sv);
  CHECK(std::string(sv.toast) == "TRACT IS OFF");
  (void)t.h->engine().set_param(kTract, TractFx::Mix, 0.5f);
  t.to_wobble();
  t.tap(Button::F4);
  t.turn(1);
  CHECK(app_sequencer().wobble().target.target == o3_303::kLaneTract);
  CHECK(app_sequencer().wobble().target.id == TractFx::Vowel);
  t.turn(-2);  // and back to CUTOFF
  CHECK(app_sequencer().wobble().target.target == o3_303::kLaneVoice);
}

namespace {

// The factory line for about four seconds from a boot, as a scenario's WAV: `setup` is the gestures before the Play (the recording starts after them).
std::string render(const std::string& setup) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / ("o3_303_tract_page_test_" + std::to_string(static_cast<long>(getpid())));
  fs::remove_all(dir);
  fs::create_directories(dir);
  auto h = std::make_unique<o3sdk::sim::Harness>(o3sdk_app(), nullptr, nullptr);
  REQUIRE(h->start());
  // The gestures first (recording off), then the clock is brought to the same millisecond whatever they took, so that the two renders start the line on the same block.
  o3sdk::sim::ScenarioResult r = o3sdk::sim::run_scenario(*h, "wait 300\n" + setup, dir.string());
  CHECK_MESSAGE(r.ok, "line " << r.line << ": " << r.error);
  const std::uint64_t at = h->uptime_us() / 1000u;
  REQUIRE(at < 6000u);
  h->run_ms(static_cast<std::uint32_t>(6000u - at));
  r = o3sdk::sim::run_scenario(*h, "record on\ntap play\nwait 3800\ntap play\nwait 300\nwav take\n", dir.string());
  CHECK_MESSAGE(r.ok, "line " << r.line << ": " << r.error);
  std::ifstream in(dir / "take.wav", std::ios::binary);
  std::stringstream bytes;
  bytes << in.rdbuf();
  fs::remove_all(dir);
  return bytes.str();
}

}  // namespace

TEST_CASE("tract page: the VOL key off is the dry chain bit for bit, whatever the knobs say (A2): the same factory line with and without a visit to the page") {
  const std::string dry = render("");
  REQUIRE(dry.size() > 44);
  // Every knob moved, the vowels played, the VOL key on and off again, a visit to GROWL's page and back: Mix is 0 again, the tract is a bypass.
  const std::string visited = render(
      "tap sampleedit\ntap vol\ntap f1\nturn 40\ntap f2\nturn -30\ntap f3\nturn 50\ntap f4\nturn 20\ntap f5\nturn 10\ntap f6\nturn 2\ntap f7\nturn 60\n"
      "paddown 24\nwait 60\npadup 24\ntap sampleplay\ntap sampleedit\ntap vol\ntap pattern\n");
  CHECK(visited.size() == dry.size());
  CHECK(visited == dry);
  // And the control: with the tract on (Mix 50 %, the vowel U) the line is not the dry one.
  const std::string on = render("tap sampleedit\ntap vol\ntap f1\nturn 40\ntap pattern\n");
  CHECK(on.size() == dry.size());
  CHECK(on != dry);
}

TEST_CASE("tract page: the wobble on MOUTH changes the sound (the tract's vowel moves with the line), and it is the tract's, not the growl's") {
  const std::string setup_tract = "tap sampleedit\ntap vol\ntap f7\nturn 50\ntap f1\nturn 20\n";
  const std::string still = render(setup_tract + "tap pattern\n");
  // WOBBLE: DEPTH 100 %, TARGET up from CUTOFF (the growl is off and the tract on: the turn goes past VOWEL to MOUTH), the latch on.
  const std::string mouth = render(setup_tract + "tap samplerec\ntap f3\nturn 50\ntap f4\nturn 1\ntap fx\ntap pattern\n");
  CHECK(mouth.size() == still.size());
  CHECK(mouth != still);
  // The same wobble on CUTOFF (the default target) moves another thing: its take differs from the MOUTH one.
  const std::string cutoff = render(setup_tract + "tap samplerec\ntap f3\nturn 50\ntap fx\ntap pattern\n");
  CHECK(cutoff != mouth);
  CHECK(cutoff != still);
}

// ---- The STEPS layer (E2a) ----------------------------------------------------------------------------------------------------------------------

TEST_CASE("tract line step of: a SAME line (or an illegal length) is the pattern's index, a fixed one wraps on its own length") {
  o3_303::TractLine line;
  o3_303::tract_clear(line);
  CHECK(o3_303::tract_line_step_of(5, line, 16) == 5u);
  line.len = 4;
  CHECK(o3_303::tract_line_step_of(5, line, 16) == 1u);
  CHECK(o3_303::tract_line_step_of(15, line, 16) == 3u);
  line.len = 32;
  CHECK(o3_303::tract_line_step_of(15, line, 16) == 15u);
  line.len = 7;  // not a legal length: the line is read as SAME
  CHECK(o3_303::tract_line_step_of(9, line, 16) == 9u);
}

TEST_CASE("tract page STEPS: F1 turned on a held step moves that step's line vowel, GLD stays, it is a line write and not a lock") {
  Hands t;
  t.to_tract();
  CHECK(bank().set_tract_step(t.selected(), 2, 1, true));
  t.down(2);
  t.tap(Button::F1);
  t.turn(1);
  t.up(2);
  CHECK(vowel_of(2) == 2u);
  CHECK(o3_303::tract_glide(step_of(2)));
  CHECK(tract_lanes(t) == 0u);
  t.down(2);
  t.turn(-9);  // down to "-", no wrap
  t.up(2);
  CHECK(vowel_of(2) == 0u);
  // A fixed line shorter than the pattern: the step under the pattern's step 6 is the line's 2, and the toast says the edit repeats.
  CHECK(bank().set_tract_length(t.selected(), 4));
  t.down(6);
  t.pad(pal_pad(2));
  t.up(6);
  CHECK(vowel_of(2) == 3u);
  CHECK(toast_now(t) == "LINE LEN 4: EDIT REPEATS");
}

TEST_CASE("tract page STEPS: a held step with F2 or F3 turned locks TONGUE and LIPS on the tract's lanes, the line is untouched, CLEAR takes them away") {
  Hands t;
  t.to_tract();
  REQUIRE(tract_lanes(t) == 0u);
  t.down(3);
  t.tap(Button::F2);  // TONGUE
  t.turn(3);
  t.tap(Button::F3);  // LIPS
  t.turn(-3);
  t.up(3);
  const int tongue = o3_303::seq::find_lane(t.pat(), o3_303::kLaneTract, TractFx::Tongue);
  const int lips = o3_303::seq::find_lane(t.pat(), o3_303::kLaneTract, TractFx::Lips);
  REQUIRE(tongue >= 0);
  REQUIRE(lips >= 0);
  CHECK(tongue != lips);
  CHECK(o3_303::step_lock_point(t.pat(), static_cast<unsigned>(tongue), 3) != o3_303::kNoLock);
  CHECK(o3_303::step_lock_point(t.pat(), static_cast<unsigned>(lips), 3) != o3_303::kNoLock);
  CHECK(o3_303::step_lock_point(t.pat(), static_cast<unsigned>(tongue), 4) == o3_303::kNoLock);
  CHECK(tract_lanes(t) == 2u);  // at most the two
  CHECK(step_of(3) == 0u);
  // Shift + F5 CLEAR: the line and the two lanes.
  CHECK(bank().set_tract_step(t.selected(), 1, 3, false));
  t.shift_tap(Button::F5);
  CHECK(step_of(1) == 0u);
  CHECK_FALSE(o3_303::seq::lane_has_motion(t.pat(), static_cast<unsigned>(tongue)));
  CHECK_FALSE(o3_303::seq::lane_has_motion(t.pat(), static_cast<unsigned>(lips)));
}

TEST_CASE("tract page strip: fill_view gives each step's vowel, GLD, its locks' values as signed percents (TONGUE, LIPS) and the gate text, and only the TRACT page fills it (E2b)") {
  Hands t;
  t.to_tract();
  REQUIRE(bank().set_tract_step(t.selected(), 0, 1, false));  // A
  REQUIRE(bank().set_tract_step(t.selected(), 1, 4, true));   // O, GLD
  REQUIRE(bank().set_tract_step(t.selected(), 5, 0, true));   // "-" with GLD: it plays no vowel, so no glide
  const unsigned sel = t.selected();
  // A VOWEL lock of step 1 as the knob's ladder or MIDI learn puts it (a held step's F1 turn on the STEPS page is a line write): the lane of TractFx::Vowel.
  CHECK(bank().lock_step(sel, o3_303::kLaneTract, static_cast<std::uint8_t>(TractFx::Vowel), 1, 40) >= 0);
  // TONGUE and LIPS locks at known points: 0 is -100 %, 127 is 0 %, 254 is +100 % (the unit's range is -100..100).
  CHECK(bank().lock_step(sel, o3_303::kLaneTract, static_cast<std::uint8_t>(TractFx::Tongue), 3, 254) >= 0);
  CHECK(bank().lock_step(sel, o3_303::kLaneTract, static_cast<std::uint8_t>(TractFx::Lips), 3, 0) >= 0);
  CHECK(bank().lock_step(sel, o3_303::kLaneTract, static_cast<std::uint8_t>(TractFx::Tongue), 6, 127) >= 0);
  CHECK(bank().lock_step(sel, o3_303::kLaneTract, static_cast<std::uint8_t>(TractFx::Lips), 7, 190) >= 0);
  o3_303::InstrumentView v = view_now(t);
  CHECK(v.tract);
  REQUIRE(v.cells >= 8u);
  CHECK(v.cell[0].vowel == 1u);
  CHECK_FALSE(v.cell[0].glide);
  CHECK(std::strcmp(v.cell[0].gate, "A") == 0);
  CHECK(v.cell[1].vowel == 4u);
  CHECK(v.cell[1].glide);
  CHECK(v.cell[1].vowel_lock);
  CHECK(std::strcmp(v.cell[1].gate, "O~*") == 0);
  CHECK(v.cell[3].tongue);
  CHECK(v.cell[3].lips);
  CHECK_FALSE(v.cell[3].vowel_lock);
  CHECK(std::strcmp(v.cell[3].gate, "-") == 0);
  CHECK(std::strcmp(v.cell[3].tongue_txt, "+100") == 0);
  CHECK(std::strcmp(v.cell[3].lips_txt, "-100") == 0);
  CHECK(std::strcmp(v.cell[6].tongue_txt, "+0") == 0);
  CHECK(std::strcmp(v.cell[6].lips_txt, "") == 0);
  CHECK(std::strcmp(v.cell[7].tongue_txt, "") == 0);
  CHECK(std::strcmp(v.cell[7].lips_txt, "+50") == 0);
  CHECK_FALSE(v.cell[5].glide);
  CHECK(std::strcmp(v.cell[5].gate, "-") == 0);
  CHECK_FALSE(v.cell[2].tongue);
  CHECK_FALSE(v.cell[2].lips);
  CHECK(std::strcmp(v.cell[2].tongue_txt, "") == 0);
  CHECK(std::strcmp(v.cell[2].lips_txt, "") == 0);
  // A fixed line of 4 wraps under the pattern's steps: step 4 shows the line's step 0 (the locks are the pattern's own, they do not wrap).
  CHECK(bank().set_tract_length(sel, 4));
  v = view_now(t);
  CHECK(v.cell[4].vowel == 1u);
  CHECK(v.cell[5].vowel == 4u);
  CHECK(std::strcmp(v.cell[4].tongue_txt, "") == 0);
  // fill_view: TRACT sets the flag, GROWL does not.
  t.to_growl();
  t.wait(60);
  CHECK_FALSE(view_now(t).tract);
}
