// SPDX-License-Identifier: GPL-3.0-or-later
// The TRACT page (TRACT-KNOBS-PLAN A2, first part): Sample Playback opens it and flips to GROWL and back, the VOL key is the on / off key that remembers the last mix, the bottom row of pads
// holds the live vowels A E I O U (a tap writes the VOWEL knob), rows 1 to 3 are the line and its palette (A2 rest), the wobble's MOUTH target swings VOWEL, and the TARGET refusal with the tract off.
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

unsigned vowel_pad(unsigned v) { return app::pad_at(3, v); }
o3_303::HomeMap& home_of(Hands& t) { return static_cast<o3_303::MainScreen*>(t.h->runtime().context().screen())->home_map(); }

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

TEST_CASE("tract page: the bottom row's first five pads are the live vowels, a tap writes VOWEL, the LEDs follow, the inert pads do nothing") {
  Hands t;
  t.to_tract();
  const float point[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
  for (unsigned v = 0; v < 5; ++v) {
    CHECK(t.led(vowel_pad(v)) != app::kLedOff);  // the five are lit (dim, the one that sounds on)
  }
  CHECK(t.led(vowel_pad(5)) == app::kLedOff);
  CHECK(t.led(vowel_pad(11)) == app::kLedOff);
  for (unsigned v : {2u, 4u, 0u, 3u, 1u}) {
    t.pad(vowel_pad(v));
    t.wait(20);
    CHECK(t.param(kTract, TractFx::Vowel) == doctest::Approx(point[v]));
    CHECK(t.led(vowel_pad(v)) == app::kLedOn);
    for (unsigned o = 0; o < 5; ++o) {
      if (o != v) {
        CHECK(t.led(vowel_pad(o)) == app::kLedDim);
      }
    }
  }
  // The inert pads (the rest of the live row, the palette's empty places, the line's pads past its end): no step toggled, no note, the vowel stays, the line is untouched.
  const float before = t.param(kTract, TractFx::Vowel);
  const auto pattern = t.pat();
  for (unsigned p : {app::pad_at(3, 5), app::pad_at(3, 11), app::pad_at(2, 6), app::pad_at(2, 9), app::pad_at(1, 11)}) {
    t.pad(p);
  }
  t.wait(20);
  CHECK(t.param(kTract, TractFx::Vowel) == before);
  CHECK(std::memcmp(&pattern, &t.pat(), sizeof pattern) == 0);
  CHECK(std::strcmp(t.title(), "TRACT") == 0);
  // Back on GROWL the pads are the 303's again.
  t.to_growl();
  CHECK(t.led(vowel_pad(0)) != app::kLedOff);  // the keyboard's own LEDs, not the vowels' (the home map)
}

namespace {

unsigned pal_pad(unsigned col) { return app::pad_at(2, col); }
std::uint8_t step_of(unsigned s) { return bank().tract_step(bank().selected(), s); }
unsigned vowel_of(unsigned s) { return o3_303::tract_vowel(step_of(s)); }
unsigned line_len_of(Hands& t) { return o3_303::tract_length(t.pat().tract, t.pat().length != 0u ? t.pat().length : o3_303::kDefaultLength); }

}  // namespace

TEST_CASE("tract page: rows 1 and 2 are the line, 24 steps a page; the LEDs show its steps inside the length and nothing past it") {
  Hands t;
  t.to_tract_line();
  REQUIRE(line_len_of(t) == 16u);
  for (unsigned n = 0; n < 16; ++n) {
    CHECK(t.led(n) != app::kLedOff);  // "-" inside the length: dim (the cursor's blinks)
  }
  for (unsigned n = 16; n < 24; ++n) {
    CHECK(t.led(n) == app::kLedOff);
  }
  CHECK(bank().set_tract_length(t.selected(), 32));
  t.wait(40);
  for (unsigned n = 0; n < 24; ++n) {
    CHECK(t.led(n) != app::kLedOff);
  }
  CHECK(bank().set_tract_step(t.selected(), 3, 2, false));
  CHECK(bank().set_tract_step(t.selected(), 4, 2, true));
  t.wait(40);
  CHECK(t.led(3) == app::kLedLow);
  CHECK(t.led(4) == app::kLedMid);
  CHECK(t.led(9) == app::kLedDim);
}

TEST_CASE("tract page: a step pad selects it, the palette writes it and goes on, held steps take one write together, GLD flips the glide, undo is one step") {
  Hands t;
  t.to_tract_line();
  REQUIRE(line_len_of(t) == 16u);
  t.pad(2);
  t.pad(pal_pad(3));  // O (4) on step 2, the cursor goes on to 3
  CHECK(vowel_of(2) == 4u);
  t.pad(pal_pad(0));  // A on step 3
  t.pad(pal_pad(1));  // E on step 4
  CHECK(vowel_of(3) == 1u);
  CHECK(vowel_of(4) == 2u);
  // Past the end of the line the pad is inert: the cursor keeps its place.
  t.pad(20);
  t.pad(pal_pad(2));  // I on the cursor's step (5)
  CHECK(vowel_of(5) == 3u);
  // Held steps: no advance, the same write on both.
  t.down(5);
  t.down(6);
  t.pad(pal_pad(4));
  t.up(6);
  t.up(5);
  CHECK(vowel_of(5) == 5u);
  CHECK(vowel_of(6) == 5u);
  // GLD: on a "-" step it is refused (and nothing changes); on a vowel it flips.
  t.pad(9);
  t.pad(pal_pad(10));
  CHECK(step_of(9) == 0u);
  t.pad(5);
  t.pad(pal_pad(10));
  CHECK(o3_303::tract_glide(step_of(5)));
  CHECK(vowel_of(5) == 5u);
  t.wait(40);
  CHECK(t.led(pal_pad(10)) == app::kLedMid);  // the cursor's step glides
  t.pad(5);
  t.pad(pal_pad(10));
  CHECK(!o3_303::tract_glide(step_of(5)));
  // "-" clears a step.
  t.down(6);
  t.pad(pal_pad(5));
  t.up(6);
  CHECK(step_of(6) == 0u);
  CHECK(vowel_of(5) == 5u);
  // One undo for the "-": step 6 is a vowel again (HOME's Shift + F8).
  t.to_home();
  t.shift_tap(Button::F8);
  CHECK(vowel_of(6) == 5u);
  CHECK(vowel_of(5) == 5u);
}

TEST_CASE("tract page: Left and Right move the line's cursor, the LINE pad flips LINE, Shift + F4 steps the length, Shift + F5 clears the line") {
  Hands t;
  t.to_tract_line();
  t.pad(4);
  t.tap(Button::Right);
  t.pad(pal_pad(0));  // A on step 5
  CHECK(vowel_of(5) == 1u);
  t.tap(Button::Left);  // from the cursor (6) back to 5
  t.tap(Button::Left);
  t.pad(pal_pad(1));  // E on step 4
  CHECK(vowel_of(4) == 2u);
  // LINE.
  CHECK(!app_sequencer().tract_line_on());
  t.pad(pal_pad(11));
  CHECK(app_sequencer().tract_line_on());
  t.wait(40);
  CHECK(t.led(pal_pad(11)) == app::kLedOn);
  t.pad(pal_pad(11));
  CHECK(!app_sequencer().tract_line_on());
  CHECK(t.led(pal_pad(11)) == app::kLedLow);
  // LEN: SAME (16) -> 4 -> 8 -> 16 -> 32 -> 64 -> SAME.
  CHECK(t.pat().tract.len == o3_303::kTractSame);
  const unsigned want[] = {4, 8, 16, 32, 64, 0};
  for (unsigned w : want) {
    t.shift_tap(Button::F4);
    CHECK(t.pat().tract.len == w);
    CHECK(std::strcmp(t.title(), "TRACT") == 0);
  }
  t.shift_tap(Button::F4);  // 4: the pads past the fourth are dark
  t.wait(40);
  CHECK(t.led(5) == app::kLedOff);
  CHECK(t.led(2) != app::kLedOff);
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

TEST_CASE("tract page: a live pad over a playing line overrides it while held and the knob comes back on release; Rec writes the sounding step") {
  Hands t;
  t.to_tract_line();
  t.pad(pal_pad(11));  // LINE on
  REQUIRE(app_sequencer().tract_line_on());
  t.pad(vowel_pad(1));  // stopped: the tap is the knob's
  CHECK(t.param(kTract, TractFx::Vowel) == doctest::Approx(0.25f));
  t.tap(Button::Play);
  t.wait(60);
  REQUIRE(app_sequencer().tract_playhead() >= 0);
  t.down(vowel_pad(3));
  t.wait(20);
  CHECK(t.param(kTract, TractFx::Vowel) == doctest::Approx(0.75f));
  CHECK((app_sequencer().perf() & o3_303::seq::kPerfTractLive) != 0u);
  t.down(vowel_pad(4));  // a second pad: the knob follows, the first value stays what comes back
  CHECK(t.param(kTract, TractFx::Vowel) == doctest::Approx(1.0f));
  t.up(vowel_pad(3));
  CHECK((app_sequencer().perf() & o3_303::seq::kPerfTractLive) != 0u);
  t.up(vowel_pad(4));
  t.wait(20);
  CHECK((app_sequencer().perf() & o3_303::seq::kPerfTractLive) == 0u);
  CHECK(t.param(kTract, TractFx::Vowel) == doctest::Approx(0.25f));
  // Rec while it plays: the vowel is written into the line's step that sounds (one step of the 16).
  for (unsigned n = 0; n < 16; ++n) {
    REQUIRE(step_of(n) == 0u);
  }
  t.tap(Button::Rec);
  t.down(vowel_pad(2));
  t.up(vowel_pad(2));
  t.tap(Button::Rec);
  unsigned written = 0;
  for (unsigned n = 0; n < 16; ++n) {
    written += vowel_of(n) == 3u ? 1u : 0u;
  }
  CHECK(written == 1u);
  t.tap(Button::Play);
  t.wait(40);
  CHECK((app_sequencer().perf() & o3_303::seq::kPerfTractLive) == 0u);
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
      "paddown 39\nwait 60\npadup 39\ntap sampleplay\ntap sampleedit\ntap vol\ntap pattern\n");
  CHECK(visited.size() == dry.size());
  CHECK(visited == dry);
  // And the control: with the tract on (Mix 50 %, the vowel U) the line is not the dry one.
  const std::string on = render("tap sampleedit\ntap vol\npaddown 40\nwait 60\npadup 40\ntap pattern\n");
  CHECK(on.size() == dry.size());
  CHECK(on != dry);
}

TEST_CASE("tract page: the wobble on MOUTH changes the sound (the tract's vowel moves with the line), and it is the tract's, not the growl's") {
  const std::string setup_tract = "tap sampleedit\ntap vol\ntap f7\nturn 50\npaddown 38\nwait 60\npadup 38\n";
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

namespace {

std::string toast_now(Hands& t) {
  o3_303::frame::StatusView sv;
  home_of(t).status(sv);
  return sv.toast;
}

// How many of the pattern's lanes drive the tract.
unsigned tract_lanes(Hands& t) {
  unsigned n = 0;
  for (const auto& l : t.pat().lanes) {
    n += l.target == o3_303::kLaneTract ? 1u : 0u;
  }
  return n;
}

}  // namespace

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

TEST_CASE("tract page: it opens on STEPS, Sample Editor again is LINE and back, and leaving and coming again is STEPS (E2a)") {
  Hands t;
  t.to_tract();
  const auto view = [&]() -> bool {
    return static_cast<o3_303::InstrumentScreen*>(t.h->runtime().context().screen())->line_layer();
  };
  CHECK_FALSE(view());
  t.tap(Button::SampleEdit);
  CHECK(view());
  CHECK(toast_now(t) == "TRACT LINE LAYER");
  t.tap(Button::SampleEdit);
  CHECK_FALSE(view());
  CHECK(toast_now(t) == "TRACT STEPS LAYER");
  t.tap(Button::SampleEdit);
  REQUIRE(view());
  t.to_growl();
  t.to_tract();
  CHECK_FALSE(view());
  // From another screen Shift + Sample Editor only opens it; on it Shift does the same as the plain press.
  t.shift_tap(Button::SampleEdit);
  CHECK(view());
}

TEST_CASE("tract page STEPS: a tap selects a pattern step, the palette writes the line under it and goes on, held steps take one write together, the other columns do nothing") {
  Hands t;
  t.to_tract();
  REQUIRE(line_len_of(t) == 16u);
  t.pad(2);
  CHECK(home_of(t).cursor() == 2);
  t.pad(pal_pad(3));  // O on step 2, the cursor goes on to 3
  CHECK(vowel_of(2) == 4u);
  CHECK(home_of(t).cursor() == 3);
  t.pad(pal_pad(0));
  t.pad(pal_pad(1));
  CHECK(vowel_of(3) == 1u);
  CHECK(vowel_of(4) == 2u);
  // Held: one write on both, the cursor stays. The second row is steps 8 to 15.
  t.down(5);
  t.down(app::pad_at(1, 2));  // step 10
  t.pad(pal_pad(4));
  t.up(app::pad_at(1, 2));
  t.up(5);
  CHECK(vowel_of(5) == 5u);
  CHECK(vowel_of(10) == 5u);
  CHECK(home_of(t).cursor() == 5);
  // GLD flips the glide of the held steps, refused on a "-" step.
  t.down(5);
  t.pad(pal_pad(10));
  t.up(5);
  CHECK(o3_303::tract_glide(step_of(5)));
  t.pad(9);
  t.pad(pal_pad(10));
  CHECK(step_of(9) == 0u);
  CHECK(toast_now(t) == "GLD NEEDS A VOWEL");
  // The old line pads past the eighth column are inert on STEPS: no step, no line change.
  const auto before = t.pat();
  for (unsigned c = 8; c < 12; ++c) {
    t.pad(app::pad_at(0, c));
    t.pad(app::pad_at(1, c));
  }
  CHECK(std::memcmp(&before, &t.pat(), sizeof before) == 0);
  // The note steps are not touched by any of it.
  for (unsigned n = 0; n < 16; ++n) {
    CHECK(t.pat().steps[n].flags == before.steps[n].flags);
  }
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
