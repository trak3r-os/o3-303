// SPDX-License-Identifier: GPL-3.0-or-later
// MASTER (docs/UX-303.md 5.6, slice M1): the pure view logic on its own functions (tempo and nudge text, the meters' scale and fall, the F2 and F3 boxes), and
// the screen end to end: F1 TEMPO latched on entry, F2 TAP, F3 NUDGE (hold + turn), F4 MASTER, the colour buttons that do nothing, the home map's pads, the
// CLOCK line, the meters and the display list. The global Master gestures (tap, nudge, Shift + Master, Shift + Play) are tempo_test.cpp's; the scenario is master.txt.
#include <doctest/doctest.h>

#include "hands.h"
#include "../app/src/master_screen.h"

#include <o3_303/app_access.h>
#include <o3_303/perform.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

using namespace hands;
using namespace o3_303;

namespace {

std::string tempo_str(std::uint32_t mbpm) {
  char s[16];
  (void)tempo_text(mbpm, s, sizeof s);
  return s;
}
std::string nudge_str(int pct) {
  char s[16];
  (void)nudge_text(pct, s, sizeof s);
  return s;
}
bool is_title(Hands& t, const char* title) { return std::strcmp(t.title(), title) == 0; }

MasterScreen& master(Hands& t) {
  REQUIRE(is_title(t, "MASTER"));
  return *static_cast<MasterScreen*>(t.h->runtime().context().screen());
}

void open_master(Hands& t) {
  t.tap(Button::Master);
  REQUIRE(is_title(t, "MASTER"));
}

}  // namespace

// ---- The view logic ----------------------------------------------------------------------------------------------------------

TEST_CASE("tempo_text: a tenth of a BPM, rounded") {
  CHECK(tempo_str(125'000) == "125.0");
  CHECK(tempo_str(120'450) == "120.5");
  CHECK(tempo_str(120'449) == "120.4");
  CHECK(tempo_str(99'950) == "100.0");
  CHECK(tempo_str(20'000) == "20.0");
  CHECK(tempo_str(300'000) == "300.0");
  CHECK(tempo_str(0) == "0.0");
  char tiny[4];
  CHECK(tempo_text(125'000, tiny, sizeof tiny) == 3u);  // cut to the room, terminated
  CHECK(std::strcmp(tiny, "125") == 0);
  CHECK(tempo_text(125'000, tiny, 0) == 0u);
}

TEST_CASE("nudge_text: the sign and the percent, clamped to the nudge's range") {
  CHECK(nudge_str(0) == "+0 %");
  CHECK(nudge_str(2) == "+2 %");
  CHECK(nudge_str(-3) == "-3 %");
  CHECK(nudge_str(7) == "+3 %");
  CHECK(nudge_str(-9) == "-3 %");
}

TEST_CASE("meter_norm: a dB scale from -48 dB to full scale; silence, a NaN and an overload stay on the scale") {
  CHECK(meter_norm(0.0f) == 0u);
  CHECK(meter_norm(-1.0f) == 0u);
  CHECK(meter_norm(std::nanf("")) == 0u);
  CHECK(meter_norm(1.0f) == kMeterMax);
  CHECK(meter_norm(2.5f) == kMeterMax);
  CHECK(meter_norm(0.0001f) == 0u);  // -80 dB
  const unsigned half = meter_norm(0.5f);  // -6.02 dB
  CHECK(half >= 873u);
  CHECK(half <= 877u);
  const unsigned tenth = meter_norm(0.1f);  // -20 dB: 28 / 48
  CHECK(tenth >= 582u);
  CHECK(tenth <= 585u);
  unsigned last = 0;
  for (float p = 0.001f; p < 1.2f; p *= 1.3f) {  // monotonic
    const unsigned n = meter_norm(p);
    CHECK(n >= last);
    CHECK(n <= kMeterMax);
    last = n;
  }
}

TEST_CASE("meter_fall: it jumps up at once and falls at its rate, never below the new value or below 0") {
  CHECK(meter_fall(500, 800, 10, 2000) == 800u);   // up: at once
  CHECK(meter_fall(500, 0, 100, 2000) == 300u);    // 2000 / s: 200 in 100 ms
  CHECK(meter_fall(500, 0, 1000, 2000) == 0u);     // down to 0, no wrap
  CHECK(meter_fall(500, 0, 250, 2000) == 0u);      // 500 exactly
  CHECK(meter_fall(0, 0, 1000, 2000) == 0u);
  CHECK(meter_fall(500, 450, 100, 2000) == 450u);  // it falls to the new value and no lower
  CHECK(meter_fall(500, 0, 0, 2000) == 500u);      // no time, no fall
  CHECK(meter_fall(1000, 0, 4'000'000'000u, 4'000'000'000u) == 0u);  // no overflow
}

TEST_CASE("the F2 TAP and F3 NUDGE boxes: counts and the nudge's place on its scale") {
  view::Knob k;
  fill_tap_knob(0, k);
  CHECK(std::strcmp(k.fkey, "F2") == 0);
  CHECK(std::strcmp(k.label, "TAP") == 0);
  CHECK(std::strcmp(k.value, "--") == 0);
  CHECK(k.norm < 0.0f);
  CHECK_FALSE(k.blank);
  fill_tap_knob(1, k);
  CHECK(std::strcmp(k.value, "1") == 0);
  CHECK(std::strcmp(k.unit, "TAP") == 0);
  fill_tap_knob(4, k);
  CHECK(std::strcmp(k.value, "4") == 0);
  CHECK(std::strcmp(k.unit, "TAPS") == 0);

  fill_nudge_knob(0, false, k);
  CHECK(std::strcmp(k.fkey, "F3") == 0);
  CHECK(std::strcmp(k.value, "+0 %") == 0);
  CHECK(k.norm == doctest::Approx(0.5f));
  CHECK_FALSE(k.held);
  fill_nudge_knob(3, true, k);
  CHECK(std::strcmp(k.value, "+3 %") == 0);
  CHECK(k.norm == doctest::Approx(1.0f));
  CHECK(k.held);
  fill_nudge_knob(-3, false, k);
  CHECK(k.norm == doctest::Approx(0.0f));
}

// ---- The screen -------------------------------------------------------------------------------------------------------------

TEST_CASE("the keys: F1 TEMPO and F4 MASTER are values, F2 and F3 are the screen's; the encoder enters on F1") {
  Hands t;
  open_master(t);
  MasterScreen& m = master(t);
  CHECK(m.focus() == 0u);
  CHECK(m.page_table().entry[0].item == Item::Tempo);
  CHECK(m.page_table().entry[3].item == Item::Master);
  CHECK(m.page_table().entry[1].item == Item::Blank);  // F2 and F3 take no latch
  CHECK(m.page_table().entry[2].item == Item::Blank);
  for (unsigned k = 4; k < 8; ++k) {
    CHECK(m.page_table().entry[k].item == Item::Blank);  // F5..F8 empty
  }
  const std::uint32_t t0 = t.h->engine().tempo();
  // F2 and F3 taps leave the latch where it is: the encoder still moves the tempo.
  t.tap(Button::F2);
  t.tap(Button::F3);
  CHECK(m.focus() == 0u);
  t.turn(2);
  CHECK(t.h->engine().tempo() == t0 + 1000);
  // F4 latches the master level.
  t.tap(Button::F4);
  CHECK(m.focus() == 3u);
  const float level0 = t.h->engine().master_level();
  t.turn(-4);
  CHECK(t.h->engine().master_level() < level0);
  // F5..F8 do nothing.
  t.tap(Button::F6);
  CHECK(m.focus() == 3u);
  // Back on F1.
  t.tap(Button::F1);
  CHECK(m.focus() == 0u);
  // Hold F4 + turn moves the level while held; the latch comes back to F1 at the release.
  t.press(Button::F4);
  const float level1 = t.h->engine().master_level();
  t.turn(3);
  t.release(Button::F4);
  CHECK(t.h->engine().master_level() > level1);
  CHECK(m.focus() == 0u);
  // The Shift row is empty: a Shift + F key does nothing, and its release is not the page's.
  t.press(Button::Shift);
  t.tap(Button::F4);
  t.release(Button::Shift);
  CHECK(m.focus() == 0u);
}

TEST_CASE("F2 TAP taps the tempo: four taps 500 ms apart are 120 BPM, and the box counts them") {
  Hands t;
  open_master(t);
  MasterScreen& m = master(t);
  CHECK(m.taps() == 0u);
  for (unsigned i = 0; i < 4; ++i) {
    t.tap(Button::F2, 30);
    t.wait(470);
  }
  t.wait(100);
  CHECK(m.taps() == 4u);
  CHECK(t.h->engine().tempo() > 118'000u);
  CHECK(t.h->engine().tempo() < 122'000u);
  CHECK(std::strcmp(m.last_view().taps, "4") == 0);
  CHECK(m.last_view().tempo[0] == '1');  // "120.0": the readout follows
  CHECK(is_title(t, "MASTER"));
  // A gap over two seconds: the measure starts again with the next tap.
  t.wait(2200);
  t.tap(Button::F2, 30);
  t.wait(100);
  CHECK(m.taps() == 1u);
  CHECK(std::strcmp(m.last_view().taps, "1") == 0);
  CHECK(t.h->engine().tempo() > 118'000u);  // one tap says nothing: the tempo stays
  CHECK(t.h->engine().tempo() < 122'000u);
  // The tap tempo is one measure for F2 and Shift + Master: two taps of Shift + Master a second apart make 60.
  t.wait(2200);
  t.tap(Button::F2, 30);
  t.wait(970);
  t.press(Button::Shift);
  t.tap(Button::Master, 30);
  t.release(Button::Shift);
  CHECK(t.h->engine().tempo() > 58'000u);
  CHECK(t.h->engine().tempo() < 62'000u);
  CHECK(is_title(t, "MASTER"));
  // The count on the screen goes when the measure is stale.
  t.wait(2100);
  CHECK(std::strcmp(m.last_view().taps, "") == 0);
}

TEST_CASE("F3 NUDGE: hold + turn nudges the tempo as holding Master does, the release gives it back; leaving the screen with F3 held does too") {
  Hands t;
  open_master(t);
  MasterScreen& m = master(t);
  const std::uint32_t t0 = t.h->engine().tempo();
  t.press(Button::F3);
  t.turn(2);
  CHECK(t.h->engine().tempo() == nudged_tempo(t0, 2));
  CHECK(m.nudge() == 2);
  t.turn(9);  // at most 3 %
  CHECK(t.h->engine().tempo() == nudged_tempo(t0, 3));
  t.wait(50);
  CHECK(std::strcmp(m.last_view().nudge, "+3 %") == 0);
  CHECK(m.last_view().nudged);
  CHECK(is_title(t, "MASTER"));
  t.release(Button::F3);
  CHECK(t.h->engine().tempo() == t0);
  CHECK(m.nudge() == 0);
  t.turn(2);  // F3 did not take the latch: the encoder is on TEMPO
  CHECK(t.h->engine().tempo() == t0 + 1000);
  // Leaving with F3 held.
  const std::uint32_t t1 = t.h->engine().tempo();
  t.press(Button::F3);
  t.turn(-2);
  CHECK(t.h->engine().tempo() == nudged_tempo(t1, -2));
  t.tap(Button::Pattern);  // HOME
  CHECK(is_title(t, "STEPS"));
  CHECK(t.h->engine().tempo() == t1);
  t.release(Button::F3);
  CHECK(t.h->engine().tempo() == t1);
}

TEST_CASE("the Master button's nudge on MASTER itself: the status line says TEMPO NUDGE and the box F3 shows it") {
  Hands t;
  open_master(t);
  MasterScreen& m = master(t);
  const std::uint32_t t0 = t.h->engine().tempo();
  t.press(Button::Master);
  t.turn(-2);
  t.wait(50);
  CHECK(m.nudge() == -2);
  CHECK(t.h->engine().tempo() == nudged_tempo(t0, -2));
  CHECK(std::strcmp(m.last_view().nudge, "-2 %") == 0);
  t.release(Button::Master);
  t.wait(5);
  CHECK(m.nudge() == 0);
  CHECK(t.h->engine().tempo() == t0);
  CHECK(is_title(t, "MASTER"));
}

TEST_CASE("the colour buttons do nothing on MASTER; on PERFORM the same buttons are live actions") {
  Hands t;
  open_master(t);
  const Button colours[4] = {Button::Note, Button::Instr, Button::Vol, Button::Fx};
  for (const Button b : colours) {
    t.press(b);
    t.wait(5);
    CHECK(app_sequencer().perf() == 0u);
    t.release(b);
    t.wait(5);
  }
  CHECK(is_title(t, "MASTER"));
  t.to_perform();
  REQUIRE(is_title(t, "PERFORM"));
  t.press(Button::Note);  // MUTE by default
  t.wait(5);
  CHECK(app_sequencer().perf() != 0u);
  t.release(Button::Note);
}

TEST_CASE("the pads are HOME's: ACC held + a step pad toggles its accent, a key pad plays the voice") {
  Hands t;
  open_master(t);
  const std::uint8_t flags0 = t.pat().steps[5].flags;
  t.down(role_pad(PadRole::Acc));
  t.pad(step_pad_of(5), 10);
  t.up(role_pad(PadRole::Acc));
  t.wait(5);
  CHECK(t.pat().steps[5].flags != flags0);
  CHECK(is_title(t, "MASTER"));
  t.h->record(true);
  t.pad(key_pad(48), 200);
  t.wait(300);
  double sum = 0.0;
  for (const std::int16_t s : t.h->audio()) {
    sum += static_cast<double>(s) * static_cast<double>(s);
  }
  CHECK(std::sqrt(sum / static_cast<double>(t.h->audio().size())) / 32768.0 > 0.005);
}

TEST_CASE("the CLOCK line says INTERNAL, and MIDI while the kit follows a master; the tempo read-out is the followed one") {
  platform::host::FakeMidi midi;
  Hands t(midi);
  open_master(t);
  MasterScreen& m = master(t);
  t.wait(100);
  CHECK(std::strcmp(m.last_view().clock, "INTERNAL") == 0);
  CHECK(tempo_str(t.h->engine().tempo()) == m.last_view().tempo);
  app::Midi& mid = t.h->runtime().context().midi();
  hal::midi::Config c = mid.config();
  c.follow = true;
  mid.set_config(c);
  midi.clock_tempo(140'000);
  t.wait(100);
  REQUIRE(t.h->engine().following());
  CHECK(std::strcmp(m.last_view().clock, "MIDI") == 0);
  CHECK(std::strcmp(m.last_view().tempo, "140.0") == 0);
}

TEST_CASE("the meters: silent at rest, moving while the pattern plays, and falling back after") {
  Hands t;
  open_master(t);
  MasterScreen& m = master(t);
  t.wait(300);
  CHECK(m.last_view().level[0] == 0u);
  CHECK(m.last_view().peak[1] == 0u);
  t.tap(Button::Play);
  unsigned top = 0;
  for (unsigned i = 0; i < 60; ++i) {
    t.wait(50);
    top = std::max(top, std::max(m.last_view().level[0], m.last_view().level[1]));
  }
  CHECK(top > 300u);  // the factory line is well above -48 dB
  CHECK(m.last_view().peak[0] >= m.last_view().level[0]);
  t.tap(Button::Play);
  t.wait(4000);  // the delay's tail goes, then the peaks fall by 500 / s
  CHECK(m.last_view().level[0] < 50u);
  CHECK(m.last_view().peak[0] < 50u);
}

TEST_CASE("every frame of MASTER fits the display list, also with the nudge up and the tap count on") {
  Hands t;
  open_master(t);
  t.tap(Button::Play);
  for (unsigned i = 0; i < 3; ++i) {
    t.tap(Button::F2, 30);
    t.wait(400);
  }
  t.press(Button::F3);
  t.turn(3);
  t.wait(300);
  t.release(Button::F3);
  t.wait(300);
  t.tap(Button::Play);
  CHECK(t.h->display().peak_frame_words() < 1800u);
}
