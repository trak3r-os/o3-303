// SPDX-License-Identifier: GPL-3.0-or-later
// INSTRUMENT (docs/UX-303.md 5.5, slice I1): the Params button (opens VOICE from any screen but the vault, again cycles the pages), the latches (VOICE shares
// the SOUND latch with PERFORM, the other pages keep their own), the keys of every page reaching their parameter, the home map's pads, Rec, colour buttons
// and locks forwarded to the one HomeMap HOME has, the empty Shift row, and the display list. The pure view logic is the screen's own; the scenario is
// instrument.txt. The view logic (read-outs, drawer cells, the lock label) is tested on its own functions.
#include <doctest/doctest.h>

#include "hands.h"

#include <o3sdk/app/pad_leds.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/pattern.h>
#include <o3_303/perform.h>
#include <o3_303/seq/ops.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/growl/growl.h>

#include <cstring>

using namespace hands;
using namespace o3_303;

namespace {

bool is_title(Hands& t, const char* title) { return std::strcmp(t.title(), title) == 0; }
bool on_instrument(Hands& t) { return std::strncmp(t.title(), "INSTRUMENT", 10) == 0; }

// F key `k` (0..7) of the page shown moves the parameter (unit, id) one way or the other.
bool key_moves(Hands& t, unsigned k, dsp::UnitId unit, unsigned id) {
  t.tap(app::fkey(k));
  const float before = t.param(unit, id);
  t.turn(7);
  const float up = t.param(unit, id);
  t.turn(-14);
  const float down = t.param(unit, id);
  return up != before || down != before;
}

}  // namespace

TEST_CASE("Params opens INSTRUMENT on VOICE from any screen, again it cycles VOICE FX MODS MORE DEEP and wraps") {
  Hands t;
  CHECK_FALSE(on_instrument(t));
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT VOICE"));
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT FX"));
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT MODS"));
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT MORE"));
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT DEEP"));
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT VOICE"));
  t.shift_tap(Button::Params);  // Shift changes nothing
  CHECK(is_title(t, "INSTRUMENT FX"));
  // From another screen it always opens VOICE (D21), whatever page was left.
  t.to_perform();
  CHECK(std::strcmp(t.title(), "PERFORM") == 0);
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT VOICE"));
  t.to_home();
  CHECK_FALSE(on_instrument(t));
  t.to_instrument(kInstMods);
  CHECK(is_title(t, "INSTRUMENT MODS"));
  t.tap(Button::Pattern);
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT VOICE"));
}

TEST_CASE("on the vault Params stays the vault's, and File opens the vault from INSTRUMENT") {
  Hands t;
  t.tap(Button::File);
  CHECK(is_title(t, "VAULT"));
  t.tap(Button::Params);  // what a load takes: no screen change
  CHECK(is_title(t, "VAULT"));
  t.tap(Button::Pattern);
  CHECK_FALSE(on_instrument(t));
  t.to_instrument(kInstFx);
  t.tap(Button::File);
  CHECK(is_title(t, "VAULT"));
}

TEST_CASE("VOICE shares the SOUND latch with PERFORM; FX, MODS and MORE each keep their own") {
  Hands t;
  const float decay0 = t.param(kVoice, AcidVoice::Decay);
  const float cutoff0 = t.param(kVoice, AcidVoice::Cutoff);
  t.to_instrument(kInstVoice);
  t.tap(Button::F4);  // DECAY, on VOICE
  t.to_perform();
  t.turn(5);  // PERFORM's sound knob is the same latch
  CHECK(t.param(kVoice, AcidVoice::Decay) != decay0);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == cutoff0);
  t.tap(Button::Params);  // VOICE again: still DECAY
  const float decay1 = t.param(kVoice, AcidVoice::Decay);
  t.turn(5);
  CHECK(t.param(kVoice, AcidVoice::Decay) != decay1);

  t.tap(Button::Params);  // FX: F1 DRIVE at first
  const float drive0 = t.param(kDist, Distortion::Drive);
  t.turn(5);
  CHECK(t.param(kDist, Distortion::Drive) != drive0);
  t.tap(Button::F6);  // DLY FB on FX
  t.tap(Button::Params);  // MODS: its own, F1 VOWEL
  const float vowel0 = t.param(kGrowl, Growl::Vowel);
  t.turn(5);
  CHECK(t.param(kGrowl, Growl::Vowel) != vowel0);
  t.tap(Button::Params);
  t.tap(Button::Params);
  t.tap(Button::Params);  // VOICE (through MORE and DEEP)
  t.tap(Button::Params);  // FX again: DLY FB is still the latched key
  const float fb0 = t.param(kDelay, units::Delay::Feedback);
  t.turn(-5);
  CHECK(t.param(kDelay, units::Delay::Feedback) != fb0);
}

TEST_CASE("every key of FX, MODS, MORE and DEEP reaches its parameter") {
  Hands t;
  t.to_instrument(kInstFx);
  CHECK(key_moves(t, 0, kDist, Distortion::Drive));
  CHECK(key_moves(t, 1, kDist, Distortion::Tone));
  CHECK(key_moves(t, 2, kDist, Distortion::Mix));
  CHECK(key_moves(t, 3, kDist, Distortion::Type));
  CHECK(key_moves(t, 4, kDelay, units::Delay::Time));
  CHECK(key_moves(t, 5, kDelay, units::Delay::Feedback));
  CHECK(key_moves(t, 7, kDelay, units::Delay::Tone));
  const float send0 = t.h->engine().mix(0, dsp::MixField::SendA);  // F7 DLY SEND: slot 0's send A
  t.tap(Button::F7);
  t.turn(-5);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) != send0);

  t.tap(Button::Params);  // MODS
  CHECK(key_moves(t, 0, kGrowl, Growl::Vowel));
  CHECK(key_moves(t, 1, kGrowl, Growl::FormantQ));
  CHECK(key_moves(t, 2, kGrowl, Growl::Mix));
  CHECK(key_moves(t, 3, kGrowl, Growl::CombMix));

  t.tap(Button::Params);  // MORE
  const std::uint32_t tempo0 = t.h->engine().tempo();
  t.tap(Button::F1);
  t.turn(4);
  CHECK(t.h->engine().tempo() != tempo0);
  const unsigned gate0 = t.pat().gate;
  t.tap(Button::F4);
  t.turn(-2);
  CHECK(t.pat().gate != gate0);
  const std::int8_t tr0 = t.pat().transpose;
  t.tap(Button::F5);
  t.turn(2);
  CHECK(t.pat().transpose != tr0);

  t.tap(Button::Params);  // DEEP
  CHECK(key_moves(t, 0, kGrowl, Growl::FormantMix));
  CHECK(key_moves(t, 1, kGrowl, Growl::CombTune));
  CHECK(key_moves(t, 2, kGrowl, Growl::CombFeedback));
  CHECK(key_moves(t, 4, kDelay, units::Delay::Mode));
}

TEST_CASE("the pads, Rec and the cursor are HOME's own: the same home map") {
  Hands t;
  t.to_instrument(kInstFx);
  t.tap(Button::Rec);  // step entry: Rec, then a key writes the cursor step and advances
  t.pad(key_pad(48));
  t.pad(key_pad(50));
  CHECK(t.pat().steps[0].note == 48);
  CHECK(t.pat().steps[1].note == 50);
  t.to_home();  // the cursor and Rec went with the screen
  t.pad(key_pad(52));
  CHECK(t.pat().steps[2].note == 52);
  t.to_instrument(kInstMods);
  t.pad(key_pad(53));
  CHECK(t.pat().steps[3].note == 53);
  t.tap(Button::Rec);
  t.tap(Button::Left);  // the arrows move the cursor: the pad of the cursor step is the same on the next screen
  t.tap(Button::Left);
  t.pad(key_pad(55));  // Rec is off: it plays the voice and writes nothing
  CHECK(t.pat().steps[3].note == 53);
  CHECK(t.pat().steps[2].note == 52);
}

TEST_CASE("a held step + a turn locks the latched knob of the page on that step, its lane claimed") {
  Hands t;
  t.to_instrument(kInstFx);
  t.tap(Button::F1);  // DRIVE: no lane drives it in a fresh pattern
  t.down(step_pad_of(3));
  t.turn(10);
  t.up(step_pad_of(3));
  CHECK(t.pat().lanes[0].target == kLaneDist);  // the first lane with no motion
  CHECK(seq::lane_has_motion(t.pat(), 0));
  // A key no lane can drive moves the knob and writes no motion (MORE's TEMPO).
  t.to_home();
  t.to_instrument(kInstMore);
  const std::uint32_t tempo0 = t.h->engine().tempo();
  t.tap(Button::F1);
  t.down(step_pad_of(5));
  t.turn(5);
  t.up(step_pad_of(5));
  CHECK(t.h->engine().tempo() != tempo0);
  CHECK_FALSE(seq::lane_has_motion(t.pat(), 1));
}

TEST_CASE("the colour buttons are HOME's actions here too, and leaving INSTRUMENT ends the ones held") {
  Hands t;
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
  t.to_instrument(kInstMods);
  t.press(Button::Fx);  // FREEZE by default
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(1.0f));
  t.tap(Button::Params);  // a page change goes on holding it
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(1.0f));
  t.release(Button::Fx);
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
  t.press(Button::Fx);
  t.wait(20);
  t.tap(Button::Pattern);  // leaving ends it: its release would reach another screen
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
  t.release(Button::Fx);
}

TEST_CASE("the Shift row is empty: Shift + F does nothing, and its release is not the page's") {
  Hands t;
  t.to_instrument(kInstMore);
  const Pattern before = t.pat();
  t.tap(Button::F4);  // GATE
  const unsigned gate0 = t.pat().gate;
  t.shift_tap(Button::F6);  // would be CLEAR on the 303 screen
  t.shift_tap(Button::F8);  // ... and UNDO
  CHECK(t.pat().steps[0].note == before.steps[0].note);
  CHECK(t.pat().steps[0].flags == before.steps[0].flags);
  t.turn(-2);  // the latch is still GATE, not F6 or F8
  CHECK(t.pat().gate != gate0);
}

TEST_CASE("every page of INSTRUMENT fits the display list, stopped, playing and with Shift held") {
  Hands t;
  t.tap(Button::Play);
  t.wait(300);
  for (unsigned page = kInstVoice; page < kInstrumentPageCount; ++page) {
    t.to_instrument(page);
    t.wait(60);
    t.press(Button::Shift);
    t.wait(700);
    t.release(Button::Shift);
    t.down(step_pad_of(2));
    t.wait(40);
    t.up(step_pad_of(2));
    t.to_home();
  }
  t.tap(Button::Play);
  CHECK(t.h->runtime().frame_failures() == 0u);
  CHECK(t.h->display().render_failures() == 0u);
  CHECK(t.h->display().peak_frame_words() < 1800u);
}

// ---- Today's latches --------------------------------------------------------------------------------------------------------------------------

TEST_CASE("VOICE's latch is PERFORM's, not HOME's: HOME still has its own until H4 (D3), and a latch made there does not reach INSTRUMENT") {
  Hands t;
  t.tap(Button::F4);  // HOME's own page keys: DECAY latched on the 303 screen
  t.to_instrument(kInstVoice);
  const float decay0 = t.param(kVoice, AcidVoice::Decay);
  const float cutoff0 = t.param(kVoice, AcidVoice::Cutoff);
  t.turn(5);  // VOICE has Rig::sound_keys: F1 CUTOFF, as PERFORM has it
  CHECK(t.param(kVoice, AcidVoice::Cutoff) != cutoff0);
  CHECK(t.param(kVoice, AcidVoice::Decay) == decay0);
  // (When H4 gives HOME the shared latch, this is the test that changes: DECAY would be the latched key here.)
}

// ---- What leaving does to the live actions -------------------------------------------------------------------------------------------------------

TEST_CASE("Params ends the momentary live actions held on HOME or PERFORM and leaves the latched ones on (D4)") {
  Hands t;
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
  t.press(Button::Fx);  // FREEZE, held on HOME
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(1.0f));
  t.tap(Button::Params);  // the screen changes: the hold ends
  t.wait(20);
  CHECK(on_instrument(t));
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
  t.release(Button::Fx);
  t.to_home();

  t.to_perform();  // held on PERFORM's own pad
  t.down(app::pad_at(0, perform_action_pad(PerfAction::Freeze)));
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(1.0f));
  t.tap(Button::Params);
  t.wait(20);
  CHECK(on_instrument(t));
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
  t.up(app::pad_at(0, perform_action_pad(PerfAction::Freeze)));
  t.to_home();

  t.shift_tap(Button::Fx);  // latched: it stays on across the change, and Shift + Fx here puts it out
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(1.0f));
  t.tap(Button::Params);
  t.wait(20);
  CHECK(on_instrument(t));
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(1.0f));
  t.shift_tap(Button::Fx);
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
}

// ---- The view logic ---------------------------------------------------------------------------------------------------------------------------

namespace {

Pattern rests(unsigned len) {
  Pattern p;
  clear(p);
  p.length = static_cast<std::uint8_t>(len);
  for (Step& s : p.steps) {
    s.flags = kStepRest;
  }
  return p;
}

void put(Pattern& p, unsigned i, unsigned note, std::uint8_t flags) {
  p.steps[i].note = static_cast<std::uint8_t>(note);
  p.steps[i].flags = flags;
}

}  // namespace

TEST_CASE("drawer cells: a note with its gate glyph, a tie, a rest, and only the steps inside the pattern") {
  Pattern p = rests(16);
  put(p, 0, 36, 0);                           // plain note
  put(p, 1, 48, kStepAccent | kStepSlide);    // both
  put(p, 2, 51, kStepAccent);
  put(p, 3, 39, kStepSlide);
  put(p, 4, 0, kStepTie);                     // a tie carries the note before it
  // step 5 stays a rest
  InstrumentView v;
  fill_cells(p, 0, v);
  CHECK(v.cells == 16);
  CHECK(std::strcmp(v.cell[0].note, "C2") == 0);
  CHECK(std::strcmp(v.cell[0].gate, "-") == 0);
  CHECK(std::strcmp(v.cell[1].note, "C3") == 0);
  CHECK(std::strcmp(v.cell[1].gate, "AS") == 0);
  CHECK(std::strcmp(v.cell[2].note, "D#3") == 0);
  CHECK(std::strcmp(v.cell[2].gate, "A") == 0);
  CHECK(std::strcmp(v.cell[3].note, "D#2") == 0);
  CHECK(std::strcmp(v.cell[3].gate, "S") == 0);
  CHECK(std::strcmp(v.cell[4].note, "~") == 0);
  CHECK(std::strcmp(v.cell[4].gate, "~") == 0);
  CHECK(std::strcmp(v.cell[5].note, "--") == 0);
  CHECK(std::strcmp(v.cell[5].gate, ".") == 0);

  Pattern shorter = rests(8);
  fill_cells(shorter, 0, v);
  CHECK(v.cells == 8);
  Pattern longer = rests(32);
  put(longer, 17, 43, 0);
  fill_cells(longer, 16, v);  // the second page of 16
  CHECK(v.cells == 16);
  CHECK(std::strcmp(v.cell[1].note, "G2") == 0);
  fill_cells(longer, 32, v);  // a page past the end: nothing
  CHECK(v.cells == 0);
}

TEST_CASE("the lock label names the held step 1-based, the value is the lock's and in Accent") {
  InstrumentView v;
  view::Knob kn;
  kn.norm = 0.2f;
  apply_lock(v, kn, 4, "1404ms", 0.7f);
  CHECK(std::strcmp(kn.label, "LOCK 5") == 0);
  CHECK(std::strcmp(kn.value, "1404ms") == 0);
  CHECK(std::strcmp(kn.unit, "") == 0);
  CHECK(kn.norm == doctest::Approx(0.7f));
  CHECK(kn.locked);
  apply_lock(v, kn, 15, "a value text much too long for the box", 0.0f);
  CHECK(std::strcmp(kn.label, "LOCK 16") == 0);
  CHECK(std::strlen(kn.value) < sizeof kn.value);
}

TEST_CASE("read-out sections: the sections each page shows, their values as the knob row writes them, the wobble ON or OFF") {
  Hands t;
  Wiring w;
  w.voice = kVoice;
  w.dist = kDist;
  w.delay = kDelay;
  w.growl = kGrowl;
  w.bank = &bank();
  Controls c;
  c.init(w);
  dsp::Engine& e = t.h->engine();
  c.set(e, Entry{Item::DistMode, 0, nullptr}, 2.0f);  // TUBE
  c.set(e, Entry{Item::Voice, AcidVoice::Cutoff, nullptr}, 1000.0f);
  t.wait(5);

  InstrumentView v;
  fill_sections(e, c, false, kInstVoice, v);  // VOICE draws its own knobs large: the others are read-outs
  REQUIRE(v.sections == 3);
  CHECK(std::strcmp(v.section[0].title, "DISTORTION") == 0);
  CHECK(std::strcmp(v.section[1].title, "DELAY") == 0);
  CHECK(std::strcmp(v.section[2].title, "MODS") == 0);
  CHECK(std::strcmp(v.section[0].label[0], "MODEL") == 0);
  CHECK(std::strcmp(v.section[0].value[0], "TUBE") == 0);
  CHECK(std::strcmp(v.section[1].label[2], "SEND") == 0);
  CHECK(std::strcmp(v.section[1].value[2], "30%") == 0);  // the base send, 30 % by default
  CHECK(std::strcmp(v.section[2].label[3], "WOB") == 0);
  CHECK(std::strcmp(v.section[2].value[3], "OFF") == 0);

  fill_sections(e, c, true, kInstVoice, v);
  CHECK(std::strcmp(v.section[2].value[3], "ON") == 0);

  fill_sections(e, c, false, kInstFx, v);  // FX draws the distortion and the delay large
  REQUIRE(v.sections == 2);
  CHECK(std::strcmp(v.section[0].title, "VOICE") == 0);
  CHECK(std::strcmp(v.section[1].title, "MODS") == 0);
  CHECK(std::strcmp(v.section[0].label[0], "CUTOFF") == 0);
  char text[24];
  (void)c.format(e, Entry{Item::Voice, AcidVoice::Cutoff, nullptr}, text, sizeof text);
  std::strcat(text, c.unit_text(e, Entry{Item::Voice, AcidVoice::Cutoff, nullptr}));
  CHECK(std::strcmp(v.section[0].value[0], text) == 0);
  CHECK(std::strlen(v.section[0].value[0]) <= 11);

  fill_sections(e, c, false, kInstMods, v);
  REQUIRE(v.sections == 3);
  CHECK(std::strcmp(v.section[0].title, "VOICE") == 0);
  CHECK(std::strcmp(v.section[1].title, "DISTORTION") == 0);
  CHECK(std::strcmp(v.section[2].title, "DELAY") == 0);

  fill_sections(e, c, false, kInstMore, v);
  CHECK(v.sections == 3);
  fill_sections(e, c, false, kInstDeep, v);
  CHECK(v.sections == 3);
  fill_sections(e, c, false, kInstrumentPageCount, v);  // a page that does not exist: no sections
  CHECK(v.sections == 0);
}
