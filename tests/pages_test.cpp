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

  Entry entry(unsigned page, unsigned key) const { return page_at(page).entry[key]; }
  std::string text(const Entry& e) const {
    char buf[24];
    (void)controls.format(engine, e, buf, sizeof buf);
    return std::string(buf) + controls.unit_text(engine, e);
  }
};

}  // namespace

TEST_CASE("pages: the five pages and their keys (DESIGN-303 section 3; PERF is M3b's, GROWL and WOBBLE M3c's; the SNAP page went with H4)") {
  Rig rig;
  CHECK(kPageCount == 5);
  CHECK(std::strcmp(page_at(kPageGrowl).name, "GROWL") == 0);
  CHECK(std::strcmp(page_at(kPageWobble).name, "WOBBLE") == 0);
  CHECK(kPageGrowl == kPageFx + 1);  // after FX
  CHECK(std::strcmp(page_at(kPagePerfSetup).name, "PERF SETUP") == 0);
  CHECK(std::strcmp(page_at(kPageSound).name, "SOUND") == 0);
  CHECK(std::strcmp(page_at(kPageFx).name, "FX") == 0);
  // Every key has a descriptor and a label that fits the F-key row (eight characters between two boxes).
  for (unsigned p = 0; p < kPageCount; ++p) {
    for (unsigned k = 0; k < kKeys; ++k) {
      const Entry e = rig.entry(p, k);
      if ((p == kPageGrowl && k >= 7) || (p == kPageWobble && k >= 5)) {  // the growl has seven keys, the wobble five
        CHECK(e.item == Item::Blank);
        continue;
      }
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

TEST_CASE("pages: the entries a motion lane can drive are the voice's, the distortion's, the delay's, VOLUME and DLY SEND (C2)") {
  Rig rig;
  LaneRef ref{kLaneNone, 99};
  // The SOUND page: the voice's ids 0..6 and VOLUME, the mixer's level.
  for (unsigned k = 0; k < 7; ++k) {
    REQUIRE(rig.controls.lane_ref(rig.entry(kPageSound, k), ref));
    CHECK(ref.target == kLaneVoice);
    CHECK(ref.id == k);
  }
  REQUIRE(rig.controls.lane_ref(rig.entry(kPageSound, 7), ref));
  CHECK(ref.target == kLaneMixer);
  CHECK(ref.id == kMixerLevel);
  // The FX page: the distortion (Drive, Tone, Mix), the delay (time, feedback), the send; not DIST (a mode) and not the master.
  REQUIRE(rig.controls.lane_ref(rig.entry(kPageFx, 0), ref));
  CHECK(ref.target == kLaneDist);
  CHECK(ref.id == Distortion::Drive);
  REQUIRE(rig.controls.lane_ref(rig.entry(kPageFx, 3), ref));
  CHECK(ref.target == kLaneDelay);
  CHECK(ref.id == units::Delay::Feedback);
  REQUIRE(rig.controls.lane_ref(rig.entry(kPageFx, 4), ref));
  CHECK(ref.target == kLaneMixer);
  CHECK(ref.id == kMixerSendA);
  CHECK_FALSE(rig.controls.lane_ref(rig.entry(kPageFx, 6), ref));
  CHECK_FALSE(rig.controls.lane_ref(rig.entry(kPageFx, 7), ref));
  for (unsigned k = 0; k < kKeys; ++k) {  // PERFORM SETUP and PATTERN SETUP are no lane's target
    CHECK_FALSE(rig.controls.lane_ref(setup_page().entry[k], ref));
    CHECK_FALSE(rig.controls.lane_ref(rig.entry(kPagePerfSetup, k), ref));
  }
  // lane_entry is the way back, and the default lanes are the SOUND page.
  for (unsigned lane = 0; lane < kMotionLanes; ++lane) {
    const Entry e = rig.controls.lane_entry(kDefaultLanes[lane]);
    CHECK(e.item == rig.entry(kPageSound, lane).item);
    CHECK(e.id == rig.entry(kPageSound, lane).id);
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
  const Entry drive = rig.entry(kPageFx, 0);
  const core::ParamDesc* dd = rig.controls.desc(rig.engine, drive);
  REQUIRE(dd != nullptr);
  CHECK(rig.controls.to_point(rig.engine, drive, dd->min) == 0);
  CHECK(rig.controls.to_point(rig.engine, drive, dd->max) == 254);
  CHECK(rig.controls.from_point(rig.engine, drive, 254) == doctest::Approx(dd->max));
  const std::uint8_t mid = rig.controls.to_point(rig.engine, drive, 0.5f * (dd->min + dd->max));
  CHECK(mid >= 126);
  CHECK(mid <= 128);
  const Entry cutoff = rig.entry(kPageSound, 0);  // a log knob: the middle of the range is not the middle of the knob
  CHECK(rig.controls.from_point(rig.engine, cutoff, 127) < 1500.0f);
  // The send: percent on the page, linear gain in the lane (0..1.5): 100 % is point 169.
  const Entry send = rig.entry(kPageFx, 4);
  CHECK(rig.controls.to_lane(send, 100.0f) == doctest::Approx(1.0f));
  CHECK(rig.controls.from_lane(send, 1.5f) == doctest::Approx(150.0f));
  CHECK(rig.controls.to_point(rig.engine, send, 0.0f) == 0);
  CHECK(rig.controls.to_point(rig.engine, send, 100.0f) == mixer_value_to_point(kMixerSendA, 1.0f));
  CHECK(rig.controls.from_point(rig.engine, send, 254) == doctest::Approx(150.0f));
  // VOLUME: the dB the page shows, the linear gain the engine takes, the mixer's 0..1.5 range.
  const Entry vol = rig.entry(kPageSound, 7);
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
    CHECK(std::strcmp(rig.controls.label(rig.engine, rig.entry(kPagePerfSetup, b)), want_label[b]) == 0);
    CHECK(rig.text(rig.entry(kPagePerfSetup, b)) == want_text[b]);
  }
  Entry vol = rig.entry(kPagePerfSetup, 2);
  rig.controls.step(rig.engine, vol, -1, false);  // FILL -> OCT UP
  CHECK(rig.wiring.perf[2] == static_cast<std::uint8_t>(PerfAction::OctUp));
  CHECK(rig.text(vol) == "OCT UP");
  rig.controls.set(rig.engine, vol, 2.0f);
  CHECK(rig.text(vol) == "SLD ALL");
  rig.controls.set(rig.engine, vol, 99.0f);  // clamped to the last action
  CHECK(rig.wiring.perf[2] == static_cast<std::uint8_t>(PerfAction::Reverse));
  // F5: what FILL plays, through the sequencer.
  const Entry fill = rig.entry(kPagePerfSetup, 4);
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
  CHECK(rig.entry(kPagePerfSetup, 4).item == Item::FillVariant);
  CHECK(rig.text(rig.entry(kPagePerfSetup, 4)) == "ACC ROT");
  rig.controls.set(rig.engine, rig.entry(kPagePerfSetup, 4), 2.0f);
  CHECK(rig.sequencer->fill_variant() == seq::FillVariant::Ratchet);
  const Entry len = rig.entry(kPagePerfSetup, 5);
  CHECK(len.item == Item::Freeze);
  CHECK(len.id == Freeze::Length);
  CHECK(rig.engine.param(rig.wiring.freeze, Freeze::Length) == 1.0f);
  rig.controls.step(rig.engine, len, 1, false);
  CHECK(rig.engine.param(rig.wiring.freeze, Freeze::Length) == 2.0f);
  CHECK(rig.controls.get(rig.engine, len) == 2.0f);
  // Every entry has a descriptor and a label that fits.
  for (unsigned k = 0; k < kKeys; ++k) {
    CHECK(rig.controls.desc(rig.engine, setup_page().entry[k]) != nullptr);
    CHECK(rig.controls.desc(rig.engine, rig.entry(kPagePerfSetup, k)) != nullptr);  // F5..F8: FILL, the freeze's length, ROLL, STOP
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
  const Entry cutoff = rig.entry(kPageSound, 0);
  const float before = rig.controls.get(rig.engine, cutoff);
  CHECK(before == doctest::Approx(350.0f));
  rig.controls.step(rig.engine, cutoff, 5, false);
  CHECK(rig.controls.get(rig.engine, cutoff) > before);
  CHECK(rig.engine.param(rig.wiring.voice, AcidVoice::Cutoff) == rig.controls.get(rig.engine, cutoff));
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
  CHECK(rig.engine.param(rig.wiring.voice, AcidVoice::Volume) == doctest::Approx(-3.0f));
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
  // CMB MIX, each a real entry the lanes know.
  CHECK(kSnapTargetCount == 22);
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
  CHECK(discrete == 5);  // the wave, the distortion's switch and model, the delay's time (a list of note values) and mode
  rig.controls.set(rig.engine, rig.entry(kPageSound, 0), 1200.0f);
  rig.controls.set(rig.engine, rig.entry(kPageSound, 7), -12.0f);
  float now[kSnapValues];
  rig.controls.snap_capture(rig.engine, now);
  CHECK(now[0] == doctest::Approx(1200.0f));
  CHECK(now[16] == doctest::Approx(-12.0f));  // the level, in dB
  CHECK(now[17] == doctest::Approx(30.0f));   // the send, in percent
  // The growl's four are read in the units the MODS page shows (the unit's own: a vowel and the mixes 0..1, the formant's Q 1..20).
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
  CHECK(now[kSnapValues - 1u] == 0.0f);  // room for two more targets stays zero
}

TEST_CASE("pages: GROWL is the growl's parameters (and lane targets), WOBBLE the wobble's five settings, PERF the freeze's roll and stop (M3c)") {
  Rig rig;
  // GROWL: F1..F7 are the growl's ids 0..5 and 7 (Note, id 6, is the sequencer's), F8 is free.
  static constexpr Growl::Param kIds[7] = {Growl::Vowel,      Growl::FormantQ, Growl::FormantMix, Growl::CombTune,
                                           Growl::CombFeedback, Growl::CombMix, Growl::Mix};
  for (unsigned k = 0; k < 7; ++k) {
    const Entry e = rig.entry(kPageGrowl, k);
    CHECK(e.item == Item::Growl);
    CHECK(e.id == kIds[k]);
    CHECK(rig.controls.get(rig.engine, e) == doctest::Approx(rig.engine.param(rig.wiring.growl, kIds[k])));
  }
  CHECK(rig.text(rig.entry(kPageGrowl, 6)) == "0%");  // Mix 0: the growl is a bit-exact bypass until the player opens it
  const float vowel0 = rig.controls.get(rig.engine, rig.entry(kPageGrowl, 0));
  rig.controls.step(rig.engine, rig.entry(kPageGrowl, 0), 10, false);
  CHECK(rig.controls.get(rig.engine, rig.entry(kPageGrowl, 0)) > vowel0);
  LaneRef ref{kLaneNone, 99};
  REQUIRE(rig.controls.lane_ref(rig.entry(kPageGrowl, 0), ref));
  CHECK(ref.target == kLaneGrowl);
  CHECK(ref.id == Growl::Vowel);
  const Entry back = rig.controls.lane_entry(ref);
  CHECK(back.item == Item::Growl);
  CHECK(back.id == Growl::Vowel);
  CHECK(std::strcmp(rig.controls.lane_name(rig.engine, ref), "VOWEL") == 0);

  // WOBBLE: rate, shape, depth, reset, target; defaults 1/8, sine, 50 %, reset on, the voice's Cutoff.
  CHECK(rig.text(rig.entry(kPageWobble, 0)) == "1/8");
  CHECK(rig.text(rig.entry(kPageWobble, 1)) == "SINE");
  CHECK(rig.text(rig.entry(kPageWobble, 2)) == "50%");
  CHECK(rig.text(rig.entry(kPageWobble, 3)) == "ON");
  CHECK(rig.text(rig.entry(kPageWobble, 4)) == "CUTOFF");
  rig.controls.step(rig.engine, rig.entry(kPageWobble, 0), 2, false);  // 1/8 -> 1/8T -> 1/16
  CHECK(rig.wiring.wobble.rate == seq::kWobble1_16);
  rig.controls.step(rig.engine, rig.entry(kPageWobble, 0), 99, false);  // stops at the last rate
  CHECK(rig.wiring.wobble.rate == seq::kWobbleRateCount - 1u);
  rig.controls.step(rig.engine, rig.entry(kPageWobble, 4), 1, false);
  CHECK(rig.wiring.wobble.target == kWobbleVowel);
  CHECK(rig.text(rig.entry(kPageWobble, 4)) == "VOWEL");
  CHECK(rig.controls.wobble_target().target == kLaneGrowl);
  CHECK(rig.controls.wobble_target().id == Growl::Vowel);
  rig.controls.step(rig.engine, rig.entry(kPageWobble, 3), -1, false);
  CHECK_FALSE(rig.wiring.wobble.reset);
  CHECK(rig.sequencer->wobble().reset_on_note == false);  // the reset applies at once, even with the wobble off
  rig.controls.step(rig.engine, rig.entry(kPageWobble, 2), -99, false);
  CHECK(rig.wiring.wobble.depth_pct == 0);
  for (unsigned k = 0; k < 5; ++k) {  // the wobble's settings drive no lane (the lane for its rate and depth is kLaneSeq's)
    CHECK_FALSE(rig.controls.lane_ref(rig.entry(kPageWobble, k), ref));
  }

  // PERFORM SETUP: F6 the freeze's Length, F7 ROLL (Item::RollMode), F8 STOP's time (the unit's id 6).
  CHECK(rig.entry(kPagePerfSetup, 5).item == Item::Freeze);
  CHECK(rig.entry(kPagePerfSetup, 5).id == Freeze::Length);
  CHECK(rig.entry(kPagePerfSetup, 6).item == Item::RollMode);
  CHECK(rig.entry(kPagePerfSetup, 7).item == Item::Freeze);
  CHECK(rig.entry(kPagePerfSetup, 7).id == Freeze::StopTime);
  CHECK(rig.text(rig.entry(kPagePerfSetup, 7)) == "1/2bar");  // the default: half a bar
  rig.controls.step(rig.engine, rig.entry(kPagePerfSetup, 7), 9, false);
  CHECK(rig.engine.param(rig.wiring.freeze, Freeze::StopTime) == doctest::Approx(4.0f));
  CHECK(rig.text(rig.entry(kPagePerfSetup, 7)) == "2bar");  // clamped at two bars
  rig.controls.step(rig.engine, rig.entry(kPagePerfSetup, 7), -9, false);
  CHECK(rig.text(rig.entry(kPagePerfSetup, 7)) == "1/8bar");
}

TEST_CASE("pages: ROLL is one list, the rate and the accelerate together (Item::RollMode, UX-303 5.4); BUILD starts at 1/8") {
  Rig rig;
  const Entry roll = rig.entry(kPagePerfSetup, 6);
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
