// SPDX-License-Identifier: GPL-3.0-or-later
#include "skin.h"

#include "panel.h"

namespace o3_303::skin {

namespace {

bool g_on = true;  // OBSIDIAN by default (SK8); main loop only
bool g_assets = true;              // app_assets_ok() as active() last saw it, for `skin stats` (on without the bitmaps draws classic); main loop only
FrameStats g_stats;                // main loop only: the screen writes it, the console reads it
std::uint64_t g_window_start_ms = 0;
std::uint32_t g_window_peak_us = 0;
std::uint32_t g_ballast = 0;       // main loop only

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

bool active(const o3sdk::app::Context& ctx) noexcept {
  g_assets = ctx.app_assets_ok();
  return g_on && g_assets;
}

const ui::Theme& theme() noexcept { return kObsidian; }

void note_frame(std::uint32_t words, std::uint32_t draw_us, std::uint64_t now_ms) noexcept {
  if (now_ms - g_window_start_ms >= 1000u) {  // a new second: the one that ended is what stats() reports as the peak, until this one beats it
    g_stats.peak_us = g_window_peak_us;
    g_window_peak_us = 0;
    g_window_start_ms = now_ms;
  }
  g_window_peak_us = draw_us > g_window_peak_us ? draw_us : g_window_peak_us;
  ++g_stats.frames;
  g_stats.last_words = words;
  g_stats.last_us = draw_us;
}

FrameStats stats() noexcept {
  FrameStats s = g_stats;
  s.peak_us = g_window_peak_us > s.peak_us ? g_window_peak_us : s.peak_us;
  return s;
}

namespace {

bool word_is(const char* a, const char* w) noexcept {
  while (*a != '\0' && *a == *w) {
    ++a;
    ++w;
  }
  return *a == '\0' && *w == '\0';
}

// A decimal number of at most nine digits; false for anything else (empty, a sign, a letter).
bool parse_uint(const char* s, std::uint32_t& out) noexcept {
  std::uint32_t v = 0;
  unsigned n = 0;
  for (; s[n] >= '0' && s[n] <= '9' && n < 9u; ++n) {
    v = v * 10u + static_cast<std::uint32_t>(s[n] - '0');
  }
  if (n == 0u || s[n] != '\0') {
    return false;
  }
  out = v;
  return true;
}

void say_stats(o3sdk::app::DiagOut& out) noexcept {
  const FrameStats s = stats();
  out.text("skin ").text(g_on ? "on" : "off").text(g_on && !g_assets ? " (no bitmaps: classic drawn)" : "").text(" frames=").dec(s.frames).text(" words=").dec(s.last_words).text(" draw_us=").dec(s.last_us).text(" peak_us=").dec(s.peak_us);
  out.text(" dropped=").dec(panel().dropped()).text(" ballast=").dec(g_ballast).nl();
}

}  // namespace

void console(const o3sdk::app::DiagArgs& args, o3sdk::app::DiagOut& out) noexcept {
  const char* word = args.arg(1);
  std::uint32_t n = 0;
  if (word_is(word, "on") || word_is(word, "off")) {
    set_on(word_is(word, "on"));
    say_stats(out);
  } else if (word_is(word, "ballast") && parse_uint(args.arg(2), n)) {
    set_ballast(n);
    say_stats(out);
  } else if (word_is(word, "stats") || word[0] == '\0') {
    say_stats(out);
  } else {
    out.text("usage: ").text(kConsoleHelp).nl();
  }
}

void reset_stats() noexcept {
  g_stats = FrameStats{};
  g_window_start_ms = 0;
  g_window_peak_us = 0;
}

void set_ballast(std::uint32_t words) noexcept { g_ballast = words > kBallastMax ? kBallastMax : words; }
std::uint32_t ballast() noexcept { return g_ballast; }

}  // namespace o3_303::skin
