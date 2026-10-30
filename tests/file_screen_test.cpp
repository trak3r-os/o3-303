// SPDX-License-Identifier: GPL-3.0-or-later
// FILE PROJECTS (app/src/file_screen.h, docs/UX-303.md 5.8 and 8.5, docs/UX-PLAN.md F3) on the app in the simulator with a scratch card: the keys and the pads, SAVE
// AS and NEW on the kit's keyboard, LOAD (the transport stops, the current project is autosaved first), REVERT and DELETE twice, DELETE of the current project
// refused, a damaged file that never replaces the session, a session with no project that is saved under a free name before a LOAD, and the NO CARD state that
// makes no file call. The view logic (the texts) is tested apart. The hands are local to this file.
#include <doctest/doctest.h>

#include "../app/src/file_screen.h"
#include "../app/src/project.h"
#include "../app/src/project_service.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/text_entry.h>
#include <o3sdk/formats/crc32.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/sim/harness.h>
#include <o3_303/seq/bank.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace o3sdk;
using namespace o3_303;
using app::Button;
namespace fs = std::filesystem;
namespace host = o3sdk::platform::host;
using Bytes = std::vector<std::uint8_t>;

namespace {

constexpr dsp::UnitId kVoice = 0;

seq::Bank& bank() { return seq::project_bank(); }

// The app on a scratch card (or none), started, with the hands to play it.
struct Session {
  host::ScratchDir dir{"o3303-f3"};
  host::DirCard card{dir.path()};
  std::unique_ptr<sim::Harness> h;

  explicit Session(bool with_card = true) {
    REQUIRE(dir.ok());
    h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, with_card ? static_cast<host::HostCard*>(&card) : nullptr, "o3_303");
    REQUIRE(h->start());
    app_project().set_autosave_period(1'000);  // (init() at setup made it the default again)
    h->run_ms(300);
  }
  void run(unsigned ms) { h->run_ms(ms); }
  app::Files& files() { return h->runtime().context().files(); }
  fs::path path(const std::string& rel) const { return fs::path(dir.path()) / "Apps" / "o3_303" / rel; }
  bool has(const std::string& rel) const { return fs::exists(path(rel)); }
  Bytes read(const std::string& rel) const {
    std::ifstream in(path(rel), std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  void write(const std::string& rel, const Bytes& b) const {
    std::ofstream out(path(rel), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
  }
  std::string text(const std::string& rel) const {
    const Bytes b = read(rel);
    return std::string(b.begin(), b.end());
  }
  void press(Button b) {
    h->press(b);
    h->run_ms(2);
  }
  void release(Button b) {
    h->release(b);
    h->run_ms(2);
  }
  void tap(Button b) {
    press(b);
    h->run_ms(40);
    release(b);
  }
  void shift_tap(Button b) {
    press(Button::Shift);
    tap(b);
    release(Button::Shift);
  }
  void pad(unsigned p) {
    h->pad_down(p);
    h->run_ms(40);
    h->pad_up(p);
    h->run_ms(2);
  }
  void turn(int d) {
    h->turn(d);
    h->run_ms(2);
  }
  // Runs until the service has nothing in flight (a request takes a few dozen passes), at most `limit_ms`.
  void settle(unsigned limit_ms = 3000) {
    for (unsigned i = 0; i < limit_ms && !(app_project().idle() && !files().busy() && !app_project().request_busy()); ++i) {
      h->run_ms(1);
    }
    h->run_ms(20);
  }
  const char* title() { return h->runtime().context().screen()->title(); }
  FileScreen& file_screen() {
    REQUIRE(std::strcmp(title(), "FILE PROJECTS") == 0);
    return *static_cast<FileScreen*>(h->runtime().context().screen());
  }
  app::SoftKeys keys() {
    app::SoftKeys k;
    h->runtime().context().screen()->soft_keys(h->runtime().context(), k);
    return k;
  }
  // The keyboard's pads for `text` (lower case letters, digits, space), then OK.
  void type_name(const char* text) {
    for (const char* c = text; *c != '\0'; ++c) {
      unsigned key = app::kTextKeys;
      for (unsigned k = 0; k < app::kTextKeys; ++k) {
        if (app::kTextKeysSmall[k] == *c) {
          key = k;
          break;
        }
      }
      REQUIRE(key < app::kTextKeys);
      unsigned p = 0;
      while (app::kTextPadKey[p] != key) {
        ++p;
      }
      pad(p);
    }
  }
  void to_file() {
    if (std::strcmp(title(), "FILE PROJECTS") != 0) {
      tap(Button::File);
      if (std::strcmp(title(), "FILE PROJECTS") != 0) {
        tap(Button::File);  // it was the library
      }
    }
    run(300);  // the list is read
  }
  void to_home() { tap(Button::Pattern); }
  float cutoff() { return h->engine().param(kVoice, static_cast<std::uint16_t>(AcidVoice::Cutoff)); }  // Hz
  void set_cutoff(float v) { (void)h->engine().set_param(kVoice, static_cast<std::uint16_t>(AcidVoice::Cutoff), v); }
  // SAVE AS `name` from FILE: the keyboard, the name, OK.
  void save_as(const char* name) {
    to_file();
    tap(Button::F3);
    REQUIRE(file_screen().naming());
    type_name(name);
    tap(Button::F7);
    settle();
  }
  void select(const char* name) {
    FileScreen& f = file_screen();
    turn(-60);  // the top of the list, then down
    for (int i = 0; i < 60 && std::strcmp(f.selected(), name) != 0; ++i) {
      turn(1);
    }
    REQUIRE(std::strcmp(f.selected(), name) == 0);
  }
  const char* notice() { return app_project().info().notice; }
};

void edit_note(unsigned p, unsigned step) {
  const unsigned n = bank().pattern(p).steps[step].note;
  REQUIRE(bank().set_note(p, step, n == 40u ? 41u : 40u));
}

}  // namespace

TEST_CASE("file: the texts say what the line shows") {
  char t[40];
  (void)ago_text(0, t, sizeof t);
  CHECK(std::string(t) == "JUST NOW");
  (void)ago_text(12'400, t, sizeof t);
  CHECK(std::string(t) == "12 S AGO");
  (void)ago_text(125'000, t, sizeof t);
  CHECK(std::string(t) == "2 MIN AGO");
  (void)ago_text(7'300'000, t, sizeof t);
  CHECK(std::string(t) == "2 H AGO");
  char small[5];
  (void)ago_text(12'400, small, sizeof small);
  CHECK(std::string(small) == "12 S");  // cut to the buffer, NUL ended

  SaveState s;
  CHECK(saved_text(s, t, sizeof t) == 0u);  // no card: nothing to say
  s.card = true;
  CHECK(std::string(t, saved_text(s, t, sizeof t)) == "NOT ON THE CARD YET");
  s.autosave = true;
  CHECK(std::string(t, saved_text(s, t, sizeof t)) == "NOTHING WRITTEN YET");
  s.ever = true;
  s.ago_ms = 12'000;
  CHECK(std::string(t, saved_text(s, t, sizeof t)) == "AUTOSAVED 12 S AGO");
  s.failed = true;
  CHECK(std::string(t, saved_text(s, t, sizeof t)) == "NOT SAVED");
  s.saving = true;
  CHECK(std::string(t, saved_text(s, t, sizeof t)) == "SAVING");

  CHECK(ProjectService::valid_name("FRIDAY JAM"));
  CHECK(ProjectService::valid_name("a-b_c 2"));
  CHECK_FALSE(ProjectService::valid_name(""));
  CHECK_FALSE(ProjectService::valid_name("a.b"));
  CHECK_FALSE(ProjectService::valid_name("Samples"));
  CHECK_FALSE(ProjectService::valid_name("ABCDEFGHIJKLMNOP"));  // 16 characters
  CHECK_FALSE(ProjectService::valid_name(nullptr));
}

TEST_CASE("file: no card, the list says so, F1..F6 are dim, and nothing is asked of the card") {
  Session s(false);
  s.tap(Button::File);
  FileScreen& f = s.file_screen();
  s.run(300);
  CHECK(f.last_view().card[0] != '\0');
  CHECK(std::string(f.last_view().card) == "NO CARD");
  const app::SoftKeys k = s.keys();
  CHECK((k.dim & 0x3Fu) == 0x3Fu);
  CHECK(std::string(k.label[0]) == "LOAD");
  CHECK(std::string(k.label[7]) == "LIBRARY");
  // Every key, a pad and the encoder: a toast, never a file call (the kit's status stays Idle: a refused call would leave it Failed).
  for (const Button b : {Button::F1, Button::F2, Button::F3, Button::F4, Button::F5, Button::F6, Button::F7}) {
    s.tap(b);
  }
  s.pad(3);
  s.turn(2);
  CHECK_FALSE(f.naming());
  CHECK(std::string(f.toast()) == "NO CARD");
  CHECK(s.files().status().phase == app::Files::Status::Idle);
  CHECK(s.files().stats().jobs == 0u);
  CHECK_FALSE(app_project().request_busy());
  // F8 still goes to the library, and File again comes back.
  s.tap(Button::F8);
  CHECK(std::string(s.title()) == "FILE LIBRARY");
  s.tap(Button::File);
  CHECK(std::string(s.title()) == "FILE PROJECTS");
  CHECK(s.files().stats().jobs == 0u);
}

TEST_CASE("file: SAVE AS on the pads makes the project, cancel writes nothing, a taken name keeps the keyboard") {
  Session s;
  s.to_file();
  FileScreen& f = s.file_screen();
  CHECK(app_project().list_valid());
  CHECK(app_project().projects() == 0u);

  // Cancel (F8) writes nothing.
  s.tap(Button::F3);
  CHECK(f.naming());
  s.type_name("zzz");
  s.tap(Button::F8);
  CHECK_FALSE(f.naming());
  s.settle();
  CHECK_FALSE(s.has("zzz"));

  s.save_as("jam");
  CHECK_FALSE(f.naming());
  CHECK(s.has("jam/live.o3a"));
  CHECK(s.has("jam/project.o3a"));
  CHECK(s.text("last.txt") == "jam\n");
  CHECK(std::string(app_project().name()) == "jam");
  CHECK(app_project().has_project());
  s.run(300);
  REQUIRE(app_project().projects() == 1u);
  CHECK(std::string(app_project().project(0)) == "jam");
  CHECK(app_project().is_current("JAM"));  // any case
  CHECK(std::string(f.selected()) == "jam");

  // NEW under the same name: refused, the keyboard is still there with the text.
  s.tap(Button::F4);
  REQUIRE(f.naming());
  s.type_name("jam");
  s.tap(Button::F7);
  s.run(50);
  CHECK(f.naming());
  CHECK(std::string(f.toast()) == "NAME TAKEN");
  s.tap(Button::F8);
  CHECK_FALSE(f.naming());

  // File while the name is typed drops it and goes on to the library.
  s.tap(Button::F3);
  REQUIRE(f.naming());
  s.tap(Button::File);
  CHECK(std::string(s.title()) == "FILE LIBRARY");
  s.tap(Button::File);
  CHECK(std::string(s.title()) == "FILE PROJECTS");
  CHECK_FALSE(s.file_screen().naming());
}

TEST_CASE("file: NEW autosaves the current project first, then the factory line; LOAD brings the first back and stops the transport") {
  Session s;
  s.save_as("jam");
  edit_note(0, 3);
  edit_note(4, 1);
  s.set_cutoff(900.0f);
  const unsigned note3 = bank().pattern(0).steps[3].note;
  const unsigned note41 = bank().pattern(4).steps[1].note;
  const Pattern edited0 = bank().pattern(0);
  REQUIRE(s.h->engine().set_tempo(99'000));
  s.run(100);  // under the autosave period: only the flush before NEW can have written it
  s.h->engine().play();
  s.run(100);
  REQUIRE(s.h->engine().playing());

  s.to_file();
  s.tap(Button::F4);
  REQUIRE(s.file_screen().naming());
  s.type_name("two");
  s.tap(Button::F7);
  s.settle();
  CHECK_FALSE(s.h->engine().playing());  // D12
  CHECK(std::string(app_project().name()) == "two");
  CHECK(app_project().has_project());
  CHECK(std::string(s.notice()) == "NEW PROJECT");
  CHECK(s.has("two/live.o3a"));
  CHECK_FALSE(s.has("two/project.o3a"));  // NEW writes live.o3a and last.txt
  CHECK(s.text("last.txt") == "two\n");
  // jam's live.o3a holds the edits made less than an autosave period ago.
  project::ProjectState st;
  const Bytes jam = s.read("jam/live.o3a");
  REQUIRE(project::decode(jam.data(), jam.size(), st).status == project::Status::Ok);
  CHECK(std::memcmp(st.pats, &edited0, sizeof(Pattern)) == 0);
  CHECK(st.tempo_mbpm == 99'000u);
  // The session is the factory line under the new name.
  CHECK(bank().pattern(0).steps[3].note != note3);
  CHECK(s.cutoff() != doctest::Approx(900.0f));
  CHECK(s.h->engine().own_tempo() != 99'000u);
  CHECK_FALSE(bank().can_undo());

  // LOAD jam: its edits are back; the transport stops again.
  s.h->engine().play();
  s.run(100);
  s.to_file();
  REQUIRE(app_project().projects() == 2u);
  s.select("jam");
  s.tap(Button::F1);
  s.settle();
  CHECK_FALSE(s.h->engine().playing());
  CHECK(std::string(app_project().name()) == "jam");
  CHECK(std::string(s.notice()) == "LOADED");
  CHECK(bank().pattern(0).steps[3].note == note3);
  CHECK(bank().pattern(4).steps[1].note == note41);
  CHECK(s.cutoff() == doctest::Approx(900.0f).epsilon(0.001));
  CHECK(s.h->engine().own_tempo() == 99'000u);
  CHECK_FALSE(bank().can_undo());
  CHECK(s.text("last.txt") == "jam\n");
  CHECK(std::string(s.file_screen().selected()) == "jam");

  // And the new project's own edit was flushed before that LOAD: two/live.o3a has the factory line's cutoff and, after another LOAD, comes back whole.
  s.select("two");
  s.tap(Button::F1);
  s.settle();
  CHECK(std::string(app_project().name()) == "two");
  CHECK(s.cutoff() != doctest::Approx(900.0f));
}

TEST_CASE("file: LOAD autosaves a session with no project on the card under a free UNTITLED name; nothing is lost") {
  Session s;
  s.save_as("keep");
  s.h->card_out();
  s.run(1'500);
  s.h->card_in();
  s.run(300);
  CHECK_FALSE(app_project().has_project());  // the card may be another one: no project until SAVE or a load
  edit_note(0, 5);
  const unsigned note = bank().pattern(0).steps[5].note;
  const Pattern edited0 = bank().pattern(0);

  s.to_file();
  s.select("keep");
  s.tap(Button::F1);
  s.settle();
  CHECK(std::string(app_project().name()) == "keep");
  CHECK(s.has("UNTITLED/live.o3a"));  // the edited session, saved before it was replaced
  project::ProjectState st;
  const Bytes saved = s.read("UNTITLED/live.o3a");
  REQUIRE(project::decode(saved.data(), saved.size(), st).status == project::Status::Ok);
  CHECK(std::memcmp(st.pats, &edited0, sizeof(Pattern)) == 0);
  CHECK(bank().pattern(0).steps[5].note != note);  // keep's own line is on the session now
}

TEST_CASE("file: REVERT needs two presses, goes back to the last SAVE, and live.o3a says so too") {
  Session s;
  s.save_as("jam");
  edit_note(0, 2);
  const unsigned saved_note = bank().pattern(0).steps[2].note;
  s.set_cutoff(1500.0f);
  s.run(100);
  s.press(Button::Shift);  // SAVE from FILE's own key would do: here the global one
  s.tap(Button::File);
  s.release(Button::Shift);
  s.settle();
  REQUIRE(std::string(s.notice()) == "SAVED");

  edit_note(0, 2);
  s.set_cutoff(500.0f);
  s.run(1'500);  // autosaved: live.o3a is the edited one
  s.settle();
  s.to_file();
  FileScreen& f = s.file_screen();
  s.tap(Button::F5);  // the first press arms
  CHECK(f.revert_armed());
  CHECK(std::string(s.keys().label[4]) == "REVERT?");
  CHECK(s.cutoff() == doctest::Approx(500.0f).epsilon(0.001));
  s.run(5'200);  // and times out
  CHECK_FALSE(f.revert_armed());
  CHECK(std::string(s.keys().label[4]) == "REVERT");
  CHECK(s.cutoff() == doctest::Approx(500.0f).epsilon(0.001));

  s.tap(Button::F5);
  s.tap(Button::F5);
  s.settle();
  CHECK(std::string(s.notice()) == "REVERTED");
  CHECK(s.cutoff() == doctest::Approx(1500.0f).epsilon(0.001));
  CHECK(bank().pattern(0).steps[2].note == saved_note);
  CHECK_FALSE(bank().can_undo());

  // A reboot brings the reverted session back, not the discarded work: live.o3a was written at once.
  REQUIRE(s.h->reboot());
  s.run(600);
  s.settle();
  CHECK(std::string(app_project().name()) == "jam");
  CHECK(s.cutoff() == doctest::Approx(1500.0f).epsilon(0.001));
  CHECK(bank().pattern(0).steps[2].note == saved_note);
}

TEST_CASE("file: REVERT of a project that was never saved says NOTHING SAVED YET") {
  Session s;
  s.save_as("jam");
  CHECK(s.has("jam/project.o3a"));
  // jam was saved by SAVE AS; take its project.o3a away (as if only autosave had ever written it).
  fs::remove(s.path("jam/project.o3a"));
  fs::remove(s.path("jam/project.o3a.bak"));
  s.set_cutoff(2000.0f);
  s.run(1'500);
  s.settle();
  s.to_file();
  s.tap(Button::F5);
  s.tap(Button::F5);
  s.settle();
  CHECK(std::string(s.notice()) == "NOTHING SAVED YET");
  CHECK(s.cutoff() == doctest::Approx(2000.0f).epsilon(0.001));  // untouched
}

TEST_CASE("file: a session with no project on the card has nothing to revert: said at the first press") {
  Session s;
  s.h->card_out();
  s.run(1'500);
  s.h->card_in();
  s.run(300);
  CHECK_FALSE(app_project().has_project());
  s.to_file();
  FileScreen& f = s.file_screen();
  s.tap(Button::F5);
  CHECK_FALSE(f.revert_armed());
  CHECK(std::string(f.toast()) == "NOTHING SAVED YET");
}

TEST_CASE("file: DELETE needs two presses on the same project, and the current project is refused") {
  Session s;
  s.save_as("one");
  s.tap(Button::F4);
  s.type_name("two");
  s.tap(Button::F7);
  s.settle();
  s.run(300);
  FileScreen& f = s.file_screen();
  REQUIRE(app_project().projects() == 2u);
  CHECK(std::string(app_project().name()) == "two");

  // The current project: refused at the first press, nothing armed, nothing removed.
  s.select("two");
  s.tap(Button::F6);
  CHECK(std::string(f.toast()) == "LOAD ANOTHER FIRST");
  CHECK_FALSE(f.delete_armed());
  s.tap(Button::F6);
  s.settle();
  CHECK_FALSE(s.has("two/.deleted"));
  CHECK(app_project().projects() == 2u);

  // Another one: the first press arms, moving the cursor disarms, two presses on the same one delete it.
  s.select("one");
  s.tap(Button::F6);
  CHECK(f.delete_armed());
  CHECK(std::string(s.keys().label[5]) == "DELETE?");
  s.turn(1);
  CHECK_FALSE(f.delete_armed());
  s.select("one");
  s.tap(Button::F6);
  s.tap(Button::F6);
  s.settle();
  s.run(300);
  CHECK(std::string(s.notice()) == "DELETED");
  CHECK(s.has("one/.deleted"));
  REQUIRE(app_project().projects() == 1u);
  CHECK(std::string(app_project().project(0)) == "two");
  CHECK(std::string(f.selected()) == "two");
  // The service refuses the current project on its own as well.
  CHECK_FALSE(app_project().remove("two"));
  CHECK(std::string(s.notice()) == "LOAD ANOTHER FIRST");
}

TEST_CASE("file: a damaged project never replaces the session; a damaged live.o3a loads the saved copy") {
  Session s;
  s.save_as("jam");
  edit_note(0, 6);
  const unsigned jam_note = bank().pattern(0).steps[6].note;
  s.shift_tap(Button::File);  // SAVE: project.o3a holds the edit
  s.settle();
  s.save_as("other");
  edit_note(0, 7);
  s.set_cutoff(1200.0f);
  const unsigned other_note = bank().pattern(0).steps[7].note;
  s.run(100);

  // Both files of jam damaged: LOAD says so and the session stays as it was (after the flush of "other").
  Bytes live = s.read("jam/live.o3a");
  const Bytes good_proj = s.read("jam/project.o3a");
  Bytes proj = good_proj;
  live[live.size() / 2] ^= 0x55u;
  proj[proj.size() / 2] ^= 0x55u;
  s.write("jam/live.o3a", live);
  s.write("jam/project.o3a", proj);
  s.to_file();
  s.select("jam");
  s.tap(Button::F1);
  s.settle();
  CHECK(std::string(s.notice()) == "PROJECT DAMAGED");
  CHECK(std::string(app_project().name()) == "other");
  CHECK(bank().pattern(0).steps[7].note == other_note);
  CHECK(s.cutoff() == doctest::Approx(1200.0f).epsilon(0.001));
  CHECK(s.read("jam/live.o3a") == live);  // never written over
  CHECK(s.read("jam/project.o3a") == proj);

  // Only live.o3a damaged: project.o3a is the copy that loads.
  s.write("jam/project.o3a", good_proj);
  s.select("jam");
  s.tap(Button::F1);
  s.settle();
  CHECK(std::string(app_project().name()) == "jam");
  CHECK(std::string(s.notice()) == "LOADED THE SAVED COPY");
  CHECK(bank().pattern(0).steps[6].note == jam_note);
}

TEST_CASE("file: a project from a newer o3-303 is refused and the session stays") {
  Session s;
  s.save_as("jam");
  Bytes live = s.read("jam/live.o3a");
  // HEAD's version (the u16 after its 8 byte chunk header): major 2.
  std::size_t pos = formats::chunk::kHeaderBytes;
  while (pos + 8 <= live.size() && std::memcmp(&live[pos], "HEAD", 4) != 0) {
    pos += 8 + ((std::size_t{live[pos + 4]} << 24) | (std::size_t{live[pos + 5]} << 16) | (std::size_t{live[pos + 6]} << 8) | live[pos + 7]);
  }
  REQUIRE(pos + 10 <= live.size());
  live[pos + 8] = 2;
  const std::size_t body = live.size() - formats::chunk::kCrcChunkBytes;
  const std::uint32_t crc = formats::crc32(live.data(), body);
  for (unsigned i = 0; i < 4; ++i) {
    live[live.size() - 4 + i] = static_cast<std::uint8_t>(crc >> (24 - 8 * i));
  }
  s.write("jam/live.o3a", live);
  fs::remove(s.path("jam/project.o3a"));
  fs::remove(s.path("jam/project.o3a.bak"));
  s.save_as("other");
  s.set_cutoff(700.0f);
  s.to_file();
  s.select("jam");
  s.tap(Button::F1);
  s.settle();
  CHECK(std::string(s.notice()) == "PROJECT FROM A NEWER O3-303");
  CHECK(std::string(app_project().name()) == "other");
  CHECK(s.cutoff() == doctest::Approx(700.0f).epsilon(0.001));
  CHECK(s.read("jam/live.o3a") == live);
}

TEST_CASE("file: the pads select the visible rows, the encoder and the arrows move the cursor; the frames fit") {
  Session s;
  s.save_as("aaa");
  for (const char* n : {"bbb", "ccc"}) {
    s.tap(Button::F4);
    s.type_name(n);
    s.tap(Button::F7);
    s.settle();
    s.run(300);
  }
  REQUIRE(app_project().projects() == 3u);
  FileScreen& f = s.file_screen();
  s.pad(0);
  CHECK(f.cursor() == 0u);
  s.pad(2);
  CHECK(f.cursor() == 2u);
  s.pad(30);  // past the list: nothing
  CHECK(f.cursor() == 2u);
  s.tap(Button::Up);
  CHECK(f.cursor() == 1u);
  s.turn(1);
  CHECK(f.cursor() == 2u);
  s.turn(5);  // clamped at the end, no wrap
  CHECK(f.cursor() == 2u);
  s.tap(Button::Down);
  CHECK(f.cursor() == 2u);
  s.turn(-9);
  CHECK(f.cursor() == 0u);
  CHECK(s.h->runtime().frame_failures() == 0u);
  CHECK(s.h->display().render_failures() == 0u);
  CHECK(s.h->display().peak_frame_words() < 1800u);
}

TEST_CASE("file: a refused name says the service's reason and keeps the keyboard (NAME USED BEFORE, TOO MANY PROJECTS)") {
  Session s;
  s.save_as("one");
  s.tap(Button::F4);
  s.type_name("two");
  s.tap(Button::F7);
  s.settle();
  s.run(300);
  FileScreen& f = s.file_screen();
  s.select("one");
  s.tap(Button::F6);
  s.tap(Button::F6);
  s.settle();
  s.run(300);
  REQUIRE(std::string(s.notice()) == "DELETED");

  s.tap(Button::F4);  // NEW under the deleted name
  REQUIRE(f.naming());
  s.type_name("one");
  s.tap(Button::F7);
  s.run(50);
  CHECK(f.naming());
  CHECK(std::string(f.toast()) == "NAME USED BEFORE: PICK ANOTHER");
  CHECK(std::string(app_project().name()) == "two");  // nothing was wiped
  s.tap(Button::F8);

  // A card with more projects than the list holds: no name can be told free.
  for (unsigned i = 0; i < 50; ++i) {
    char name[8];
    std::snprintf(name, sizeof name, "q%02u", i);
    fs::create_directories(s.path(name));
  }
  s.tap(Button::File);  // LIBRARY and back: the list is read again
  s.tap(Button::File);
  s.run(500);
  REQUIRE(app_project().list_truncated());
  s.tap(Button::F3);
  REQUIRE(f.naming());
  s.type_name("zz");
  s.tap(Button::F7);
  s.run(50);
  CHECK(f.naming());
  CHECK(std::string(f.toast()) == "TOO MANY PROJECTS");
}
