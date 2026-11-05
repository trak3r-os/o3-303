// SPDX-License-Identifier: GPL-3.0-or-later
// The pages of F1..F8 (app/include/o3_303/pages.h): which entry is which value, and how each formats, steps and clamps, against a real
// engine with the voice, the distortion and the delay attached as the app does.
#include <doctest/doctest.h>

#include <o3sdk/dsp/engine.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/pages.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/acid_voice/acid_voice.h>

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
  AcidVoice voice;
  Distortion dist;
  units::Delay delay{l, r, kCap};
  float fl[kCap] = {};
  float fr[kCap] = {};
  Freeze freeze{fl, fr, kCap};
  Growl growl;
  dsp::Engine engine;
  std::unique_ptr<seq::Bank> bank = std::make_unique<seq::Bank>();
  std::unique_ptr<seq::Sequencer> sequencer = std::make_unique<seq::Sequencer>(*bank);
  Wiring wiring;
  Controls controls;

  Rig() {
    wiring.voice = engine.attach(voice);
    wiring.dist = engine.attach(dist);
    wiring.delay = engine.attach(delay);
    wiring.freeze = engine.attach(freeze);
    wiring.growl = engine.attach(growl);
    (void)engine.set_source(0, wiring.voice);
    (void)engine.set_insert(0, 0, wiring.dist);
    (void)engine.set_insert(0, 1, wiring.growl);
    (void)engine.set_send_effect(0, wiring.delay);
    bank->init();
    wiring.bank = bank.get();
    wiring.seq = sequencer.get();
    controls.init(wiring);
  }

  // The page is one of the live tables (the global page_at() or pages.h's INSTRUMENT tables), never a copy.
  Entry entry(const Page& page, unsigned key) const { return page.entry[key]; }
  std::string text(const Entry& e) const {
    char buf[24];
    (void)controls.format(engine, e, buf, sizeof buf);
    return std::string(buf) + controls.unit_text(engine, e);
  }
};

}  // namespace

TEST_CASE("pages: the global table (SOUND, PERF SETUP) and the INSTRUMENT screen's tables (FX, NOTES, TONE, GROWL, WOBBLE) and their keys") {
  Rig rig;
  CHECK(kPageCount == 2);
  CHECK(kPageSound == 0);
  CHECK(kPagePerfSetup == 1);
  CHECK(std::strcmp(page_at(kPageSound).name, "SOUND") == 0);
  CHECK(std::strcmp(page_at(kPagePerfSetup).name, "PERF SETUP") == 0);
  CHECK(std::strcmp(kFxPage.name, "FX") == 0);
  CHECK(std::strcmp(kNotesPage.name, "NOTES") == 0);
  CHECK(std::strcmp(kTonePage.name, "TONE") == 0);
  CHECK(std::strcmp(kGrowlPage.name, "GROWL") == 0);
  CHECK(std::strcmp(kWobblePage.name, "WOBBLE") == 0);
  CHECK(&page_at(kPageCount) == &page_at(0));  // an index past the pages gives the first
  CHECK(std::strcmp(page_at(99).name, "SOUND") == 0);
  // Every key has a descriptor and a label that fits its row: eight characters between two boxes on the F-key row of HOME and PERFORM SETUP, nine in the
  // INSTRUMENT screen's knob strip (instrument_screen_test checks the same against what is drawn); the free keys are blank.
  struct Shown {
    const Page* page;
    unsigned blank_mask;  // the free keys
    std::size_t max_label;
  };
  const Shown shown[] = {{&page_at(kPageSound), 0, 8},  {&page_at(kPagePerfSetup), 0, 8}, {&kFxPage, 0, 9},
                         {&kNotesPage, 0, 9},           {&kTonePage, (1u << 5) | (1u << 6), 9},
                         {&kGrowlPage, 1u << 7, 9},     {&kWobblePage, (1u << 5) | (1u << 6), 9}};
  for (const Shown& sh : shown) {
    for (unsigned k = 0; k < kKeys; ++k) {
      const Entry e = rig.entry(*sh.page, k);
      if ((sh.blank_mask >> k) & 1u) {
        CHECK(e.item == Item::Blank);
        continue;
      }
      CHECK(e.item != Item::Blank);
      CHECK(rig.controls.desc(rig.engine, e) != nullptr);
      CHECK(std::strlen(rig.controls.label(rig.engine, e)) > 0);
      CHECK(std::strlen(rig.controls.label(rig.engine, e)) <= sh.max_label);
    }
  }
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(page_at(kPageSound), 0)), "CUTOFF") == 0);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(page_at(kPageSound), 7)), "VOLUME") == 0);
  // FX in the order the INSTRUMENT screen shows: DRIVE, TONE, MIX, MODEL, then the delay's TIME, FB, SEND and TONE.
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kFxPage, 2)), "MIX") == 0);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kFxPage, 3)), "MODEL") == 0);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kFxPage, 4)), "DLY TIME") == 0);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kFxPage, 5)), "DLY FB") == 0);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kFxPage, 6)), "DLY SEND") == 0);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kFxPage, 7)), "DLY TONE") == 0);
  // MODDED: F8 is the STOCK action key on both pages.
  CHECK(rig.entry(kNotesPage, 7).item == Item::Stock);
  CHECK(rig.entry(kTonePage, 7).item == Item::Stock);
}

TEST_CASE("pages: the entries a motion lane can drive are the voice's, the distortion's, the delay's, VOLUME and DLY SEND (C2)") {
  Rig rig;
  LaneRef ref{kLaneNone, 99};
  // The SOUND page: the voice's ids 0..6 and VOLUME, the mixer's level.
  for (unsigned k = 0; k < 7; ++k) {
    REQUIRE(rig.controls.lane_ref(rig.entry(page_at(kPageSound), k), ref));
    CHECK(ref.target == kLaneVoice);
    CHECK(ref.id == k);
  }
  REQUIRE(rig.controls.lane_ref(rig.entry(page_at(kPageSound), 7), ref));
  CHECK(ref.target == kLaneMixer);
  CHECK(ref.id == kMixerLevel);
  // The voice's mods are lanes like its knobs (ids 8..18); Stock, the switch over them (id 19), is not one, nor is an id past the knobs.
  for (unsigned id = AcidVoice::AccDecay; id < AcidVoice::Stock; ++id) {
    REQUIRE(rig.controls.lane_ref(Entry{Item::Voice, static_cast<std::uint16_t>(id), nullptr}, ref));
    CHECK(ref.target == kLaneVoice);
    CHECK(ref.id == id);
  }
  CHECK_FALSE(rig.controls.lane_ref(Entry{Item::Voice, AcidVoice::Stock, nullptr}, ref));
  CHECK_FALSE(rig.controls.lane_ref(Entry{Item::Voice, 32, nullptr}, ref));
  // The FX page: the distortion (Drive, Tone, Mix), the delay (time, feedback), the send; not MODEL (a mode) and not the master.
  REQUIRE(rig.controls.lane_ref(rig.entry(kFxPage, 0), ref));
  CHECK(ref.target == kLaneDist);
  CHECK(ref.id == Distortion::Drive);
  REQUIRE(rig.controls.lane_ref(rig.entry(kFxPage, 5), ref));
  CHECK(ref.target == kLaneDelay);
  CHECK(ref.id == units::Delay::Feedback);
  REQUIRE(rig.controls.lane_ref(rig.entry(kFxPage, 6), ref));
  CHECK(ref.target == kLaneMixer);
  CHECK(ref.id == kMixerSendA);
  CHECK_FALSE(rig.controls.lane_ref(rig.entry(kFxPage, 3), ref));
  CHECK_FALSE(rig.controls.lane_ref(Entry{Item::Master, 0, nullptr}, ref));
  for (unsigned k = 0; k < kKeys; ++k) {  // PERFORM SETUP and PATTERN SETUP are no lane's target
    CHECK_FALSE(rig.controls.lane_ref(setup_page().entry[k], ref));
    CHECK_FALSE(rig.controls.lane_ref(rig.entry(page_at(kPagePerfSetup), k), ref));
  }
  // lane_entry is the way back, and the default lanes are the SOUND page.
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    const Entry e = rig.controls.lane_entry(kDefaultLanes[lane]);
    CHECK(e.item == rig.entry(page_at(kPageSound), lane).item);
    CHECK(e.id == rig.entry(page_at(kPageSound), lane).id);
  }
  CHECK(rig.controls.lane_entry(LaneRef{kLaneMixer, kMixerPan}).item == Item::Blank);  // no page has the pan
  CHECK(rig.controls.lane_entry(LaneRef{kLaneNone, 0}).item == Item::Blank);
  CHECK(std::strcmp(rig.controls.lane_name(rig.engine, LaneRef{kLaneVoice, AcidVoice::Cutoff}), "CUTOFF") == 0);
  CHECK(std::strcmp(rig.controls.lane_name(rig.engine, LaneRef{kLaneDist, Distortion::Drive}), "DRIVE") == 0);
  CHECK(std::strcmp(rig.controls.lane_name(rig.engine, LaneRef{kLaneMixer, kMixerSendA}), "SEND A") == 0);
  CHECK(std::strcmp(rig.controls.lane_name(rig.engine, LaneRef{kLaneNone, 0}), "") == 0);
}

TEST_CASE("pages: a motion point is scaled with the target's own descriptor: a unit's curve, the mixer's range, the dB of VOLUME") {
  Rig rig;
  // A unit's parameter: the point is the knob position through the descriptor's curve, whatever the lane number.
  const Entry drive = rig.entry(kFxPage, 0);
  const core::ParamDesc* dd = rig.controls.desc(rig.engine, drive);
  REQUIRE(dd != nullptr);
  CHECK(rig.controls.to_point(rig.engine, drive, dd->min) == 0);
  CHECK(rig.controls.to_point(rig.engine, drive, dd->max) == 254);
  CHECK(rig.controls.from_point(rig.engine, drive, 254) == doctest::Approx(dd->max));
  const std::uint8_t mid = rig.controls.to_point(rig.engine, drive, 0.5f * (dd->min + dd->max));
  CHECK(mid >= 126);
  CHECK(mid <= 128);
  const Entry cutoff = rig.entry(page_at(kPageSound), 0);  // a log knob: the middle of the range is not the middle of the knob
  CHECK(rig.controls.from_point(rig.engine, cutoff, 127) < 1500.0f);
  // The send: percent on the page, linear gain in the lane (0..1.5): 100 % is point 169.
  const Entry send = rig.entry(kFxPage, 6);
  CHECK(rig.controls.to_lane(send, 100.0f) == doctest::Approx(1.0f));
  CHECK(rig.controls.from_lane(send, 1.5f) == doctest::Approx(150.0f));
  CHECK(rig.controls.to_point(rig.engine, send, 0.0f) == 0);
  CHECK(rig.controls.to_point(rig.engine, send, 100.0f) == mixer_value_to_point(kMixerSendA, 1.0f));
  CHECK(rig.controls.from_point(rig.engine, send, 254) == doctest::Approx(150.0f));
  // VOLUME: the dB the page shows, the linear gain the engine takes, the mixer's 0..1.5 range.
  const Entry vol = rig.entry(page_at(kPageSound), 7);
  CHECK(rig.controls.to_lane(vol, 0.0f) == doctest::Approx(1.0f));
  CHECK(rig.controls.to_lane(vol, -60.0f) == 0.0f);
  CHECK(rig.controls.to_lane(vol, 3.5f) <= 1.5f);
  CHECK(rig.controls.from_lane(vol, 1.0f) == doctest::Approx(0.0f).epsilon(0.001));
  CHECK(rig.controls.from_lane(vol, 0.0f) == -60.0f);
  CHECK(rig.controls.from_lane(vol, 9.0f) == doctest::Approx(3.5f));
  const std::uint8_t p0 = rig.controls.to_point(rig.engine, vol, 0.0f);  // 0 dB = unity = point 169 of 254
  CHECK(p0 == mixer_value_to_point(kMixerLevel, 1.0f));
  CHECK(rig.controls.from_point(rig.engine, vol, p0) == doctest::Approx(0.0f).epsilon(0.05));
  // An entry no lane can drive has no point.
  CHECK(rig.controls.to_point(rig.engine, Entry{Item::Tempo, 0, nullptr}, 120.0f) == 0);
}

TEST_CASE("pages: PERFORM SETUP's page (the four assignments, FILL, the freeze's length) (F5 FILL's variant, F6 the freeze's length)") {
  Rig rig;
  // F1..F4: Note, Instr, Vol, Fx with the defaults of C4.
  const char* const want_label[4] = {"NOTE", "INSTR", "VOL", "FX"};
  const char* const want_text[4] = {"MUTE", "ACC ALL", "FILL", "FREEZE"};
  for (unsigned b = 0; b < 4; ++b) {
    CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(page_at(kPagePerfSetup), b)), want_label[b]) == 0);
    CHECK(rig.text(rig.entry(page_at(kPagePerfSetup), b)) == want_text[b]);
  }
  Entry vol = rig.entry(page_at(kPagePerfSetup), 2);
  rig.controls.step(rig.engine, vol, -1, false);  // FILL -> OCT UP
  CHECK(rig.wiring.perf[2] == static_cast<std::uint8_t>(PerfAction::OctUp));
  CHECK(rig.text(vol) == "OCT UP");
  rig.controls.set(rig.engine, vol, 2.0f);
  CHECK(rig.text(vol) == "SLD ALL");
  rig.controls.set(rig.engine, vol, 99.0f);  // clamped to the last action
  CHECK(rig.wiring.perf[2] == static_cast<std::uint8_t>(PerfAction::Reverse));
  // F5: what FILL plays, through the sequencer.
  const Entry fill = rig.entry(page_at(kPagePerfSetup), 4);
  CHECK(rig.text(fill) == "MUTATE");
  rig.controls.step(rig.engine, fill, 1, false);
  CHECK(rig.sequencer->fill_variant() == seq::FillVariant::OctUp);
  CHECK(rig.text(fill) == "OCT UP");
  rig.controls.set(rig.engine, fill, 3.0f);
  CHECK(rig.sequencer->fill_variant() == seq::FillVariant::AccRotate);
  CHECK(rig.text(fill) == "ACC ROT");
  rig.controls.set(rig.engine, fill, 7.0f);
  CHECK(rig.sequencer->fill_variant() == seq::FillVariant::AccRotate);  // clamped
  // PERFORM SETUP's F5 is the same FILL variant (Item::FillVariant), F6 the beat freeze's Length, the unit's own parameter.
  CHECK(rig.entry(page_at(kPagePerfSetup), 4).item == Item::FillVariant);
  CHECK(rig.text(rig.entry(page_at(kPagePerfSetup), 4)) == "ACC ROT");
  rig.controls.set(rig.engine, rig.entry(page_at(kPagePerfSetup), 4), 2.0f);
  CHECK(rig.sequencer->fill_variant() == seq::FillVariant::Ratchet);
  const Entry len = rig.entry(page_at(kPagePerfSetup), 5);
  CHECK(len.item == Item::Freeze);
  CHECK(len.id == Freeze::Length);
  CHECK(rig.engine.param(rig.wiring.freeze, Freeze::Length) == 1.0f);
  rig.controls.step(rig.engine, len, 1, false);
  CHECK(rig.engine.param(rig.wiring.freeze, Freeze::Length) == 2.0f);
  CHECK(rig.controls.get(rig.engine, len) == 2.0f);
  // Every entry has a descriptor and a label that fits.
  for (unsigned k = 0; k < kKeys; ++k) {
    CHECK(rig.controls.desc(rig.engine, setup_page().entry[k]) != nullptr);
    CHECK(rig.controls.desc(rig.engine, rig.entry(page_at(kPagePerfSetup), k)) != nullptr);  // F5..F8: FILL, the freeze's length, ROLL, STOP
  }
}

TEST_CASE("pages: choosing a scale locks the keyboard to it (the generator's default scale is not a choice)") {
  Rig rig;
  CHECK_FALSE(rig.wiring.scale_set);
  const Entry scale = setup_page().entry[4];  // PATTERN SETUP's F5
  REQUIRE(scale.item == Item::Scale);
  rig.controls.set(rig.engine, scale, 3.0f);
  CHECK(rig.wiring.scale_set);
  // The other generator entries do not set it.
  Rig other;
  other.controls.set(other.engine, setup_page().entry[5], 40.0f);  // DENSITY
  CHECK_FALSE(other.wiring.scale_set);
}

TEST_CASE("pages: the SOUND page goes through the engine to the voice") {
  Rig rig;
  const Entry cutoff = rig.entry(page_at(kPageSound), 0);
  const float before = rig.controls.get(rig.engine, cutoff);
  CHECK(before == doctest::Approx(350.0f));
  rig.controls.step(rig.engine, cutoff, 5, false);
  CHECK(rig.controls.get(rig.engine, cutoff) > before);
  CHECK(rig.engine.param(rig.wiring.voice, AcidVoice::Cutoff) == rig.controls.get(rig.engine, cutoff));
  rig.controls.set(rig.engine, cutoff, 1.0e9f);  // clamped by the descriptor
  CHECK(rig.controls.get(rig.engine, cutoff) == doctest::Approx(4500.0f));
  const Entry wave = rig.entry(page_at(kPageSound), 5);
  CHECK(rig.text(wave) == "SAW");
  rig.controls.step(rig.engine, wave, 1, false);
  CHECK(rig.text(wave) == "SQR");
  rig.controls.step(rig.engine, wave, 1, false);  // stays at the end
  CHECK(rig.text(wave) == "SQR");
}

TEST_CASE("pages: VOLUME is slot 0's level in dB, silent at the bottom, capped at the engine's 1.5") {
  Rig rig;
  const Entry vol = rig.entry(page_at(kPageSound), 7);
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
  CHECK(rig.engine.param(rig.wiring.voice, AcidVoice::Volume) == doctest::Approx(-3.0f));
}

TEST_CASE("pages: the FX page: the distortion, the delay, the send, and the master (MASTER's entry)") {
  Rig rig;
  const Entry drive = rig.entry(kFxPage, 0);
  rig.controls.set(rig.engine, drive, 0.8f);
  CHECK(rig.engine.param(rig.wiring.dist, Distortion::Drive) == doctest::Approx(0.8f));
  CHECK(rig.text(drive) == "80%");
  // MODEL: the insert's Active and Type as one key, OFF then the five models.
  const Entry mode = rig.entry(kFxPage, 3);
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
  const Entry mix = rig.entry(kFxPage, 2);
  CHECK(rig.text(mix) == "85%");
  rig.controls.set(rig.engine, mix, 0.3f);
  CHECK(rig.engine.param(rig.wiring.dist, Distortion::Mix) == doctest::Approx(0.3f));
  const Entry time = rig.entry(kFxPage, 4);
  rig.controls.step(rig.engine, time, 1, false);
  CHECK(rig.engine.param(rig.wiring.delay, units::Delay::Time) == doctest::Approx(3.0f));

  const Entry send = rig.entry(kFxPage, 6);
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

  const Entry master{Item::Master, 0, nullptr};  // MASTER F4's entry
  CHECK(rig.text(master) == "100%");
  rig.controls.set(rig.engine, master, 150.0f);
  CHECK(rig.engine.master_level() == doctest::Approx(1.5f));
  rig.controls.set(rig.engine, master, 500.0f);
  CHECK(rig.engine.master_level() == doctest::Approx(1.5f));
}

TEST_CASE("pages: the tempo and the fields of the selected pattern (MASTER's and PATTERN SETUP's entries)") {
  Rig rig;
  const Entry tempo = Entry{Item::Tempo, 0, nullptr};
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
  rig.controls.set(rig.engine, setup_page().entry[1], 40.0f);
  CHECK(bank.pattern(sel).swing == 40);
  rig.controls.set(rig.engine, setup_page().entry[1], 90.0f);
  CHECK(bank.pattern(sel).swing == kMaxSwing);
  rig.controls.set(rig.engine, setup_page().entry[2], 75.0f);
  CHECK(bank.pattern(sel).gate == 75);
  rig.controls.set(rig.engine, setup_page().entry[3], -7.0f);
  CHECK(bank.pattern(sel).transpose == -7);
  CHECK(rig.text(setup_page().entry[3]) == "-7st");

  const Entry length = setup_page().entry[0];
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
  rig.controls.set(rig.engine, setup_page().entry[1], 12.0f);
  CHECK(bank.pattern(7).swing == 12);
  CHECK(bank.pattern(sel).swing == kMaxSwing);
}

TEST_CASE("pages: the generator's settings, PATTERN SETUP's keys: scale, density, accent and slide chance") {
  Rig rig;
  const Entry scale = setup_page().entry[4];
  const Entry density = setup_page().entry[5];
  CHECK(rig.wiring.gen.scale == 18);
  CHECK(rig.text(scale) == "Penta Min");
  rig.controls.set(rig.engine, scale, 3.0f);
  CHECK(rig.wiring.gen.scale == 3);
  CHECK(rig.text(scale) == "Major");
  rig.controls.set(rig.engine, scale, 100.0f);
  CHECK(rig.wiring.gen.scale == 39);

  rig.controls.set(rig.engine, density, 40.0f);
  CHECK(rig.wiring.gen.density == 40);
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

TEST_CASE("pages: what a snapshot holds (R2); the SNAP page's keys went with H4, the snapshots are PERFORM's") {
  Rig rig;
  rig.wiring.snaps.store(2, nullptr);  // a missing buffer stores nothing
  CHECK_FALSE(rig.wiring.snaps.used(2));
  float v[kSnapValues] = {};
  rig.wiring.snaps.store(2, v);
  CHECK(rig.wiring.snaps.used(2));

  // The targets: SOUND's seven, the distortion's five, the delay's four, the level and the send, then (P2, D29) the growl's VOWEL, FMT Q, GRL MIX and
  // CMB MIX, then (MODS S2, docs/MODS-PLAN.md 5.9) the voice's eleven mods in id order and the growl's FMT MIX, CMB TUNE and CMB FB, each a real entry the lanes know.
  CHECK(kSnapTargetCount == 36);
  CHECK(snap_target(18).item == Item::Growl);
  CHECK(snap_target(18).id == Growl::Vowel);
  CHECK(snap_target(19).id == Growl::FormantQ);
  CHECK(snap_target(20).id == Growl::Mix);
  CHECK(snap_target(21).id == Growl::CombMix);
  unsigned discrete = 0;
  for (unsigned i = 0; i < kSnapTargetCount; ++i) {
    const Entry t = snap_target(i);
    CHECK(rig.controls.desc(rig.engine, t) != nullptr);
    LaneRef ref{kLaneNone, 0};
    CHECK(rig.controls.lane_ref(t, ref));  // every target can be slewed
    discrete += rig.controls.snap_discrete(rig.engine, i) ? 1u : 0u;
  }
  CHECK(discrete == 10);  // the wave, the distortion's switch and model, the delay's time (a list of note values) and mode; the mods' SOFT ATK, SUB OSC, ACC SWEEP, MUFFLER and SWP SPEED
  rig.controls.set(rig.engine, rig.entry(page_at(kPageSound), 0), 1200.0f);
  rig.controls.set(rig.engine, rig.entry(page_at(kPageSound), 7), -12.0f);
  float now[kSnapValues];
  rig.controls.snap_capture(rig.engine, now);
  CHECK(now[0] == doctest::Approx(1200.0f));
  CHECK(now[16] == doctest::Approx(-12.0f));  // the level, in dB
  CHECK(now[17] == doctest::Approx(30.0f));   // the send, in percent
  // The growl's four are read in the units GROWL shows (the unit's own: a vowel and the mixes 0..1, the formant's Q 1..20).
  rig.controls.set(rig.engine, snap_target(18), 0.5f);
  rig.controls.set(rig.engine, snap_target(19), 12.0f);
  rig.controls.set(rig.engine, snap_target(20), 0.25f);
  rig.controls.set(rig.engine, snap_target(21), 0.75f);
  rig.controls.snap_capture(rig.engine, now);
  CHECK(now[18] == doctest::Approx(0.5f));
  CHECK(now[19] == doctest::Approx(12.0f).epsilon(0.02));
  CHECK(now[20] == doctest::Approx(0.25f));
  CHECK(now[21] == doctest::Approx(0.75f));
  CHECK(now[kSnapTargetCount] == 0.0f);
  CHECK(now[kSnapValues - 1u] == 0.0f);  // room for four more targets stays zero

  // The mods: targets 22..32 are the voice's ids 8..18 in id order (never STOCK, id 19), and 33..35 the growl's FMT MIX, CMB TUNE and CMB FB; each is read and written in its own units.
  static_assert(kSnapValues - kSnapTargetCount == 4, "room for four more targets");
  for (unsigned i = 0; i < 11; ++i) {
    CHECK(snap_target(22 + i).item == Item::Voice);
    CHECK(snap_target(22 + i).id == AcidVoice::AccDecay + i);
    CHECK(snap_target(22 + i).id != AcidVoice::Stock);
  }
  CHECK(snap_target(33).id == Growl::FormantMix);
  CHECK(snap_target(34).id == Growl::CombTune);
  CHECK(snap_target(35).id == Growl::CombFeedback);
  CHECK(snap_target(35).item == Item::Growl);
  rig.controls.set(rig.engine, snap_target(22), 900.0f);                    // ACC DECAY, ms
  rig.controls.set(rig.engine, snap_target(26), 3.0f);                      // SUB OSC: HIGH
  rig.controls.set(rig.engine, snap_target(32), 250.0f);                    // AMP DECAY, ms
  rig.controls.set(rig.engine, snap_target(34), 7.0f);                      // CMB TUNE
  rig.controls.snap_capture(rig.engine, now);
  CHECK(now[22] == doctest::Approx(900.0f).epsilon(0.01));
  CHECK(now[26] == doctest::Approx(3.0f));
  CHECK(now[32] == doctest::Approx(250.0f).epsilon(0.01));
  CHECK(now[34] == doctest::Approx(7.0f).epsilon(0.02));
  CHECK(rig.controls.snap_discrete(rig.engine, 26));
  CHECK_FALSE(rig.controls.snap_discrete(rig.engine, 22));
  CHECK_FALSE(rig.controls.snap_discrete(rig.engine, 34));
}

TEST_CASE("pages: GROWL is the growl's parameters (and lane targets), WOBBLE the wobble's settings in the screen's order, PERF the freeze's roll and stop (M3c)") {
  Rig rig;
  // GROWL: F1..F7 are the growl's ids 0..5 and 7 (Note, id 6, is the sequencer's), F8 is free.
  static constexpr Growl::Param kIds[7] = {Growl::Vowel,      Growl::FormantQ, Growl::FormantMix, Growl::CombTune,
                                           Growl::CombFeedback, Growl::CombMix, Growl::Mix};
  for (unsigned k = 0; k < 7; ++k) {
    const Entry e = rig.entry(kGrowlPage, k);
    CHECK(e.item == Item::Growl);
    CHECK(e.id == kIds[k]);
    CHECK(rig.controls.get(rig.engine, e) == doctest::Approx(rig.engine.param(rig.wiring.growl, kIds[k])));
  }
  CHECK(rig.text(rig.entry(kGrowlPage, 6)) == "0%");  // Mix 0: the growl is a bit-exact bypass until the player opens it
  const float vowel0 = rig.controls.get(rig.engine, rig.entry(kGrowlPage, 0));
  rig.controls.step(rig.engine, rig.entry(kGrowlPage, 0), 10, false);
  CHECK(rig.controls.get(rig.engine, rig.entry(kGrowlPage, 0)) > vowel0);
  LaneRef ref{kLaneNone, 99};
  REQUIRE(rig.controls.lane_ref(rig.entry(kGrowlPage, 0), ref));
  CHECK(ref.target == kLaneGrowl);
  CHECK(ref.id == Growl::Vowel);
  const Entry back = rig.controls.lane_entry(ref);
  CHECK(back.item == Item::Growl);
  CHECK(back.id == Growl::Vowel);
  CHECK(std::strcmp(rig.controls.lane_name(rig.engine, ref), "VOWEL") == 0);

  // WOBBLE, in the order the screen shows: F1 RATE, F2 SHAPE, F3 DEPTH, F4 TARGET, F5 RESET (F6 and F7 are free, F8 is the WOB action key);
  // defaults 1/8, sine, 50 %, the voice's Cutoff, reset on.
  CHECK(rig.entry(kWobblePage, 5).item == Item::Blank);
  CHECK(rig.entry(kWobblePage, 6).item == Item::Blank);
  CHECK(rig.entry(kWobblePage, 7).item == Item::WobOn);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kWobblePage, 3)), "TARGET") == 0);
  CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kWobblePage, 4)), "RESET") == 0);
  CHECK(rig.text(rig.entry(kWobblePage, 0)) == "1/8");
  CHECK(rig.text(rig.entry(kWobblePage, 1)) == "SINE");
  CHECK(rig.text(rig.entry(kWobblePage, 2)) == "50%");
  CHECK(rig.text(rig.entry(kWobblePage, 3)) == "CUTOFF");
  CHECK(rig.text(rig.entry(kWobblePage, 4)) == "ON");
  rig.controls.step(rig.engine, rig.entry(kWobblePage, 0), 2, false);  // 1/8 -> 1/8T -> 1/16
  CHECK(rig.wiring.wobble.rate == seq::kWobble1_16);
  rig.controls.step(rig.engine, rig.entry(kWobblePage, 0), 99, false);  // stops at the last rate
  CHECK(rig.wiring.wobble.rate == seq::kWobbleRateCount - 1u);
  rig.controls.step(rig.engine, rig.entry(kWobblePage, 3), 1, false);
  CHECK(rig.wiring.wobble.target == kWobbleVowel);
  CHECK(rig.text(rig.entry(kWobblePage, 3)) == "VOWEL");
  CHECK(rig.controls.wobble_target().target == kLaneGrowl);
  CHECK(rig.controls.wobble_target().id == Growl::Vowel);
  rig.controls.step(rig.engine, rig.entry(kWobblePage, 4), -1, false);
  CHECK_FALSE(rig.wiring.wobble.reset);
  CHECK(rig.sequencer->wobble().reset_on_note == false);  // the reset applies at once, even with the wobble off
  rig.controls.step(rig.engine, rig.entry(kWobblePage, 2), -99, false);
  CHECK(rig.wiring.wobble.depth_pct == 0);
  for (unsigned k = 0; k < 5; ++k) {  // the wobble's settings drive no lane (the lane for its rate and depth is kLaneSeq's)
    CHECK_FALSE(rig.controls.lane_ref(rig.entry(kWobblePage, k), ref));
  }

  // PERFORM SETUP: F6 the freeze's Length, F7 ROLL (Item::RollMode), F8 STOP's time (the unit's id 6).
  CHECK(rig.entry(page_at(kPagePerfSetup), 5).item == Item::Freeze);
  CHECK(rig.entry(page_at(kPagePerfSetup), 5).id == Freeze::Length);
  CHECK(rig.entry(page_at(kPagePerfSetup), 6).item == Item::RollMode);
  CHECK(rig.entry(page_at(kPagePerfSetup), 7).item == Item::Freeze);
  CHECK(rig.entry(page_at(kPagePerfSetup), 7).id == Freeze::StopTime);
  CHECK(rig.text(rig.entry(page_at(kPagePerfSetup), 7)) == "1/2bar");  // the default: half a bar
  rig.controls.step(rig.engine, rig.entry(page_at(kPagePerfSetup), 7), 9, false);
  CHECK(rig.engine.param(rig.wiring.freeze, Freeze::StopTime) == doctest::Approx(4.0f));
  CHECK(rig.text(rig.entry(page_at(kPagePerfSetup), 7)) == "2bar");  // clamped at two bars
  rig.controls.step(rig.engine, rig.entry(page_at(kPagePerfSetup), 7), -9, false);
  CHECK(rig.text(rig.entry(page_at(kPagePerfSetup), 7)) == "1/8bar");
}

TEST_CASE("pages: ROLL is one list, the rate and the accelerate together (Item::RollMode, UX-303 5.4); BUILD starts at 1/8") {
  Rig rig;
  const Entry roll = rig.entry(page_at(kPagePerfSetup), 6);
  REQUIRE(roll.item == Item::RollMode);
  CHECK(std::strcmp(rig.controls.label(rig.engine, roll), "ROLL") == 0);
  const core::ParamDesc* d = rig.controls.desc(rig.engine, roll);
  REQUIRE(d != nullptr);
  CHECK(d->list_count == 6);
  for (unsigned i = 0; i < 6; ++i) {  // every name fits the knob box's eight characters
    CHECK(std::strlen(d->names[i]) <= 8);
  }
  // The unit's defaults: 1/16, accelerate off.
  CHECK(rig.text(roll) == "1/16");
  const char* const want[6] = {"1/8", "1/16", "1/32", "1/64", "BLD BEAT", "BLD BAR"};
  for (unsigned i = 0; i < 6; ++i) {
    rig.controls.set(rig.engine, roll, static_cast<float>(i));
    CHECK(rig.text(roll) == want[i]);
    CHECK(rig.controls.get(rig.engine, roll) == doctest::Approx(static_cast<float>(i)));
    if (i < 4) {  // a rate: that rate, the accelerate off
      CHECK(rig.engine.param(rig.wiring.freeze, Freeze::RollRate) == doctest::Approx(static_cast<float>(i)));
      CHECK(rig.engine.param(rig.wiring.freeze, Freeze::Accelerate) == doctest::Approx(0.0f));
    } else {  // BUILD: from 1/8, BEAT or BAR
      CHECK(rig.engine.param(rig.wiring.freeze, Freeze::RollRate) == doctest::Approx(0.0f));
      CHECK(rig.engine.param(rig.wiring.freeze, Freeze::Accelerate) == doctest::Approx(static_cast<float>(i - 3)));
    }
  }
  // The encoder walks the list and clamps at both ends; BLD BAR back one place is BLD BEAT, then the rates from 1/64.
  rig.controls.step(rig.engine, roll, 9, false);
  CHECK(rig.text(roll) == "BLD BAR");
  rig.controls.step(rig.engine, roll, -1, false);
  CHECK(rig.text(roll) == "BLD BEAT");
  rig.controls.step(rig.engine, roll, -1, false);
  CHECK(rig.text(roll) == "1/64");
  rig.controls.step(rig.engine, roll, -9, false);
  CHECK(rig.text(roll) == "1/8");
  // A project that saved a rate with an accelerate (the unit's own two parameters, as P1 saved them) reads as BUILD: the accelerate decides.
  (void)rig.engine.set_param(rig.wiring.freeze, Freeze::RollRate, 2.0f);
  (void)rig.engine.set_param(rig.wiring.freeze, Freeze::Accelerate, 2.0f);
  CHECK(rig.text(roll) == "BLD BAR");
  // No lane drives it and no snapshot holds it.
  LaneRef ref{kLaneNone, 0};
  CHECK_FALSE(rig.controls.lane_ref(roll, ref));
}

TEST_CASE("pages: PATTERN SETUP's page (UX-303 5.2): this pattern's four, then the generator's four, and the entries it appends") {
  Rig rig;
  const Page& pg = setup_page();
  CHECK(std::strcmp(pg.name, "SETUP") == 0);
  static constexpr Item kItems[kKeys] = {Item::Length, Item::Swing, Item::Gate, Item::Transpose, Item::Scale, Item::Density, Item::AccPct, Item::SldPct};
  static constexpr const char* kLabels[kKeys] = {"LENGTH", "SWING", "GATE", "TRANSP", "SCALE", "DENSITY", "ACC %", "SLD %"};
  for (unsigned k = 0; k < kKeys; ++k) {
    CHECK(pg.entry[k].item == kItems[k]);
    CHECK(rig.controls.desc(rig.engine, pg.entry[k]) != nullptr);
    CHECK(std::strcmp(rig.controls.label(rig.engine, pg.entry[k]), kLabels[k]) == 0);
    CHECK(std::strlen(rig.controls.label(rig.engine, pg.entry[k])) <= 8);  // the F-key row's room
  }
  // (No Item value is stored in a project: the chunks hold unit targets and parameter ids, snapshot target indices, PerfAction numbers and LaneRefs.)
  // No lane drives a SETUP entry.
  LaneRef ref{kLaneNone, 0};
  for (unsigned k = 0; k < kKeys; ++k) {
    CHECK_FALSE(rig.controls.lane_ref(pg.entry[k], ref));
  }
}

TEST_CASE("pages: ACC % and SLD % are two settings of the generator, project-wide") {
  Rig rig;
  const Entry acc{Item::AccPct, 0, nullptr};
  const Entry sld{Item::SldPct, 0, nullptr};
  CHECK(rig.text(acc) == "25%");
  CHECK(rig.text(sld) == "20%");
  rig.controls.set(rig.engine, acc, 60.0f);
  CHECK(rig.wiring.gen.accent_pct == 60);
  CHECK(rig.wiring.gen.slide_pct == 20);  // the slide chance is its own
  rig.controls.set(rig.engine, sld, 5.0f);
  CHECK(rig.wiring.gen.accent_pct == 60);
  CHECK(rig.wiring.gen.slide_pct == 5);
  CHECK(rig.text(sld) == "5%");
  rig.controls.step(rig.engine, sld, 1, true);  // ten a coarse step
  CHECK(rig.wiring.gen.slide_pct == 15);
  rig.controls.set(rig.engine, sld, 500.0f);
  CHECK(rig.wiring.gen.slide_pct == 100);
  rig.controls.set(rig.engine, acc, -9.0f);
  CHECK(rig.wiring.gen.accent_pct == 0);
  // Both read what the generator holds, whichever way it was set.
  rig.wiring.gen.accent_pct = 50;
  rig.wiring.gen.slide_pct = 40;
  CHECK(rig.controls.get(rig.engine, acc) == doctest::Approx(50.0f));
  CHECK(rig.controls.get(rig.engine, sld) == doctest::Approx(40.0f));
  // The generator is not the pattern's: the selected pattern changing leaves the settings alone (D1).
  rig.bank->select(9);
  CHECK(rig.wiring.gen.accent_pct == 50);
  CHECK(rig.wiring.gen.slide_pct == 40);
}

TEST_CASE("pages: LENGTH goes through seq::resize: a step up copies the line into the new part, a step down truncates, each one undo") {
  Rig rig;
  seq::Bank& bank = *rig.bank;
  const unsigned sel = bank.selected();
  const Entry length = setup_page().entry[0];
  // A line with something in every step, so the copy can be told from the old part.
  for (unsigned i = 0; i < 16; ++i) {
    (void)bank.set_note(sel, i, 40u + i);
  }
  bank.clear_history();
  const Pattern before = bank.pattern(sel);
  REQUIRE(before.length == 16);
  rig.controls.step(rig.engine, length, 1, false);  // 32
  REQUIRE(bank.pattern(sel).length == 32);
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(bank.pattern(sel).steps[16 + i].note == bank.pattern(sel).steps[i].note);
    CHECK(bank.pattern(sel).steps[16 + i].flags == bank.pattern(sel).steps[i].flags);
  }
  rig.controls.step(rig.engine, length, -2, false);  // 8: the steps past the 8th are cut
  REQUIRE(bank.pattern(sel).length == 8);
  CHECK(bank.pattern(sel).steps[9].flags == kStepRest);
  CHECK(bank.pattern(sel).steps[3].note == before.steps[3].note);
  // One undo each way.
  CHECK(bank.undo() == static_cast<int>(sel));
  CHECK(bank.pattern(sel).length == 32);
  CHECK(bank.pattern(sel).steps[16 + 5].note == before.steps[5].note);
  CHECK(bank.undo() == static_cast<int>(sel));
  CHECK(bank.pattern(sel).length == 16);
  CHECK_FALSE(bank.can_undo());
  for (unsigned i = 0; i < 16; ++i) {
    CHECK(bank.pattern(sel).steps[i].note == before.steps[i].note);
  }
  // The same length again is no edit.
  rig.controls.set(rig.engine, length, 1.0f);
  CHECK_FALSE(bank.can_undo());
}

TEST_CASE("a press of Left or Right moves a knob by 1/20 of its range; an enum, a switch or a knob under 20 steps moves one value") {
  const char* const names[3] = {"A", "B", "C"};
  core::ParamDesc d{};
  d.min = 0.0f;
  d.max = 1.0f;
  d.detents = 100;
  CHECK(press_detents(d) == 5);  // a range: 100 / 20
  d.detents = 400;
  CHECK(press_detents(d) == 20);
  d.detents = 20;
  CHECK(press_detents(d) == 1);
  d.detents = 19;
  CHECK(press_detents(d) == 1);  // under 20 steps: one value
  d.detents = 8;
  CHECK(press_detents(d) == 1);
  d.detents = 400;
  d.names = names;
  CHECK(press_detents(d) == 1);  // an enum
}
