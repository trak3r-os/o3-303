// SPDX-License-Identifier: GPL-3.0-or-later
// FILE LIBRARY (the vault, UX-303 5.9) end to end: opening it with File twice, loading a slot with a pad or INSERT, its Shift row, the display list with all 512 slots,
// the headroom of the presets, the screen following the playing pattern. (Was app_vault_test.cpp; the hands are in app_hands.h.)
#include "app_hands.h"

#include "../app/src/vault_screen.h"

#include <o3_303/seq/gen.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/distortion/distortion.h>

#include <cstring>
#include <utility>

// FILE LIBRARY is File's second press: PROJECTS first.
void to_library(Hands& t) {
  t.tap(Button::File);
  t.tap(Button::File);
}

TEST_CASE("File opens FILE PROJECTS, again FILE LIBRARY, again PROJECTS; Pattern goes home") {
  Hands t;
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "FILE PROJECTS") == 0);
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "FILE LIBRARY") == 0);
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "FILE PROJECTS") == 0);
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "FILE LIBRARY") == 0);
  t.tap(Button::Pattern);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);  // HOME
  t.tap(Button::File);  // from HOME: PROJECTS again (File opens PROJECTS from every screen)
  CHECK(std::strcmp(t.title(), "FILE PROJECTS") == 0);
  t.to_instrument();
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "FILE PROJECTS") == 0);
}

TEST_CASE("the library: INSERT loads the slot shown into the pattern and the sound and stays; a pad loads its slot") {
  Hands t;
  seq::gen::Preset slot;
  to_library(t);
  t.tap(Button::F4);  // bank D: ACID
  for (unsigned i = 0; i < 17; ++i) {
    t.turn(1);  // slot 17
  }
  seq::gen::factory(slot, 3, 17);
  const seq::gen::ParamValues v = seq::gen::to_values(slot.patch);
  const std::uint32_t undo_before = bank().version(0);
  t.tap(Button::Insert);
  CHECK(std::strcmp(t.title(), "FILE LIBRARY") == 0);  // a load leaves you here
  CHECK(bank().version(0) != undo_before);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == slot.pattern.steps[i].note);
    CHECK(t.pat().steps[i].flags == slot.pattern.steps[i].flags);
  }
  CHECK(t.h->engine().tempo() == v.tempo_mbpm);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(v.voice[0]).epsilon(0.001));
  CHECK(t.param(kVoice, AcidVoice::Reso) == doctest::Approx(v.voice[1]).epsilon(0.001));
  CHECK(t.param(kDist, Distortion::Drive) == doctest::Approx(v.dist[2]).epsilon(0.001));
  CHECK(t.param(kVoice, AcidVoice::Volume) == doctest::Approx(0.0f));  // the level is not part of a preset

  // A pad is a slot of the bank (0..47) and loads it.
  seq::gen::factory(slot, 3, 40);
  t.pad(40);
  CHECK(std::strcmp(t.title(), "FILE LIBRARY") == 0);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == slot.pattern.steps[i].note);
  }
  // Slots 48..63 have no pad: the encoder reaches them and INSERT loads them.
  t.turn(12);  // 40 + 12 = 52
  seq::gen::factory(slot, 3, 52);
  t.tap(Button::Insert);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == slot.pattern.steps[i].note);
  }
  // Arrows: LEFT / RIGHT one slot, UP / DOWN a row of eight.
  t.tap(Button::Down);
  t.tap(Button::Left);
  t.tap(Button::Up);
  t.tap(Button::Right);
  t.tap(Button::Insert);  // 52 + 8 - 1 - 8 + 1 = 52 again
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == slot.pattern.steps[i].note);
  }
}

TEST_CASE("the library's Shift row: F1 NEW SEED, F2 LOAD what (BOTH, LINE, SOUND); Params and Rec are no longer its keys") {
  Hands t;
  to_library(t);
  t.pad(2);  // a slot with a line of its own
  const float cutoff = t.param(kVoice, AcidVoice::Cutoff);
  const Pattern loaded = t.pat();

  // Shift + F1: another take of the slot (a new seed), then a LINE-only load leaves the sound alone.
  t.shift_tap(Button::F1);
  t.shift_tap(Button::F2);  // LINE
  t.tap(Button::Insert);
  bool pattern_changed = false;
  for (unsigned i = 0; i < 16; ++i) {
    pattern_changed = pattern_changed || t.pat().steps[i].note != loaded.steps[i].note || t.pat().steps[i].flags != loaded.steps[i].flags;
  }
  CHECK(pattern_changed);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(cutoff));  // the sound was not loaded
  CHECK(std::strcmp(t.title(), "FILE LIBRARY") == 0);

  // Params leaves for INSTRUMENT (it is no longer the library's), and Rec does nothing there.
  to_library(t);
  const unsigned bank_before = t.pat().steps[0].note;
  t.tap(Button::Rec);
  CHECK(std::strcmp(t.title(), "FILE LIBRARY") == 0);
  CHECK(t.pat().steps[0].note == bank_before);
  t.tap(Button::Params);
  CHECK(std::strncmp(t.title(), "INSTRUMENT", 10) == 0);

  // Pattern goes back without loading.
  to_library(t);
  const Pattern kept = t.pat();
  t.tap(Button::Right);
  t.tap(Button::Pattern);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(t.pat().steps[i].note == kept.steps[i].note);
  }
}

TEST_CASE("the library's pure texts") {
  char text[96];
  (void)library_pads_text(0, text, sizeof text);
  CHECK(std::strncmp(text, "SLOTS 0-47 OF AMBER . TAP = LOAD", 32) == 0);
  (void)library_pads_text(6, text, sizeof text);
  CHECK(std::strncmp(text, "SLOTS 0-47 OF OFF WORLD", 23) == 0);
  CHECK(std::strcmp(load_what_name(LoadWhat::Both), "BOTH") == 0);
  CHECK(std::strcmp(load_what_name(LoadWhat::Pattern), "LINE") == 0);
  CHECK(std::strcmp(load_what_name(LoadWhat::Sound), "SOUND") == 0);
  char tiny[8];
  CHECK(library_pads_text(1, tiny, sizeof tiny) == 7);  // cut to the buffer, NUL ended
}

TEST_CASE("the vault draws every one of its 512 slots, and a GEN take of each, inside the display list") {
  Hands t;
  to_library(t);
  REQUIRE(std::strcmp(t.title(), "FILE LIBRARY") == 0);
  const Button banks[8] = {Button::F1, Button::F2, Button::F3, Button::F4, Button::F5, Button::F6, Button::F7, Button::F8};
  for (unsigned b = 0; b < 8; ++b) {
    t.tap(banks[b], 5);
    for (unsigned s = 0; s < 64; ++s) {
      t.turn(1);
      t.wait(36);
      if (s % 8 == 7) {
        t.shift_tap(Button::F1);  // a take with another seed every few slots
        t.wait(36);
      }
    }
  }
  CHECK(t.h->runtime().frames() > 300u);
  CHECK(t.h->runtime().frame_failures() == 0u);
  CHECK(t.h->display().render_failures() == 0u);
  CHECK(t.h->display().peak_frame_words() < 1900u);
}

// Loads vault slot (bank, slot) the way a hand does, plays it from the top and returns the master's peak before the clipper. What it
// loaded is checked against the generator (the distortion's Active, Type and Drive), so that a guard built on a slot cannot drift onto
// another preset unseen.
float vault_peak(Hands& t, unsigned bank_index, unsigned slot_index, unsigned& at_slot) {
  static const Button banks[8] = {Button::F1, Button::F2, Button::F3, Button::F4, Button::F5, Button::F6, Button::F7, Button::F8};
  seq::gen::Preset want;
  seq::gen::factory(want, bank_index, slot_index);
  const seq::gen::ParamValues v = seq::gen::to_values(want.patch);
  to_library(t);
  t.tap(banks[bank_index], 5);
  t.turn(static_cast<int>(slot_index) - static_cast<int>(at_slot));
  at_slot = slot_index;
  t.tap(Button::Insert);  // load
  t.tap(Button::Pattern);  // back to the 303 screen
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

TEST_CASE("the screen keeps following the playing pattern while the vault is up") {
  Hands t;
  t.duplicate_next();  // A02, a copy of A01
  t.select_pattern(0);
  t.tap(Button::Play);
  t.wait(300);
  t.press(Button::Pattern);
  t.wait(350);
  t.pad(1);
  t.release(Button::Pattern);
  to_library(t);
  REQUIRE(std::strcmp(t.title(), "FILE LIBRARY") == 0);
  CHECK(t.selected() == 0);
  t.wait(2400);  // A01 ends: A02 starts
  CHECK(std::strcmp(t.title(), "FILE LIBRARY") == 0);
  CHECK(t.selected() == 1);
  t.tap(Button::Play);
}
