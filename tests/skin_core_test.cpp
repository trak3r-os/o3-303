// SPDX-License-Identifier: GPL-3.0-or-later
// The obsidian skin's core (docs/SKIN-PLAN.md section 7, slice SK2): the 7-segment mapping, the header's mapping from the StatusView and the context (2.1, 2.2), the PERFORM slots,
// the layer queue's order (shapes by layer, texts by token colour) and its bounds. Pure logic and words; the pictures are app/scenarios/skin_home.txt.
#include <doctest/doctest.h>

#include "../app/src/skin/header.h"
#include "../app/src/skin/panel.h"
#include "../app/src/skin/seg.h"
#include "../app/src/skin/skin.h"

#include <o3sdk/eve/display_list_builder.h>
#include <o3sdk/eve/dl.h>
#include <o3sdk/platform/host/fakes.h>
#include <o3sdk/ui/plot/canvas.h>
#include <o3sdk/ui/renderer.h>
#include <o3_303/perform.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace o3_303;
using namespace o3_303::skin;
namespace dl = o3sdk::eve::dl;
namespace ui = o3sdk::ui;

namespace {

std::string str(const char* s) { return std::string(s); }

constexpr std::uint16_t bit(PerfAction a) { return perf_bit(a); }

// The default assignment of the four colour buttons: MUTE, ACC ALL, FILL, FREEZE.
const std::uint8_t (&defaults())[kPerfButtons] { return kDefaultPerf; }

// The COLOR_RGB words of a frame drawn by `draw`, in order, after the Renderer's own preamble.
template <typename Fn>
std::vector<std::uint32_t> colours(Fn draw, const ui::Theme& theme) {
  o3sdk::platform::host::FakeDisplay sink;
  std::vector<std::uint32_t> storage(o3sdk::eve::kMaxDisplayListWords);
  o3sdk::eve::DisplayListBuilder builder(sink, storage.data(), storage.size());
  ui::Renderer r(builder, theme);
  REQUIRE(r.begin(ui::Color::Pane));
  const std::size_t preamble = r.words();
  draw(r);
  REQUIRE(r.end());
  std::vector<std::uint32_t> out;
  const auto& f = sink.last_frame();
  for (std::size_t i = preamble; i < f.size(); ++i) {
    if ((f[i] >> 24) == o3sdk::eve::opcode::kColorRgb) {
      out.push_back(f[i]);
    }
  }
  return out;
}

}  // namespace

// ---- seg.h -----------------------------------------------------------------------------------------------------------------------

TEST_CASE("seg: a character is a cell of the bitmap: 0-9, A b C d E F G H, a dash, the blank") {
  for (char c = '0'; c <= '9'; ++c) {
    CHECK(seg_cell(c) == static_cast<unsigned>(c - '0'));
  }
  CHECK(seg_cell('A') == 10);
  CHECK(seg_cell('B') == 11);
  CHECK(seg_cell('C') == 12);
  CHECK(seg_cell('D') == 13);
  CHECK(seg_cell('E') == 14);
  CHECK(seg_cell('F') == 15);
  CHECK(seg_cell('G') == 16);
  CHECK(seg_cell('H') == 17);
  CHECK(seg_cell('-') == kSegCellDash);
  CHECK(kSegCellDash == 18);
  CHECK(seg_cell(' ') == kSegCellBlank);
  CHECK(kSegCellBlank == 19);
  CHECK(seg_cell('z') == kSegCellBlank);  // anything else is the ghost 8 alone
  CHECK(seg_cell('a') == kSegCellBlank);  // upper case only
  CHECK(skin_assets::info(skin_assets::Id::Seg).cells == kSegCellBlank + 1);
}

TEST_CASE("seg: a string is cells and the place of the point, which belongs to the character before it") {
  const SegRun a = seg_text("125.0");
  REQUIRE(a.n == 4);
  CHECK(a.cell[0] == 1);
  CHECK(a.cell[1] == 2);
  CHECK(a.cell[2] == 5);
  CHECK(a.cell[3] == 0);
  CHECK(a.dot == 2);
  const SegRun b = seg_text(" 95.5");
  REQUIRE(b.n == 4);
  CHECK(b.cell[0] == kSegCellBlank);
  CHECK(b.cell[1] == 9);
  CHECK(b.cell[2] == 5);
  CHECK(b.cell[3] == 5);
  CHECK(b.dot == 2);
  const SegRun p = seg_text("A01");
  REQUIRE(p.n == 3);
  CHECK(p.cell[0] == 10);
  CHECK(p.cell[1] == 0);
  CHECK(p.cell[2] == 1);
  CHECK(p.dot == -1);
  // A leading or a second point is dropped; a string past the cells is cut; null and empty are nothing.
  CHECK(seg_text(".5").dot == -1);
  CHECK(seg_text(".5").n == 1);
  CHECK(seg_text("1.2.3").dot == 0);
  CHECK(seg_text("1.2.3").n == 3);
  CHECK(seg_text("123456789").n == kSegMaxCells);
  CHECK(seg_text("").n == 0);
  CHECK(seg_text(nullptr).n == 0);
  CHECK(seg_text("--").cell[1] == kSegCellDash);
}

// ---- the header's mapping ----------------------------------------------------------------------------------------------------------

TEST_CASE("header: the tempo display is four cells and the point, a leading blank for under 100") {
  char t[6];
  CHECK(str(tempo_display(125'000, t)) == "125.0");
  CHECK(str(tempo_display(95'500, t)) == " 95.5");
  CHECK(str(tempo_display(300'000, t)) == "300.0");
  CHECK(str(tempo_display(20'000, t)) == " 20.0");
  CHECK(str(tempo_display(120'050, t)) == "120.1");  // rounds to the tenth, as the MASTER card does
  CHECK(str(tempo_display(120'049, t)) == "120.0");
  CHECK(str(tempo_display(0, t)) == "  0.0");
  CHECK(str(tempo_display(4'000'000'000u, t)) == "999.9");  // never wider than the display
  const SegRun run = seg_text(tempo_display(95'500, t));
  CHECK(run.n == 4);
  CHECK(run.cell[0] == kSegCellBlank);
}

TEST_CASE("header: the PERFORM slots: the actions that are on first, lit; then the buttons' own, dark; \"+N\" when more are on than there are slots") {
  PerformSlot s[4];
  fill_perform_slots(s, 0, defaults());  // nothing on: the four buttons' actions, dark
  CHECK(str(s[0].cap) == "MUTE");
  CHECK(str(s[1].cap) == "ACC");
  CHECK(str(s[2].cap) == "FILL");
  CHECK(str(s[3].cap) == "FRZ");
  for (const PerformSlot& x : s) {
    CHECK_FALSE(x.lit);
  }
  fill_perform_slots(s, bit(PerfAction::Fill) | bit(PerfAction::Freeze), defaults());
  CHECK(str(s[0].cap) == "FILL");
  CHECK(s[0].lit);
  CHECK(str(s[1].cap) == "FRZ");
  CHECK(s[1].lit);
  CHECK(str(s[2].cap) == "MUTE");
  CHECK_FALSE(s[2].lit);
  CHECK(str(s[3].cap) == "ACC");
  CHECK_FALSE(s[3].lit);
  // An action no button carries (THROW, from a pad) is shown all the same.
  fill_perform_slots(s, bit(PerfAction::Throw), defaults());
  CHECK(str(s[0].cap) == "THRW");
  CHECK(s[0].lit);
  CHECK(str(s[1].cap) == "MUTE");
  CHECK_FALSE(s[1].lit);
  // Four on: all four lit, none left over. Five: three lit and "+2" for the other two.
  fill_perform_slots(s, bit(PerfAction::Mute) | bit(PerfAction::AccAll) | bit(PerfAction::Fill) | bit(PerfAction::Freeze), defaults());
  CHECK(s[3].lit);
  CHECK(str(s[3].cap) == "FRZ");
  fill_perform_slots(s, bit(PerfAction::Mute) | bit(PerfAction::AccAll) | bit(PerfAction::Fill) | bit(PerfAction::Freeze) | bit(PerfAction::Roll), defaults());
  CHECK(str(s[0].cap) == "MUTE");
  CHECK(str(s[2].cap) == "FILL");
  CHECK(str(s[3].cap) == "+2");
  CHECK(s[3].lit);
  fill_perform_slots(s, 0x7FF, defaults());  // all eleven
  CHECK(str(s[3].cap) == "+8");
  // A button with the same action as another shows it once; an assignment out of range shows nothing.
  const std::uint8_t twice[kPerfButtons] = {0, 0, 200, 4};
  fill_perform_slots(s, 0, twice);
  CHECK(str(s[0].cap) == "MUTE");
  CHECK(str(s[1].cap) == "FILL");
  CHECK(str(s[2].cap) == "");
}

TEST_CASE("header: a message is Label to 20 characters, Micro to 27, and cut at 26 with a dot after that") {
  auto fit = [](unsigned n, char (&out)[28]) {
    char in[64];
    for (unsigned i = 0; i < n; ++i) {
      in[i] = static_cast<char>('A' + i % 26u);
    }
    in[n] = '\0';
    skin::cut_message(out, sizeof out, in);
    return std::string(in);
  };
  char out[28];
  for (const unsigned n : {1u, 20u}) {
    const std::string in = fit(n, out);
    CHECK(in == out);
    CHECK(skin::message_face(static_cast<unsigned>(std::strlen(out))) == ui::Face::Label);
  }
  for (const unsigned n : {21u, 27u}) {
    const std::string in = fit(n, out);
    CHECK(in == out);
    CHECK(skin::message_face(static_cast<unsigned>(std::strlen(out))) == ui::Face::Micro);
  }
  for (const unsigned n : {28u, 29u, 40u}) {
    const std::string in = fit(n, out);
    CHECK(std::string(out) == in.substr(0, 26) + ".");
    CHECK(std::strlen(out) == 27u);
    CHECK(skin::message_face(static_cast<unsigned>(std::strlen(out))) == ui::Face::Micro);
  }
  // 27 Micro characters fit the window: 162 px of 172, 6 in from the left.
  CHECK(27 * 6 + 6 <= 172);
}

TEST_CASE("header: fill_header maps the StatusView and the context: LEDs, octave, take, message, windows") {
  frame::StatusView st;
  HeaderInputs in;
  in.title = "PATTERN SETUP";
  in.pattern = 16;  // B05
  in.tempo_mbpm = 125'000;
  in.playing = true;
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    in.perf[b] = kDefaultPerf[b];
  }
  std::strcpy(st.enc, "F1 CUTOFF");
  st.keys = true;
  st.octave = 1;
  st.rec = true;
  st.save_failed = true;
  HeaderView v;
  fill_header(v, st, in);
  CHECK(str(v.pattern) == "B05");
  CHECK(str(v.tempo) == "125.0");
  CHECK(v.playing);
  CHECK(v.rec);
  CHECK(v.keys);
  CHECK_FALSE(v.trnsp);
  CHECK(str(v.octave) == "OCT 3-4");
  CHECK(str(v.page) == "PATTERN SETUP");
  CHECK(str(v.enc) == "F1 CUTOFF");
  CHECK(v.save_failed);
  CHECK_FALSE(v.stock);
  st.stock = true;  // STOCK has the mods out: the header carries it (the tagline says so, see the next test)
  fill_header(v, st, in);
  CHECK(v.stock);
  st.stock = false;
  CHECK(v.take == Take::None);
  CHECK_FALSE(v.take_lit);
  CHECK(str(v.take_text) == "TAKE");
  CHECK(str(v.message) == "");

  // TRNSP lit instead of KEYS; neither, and no octave, when the pads are not a keyboard.
  st.trnsp = true;
  fill_header(v, st, in);
  CHECK_FALSE(v.keys);
  CHECK(v.trnsp);
  st.keys = false;
  fill_header(v, st, in);
  CHECK_FALSE(v.keys);
  CHECK_FALSE(v.trnsp);
  CHECK(str(v.octave) == "");

  // The take: armed blinks, recording shows the time in red, ready and saving are on.
  st.take = "TAKE ARMED";
  in.now_ms = 0;
  fill_header(v, st, in);
  CHECK(v.take == Take::Armed);
  CHECK(v.take_lit);
  CHECK(str(v.take_text) == "ARMED");
  in.now_ms = 400;
  fill_header(v, st, in);
  CHECK_FALSE(v.take_lit);
  st.take = "TAKE REC 0:05";
  fill_header(v, st, in);
  CHECK(v.take == Take::Recording);
  CHECK(v.take_lit);
  CHECK(str(v.take_text) == "0:05");
  st.take = "TAKE READY";
  fill_header(v, st, in);
  CHECK(v.take == Take::Ready);
  CHECK(str(v.take_text) == "READY");
  st.take = "TAKE SAVING";
  fill_header(v, st, in);
  CHECK(v.take == Take::Saving);
  CHECK(v.take_lit);

  // One message in the PAGE window: the toast, else the take's word; an outline when it says something did not happen; fitted to the window.
  st.take_msg = "SAVED";
  fill_header(v, st, in);
  CHECK(str(v.message) == "SAVED");
  CHECK_FALSE(v.message_off);
  st.toast = "NEW LINE";
  fill_header(v, st, in);
  CHECK(str(v.message) == "NEW LINE");
  st.toast = "NO CHANGE";
  st.toast_off = true;
  fill_header(v, st, in);
  CHECK(v.message_off);
  st.toast = "THIS TOAST IS MUCH TOO LONG FOR THE WINDOW";
  fill_header(v, st, in);
  CHECK(str(v.message) == "THIS TOAST IS MUCH TOO LON.");
  CHECK(std::strlen(v.message) == 27);
  st.toast = "";
  st.take_msg = "NO CARD";
  st.take_msg_off = true;
  fill_header(v, st, in);
  CHECK(str(v.message) == "NO CARD");
  CHECK(v.message_off);

  // The encoder's owner is cut to its window as well.
  std::strcpy(st.enc, "MORPH 1>2 50% AND MORE");
  fill_header(v, st, in);
  CHECK(std::strlen(v.enc) == 17);
}

TEST_CASE("header: PLAYS: SONG changes the TRANSPORT legend and nothing else") {
  frame::StatusView st;
  HeaderInputs in;
  in.title = "STEPS";
  HeaderView pattern;
  fill_header(pattern, st, in);
  CHECK_FALSE(pattern.song);  // PATTERN: the header is what it always was
  st.song = true;
  HeaderView song;
  fill_header(song, st, in);
  CHECK(song.song);
  CHECK(str(song.pattern) == str(pattern.pattern));  // the rest is the same
  CHECK(str(song.tempo) == str(pattern.tempo));
  CHECK(str(song.page) == str(pattern.page));
  CHECK(str(song.enc) == str(pattern.enc));
  CHECK(song.playing == pattern.playing);
  CHECK(song.keys == pattern.keys);
  CHECK(song.message[0] == pattern.message[0]);
  // The drawing: the same number of primitives either way (the legend is four characters in both).
  Panel& p = panel();
  p.clear();
  draw_header(p, pattern);
  const unsigned shapes = p.shapes();
  const unsigned texts = p.texts();
  p.clear();
  draw_header(p, song);
  CHECK(p.shapes() == shapes);
  CHECK(p.texts() == texts);
}

TEST_CASE("header: the tagline is ACID BASS VOICE, STOCK . MODS OFF in the accent colour while STOCK is on, and a failed save (red) wins over both") {
  const auto taglines = [](const frame::StatusView& st, unsigned& dim, unsigned& accent, unsigned& red) {
    Panel p;
    p.clear();
    queue_panel(p, kHeaderH, 384);
    HeaderInputs in;
    in.title = "STEPS";
    for (unsigned b = 0; b < kPerfButtons; ++b) {
      in.perf[b] = kDefaultPerf[b];
    }
    HeaderView v;
    fill_header(v, st, in);
    draw_header(p, v);
    dim = accent = red = 0;
    for (unsigned i = 0; i < p.texts(); ++i) {
      Panel::TextInfo tx;
      if (p.text_at(i, tx) && tx.x == 14 && tx.y == 52) {  // the brand's line
        dim += tx.color == ui::Color::ChromeDim ? 1u : 0u;
        accent += tx.color == ui::Color::Accent ? 1u : 0u;
        red += tx.color == ui::Color::Rec ? 1u : 0u;
      }
    }
  };
  frame::StatusView st;
  unsigned dim = 0;
  unsigned accent = 0;
  unsigned red = 0;
  taglines(st, dim, accent, red);
  CHECK(dim == 1u);
  CHECK(accent == 0u);
  CHECK(red == 0u);
  st.stock = true;
  taglines(st, dim, accent, red);
  CHECK(dim == 0u);
  CHECK(accent == 1u);
  CHECK(red == 0u);
  st.save_failed = true;
  taglines(st, dim, accent, red);
  CHECK(dim == 0u);
  CHECK(accent == 0u);
  CHECK(red == 1u);
}

TEST_CASE("header: a full header, with every lamp and window lit, is far inside the queue") {
  Panel p;
  p.clear();
  queue_panel(p, kHeaderH, 384);
  frame::StatusView st;
  HeaderInputs in;
  in.title = "STEPS";
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    in.perf[b] = kDefaultPerf[b];
  }
  std::strcpy(st.enc, "F1 CUTOFF");
  st.keys = true;
  st.rec = true;
  st.take = "TAKE REC 0:05";
  st.toast = "NO CHANGE";
  st.toast_off = true;
  st.actions = 0x7FF;
  HeaderView v;
  fill_header(v, st, in);
  v.playing = true;
  draw_header(p, v);
  CHECK(p.dropped() == 0);
  CHECK(p.shapes() < Panel::kShapes / 2);
  CHECK(p.texts() < Panel::kTexts / 2);
}

// ---- the layer queue -----------------------------------------------------------------------------------------------------------------

TEST_CASE("panel: shapes are drawn layer by layer in the order of Layer, whatever the order they were queued in; a colour is set once a layer") {
  Panel p;
  p.clear();
  // Two shapes of a colour for every layer, queued from the last layer to the first: a layer's colour is its number + 1 in red.
  for (unsigned l = kLayerCount; l-- > 0;) {
    const ui::Rgb rgb{static_cast<std::uint8_t>(l + 1u), 0, 0};
    for (int k = 0; k < 2; ++k) {
      const int x = 100 + 50 * k;
      switch (static_cast<Layer>(l)) {
        case Layer::Base:
        case Layer::Rule:
        case Layer::Outline:
        case Layer::MeterOff:
        case Layer::MeterOn:
        case Layer::Wave: p.rect(static_cast<Layer>(l), ui::Rect{x, 10, 20, 8}, rgb); break;
        case Layer::Texture: p.texture(k * 100, 20, rgb); break;
        case Layer::Sheen: p.line(Layer::Sheen, x * 16, 0, x * 16 + 160, 800, 64, rgb, 9); break;
        case Layer::GlassRim:
        case Layer::GlassFace:
        case Layer::WindowLip:
        case Layer::HeldRim:
        case Layer::WindowFace:
        case Layer::Fill:
        case Layer::Tab:
        case Layer::Switch: p.round_rect(static_cast<Layer>(l), ui::Rect{x, 20, 30, 20}, 48, rgb); break;
        case Layer::ActKey: p.sprite(Layer::ActKey, skin_assets::Id::ActKey, 0, x, 100, rgb); break;
        case Layer::KnobScale: p.sprite(Layer::KnobScale, skin_assets::Id::Knob42Scale, 0, x, 100, rgb); break;
        case Layer::KnobBody: p.sprite(Layer::KnobBody, skin_assets::Id::Knob42Body, 0, x, 100, rgb); break;
        case Layer::KnobCap: p.sprite(Layer::KnobCap, skin_assets::Id::Knob42Cap, 0, x, 100, rgb); break;
        case Layer::Pointer: p.line(Layer::Pointer, x * 16, 100, x * 16 + 160, 300, 18, rgb); break;
        case Layer::Glow:
        case Layer::SegDot: p.point(static_cast<Layer>(l), x * 16, 300, 64, rgb); break;
        case Layer::Led: p.sprite(Layer::Led, skin_assets::Id::Led, 0, x, 100, rgb); break;
        case Layer::KeyBody: p.sprite(Layer::KeyBody, skin_assets::Id::Key, 0, x, 100, rgb); break;
        case Layer::KeyLight: p.sprite(Layer::KeyLight, skin_assets::Id::Key, 1, x, 100, rgb); break;
        case Layer::KeyDark: p.sprite(Layer::KeyDark, skin_assets::Id::Key, 2, x, 100, rgb); break;
        case Layer::Seg: p.sprite(Layer::Seg, skin_assets::Id::Seg, 3, x, 100, rgb); break;
        case Layer::Logo: p.sprite(Layer::Logo, skin_assets::Id::Logo, 0, x, 100, rgb); break;
        case Layer::kCount: break;
      }
    }
  }
  REQUIRE(p.dropped() == 0);
  REQUIRE(p.shapes() == 2 * kLayerCount);
  const std::vector<std::uint32_t> got = colours(
      [&](ui::Renderer& r) {
        ui::plot::Canvas canvas(r);
        canvas.open();
        p.flush_shapes(canvas);
        canvas.close();
      },
      theme());
  std::vector<std::uint32_t> want;
  for (unsigned l = 0; l < kLayerCount; ++l) {
    want.push_back(dl::color_rgb(static_cast<std::uint8_t>(l + 1u), 0, 0));
  }
  CHECK(got == want);
}

TEST_CASE("panel: texts are drawn after the shapes, one colour token at a time in the order of ui::Color") {
  Panel p;
  p.clear();
  const char* word = "TEXT";
  for (unsigned c = ui::kColorCount; c-- > 0;) {
    p.text(10, 10 + static_cast<int>(c), word, static_cast<ui::Color>(c), ui::Face::Micro);
    p.text(40, 10 + static_cast<int>(c), word, static_cast<ui::Color>(c), ui::Face::Micro);
  }
  REQUIRE(p.dropped() == 0);
  const ui::Theme& th = theme();
  const std::vector<std::uint32_t> got = colours([&](ui::Renderer& r) { p.flush_text(r); }, th);
  std::vector<std::uint32_t> want;
  for (unsigned c = 0; c < ui::kColorCount; ++c) {
    const ui::Rgb& rgb = th.rgb(static_cast<ui::Color>(c));
    const std::uint32_t w = dl::color_rgb(rgb.r, rgb.g, rgb.b);
    if (want.empty() || want.back() != w) {  // the renderer sets a colour once for two texts that share it
      want.push_back(w);
    }
  }
  CHECK(got == want);
}

TEST_CASE("panel: a full queue drops what does not fit, counts it, and clear() empties it") {
  Panel p;
  p.clear();
  for (unsigned i = 0; i < Panel::kShapes + 3; ++i) {
    (void)p.rect(Layer::Base, ui::Rect{0, 0, 1, 1}, ui::Rgb{1, 2, 3});
  }
  CHECK(p.shapes() == Panel::kShapes);
  CHECK(p.dropped() == 3);
  for (unsigned i = 0; i < Panel::kTexts + 1; ++i) {
    (void)p.text(0, 0, "X", ui::Color::Text, ui::Face::Micro);
  }
  CHECK(p.texts() == Panel::kTexts);
  CHECK(p.dropped() == 4);
  p.clear();
  CHECK(p.shapes() == 0);
  CHECK(p.texts() == 0);
  CHECK(p.dropped() == 0);
  // Measured: 11,552 B with 384 shapes and 192 texts (18 and 12 B each plus the queue's indices). The bound is the measured size and a little: a bigger queue is a decision (it lives in the SDRAM).
  static_assert(sizeof(Panel) <= 11600, "the queue grew: raise the bound only on purpose");
}

TEST_CASE("panel: the texture bands are the tile's rows drawn 800 wide, and valid descs") {
  const ui::plot::BitmapDesc top = Panel::texture_desc(0, 66);
  const ui::plot::BitmapDesc low = Panel::texture_desc(384, 96);
  CHECK(ui::plot::bitmap_desc_valid(top));
  CHECK(ui::plot::bitmap_desc_valid(low));
  CHECK(top.ram_g == ui::plot::kRamGApp);
  CHECK(low.ram_g == ui::plot::kRamGApp + 384u * 128u);  // 128 bytes a row: the band starts at its own rows of the tile
  CHECK(top.draw_w == 800);
  CHECK(top.draw_h == 66);
  CHECK(low.draw_h == 96);
  CHECK(low.wrap_x == o3sdk::eve::Wrap::Repeat);
}

TEST_CASE("the frame's CLEAR is the glass: the theme's Pane is the palette's glass") {
  const ui::Rgb& pane = theme().rgb(ui::Color::Pane);
  CHECK(pane.r == pal::kGlass.r);
  CHECK(pane.g == pal::kGlass.g);
  CHECK(pane.b == pal::kGlass.b);
}
