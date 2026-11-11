// SPDX-License-Identifier: GPL-3.0-or-later
// The TRACT page (TRACT-KNOBS-PLAN A2, first part): Sample Playback opens it and flips to GROWL and back, F8 is the on / off key that remembers the last mix, the bottom row of pads
// holds the live vowels A E I O U (a tap writes the VOWEL knob) and the other pads do nothing yet, the wobble's MOUTH target swings VOWEL, and the TARGET refusal with the tract off.
// The pure parts (the read-out, the vowel table, the page entries) are in pages_test.cpp.
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/frame.h"
#include "../app/src/home_map.h"
#include "../app/src/main_screen.h"

#include <o3sdk/dsp/engine.h>
#include <o3_303/app_access.h>
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

TEST_CASE("tract page: Sample Playback opens TRACT, again GROWL, again TRACT; the other Sample buttons are as they were") {
  Hands t;
  CHECK(std::strcmp(t.title(), "TRACT") != 0);
  t.tap(Button::SamplePlay);
  CHECK(std::strcmp(t.title(), "TRACT") == 0);
  t.tap(Button::SamplePlay);
  CHECK(std::strcmp(t.title(), "GROWL") == 0);
  t.tap(Button::SamplePlay);
  CHECK(std::strcmp(t.title(), "TRACT") == 0);
  t.to_wobble();
  CHECK(std::strcmp(t.title(), "WOBBLE") == 0);
  t.tap(Button::SamplePlay);  // from another screen it opens the first page again
  CHECK(std::strcmp(t.title(), "TRACT") == 0);
  t.to_home();
  t.to_growl();
  CHECK(std::strcmp(t.title(), "GROWL") == 0);
}

TEST_CASE("tract page: F8 is the on / off key (Mix 0 off, the last mix on), F7 is the mix, MIDI-learnable knobs F1 to F7") {
  Hands t;
  t.to_tract();
  CHECK(t.param(kTract, TractFx::Mix) == 0.0f);  // silent at a boot
  t.tap(Button::F8);
  CHECK(t.param(kTract, TractFx::Mix) == doctest::Approx(0.5f));
  t.tap(Button::F7);  // F7 focuses the mix
  t.turn(4);
  const float mix = t.param(kTract, TractFx::Mix);
  CHECK(mix > 0.5f);
  t.tap(Button::F8);  // off: Mix 0, the unit is a bit-exact bypass
  CHECK(t.param(kTract, TractFx::Mix) == 0.0f);
  t.tap(Button::F8);  // on: the mix it had
  CHECK(t.param(kTract, TractFx::Mix) == doctest::Approx(mix));
  t.tap(Button::F1);
  t.turn(3);
  CHECK(t.param(kTract, TractFx::Vowel) > 0.0f);  // the knob moves the unit
}

TEST_CASE("tract page: the bottom row's first five pads are the live vowels, a tap writes VOWEL, the LEDs follow, the other pads do nothing") {
  Hands t;
  t.to_tract();
  const float point[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
  for (unsigned v = 0; v < 5; ++v) {
    CHECK(t.led(vowel_pad(v)) != app::kLedOff);  // the five are lit (dim, the one that sounds on)
  }
  CHECK(t.led(vowel_pad(5)) == app::kLedOff);
  CHECK(t.led(app::pad_at(0, 0)) == app::kLedOff);  // rows 1 to 3 are dark and silent until the line slice
  CHECK(t.led(app::pad_at(2, 3)) == app::kLedOff);
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
  // Other pads: no step is selected or toggled, no note sounds, the vowel stays.
  const float before = t.param(kTract, TractFx::Vowel);
  const auto pattern = t.pat();
  for (unsigned p : {app::pad_at(0, 0), app::pad_at(1, 4), app::pad_at(2, 7), app::pad_at(3, 5), app::pad_at(3, 11)}) {
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

TEST_CASE("tract page: the wobble's TARGET stops before MOUTH while the tract is off (TRACT IS OFF), and goes on with it on") {
  Hands t;
  (void)t.h->engine().set_param(kGrowl, o3_303::Growl::Mix, 0.5f);  // VOWEL needs the growl
  t.to_wobble();
  t.tap(Button::F8);  // WOB on: the sequencer takes the target at once
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

TEST_CASE("tract page: F8 off is the dry chain bit for bit, whatever the knobs say (A2): the same factory line with and without a visit to the page") {
  const std::string dry = render("");
  REQUIRE(dry.size() > 44);
  // Every knob moved, the vowels played, F8 on and off again, a visit to GROWL's page and back: Mix is 0 again, the tract is a bypass.
  const std::string visited = render(
      "tap sampleplay\ntap f8\ntap f1\nturn 40\ntap f2\nturn -30\ntap f3\nturn 50\ntap f4\nturn 20\ntap f5\nturn 10\ntap f6\nturn 2\ntap f7\nturn 60\n"
      "paddown 39\nwait 60\npadup 39\ntap sampleplay\ntap sampleplay\ntap f8\ntap pattern\n");
  CHECK(visited.size() == dry.size());
  CHECK(visited == dry);
  // And the control: with the tract on (Mix 50 %, the vowel U) the line is not the dry one.
  const std::string on = render("tap sampleplay\ntap f8\npaddown 40\nwait 60\npadup 40\ntap pattern\n");
  CHECK(on.size() == dry.size());
  CHECK(on != dry);
}

TEST_CASE("tract page: the wobble on MOUTH changes the sound (the tract's vowel moves with the line), and it is the tract's, not the growl's") {
  const std::string setup_tract = "tap sampleplay\ntap f8\ntap f7\nturn 50\npaddown 38\nwait 60\npadup 38\n";
  const std::string still = render(setup_tract + "tap pattern\n");
  // WOBBLE: DEPTH 100 %, TARGET up from CUTOFF (the growl is off and the tract on: the turn goes past VOWEL to MOUTH), the latch on.
  const std::string mouth = render(setup_tract + "tap sampleedit\ntap f3\nturn 50\ntap f4\nturn 1\ntap f8\ntap pattern\n");
  CHECK(mouth.size() == still.size());
  CHECK(mouth != still);
  // The same wobble on CUTOFF (the default target) moves another thing: its take differs from the MOUTH one.
  const std::string cutoff = render(setup_tract + "tap sampleedit\ntap f3\nturn 50\ntap f8\ntap pattern\n");
  CHECK(cutoff != mouth);
  CHECK(cutoff != still);
}
