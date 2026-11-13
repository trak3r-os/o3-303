// SPDX-License-Identifier: GPL-3.0-or-later
// The beat freeze and the one voice in the app (docs/M3B-PLAN.md R6 and C1): the engine has the voice, the distortion, the delay and the
// freeze (master insert 0) and nothing on the other slots; the freeze is off at boot and invisible while off (the run is the same one
// without it, bit for bit, outside a freeze); the Fx performance button (the FREEZE action, C4) turns it on while held, Shift + Fx latches
// it, and PERFORM SETUP's FREEZE (F6) steps its Length; frozen, the master repeats at the tempo's 1/4, 1/8 or 1/16 of a bar; and the loop
// is latched at entry. The unit itself is tested in units/freeze/tests.
#include <doctest/doctest.h>

#include "support/need.h"

#include <o3sdk/app/app.h>
#include <o3sdk/sim/harness.h>
#include <o3_303/pages.h>
#include <o3_303/units/freeze/freeze.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace o3sdk;
using app::Button;
using o3_303::Freeze;

namespace {

constexpr dsp::UnitId kVoice = 0;  // the units are attached in this order (o3_303_app.cpp)
constexpr dsp::UnitId kDist = 1;
constexpr dsp::UnitId kDelay = 2;
constexpr dsp::UnitId kFreeze = 3;

struct Hands {
  std::unique_ptr<sim::Harness> h = std::make_unique<sim::Harness>(o3sdk_app());

  Hands() {
    const bool started = h->start();
    CHECK(started);
    h->run_ms(100);
  }
  void wait(unsigned ms) { h->run_ms(ms); }
  void press(Button b) {
    h->press(b);
    h->run_ms(2);
  }
  void release(Button b) {
    h->release(b);
    h->run_ms(2);
  }
  void tap(Button b, unsigned hold = 40) {
    press(b);
    h->run_ms(hold);
    release(b);
  }
  void shift_tap(Button b) {
    press(Button::Shift);
    tap(b);
    release(Button::Shift);
  }
  // PERFORM's FRZ 1/4 pad (row 2, col 0): the beat freeze at a quarter of a bar. Held while the pad is down; Shift + a press latches it (PF1: the colour keys carry no action).
  static constexpr unsigned kFrzPad = app::pad_at(1, 0);
  void frz_down() {
    h->pad_down(kFrzPad);
    h->run_ms(2);
  }
  void frz_up() {
    h->pad_up(kFrzPad);
    h->run_ms(2);
  }
  void frz_latch() {
    press(Button::Shift);
    frz_down();
    h->run_ms(10);
    frz_up();
    release(Button::Shift);
  }
  void frz_tap() {
    frz_down();
    h->run_ms(10);
    frz_up();
  }
  void turn(int d) {
    h->turn(d);
    h->run_ms(2);
  }
  // PERFORM, whose pads are the live actions. From the 303 screen.
  void to_perform() { tap(Button::Performance); }
  // The beat freeze's Length from PERFORM SETUP (F6, FREEZE): from the 303 screen Shift + Perform (SETUP), and Perform once more for PERFORM. The encoder walks 1/4, 1/8,
  // 1/16 and stops at the ends.
  void choose_length(Freeze::LengthValue v) {
    tap(Button::Pattern);
    shift_tap(Button::Performance);
    tap(Button::F6);
    turn(-3);
    turn(static_cast<int>(v));
    tap(Button::Performance);
  }
  float param(dsp::UnitId u, unsigned id) { return h->engine().param(u, static_cast<std::uint16_t>(id)); }
  std::size_t frames() const { return h->audio().size() / 2u; }
};

// One run of the app at 125 BPM (the factory line A01): Play, 1.5 s, then the FRZ 1/4 pad held for `hold_ms` (or, with `freeze` false, the same time
// passing without the pad), then 1.2 s more. The interleaved samples
// and the frames of the key's press and release. The two kinds of run take the same time, so their samples line up.
struct Run {
  std::vector<std::int16_t> audio;
  std::size_t press_frame = 0;
  std::size_t release_frame = 0;
};

Run play_run(bool freeze, unsigned hold_ms = 900) {
  Hands t;
  t.h->record(true);
  t.to_perform();
  t.tap(Button::Play);
  t.wait(1500);
  Run r;
  r.press_frame = t.frames();
  if (freeze) {
    t.frz_down();
    t.wait(hold_ms);
    t.frz_up();
  } else {
    t.wait(2 + hold_ms + 2);  // the pad's press takes 2 ms (PF1: it was the colour key's 6), the release 2
  }
  r.release_frame = t.frames();
  t.wait(1200);
  r.audio = t.h->audio();
  return r;
}

// True when sample i and i + lag are the same, on both channels, for every frame of [from, to).
bool repeats(const std::vector<std::int16_t>& a, std::size_t from, std::size_t to, std::size_t lag) {
  if (2u * (to + lag) > a.size()) {
    return false;
  }
  for (std::size_t i = 2u * from; i < 2u * to; ++i) {
    if (a[i] != a[i + 2u * lag]) {
      return false;
    }
  }
  return true;
}

bool same(const std::vector<std::int16_t>& a, const std::vector<std::int16_t>& b, std::size_t from, std::size_t to) {
  if (2u * to > a.size() || 2u * to > b.size()) {
    return false;
  }
  for (std::size_t i = 2u * from; i < 2u * to; ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

TEST_CASE("one voice: the engine has the voice, the distortion, the delay, the freeze, the growl and the tract, and only slot 0 sounds (C1)") {
  Hands t;
  dsp::Engine& e = t.h->engine();
  CHECK(e.unit_count() == 7u);
  NEED(e.unit_info(kVoice) != nullptr);
  CHECK(std::string(e.unit_info(kVoice)->name) == "ACID");
  CHECK(std::string(e.unit_info(kDist)->name) == "DISTORTION");
  CHECK(std::string(e.unit_info(kDelay)->name) == "DELAY");
  CHECK(std::string(e.unit_info(kFreeze)->name) == "FREEZE");
  CHECK(e.unit_info(kFreeze)->kind == dsp::UnitKind::Effect);
  CHECK(std::string(e.unit_info(4)->name) == "GROWL");  // M3c: attached after the freeze, so the ids above stay
  CHECK(std::string(e.unit_info(5)->name) == "TRACT");  // A1: attached after the growl, so the ids above stay
  CHECK(std::string(e.unit_info(6)->name) == "WOBBLE");  // D1: the MIDI-learn proxy of the wobble settings, attached last, never routed
  CHECK(e.unit_info(7) == nullptr);                          // no second voice, no unit after the wobble proxy
  t.tap(Button::Play);
  float slot0 = 0.0f;
  float others = 0.0f;
  for (int i = 0; i < 40; ++i) {
    t.wait(50);
    const dsp::EngineStats s = e.stats();
    slot0 = s.slot_peak[0] > slot0 ? s.slot_peak[0] : slot0;
    for (unsigned k = 1; k < dsp::kMaxSlots; ++k) {
      others = s.slot_peak[k] > others ? s.slot_peak[k] : others;
    }
  }
  CHECK(slot0 > 0.05f);
  CHECK(others == 0.0f);
}

TEST_CASE("freeze: off at boot with the unit's defaults; the FRZ 1/4 pad is FREEZE: held = on, Shift + tap latches; the Fx key and the Song key do nothing to it") {
  Hands t;
  t.to_perform();
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  CHECK(t.param(kFreeze, Freeze::Length) == 1.0f);  // 1/8 of a bar
  CHECK(t.param(kFreeze, Freeze::Mix) == 1.0f);
  t.frz_down();
  CHECK(t.param(kFreeze, Freeze::Active) == 1.0f);
  CHECK(t.param(kFreeze, Freeze::Length) == 0.0f);  // the FRZ 1/4 pad is the unit's 1/4
  t.frz_up();
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  // The Fx key flips WOBBLE now (PF1) and touches no freeze, with or without Shift.
  t.tap(Button::Fx);
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  t.tap(Button::Fx);
  // The provisional Song key is gone (M3b ui-1): it opens SONG (SG2) and freezes nothing, with or without Shift; back on PERFORM for the rest.
  t.press(Button::Song);
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  t.release(Button::Song);
  t.shift_tap(Button::Song);
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  t.to_perform();
  // Shift + the pad latches the freeze, a tap on the latched pad unlatches it.
  t.frz_latch();
  CHECK(t.param(kFreeze, Freeze::Active) == 1.0f);
  t.frz_tap();
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  // The Length is PERFORM SETUP's F6: 1/8 -> 1/16 -> 1/4 -> 1/8, and the freeze never turns on by it.
  const float want[] = {2.0f, 0.0f, 1.0f};
  const Freeze::LengthValue pick[] = {Freeze::Sixteenth, Freeze::Quarter, Freeze::Eighth};
  for (unsigned i = 0; i < 3; ++i) {
    t.choose_length(pick[i]);
    CHECK(t.param(kFreeze, Freeze::Length) == want[i]);
    CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  }
  // The pad pressed with Shift held, then Shift let go before the pad: the freeze was latched, not held; the release leaves it as it is.
  t.press(Button::Shift);
  t.frz_down();
  t.release(Button::Shift);
  t.frz_up();
  CHECK(t.param(kFreeze, Freeze::Active) == 1.0f);
  t.frz_tap();
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  // A run starts with it off again.
  Hands again;
  CHECK(again.param(kFreeze, Freeze::Active) == 0.0f);
  CHECK(again.param(kFreeze, Freeze::Length) == 1.0f);
}

TEST_CASE("freeze: off it is invisible; held (the FRZ 1/4 pad) it loops the master at a quarter of a bar (125 BPM: 21,168 frames) and then the run is the unfrozen one") {
  const Run dry = play_run(false);
  const Run frozen = play_run(true);
  NEED(dry.audio.size() == frozen.audio.size());
  NEED(dry.press_frame == frozen.press_frame);
  const std::size_t n0 = dry.press_frame;
  const std::size_t n1 = frozen.release_frame;
  // Until the pad, the same run; after it (and the 5 ms exit) the same run again: the freeze touches nothing else.
  CHECK(same(dry.audio, frozen.audio, 0, n0));
  CHECK(same(dry.audio, frozen.audio, n1 + 400, dry.audio.size() / 2u));
  // Held, it differs from the run, and it repeats at the loop: after the entry crossfade, one loop later, and every loop after.
  CHECK_FALSE(same(dry.audio, frozen.audio, n0 + 800, n1));
  CHECK(repeats(frozen.audio, n0 + 800, n1 - 21168u - 400u, 21168));
  CHECK_FALSE(repeats(dry.audio, n0 + 800, n1 - 21168u - 400u, 21168));  // the pattern itself does not repeat at a quarter of a bar here
  CHECK_FALSE(repeats(frozen.audio, n0 + 800, n1 - 21168u - 400u, 21167));
}

TEST_CASE("freeze: the pad's quarter is not PERFORM SETUP's F6 Length: after F6 picks 1/16 the FRZ 1/4 pad still loops a quarter and writes the unit's 1/4") {
  Hands t;
  t.h->record(true);
  t.to_perform();
  t.choose_length(Freeze::Sixteenth);
  CHECK(t.param(kFreeze, Freeze::Length) == 2.0f);
  t.tap(Button::Play);
  t.wait(1500);
  const std::size_t p0 = t.frames();
  t.frz_down();
  t.wait(900);
  const std::size_t p1 = t.frames();
  CHECK(t.param(kFreeze, Freeze::Length) == 0.0f);
  t.frz_up();
  t.wait(300);
  CHECK(repeats(t.h->audio(), p0 + 800, p1 - 21168u - 400u, 21168));
}

TEST_CASE("freeze: a tempo change while frozen leaves the loop; the next entry takes the new tempo") {
  Hands t;
  t.h->record(true);
  t.to_perform();
  t.tap(Button::Play);
  t.wait(1500);
  const std::size_t n0 = t.frames();
  t.frz_down();
  t.wait(600);
  (void)t.h->engine().set_tempo(200'000);  // from the main loop, as the encoder or a tap does
  t.wait(500);
  const std::size_t mid = t.frames();
  t.frz_up();
  t.wait(300);
  // At 125 BPM a quarter of a bar is 21,168 frames; the loop went on at that length while the transport moved to 200 BPM.
  CHECK(repeats(t.h->audio(), n0 + 800, mid - 21168u - 400u, 21168));
  const std::size_t p2 = t.frames();
  t.frz_down();
  t.wait(900);
  const std::size_t n2 = t.frames();
  t.frz_up();
  // A quarter of a bar at 200 BPM: 13,230 frames.
  CHECK(repeats(t.h->audio(), p2 + 800, n2 - 13230u - 400u, 13230));
  CHECK_FALSE(repeats(t.h->audio(), p2 + 800, n2 - 13230u - 400u, 21168));
}
