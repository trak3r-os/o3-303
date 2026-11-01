// SPDX-License-Identifier: GPL-3.0-or-later
// Every scenario of app/scenarios runs as it is: each command is understood, every `snap` writes its picture, every `wav` its file, and no
// frame of the whole run fails to fit the display list (UX-303 section 4: under 1,800 of its 2048 words). The pictures are looked at by a person (docs/M3-PLAN.md); this is
// the gate that keeps them reproducible.
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/platform/host/fake_midi.h>
#include <o3sdk/sim/harness.h>
#include <o3sdk/sim/scenario.h>

#include "../app/src/skin/skin.h"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace o3sdk;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// The words after `snap` (or `boot`, the boot screen's picture) and `wav` on each line of a scenario.
void outputs(const std::string& text, std::vector<std::string>& snaps, std::vector<std::string>& wavs) {
  std::istringstream lines(text);
  for (std::string line; std::getline(lines, line);) {
    std::istringstream words(line.substr(0, line.find('#')));
    std::string cmd;
    std::string name;
    if (words >> cmd >> name) {
      if (cmd == "snap" || cmd == "boot") {
        snaps.push_back(name);
      } else if (cmd == "wav") {
        wavs.push_back(name);
      }
    }
  }
}

// The skin's budgets (docs/SKIN-PLAN.md 4.3): a skinned frame under 1,350 display-list words, at most 100 primitives and 4,800 box pixels on any one line. HELP over a skinned
// screen is not held to this (its frames keep the kit's 1,800).
constexpr std::size_t kSkinMaxWords = 1350;
constexpr unsigned kSkinMaxLinePrims = 100;
constexpr unsigned kSkinMaxLinePx = 4800;
// FILE LIBRARY with the skin on is the one screen that is not skinned (docs/SKIN-PLAN.md 2.4, 4.3 and SK7): its classic layout with the kit's header and soft-key row, in the obsidian colours,
// peaks at 1,481 words (the `library` scenario). A snap named library_* in a skin_* file must show it so (VaultScreen, Chrome::Full, the obsidian theme) and is held to this bound, and to the
// kit's 1,800 for the whole run, instead of the skin's three budgets and "is skinned".
constexpr std::size_t kLibraryMaxWords = 1500;

// The first word of a scenario line, without its comment.
std::string command_of(const std::string& line) {
  std::istringstream words(line.substr(0, line.find('#')));
  std::string cmd;
  words >> cmd;
  return cmd;
}

// Runs a skin scenario one snap at a time (the harness keeps its state between the pieces) and holds each snapped frame to the skin's budgets.
void run_skin_scenario(sim::Harness& h, const std::string& text, const std::string& dir, sim::ScenarioResult& total) {
  std::istringstream lines(text);
  std::string piece;
  auto run_piece = [&](const std::string& name) {
    if (piece.empty()) {
      return;
    }
    const std::size_t app_uses = h.raster().handle_app_uses();
    const sim::ScenarioResult r = sim::run_scenario(h, piece, dir);
    if (!r.ok && total.ok) {
      total = r;
    }
    total.snapshots += r.snapshots;
    piece.clear();
    // A snap named help_* must be the kit's HELP overlay (Shift + Config) over the skinned screen: the test checks that it is open. Its frames are the kit's, so they keep the kit's 1,800
    // words (checked for the whole run below) and not the skin's budgets. A skinned frame that is named help_* fails here instead of skipping its checks.
    const bool help_snap = name.rfind("help_", 0) == 0;
    if (help_snap) {
      CHECK_MESSAGE(h.runtime().help_open(), name << ": a help_* snap is the kit's HELP, and it is not open");
    }
    // A library_* snap is the fallback: FILE LIBRARY with the kit's chrome and the obsidian theme, under the fallback's own bound.
    const bool library_snap = name.rfind("library_", 0) == 0;
    if (library_snap) {
      app::Context& ctx = h.runtime().context();
      REQUIRE(ctx.screen() != nullptr);
      CHECK_MESSAGE(std::strcmp(ctx.screen()->title(), "FILE LIBRARY") == 0, name << ": a library_* snap is FILE LIBRARY, not " << ctx.screen()->title());
      CHECK_MESSAGE(ctx.screen()->chrome(ctx) == app::Chrome::Full, name << ": FILE LIBRARY keeps the kit's chrome");
      CHECK_MESSAGE(ctx.screen()->theme(ctx) == &o3_303::skin::theme(), name << ": FILE LIBRARY is drawn in the obsidian theme");
      const std::size_t words = h.display().last_frame_words();
      if (std::getenv("O3_303_SKIN_REPORT") != nullptr) {
        std::fprintf(stderr, "skin report: %s: %zu words (the fallback: its own bound %zu)\n", name.c_str(), words, kLibraryMaxWords);
      }
      CHECK_MESSAGE(words < kLibraryMaxWords, name << ": " << words << " words");
    }
    if (!name.empty() && !help_snap && !library_snap) {
      // The frames of this piece drew bitmaps of the app's own (handle 0, which only the skin's Canvas uses): the screen really was skinned. If the SKIN row moved and the scenario
      // left the skin off, every bound below would hold for a classic frame and prove nothing.
      CHECK_MESSAGE(h.raster().handle_app_uses() > app_uses, name << ": the frame is not skinned");
      const std::size_t words = h.display().last_frame_words();
      const unsigned prims = h.raster().last_frame_peak_line_prims();
      const unsigned px = h.raster().last_frame_peak_line_px();
      if (std::getenv("O3_303_SKIN_REPORT") != nullptr) {  // `O3_303_SKIN_REPORT=1 ctest -R scenarios -V` lists every skinned snap's cost
        std::fprintf(stderr, "skin report: %s: %zu words, %u prims, %u px\n", name.c_str(), words, prims, px);
      }
      CHECK_MESSAGE(words < kSkinMaxWords, name << ": " << words << " words");
      CHECK_MESSAGE(prims <= kSkinMaxLinePrims, name << ": " << prims << " primitives on a line");
      CHECK_MESSAGE(px <= kSkinMaxLinePx, name << ": " << px << " box px on a line");
    }
  };
  for (std::string line; std::getline(lines, line);) {
    piece += line + "\n";
    const std::string cmd = command_of(line);
    if (cmd == "snap" || cmd == "boot") {
      std::istringstream w(line.substr(0, line.find('#')));
      std::string c;
      std::string name;
      w >> c >> name;
      run_piece(name);
    }
  }
  run_piece("");
}

}  // namespace

TEST_CASE("scenarios: there is one for each screen and gesture group") {
  const fs::path dir = O3_303_SCENARIO_DIR;
  for (const char* name : {"boot", "write", "edit", "home", "instrument", "config_303", "tools", "patterns", "pattern_setup", "perform", "perform_pads", "perform_setup", "home_rows", "home_tools", "project", "file", "library", "record", "hooks", "long", "skin_home", "help_skin", "song", "skin_song", "skin_perform", "skin_instrument", "skin_master", "skin_record", "skin_file", "manual_tour", "manual_screens"}) {
    CHECK_MESSAGE(fs::exists(dir / (std::string(name) + ".txt")), name);
  }
}

TEST_CASE("scenarios: every one runs, writes its pictures and its WAV, and every frame fits") {
  const fs::path dir = O3_303_SCENARIO_DIR;
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".txt") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  REQUIRE(files.size() >= 9);

  // A directory of this run's own (the process id): ctest runs from several engineers at once must not remove each other's pictures.
  const fs::path out = fs::temp_directory_path() / ("o3_303_scenarios_test_" + std::to_string(static_cast<long>(getpid())));
  for (const fs::path& file : files) {
    INFO(file.filename().string());
    const std::string text = slurp(file);
    REQUIRE_FALSE(text.empty());
    // A scenario with a `midi` or `clock` line gets a MIDI device, as <app>_sim makes one (sim/README.md).
    std::unique_ptr<platform::host::FakeMidi> midi;
    if (text.rfind("midi ", 0) == 0 || text.find("\nmidi ") != std::string::npos || text.find("\nclock ") != std::string::npos) {
      midi = std::make_unique<platform::host::FakeMidi>();
    }
    // A scenario with a `card` line (the NO CARD snap pulls it) needs one, as <app>_sim does with --card: a scratch directory of its own, which outlives the harness.
    const bool needs_card = text.rfind("card ", 0) == 0 || text.find("\ncard ") != std::string::npos;
    std::unique_ptr<platform::host::ScratchDir> card_dir;
    std::unique_ptr<platform::host::DirCard> card;
    if (needs_card) {
      card_dir = std::make_unique<platform::host::ScratchDir>("o3303-scn");
      REQUIRE(card_dir->ok());
      card = std::make_unique<platform::host::DirCard>(card_dir->path());
    }
    auto h = needs_card ? std::make_unique<sim::Harness>(o3sdk_app(), nullptr, midi.get(), card.get(), "o3_303") : std::make_unique<sim::Harness>(o3sdk_app(), nullptr, midi.get());
    REQUIRE(h->start());
    const fs::path sub = out / file.stem();
    fs::remove_all(sub);
    fs::create_directories(sub);
    sim::ScenarioResult r;
    if (file.stem().string().rfind("skin_", 0) == 0) {
      run_skin_scenario(*h, text, sub.string(), r);
    } else {
      r = sim::run_scenario(*h, text, sub.string());
    }
    CHECK_MESSAGE(r.ok, "line " << r.line << ": " << r.error);

    std::vector<std::string> snaps;
    std::vector<std::string> wavs;
    outputs(text, snaps, wavs);
    CHECK(snaps.size() >= 1);
    for (const std::string& s : snaps) {
      CHECK_MESSAGE(fs::exists(sub / (s + ".png")), s);
    }
    for (const std::string& w : wavs) {
      CHECK_MESSAGE(fs::exists(sub / (w + ".wav")), w);
      if (fs::exists(sub / (w + ".wav"))) {
        CHECK(fs::file_size(sub / (w + ".wav")) > 44);  // more than the header
      }
    }
    CHECK(h->runtime().frame_failures() == 0u);
    CHECK(h->display().render_failures() == 0u);
    CHECK(h->display().peak_frame_words() < 1800u);
    CHECK(h->engine().dropped_commands() == 0u);
    fs::remove_all(sub);
  }
  fs::remove_all(out);
}
