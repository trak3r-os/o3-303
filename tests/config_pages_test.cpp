// SPDX-License-Identifier: GPL-3.0-or-later
// The app's page of the system Config screen (app/src/config_pages.h, docs/UX-303.md 5.12, docs/UX-PLAN.md C1): the keys fixed for ever, every value wired to what it
// controls (AUTOSAVE, KEYS AT BOOT, LADDER SLEW, PAD LEGEND, SKIN), RESET PROJECT, the values back after a reboot, and the retired page DEEP (keys 20..24, docs/MODS-PLAN.md 7):
// no item, no effect, and a settings file that still holds them loads with them dropped and the engine's values untouched.
#include <doctest/doctest.h>

#include "hands.h"

#include "../app/src/frame.h"
#include "../app/src/project_service.h"
#include "../app/src/skin/skin.h"

#include <o3sdk/app/config_page.h>
#include <o3sdk/app/settings.h>
#include <o3sdk/core/param.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/app_access.h>
#include <o3_303/config_keys.h>
#include <o3_303/seq/gen.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/growl/growl.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace hands;
using namespace o3_303;
namespace fs = std::filesystem;
namespace host = o3sdk::platform::host;
using Bytes = std::vector<std::uint8_t>;

namespace {

// The app on a scratch card, started; the autosave period is the test's (a short one keeps the simulated time down).
struct Session {
  host::ScratchDir dir{"o3303-c1"};
  host::DirCard card{dir.path()};
  std::unique_ptr<sim::Harness> h;

  explicit Session(std::uint32_t period_ms = 1'000) {
    REQUIRE(dir.ok());
    h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card, "o3_303");
    REQUIRE(h->start());
    app_project().set_autosave_period(period_ms);
  }
  void run(unsigned ms) { h->run_ms(ms); }
  app::Context& ctx() { return h->runtime().context(); }
  float get(std::uint16_t key) { return o3sdk_app().config_get(ctx(), key); }
  void set(std::uint16_t key, float v) { o3sdk_app().config_set(ctx(), key, v); }
  fs::path path(const std::string& rel) const { return fs::path(dir.path()) / "Apps" / "o3_303" / rel; }
  Bytes read(const std::string& rel) const {
    std::ifstream in(path(rel), std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  void write(const std::string& rel, const Bytes& b) const {
    std::ofstream out(path(rel), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
  }
  // The kit writes the settings 1.5 s after the last change and the project follows its autosave: three seconds hold both.
  void settle() { h->run_ms(3000); }
  float param(dsp::UnitId u, unsigned id) { return h->engine().param(u, static_cast<std::uint16_t>(id)); }
};

const app::ConfigItem* find_item(const app::ConfigView& v, std::uint16_t key) {
  for (unsigned p = 0; p < v.pages; ++p) {
    for (unsigned i = 0; i < v.page[p].count; ++i) {
      if (((v.page[p].mask >> i) & 1u) != 0u && v.page[p].items[i].key == key) {
        return &v.page[p].items[i];
      }
    }
  }
  return nullptr;
}

}  // namespace

TEST_CASE("the pages: only 303, with the keys UX-303 5.12 fixes, every descriptor valid, key 6 reserved, key 7 SKIN, keys 20..24 retired") {
  Hands t;
  const app::ConfigView& v = t.h->runtime().context().config_view();
  REQUIRE(v.pages == 1);
  CHECK(std::string(v.page[0].title) == "303");
  const struct {
    std::uint16_t key;
    const char* label;
    bool value;
  } expect[] = {{1, "AUTOSAVE", true}, {2, "KEYS AT BOOT", true}, {3, "LADDER SLEW", true}, {4, "PAD LEGEND", true}, {5, "RESET PROJECT", false}, {7, "SKIN", true}};
  REQUIRE(v.page[0].valid() == 6);
  for (unsigned i = 0; i < v.page[0].valid(); ++i) {
    const app::ConfigItem* it = v.page[0].nth(i);
    REQUIRE(it != nullptr);
    INFO(expect[i].label);
    CHECK(it->key == expect[i].key);
    CHECK(std::string(it->label) == expect[i].label);
    CHECK((it->value != nullptr) == expect[i].value);
    CHECK(std::strlen(it->label) <= app::kConfigLabelChars);
    if (it->value != nullptr) {
      CHECK(core::param_desc_valid(*it->value));
    }
  }
  CHECK(v.values == 5);
  CHECK(find_item(v, 6) == nullptr);  // reserved: SEQ MIDI OUT, deferred (D16)
  for (std::uint16_t retired = 20; retired <= 24; ++retired) {
    CHECK_MESSAGE(find_item(v, retired) == nullptr, "key " << retired << " is retired (the page DEEP, docs/MODS-PLAN.md 7)");
  }
  CHECK(config::kAutosave == 1);
  CHECK(config::kResetProject == 5);
  CHECK(config::kSkin == 7);
}

TEST_CASE("a fresh boot reads the 303 page's defaults") {
  Hands t;
  const app::ConfigView& v = t.h->runtime().context().config_view();
  // The 303 page's defaults: 10 S, PLAY, 30 ms, ON, CLASSIC.
  CHECK(t.config_get(config::kAutosave) == 1.0f);
  CHECK(t.config_get(config::kKeysAtBoot) == 0.0f);
  CHECK(t.config_get(config::kLadderSlew) == 30.0f);
  CHECK(t.config_get(config::kPadLegend) == 1.0f);
  CHECK(t.config_get(config::kSkin) == 0.0f);
  for (std::uint16_t key : {config::kAutosave, config::kKeysAtBoot, config::kLadderSlew, config::kPadLegend, config::kSkin}) {
    const app::ConfigItem* it = find_item(v, key);
    REQUIRE(it != nullptr);
    CHECK(t.config_get(key) == it->value->def);
  }
}

TEST_CASE("AUTOSAVE's places and periods") {
  CHECK(config::autosave_period_ms(config::kAutosaveOff) == 0);
  CHECK(config::autosave_period_ms(config::kAutosave10s) == 10'000);
  CHECK(config::autosave_period_ms(config::kAutosave30s) == 30'000);
  CHECK(config::autosave_period_ms(7) == 0);
  CHECK(config::autosave_choice(0) == config::kAutosaveOff);
  CHECK(config::autosave_choice(10'000) == config::kAutosave10s);
  CHECK(config::autosave_choice(1'000) == config::kAutosave10s);  // a test's short period reads as the first place
  CHECK(config::autosave_choice(30'000) == config::kAutosave30s);
  for (unsigned c = 0; c < config::kAutosaveChoices; ++c) {
    CHECK(config::autosave_choice(config::autosave_period_ms(c)) == c);
  }
}

TEST_CASE("AUTOSAVE: the period is the project service's, OFF writes nothing and ON writes what waited") {
  Hands t;
  t.config_set(config::kAutosave, 0.0f);
  CHECK(app_project().autosave_period() == 0);
  t.config_set(config::kAutosave, 2.0f);
  CHECK(app_project().autosave_period() == 30'000);
  CHECK(t.config_get(config::kAutosave) == 2.0f);
  t.config_set(config::kAutosave, 1.0f);
  CHECK(app_project().autosave_period() == 10'000);
  t.config_set(config::kAutosave, 9.0f);  // out of range: the last place
  CHECK(app_project().autosave_period() == 30'000);

  Session s;
  s.run(500);  // the boot: a card with nothing on it makes the session the project UNTITLED
  s.set(config::kAutosave, 0.0f);
  REQUIRE(seq::project_bank().set_note(0, 3, seq::project_bank().pattern(0).steps[3].note == 40u ? 41u : 40u));
  s.run(15'000);
  CHECK(app_project().saves() == 0);  // OFF: the change waits
  CHECK(app_project().changes_waiting());
  s.set(config::kAutosave, 1.0f);
  s.run(12'000);
  CHECK(app_project().saves() >= 1);  // ON: it is written
  CHECK(fs::exists(s.path("UNTITLED/live.o3a")));
}

TEST_CASE("PAD LEGEND turns the PADS line's text off on every screen, and back") {
  Hands t;
  CHECK(frame::pad_legend());
  t.config_set(config::kPadLegend, 0.0f);
  CHECK_FALSE(frame::pad_legend());
  CHECK(t.config_get(config::kPadLegend) == 0.0f);
  t.wait(50);
  CHECK(t.h->runtime().frame_failures() == 0u);
  t.config_set(config::kPadLegend, 1.0f);
  CHECK(frame::pad_legend());
}

TEST_CASE("SKIN: CLASSIC at start, OBSIDIAN on the player's choice, and HOME has no kit chrome while it is on (docs/SKIN-PLAN.md)") {
  Hands t;
  app::Context& ctx = t.h->runtime().context();
  const app::ConfigView& v = t.h->runtime().context().config_view();
  const app::ConfigItem* it = find_item(v, config::kSkin);
  REQUIRE(it != nullptr);
  REQUIRE(it->value != nullptr);
  CHECK(it->value->list_count == 2);
  CHECK(std::string(it->value->names[0]) == "CLASSIC");
  CHECK(std::string(it->value->names[1]) == "OBSIDIAN");
  CHECK_FALSE(skin::on());
  CHECK_FALSE(skin::active(ctx));
  CHECK(ctx.screen()->chrome(ctx) == app::Chrome::Full);
  CHECK(ctx.screen()->theme(ctx) == nullptr);
  t.skin_on();
  CHECK(skin::on());
  CHECK(t.config_get(config::kSkin) == 1.0f);
  CHECK(skin::active(ctx));
  CHECK(ctx.screen()->chrome(ctx) == app::Chrome::None);
  CHECK(ctx.screen()->theme(ctx) == &skin::theme());
  CHECK(t.h->runtime().frame_failures() == 0u);
  t.skin_off();
  CHECK_FALSE(skin::on());
  CHECK(ctx.screen()->chrome(ctx) == app::Chrome::Full);
  // A value out of range is the nearest place, and a new run starts at CLASSIC again (the app object outlives a run).
  t.config_set(config::kSkin, 7.0f);
  CHECK(skin::on());
  t.h = std::make_unique<sim::Harness>(o3sdk_app());
  t.boot();
  CHECK_FALSE(skin::on());
}

TEST_CASE("skin::active is false when the app's bitmaps are not in RAM_G: the whole app runs classic") {
  Hands t;
  t.skin_on();
  app::Context& up = t.h->runtime().context();
  REQUIRE(up.app_assets_ok());
  CHECK(skin::active(up));
  const app::Context failed;  // a context whose upload never happened (or failed): app_assets_ok() is false
  REQUIRE_FALSE(failed.app_assets_ok());
  CHECK(skin::on());
  CHECK_FALSE(skin::active(failed));
  CHECK(up.screen()->chrome(failed) == app::Chrome::Full);   // the screen draws classic, with the kit's chrome
  CHECK(up.screen()->theme(failed) == nullptr);
}

TEST_CASE("LADDER SLEW is the glide of a ladder tap: a longer one is still running after 60 ms, and the value is kept inside 10..100") {
  Hands t;
  seq::Sequencer& s = app_sequencer();
  const unsigned filt = role_pad(PadRole::Filt);
  const unsigned pad = app::pad_at(2, 10);
  t.pad(filt);
  t.pad(pad, 10);
  t.wait(60);
  CHECK(s.slewing() == 0);  // 30 ms, the default
  t.pad(app::pad_at(2, 0), 10);
  t.wait(200);
  t.config_set(config::kLadderSlew, 100.0f);
  CHECK(t.config_get(config::kLadderSlew) == 100.0f);
  t.pad(pad, 10);
  t.wait(60);
  CHECK(s.slewing() >= 1);
  t.wait(200);
  CHECK(s.slewing() == 0);
  t.config_set(config::kLadderSlew, 3.0f);
  CHECK(t.config_get(config::kLadderSlew) == 10.0f);
  t.config_set(config::kLadderSlew, 500.0f);
  CHECK(t.config_get(config::kLadderSlew) == 100.0f);
  t.config_set(config::kLadderSlew, 55.0f);
  CHECK(t.config_get(config::kLadderSlew) == 55.0f);
}

TEST_CASE("the retired DEEP keys (20..24) do nothing: a set changes no parameter, a get reads nothing") {
  Hands t;
  const float fmt = t.param(kGrowl, Growl::FormantMix);
  const float tune = t.param(kGrowl, Growl::CombTune);
  const float fb = t.param(kGrowl, Growl::CombFeedback);
  const float mode = t.param(kDelay, units::Delay::Mode);
  const bool reset = app_sequencer().wobble().reset_on_note;
  for (std::uint16_t key = 20; key <= 24; ++key) {
    t.config_set(key, 0.0f);
    t.config_set(key, 0.77f);
  }
  t.wait(10);
  CHECK(t.param(kGrowl, Growl::FormantMix) == fmt);
  CHECK(t.param(kGrowl, Growl::CombTune) == tune);
  CHECK(t.param(kGrowl, Growl::CombFeedback) == fb);
  CHECK(t.param(kDelay, units::Delay::Mode) == mode);
  CHECK(app_sequencer().wobble().reset_on_note == reset);
}

TEST_CASE("RESET PROJECT: the factory line, the default sound and tempo, the other patterns fresh, undoable for the patterns; the name stays") {
  Hands t;
  const float cutoff0 = t.param(kVoice, AcidVoice::Cutoff);
  const unsigned tempo0 = t.h->engine().own_tempo();
  const Pattern factory = t.pat();  // A01 as the boot made it
  Pattern fresh;
  clear(fresh);
  // A jam: edits in A01 and pattern 3, another sound, tempo and growl.
  REQUIRE(bank().set_note(0, 5, factory.steps[5].note == 40u ? 41u : 40u));
  REQUIRE(bank().set_note(3, 2, 44));
  const Pattern jam0 = bank().pattern(0);
  const Pattern jam3 = bank().pattern(3);
  (void)t.h->engine().set_param(kVoice, AcidVoice::Cutoff, 900.0f);
  (void)t.h->engine().set_tempo(150'000);
  (void)t.h->engine().set_param(kGrowl, Growl::FormantMix, 0.2f);
  (void)t.h->engine().set_param(kDelay, units::Delay::Mode, 0.0f);
  t.tap(Button::Play);
  t.wait(300);
  REQUIRE(t.h->engine().playing());

  t.config_run(config::kResetProject);  // the kit calls it after the second F1; the transport stops first
  t.wait(100);
  CHECK_FALSE(t.h->engine().playing());
  CHECK(std::string(app_project().info().notice) == "PROJECT RESET");
  CHECK(std::memcmp(&bank().pattern(0), &factory, sizeof factory) == 0);
  CHECK(std::memcmp(&bank().pattern(3), &fresh, sizeof fresh) == 0);
  CHECK(bank().selected() == 0);
  CHECK(t.param(kVoice, AcidVoice::Cutoff) == doctest::Approx(cutoff0));
  CHECK(t.h->engine().own_tempo() == tempo0);
  CHECK(t.param(kGrowl, Growl::FormantMix) == doctest::Approx(1.0f));
  CHECK(t.param(kDelay, units::Delay::Mode) == doctest::Approx(1.0f));
  CHECK(std::string(app_project().name()) == "UNTITLED");
  // UNDO walks the patterns back, the newest edit first: A01's factory line, then pattern 3.
  REQUIRE(bank().can_undo());
  CHECK(bank().undo() == 0);
  CHECK(std::memcmp(&bank().pattern(0), &jam0, sizeof jam0) == 0);
  CHECK(bank().undo() == 3);
  CHECK(std::memcmp(&bank().pattern(3), &jam3, sizeof jam3) == 0);
  // Reset again from a stopped session: nothing playing to wait for.
  t.config_run(config::kResetProject);
  t.wait(10);
  CHECK(std::memcmp(&bank().pattern(3), &fresh, sizeof fresh) == 0);
}

TEST_CASE("RESET PROJECT with a card is a change like any other: autosave writes it, and the project keeps its name") {
  Session s;
  s.run(1500);
  REQUIRE(app_project().has_project());
  REQUIRE(seq::project_bank().set_note(2, 1, 43));
  s.run(3000);
  const std::uint32_t saves0 = app_project().saves();
  o3sdk_app().config_run(s.ctx(), config::kResetProject);
  s.run(4000);
  CHECK(app_project().saves() > saves0);
  CHECK(std::string(app_project().name()) == "UNTITLED");
  CHECK(std::string(app_project().info().notice) == "PROJECT RESET");
}

TEST_CASE("RESET PROJECT is never refused: it waits for an encode in progress (no torn image), then resets") {
  Session s;
  s.run(500);
  REQUIRE(seq::project_bank().set_note(2, 1, 43));
  Pattern fresh;
  clear(fresh);
  unsigned guard = 0;
  while (app_project().phase() != ProjectService::Phase::Encoding && guard++ < 5000) {
    s.run(1);
  }
  REQUIRE(app_project().phase() == ProjectService::Phase::Encoding);  // the autosave is reading the bank slice by slice
  o3sdk_app().config_run(s.ctx(), config::kResetProject);
  guard = 0;
  while (app_project().phase() == ProjectService::Phase::Encoding && guard++ < 1000) {
    CHECK(seq::project_bank().pattern(2).steps[1].note == 43);  // the bank is not touched while the image is being built
    s.run(1);
  }
  s.run(3000);
  CHECK(std::memcmp(&seq::project_bank().pattern(2), &fresh, sizeof fresh) == 0);
  CHECK(std::string(app_project().info().notice) == "PROJECT RESET");
}


TEST_CASE("the values are back after a reboot: AUTOSAVE, KEYS AT BOOT (TRNSP is on), LADDER SLEW, PAD LEGEND and SKIN") {
  Session s;
  s.set(config::kAutosave, 2.0f);
  s.set(config::kKeysAtBoot, 1.0f);
  s.set(config::kLadderSlew, 65.0f);
  s.set(config::kPadLegend, 0.0f);
  s.set(config::kSkin, 1.0f);
  s.settle();
  REQUIRE(fs::exists(s.path("system.o3s")));
  app_project().set_autosave_period(10'000);  // the service keeps its period across a simulated reboot: only the kit's file can bring 30 S back
  REQUIRE(s.h->reboot());
  s.run(200);
  CHECK(s.get(config::kAutosave) == 2.0f);
  CHECK(app_project().autosave_period() == 30'000);
  CHECK(s.get(config::kKeysAtBoot) == 1.0f);
  CHECK(s.get(config::kLadderSlew) == 65.0f);
  CHECK(s.get(config::kPadLegend) == 0.0f);
  CHECK_FALSE(frame::pad_legend());
  CHECK(s.get(config::kSkin) == 1.0f);
  CHECK(skin::on());
  CHECK(skin::active(s.ctx()));
  // TRNSP is on at the boot (the TRNSP pad's lamp, on HOME) ...
  const unsigned trnsp = role_pad(PadRole::Trnsp);
  CHECK(s.h->leds().level(trnsp) == app::kLedOn);
  // ... and KEYS AT BOOT is only about the boot: changing it now leaves the keyboard as it is.
  s.set(config::kKeysAtBoot, 0.0f);
  s.run(50);
  CHECK(s.h->leds().level(trnsp) == app::kLedOn);
  // A third boot with the setting back at PLAY: TRNSP is off.
  s.settle();
  REQUIRE(s.h->reboot());
  s.run(200);
  CHECK(s.get(config::kKeysAtBoot) == 0.0f);
  CHECK(s.h->leds().level(trnsp) == app::kLedLow);
}

TEST_CASE("a system.o3s that still holds the retired keys 20..24 loads, drops them with the engine's values untouched, and leaves them out at its next save") {
  Session s;
  s.run(500);
  // The project owns the sound: the growl's and the delay's values (what DEEP used to set) are the project's, written by the autosave.
  REQUIRE(s.h->engine().set_param(kGrowl, Growl::FormantMix, 0.2f));
  REQUIRE(s.h->engine().set_param(kGrowl, Growl::CombTune, 5.0f));
  REQUIRE(s.h->engine().set_param(kGrowl, Growl::CombFeedback, -0.4f));
  REQUIRE(s.h->engine().set_param(kDelay, units::Delay::Mode, 0.0f));
  s.settle();
  const Bytes live = s.read("UNTITLED/live.o3a");
  REQUIRE_FALSE(live.empty());
  // An older firmware's settings file: AUTOSAVE 30 S (key 1, still the page's), and DEEP's five keys with values the project does not have.
  app::KitValues kv;
  kv.app_count = 6;
  const std::uint16_t keys[6] = {1, 20, 21, 22, 23, 24};
  const float vals[6] = {2.0f, 0.9f, -7.0f, 0.5f, 0.0f, 1.0f};
  for (unsigned i = 0; i < 6; ++i) {
    kv.app_key[i] = keys[i];
    kv.app_value[i] = vals[i];
  }
  std::uint8_t file[app::settings::kMaxBytes];
  const std::size_t n = app::settings::write(kv, file, sizeof file);
  REQUIRE(n > 0);
  s.write("system.o3s", Bytes(file, file + n));
  REQUIRE(s.h->reboot());
  s.run(1500);
  CHECK(app_project().has_project());  // the file with the retired keys did not keep the project from loading
  CHECK(s.get(config::kAutosave) == 2.0f);  // the key the page still has is applied
  const app::ConfigView& v = s.ctx().config_view();
  for (std::uint16_t retired = 20; retired <= 24; ++retired) {
    CHECK(find_item(v, retired) == nullptr);
  }
  CHECK(s.param(kGrowl, Growl::FormantMix) == doctest::Approx(0.2f));  // the engine keeps the project's values: the file's 0.9, -7, 0.5, 0, 1 were not applied
  CHECK(s.param(kGrowl, Growl::CombTune) == doctest::Approx(5.0f));
  CHECK(s.param(kGrowl, Growl::CombFeedback) == doctest::Approx(-0.4f));
  CHECK(s.param(kDelay, units::Delay::Mode) == doctest::Approx(0.0f));
  CHECK_FALSE(app_sequencer().wobble().reset_on_note);  // WOB RESET defaults off (B5a)
  // The next save of the settings has no retired key.
  s.set(config::kPadLegend, 0.0f);
  s.settle();
  const Bytes saved = s.read("system.o3s");
  REQUIRE_FALSE(saved.empty());
  app::KitValues back;
  app::settings::Report rep;
  REQUIRE(app::settings::read(saved.data(), saved.size(), back, rep) == app::settings::Result::Ok);
  CHECK(back.app_count == 5);
  for (unsigned i = 0; i < back.app_count; ++i) {
    CHECK(back.app_key[i] < 20);
  }
}
