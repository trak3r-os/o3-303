// SPDX-License-Identifier: GPL-3.0-or-later
// The project service (app/src/project_service.h, docs/UX-303.md 8.4 to 8.6, docs/UX-PLAN.md F2) on the app in the simulator, with a card that is a scratch
// directory: autosave after the period and not before, the change detection (a pattern edit, a knob), the 60 s cap, SAVE (Shift + File), the project back after
// a reboot, the boot rules (a load dropped when the player edited, a damaged or newer file never written over, a project whose patterns were skipped saved
// only after a change), the card rules (none: no file call; out, write protected: SAVE FAILED and a save when it is back) and the staged encode, byte for byte
// the one project::encode() makes.
#include <doctest/doctest.h>

#include "../app/src/project.h"
#include "../app/src/project_service.h"

#include <o3sdk/app/app.h>
#include <o3sdk/formats/chunk_file.h>
#include <o3sdk/formats/crc32.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/sim/harness.h>
#include <o3_303/pad_map.h>
#include <o3_303/seq/bank.h>

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
namespace chunk = o3sdk::formats::chunk;
using Bytes = std::vector<std::uint8_t>;

namespace {

constexpr dsp::UnitId kVoice = 0;

seq::Bank& bank() { return seq::project_bank(); }

// The app on a scratch card (or none), started.
struct Session {
  host::ScratchDir dir{"o3303-f2"};
  host::DirCard card{dir.path()};
  std::unique_ptr<sim::Harness> h;

  // `period_ms`: the autosave period the test runs with (a short one keeps the simulated time down: the clock's rules are autosave_test's).
  explicit Session(bool with_card = true, std::uint32_t period_ms = 1'000) {
    REQUIRE(dir.ok());
    h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, with_card ? static_cast<host::HostCard*>(&card) : nullptr, "o3_303");
    REQUIRE(h->start());
    app_project().set_autosave_period(period_ms);  // (init() at setup made it the default again)
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
  void shift_file() {
    h->press(Button::Shift);
    h->run_ms(2);
    h->press(Button::File);
    h->run_ms(2);
    h->release(Button::File);
    h->release(Button::Shift);
    h->run_ms(2);
  }
  // Runs until the service is idle again (a save takes a few dozen passes), at most `limit_ms`.
  void settle(unsigned limit_ms = 1000) {
    for (unsigned i = 0; i < limit_ms && !(app_project().idle() && !files().busy()); ++i) {
      h->run_ms(1);
    }
  }
};

// A change of pattern `p` that is not the one it has: the step's note.
void edit_note(unsigned p, unsigned step) {
  const unsigned n = bank().pattern(p).steps[step].note;
  REQUIRE(bank().set_note(p, step, n == 40u ? 41u : 40u));
}

// The image on the card decodes, and its patterns are the bank's.
void check_image(const Bytes& image, bool bank_equal) {
  project::ProjectState s;
  const project::Result r = project::decode(image.data(), image.size(), s);
  CHECK(r.status == project::Status::Ok);
  if (r.status != project::Status::Ok) {
    return;
  }
  CHECK(r.patterns_ok);
  CHECK(r.skipped == 0u);
  if (bank_equal) {
    for (unsigned i = 0; i < kPatternCount; ++i) {
      CHECK(std::memcmp(s.pats + i * sizeof(Pattern), &bank().pattern(i), sizeof(Pattern)) == 0);
    }
  }
}

// A chunk's offset in a file ("PATS": its header), 0 when it is not there.
std::size_t find_chunk(const Bytes& b, const char (&id)[5]) {
  std::size_t pos = chunk::kHeaderBytes;
  while (pos + 8 <= b.size()) {
    const std::size_t n = (std::size_t{b[pos + 4]} << 24) | (std::size_t{b[pos + 5]} << 16) | (std::size_t{b[pos + 6]} << 8) | b[pos + 7];
    if (std::memcmp(&b[pos], id, 4) == 0) {
      return pos;
    }
    pos += 8 + n + (n & 1u);
  }
  return 0;
}

// The CRC chunk made good again after a patch.
void fix_crc(Bytes& b) {
  const std::size_t body = b.size() - chunk::kCrcChunkBytes;
  const std::uint32_t crc = formats::crc32(b.data(), body);
  for (unsigned i = 0; i < 4; ++i) {
    b[b.size() - 4 + i] = static_cast<std::uint8_t>(crc >> (24 - 8 * i));
  }
}

}  // namespace

TEST_CASE("project service: no card, no file call, and autosave stays off") {
  Session s(false, 10'000);
  s.run(100);
  CHECK_FALSE(app_project().has_project());
  CHECK(std::string(app_project().name()) == "UNTITLED");
  edit_note(0, 3);
  s.run(12'000);
  CHECK(app_project().saves() == 0u);
  CHECK(s.files().stats().jobs == 0u);  // nothing was asked of the files service
  CHECK(s.files().status().phase == app::Files::Status::Idle);
  CHECK_FALSE(app_project().info().failed);

  s.shift_file();  // SAVE with no card: told, no call
  s.run(50);
  CHECK(std::string(app_project().info().notice) == "NO CARD");
  CHECK(s.files().stats().jobs == 0u);
  CHECK(s.files().status().phase == app::Files::Status::Idle);
}

TEST_CASE("project service: a card with nothing on it is a new UNTITLED that autosaves after the period, and writes last.txt") {
  Session s(true, 10'000);
  s.run(500);
  CHECK(app_project().has_project());
  CHECK(std::string(app_project().name()) == "UNTITLED");
  CHECK(app_project().info().autosave);
  // Nothing changed: nothing is written, however long it runs.
  s.run(12'000);
  CHECK(app_project().saves() == 0u);
  CHECK_FALSE(s.has("UNTITLED/live.o3a"));

  edit_note(0, 3);
  s.run(9'000);  // the period is 10 s after the last change
  CHECK(app_project().saves() == 0u);
  s.run(2'000);
  s.settle();
  CHECK(app_project().saves() == 1u);
  REQUIRE(s.has("UNTITLED/live.o3a"));
  CHECK_FALSE(s.has("UNTITLED/project.o3a"));  // an autosave writes the working file only
  CHECK(s.text("last.txt") == "UNTITLED\n");
  check_image(s.read("UNTITLED/live.o3a"), true);
  CHECK_FALSE(app_project().info().failed);
  CHECK_FALSE(app_project().info().saving);

  // No further change, no further save.
  s.run(12'000);
  CHECK(app_project().saves() == 1u);
}

TEST_CASE("project service: a knob turned is a change (the small chunks), and a pattern edit is one too") {
  Session s;
  s.run(500);
  (void)s.h->engine().set_param(kVoice, 0, 0.5f);  // cutoff, a SND entry
  s.run(2'500);  // checked once a second, then the period
  s.settle();
  CHECK(app_project().saves() == 1u);
  edit_note(1, 0);
  s.run(2'500);
  s.settle();
  CHECK(app_project().saves() == 2u);
  check_image(s.read("UNTITLED/live.o3a"), true);
}

TEST_CASE("project service: Shift + File saves live.o3a and project.o3a, both the same image, and SAVED") {
  Session s;
  s.run(500);
  edit_note(2, 5);
  s.shift_file();
  s.settle();
  REQUIRE(s.has("UNTITLED/project.o3a"));
  REQUIRE(s.has("UNTITLED/live.o3a"));
  CHECK(s.read("UNTITLED/project.o3a") == s.read("UNTITLED/live.o3a"));
  check_image(s.read("UNTITLED/project.o3a"), true);
  CHECK(std::string(app_project().info().notice) == "SAVED");
  CHECK(s.text("last.txt") == "UNTITLED\n");
  CHECK(app_project().saves() == 1u);
  // The edit that the save holds is not a change any more.
  s.run(4'000);
  CHECK(app_project().saves() == 1u);
}

TEST_CASE("project service: a reboot brings the project back, and nothing is written for it") {
  Session s;
  s.run(500);
  edit_note(0, 3);
  edit_note(5, 7);
  const unsigned note0 = bank().pattern(0).steps[3].note;
  const unsigned note5 = bank().pattern(5).steps[7].note;
  REQUIRE(s.h->engine().set_tempo(98'000));
  s.run(2'500);
  s.settle();
  REQUIRE(app_project().saves() == 1u);
  const Bytes before = s.read("UNTITLED/live.o3a");

  REQUIRE(s.h->reboot());
  // The factory line is in pattern 0 for a moment, and the file's replaces it within the first passes.
  s.run(500);
  s.settle();
  CHECK(app_project().has_project());
  CHECK(std::string(app_project().name()) == "UNTITLED");
  CHECK(bank().pattern(0).steps[3].note == note0);
  CHECK(bank().pattern(5).steps[7].note == note5);
  CHECK(s.h->engine().own_tempo() == 98'000u);
  CHECK_FALSE(bank().can_undo());  // a load is no edit of the player's

  // The load changed nothing the service would write: the file is as it was after a long quiet.
  s.run(4'000);
  CHECK(app_project().saves() == 0u);  // counted from init(): none since the reboot
  CHECK(s.read("UNTITLED/live.o3a") == before);
}

TEST_CASE("project service: SAVE AS names the session, last.txt follows, and the next boot loads that project") {
  Session s;
  s.run(500);
  CHECK_FALSE(app_project().save_as("bad.name"));
  CHECK_FALSE(app_project().save_as(""));
  CHECK_FALSE(app_project().save_as("Samples"));
  CHECK_FALSE(app_project().save_as("THIS NAME IS FAR TOO LONG"));
  edit_note(3, 2);
  REQUIRE(app_project().save_as("Jam 1"));
  s.run(5);
  s.settle();
  CHECK(std::string(app_project().name()) == "Jam 1");
  CHECK(s.has("Jam 1/project.o3a"));
  CHECK(s.has("Jam 1/live.o3a"));
  CHECK(s.text("last.txt") == "Jam 1\n");
  CHECK_FALSE(s.has("UNTITLED"));

  edit_note(3, 9);
  const unsigned n = bank().pattern(3).steps[9].note;
  s.run(2'500);
  s.settle();
  REQUIRE(s.h->reboot());
  s.run(500);
  s.settle();
  CHECK(std::string(app_project().name()) == "Jam 1");
  CHECK(bank().pattern(3).steps[9].note == n);
}

TEST_CASE("project service: a session that began without a project saves as the first free UNTITLED name (D11)") {
  Session s;
  s.run(500);
  edit_note(0, 1);
  s.shift_file();
  s.settle();
  REQUIRE(s.has("UNTITLED/live.o3a"));
  const Bytes first = s.read("UNTITLED/live.o3a");

  // A boot where the player edits before the load arrives drops the load: no project, and autosave is off.
  REQUIRE(s.h->reboot());
  edit_note(0, 2);
  s.run(4'000);
  s.settle();
  CHECK_FALSE(app_project().has_project());
  CHECK(app_project().saves() == 0u);
  CHECK(s.read("UNTITLED/live.o3a") == first);  // the card's UNTITLED was not touched

  // SAVE: SAVE AS, under a name that is free.
  s.shift_file();
  s.settle();
  CHECK(app_project().has_project());
  CHECK(std::string(app_project().name()) == "UNTITLED 2");
  CHECK(s.has("UNTITLED 2/project.o3a"));
  CHECK(s.read("UNTITLED/live.o3a") == first);
  CHECK(s.text("last.txt") == "UNTITLED 2\n");

  // And again from a session with no project: UNTITLED 3.
  REQUIRE(s.h->reboot());
  edit_note(0, 4);
  s.run(1'000);
  CHECK_FALSE(app_project().has_project());
  s.shift_file();
  s.settle();
  CHECK(std::string(app_project().name()) == "UNTITLED 3");
}

TEST_CASE("project service: a damaged live.o3a falls back to project.o3a; both damaged is a session with no project that is never written over") {
  Session s;
  s.run(500);
  edit_note(0, 3);
  s.shift_file();
  s.settle();
  const unsigned saved_note = bank().pattern(0).steps[3].note;
  edit_note(0, 3);  // the working file moves on from the SAVE
  s.run(2'500);
  s.settle();

  Bytes live = s.read("UNTITLED/live.o3a");
  live[live.size() / 2] ^= 0x55u;
  s.write("UNTITLED/live.o3a", live);
  REQUIRE(s.h->reboot());
  s.run(500);
  s.settle();
  CHECK(std::string(app_project().info().notice) == "PROJECT DAMAGED");
  CHECK(app_project().has_project());
  CHECK(bank().pattern(0).steps[3].note == saved_note);  // the last SAVE

  Bytes proj = s.read("UNTITLED/project.o3a");
  proj[proj.size() / 2] ^= 0x55u;
  s.write("UNTITLED/project.o3a", proj);
  const Bytes live_now = s.read("UNTITLED/live.o3a");
  REQUIRE(s.h->reboot());
  s.run(500);
  s.settle();
  CHECK_FALSE(app_project().has_project());
  edit_note(0, 3);
  s.run(4'000);
  CHECK(app_project().saves() == 0u);
  CHECK(s.read("UNTITLED/project.o3a") == proj);
  CHECK(s.read("UNTITLED/live.o3a") == live_now);
}

TEST_CASE("project service: a project from a newer o3-303 is never written over") {
  Session s;
  s.run(500);
  edit_note(0, 3);
  s.shift_file();
  s.settle();
  Bytes live = s.read("UNTITLED/live.o3a");
  live[20] = 2;  // HEAD's version: major 2
  fix_crc(live);
  s.write("UNTITLED/live.o3a", live);

  REQUIRE(s.h->reboot());
  s.run(500);
  s.settle();
  CHECK(std::string(app_project().info().notice) == "PROJECT FROM A NEWER O3-303");
  CHECK_FALSE(app_project().has_project());
  edit_note(0, 3);
  s.run(4'000);
  CHECK(app_project().saves() == 0u);
  CHECK(s.read("UNTITLED/live.o3a") == live);
}

TEST_CASE("project service: a project whose patterns were skipped loads and is saved over only once the player has changed something") {
  Session s;
  s.run(500);
  (void)s.h->engine().set_param(kVoice, 1, 0.8f);  // a sound that is not the default
  const float reso = s.h->engine().param(kVoice, 1);
  REQUIRE(reso != doctest::Approx(0.0f));
  edit_note(0, 3);
  s.shift_file();
  s.settle();
  Bytes live = s.read("UNTITLED/live.o3a");
  const std::size_t pats = find_chunk(live, "PATS");
  REQUIRE(pats != 0);
  live[pats + 8] = 2;  // PATS's version: major 2, this build skips it
  fix_crc(live);
  s.write("UNTITLED/live.o3a", live);

  REQUIRE(s.h->reboot());
  s.run(500);
  s.settle();
  CHECK(std::string(app_project().info().notice) == "PART OF THE PROJECT SKIPPED");
  CHECK(app_project().has_project());
  CHECK(s.h->engine().param(kVoice, 1) == doctest::Approx(reso));  // the rest of the project came
  s.run(4'000);
  CHECK(app_project().saves() == 0u);  // nothing the player did: the file keeps its patterns
  CHECK(s.read("UNTITLED/live.o3a") == live);

  edit_note(0, 4);  // the player acts
  s.run(2'500);
  s.settle();
  CHECK(app_project().saves() == 1u);
}

TEST_CASE("project service: a card out and back is a card that may be another: autosave off, no file call, until SAVE") {
  Session s;
  s.run(500);
  edit_note(0, 3);
  s.h->card_out();
  const std::uint32_t jobs = s.files().stats().jobs;
  s.run(2'500);
  CHECK(app_project().saves() == 0u);
  CHECK_FALSE(app_project().has_project());
  CHECK(std::string(app_project().info().notice) == "CARD CHANGED: SAVE TO KEEP THE PROJECT");
  CHECK(s.files().stats().jobs == jobs);  // no file call without a card

  s.h->card_in();
  edit_note(0, 4);
  s.run(2'500);
  CHECK(app_project().saves() == 0u);  // the card is back, but is it the same one? Nothing is written until SAVE
  CHECK_FALSE(s.has("UNTITLED/live.o3a"));
  s.shift_file();
  s.settle();
  CHECK(app_project().has_project());
  CHECK(app_project().saves() == 1u);
  check_image(s.read("UNTITLED/live.o3a"), true);
  edit_note(0, 5);  // and now autosave is on again
  s.run(2'500);
  s.settle();
  CHECK(app_project().saves() == 2u);
}

TEST_CASE("project service: a swapped card that holds a project of the same name is never written over; SAVE takes the next free name") {
  Session s;
  s.run(500);
  edit_note(0, 3);
  s.shift_file();
  s.settle();
  REQUIRE(s.has("UNTITLED/live.o3a"));

  // Another card: the same folder, someone else's jam.
  s.h->card_out();
  s.run(100);
  Bytes theirs = s.read("UNTITLED/live.o3a");
  theirs[theirs.size() / 3] ^= 0x01u;  // (no longer valid: any bytes will do, it must stay as it is)
  s.write("UNTITLED/live.o3a", theirs);
  s.write("UNTITLED/project.o3a", theirs);
  s.h->card_in();
  s.run(100);
  CHECK_FALSE(app_project().has_project());
  edit_note(0, 4);
  s.run(5'000);  // the period is 1 s here: plenty
  s.settle();
  CHECK(app_project().saves() == 1u);  // (the one before the swap)
  CHECK(s.read("UNTITLED/live.o3a") == theirs);
  CHECK(s.read("UNTITLED/project.o3a") == theirs);

  s.shift_file();
  s.settle();
  CHECK(std::string(app_project().name()) == "UNTITLED 2");
  CHECK(s.has("UNTITLED 2/live.o3a"));
  CHECK(s.read("UNTITLED/live.o3a") == theirs);
  CHECK(s.text("last.txt") == "UNTITLED 2\n");
}

TEST_CASE("project service: a write protected card is told, not written; the switch is a card change, so SAVE gives the session a project again") {
  Session s;
  s.run(500);
  edit_note(0, 3);
  s.h->card_protect(true);  // the kit takes the switch as a card change
  s.run(2'500);
  CHECK(app_project().saves() == 0u);
  CHECK_FALSE(app_project().has_project());
  CHECK(std::string(app_project().info().notice) == "CARD CHANGED: SAVE TO KEEP THE PROJECT");
  const std::uint32_t jobs = s.files().stats().jobs;
  s.shift_file();  // SAVE on a protected card: told, no file call
  s.run(50);
  CHECK(std::string(app_project().info().notice) == "CARD WRITE PROTECTED");
  CHECK(s.files().stats().jobs == jobs);
  s.h->card_protect(false);
  s.run(50);
  s.shift_file();
  s.settle();
  CHECK(app_project().saves() == 1u);
  CHECK(app_project().has_project());
  CHECK_FALSE(app_project().info().failed);
}

TEST_CASE("project service: a card pulled during a save fails it once, the old file stays whole, and a SAVE after the card is back saves") {
  Session s;
  s.run(500);
  edit_note(0, 3);
  s.shift_file();
  s.settle();
  edit_note(0, 4);
  s.shift_file();
  for (unsigned i = 0; i < 500 && app_project().phase() != ProjectService::Phase::Writing; ++i) {
    s.run(1);
  }
  REQUIRE(app_project().phase() == ProjectService::Phase::Writing);
  s.h->card_out();
  s.run(50);
  CHECK(app_project().idle());
  CHECK_FALSE(app_project().has_project());
  s.h->card_in();
  s.run(100);
  s.settle();
  // The card's file is the old one or the new one, whole.
  const Bytes now = s.read("UNTITLED/live.o3a");
  project::ProjectState st;
  CHECK(project::decode(now.data(), now.size(), st).status == project::Status::Ok);
  s.shift_file();  // the session has no project on the card now: a free name, UNTITLED being taken
  s.settle();
  CHECK_FALSE(app_project().info().failed);
  CHECK(std::string(app_project().name()) == "UNTITLED 2");
  check_image(s.read("UNTITLED 2/live.o3a"), true);
}

TEST_CASE("project service: a SAVE writes live.o3a before project.o3a, so a card pulled between them keeps the newest image where boot looks") {
  Session s;
  s.run(500);
  edit_note(0, 3);
  s.shift_file();
  s.settle();
  const Bytes first = s.read("UNTITLED/live.o3a");
  REQUIRE(s.read("UNTITLED/project.o3a") == first);
  edit_note(0, 4);
  s.shift_file();
  bool seen = false;  // a moment when live.o3a is the new image and project.o3a still the old one
  for (unsigned i = 0; i < 500 && !(app_project().idle() && i > 5); ++i) {
    s.run(1);
    if (s.read("UNTITLED/live.o3a") != first && s.read("UNTITLED/project.o3a") == first) {
      seen = true;
    }
  }
  CHECK(seen);
  s.settle();
  CHECK(s.read("UNTITLED/project.o3a") == s.read("UNTITLED/live.o3a"));
}

TEST_CASE("project service: the autosave period is the setting, OFF saves nothing until it is on again") {
  Session s;
  s.run(500);
  app_project().set_autosave_period(0);
  edit_note(0, 3);
  s.run(4'000);
  CHECK(app_project().saves() == 0u);
  app_project().set_autosave_period(3'000);  // the change is older than that: what waited is saved now
  s.run(300);
  s.settle();
  CHECK(app_project().saves() == 1u);
}

TEST_CASE("project service: the screens' fields say what happens") {
  Session s;
  s.run(500);
  CHECK(app_project().info().autosave);
  CHECK(std::string(app_project().info().name) == "UNTITLED");
  edit_note(0, 3);
  s.run(900);
  bool saw_saving = false;
  for (unsigned i = 0; i < 400 && !saw_saving; ++i) {
    saw_saving = app_project().info().saving;
    s.run(1);
  }
  CHECK(saw_saving);
  s.settle();
  CHECK_FALSE(app_project().info().saving);
}

TEST_CASE("project service: the staged encode is byte for byte project::encode(), spread over many steps") {
  project::ProjectState st;
  std::snprintf(st.name, sizeof st.name, "%s", "STAGED");
  st.tempo_mbpm = 91'250;
  st.sound_n = 2;
  st.sound[0] = project::SoundEntry{1, 0, 0.5f};
  st.sound[1] = project::SoundEntry{project::kSndApp, project::kAppSend, 12.0f};
  st.snap_used = 0x05;
  st.snap[0][3] = 0.25f;
  st.snap[2][7] = 0.75f;
  Bytes a(project::kProjectBytes);
  Bytes b(project::kProjectBytes);
  const std::size_t n = project::encode(st, bank(), a.data(), a.size());
  REQUIRE(n > 100'000);

  project::Encoder enc;
  enc.begin(st, bank(), b.data(), b.size());
  unsigned steps = 0;
  while (!enc.step(ProjectService::kPatternsPerPass, ProjectService::kCrcBytesPerPass)) {
    REQUIRE(++steps < 100);
  }
  CHECK(enc.done());
  CHECK_FALSE(enc.failed());
  CHECK(enc.size() == n);
  CHECK(steps >= 10);  // no step carries the image
  CHECK(std::memcmp(a.data(), b.data(), n) == 0);
  chunk::Reader r;
  CHECK(r.open(b.data(), enc.size(), "303P") == chunk::Error::None);

  // One step with everything is the same image.
  Bytes c(project::kProjectBytes);
  project::Encoder one;
  one.begin(st, bank(), c.data(), c.size());
  while (!one.step(kPatternCount, project::kProjectBytes)) {
  }
  CHECK(one.size() == n);
  CHECK(std::memcmp(a.data(), c.data(), n) == 0);

  // A buffer too small fails, and says so.
  Bytes tiny(1000);
  project::Encoder bad;
  bad.begin(st, bank(), tiny.data(), tiny.size());
  while (!bad.step(ProjectService::kPatternsPerPass, ProjectService::kCrcBytesPerPass)) {
  }
  CHECK(bad.failed());
  CHECK(bad.size() == 0u);
}

TEST_CASE("project service: small_crc follows the small chunks and ignores the patterns") {
  project::ProjectState a;
  const std::uint32_t base = project::small_crc(a);
  CHECK(project::small_crc(a) == base);
  project::ProjectState t = a;
  t.tempo_mbpm += 1;
  CHECK(project::small_crc(t) != base);
  project::ProjectState n = a;
  std::snprintf(n.name, sizeof n.name, "%s", "X");
  CHECK(project::small_crc(n) != base);
  project::ProjectState v = a;
  v.sound_n = 1;
  v.sound[0] = project::SoundEntry{1, 2, 0.5f};
  const std::uint32_t one = project::small_crc(v);
  CHECK(one != base);
  v.sound[0].value = 0.6f;
  CHECK(project::small_crc(v) != one);
  project::ProjectState sn = a;
  sn.snap[1][1] = 0.5f;
  CHECK(project::small_crc(sn) != base);
  project::ProjectState p = a;
  p.perf[0] = static_cast<std::uint8_t>(p.perf[0] + 1u);
  CHECK(project::small_crc(p) != base);
  // The patterns are not in it: an edit of one leaves it as it was.
  edit_note(0, 3);
  CHECK(project::small_crc(a) == base);
}

TEST_CASE("project service: a read that fails for any reason but NOT FOUND is never written over") {
  Session s;
  s.run(500);
  edit_note(0, 3);
  s.shift_file();
  s.settle();
  const Bytes live = s.read("UNTITLED/live.o3a");
  fs::permissions(s.path("UNTITLED/live.o3a"), fs::perms::none);  // the card cannot read it: an I/O error, not a missing file
  REQUIRE(s.h->reboot());
  s.run(500);
  s.settle();
  CHECK(std::string(app_project().info().notice) == "CARD READ FAILED: AUTOSAVE OFF");
  CHECK_FALSE(app_project().has_project());
  edit_note(0, 4);
  s.run(4'000);
  CHECK(app_project().saves() == 0u);
  fs::permissions(s.path("UNTITLED/live.o3a"), fs::perms::owner_read | fs::perms::owner_write);
  CHECK(s.read("UNTITLED/live.o3a") == live);  // untouched
}

TEST_CASE("project service: a boot load that is dropped says why, and autosave is off") {
  Session s;
  s.run(500);
  edit_note(0, 3);
  s.shift_file();
  s.settle();
  REQUIRE(s.h->reboot());
  edit_note(0, 5);  // before the load arrives
  s.run(500);
  CHECK(std::string(app_project().info().notice) == "AUTOSAVE OFF: EDITED BEFORE THE LOAD");
  CHECK_FALSE(app_project().has_project());

  REQUIRE(s.h->reboot());
  REQUIRE(s.h->engine().play());  // the transport runs before the load arrives
  s.run(500);
  CHECK(std::string(app_project().info().notice) == "AUTOSAVE OFF: PLAYING BEFORE THE LOAD");
  CHECK_FALSE(app_project().has_project());
}

TEST_CASE("project service: a name the kit refuses (a deleted project's hidden folder) goes on to the next free one") {
  Session s;
  s.run(500);
  fs::create_directories(s.path("UNTITLED"));
  { std::ofstream out(s.path("UNTITLED/.deleted")); }  // DELETE's marker: an empty file
  s.h->card_out();
  s.h->card_in();  // a card that is new to the session: no project
  s.run(100);
  CHECK_FALSE(app_project().has_project());
  edit_note(0, 3);
  s.shift_file();
  s.settle();
  CHECK(std::string(app_project().name()) == "UNTITLED 2");
  CHECK(s.has("UNTITLED 2/live.o3a"));
  CHECK(s.has("UNTITLED 2/project.o3a"));
  CHECK_FALSE(s.has("UNTITLED/live.o3a"));
  CHECK(s.text("last.txt") == "UNTITLED 2\n");
  CHECK_FALSE(app_project().info().failed);
}

TEST_CASE("project service: a new UNTITLED whose folder the card hides cannot autosave: no project, and a notice") {
  Session s(false);
  s.run(100);
  s.h.reset();  // (a card is made up by hand: the session starts on it)
  fs::create_directories(s.path("UNTITLED"));
  { std::ofstream out(s.path("UNTITLED/.deleted")); }
  s.h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &s.card, "o3_303");
  REQUIRE(s.h->start());
  app_project().set_autosave_period(1'000);
  s.run(500);
  CHECK(app_project().has_project());  // nothing to load: a new project (D25)
  edit_note(0, 3);
  s.run(2'500);
  s.settle();
  CHECK_FALSE(app_project().has_project());
  CHECK(std::string(app_project().info().notice) == "PROJECT FOLDER DELETED: SAVE TO KEEP");
  CHECK(app_project().info().failed);
  s.shift_file();
  s.settle();
  CHECK(std::string(app_project().name()) == "UNTITLED 2");
  CHECK_FALSE(app_project().info().failed);
}

TEST_CASE("project service: an edit while the image is being built is saved by the next autosave") {
  Session s;
  s.run(500);
  edit_note(0, 3);
  for (unsigned i = 0; i < 3'000 && app_project().phase() != ProjectService::Phase::Encoding; ++i) {
    s.run(1);
  }
  REQUIRE(app_project().phase() == ProjectService::Phase::Encoding);
  s.run(3);  // a few steps: pattern 0 is already in the image
  const unsigned before = bank().pattern(0).steps[3].note;
  edit_note(0, 3);
  const unsigned after = bank().pattern(0).steps[3].note;
  REQUIRE(after != before);
  s.settle();
  CHECK(app_project().saves() == 1u);
  {
    project::ProjectState st;
    const Bytes img = s.read("UNTITLED/live.o3a");
    REQUIRE(project::decode(img.data(), img.size(), st).status == project::Status::Ok);
    CHECK(st.pats[3 * sizeof(Step)] == before);  // the image holds what pattern 0 was when it was copied
  }
  CHECK(app_project().changes_waiting());  // and the edit is a change after that save
  s.run(2'500);
  s.settle();
  CHECK(app_project().saves() == 2u);
  check_image(s.read("UNTITLED/live.o3a"), true);
}

TEST_CASE("project service: Shift + File saves everywhere but on the vault, where it still loads the slot") {
  Session s;
  s.run(500);
  const auto* home = s.h->runtime().context().screen();
  s.h->press(Button::File);  // the vault
  s.h->run_ms(2);
  s.h->release(Button::File);
  s.h->run_ms(20);
  const auto* vault = s.h->runtime().context().screen();
  REQUIRE(vault != home);
  const std::uint32_t seq = app_project().info().notice_seq;
  s.shift_file();  // on the vault: the vault's own File, not SAVE
  s.run(300);
  CHECK(s.h->runtime().context().screen() == home);  // it loaded the slot and went back
  CHECK(app_project().info().notice_seq == seq);
  CHECK_FALSE(s.has("UNTITLED/project.o3a"));
  CHECK(app_project().saves() == 0u);
  s.shift_file();  // on HOME: SAVE
  s.settle();
  CHECK(s.has("UNTITLED/project.o3a"));
}
