// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The hands of the app tests (app_*_test.cpp): a started simulator, the buttons and pads to play it with, and the few measuring helpers
// they share. Every function here is `inline` in an unnamed namespace, so a test file that does not use one is not warned about it.
// (tests/hands.h is the newer, namespaced copy for the tests of M3b's ui-1 slice and later.)
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/sim/harness.h>
#include <o3_303/pad_map.h>
#include <o3_303/pages.h>
#include <o3_303/pattern.h>
#include <o3_303/perform.h>
#include <o3_303/seq/bank.h>

#include "../app/src/instrument_screen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace o3sdk;
using namespace o3_303;
using app::Button;

namespace {

constexpr dsp::UnitId kVoice = 0;  // the units are attached in this order (o3_303_app.cpp)
constexpr dsp::UnitId kDist = 1;
constexpr dsp::UnitId kDelay = 2;
constexpr dsp::UnitId kGrowl = 4;  // attached after the freeze (3): the first four ids are the tests' contract

inline seq::Bank& bank() { return seq::project_bank(); }

// The rms of the interleaved int16 samples from index `from`, as a fraction of full scale.
inline double rms(const std::vector<std::int16_t>& lr, std::size_t from) {
  double sum = 0.0;
  std::size_t n = 0;
  for (std::size_t i = from; i < lr.size(); ++i) {
    sum += static_cast<double>(lr[i]) * static_cast<double>(lr[i]);
    ++n;
  }
  return n != 0 ? std::sqrt(sum / static_cast<double>(n)) / 32768.0 : 0.0;
}

// A started simulator and the hands to play it with.
struct Hands {
  std::unique_ptr<sim::Harness> h = std::make_unique<sim::Harness>(o3sdk_app());

  Hands() {
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
  void shift_tap(Button b) {
    press(Button::Shift);
    tap(b);
    release(Button::Shift);
  }
  void turn(int d) {
    h->turn(d);
    h->run_ms(2);
  }
  // INSTRUMENT (Params opens VOICE; again, the next page: VOICE, FX, MODS) on page `page` (kInstVoice ...), from any screen but INSTRUMENT.
  void to_instrument(unsigned page = kInstVoice) {
    tap(Button::Params);
    for (unsigned i = 0; i < page; ++i) {
      tap(Button::Params);
    }
  }
  // Back to the 303 screen (Pattern).
  void to_home() { tap(Button::Pattern); }
  // HOME's Shift row (UX-303 5.1), at once: F1 NEW, F2 VARY, F3 RHYTHM, F4 NOTES, F5 REVERSE, F6 CLEAR, F7 REDO, F8 UNDO.
  void new_line() { shift_tap(Button::F1); }
  // PATTERN SETUP's F1 LENGTH (from HOME: Pattern, a tap): `steps` detents (8 / 16 / 32 / 64: DOUBLE is +1), then back on HOME.
  void length_steps(int steps) {
    tap(Button::Pattern);
    tap(Button::F1);
    turn(steps);
    tap(Button::Pattern);
  }
  // A performance button's action, set on PERFORM SETUP: F1..F4 are Note, Instr, Vol, Fx; the encoder walks the eleven actions (to the first,
  // then up). From any screen; it ends on the 303 screen, or on PERFORM with `stay`, where the four buttons are the live actions (on the 303 screen
  // they are the drawer's rows).
  void assign_perf(Button which, PerfAction action, bool stay = false) {
    if (std::strncmp(title(), "PERFORM", 7) == 0) {
      tap(Button::Pattern);
    }
    tap(Button::Performance);
    tap(Button::Performance);  // PERFORM SETUP
    tap(app::fkey(static_cast<unsigned>(which) - static_cast<unsigned>(Button::Note)));
    turn(-16);
    if (action != PerfAction::Mute) {
      turn(static_cast<int>(action));
    }
    tap(stay ? Button::Performance : Button::Pattern);
  }
  std::uint8_t led(unsigned pad_index) const { return h->leds().level(pad_index); }
  const char* title() { return h->runtime().context().screen()->title(); }
  float param(dsp::UnitId u, unsigned id) { return h->engine().param(u, static_cast<std::uint16_t>(id)); }
  unsigned selected() const { return bank().selected(); }
  const Pattern& pat() const { return bank().pattern(bank().selected()); }
  // PATTERN SETUP's DUP>NEXT (Pattern, Shift + F3, Pattern) from HOME, back on HOME: the selected pattern is copied into the next slot, which is
  // selected (queued while playing). (H2 replaced the bridge Pattern held + Insert.)
  void duplicate_next() {
    tap(Button::Pattern);
    press(Button::Shift);
    tap(Button::F3);
    release(Button::Shift);
    tap(Button::Pattern);
    wait(5);
  }
  // Holds Pattern and taps a pad (stopped: selects that pattern).
  void select_pattern(unsigned index) {
    press(Button::Pattern);
    wait(350);
    pad(index);
    release(Button::Pattern);
    wait(5);
  }
};

// The key pad of a note at octave shift 0: the bottom row is C2 (36) .. B2, the row above it C3 (48) .. B3.
inline unsigned key_pad(unsigned note) { return note >= 48u ? 24u + (note - 48u) : 36u + (note - 36u); }
inline unsigned step_pad_of(unsigned k) { return step_pad(k); }

// The master's peak before its clipper over `ms` of the run, block by block.
inline float peak_over(Hands& t, unsigned ms) {
  float peak = 0.0f;
  for (unsigned i = 0; i < ms; ++i) {
    t.wait(1);
    const dsp::EngineStats st = t.h->engine().stats();
    peak = std::max(peak, std::max(st.peak_l, st.peak_r));
  }
  return peak;
}

const unsigned kAccPad = role_pad(PadRole::Acc);
const unsigned kSlidePad = role_pad(PadRole::Slide);
const unsigned kOctDownPad = role_pad(PadRole::OctDown);
const unsigned kOctUpPad = role_pad(PadRole::OctUp);
const unsigned kTiePad = role_pad(PadRole::Tie);
const unsigned kRestPad = role_pad(PadRole::Rest);
const unsigned kTrnspPad = role_pad(PadRole::Trnsp);
const unsigned kFiltPad = role_pad(PadRole::Filt);

}  // namespace
