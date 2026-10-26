// SPDX-License-Identifier: GPL-3.0-or-later
// The app end to end in the simulator: it starts with the factory line in pattern A01, draws every frame inside the display list, and
// every pad and button gesture of DESIGN-303 sections 2 to 5 that does not need the card or MIDI does what the design says. The checks
// read the project's bank (o3_303::seq::project_bank()), the engine's values and the pad LEDs; the sound itself is the units' tests'.
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/pad_leds.h>
#include <o3sdk/sim/harness.h>
#include <o3_303/app_access.h>
#include <o3_303/pad_map.h>
#include <o3_303/pattern.h>
#include <o3_303/perform.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/gen.h>
#include <o3_303/step_model.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/tb303/tb303.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

using namespace o3sdk;
using namespace o3_303;
using app::Button;

namespace {

constexpr dsp::UnitId kVoice = 0;  // the units are attached in this order (o3_303_app.cpp)
constexpr dsp::UnitId kDist = 1;

seq::Bank& bank() { return seq::project_bank(); }

// The rms of the interleaved int16 samples from index `from`, as a fraction of full scale.
double rms(const std::vector<std::int16_t>& lr, std::size_t from) {
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
  // A performance button's action from the SOUND page: Shift + Params is the previous page, PERF; F1..F4 are Note, Instr, Vol, Fx; the
  // encoder walks the seven actions (to the first, then up); Params wraps back to SOUND.
  void assign_perf(Button which, PerfAction action) {
    shift_tap(Button::Params);
    tap(app::fkey(static_cast<unsigned>(which) - static_cast<unsigned>(Button::Note)));
    turn(-8);
    if (action != PerfAction::Mute) {
      turn(static_cast<int>(action));
    }
    tap(Button::Params);
  }
  std::uint8_t led(unsigned pad_index) const { return h->leds().level(pad_index); }
  const char* title() { return h->runtime().context().screen()->title(); }
  float param(dsp::UnitId u, unsigned id) { return h->engine().param(u, static_cast<std::uint16_t>(id)); }
  unsigned selected() const { return bank().selected(); }
  const Pattern& pat() const { return bank().pattern(bank().selected()); }
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
unsigned key_pad(unsigned note) { return note >= 48u ? 24u + (note - 48u) : 36u + (note - 36u); }
unsigned step_pad_of(unsigned k) { return step_pad(k); }

const unsigned kAccPad = role_pad(PadRole::Acc);
const unsigned kSlidePad = role_pad(PadRole::Slide);
const unsigned kOctDownPad = role_pad(PadRole::OctDown);
const unsigned kOctUpPad = role_pad(PadRole::OctUp);
const unsigned kTiePad = role_pad(PadRole::Tie);
const unsigned kRestPad = role_pad(PadRole::Rest);
const unsigned kKeysPad = role_pad(PadRole::Keys);
const unsigned kMotionPad = role_pad(PadRole::Motion);

}  // namespace

TEST_CASE("o3_303: the app identifies itself") {
  CHECK(std::strcmp(o3sdk_app().info().name, "O3-303") == 0);
  CHECK(std::strcmp(o3sdk_app().info().version, "0.0.1") == 0);
}

TEST_CASE("boot: the factory line is in A01, the rest of the project is empty, and the LEDs say so (DESIGN-303 section 2)") {
  Hands t;
  seq::gen::Preset first;
  seq::gen::factory(first, 0, 0);
  CHECK_FALSE(pattern_is_empty(bank().pattern(0)));
  for (unsigned i = 1; i < kPatternCount; ++i) {
    CHECK(pattern_is_empty(bank().pattern(i)));
  }
  CHECK(bank().selected() == 0);
  CHECK_FALSE(bank().can_undo());  // the boot load is not an edit of the user's: the first UNDO must not empty A01
  CHECK_FALSE(bank().can_redo());
  CHECK(t.h->engine().tempo() == seq::gen::to_values(first.patch).tempo_mbpm);
  CHECK_FALSE(t.h->engine().playing());
  CHECK(std::strcmp(t.title(), "STEPS") == 0);

  // Step LEDs: off = rest, dim = a note, low = an accented note (the cursor, step 1, blinks: not checked).
  const Pattern& p = bank().pattern(0);
  for (unsigned k = 1; k < 16; ++k) {
    const StepInfo info = describe(p, k);
    const std::uint8_t expect = info.kind == Cell::Rest ? app::kLedOff : (info.accent ? app::kLedLow : app::kLedDim);
    CHECK(t.led(step_pad_of(k)) == expect);
  }
  // Keyboard LEDs: naturals dim, sharps off, the current step's note at full level. The keyboard is in the pattern's register: the
  // bottom left pad is C2, which is the first note of the factory line, under the cursor.
  const StepInfo cursor_step = describe(p, 0);
  REQUIRE(cursor_step.kind == Cell::Note);
  REQUIRE(cursor_step.note >= 36);
  REQUIRE(cursor_step.note <= 59);
  const unsigned current_pad = key_pad(cursor_step.note);
  for (unsigned pad = 24; pad < 48; ++pad) {
    const int note = key_note(pad, 0);
    REQUIRE(note >= 0);
    const std::uint8_t expect = pad == current_pad ? app::kLedOn : (is_natural(static_cast<unsigned>(note)) ? app::kLedDim : app::kLedOff);
    CHECK(t.led(pad) == expect);
  }
  CHECK(t.led(key_pad(36)) == app::kLedOn);  // C2
  CHECK(t.led(key_pad(37)) == app::kLedOff); // C#2
  CHECK(t.led(key_pad(52)) == app::kLedDim); // E3
  // Move the cursor to a step with another note: its key is the one at full level.
  unsigned other = 1;
  while (other < 16 && (describe(p, other).kind != Cell::Note || describe(p, other).note == cursor_step.note || describe(p, other).note > 59)) {
    ++other;
  }
  REQUIRE(other < 16);
  t.pad(step_pad_of(other));
  t.wait(30);
  CHECK(t.led(key_pad(describe(p, other).note)) == app::kLedOn);
  CHECK(t.led(key_pad(cursor_step.note)) == (is_natural(cursor_step.note) ? app::kLedDim : app::kLedOff));
}

TEST_CASE("the first UNDO does not empty the factory line; the edits after it can be undone") {
  Hands t;
  const Pattern first = bank().pattern(0);
  t.shift_tap(Button::F8);  // UNDO with nothing to undo
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == first.steps[i].note);
    CHECK(t.pat().steps[i].flags == first.steps[i].flags);
  }
  CHECK_FALSE(pattern_is_empty(t.pat()));
  t.tap(Button::Delete);  // one edit
  CHECK(bank().can_undo());
  t.shift_tap(Button::F8);
  CHECK(t.pat().steps[0].flags == first.steps[0].flags);
  CHECK_FALSE(bank().can_undo());
}

TEST_CASE("every frame of a session fits the display list") {
  Hands t;
  t.select_pattern(5);
  t.tap(Button::Rec);
  t.pad(key_pad(50));
  t.shift_tap(Button::F1);
  t.tap(Button::Play);
  t.wait(1500);
  t.tap(Button::Params);
  t.tap(Button::Params);
  t.press(Button::Pattern);
  t.wait(400);
  t.release(Button::Pattern);
  t.tap(Button::File);
  t.wait(200);
  CHECK(t.h->runtime().frames() > 30u);
  CHECK(t.h->runtime().frame_failures() == 0u);
  CHECK(t.h->display().render_failures() == 0u);
  CHECK(t.h->display().peak_frame_words() < 2048u);
  CHECK(t.h->engine().dropped_commands() == 0u);
}

TEST_CASE("a key pad lights while it is held, and a step pad taps the cursor there") {
  Hands t;
  t.down(key_pad(52));
  CHECK(t.led(key_pad(52)) == app::kLedOn);
  t.up(key_pad(52));
  t.wait(40);
  CHECK(t.led(key_pad(52)) != app::kLedOn);

  t.pad(step_pad_of(6));  // a tap selects step 7: the delete that follows clears it
  t.tap(Button::Delete);
  CHECK((t.pat().steps[6].flags & kStepRest) != 0);
  CHECK((t.pat().steps[7].flags & kStepRest) == (bank().pattern(0).steps[7].flags & kStepRest));
}

TEST_CASE("REC + keys write the cursor step and advance, with ACC or SLIDE held; REST and TIE advance too") {
  Hands t;
  t.select_pattern(12);
  REQUIRE(t.selected() == 12);
  REQUIRE(pattern_is_empty(t.pat()));
  t.tap(Button::Rec);
  t.pad(key_pad(48));                       // step 1: C3
  t.down(kAccPad);                          // step 2: D#3 with accent
  t.pad(key_pad(51));
  t.up(kAccPad);
  t.down(kSlidePad);                        // step 3: G3 with slide
  t.pad(key_pad(55));
  t.up(kSlidePad);
  t.pad(kRestPad);                          // step 4: a rest
  t.pad(key_pad(53));                       // step 5: F3
  t.pad(kTiePad);                           // step 6: a tie
  t.down(kAccPad);                          // step 7: both held: accent and slide
  t.down(kSlidePad);
  t.pad(key_pad(58));
  t.up(kSlidePad);
  t.up(kAccPad);

  const Pattern& p = t.pat();
  CHECK(p.steps[0].note == 48);
  CHECK(p.steps[0].flags == 0);
  CHECK(p.steps[1].note == 51);
  CHECK(p.steps[1].flags == kStepAccent);
  CHECK(p.steps[2].note == 55);
  CHECK(p.steps[2].flags == kStepSlide);
  CHECK(p.steps[3].flags == kStepRest);
  CHECK(p.steps[4].note == 53);
  CHECK(p.steps[4].flags == 0);
  CHECK(p.steps[5].flags == kStepTie);
  CHECK(p.steps[6].note == 58);
  CHECK(p.steps[6].flags == (kStepAccent | kStepSlide));
  CHECK((p.steps[7].flags & kStepRest) != 0);  // untouched: still a rest

  // Without REC a key writes nothing.
  t.tap(Button::Rec);
  t.pad(key_pad(59));
  CHECK(t.pat().steps[7].note != 59);
  // The cursor wraps at the end of the length: 16 entries and the 17th is step 1 again.
  t.tap(Button::Rec);
  for (unsigned i = 0; i < 9; ++i) {
    t.pad(key_pad(49));
  }
  t.pad(key_pad(50));
  CHECK(t.pat().steps[0].note == 50);
}

TEST_CASE("OCT- and OCT+ move the keyboard; the keyboard writes the notes it shows") {
  Hands t;
  t.select_pattern(13);
  t.tap(Button::Rec);
  t.pad(36);  // the bottom left pad: C2
  CHECK(t.pat().steps[0].note == 36);
  t.pad(kOctUpPad);
  t.pad(36);  // C3 now
  CHECK(t.pat().steps[1].note == 48);
  t.pad(24);  // the row above: C4
  CHECK(t.pat().steps[2].note == 60);
  t.pad(kOctDownPad);
  t.pad(kOctDownPad);
  t.pad(36);
  CHECK(t.pat().steps[3].note == 24);
  for (unsigned i = 0; i < 6; ++i) {  // the shift stops at the ends of the note range
    t.pad(kOctDownPad);
  }
  t.pad(36);
  CHECK(t.pat().steps[4].note == 0);
  for (unsigned i = 0; i < 12; ++i) {
    t.pad(kOctUpPad);
  }
  t.pad(24);
  CHECK(t.pat().steps[5].note == 36 + 12 * (kMaxOctave + 1));
}

TEST_CASE("hold a step + a key sets that step's note and leaves the cursor where it was") {
  Hands t;
  t.select_pattern(14);
  t.tap(Button::Rec);
  t.pad(key_pad(48));  // step 1 written, the cursor is on step 2
  t.down(step_pad_of(5));
  t.pad(key_pad(52));
  t.pad(key_pad(55));  // a second key on the same held step: the last one wins
  t.up(step_pad_of(5));
  CHECK(t.pat().steps[5].note == 55);
  CHECK((t.pat().steps[5].flags & kStepRest) == 0);  // a rest becomes a note
  t.pad(key_pad(57));                                // the cursor was still on step 2
  CHECK(t.pat().steps[1].note == 57);
  CHECK(t.pat().steps[5].note == 55);
}

TEST_CASE("hold a step + ACC / SLIDE / TIE / REST toggles that attribute on it, OCT+ is its ratchet, and the step is not selected") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.down(step_pad_of(3));  // step 4: C3, plain
  t.pad(kAccPad);
  t.pad(kSlidePad);
  t.pad(kOctUpPad);  // M3b R4: the ratchet, no longer the octave
  t.up(step_pad_of(3));
  CHECK(seq::get_ratchet(t.pat(), 3) == 1);
  CHECK((t.pat().steps[3].flags & ~kStepRatchetMask) == ((before.steps[3].flags ^ kStepAccent ^ kStepSlide) & ~kStepRatchetMask));
  CHECK(t.pat().steps[3].note == before.steps[3].note);
  t.pad(step_pad_of(3), 5);  // a tap selects; a second toggle through the cursor and the tap on a modifier
  t.tap(Button::Delete);
  CHECK((t.pat().steps[3].flags & kStepRest) != 0);
  t.down(step_pad_of(3));
  t.pad(kRestPad);  // REST toggles off again
  t.pad(kTiePad);
  t.up(step_pad_of(3));
  CHECK((t.pat().steps[3].flags & kStepRest) == 0);
  CHECK((t.pat().steps[3].flags & kStepTie) != 0);
  CHECK(t.pat().steps[0].note == before.steps[0].note);  // nothing else moved
  // The cursor did not follow the held steps: Up changes the note of step 4, where the tap left it.
  const unsigned note = t.pat().steps[3].note;
  t.tap(Button::Up);
  CHECK(t.pat().steps[3].note == note + 1);
}

TEST_CASE("hold a modifier alone: the step pads show its steps and tapping them toggles (TR style)") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.down(kAccPad);
  t.wait(20);
  for (unsigned k = 1; k < 16; ++k) {
    const bool acc = (before.steps[k].flags & kStepAccent) != 0;
    CHECK(t.led(step_pad_of(k)) == (acc ? app::kLedMid : app::kLedOff));
  }
  t.pad(step_pad_of(1));  // step 2 had an accent: off now
  t.pad(step_pad_of(2));  // step 3 had none: on now
  t.wait(300);            // held longer than a tap: only a look at the steps, not a toggle of the cursor step
  t.up(kAccPad);
  CHECK(((t.pat().steps[1].flags ^ before.steps[1].flags) & kStepAccent) != 0);  // toggled
  CHECK(((t.pat().steps[2].flags ^ before.steps[2].flags) & kStepAccent) != 0);
  CHECK(t.pat().steps[0].flags == before.steps[0].flags);

  // REST held alone works the same way.
  t.down(kRestPad);
  t.pad(step_pad_of(8));
  t.pad(step_pad_of(9));
  t.up(kRestPad);
  CHECK((t.pat().steps[8].flags & kStepRest) != (before.steps[8].flags & kStepRest));
  CHECK((t.pat().steps[9].flags & kStepRest) != (before.steps[9].flags & kStepRest));
}

TEST_CASE("a tap on ACC, SLIDE, TIE or REST toggles it on the cursor step; with REC and stopped, TIE and REST are entries") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.pad(kAccPad);  // the cursor is on step 1
  CHECK(t.pat().steps[0].flags == (before.steps[0].flags ^ kStepAccent));
  t.pad(kAccPad);
  CHECK(t.pat().steps[0].flags == before.steps[0].flags);
  t.pad(kSlidePad);
  CHECK((t.pat().steps[0].flags & kStepSlide) != (before.steps[0].flags & kStepSlide));
  t.pad(kSlidePad);

  t.tap(Button::Rec);
  t.pad(kRestPad);  // an entry: step 1 becomes a rest and the cursor moves to step 2
  CHECK(t.pat().steps[0].flags == kStepRest);
  t.pad(kTiePad);   // step 2 becomes a tie
  CHECK(t.pat().steps[1].flags == kStepTie);
  t.tap(Button::Rec);
  t.pad(kAccPad);   // REC off again: a toggle on the cursor (step 3)
  CHECK((t.pat().steps[2].flags & kStepAccent) != (before.steps[2].flags & kStepAccent));
}

TEST_CASE("the arrows move the cursor across pages; Shift + arrows rotate and transpose") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.tap(Button::Right);
  t.tap(Button::Delete);  // step 2
  CHECK((t.pat().steps[1].flags & kStepRest) != 0);
  t.tap(Button::Left);
  t.tap(Button::Left);  // wraps to step 16
  t.tap(Button::Delete);
  CHECK((t.pat().steps[15].flags & kStepRest) != 0);

  t.tap(Button::Up);  // the cursor step's note +1 (step 16 is a rest now but keeps its note)
  CHECK(t.pat().steps[15].note == before.steps[15].note + 1);
  t.tap(Button::Down);
  CHECK(t.pat().steps[15].note == before.steps[15].note);

  const Pattern now = t.pat();
  t.shift_tap(Button::Right);  // rotate: every step moves one later
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[(i + 1) % 16].note == now.steps[i].note);
    CHECK(t.pat().steps[(i + 1) % 16].flags == now.steps[i].flags);
  }
  t.shift_tap(Button::Left);
  CHECK(t.pat().steps[3].note == now.steps[3].note);
  t.shift_tap(Button::Up);  // the playback transpose of the pattern
  t.shift_tap(Button::Up);
  CHECK(t.pat().transpose == 2);
  t.shift_tap(Button::Down);
  CHECK(t.pat().transpose == 1);
}

TEST_CASE("Copy, Shift + Copy, Insert, Delete work on steps; Shift + Insert duplicates and Shift + Delete clears the pattern") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.pad(step_pad_of(2));  // step 3
  t.tap(Button::Copy);
  t.pad(step_pad_of(4));  // step 5
  t.shift_tap(Button::Copy);
  CHECK(t.pat().steps[4].note == before.steps[2].note);
  CHECK(t.pat().steps[4].flags == before.steps[2].flags);

  t.pad(step_pad_of(8));  // step 9: insert shifts the steps after it one later
  const Pattern pre = t.pat();
  t.tap(Button::Insert);
  CHECK((t.pat().steps[8].flags & kStepRest) != 0);
  CHECK(t.pat().steps[9].note == pre.steps[8].note);
  CHECK(t.pat().steps[10].note == pre.steps[9].note);

  t.pad(step_pad_of(0));
  t.tap(Button::Delete);
  CHECK(t.pat().steps[0].flags == kStepRest);

  t.shift_tap(Button::Insert);  // a copy in the next slot, which is now selected
  CHECK(t.selected() == 1);
  CHECK_FALSE(pattern_is_empty(t.pat()));
  CHECK(t.pat().steps[4].note == bank().pattern(0).steps[4].note);
  t.shift_tap(Button::Delete);
  CHECK(pattern_is_empty(t.pat()));
  CHECK_FALSE(pattern_is_empty(bank().pattern(0)));
}

TEST_CASE("pages: tap an F key to latch it, hold one and turn for a moment, two held move together, Shift is coarse") {
  Hands t;
  const float cutoff0 = t.param(kVoice, Tb303::Cutoff);
  const float reso0 = t.param(kVoice, Tb303::Reso);
  t.turn(5);  // F1 (cutoff) is latched at the start
  const float cutoff1 = t.param(kVoice, Tb303::Cutoff);
  CHECK(cutoff1 > cutoff0);
  t.tap(Button::F2);
  t.turn(-10);
  CHECK(t.param(kVoice, Tb303::Reso) < reso0);
  CHECK(t.param(kVoice, Tb303::Cutoff) == cutoff1);

  const float decay0 = t.param(kVoice, Tb303::Decay);
  t.press(Button::F4);
  t.turn(10);
  t.release(Button::F4);
  CHECK(t.param(kVoice, Tb303::Decay) > decay0);
  const float reso1 = t.param(kVoice, Tb303::Reso);
  t.turn(3);  // back on F2, the latched key
  CHECK(t.param(kVoice, Tb303::Reso) > reso1);

  const float env0 = t.param(kVoice, Tb303::EnvMod);
  const float acc0 = t.param(kVoice, Tb303::Accent);
  t.press(Button::F3);
  t.press(Button::F5);
  t.turn(10);
  t.release(Button::F3);
  t.release(Button::F5);
  CHECK(t.param(kVoice, Tb303::EnvMod) > env0);
  CHECK(t.param(kVoice, Tb303::Accent) > acc0);

  const float tune0 = t.param(kVoice, Tb303::Tune);
  t.tap(Button::F7);
  t.turn(1);
  const float fine = t.param(kVoice, Tb303::Tune) - tune0;
  t.press(Button::Shift);
  t.turn(1);
  t.release(Button::Shift);
  const float coarse = t.param(kVoice, Tb303::Tune) - tune0 - fine;
  CHECK(coarse > fine * 5.0f);

  t.tap(Button::F6);
  t.turn(1);
  CHECK(t.param(kVoice, Tb303::Wave) == doctest::Approx(1.0f));
}

TEST_CASE("PARAMS goes to the next page (SOUND, FX, SEQ, PERF), Shift + PARAMS to the previous one; the pages reach the whole engine") {
  Hands t;
  const float drive0 = t.param(kDist, Distortion::Drive);
  t.tap(Button::Params);  // FX
  t.tap(Button::F1);
  t.turn(10);
  CHECK(t.param(kDist, Distortion::Drive) > drive0);
  t.tap(Button::F7);  // the distortion model
  t.turn(1);
  CHECK(t.param(kDist, Distortion::Type) == doctest::Approx(1.0f));
  t.tap(Button::F6);  // the reverb send is not there yet: the key does nothing
  t.turn(5);
  t.tap(Button::F5);
  t.turn(-10);
  t.tap(Button::F8);
  t.turn(-20);
  CHECK(t.h->engine().master_level() < 1.0f);

  t.tap(Button::Params);  // SEQ
  const std::uint32_t tempo0 = t.h->engine().tempo();
  t.tap(Button::F1);
  t.turn(10);
  CHECK(t.h->engine().tempo() > tempo0);
  t.tap(Button::F2);
  t.turn(10);
  t.tap(Button::F4);
  t.turn(10);
  CHECK(t.pat().swing == 10 * 1);
  t.tap(Button::F3);
  t.turn(1);
  CHECK(t.pat().length == 32);

  t.tap(Button::Params);  // PERF: its F3 is Vol's action; the encoder would change that, which this test does not do
  t.tap(Button::Params);  // wraps to SOUND; F3 was the latched key on SEQ, so it is on SOUND too (ENV MOD)
  const float env0 = t.param(kVoice, Tb303::EnvMod);
  t.turn(3);
  CHECK(t.param(kVoice, Tb303::EnvMod) != env0);
  t.shift_tap(Button::Params);  // the previous page: PERF
  t.shift_tap(Button::Params);  // and SEQ
  const std::uint32_t tempo1 = t.h->engine().tempo();
  t.tap(Button::F1);
  t.turn(2);
  CHECK(t.h->engine().tempo() != tempo1);
}

TEST_CASE("VOLUME is slot 0's level: the voice's own volume stays at 0 dB") {
  Hands t;
  CHECK(t.param(kVoice, Tb303::Volume) == doctest::Approx(0.0f));
  t.tap(Button::F8);
  t.turn(-4);
  CHECK(t.param(kVoice, Tb303::Volume) == doctest::Approx(0.0f));
  CHECK(t.h->engine().mix(0, dsp::MixField::Level) < 0.7f);
}

TEST_CASE("hold a step + the encoder p-locks the focused parameter on that step; hold MOTION shows the steps with automation") {
  Hands t;
  const float knob = t.param(kVoice, Tb303::Cutoff);
  t.down(step_pad_of(3));
  t.turn(10);
  t.turn(10);
  t.up(step_pad_of(3));
  const Pattern& p = t.pat();
  REQUIRE(step_has_motion(p, 0, 3));
  const std::uint8_t first = p.motion[0][3 * kMotionPointsPerStep];
  for (unsigned q = 1; q < kMotionPointsPerStep; ++q) {
    CHECK(p.motion[0][3 * kMotionPointsPerStep + q] == first);  // all four points of the step
  }
  const core::ParamDesc* desc = t.h->engine().param_desc(kVoice, Tb303::Cutoff);
  REQUIRE(desc != nullptr);
  CHECK(first > seq::value_to_point(*desc, knob));                     // turned up from the knob's value
  CHECK(t.param(kVoice, Tb303::Cutoff) == doctest::Approx(knob));      // the knob itself did not move
  CHECK_FALSE(step_has_motion(p, 0, 4));
  CHECK_FALSE(step_has_motion(p, 1, 3));

  t.tap(Button::F2);  // another lane, on another step
  t.down(step_pad_of(9));
  t.turn(-15);
  t.up(step_pad_of(9));
  CHECK(step_has_motion(t.pat(), 1, 9));

  t.down(kMotionPad);
  t.wait(20);
  CHECK(t.led(step_pad_of(9)) == app::kLedMid);  // the focused lane is RESO now
  CHECK(t.led(step_pad_of(3)) == app::kLedOff);  // the cutoff lock is not in this lane
  t.up(kMotionPad);
  t.wait(5);

  // Delete with MOTION held clears the lane.
  t.down(kMotionPad);
  t.tap(Button::Delete);
  t.up(kMotionPad);
  CHECK_FALSE(step_has_motion(t.pat(), 1, 9));
  CHECK(step_has_motion(t.pat(), 0, 3));
}

TEST_CASE("REC + Play + the encoder records motion under the playhead; a tap on MOTION latches the recording") {
  Hands t;
  t.tap(Button::Rec);
  t.tap(Button::Play);
  t.wait(200);
  for (unsigned i = 0; i < 12; ++i) {
    t.turn(4);
    t.wait(100);
  }
  t.tap(Button::Play);
  t.tap(Button::Rec);
  CHECK(pattern_has_motion(t.pat(), 0));

  // Without REC, the latch on MOTION does the same.
  t.shift_tap(Button::Delete);  // clear the pattern: no motion either
  CHECK_FALSE(pattern_has_motion(t.pat(), kMotionLanes));
  t.pad(kMotionPad);
  t.tap(Button::Play);
  t.wait(200);
  for (unsigned i = 0; i < 8; ++i) {
    t.turn(4);
    t.wait(100);
  }
  t.tap(Button::Play);
  CHECK(pattern_has_motion(t.pat(), 0));
  CHECK(t.led(kMotionPad) == app::kLedMid);  // the latch shows on the pad
}

TEST_CASE("the tools under Shift + F1..F8 change the pattern and UNDO / REDO walk them (DESIGN-303 section 3)") {
  Hands t;
  const Pattern first = bank().pattern(0);
  t.shift_tap(Button::F1);  // RANDOM
  const Pattern random = t.pat();
  bool differs = false;
  for (unsigned i = 0; i < 16; ++i) {
    differs = differs || random.steps[i].note != first.steps[i].note || random.steps[i].flags != first.steps[i].flags;
  }
  CHECK(differs);
  t.shift_tap(Button::F3);  // REVERSE
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == random.steps[15 - i].note);
  }
  t.shift_tap(Button::F5);  // DOUBLE
  CHECK(t.pat().length == 32);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[16 + i].note == t.pat().steps[i].note);
  }
  t.shift_tap(Button::F6);  // CLEAR
  CHECK(pattern_is_empty(t.pat()));
  t.shift_tap(Button::F8);  // UNDO
  CHECK_FALSE(pattern_is_empty(t.pat()));
  t.shift_tap(Button::F8);
  CHECK(t.pat().length == 16);
  t.shift_tap(Button::F7);  // REDO
  CHECK(t.pat().length == 32);
  t.shift_tap(Button::F2);  // MUTATE
  t.shift_tap(Button::F4);  // INVERT
  // A tool is not a page key: the pages' latch is where it was.
  const float cutoff0 = t.param(kVoice, Tb303::Cutoff);
  t.turn(3);
  CHECK(t.param(kVoice, Tb303::Cutoff) != cutoff0);
}

TEST_CASE("hold Pattern: the 48 pads are the patterns; stopped, a tap selects, a quick tap on Pattern flips to the piano roll") {
  Hands t;
  t.press(Button::Pattern);
  t.wait(350);
  CHECK(std::strcmp(t.title(), "PATTERNS") == 0);
  CHECK(t.led(0) != app::kLedOff);  // A01 holds the factory line
  CHECK(t.led(30) == app::kLedOff); // C07 is empty
  t.pad(30);
  CHECK(t.selected() == 30);
  t.release(Button::Pattern);
  t.wait(5);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  CHECK(t.selected() == 30);  // the grid was not a tap: the view did not flip

  t.tap(Button::Pattern, 40);
  CHECK(std::strcmp(t.title(), "ROLL") == 0);
  t.tap(Button::Pattern, 40);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
}

TEST_CASE("playing, taps with Pattern held queue the next pattern and several taps chain; the screen follows what plays") {
  Hands t;
  // Two more patterns with something in them: copies of A01.
  t.shift_tap(Button::Insert);  // A02
  t.shift_tap(Button::Insert);  // A03
  REQUIRE(t.selected() == 2);
  t.select_pattern(0);
  t.tap(Button::Play);
  t.wait(300);
  t.press(Button::Pattern);
  t.wait(350);
  t.pad(2);
  t.pad(1);
  t.release(Button::Pattern);
  t.wait(200);
  CHECK(t.selected() == 0);  // still A01 until it ends
  // A01 is 16 steps: at 125 BPM about 1.9 s. Then A03, then A02.
  t.wait(2200);
  CHECK(t.selected() == 2);
  t.wait(2100);
  CHECK(t.selected() == 1);
  t.wait(2100);
  CHECK(t.selected() == 1);  // the chain is used up: the last pattern repeats
  t.tap(Button::Play);
  CHECK_FALSE(t.h->engine().playing());
}

TEST_CASE("Play plays and stops at the press; Shift + Play taps the tempo") {
  Hands t;
  t.press(Button::Play);
  t.wait(5);
  CHECK(t.h->engine().playing());
  t.release(Button::Play);
  t.wait(50);
  CHECK(t.h->engine().playing());
  t.tap(Button::Play);
  CHECK_FALSE(t.h->engine().playing());

  t.press(Button::Shift);
  for (unsigned i = 0; i < 5; ++i) {
    t.tap(Button::Play, 30);
    t.wait(470);  // 500 ms between the taps: 120 BPM
  }
  t.release(Button::Shift);
  CHECK_FALSE(t.h->engine().playing());
  CHECK(t.h->engine().tempo() > 118'000u);
  CHECK(t.h->engine().tempo() < 122'000u);
  // A gap over two seconds starts the measure again.
  t.wait(2500);
  t.press(Button::Shift);
  t.tap(Button::Play, 30);
  t.wait(970);
  t.tap(Button::Play, 30);
  t.release(Button::Shift);
  CHECK(t.h->engine().tempo() > 58'000u);
  CHECK(t.h->engine().tempo() < 62'000u);
}

TEST_CASE("performance: MUTE, ACC ALL, SLIDE ALL and DELAY THROW on the four buttons (assigned), momentary, and Shift + the button latches") {
  Hands t;
  // The defaults are Note = MUTE, Instr = ACC ALL, Vol = FILL, Fx = FREEZE (tests/perf_buttons_test.cpp); SLIDE ALL and THROW, which
  // this test checks as it did before C4, are one assignment each away.
  t.assign_perf(Button::Vol, PerfAction::SlideAll);
  t.assign_perf(Button::Fx, PerfAction::Throw);
  seq::Sequencer& sequencer = app_sequencer();
  const std::uint8_t kMute = seq::kPerfMute;
  const std::uint8_t kAcc = seq::kPerfAccAll;
  const std::uint8_t kSlide = seq::kPerfSlideAll;
  CHECK(sequencer.perf() == 0);

  // Momentary: the flag is on while the button is down.
  t.press(Button::Note);
  CHECK(sequencer.perf() == kMute);
  t.release(Button::Note);
  CHECK(sequencer.perf() == 0);
  t.press(Button::Instr);
  CHECK(sequencer.perf() == kAcc);
  t.release(Button::Instr);
  t.press(Button::Vol);
  CHECK(sequencer.perf() == kSlide);
  t.press(Button::Instr);
  CHECK(sequencer.perf() == (kSlide | kAcc));  // together
  t.release(Button::Vol);
  t.release(Button::Instr);
  CHECK(sequencer.perf() == 0);

  // DELAY THROW is the engine's send A, not a sequencer flag.
  const float base = t.h->engine().mix(0, dsp::MixField::SendA);
  CHECK(base == doctest::Approx(0.3f));
  t.press(Button::Fx);
  t.wait(10);
  CHECK(sequencer.perf() == 0);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(1.0f));
  t.release(Button::Fx);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(base));

  // Latched with Shift, off with Shift again; the button's own release does not undo the latch.
  t.shift_tap(Button::Note);
  CHECK(sequencer.perf() == kMute);
  t.shift_tap(Button::Instr);
  CHECK(sequencer.perf() == (kMute | kAcc));
  t.shift_tap(Button::Note);
  CHECK(sequencer.perf() == kAcc);
  t.shift_tap(Button::Fx);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(1.0f));
  t.shift_tap(Button::Fx);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(base));

  // A latch stays when the screen is left; a button held when it is left is let go (its release would reach another screen).
  t.press(Button::Vol);
  CHECK(sequencer.perf() == (kAcc | kSlide));
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "VAULT") == 0);
  CHECK(sequencer.perf() == kAcc);
  t.release(Button::Vol);
  t.tap(Button::Pattern);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  CHECK(sequencer.perf() == kAcc);
  t.shift_tap(Button::Instr);
  CHECK(sequencer.perf() == 0);
}

// An in-place radix-2 FFT (host tests may use double).
void fft(std::vector<std::complex<double>>& a) {
  const std::size_t n = a.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; (j & bit) != 0; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      std::swap(a[i], a[j]);
    }
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
    const std::complex<double> wlen(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      for (std::size_t j = 0; j < len / 2; ++j) {
        const std::complex<double> u = a[i + j];
        const std::complex<double> v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
}

// The fundamental, in Hz, of the left channel of `lr` over `frames` frames from sample index `from`: the harmonic product spectrum of a
// Hann-windowed block (five harmonics), which does not mistake a filter's resonance on the fourth harmonic for the note.
double pitch_hz(const std::vector<std::int16_t>& lr, std::size_t from, std::size_t frames) {
  const std::size_t n = 32768;
  std::vector<std::complex<double>> a(n, std::complex<double>(0.0, 0.0));
  const std::size_t used = frames < n ? frames : n;
  for (std::size_t i = 0; i < used; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(used - 1));
    a[i] = std::complex<double>(static_cast<double>(lr[from + 2 * i]) * w, 0.0);
  }
  fft(a);
  std::vector<double> mag(n / 2);
  for (std::size_t k = 0; k < n / 2; ++k) {
    mag[k] = std::abs(a[k]);
  }
  const double bin_hz = 44100.0 / static_cast<double>(n);
  double best = 0.0;
  std::size_t best_k = 0;
  for (std::size_t k = static_cast<std::size_t>(40.0 / bin_hz); k < static_cast<std::size_t>(500.0 / bin_hz); ++k) {
    const double product = mag[k] * mag[2 * k] * mag[3 * k] * mag[4 * k] * mag[5 * k];
    if (product > best) {
      best = product;
      best_k = k;
    }
  }
  return static_cast<double>(best_k) * bin_hz;
}

TEST_CASE("playing, the keyboard transposes the pattern (the bottom left C is none); KEYS makes it play the voice instead") {
  Hands t;
  seq::Sequencer& sequencer = app_sequencer();
  // A steady tone: B01 with sixteen C2, every step sliding (SLIDE ALL latched), so the pitch is the transposition's.
  t.select_pattern(12);
  t.tap(Button::Rec);
  for (unsigned i = 0; i < 16; ++i) {
    t.pad(36);
  }
  t.tap(Button::Rec);
  t.assign_perf(Button::Vol, PerfAction::SlideAll);
  t.shift_tap(Button::Vol);
  CHECK(sequencer.perf() == seq::kPerfSlideAll);
  // A plain saw so that the pitch can be measured: the filter wide open with no resonance and no envelope, the distortion off, no delay
  // send (no echo of the old pitch in the measure).
  t.tap(Button::F1);
  t.turn(250);
  t.tap(Button::F2);
  t.turn(-100);
  t.tap(Button::F3);
  t.turn(-100);
  t.tap(Button::Params);
  t.tap(Button::F5);
  t.turn(-40);
  t.tap(Button::F7);
  t.turn(-10);
  CHECK(t.param(kDist, Distortion::Active) == doctest::Approx(0.0f));
  t.tap(Button::Params);
  t.tap(Button::Params);
  t.h->record(true);
  t.tap(Button::Play);
  t.wait(1500);
  const std::size_t window = 22050;  // half a second of frames
  const auto measure = [&](unsigned settle_ms) {
    t.wait(settle_ms);
    const std::size_t at = t.h->audio().size() - 2 * window;
    return pitch_hz(t.h->audio(), at, window);
  };
  const double base = measure(500);
  CHECK(sequencer.live_transpose() == 0);
  CHECK(base > 55.0);
  CHECK(base < 80.0);  // C2 is 65.4 Hz

  t.pad(key_pad(48));  // C3: twelve semitones above the bottom left C
  t.wait(30);
  CHECK(sequencer.live_transpose() == 12);
  CHECK(t.led(key_pad(48)) == app::kLedMid);
  CHECK(t.led(key_pad(36)) == app::kLedOn);  // the pattern's own note (all C2), at full level
  const double octave_up = measure(1200);
  CHECK(octave_up / base > 1.9);
  CHECK(octave_up / base < 2.1);

  t.pad(key_pad(43));  // G2: seven semitones
  CHECK(sequencer.live_transpose() == 7);
  const double fifth = measure(1200);
  CHECK(fifth / base > 1.45);  // 2^(7/12) = 1.498
  CHECK(fifth / base < 1.55);

  t.pad(key_pad(36));  // back to none
  t.wait(30);
  CHECK(sequencer.live_transpose() == 0);
  CHECK(t.led(key_pad(48)) == app::kLedDim);
  const double back = measure(1200);
  CHECK(back / base > 0.95);
  CHECK(back / base < 1.05);

  t.pad(kKeysPad);  // KEYS: the keyboard plays the voice, the transposition is left alone (a tap: it toggles at the release)
  t.pad(key_pad(48));
  t.wait(30);
  CHECK(sequencer.live_transpose() == 0);
  CHECK(t.led(kKeysPad) == app::kLedMid);
  t.pad(kKeysPad);
  t.wait(40);
  CHECK(t.led(kKeysPad) == app::kLedDim);

  // The transposition ends with the run, also when another screen is up.
  t.pad(key_pad(47));
  CHECK(sequencer.live_transpose() == 11);
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "VAULT") == 0);
  t.tap(Button::Play);  // Play is global
  t.wait(50);
  CHECK_FALSE(t.h->engine().playing());
  CHECK(sequencer.live_transpose() == 0);
  t.tap(Button::Pattern);
  t.shift_tap(Button::Vol);
  CHECK(sequencer.perf() == 0);
}

TEST_CASE("File opens the GEN vault; FILE again loads the slot into the pattern and the sound and goes back") {
  Hands t;
  seq::gen::Preset slot;
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "VAULT") == 0);
  t.tap(Button::F4);                    // bank D: ACID
  for (unsigned i = 0; i < 17; ++i) {
    t.turn(1);                          // slot 17
  }
  seq::gen::factory(slot, 3, 17);
  const seq::gen::ParamValues v = seq::gen::to_values(slot.patch);
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == slot.pattern.steps[i].note);
    CHECK(t.pat().steps[i].flags == slot.pattern.steps[i].flags);
  }
  CHECK(t.h->engine().tempo() == v.tempo_mbpm);
  CHECK(t.param(kVoice, Tb303::Cutoff) == doctest::Approx(v.voice[0]).epsilon(0.001));
  CHECK(t.param(kVoice, Tb303::Reso) == doctest::Approx(v.voice[1]).epsilon(0.001));
  CHECK(t.param(kDist, Distortion::Drive) == doctest::Approx(v.dist[2]).epsilon(0.001));
  CHECK(t.param(kVoice, Tb303::Volume) == doctest::Approx(0.0f));  // the level is not part of a preset

  // GEN: a new seed gives a take that differs; PARAMS says what a load takes.
  const float cutoff = t.param(kVoice, Tb303::Cutoff);
  const Pattern loaded = t.pat();
  t.tap(Button::File);
  t.tap(Button::Rec);   // GEN
  t.tap(Button::Params);  // PATTERN only
  t.tap(Button::File);
  bool pattern_changed = false;
  for (unsigned i = 0; i < 16; ++i) {
    pattern_changed = pattern_changed || t.pat().steps[i].note != loaded.steps[i].note || t.pat().steps[i].flags != loaded.steps[i].flags;
  }
  CHECK(pattern_changed);
  CHECK(t.param(kVoice, Tb303::Cutoff) == doctest::Approx(cutoff));  // the sound was not loaded

  // PATTERN goes back without loading.
  const Pattern kept = t.pat();
  t.tap(Button::File);
  t.tap(Button::Right);
  t.tap(Button::Pattern);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == kept.steps[i].note);
  }
}

TEST_CASE("long patterns: the step pads show a page of 16 and the cursor and the playhead take the page along") {
  Hands t;
  t.shift_tap(Button::F5);  // DOUBLE: 32 steps
  CHECK(t.pat().length == 32);
  const Pattern p = t.pat();
  // Page 1: the pads are steps 1..16.
  for (unsigned k = 1; k < 16; ++k) {
    CHECK((t.led(step_pad_of(k)) == app::kLedOff) == (describe(p, k).kind == Cell::Rest));
  }
  t.tap(Button::Left);  // wraps to step 32: page 2
  t.wait(30);
  t.shift_tap(Button::Delete);  // (clears everything: only to compare the pads of page 2 with the pattern)
  CHECK(pattern_is_empty(t.pat()));
  t.shift_tap(Button::F8);  // UNDO
  t.wait(30);
  for (unsigned k = 0; k < 15; ++k) {  // the cursor is step 32 = pad 15 of page 2
    CHECK((t.led(step_pad_of(k)) == app::kLedOff) == (describe(p, 16 + k).kind == Cell::Rest));
  }
  t.tap(Button::Right);  // step 1: page 1
  t.wait(30);
  CHECK((t.led(step_pad_of(4)) == app::kLedOff) == (describe(p, 4).kind == Cell::Rest));

  // Playing follows the playhead's page.
  t.tap(Button::Play);
  t.wait(2200);  // a little over 16 steps at 125 BPM
  bool second_page = false;
  for (unsigned k = 0; k < 16; ++k) {
    second_page = second_page || t.led(step_pad_of(k)) == app::kLedOn;
  }
  CHECK(second_page);
  t.tap(Button::Play);
  CHECK(t.h->display().peak_frame_words() < 2048u);

  // Shorter lengths: the pads past the end are dark.
  t.tap(Button::Params);
  t.tap(Button::Params);
  t.tap(Button::F3);
  t.turn(-3);  // 8 steps
  CHECK(t.pat().length == 8);
  t.wait(30);
  for (unsigned k = 8; k < 16; ++k) {
    CHECK(t.led(step_pad_of(k)) == app::kLedOff);
  }
}

TEST_CASE("CONFIG changes the theme") {
  Hands t;
  const ui::ThemeId a = t.h->runtime().context().theme();
  t.tap(Button::Config);
  CHECK(t.h->runtime().context().theme() != a);
  t.tap(Button::Config);
  t.tap(Button::Config);
  CHECK(t.h->runtime().context().theme() == a);
}

TEST_CASE("REC + Play + a key writes the note under the playhead and sounds it") {
  Hands t;
  t.select_pattern(20);
  REQUIRE(pattern_is_empty(t.pat()));
  t.tap(Button::Rec);
  t.tap(Button::Play);
  t.wait(300);
  t.pad(key_pad(52));
  t.tap(Button::Play);
  unsigned written = 0;
  for (unsigned i = 0; i < 16; ++i) {
    if ((t.pat().steps[i].flags & kStepRest) == 0 && t.pat().steps[i].note == 52) {
      ++written;
    }
  }
  CHECK(written == 1);
}

TEST_CASE("worst cases for the display list: a full 64-step line, every ramp, all the locks, 48 patterns and a chain of 16") {
  Hands t;
  // The generator at full density with every accent and slide chance, over 64 steps.
  t.tap(Button::Params);
  t.tap(Button::Params);
  t.tap(Button::F7);
  t.turn(100);
  t.tap(Button::F8);
  t.turn(100);
  t.shift_tap(Button::F5);
  t.shift_tap(Button::F5);
  REQUIRE(t.pat().length == 64);
  t.shift_tap(Button::F1);  // RANDOM over the 64 steps
  // Every step of the first page locked on a lane, the cursor on the last step of the page.
  t.tap(Button::Params);
  t.tap(Button::F1);
  for (unsigned k = 0; k < 16; ++k) {
    t.down(step_pad_of(k));
    t.turn(7 + static_cast<int>(k));
    t.up(step_pad_of(k));
  }
  t.tap(Button::Rec);
  t.press(Button::Shift);
  t.wait(30);
  const std::size_t peak_tools = t.h->display().peak_frame_words();
  t.release(Button::Shift);
  CHECK(peak_tools < 2048u);
  t.tap(Button::Pattern, 40);  // the piano roll
  t.wait(60);
  t.tap(Button::Pattern, 40);
  t.wait(60);

  // 48 patterns, all with something in them, and a chain of 16 while playing.
  for (unsigned i = 1; i < kPatternCount; ++i) {
    t.shift_tap(Button::Insert);
  }
  t.select_pattern(0);
  t.tap(Button::Play);
  t.wait(200);
  t.press(Button::Pattern);
  t.wait(350);
  for (unsigned i = 1; i <= 16; ++i) {
    t.pad(i * 2);
  }
  t.wait(100);
  CHECK(t.h->display().peak_frame_words() < 2048u);
  t.release(Button::Pattern);
  t.tap(Button::Play);
  CHECK(t.h->runtime().frame_failures() == 0u);
  CHECK(t.h->display().render_failures() == 0u);
  CHECK(t.h->display().peak_frame_words() < 2048u);
  // Headroom: the worst frame (the grid of 48 patterns with a chain of 16, drawn in colour passes) is about 1400 words.
  CHECK(t.h->display().peak_frame_words() < 1700u);
}

TEST_CASE("the vault draws every one of its 512 slots, and a GEN take of each, inside the display list") {
  Hands t;
  t.tap(Button::File);
  REQUIRE(std::strcmp(t.title(), "VAULT") == 0);
  const Button banks[8] = {Button::F1, Button::F2, Button::F3, Button::F4, Button::F5, Button::F6, Button::F7, Button::F8};
  for (unsigned b = 0; b < 8; ++b) {
    t.tap(banks[b], 5);
    for (unsigned s = 0; s < 64; ++s) {
      t.turn(1);
      t.wait(36);
      if (s % 8 == 7) {
        t.tap(Button::Rec, 5);  // a take with another seed every few slots
        t.wait(36);
      }
    }
  }
  CHECK(t.h->runtime().frames() > 300u);
  CHECK(t.h->runtime().frame_failures() == 0u);
  CHECK(t.h->display().render_failures() == 0u);
  CHECK(t.h->display().peak_frame_words() < 1900u);
}

TEST_CASE("Pattern held + Copy copies the pattern; Shift + Copy pastes it into the selected one") {
  Hands t;
  const Pattern first = bank().pattern(0);
  t.press(Button::Pattern);
  t.wait(350);
  t.tap(Button::Copy);  // the pattern, not the step
  t.release(Button::Pattern);
  t.wait(5);
  t.select_pattern(7);
  REQUIRE(pattern_is_empty(t.pat()));
  t.shift_tap(Button::Copy);
  CHECK_FALSE(pattern_is_empty(t.pat()));
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == first.steps[i].note);
    CHECK(t.pat().steps[i].flags == first.steps[i].flags);
  }
  CHECK(pattern_is_empty(bank().pattern(8)));
  // Copying a step afterwards makes the next paste a step again.
  t.tap(Button::Copy);
  t.pad(step_pad_of(1));
  t.shift_tap(Button::Copy);
  CHECK(t.pat().steps[1].note == t.pat().steps[0].note);
}

TEST_CASE("sound: silent until something plays; a key pad plays the voice; Play plays the factory line") {
  Hands t;
  t.h->record(true);
  t.wait(200);
  CHECK(rms(t.h->audio(), 0) < 1e-6);  // nothing played yet: digital silence

  t.down(key_pad(48));
  t.wait(300);
  t.up(key_pad(48));
  t.wait(600);
  CHECK(rms(t.h->audio(), 0) > 0.01);

  t.wait(2000);  // let the delay's tail go
  const std::size_t mark = t.h->audio().size();
  t.tap(Button::Play);
  t.wait(2500);
  CHECK(rms(t.h->audio(), mark) > 0.05);
  t.tap(Button::Play);
}

// The master's peak before its clipper over `ms` of the run, block by block.
float peak_over(Hands& t, unsigned ms) {
  float peak = 0.0f;
  for (unsigned i = 0; i < ms; ++i) {
    t.wait(1);
    const dsp::EngineStats st = t.h->engine().stats();
    peak = std::max(peak, std::max(st.peak_l, st.peak_r));
  }
  return peak;
}

TEST_CASE("headroom: the factory line at the default levels stays under full scale before the clipper") {
  Hands t;
  t.tap(Button::Play);
  const float peak = peak_over(t, 4500);
  t.tap(Button::Play);
  CHECK(peak < 1.0f);  // measured 0.86 (the master's soft clip starts at 0.8: the engine counts those blocks as `clipped`)
}

// Loads vault slot (bank, slot) the way a hand does, plays it from the top and returns the master's peak before the clipper. What it
// loaded is checked against the generator (the distortion's Active, Type and Drive), so that a guard built on a slot cannot drift onto
// another preset unseen.
float vault_peak(Hands& t, unsigned bank_index, unsigned slot_index, unsigned& at_slot) {
  static const Button banks[8] = {Button::F1, Button::F2, Button::F3, Button::F4, Button::F5, Button::F6, Button::F7, Button::F8};
  seq::gen::Preset want;
  seq::gen::factory(want, bank_index, slot_index);
  const seq::gen::ParamValues v = seq::gen::to_values(want.patch);
  t.tap(Button::File);
  t.tap(banks[bank_index], 5);
  t.turn(static_cast<int>(slot_index) - static_cast<int>(at_slot));
  at_slot = slot_index;
  t.tap(Button::File);  // load, back to the 303 screen
  CHECK(t.param(kDist, Distortion::Active) == doctest::Approx(v.dist[0]));
  CHECK(t.param(kDist, Distortion::Type) == doctest::Approx(v.dist[1]));
  CHECK(t.param(kDist, Distortion::Drive) == doctest::Approx(v.dist[2]).epsilon(0.001));
  t.tap(Button::Play);
  const float peak = peak_over(t, 4000);
  t.tap(Button::Play);
  t.wait(300);
  return peak;
}

// The named presets of the sample: the loudest of the vault before the distortion models were leveled across Drive (B28 peaked at 1.88,
// D53 at 1.16) and the loudest after (A20, 0.91 of all 512 slots). The distortion model and Drive they must have are checked against the
// generator, so that a change of the generator's content moves the guard visibly instead of onto other presets.
struct Named {
  unsigned bank;
  unsigned slot;
  int type;     // 0 GRIT .. 3 FUZZ
  float drive;
  const char* name;
};
const Named kNamed[] = {
    {1, 27, 3, 0.835f, "B28"},  // FUZZ, a square, Drive 0.84
    {3, 52, 0, 0.879f, "D53"},  // GRIT, Drive 0.88
    {0, 19, 0, 0.633f, "A20"},  // GRIT, Drive 0.63, Mix 0.99: the loudest of the vault now
};

// A sample of the vault: a slot of three banks, the first FUZZ preset of four banks, and the named presets.
std::vector<std::pair<unsigned, unsigned>> vault_sample() {
  std::vector<std::pair<unsigned, unsigned>> picks;
  for (unsigned b = 0; b < 8; b += 3) {
    picks.emplace_back(b, (b * 11u + 5u) % 64u);
  }
  seq::gen::Preset pr;
  for (unsigned b = 1; b < 8; b += 2) {
    for (unsigned s = 1; s < 64; ++s) {
      seq::gen::factory(pr, b, s);
      if (pr.patch.dist_active && pr.patch.dist_type == 3) {
        picks.emplace_back(b, s);
        break;
      }
    }
  }
  for (const Named& n : kNamed) {
    picks.emplace_back(n.bank, n.slot);
  }
  return picks;
}

TEST_CASE("headroom: the named presets of the sample are the presets they stand for") {
  for (const Named& n : kNamed) {
    INFO(n.name);
    seq::gen::Preset pr;
    seq::gen::factory(pr, n.bank, n.slot);
    CHECK(pr.patch.dist_active);
    CHECK(pr.patch.dist_type == n.type);
    CHECK(pr.patch.dist_drive == doctest::Approx(n.drive).epsilon(0.002));
    CHECK(pr.patch.dist_mix > 0.9f);  // all three are nearly all wet
  }
}

// The worst peak of the vault sample at the default levels.
float vault_sample_worst() {
  static const float worst = [] {
    Hands t;
    unsigned at_slot = 0;
    float w = 0.0f;
    for (const auto& pick : vault_sample()) {
      w = std::max(w, vault_peak(t, pick.first, pick.second, at_slot));
    }
    return w;
  }();
  return worst;
}

TEST_CASE("headroom: vault presets (FUZZ ones included) at the default levels stay under full scale before the clipper") {
  CHECK(vault_sample_worst() < 1.0f);  // measured 0.90, A20 (1.45 over the first seven picks, 1.88 for B28, before the models were leveled across Drive)
}

TEST_CASE("sound: MUTE holds the notes back while the pattern runs; VOLUME is the level of the whole voice") {
  Hands t;
  t.h->record(true);
  t.tap(Button::Play);
  t.wait(2200);
  const std::size_t a0 = t.h->audio().size();
  t.wait(1500);
  const double playing = rms(std::vector<std::int16_t>(t.h->audio().begin() + static_cast<std::ptrdiff_t>(a0), t.h->audio().end()), 0);
  REQUIRE(playing > 0.05);

  t.press(Button::Note);  // MUTE
  t.wait(1500);           // the echoes of the last notes die away
  const std::size_t m0 = t.h->audio().size();
  t.wait(1500);
  const double muted = rms(std::vector<std::int16_t>(t.h->audio().begin() + static_cast<std::ptrdiff_t>(m0), t.h->audio().end()), 0);
  CHECK(muted < playing * 0.15);
  t.release(Button::Note);
  t.wait(2000);
  const std::size_t b0 = t.h->audio().size();
  t.wait(1500);
  const double back = rms(std::vector<std::int16_t>(t.h->audio().begin() + static_cast<std::ptrdiff_t>(b0), t.h->audio().end()), 0);
  CHECK(back > playing * 0.5);

  t.tap(Button::F8);  // VOLUME: -6 dB down to about -24 dB
  t.turn(-36);
  t.wait(1500);
  const std::size_t v0 = t.h->audio().size();
  t.wait(1500);
  const double quiet = rms(std::vector<std::int16_t>(t.h->audio().begin() + static_cast<std::ptrdiff_t>(v0), t.h->audio().end()), 0);
  CHECK(quiet < back * 0.5);  // 18 dB down, but the hot signal is clipped before: well under half
  t.tap(Button::Play);
}

TEST_CASE("MOTION + Delete clears the shown lane and nothing else; MOTION + a step tap clears that step's locks of the lane") {
  Hands t;
  // Locks on the cutoff lane (steps 4 and 7) and on the reso lane (step 4).
  t.down(step_pad_of(3));
  t.turn(10);
  t.tap(Button::F2);
  t.turn(-10);
  t.up(step_pad_of(3));
  t.tap(Button::F1);
  t.down(step_pad_of(6));
  t.turn(10);
  t.up(step_pad_of(6));
  REQUIRE(step_has_motion(t.pat(), 0, 3));
  REQUIRE(step_has_motion(t.pat(), 0, 6));
  REQUIRE(step_has_motion(t.pat(), 1, 3));

  // A tap on a step pad with MOTION held: that step's locks of the lane shown (cutoff) go, the rest stays, the cursor stays.
  t.pad(step_pad_of(4));  // the cursor on step 5
  const Pattern before = t.pat();
  t.down(kMotionPad);
  t.pad(step_pad_of(3));
  t.up(kMotionPad);
  CHECK_FALSE(step_has_motion(t.pat(), 0, 3));
  CHECK(step_has_motion(t.pat(), 0, 6));
  CHECK(step_has_motion(t.pat(), 1, 3));  // another lane of the same step
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == before.steps[i].note);
    CHECK(t.pat().steps[i].flags == before.steps[i].flags);
  }
  t.tap(Button::Delete);  // the cursor is still on step 5 (the tap with MOTION held did not move it)
  CHECK((t.pat().steps[4].flags & kStepRest) != 0);
  CHECK(t.pat().steps[3].flags == before.steps[3].flags);

  // MOTION + Delete: the whole lane, and the cursor's step is left as it is.
  t.pad(step_pad_of(6));  // the cursor on step 7
  const Pattern kept = t.pat();
  t.down(kMotionPad);
  t.tap(Button::Delete);
  t.up(kMotionPad);
  CHECK_FALSE(pattern_has_motion(t.pat(), 0));
  CHECK(step_has_motion(t.pat(), 1, 3));  // the reso lane is not the one shown
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == kept.steps[i].note);
    CHECK(t.pat().steps[i].flags == kept.steps[i].flags);  // no REST appeared on the cursor's step
  }
  // Shift + Delete with MOTION held: every lane.
  t.down(kMotionPad);
  t.press(Button::Shift);
  t.tap(Button::Delete);
  t.release(Button::Shift);
  t.up(kMotionPad);
  CHECK_FALSE(pattern_has_motion(t.pat(), kMotionLanes));
  CHECK_FALSE(pattern_is_empty(t.pat()));  // and the notes are still there
}

TEST_CASE("a p-lock with an F key held gives the latch back at the release, like any hold-and-turn") {
  Hands t;
  t.tap(Button::F1);  // the cutoff is latched
  t.down(step_pad_of(3));
  t.press(Button::F3);  // hold ENV MOD
  t.turn(5);            // p-lock on its lane
  t.release(Button::F3);
  t.up(step_pad_of(3));
  CHECK(step_has_motion(t.pat(), 2, 3));
  CHECK_FALSE(step_has_motion(t.pat(), 0, 3));
  const float cutoff = t.param(kVoice, Tb303::Cutoff);
  const float env = t.param(kVoice, Tb303::EnvMod);
  t.turn(5);  // nothing held: the latched key, the cutoff
  CHECK(t.param(kVoice, Tb303::Cutoff) > cutoff);
  CHECK(t.param(kVoice, Tb303::EnvMod) == doctest::Approx(env));
}

TEST_CASE("Shift shows the tools row only while the encoder is still: a coarse Shift + turn keeps the parameter's name on the keys") {
  Hands t;
  const auto first_label = [&]() {
    app::SoftKeys keys;
    t.h->runtime().context().screen()->soft_keys(t.h->runtime().context(), keys);
    return std::string(keys.label[0] != nullptr ? keys.label[0] : "");
  };
  CHECK(first_label() == "CUTOFF");
  t.press(Button::Shift);
  t.wait(100);
  CHECK(first_label() == "CUTOFF");  // not yet: a turn may follow
  t.wait(700);
  CHECK(first_label() == "RANDOM");
  t.turn(3);  // a coarse step: the strip with its names and values comes back
  CHECK(first_label() == "CUTOFF");
  t.wait(300);
  CHECK(first_label() == "CUTOFF");
  t.wait(700);
  CHECK(first_label() == "RANDOM");
  t.release(Button::Shift);
  CHECK(first_label() == "CUTOFF");

  // The tools work from the press of Shift, with or without the row shown.
  t.press(Button::Shift);
  t.tap(Button::F6);  // CLEAR at once
  t.release(Button::Shift);
  CHECK(pattern_is_empty(t.pat()));
}

TEST_CASE("Shift + Insert on the last pattern refuses (nothing wraps round to overwrite A01)") {
  Hands t;
  const Pattern first = bank().pattern(0);
  t.select_pattern(kPatternCount - 1);
  REQUIRE(t.selected() == kPatternCount - 1);
  t.shift_tap(Button::Insert);
  CHECK(t.selected() == kPatternCount - 1);
  CHECK_FALSE(pattern_is_empty(bank().pattern(0)));
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(bank().pattern(0).steps[i].note == first.steps[i].note);
    CHECK(bank().pattern(0).steps[i].flags == first.steps[i].flags);
  }
  // One before the last still duplicates.
  t.select_pattern(kPatternCount - 2);
  t.shift_tap(Button::Insert);
  CHECK(t.selected() == kPatternCount - 1);
}

TEST_CASE("hold a step + UP / DOWN moves the held step's octave, not the cursor's; UP / DOWN alone are a semitone on the cursor step") {
  Hands t;
  const Pattern before = bank().pattern(0);
  t.down(step_pad_of(5));
  t.tap(Button::Up);
  t.tap(Button::Up);
  t.tap(Button::Down);
  t.up(step_pad_of(5));
  CHECK(t.pat().steps[5].note == before.steps[5].note + 12);
  CHECK(t.pat().steps[0].note == before.steps[0].note);  // the cursor's step is untouched
  // Two held steps move together.
  t.down(step_pad_of(2));
  t.down(step_pad_of(3));
  t.tap(Button::Down);
  t.up(step_pad_of(2));
  t.up(step_pad_of(3));
  CHECK(t.pat().steps[2].note == before.steps[2].note - 12);
  CHECK(t.pat().steps[3].note == before.steps[3].note - 12);
  // Not held: the cursor's step, as before; and the held pads did not move the cursor.
  t.tap(Button::Up);
  CHECK(t.pat().steps[0].note == before.steps[0].note + 1);
}

TEST_CASE("the screen keeps following the playing pattern while the vault is up") {
  Hands t;
  t.shift_tap(Button::Insert);  // A02, a copy of A01
  t.select_pattern(0);
  t.tap(Button::Play);
  t.wait(300);
  t.press(Button::Pattern);
  t.wait(350);
  t.pad(1);
  t.release(Button::Pattern);
  t.tap(Button::File);
  REQUIRE(std::strcmp(t.title(), "VAULT") == 0);
  CHECK(t.selected() == 0);
  t.wait(2400);  // A01 ends: A02 starts
  CHECK(std::strcmp(t.title(), "VAULT") == 0);
  CHECK(t.selected() == 1);
  t.tap(Button::Play);
}

TEST_CASE("RANDOM is not the same line after every boot") {
  Pattern first;
  {
    Hands t;
    t.shift_tap(Button::F1);
    first = t.pat();
  }
  Pattern second;
  {
    Hands t;
    t.wait(37);  // a hand does not press at the same moment twice
    t.shift_tap(Button::F1);
    second = t.pat();
  }
  bool differs = false;
  for (unsigned i = 0; i < 16; ++i) {
    differs = differs || first.steps[i].note != second.steps[i].note || first.steps[i].flags != second.steps[i].flags;
  }
  CHECK(differs);
}

TEST_CASE("playing, the playhead stays visible on the pad when the cursor is on the same step") {
  Hands t;
  t.tap(Button::Left);  // the cursor on step 16
  t.tap(Button::Play);
  unsigned on_cursor = 0;
  unsigned bright = 0;
  unsigned run = 0;
  for (unsigned i = 0; i < 2500; ++i) {
    t.wait(1);
    const seq::Playhead ph = app_sequencer().playhead();
    if (ph.playing && ph.pattern == 0 && ph.step == 15) {
      ++run;
      if (run >= 70) {  // the LEDs follow once a frame (33 ms): after two frames they show this step
        ++on_cursor;
        bright += t.led(step_pad_of(15)) >= app::kLedMid ? 1u : 0u;
      }
    } else {
      run = 0;
    }
  }
  t.tap(Button::Play);
  CHECK(on_cursor > 20);
  CHECK(bright == on_cursor);
}
