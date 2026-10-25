// SPDX-License-Identifier: GPL-3.0-or-later
// The pages of F1..F8 (app/include/o3_303/pages.h): which entry is which value, and how each formats, steps and clamps, against a real
// engine with the voice, the distortion and the delay attached as the app does.
#include <doctest/doctest.h>

#include <o3sdk/dsp/engine.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/pages.h>
#include <o3_303/seq/bank.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/tb303/tb303.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

using namespace o3_303;
using namespace o3sdk;

namespace {

struct Rig {
  static constexpr std::uint32_t kCap = 4096;
  float l[kCap] = {};
  float r[kCap] = {};
  Tb303 voice;
  Distortion dist;
  units::Delay delay{l, r, kCap};
  dsp::Engine engine;
  std::unique_ptr<seq::Bank> bank = std::make_unique<seq::Bank>();
  Wiring wiring;
  Controls controls;

  Rig() {
    wiring.voice = engine.attach(voice);
    wiring.dist = engine.attach(dist);
    wiring.delay = engine.attach(delay);
    (void)engine.set_source(0, wiring.voice);
    (void)engine.set_insert(0, 0, wiring.dist);
    (void)engine.set_send_effect(0, wiring.delay);
    bank->init();
    wiring.bank = bank.get();
    controls.init(wiring);
  }

  Entry entry(unsigned page, unsigned key) const { return page_at(page).entry[key]; }
  std::string text(const Entry& e) const {
    char buf[24];
    (void)controls.format(engine, e, buf, sizeof buf);
    return std::string(buf) + controls.unit_text(engine, e);
  }
};

}  // namespace

TEST_CASE("pages: the three pages and their keys (DESIGN-303 section 3)") {
  Rig rig;
  CHECK(kPageCount == 3);
  CHECK(std::strcmp(page_at(kPageSound).name, "SOUND") == 0);
  CHECK(std::strcmp(page_at(kPageFx).name, "FX") == 0);
  CHECK(std::strcmp(page_at(kPageSeq).name, "SEQ") == 0);
  // Every key has a descriptor and a label that fits the F-key row (eight characters between two boxes).
  for (unsigned p = 0; p < kPageCount; ++p) {
    for (unsigned k = 0; k < kKeys; ++k) {
      const Entry e = rig.entry(p, k);
      CHECK(e.item != Item::Blank);
      CHECK(rig.controls.desc(rig.engine, e) != nullptr);
      CHECK(std::strlen(rig.controls.label(rig.engine, e)) > 0);
      CHECK(std::strlen(rig.controls.label(rig.engine, e)) <= 8);
    }
  }
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kPageSound, 0)), "CUTOFF") == 0);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kPageSound, 7)), "VOLUME") == 0);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kPageFx, 2)), "DLY TIME") == 0);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kPageFx, 5)), "DIST MIX") == 0);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kPageFx, 6)), "DIST") == 0);
  CHECK(std::strcmp(page_at(99).name, "SOUND") == 0);  // an index past the pages gives the first
}

TEST_CASE("pages: only the voice's own parameters are motion lanes") {
  Rig rig;
  unsigned lane = 99;
  for (unsigned k = 0; k < 7; ++k) {
    REQUIRE(rig.controls.lane(rig.entry(kPageSound, k), lane));
    CHECK(lane == k);  // the lane is the parameter id
  }
  CHECK_FALSE(rig.controls.lane(rig.entry(kPageSound, 7), lane));  // VOLUME is the slot's level, not a lane
  for (unsigned k = 0; k < kKeys; ++k) {
    CHECK_FALSE(rig.controls.lane(rig.entry(kPageFx, k), lane));
    CHECK_FALSE(rig.controls.lane(rig.entry(kPageSeq, k), lane));
  }
}

TEST_CASE("pages: the SOUND page goes through the engine to the voice") {
  Rig rig;
  const Entry cutoff = rig.entry(kPageSound, 0);
  const float before = rig.controls.get(rig.engine, cutoff);
  CHECK(before == doctest::Approx(350.0f));
  rig.controls.step(rig.engine, cutoff, 5, false);
  CHECK(rig.controls.get(rig.engine, cutoff) > before);
  CHECK(rig.engine.param(rig.wiring.voice, Tb303::Cutoff) == rig.controls.get(rig.engine, cutoff));
  rig.controls.set(rig.engine, cutoff, 1.0e9f);  // clamped by the descriptor
  CHECK(rig.controls.get(rig.engine, cutoff) == doctest::Approx(4500.0f));
  const Entry wave = rig.entry(kPageSound, 5);
  CHECK(rig.text(wave) == "SAW");
  rig.controls.step(rig.engine, wave, 1, false);
  CHECK(rig.text(wave) == "SQR");
  rig.controls.step(rig.engine, wave, 1, false);  // stays at the end
  CHECK(rig.text(wave) == "SQR");
}

TEST_CASE("pages: VOLUME is slot 0's level in dB, silent at the bottom, capped at the engine's 1.5") {
  Rig rig;
  const Entry vol = rig.entry(kPageSound, 7);
  CHECK(rig.text(vol) == "-6.0dB");  // the default: headroom for the delay's wet signal and the accents
  rig.controls.set(rig.engine, vol, 0.0f);
  CHECK(rig.engine.mix(0, dsp::MixField::Level) == doctest::Approx(1.0f).epsilon(0.001));
  rig.controls.set(rig.engine, vol, -6.0f);
  CHECK(rig.engine.mix(0, dsp::MixField::Level) == doctest::Approx(std::pow(10.0f, -6.0f / 20.0f)).epsilon(0.001));
  rig.controls.set(rig.engine, vol, -60.0f);
  CHECK(rig.engine.mix(0, dsp::MixField::Level) == 0.0f);
  rig.controls.set(rig.engine, vol, 99.0f);
  CHECK(rig.controls.get(rig.engine, vol) == doctest::Approx(3.5f));
  CHECK(rig.engine.mix(0, dsp::MixField::Level) <= 1.5f);
  CHECK(rig.engine.mix(0, dsp::MixField::Level) > 1.49f);
  // The voice's own volume is untouched by the page.
  CHECK(rig.engine.param(rig.wiring.voice, Tb303::Volume) == doctest::Approx(-3.0f));
}

TEST_CASE("pages: the FX page: the distortion, the delay, the send and the master") {
  Rig rig;
  const Entry drive = rig.entry(kPageFx, 0);
  rig.controls.set(rig.engine, drive, 0.8f);
  CHECK(rig.engine.param(rig.wiring.dist, Distortion::Drive) == doctest::Approx(0.8f));
  CHECK(rig.text(drive) == "80%");
  // DIST: the insert's Active and Type as one key, OFF then the five models.
  const Entry mode = rig.entry(kPageFx, 6);
  CHECK(rig.text(mode) == "GRIT");  // active, GRIT
  rig.controls.step(rig.engine, mode, 2, false);
  CHECK(rig.text(mode) == "HARD");
  CHECK(rig.engine.param(rig.wiring.dist, Distortion::Active) == doctest::Approx(1.0f));
  CHECK(rig.engine.param(rig.wiring.dist, Distortion::Type) == doctest::Approx(2.0f));
  rig.controls.step(rig.engine, mode, -5, false);
  CHECK(rig.text(mode) == "OFF");
  CHECK(rig.engine.param(rig.wiring.dist, Distortion::Active) == doctest::Approx(0.0f));
  CHECK(rig.engine.param(rig.wiring.dist, Distortion::Type) == doctest::Approx(2.0f));  // the model is remembered
  rig.controls.step(rig.engine, mode, 1, false);
  CHECK(rig.text(mode) == "GRIT");
  rig.controls.set(rig.engine, mode, 5.0f);
  CHECK(rig.text(mode) == "FOLD");
  CHECK(rig.engine.param(rig.wiring.dist, Distortion::Type) == doctest::Approx(4.0f));
  const Entry mix = rig.entry(kPageFx, 5);
  CHECK(rig.text(mix) == "85%");
  rig.controls.set(rig.engine, mix, 0.3f);
  CHECK(rig.engine.param(rig.wiring.dist, Distortion::Mix) == doctest::Approx(0.3f));
  const Entry time = rig.entry(kPageFx, 2);
  rig.controls.step(rig.engine, time, 1, false);
  CHECK(rig.engine.param(rig.wiring.delay, units::Delay::Time) == doctest::Approx(3.0f));

  const Entry send = rig.entry(kPageFx, 4);
  CHECK(rig.text(send) == "30%");
  rig.controls.apply_send(rig.engine);
  CHECK(rig.engine.mix(0, dsp::MixField::SendA) == doctest::Approx(0.3f));
  rig.controls.set(rig.engine, send, 60.0f);
  CHECK(rig.engine.mix(0, dsp::MixField::SendA) == doctest::Approx(0.6f));
  rig.wiring.throw_on = true;  // DELAY THROW opens the send to unity
  rig.controls.apply_send(rig.engine);
  CHECK(rig.engine.mix(0, dsp::MixField::SendA) == doctest::Approx(1.0f));
  rig.wiring.send_a_pct = 100.0f;
  rig.wiring.throw_on = false;
  rig.controls.apply_send(rig.engine);
  CHECK(rig.engine.mix(0, dsp::MixField::SendA) == doctest::Approx(1.0f));

  const Entry master = rig.entry(kPageFx, 7);
  CHECK(rig.text(master) == "100%");
  rig.controls.set(rig.engine, master, 150.0f);
  CHECK(rig.engine.master_level() == doctest::Approx(1.5f));
  rig.controls.set(rig.engine, master, 500.0f);
  CHECK(rig.engine.master_level() == doctest::Approx(1.5f));
}

TEST_CASE("pages: the SEQ page: the tempo and the fields of the selected pattern") {
  Rig rig;
  const Entry tempo = rig.entry(kPageSeq, 0);
  rig.controls.set(rig.engine, tempo, 140.0f);
  CHECK(rig.engine.tempo() == 140'000u);
  CHECK(rig.text(tempo) == "140.0BPM");
  rig.controls.step(rig.engine, tempo, 1, false);  // half a BPM a detent
  CHECK(rig.engine.tempo() == 140'500u);
  rig.controls.step(rig.engine, tempo, 1, true);   // ten detents a coarse step
  CHECK(rig.engine.tempo() == 145'500u);
  rig.controls.set(rig.engine, tempo, 5.0f);
  CHECK(rig.engine.tempo() == 20'000u);
  rig.controls.set(rig.engine, tempo, 999.0f);
  CHECK(rig.engine.tempo() == 300'000u);

  seq::Bank& bank = *rig.bank;
  const unsigned sel = bank.selected();
  rig.controls.set(rig.engine, rig.entry(kPageSeq, 1), 40.0f);
  CHECK(bank.pattern(sel).swing == 40);
  rig.controls.set(rig.engine, rig.entry(kPageSeq, 1), 90.0f);
  CHECK(bank.pattern(sel).swing == kMaxSwing);
  rig.controls.set(rig.engine, rig.entry(kPageSeq, 3), 75.0f);
  CHECK(bank.pattern(sel).gate == 75);
  rig.controls.set(rig.engine, rig.entry(kPageSeq, 4), -7.0f);
  CHECK(bank.pattern(sel).transpose == -7);
  CHECK(rig.text(rig.entry(kPageSeq, 4)) == "-7st");

  const Entry length = rig.entry(kPageSeq, 2);
  CHECK(rig.text(length) == "16");
  rig.controls.step(rig.engine, length, 1, false);
  CHECK(bank.pattern(sel).length == 32);
  CHECK(rig.text(length) == "32");
  rig.controls.step(rig.engine, length, 1, false);
  CHECK(bank.pattern(sel).length == 64);
  rig.controls.step(rig.engine, length, 5, false);
  CHECK(bank.pattern(sel).length == 64);
  rig.controls.step(rig.engine, length, -3, false);
  CHECK(bank.pattern(sel).length == 8);

  // The pages edit the selected pattern, whichever it is.
  bank.select(7);
  rig.controls.set(rig.engine, rig.entry(kPageSeq, 1), 12.0f);
  CHECK(bank.pattern(7).swing == 12);
  CHECK(bank.pattern(sel).swing == kMaxSwing);
}

TEST_CASE("pages: the generator's settings: scale, density and the accent / slide macro") {
  Rig rig;
  const Entry scale = rig.entry(kPageSeq, 5);
  CHECK(rig.wiring.gen.scale == 18);
  CHECK(rig.text(scale) == "Penta Min");
  rig.controls.set(rig.engine, scale, 3.0f);
  CHECK(rig.wiring.gen.scale == 3);
  CHECK(rig.text(scale) == "Major");
  rig.controls.set(rig.engine, scale, 100.0f);
  CHECK(rig.wiring.gen.scale == 39);

  const Entry density = rig.entry(kPageSeq, 6);
  rig.controls.set(rig.engine, density, 40.0f);
  CHECK(rig.wiring.gen.density == 40);

  const Entry acc = rig.entry(kPageSeq, 7);
  rig.controls.set(rig.engine, acc, 25.0f);
  CHECK(rig.text(acc) == "25/20%");  // the slide chance follows at four fifths of the accent chance
  rig.controls.set(rig.engine, acc, 100.0f);
  CHECK(rig.wiring.gen.accent_pct == 100);
  CHECK(rig.wiring.gen.slide_pct == 80);
  CHECK(rig.text(acc) == "100/80%");
  rig.controls.set(rig.engine, acc, 0.0f);
  CHECK(rig.text(acc) == "0/0%");
}

TEST_CASE("pages: a blank key has no value and does nothing") {
  Rig rig;
  const Entry blank{};  // no page has one now: the reverb's key (F6 of FX) is taken until W5
  CHECK(blank.item == Item::Blank);
  CHECK(rig.controls.desc(rig.engine, blank) == nullptr);
  CHECK(rig.controls.get(rig.engine, blank) == 0.0f);
  rig.controls.set(rig.engine, blank, 5.0f);
  rig.controls.step(rig.engine, blank, 3, true);
  char buf[8] = "x";
  CHECK(rig.controls.format(rig.engine, blank, buf, sizeof buf) == 0);
  CHECK(buf[0] == '\0');
}
