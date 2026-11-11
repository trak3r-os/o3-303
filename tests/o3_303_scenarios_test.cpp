// SPDX-License-Identifier: GPL-3.0-or-later
// Every scenario of app/scenarios runs as it is: each command is understood, every `snap` writes its picture, every `wav` its file, and no
// frame of the whole run fails to fit the display list (UX-303 section 4: under 1,800 of its 2048 words). The pictures are looked at by a person (docs/M3-PLAN.md); this is
// the gate that keeps them reproducible.
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/system_config.h>
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

// O3_303_SCENARIO_SHARD="i/n" (tests/CMakeLists.txt runs six ctests): this process takes the i-th of n shares. Unset: everything.
// O3_303_SCENARIO_LIST=<file>: instead of running, append the name of each scenario this share takes to that file (o3_303_shard_coverage).
struct Shard {
  unsigned i = 0;
  unsigned n = 1;
  Shard() {
    const char* v = std::getenv("O3_303_SCENARIO_SHARD");
    unsigned a = 0;
    unsigned b = 0;
    if (v != nullptr && std::sscanf(v, "%u/%u", &a, &b) == 2 && b >= 1u && a < b) {
      i = a;
      n = b;
    }
  }
  bool first() const { return i == 0u; }  // the one-off TEST_CASEs run in shard 0 only
};

std::string slurp(const fs::path& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string slurp_bytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream bytes;
  bytes << in.rdbuf();
  return bytes.str();
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
// The kit's system strip (a notice such as LEARNED CC 75 CHANNEL 1, up for 3 s) is drawn last by the runtime, over the bottom 28 px of a screen that draws its own chrome. The skin leaves out what
// it covers (Context::strip_area(), Panel::clear), so a frame under a strip meets the same budgets as any other. A snap named strip_* is a frame taken under a strip: the runtime's strip must be
// up in it, and in every other snap it must not be, so no strip frame passes for a plain one.
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
      const bool strip_snap = name.rfind("strip_", 0) == 0;
      CHECK_MESSAGE(h.runtime().system().strip_active() == strip_snap, name << (strip_snap ? ": a strip_* snap is taken under the kit's strip" : ": the kit's strip is up (name the snap strip_*)"));
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
  if (!Shard().first()) {
    return;
  }
  const fs::path dir = O3_303_SCENARIO_DIR;
  for (const char* name : {"boot", "write", "edit", "home", "instrument", "config_303", "tools", "patterns", "pattern_setup", "perform", "perform_pads", "perform_setup", "home_rows", "home_tools", "project", "file", "library", "record", "growl_wobble", "long", "skin_home", "help_skin", "song", "skin_song", "skin_perform", "skin_instrument", "skin_master", "skin_record", "skin_file", "manual_tour", "manual_screens", "modded", "mods", "mods_stock", "mods_stock_ab", "skin_modded", "skin_strip", "classic"}) {
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
  // The shard of a file is its sorted index mod n; the A/B pair mods_stock* is compared below, so those files all take the share of the first of them.
  const Shard shard;
  std::size_t stock_index = files.size();
  for (std::size_t k = 0; k < files.size() && stock_index == files.size(); ++k) {
    if (files[k].stem().string().rfind("mods_stock", 0) == 0) {
      stock_index = k;
    }
  }
  bool ran_stock = false;

  // A directory of this run's own (the process id): ctest runs from several engineers at once must not remove each other's pictures.
  const fs::path out = fs::temp_directory_path() / ("o3_303_scenarios_test_" + std::to_string(static_cast<long>(getpid())));
  const char* const list_file = std::getenv("O3_303_SCENARIO_LIST");
  for (std::size_t k = 0; k < files.size(); ++k) {
    const fs::path& file = files[k];
    const bool is_stock = file.stem().string().rfind("mods_stock", 0) == 0;
    if ((is_stock ? stock_index : k) % shard.n != shard.i) {
      continue;
    }
    if (list_file != nullptr) {  // list mode (the coverage guard, tests/CMakeLists.txt): name the scenario this share takes and run nothing
      std::ofstream(list_file, std::ios::app) << file.filename().string() << '\n';
      continue;
    }
    ran_stock = ran_stock || is_stock;
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
    if (file.stem().string().rfind("mods_stock", 0) == 0) {  // the A/B's two takes are compared below: keep each one's `stock.wav` (the directory goes with the scenario)
      const fs::path keep = out / (file.stem().string() + ".stock.wav");
      fs::remove(keep);
      fs::copy_file(sub / "stock.wav", keep);
    }
    fs::remove_all(sub);
  }
  // MODS-PLAN 5.11, 13: with STOCK on, every mod at an extreme (mods_stock_ab) is the stock voice (mods_stock) bit for bit: the two files are the same, byte for byte, and not "close".
  if (ran_stock) {  // both files of the pair ran in this shard
    const fs::path stock = out / "mods_stock.stock.wav";
    const fs::path stock_ab = out / "mods_stock_ab.stock.wav";
    REQUIRE(fs::exists(stock));
    REQUIRE(fs::exists(stock_ab));
    CHECK(fs::file_size(stock) == fs::file_size(stock_ab));
    CHECK(slurp_bytes(stock) == slurp_bytes(stock_ab));
  }
  fs::remove_all(out);
}

// The longest notice the strip takes, kStripChars - 1 characters: 10 px each on top of the strip's 800. Over HOME, PATTERN SETUP with F3 held, PERFORM and PERFORM SETUP, in the skin, every
// frame meets the same ceiling as a frame with no strip (docs/SKIN-PLAN.md 4.3). The notice is put up by the runtime, as a save or a learn does, and kept up by its hold.
TEST_CASE("scenarios: the longest strip notice over the skinned HOME, SETUP and PERFORM stays under the ceiling") {
  if (!Shard().first()) {
    return;
  }
  const std::string longest(app::SystemConfig::kStripChars - 1u, 'M');  // wide letters; the Data face is 10 px a character whatever they are
  const fs::path dir = fs::temp_directory_path() / ("o3_303_strip_notice_" + std::to_string(static_cast<long>(getpid())));
  fs::remove_all(dir);
  fs::create_directories(dir);
  auto h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr);
  REQUIRE(h->start());
  sim::ScenarioResult total;
  // The skin on, as the scenarios do, then HOME with no strip: the reference picture and its cost.
  run_skin_scenario(*h,
                    "wait 300\ntap config\ntap left\nwait 62\ntap down\ntap down\ntap down\ntap down\ntap down\nturn 4\ntap config\nwait 100\n"
                    "snap 00_home_plain\n",
                    dir.string(), total);
  const unsigned plain_px = h->raster().last_frame_peak_line_px();
  CHECK_FALSE(h->runtime().system().strip_active());
  h->runtime().system_strip(longest.c_str(), -1, 3000);
  run_skin_scenario(*h, "wait 100\nsnap strip_01_home_notice\n", dir.string(), total);
  const unsigned strip_px = h->raster().last_frame_peak_line_px();
  CHECK_MESSAGE(strip_px <= kSkinMaxLinePx, strip_px << " box px on a line with a 71 character notice over HOME (plain " << plain_px << ")");
  run_skin_scenario(*h,
                    "tap pattern\nwait 100\npress f3\nwait 100\nsnap strip_02_setup_f3_held_notice\nrelease f3\n"
                    "tap performance\nwait 100\nsnap strip_03_perform_notice\ntap performance\nwait 100\nsnap strip_04_perform_setup_notice\n",
                    dir.string(), total);
  CHECK_MESSAGE(total.ok, "line " << total.line << ": " << total.error);
  CHECK(h->runtime().frame_failures() == 0u);
  fs::remove_all(dir);
}
