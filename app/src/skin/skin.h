// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin (docs/SKIN-PLAN.md sections 2, 3 and 7): whether it is on and usable, the obsidian ui::Theme every skinned frame is drawn with, and the colours of
// its shapes. A screen asks active(ctx) once a frame and draws with the skin or as before; the skin changes the drawing, never the logic.
//
// The switch is Config page "303", key 7, SKIN (CLASSIC / OBSIDIAN; CLASSIC until SK8). Main loop only: the Config value writes it and the drawing reads it.

#include <o3sdk/app/context.h>
#include <o3sdk/ui/style.h>
#include <o3sdk/ui/theme.h>

namespace o3_303::skin {

namespace ui = o3sdk::ui;

// The player's choice. It is kept in the kit's settings file by the Config page (config_pages.cpp), so it survives a reboot.
void set_on(bool on) noexcept;
bool on() noexcept;

// The skin is drawn: it is on, and its bitmaps are in RAM_G (Context::app_assets_ok(): false when the app has none, or the upload failed). Without them the whole app runs
// classic: a skin drawn from bitmaps that are not there would be a screen of boxes.
bool active(const o3sdk::app::Context& ctx) noexcept;

// The theme of a skinned frame: dark glass for the Pane, the panel's material for the Chrome (the info strip is drawn on it), and the text colours of the whole skin, which are
// its 17 tokens (docs/SKIN-PLAN.md section 7): ChromeDim is the silkscreen, Instr / Vol / Fx / Accent the colour buttons (LOCK cyan, NOTE teal, PLAYS purple, GATE amber),
// Rec the red. It holds every pair of ui::kContrastPairs (tests/skin_theme_test.cpp). Lives as long as the program.
const ui::Theme& theme() noexcept;

// The colours of the shapes (never of text: that is the theme's). From the skin spike's obsidian set.
namespace pal {
inline constexpr ui::Rgb kBase{12, 9, 11};         // the panel under the texture; the header and the F row
inline constexpr ui::Rgb kTexTint{62, 24, 28};     // the texture's ink (a 16-level mask tinted)
inline constexpr ui::Rgb kSheen{255, 225, 225};    // two translucent lines over the header
inline constexpr std::uint8_t kSheenAlpha = 9;
inline constexpr ui::Rgb kGlass{7, 8, 12};         // the glass (also the frame's CLEAR: the theme's Pane)
inline constexpr ui::Rgb kBezel{30, 24, 26};       // the glass's rim
inline constexpr ui::Rgb kWindow{4, 2, 3};         // the recess of a display or a window
inline constexpr ui::Rgb kWindowLip{74, 46, 46};   // the lit edge under it
inline constexpr std::uint8_t kWindowLipAlpha = 150;
inline constexpr ui::Rgb kRule{120, 40, 34};       // silkscreen rules
inline constexpr ui::Rgb kSeg{255, 46, 30};        // the 7-segment cells and their point
inline constexpr ui::Rgb kLogo{240, 104, 80};      // the wordmark
inline constexpr ui::Rgb kLedOff{58, 12, 10};
inline constexpr ui::Rgb kLedRed{255, 50, 34};
inline constexpr ui::Rgb kLedGreen{90, 232, 80};
inline constexpr ui::Rgb kLedAmber{255, 176, 32};
inline constexpr std::uint8_t kGlowAlpha = 50;     // a lit LED's glow, a POINT behind it
}  // namespace pal

}  // namespace o3_303::skin
