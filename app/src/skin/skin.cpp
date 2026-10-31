// SPDX-License-Identifier: GPL-3.0-or-later
#include "skin.h"

namespace o3_303::skin {

namespace {

bool g_on = false;  // CLASSIC until SK8; main loop only

using ui::Font;

// The kit's own spacing and faces (themes.cpp): a skinned screen's content is today's, laid out on the same grid.
constexpr ui::Metrics kMetrics = {1, 4, 4, 24, 32, 26, 16, 22, 30, 4};

// The tokens. The pairs of kContrastPairs hold for them (tests/skin_theme_test.cpp): the text colours read on the glass and on the panel, the amber and the red carry dark and
// white ink. The values are the obsidian set of the spike (skin.py) where a token has one, and the buttons' colours of mockup 01.
constexpr ui::Theme kObsidian = {
    "obsidian",
    {
        {41, 37, 39},     // Chrome: the info strip, the key caps and the chips over the glass (1.32:1 on it: Sel and Rec must stay 3:1 on Chrome and 4.5:1 under white, which caps it there)
        {240, 236, 236},  // ChromeText: values on it
        {230, 116, 92},   // ChromeDim: the silkscreen: captions, group titles
        {7, 8, 12},       // Pane: the glass
        {232, 236, 244},  // Text
        {140, 148, 170},  // TextDim
        {52, 57, 72},     // Empty: 1.74:1 on the glass (the kit's dim band is 1.6 to 3.0)
        {70, 192, 255},   // Instr: LOCK, cyan (and the ENC window's value)
        {40, 208, 172},   // Vol: NOTE, teal
        {178, 132, 255},  // Fx: PLAYS, purple
        {36, 112, 226},   // Sel
        {255, 255, 255},  // SelText
        {250, 196, 44},   // Accent: GATE, amber (and a toast)
        {14, 14, 18},     // AccentText
        {70, 220, 90},    // Play
        {0, 20, 0},       // PlayText
        {214, 48, 44},    // Rec
    },
    {Font::F16x32, Font::F10x20, Font::F8x16, Font::F6x12},
    kMetrics,
};

static_assert(kObsidian.colour[static_cast<unsigned>(ui::Color::Pane)].r == pal::kGlass.r && kObsidian.colour[static_cast<unsigned>(ui::Color::Pane)].g == pal::kGlass.g &&
                  kObsidian.colour[static_cast<unsigned>(ui::Color::Pane)].b == pal::kGlass.b,
              "the frame's CLEAR is the glass");

}  // namespace

void set_on(bool on) noexcept { g_on = on; }
bool on() noexcept { return g_on; }

bool active(const o3sdk::app::Context& ctx) noexcept { return g_on && ctx.app_assets_ok(); }

const ui::Theme& theme() noexcept { return kObsidian; }

}  // namespace o3_303::skin
