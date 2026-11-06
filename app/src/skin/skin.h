// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The obsidian skin (docs/SKIN-PLAN.md sections 2, 3 and 7): whether it is on and usable, the obsidian ui::Theme every skinned frame is drawn with, and the colours of
// its shapes. A screen asks active(ctx) once a frame and draws with the skin or as before; the skin changes the drawing, never the logic.
//
// The switch is Config page "303", key 7, SKIN (CLASSIC / OBSIDIAN; OBSIDIAN by default since SK8). Main loop only: the Config value writes it and the drawing reads it.

#include <o3sdk/app/context.h>
#include <o3sdk/app/diag.h>
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

// What a skinned frame cost, for the console's `skin stats` (docs/SKIN-PLAN.md 4.6, check 7): the display-list words the screen had emitted when it finished (the Renderer's own END
// and swap come after), the CPU time of its draw() in microseconds, and the longest of those over the last full second. note_frame() is the screen's, once a frame; stats() the
// console's. Main loop only.
struct FrameStats {
  std::uint32_t frames = 0;      // skinned frames drawn since start (or the last reset)
  std::uint32_t last_words = 0;
  std::uint32_t last_us = 0;
  std::uint32_t peak_us = 0;     // the largest draw time in the second that just ended or is running
};
// The MK66's clock in cycles a microsecond; the host's hal::cycles() is scaled to it. The screens divide their draw's cycles by it for note_frame().
inline constexpr std::uint32_t kCyclesPerUs = 180;
void note_frame(std::uint32_t words, std::uint32_t draw_us, std::uint64_t now_ms) noexcept;
FrameStats stats() noexcept;
void reset_stats() noexcept;  // the counters and the peak back to zero (a test's; the console has no use for it)

// The console's `skin ballast N`: N words of one-pixel rects at (0, 479), drawn after the frame's content, so a unit can find the longest display list that scans out clean (SU1, check
// 2). 0 is off. Clamped to kBallastMax. Main loop only.
inline constexpr std::uint32_t kBallastMax = 1900;
void set_ballast(std::uint32_t words) noexcept;
std::uint32_t ballast() noexcept;

// The console's `skin` command (App::diag_commands, docs/SKIN-PLAN.md 4.6): `skin on`, `skin off` (the same switch as the Config page's SKIN: Config shows it, and it is saved with the settings like any Config change), `skin ballast N`, `skin stats`
// (also with no word): the state, the skinned frames drawn, the last frame's words and draw time, the peak draw time of the last second, the ballast. A reply is one line.
void console(const o3sdk::app::DiagArgs& args, o3sdk::app::DiagOut& out) noexcept;
inline constexpr const char* kConsoleHelp = "skin: on | off | ballast N | stats - the obsidian skin";

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
// The F row (docs/SKIN-PLAN.md 2.3), from the spike's obsidian set.
inline constexpr ui::Rgb kScale{190, 66, 54};      // a knob's ticks
inline constexpr ui::Rgb kSkirt{16, 16, 18};       // its skirt, near black so that the shadow merged into the bitmap reads as a shadow
inline constexpr ui::Rgb kCap{150, 150, 160};      // its cap
inline constexpr ui::Rgb kPointer{255, 74, 50};
inline constexpr ui::Rgb kFocusFill{186, 30, 22};  // the readout of the knob the encoder drives
inline constexpr ui::Rgb kLever{170, 170, 178};    // the switch's lever
inline constexpr ui::Rgb kKey{64, 58, 60};         // an action key
inline constexpr ui::Rgb kKeyDim{32, 29, 31};      // an action key with nothing to do now
}  // namespace pal

}  // namespace o3_303::skin
