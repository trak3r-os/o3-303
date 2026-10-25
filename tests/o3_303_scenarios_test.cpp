// SPDX-License-Identifier: GPL-3.0-or-later
// Every scenario of app/scenarios runs as it is: each command is understood, every `snap` writes its picture, every `wav` its file, and no
// frame of the whole run fails to fit the display list (2048 words). The pictures are looked at by a person (docs/M3-PLAN.md); this is
// the gate that keeps them reproducible.
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/sim/harness.h>
#include <o3sdk/sim/scenario.h>

#include <algorithm>
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

// The words after `snap` and `wav` on each line of a scenario.
void outputs(const std::string& text, std::vector<std::string>& snaps, std::vector<std::string>& wavs) {
  std::istringstream lines(text);
  for (std::string line; std::getline(lines, line);) {
    std::istringstream words(line.substr(0, line.find('#')));
    std::string cmd;
    std::string name;
    if (words >> cmd >> name) {
      if (cmd == "snap") {
        snaps.push_back(name);
      } else if (cmd == "wav") {
        wavs.push_back(name);
      }
    }
  }
}

}  // namespace

TEST_CASE("scenarios: there is one for each screen and gesture group") {
  const fs::path dir = O3_303_SCENARIO_DIR;
  for (const char* name : {"boot", "write", "edit", "pages", "tools", "patterns", "perform", "vault", "long"}) {
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

  const fs::path out = fs::temp_directory_path() / "o3_303_scenarios_test";
  for (const fs::path& file : files) {
    INFO(file.filename().string());
    auto h = std::make_unique<sim::Harness>(o3sdk_app());
    REQUIRE(h->start());
    const std::string text = slurp(file);
    REQUIRE_FALSE(text.empty());
    const fs::path sub = out / file.stem();
    fs::remove_all(sub);
    fs::create_directories(sub);
    const sim::ScenarioResult r = sim::run_scenario(*h, text, sub.string());
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
    CHECK(h->display().peak_frame_words() < 2048u);
    CHECK(h->engine().dropped_commands() == 0u);
    fs::remove_all(sub);
  }
  fs::remove_all(out);
}
