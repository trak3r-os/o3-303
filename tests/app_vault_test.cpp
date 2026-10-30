// SPDX-License-Identifier: GPL-3.0-or-later
// The GEN vault end to end: opening and loading a slot, the display list with all 512 slots, the headroom of the presets, the screen following the playing pattern.
// (Split from the former o3_303_app_test.cpp; the hands are in app_hands.h.)
#include "app_hands.h"

#include <o3_303/seq/gen.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/distortion/distortion.h>

#include <cstring>
#include <utility>

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
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(v.voice[0]).epsilon(0.001));
  CHECK(t.param(kVoice, AcidVoice::Reso) == doctest::Approx(v.voice[1]).epsilon(0.001));
  CHECK(t.param(kDist, Distortion::Drive) == doctest::Approx(v.dist[2]).epsilon(0.001));
  CHECK(t.param(kVoice, AcidVoice::Volume) == doctest::Approx(0.0f));  // the level is not part of a preset

  // GEN: a new seed gives a take that differs; PARAMS says what a load takes.
  const float cutoff = t.param(kVoice, AcidVoice::Cutoff);
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
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(cutoff));  // the sound was not loaded

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
  t.tap(Button::File);
  REQUIRE(std::strcmp(t.title(), "VAULT") == 0);
  CHECK(t.selected() == 0);
  t.wait(2400);  // A01 ends: A02 starts
  CHECK(std::strcmp(t.title(), "VAULT") == 0);
  CHECK(t.selected() == 1);
  t.tap(Button::Play);
}
