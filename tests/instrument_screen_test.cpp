// SPDX-License-Identifier: GPL-3.0-or-later
// INSTRUMENT (docs/UX-303.md 5.5, slice I1): the Params button (opens VOICE from any screen but the vault, again cycles the pages), the latches (VOICE shares
// the SOUND latch with PERFORM, the other pages keep their own), the keys of every page reaching their parameter, the home map's pads, Rec, colour buttons
// and locks forwarded to the one HomeMap HOME has, the Shift row (MIDI learn, C1), and the display list. The pure view logic is the screen's own; the scenario is
// instrument.txt. The view logic (read-outs, drawer cells, the lock label) is tested on its own functions.
#include <doctest/doctest.h>

#include "hands.h"

#include <o3sdk/app/pad_leds.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/app_access.h>
#include <o3_303/pattern.h>
#include <o3_303/perform.h>
#include <o3_303/seq/ops.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/growl/growl.h>

#include "../app/src/home_map.h"
#include "../app/src/instrument_screen.h"
#include "../app/src/main_screen.h"

#include <cstring>
#include <string>

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

TEST_CASE("Params opens INSTRUMENT on VOICE from any screen, again it cycles VOICE FX MODS and wraps") {
  Hands t;
  CHECK_FALSE(on_instrument(t));
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT VOICE"));
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT FX"));
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT MODS"));
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT VOICE"));  // the temporary DEEP page is Config's now (C1)
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

TEST_CASE("Params is INSTRUMENT from FILE and from the library, and File opens FILE PROJECTS from INSTRUMENT") {
  Hands t;
  t.tap(Button::File);
  CHECK(is_title(t, "FILE PROJECTS"));
  t.tap(Button::Params);
  CHECK(is_title(t, "INSTRUMENT VOICE"));
  t.tap(Button::File);
  t.tap(Button::File);
  CHECK(is_title(t, "FILE LIBRARY"));
  t.tap(Button::Params);  // no longer the library's key
  CHECK(is_title(t, "INSTRUMENT VOICE"));
  t.tap(Button::Pattern);
  CHECK_FALSE(on_instrument(t));
  t.to_instrument(kInstFx);
  t.tap(Button::File);
  CHECK(is_title(t, "FILE PROJECTS"));
}

TEST_CASE("VOICE shares the SOUND latch with PERFORM; FX and MODS each keep their own") {
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
  t.tap(Button::Params);  // VOICE
  t.tap(Button::Params);  // FX again: DLY FB is still the latched key
  const float fb0 = t.param(kDelay, units::Delay::Feedback);
  t.turn(-5);
  CHECK(t.param(kDelay, units::Delay::Feedback) != fb0);
}

TEST_CASE("every key of FX and MODS reaches its parameter") {
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
}

TEST_CASE("every label of the pages' knobs fits the knob strip's nine characters (U3: WOB TARGET showed as WOB TARGE)") {
  Hands t;
  for (unsigned page = kInstFx; page < kInstrumentPageCount; ++page) {
    t.to_instrument(page);
    const InstrumentScreen& screen = *static_cast<InstrumentScreen*>(t.h->runtime().context().screen());
    const Page& pg = screen.page_table();
    for (unsigned k = 0; k < kKeys; ++k) {
      if (pg.entry[k].item != Item::Blank && pg.entry[k].label != nullptr) {
        INFO(pg.name << " F" << (k + 1) << " " << pg.entry[k].label);
        CHECK(std::strlen(pg.entry[k].label) <= 9);  // the strip: (box 94 - 22) / 8 = 9 characters; the large box has room for ten
      }
    }
    t.to_home();
  }
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
  // A key no lane can drive moves the knob and writes no motion (FX's MODEL).
  t.tap(Button::F4);
  const float type0 = t.param(kDist, Distortion::Type);
  t.down(step_pad_of(5));
  t.turn(2);
  t.up(step_pad_of(5));
  CHECK(t.param(kDist, Distortion::Type) != type0);
  CHECK_FALSE(seq::lane_has_motion(t.pat(), 1));
}

TEST_CASE("the colour buttons are the drawer's rows here, not live actions: nothing freezes, and Params gives the encoder back to the knob") {
  Hands t;
  HomeMap& home = static_cast<MainScreen*>(t.h->runtime().context().screen())->home_map();
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
  t.to_instrument(kInstMods);
  t.press(Button::Fx);  // FREEZE on PERFORM; here the LOCK row
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
  CHECK(home.row() == DrawerRow::Lock);
  t.release(Button::Fx);
  t.tap(Button::Vol);
  CHECK(home.row() == DrawerRow::Plays);
  t.tap(Button::Params);  // the page has its own knobs: the encoder is the knob's again
  CHECK(home.row() == DrawerRow::None);
  t.tap(Button::Instr);
  CHECK(home.row() == DrawerRow::Gate);
  t.tap(Button::F1);  // an F key gives it back
  CHECK(home.row() == DrawerRow::None);
  t.tap(Button::Note);
  t.shift_tap(Button::Vol);  // Shift + a colour button is the button alone (D22)
  CHECK(home.row() == DrawerRow::Plays);
  t.tap(Button::Pattern);  // leaving: HOME's encoder is the knob's
  CHECK(home.row() == DrawerRow::None);
}

TEST_CASE("the Shift row is MIDI learn: Shift + F arms the knob of a unit parameter, again cancels, the next controller binds it") {
  platform::host::FakeMidi midi;
  Hands t(midi);
  app::Context& ctx = t.h->runtime().context();
  t.to_instrument(kInstVoice);
  const Pattern before = t.pat();
  const float cutoff0 = t.param(kVoice, AcidVoice::Cutoff);
  CHECK_FALSE(ctx.midi().learning());
  t.shift_tap(Button::F1);  // CUTOFF
  CHECK(ctx.midi().learning());
  t.shift_tap(Button::F1);  // again: cancelled
  CHECK_FALSE(ctx.midi().learning());
  t.shift_tap(Button::F1);
  CHECK(ctx.midi().learning());
  midi.in(platform::host::FakeMidi::Port::Usb, 0xB0, 74, 0x7F);
  t.wait(30);
  CHECK_FALSE(ctx.midi().learning());  // bound: the kit's strip says LEARNED CC 74
  CHECK(ctx.midi().bindings() == 1u);
  CHECK(ctx.midi().learned_cc() == 74);
  midi.in(platform::host::FakeMidi::Port::Usb, 0xB0, 74, 0);  // the knob is swept: it picks the value up on the way
  t.wait(30);
  midi.in(platform::host::FakeMidi::Port::Usb, 0xB0, 74, 0x7F);
  t.wait(30);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) > cutoff0);
  // Shift never does what the page's key does: the latch stayed, the pattern is as it was.
  CHECK(t.pat().steps[0].note == before.steps[0].note);
  CHECK(t.pat().steps[0].flags == before.steps[0].flags);
  t.tap(Button::F2);
  const float reso0 = t.param(kVoice, AcidVoice::Reso);
  t.turn(5);
  CHECK(t.param(kVoice, AcidVoice::Reso) != reso0);
}

TEST_CASE("MIDI learn arms only unit parameters: VOLUME, DLY SEND, MODEL and the WOB knobs show '-' and cannot be armed; a page change or leaving cancels") {
  platform::host::FakeMidi midi;
  Hands t(midi);
  app::Context& ctx = t.h->runtime().context();
  const auto row = [&](unsigned k) {
    InstrumentScreen& screen = *static_cast<InstrumentScreen*>(ctx.screen());
    app::SoftKeys keys;
    screen.soft_keys(ctx, keys);
    return std::string(keys.label[k] != nullptr ? keys.label[k] : "");
  };
  // Without Shift the row is empty.
  t.to_instrument(kInstVoice);
  CHECK(row(0).empty());
  t.press(Button::Shift);
  for (unsigned k = 0; k < 7; ++k) {
    CHECK(row(k) == "LEARN");
  }
  CHECK(row(7) == "-");  // VOLUME is the level, an app value
  t.release(Button::Shift);
  t.shift_tap(Button::F8);
  CHECK_FALSE(ctx.midi().learning());

  t.tap(Button::Params);  // FX: DRIVE TONE MIX | MODEL | DLY TIME DLY FB | DLY SEND | DLY TONE
  t.press(Button::Shift);
  const char* fx[8] = {"LEARN", "LEARN", "LEARN", "-", "LEARN", "LEARN", "-", "LEARN"};
  for (unsigned k = 0; k < 8; ++k) {
    INFO("FX F" << (k + 1));
    CHECK(row(k) == fx[k]);
  }
  t.release(Button::Shift);
  t.shift_tap(Button::F4);
  CHECK_FALSE(ctx.midi().learning());
  t.shift_tap(Button::F7);
  CHECK_FALSE(ctx.midi().learning());
  t.shift_tap(Button::F5);  // DLY TIME: armed ...
  CHECK(ctx.midi().learning());
  t.tap(Button::Params);    // ... and the page change drops it
  t.wait(5);
  CHECK_FALSE(ctx.midi().learning());

  CHECK(is_title(t, "INSTRUMENT MODS"));
  t.press(Button::Shift);
  const char* mods[8] = {"LEARN", "LEARN", "LEARN", "LEARN", "-", "-", "-", "-"};
  for (unsigned k = 0; k < 8; ++k) {
    INFO("MODS F" << (k + 1));
    CHECK(row(k) == mods[k]);
  }
  t.release(Button::Shift);
  for (unsigned k = 4; k < 8; ++k) {
    t.shift_tap(app::fkey(k));
    CHECK_FALSE(ctx.midi().learning());
  }
  t.shift_tap(Button::F1);  // VOWEL: armed, and leaving the screen drops it
  CHECK(ctx.midi().learning());
  t.to_home();
  CHECK_FALSE(ctx.midi().learning());
  CHECK(ctx.midi().bindings() == 0u);
}

TEST_CASE("learn_target: the unit parameters of the pages, and nothing for the app's values") {
  Wiring w;
  w.voice = 0;
  w.dist = 1;
  w.delay = 2;
  w.freeze = 3;
  w.growl = 4;
  dsp::UnitId unit = dsp::kNoUnit;
  std::uint16_t param = 99;
  CHECK(learn_target(w, Entry{Item::Voice, AcidVoice::Reso, nullptr}, unit, param));
  CHECK(unit == 0);
  CHECK(param == AcidVoice::Reso);
  CHECK(learn_target(w, Entry{Item::Dist, Distortion::Tone, nullptr}, unit, param));
  CHECK(unit == 1);
  CHECK(learn_target(w, Entry{Item::Delay, units::Delay::Tone, nullptr}, unit, param));
  CHECK(unit == 2);
  CHECK(learn_target(w, Entry{Item::Growl, Growl::Vowel, nullptr}, unit, param));
  CHECK(unit == 4);
  for (const Item item : {Item::Blank, Item::Level, Item::SendA, Item::DistMode, Item::Wobble, Item::Tempo, Item::Master, Item::Perf, Item::FillVariant}) {
    CHECK_FALSE(learn_target(w, Entry{item, 0, nullptr}, unit, param));
  }
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

TEST_CASE("VOICE's latch is HOME's and PERFORM's: one SOUND latch on Rig::sound_keys (D3)") {
  Hands t;
  t.tap(Button::F4);  // DECAY latched on the 303 screen (HOME has no latch of its own since H4)
  t.to_instrument(kInstVoice);
  const float decay0 = t.param(kVoice, AcidVoice::Decay);
  const float cutoff0 = t.param(kVoice, AcidVoice::Cutoff);
  t.turn(5);  // VOICE has Rig::sound_keys: F4 DECAY, as HOME and PERFORM have it
  CHECK(t.param(kVoice, AcidVoice::Decay) != decay0);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == cutoff0);
}

// ---- What leaving does to the live actions -------------------------------------------------------------------------------------------------------

TEST_CASE("Params ends the momentary live actions held on PERFORM and leaves the latched ones on (D4)") {
  Hands t;
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
  t.to_perform();
  t.press(Button::Fx);  // FREEZE, held on PERFORM
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(1.0f));
  t.tap(Button::Params);  // the screen changes: the hold ends
  t.wait(20);
  CHECK(on_instrument(t));
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
  t.release(Button::Fx);

  t.to_perform();  // held on PERFORM's own pad
  t.down(app::pad_at(0, perform_action_pad(PerfAction::Freeze)));
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(1.0f));
  t.tap(Button::Params);
  t.wait(20);
  CHECK(on_instrument(t));
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(0.0f));
  t.up(app::pad_at(0, perform_action_pad(PerfAction::Freeze)));

  t.to_perform();
  t.shift_tap(Button::Fx);  // latched: it stays on across the change; here Shift + Fx is the LOCK row, so PERFORM puts it out
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(1.0f));
  t.tap(Button::Params);
  t.wait(20);
  CHECK(on_instrument(t));
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(1.0f));
  t.shift_tap(Button::Fx);
  t.wait(20);
  CHECK(t.param(kFreeze, Freeze::Active) == doctest::Approx(1.0f));
  t.to_perform();
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

  fill_sections(e, c, false, kInstrumentPageCount, v);  // a page that does not exist: no sections
  CHECK(v.sections == 0);
}
