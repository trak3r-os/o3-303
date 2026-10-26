// SPDX-License-Identifier: GPL-3.0-or-later
// The beat freeze and the one voice in the app (docs/M3B-PLAN.md R6 and C1): the engine has the voice, the distortion, the delay and the
// freeze (master insert 0) and nothing on the other slots; the freeze is off at boot and invisible while off (the run is the same one
// without it, bit for bit, outside a freeze); the Fx performance button (the FREEZE action, C4) turns it on while held, Shift + Fx latches
// it, and the PERF page's FRZ LEN steps its Length; frozen, the master repeats at the tempo's 1/4, 1/8 or 1/16 of a bar; and the loop
// is latched at entry. The unit itself is tested in units/freeze/tests.
#include <doctest/doctest.h>

#include "support/need.h"

#include <o3sdk/app/app.h>
#include <o3sdk/sim/harness.h>
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
  void turn(int d) {
    h->turn(d);
    h->run_ms(2);
  }
  // The beat freeze's Length from the PERF page (F6, FRZ LEN): from the SOUND page Shift + Params is the previous page, PERF; Params
  // comes back. The encoder walks 1/4, 1/8, 1/16 and stops at the ends.
  void choose_length(Freeze::LengthValue v) {
    shift_tap(Button::Params);
    tap(Button::F6);
    turn(-3);
    turn(static_cast<int>(v));
    tap(Button::Params);
  }
  float param(dsp::UnitId u, unsigned id) { return h->engine().param(u, static_cast<std::uint16_t>(id)); }
  std::size_t frames() const { return h->audio().size() / 2u; }
};

// One run of the app at 125 BPM (the factory line A01): the Length chosen on the PERF page (when it is not the default 1/8), Play, 1.5 s,
// then Fx held for `hold_ms` (or, with `freeze` false, the same time passing without the key), then 1.2 s more. The interleaved samples
// and the frames of the key's press and release. The two kinds of run take the same time, so their samples line up.
struct Run {
  std::vector<std::int16_t> audio;
  std::size_t press_frame = 0;
  std::size_t release_frame = 0;
};

Run play_run(bool freeze, unsigned hold_ms = 900, Freeze::LengthValue length = Freeze::Eighth) {
  Hands t;
  t.h->record(true);
  if (length != Freeze::Eighth) {
    t.choose_length(length);
  }
  t.tap(Button::Play);
  t.wait(1500);
  Run r;
  r.press_frame = t.frames();
  if (freeze) {
    t.press(Button::Fx);
    t.wait(hold_ms);
    t.release(Button::Fx);
  } else {
    t.wait(2 + hold_ms + 2);  // a press and a release take 2 ms each
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

TEST_CASE("one voice: the engine has the voice, the distortion, the delay and the freeze, and only slot 0 sounds (C1)") {
  Hands t;
  dsp::Engine& e = t.h->engine();
  CHECK(e.unit_count() == 4u);
  NEED(e.unit_info(kVoice) != nullptr);
  CHECK(std::string(e.unit_info(kVoice)->name) == "TB303");
  CHECK(std::string(e.unit_info(kDist)->name) == "DISTORTION");
  CHECK(std::string(e.unit_info(kDelay)->name) == "DELAY");
  CHECK(std::string(e.unit_info(kFreeze)->name) == "FREEZE");
  CHECK(e.unit_info(kFreeze)->kind == dsp::UnitKind::Effect);
  CHECK(e.unit_info(4) == nullptr);  // no second voice, no unit after the freeze
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

TEST_CASE("freeze: off at boot with the unit's defaults; Fx is FREEZE: held = on, Shift + tap latches; the Song key does nothing now") {
  Hands t;
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  CHECK(t.param(kFreeze, Freeze::Length) == 1.0f);  // 1/8 of a bar
  CHECK(t.param(kFreeze, Freeze::Mix) == 1.0f);
  t.press(Button::Fx);
  CHECK(t.param(kFreeze, Freeze::Active) == 1.0f);
  t.release(Button::Fx);
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  // The provisional Song key is gone (M3b ui-1): pressed, with or without Shift, it does nothing.
  t.press(Button::Song);
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  t.release(Button::Song);
  t.shift_tap(Button::Song);
  CHECK(t.param(kFreeze, Freeze::Length) == 1.0f);
  // Shift + Fx latches the freeze, again unlatches; the button's own release does not undo the latch.
  t.shift_tap(Button::Fx);
  CHECK(t.param(kFreeze, Freeze::Active) == 1.0f);
  t.shift_tap(Button::Fx);
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  // The Length is the PERF page's F6: 1/8 -> 1/16 -> 1/4 -> 1/8, and the freeze never turns on by it.
  const float want[] = {2.0f, 0.0f, 1.0f};
  const Freeze::LengthValue pick[] = {Freeze::Sixteenth, Freeze::Quarter, Freeze::Eighth};
  for (unsigned i = 0; i < 3; ++i) {
    t.choose_length(pick[i]);
    CHECK(t.param(kFreeze, Freeze::Length) == want[i]);
    CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  }
  // Fx pressed with Shift held, then released after Shift: the freeze was latched, not held; the release leaves it as it is.
  t.press(Button::Shift);
  t.press(Button::Fx);
  t.release(Button::Shift);
  t.release(Button::Fx);
  CHECK(t.param(kFreeze, Freeze::Active) == 1.0f);
  t.shift_tap(Button::Fx);
  CHECK(t.param(kFreeze, Freeze::Active) == 0.0f);
  // A run starts with it off again.
  Hands again;
  CHECK(again.param(kFreeze, Freeze::Active) == 0.0f);
  CHECK(again.param(kFreeze, Freeze::Length) == 1.0f);
}

TEST_CASE("freeze: off it is invisible; held it loops the master at 1/8 of a bar (125 BPM: 10,584 frames) and then the run is the unfrozen one") {
  const Run dry = play_run(false);
  const Run frozen = play_run(true);
  NEED(dry.audio.size() == frozen.audio.size());
  NEED(dry.press_frame == frozen.press_frame);
  const std::size_t n0 = dry.press_frame;
  const std::size_t n1 = frozen.release_frame;
  // Until the key, the same run; after it (and the 5 ms exit) the same run again: the freeze touches nothing else.
  CHECK(same(dry.audio, frozen.audio, 0, n0));
  CHECK(same(dry.audio, frozen.audio, n1 + 400, dry.audio.size() / 2u));
  // Held, it differs from the run, and it repeats at the loop: after the entry crossfade, one loop later, and every loop after.
  CHECK_FALSE(same(dry.audio, frozen.audio, n0 + 800, n1));
  CHECK(repeats(frozen.audio, n0 + 800, n1 - 10584u - 400u, 10584));
  CHECK_FALSE(repeats(dry.audio, n0 + 800, n1 - 10584u - 400u, 10584));  // the pattern itself does not repeat at 1/8 of a bar here
  CHECK_FALSE(repeats(frozen.audio, n0 + 800, n1 - 10584u - 400u, 10583));
}

TEST_CASE("freeze: the Length is 1/4 or 1/16 of a bar after the PERF page's FRZ LEN picks it") {
  // 1/16 is 5,292 frames at 125 BPM, 1/4 a whole beat (21,168).
  const Run sixteenth = play_run(true, 900, Freeze::Sixteenth);
  CHECK(repeats(sixteenth.audio, sixteenth.press_frame + 800, sixteenth.release_frame - 5292u - 400u, 5292));
  const Run quarter = play_run(true, 900, Freeze::Quarter);
  CHECK(repeats(quarter.audio, quarter.press_frame + 800, quarter.release_frame - 21168u - 400u, 21168));
  CHECK_FALSE(repeats(quarter.audio, quarter.press_frame + 800, quarter.release_frame - 21168u - 400u, 10584));
}

TEST_CASE("freeze: a tempo change while frozen leaves the loop; the next entry takes the new tempo") {
  Hands t;
  t.h->record(true);
  t.tap(Button::Play);
  t.wait(1500);
  const std::size_t n0 = t.frames();
  t.press(Button::Fx);
  t.wait(400);
  (void)t.h->engine().set_tempo(200'000);  // from the main loop, as the encoder or a tap does
  t.wait(500);
  const std::size_t mid = t.frames();
  t.release(Button::Fx);
  t.wait(300);
  // At 125 BPM 1/8 is 10,584 frames; the loop went on at that length while the transport moved to 200 BPM.
  CHECK(repeats(t.h->audio(), n0 + 800, mid - 10584u - 400u, 10584));
  const std::size_t p2 = t.frames();
  t.press(Button::Fx);
  t.wait(900);
  const std::size_t n2 = t.frames();
  t.release(Button::Fx);
  // 1/8 of a bar at 200 BPM: 6,615 frames.
  CHECK(repeats(t.h->audio(), p2 + 800, n2 - 6615u - 400u, 6615));
  CHECK_FALSE(repeats(t.h->audio(), p2 + 800, n2 - 6615u - 400u, 10584));
}
