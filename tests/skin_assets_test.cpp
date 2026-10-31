// SPDX-License-Identifier: GPL-3.0-or-later
// The obsidian skin's bitmaps (docs/SKIN-PLAN.md sections 3 and 6, slice SK1): the generated table is the plan's, every desc is drawable inside the app's
// region, the app hands the table to the runtime and the bytes land in RAM_G where the table says. The generator's determinism and the hash are the ctest
// skin_assets_fresh (tests/CMakeLists.txt); nothing draws the bitmaps yet.
#include <doctest/doctest.h>

#include <o3_303/skin_assets.h>
#include <o3sdk/app/app.h>
#include <o3sdk/app/app_assets.h>
#include <o3sdk/sim/harness.h>
#include <o3sdk/ui/plot/canvas.h>

#include <cstdint>
#include <cstring>

using namespace o3_303::skin_assets;
using namespace o3sdk;

namespace {

struct Want {
  Id id;
  eve::BitmapFormat format;
  unsigned cells, cell_w, cell_h, bytes;
};

// docs/SKIN-PLAN.md section 3.
constexpr Want kWant[] = {
    {Id::TexObsidian, eve::BitmapFormat::L4, 1, 256, 480, 61'440},  {Id::Knob42Scale, eve::BitmapFormat::L4, 1, 60, 60, 1'800},
    {Id::Knob42Body, eve::BitmapFormat::L4, 1, 54, 54, 1'460},      {Id::Knob42Cap, eve::BitmapFormat::L8, 1, 44, 44, 1'936},
    {Id::Knob58Scale, eve::BitmapFormat::L4, 1, 76, 76, 2'888},     {Id::Knob58Body, eve::BitmapFormat::L4, 1, 70, 70, 2'452},
    {Id::Knob58Cap, eve::BitmapFormat::L8, 1, 60, 60, 3'600},       {Id::Key, eve::BitmapFormat::L8, 3, 42, 36, 4'536},
    {Id::Seg, eve::BitmapFormat::L4, 20, 27, 38, 10'640},           {Id::Logo, eve::BitmapFormat::L4, 1, 150, 50, 3'752},
    {Id::Led, eve::BitmapFormat::L8, 1, 16, 16, 256},               {Id::Lamp, eve::BitmapFormat::L8, 1, 58, 40, 2'320},
    {Id::ActKey, eve::BitmapFormat::L8, 1, 84, 40, 3'360},
};
static_assert(sizeof kWant / sizeof kWant[0] == kCount, "every bitmap of the plan is generated");

const std::uint8_t* bytes_of(Id id) { return kRows[static_cast<std::size_t>(id)].bytes; }

}  // namespace

TEST_CASE("skin assets: the table is the plan's: formats, cells, bytes, 100,440 in all, inside the app's region") {
  std::uint32_t sum = 0;
  for (const Want& w : kWant) {
    const Info& i = info(w.id);
    CAPTURE(i.name);
    CHECK(i.format == w.format);
    CHECK(i.cells == w.cells);
    CHECK(i.cell_w == w.cell_w);
    CHECK(i.cell_h == w.cell_h);
    CHECK(i.bytes == w.bytes);
    CHECK(i.bytes % hal::kUploadAlign == 0);
    CHECK(i.offset % hal::kUploadAlign == 0);
    // all the cells fit the bytes, a row is padded to a byte and a cell's rows follow each other
    const unsigned bpp = i.format == eve::BitmapFormat::L4 ? 4u : 8u;
    CHECK(i.stride == (i.cell_w * bpp + 7u) / 8u);
    CHECK(i.stride * i.cell_h * i.cells <= i.bytes);
    CHECK(i.bytes - i.stride * i.cell_h * i.cells < hal::kUploadAlign);
    sum += i.bytes;
  }
  CHECK(sum == kTotalBytes);
  CHECK(kTotalBytes == 100'440u);
  CHECK(kTotalBytes <= 110'000u);
  CHECK(kTotalBytes <= ui::plot::kRamGAppBytes);
  CHECK(app::app_assets_valid(kRows, kCount));
  CHECK(app::app_assets_aligned(kRows, kCount));  // every array on a word, which only the linked image knows
}

TEST_CASE("skin assets: every desc is valid, the texture is a 256 wide REPEAT tile drawn 800 wide") {
  for (std::size_t n = 0; n < kCount; ++n) {
    const ui::plot::BitmapDesc d = desc(static_cast<Id>(n));
    CAPTURE(info(static_cast<Id>(n)).name);
    CHECK(ui::plot::bitmap_desc_valid(d));
    CHECK(d.ram_g >= ui::plot::kRamGApp);
    CHECK(d.ram_g % 4u == 0);
  }
  const ui::plot::BitmapDesc t = desc(Id::TexObsidian);
  CHECK(t.ram_g == ui::plot::kRamGApp);
  CHECK(t.wrap_x == eve::Wrap::Repeat);
  CHECK(t.wrap_y == eve::Wrap::Border);
  CHECK(t.cell_w == 256);
  CHECK(t.stride == 128);
  CHECK(t.draw_w == 800);
  CHECK(t.draw_h == 480);
  CHECK(desc(Id::Seg).wrap_x == eve::Wrap::Border);
}

TEST_CASE("skin assets: the pictures are what they claim (a lit gradient, the 7-segment set, an odd row's padding)") {
  // The texture is lighter at the top than at the bottom, and never empty or flat: a sum of L4 nibbles over the first and the last 60 rows.
  const ui::plot::BitmapDesc t = desc(Id::TexObsidian);
  const std::uint8_t* tex = bytes_of(Id::TexObsidian);
  auto nibbles = [&](unsigned row0, unsigned rows) {
    unsigned sum = 0;
    for (unsigned y = row0; y < row0 + rows; ++y) {
      for (unsigned x = 0; x < t.stride; ++x) {
        sum += (tex[y * t.stride + x] >> 4) + (tex[y * t.stride + x] & 15u);
      }
    }
    return sum;
  };
  CHECK(nibbles(0, 60) > nibbles(420, 60) + 2000u);
  CHECK(nibbles(420, 60) > 0u);

  // 7-segment: 20 cells, the "8" lights every segment of the "1", the blank (the last) holds the ghost alone, so it is dimmer than any digit; the glyph row of 27
  // pixels is 14 bytes with its last nibble zero.
  const Info& s = info(Id::Seg);
  REQUIRE(s.cells == 20);
  const std::uint8_t* seg = bytes_of(Id::Seg);
  const unsigned cell_bytes = s.stride * s.cell_h;
  auto sum_cell = [&](unsigned c) {
    unsigned sum = 0;
    for (unsigned i = 0; i < cell_bytes; ++i) {
      sum += (seg[c * cell_bytes + i] >> 4) + (seg[c * cell_bytes + i] & 15u);
    }
    return sum;
  };
  CHECK(sum_cell(8) > sum_cell(1));
  CHECK(sum_cell(19) > 0u);
  for (unsigned c = 0; c < 19; ++c) {
    CHECK(sum_cell(c) > sum_cell(19));
  }
  for (unsigned c = 0; c < s.cells; ++c) {
    for (unsigned y = 0; y < s.cell_h; ++y) {
      CHECK((seg[c * cell_bytes + y * s.stride + (s.stride - 1)] & 15u) == 0u);
    }
  }
  // An LED is brightest in the middle (its lens), and the cap sprites are not empty.
  const std::uint8_t* led = bytes_of(Id::Led);
  CHECK(led[8 * 16 + 8] > 200);
  CHECK(led[0] == 0);
  CHECK(bytes_of(Id::Lamp)[20 * 58 + 29] > 100);
  CHECK(bytes_of(Id::ActKey)[20 * 84 + 42] > 100);
}

TEST_CASE("skin assets: the app hands the table to the runtime and every byte is in RAM_G where the table says") {
  sim::Harness h(o3sdk_app());
  REQUIRE(h.start());
  h.run_ms(50);
  const app::AppAsset* rows = nullptr;
  const std::size_t n = o3sdk_app().assets(rows);
  CHECK(n == kCount);
  CHECK(rows == kRows);
  CHECK(h.runtime().context().app_assets_ok());
  for (std::size_t i = 0; i < n; ++i) {
    CAPTURE(info(static_cast<Id>(i)).name);
    CHECK(std::memcmp(h.raster().ram_g() + ui::plot::kRamGApp + rows[i].offset, rows[i].bytes, rows[i].size) == 0);
  }
}
