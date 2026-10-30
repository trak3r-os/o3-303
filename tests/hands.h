// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A started simulator and the hands to play it with, for the gesture tests of M3b's ui-1 slice (perf_buttons_test.cpp, step_edit_test.cpp,
// ladders_test.cpp, tempo_test.cpp, scale_lock_test.cpp, lanes_test.cpp). The older tests keep their own copies of this.

#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/platform/host/fake_midi.h>
#include <o3sdk/sim/harness.h>
#include <o3_303/pad_map.h>
#include <o3_303/pages.h>
#include <o3_303/pattern.h>
#include <o3_303/perform.h>
#include <o3_303/seq/bank.h>

#include "../app/src/instrument_screen.h"

#include <cstdint>
#include <memory>

namespace hands {

using namespace o3sdk;
using o3_303::PadRole;
using app::Button;

inline constexpr dsp::UnitId kVoice = 0;  // the units are attached in this order (o3_303_app.cpp)
inline constexpr dsp::UnitId kDist = 1;
inline constexpr dsp::UnitId kDelay = 2;
inline constexpr dsp::UnitId kFreeze = 3;
inline constexpr dsp::UnitId kGrowl = 4;  // attached after the freeze (M3c G): slot 0's insert 1

inline o3_303::seq::Bank& bank() { return o3_303::seq::project_bank(); }

// The key pad of a note at octave shift 0: the bottom row is C2 (36) .. B2, the row above it C3 (48) .. B3.
inline unsigned key_pad(unsigned note) { return note >= 48u ? 24u + (note - 48u) : 36u + (note - 36u); }
inline unsigned step_pad_of(unsigned k) { return o3_303::step_pad(k); }

struct Hands {
  std::unique_ptr<sim::Harness> h = std::make_unique<sim::Harness>(o3sdk_app());

  Hands() { boot(); }
  // With a MIDI device (the platform's; the system Config then has a MIDI section and setup()'s route takes).
  explicit Hands(platform::host::FakeMidi& midi) : h(std::make_unique<sim::Harness>(o3sdk_app(), nullptr, &midi)) { boot(); }
  void boot() {
    REQUIRE(h->start());
    h->run_ms(100);
  }
  void wait(unsigned ms) { h->run_ms(ms); }
  void down(unsigned pad) {
    h->pad_down(pad);
    h->run_ms(2);
  }
  void up(unsigned pad) {
    h->pad_up(pad);
    h->run_ms(2);
  }
  void pad(unsigned p, unsigned hold = 40) {
    down(p);
    h->run_ms(hold);
    up(p);
  }
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
  // Holds Pattern and taps a pad (stopped: selects that pattern).
  void select_pattern(unsigned index) {
    press(Button::Pattern);
    wait(350);
    pad(index);
    release(Button::Pattern);
    wait(5);
  }
  // The TRNSP pad (a toggle): the keyboard transposes the running pattern, else it plays the voice.
  void toggle_trnsp() { pad(o3_303::role_pad(PadRole::Trnsp), 10); }
  void shift_tap(Button b) {
    press(Button::Shift);
    tap(b);
    release(Button::Shift);
  }
  void turn(int d) {
    h->turn(d);
    h->run_ms(2);
  }
  std::uint8_t led(unsigned pad_index) const { return h->leds().level(pad_index); }
  const char* title() { return h->runtime().context().screen()->title(); }
  float param(dsp::UnitId u, unsigned id) { return h->engine().param(u, static_cast<std::uint16_t>(id)); }
  unsigned selected() const { return bank().selected(); }
  const o3_303::Pattern& pat() const { return bank().pattern(bank().selected()); }

  // INSTRUMENT (Params opens VOICE; again, the next page: VOICE, FX, MODS, MORE, DEEP) on page `page` (o3_303::kInstVoice ...), from any screen but
  // INSTRUMENT itself. to_home() leaves it.
  void to_instrument(unsigned page = o3_303::kInstVoice) {
    tap(Button::Params);
    for (unsigned i = 0; i < page; ++i) {
      tap(Button::Params);
    }
  }
  // PERFORM (the Perform button; again it is PERFORM SETUP) and back to the 303 screen (Pattern). The title tells which page is up.
  void to_perform() { tap(Button::Performance); }
  void to_home() { tap(Button::Pattern); }
  // The RANDOM popup (Shift + F1), and the key that sets its three settings up.
  void random_popup() { shift_tap(Button::F1); }
  // A performance button's action, set on PERFORM SETUP (F1..F4 are Note, Instr, Vol, Fx; the encoder walks the eleven actions: down to the first,
  // then up). Starts from the 303 screen and ends on it.
  void assign_perf(Button which, o3_303::PerfAction action) {
    to_perform();
    tap(Button::Performance);  // PERFORM SETUP
    tap(app::fkey(static_cast<unsigned>(which) - static_cast<unsigned>(Button::Note)));
    turn(-16);
    if (action != o3_303::PerfAction::Mute) {
      turn(static_cast<int>(action));
    }
    to_home();
  }
};

}  // namespace hands
