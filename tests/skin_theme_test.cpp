// SPDX-License-Identifier: GPL-3.0-or-later
// The obsidian theme (app/src/skin/skin.cpp): the 17 tokens are every text colour of the skin (docs/SKIN-PLAN.md section 7), so they must hold every pair of ui::kContrastPairs, the
// same check the kit holds its three themes to (o3-sdk tests/theme_test.cpp). Also: what the kit draws in this theme (HELP, the system strip) reads on its own panels.
#include <doctest/doctest.h>

#include "../app/src/skin/skin.h"

#include <o3sdk/ui/style.h>
#include <o3sdk/ui/theme.h>

#include <cmath>
#include <cstdio>
#include <string>

using namespace o3_303;
namespace ui = o3sdk::ui;

namespace {

// WCAG 2.x relative luminance and contrast ratio.
double channel(std::uint8_t v) {
  const double c = static_cast<double>(v) / 255.0;
  return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double luminance(const ui::Rgb& c) { return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b); }
double contrast(const ui::Rgb& a, const ui::Rgb& b) {
  const double la = luminance(a);
  const double lb = luminance(b);
  return (std::fmax(la, lb) + 0.05) / (std::fmin(la, lb) + 0.05);
}

const char* kNames[ui::kColorCount] = {"Chrome", "ChromeText", "ChromeDim", "Pane", "Text", "TextDim", "Empty", "Instr", "Vol", "Fx", "Sel", "SelText", "Accent", "AccentText", "Play", "PlayText", "Rec"};

}  // namespace

TEST_CASE("obsidian theme: every pair of kContrastPairs meets its ratio") {
  const ui::Theme& t = skin::theme();
  for (const ui::ContrastPair& p : ui::kContrastPairs) {
    const double ratio = contrast(t.rgb(p.fg), t.rgb(p.bg));
    const std::string what = std::string(kNames[static_cast<unsigned>(p.fg)]) + " on " + kNames[static_cast<unsigned>(p.bg)] + ": " + std::to_string(ratio);
    INFO(what);
    switch (p.role) {
      case ui::PairRole::Text: CHECK(ratio >= static_cast<double>(ui::kMinTextContrast)); break;
      case ui::PairRole::Large: CHECK(ratio >= static_cast<double>(ui::kMinLargeContrast)); break;
      case ui::PairRole::Dim:
        CHECK(ratio >= static_cast<double>(ui::kDimContrastLow));
        CHECK(ratio <= static_cast<double>(ui::kDimContrastHigh));
        break;
    }
  }
}

TEST_CASE("obsidian theme: the skin's own text colours read where the skin draws them (silkscreen on the panel and the windows, values on the glass)") {
  const ui::Theme& t = skin::theme();
  const ui::Rgb& chrome = t.rgb(ui::Color::Chrome);
  const ui::Rgb& pane = t.rgb(ui::Color::Pane);
  // The header's texts sit on the panel's base or in a window (a recess a little darker than the base), the content's on the glass.
  const ui::Rgb& base = skin::pal::kBase;
  const ui::Rgb& window = skin::pal::kWindow;
  for (ui::Color c : {ui::Color::ChromeDim, ui::Color::ChromeText, ui::Color::Text, ui::Color::Instr, ui::Color::Accent, ui::Color::Rec}) {
    INFO(kNames[static_cast<unsigned>(c)]);
    CHECK(contrast(t.rgb(c), base) >= static_cast<double>(ui::kMinLargeContrast));
    CHECK(contrast(t.rgb(c), window) >= static_cast<double>(ui::kMinLargeContrast));
  }
  // The buttons' colours are text on the glass (the step view's captions and glyphs): 4.5:1 like any text.
  for (ui::Color c : {ui::Color::Vol, ui::Color::Accent, ui::Color::Fx, ui::Color::Instr, ui::Color::Text, ui::Color::TextDim}) {
    INFO(kNames[static_cast<unsigned>(c)]);
    CHECK(contrast(t.rgb(c), pane) >= static_cast<double>(ui::kMinTextContrast));
  }
  // The owned row's caption: dark ink on the button's colour.
  for (ui::Color c : {ui::Color::Vol, ui::Color::Accent, ui::Color::Fx, ui::Color::Instr}) {
    INFO(kNames[static_cast<unsigned>(c)]);
    CHECK(contrast(t.rgb(ui::Color::AccentText), t.rgb(c)) >= static_cast<double>(ui::kMinTextContrast));
  }
  // The kit fills active key caps and chips with Chrome (o3-sdk help_overlay.cpp), so Chrome must separate from the glass or HELP's caps vanish. 1.3:1 is the most the kit's own pairs
  // allow: Sel and Rec must be 3:1 on Chrome and 4.5:1 under white, which bounds Chrome's luminance from above.
  CHECK(contrast(chrome, pane) >= 1.3);
}

TEST_CASE("obsidian theme: a theme in the kit's own shape: lower case name, the kit's faces and metrics") {
  const ui::Theme& t = skin::theme();
  CHECK(std::string(t.name) == "obsidian");
  const ui::Theme& amiga = ui::theme(ui::ThemeId::Amiga);
  for (unsigned f = 0; f < ui::kFaceCount; ++f) {
    CHECK(t.face[f] == amiga.face[f]);  // the content is laid out on the kit's geometry
  }
  CHECK(t.metrics.border == amiga.metrics.border);
  CHECK(t.metrics.pad == amiga.metrics.pad);
  CHECK(t.metrics.gap == amiga.metrics.gap);
  CHECK(t.metrics.row_pitch == amiga.metrics.row_pitch);
  CHECK(t.metrics.strip_h == amiga.metrics.strip_h);
  CHECK(t.metrics.keys_h == amiga.metrics.keys_h);
  CHECK(t.metrics.header_h == amiga.metrics.header_h);
  CHECK(t.metrics.info_h == amiga.metrics.info_h);
  CHECK(t.metrics.index_w == amiga.metrics.index_w);
  CHECK(t.metrics.meter_h == amiga.metrics.meter_h);
  // Not one of the built-ins: ThemeId has no obsidian, the skin's theme is the app's own.
  for (unsigned i = 0; i < ui::kThemeCount; ++i) {
    CHECK(&ui::theme(ui::theme_id_at(i)) != &t);
  }
}
