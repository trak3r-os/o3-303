// SPDX-License-Identifier: GPL-3.0-or-later
// The obsidian skin's MASTER, RECORD and HOOKS (docs/SKIN-PLAN.md 2.4, SK6): the pure rules of the LED-segment bars (which segments a level and a held peak light, which are amber, where each sits),
// of the tempo display's cells and of RECORD's envelope (the columns, the bar marks, how far a take has come), and the screens end to end in the simulator with the skin on: the F row's slot kinds,
// ARM / STOP lit red while a take records, SAVE and DISCARD dim until one is finished, the bars against the view's levels, the TAKE LED in the header of every skinned screen, and the fullest frame of
// each screen queued whole (`dropped() == 0`). The pictures are skin_master.txt's and skin_record.txt's.
#include <doctest/doctest.h>

#include "hands.h"
#include "../app/src/hooks_screen.h"
#include "../app/src/master_screen.h"
#include "../app/src/record_screen.h"
#include "../app/src/skin/frow.h"
#include "../app/src/skin/hooks.h"
#include "../app/src/skin/master.h"
#include "../app/src/skin/meter.h"
#include "../app/src/skin/panel.h"
#include "../app/src/skin/perform.h"
#include "../app/src/skin/record.h"

#include <o3sdk/dsp/engine.h>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace hands;
using namespace o3_303;
using hal_state = o3sdk::hal::record::State;

namespace {

bool same(const ui::Rgb& a, const ui::Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
bool inside(const ui::Rect& in, const ui::Rect& out) { return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h; }
bool disjoint(const ui::Rect& a, const ui::Rect& b) { return a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y; }

// The Panel's last frame, counted.
struct Queue {
  const skin::Panel& q = skin::panel();
  unsigned sprites(skin::Layer layer, skin_assets::Id id, int y_from = 0) const {
    unsigned n = 0;
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::SpriteInfo sp;
      n += q.sprite_at(i, sp) && sp.layer == layer && sp.id == id && sp.y >= y_from ? 1u : 0u;
    }
    return n;
  }
  unsigned rects(skin::Layer layer) const {
    unsigned n = 0;
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::RectInfo r;
      n += q.rect_at(i, r) && r.layer == layer ? 1u : 0u;
    }
    return n;
  }
  unsigned rects(skin::Layer layer, const ui::Rgb& rgb) const {
    unsigned n = 0;
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::RectInfo r;
      n += q.rect_at(i, r) && r.layer == layer && same(r.rgb, rgb) ? 1u : 0u;
    }
    return n;
  }
  // Round rects of `layer` whose box is w x h.
  unsigned round_rects(skin::Layer layer, int w, int h) const {
    unsigned n = 0;
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::RoundRectInfo r;
      n += q.round_rect_at(i, r) && r.layer == layer && r.box.w == w && r.box.h == h ? 1u : 0u;
    }
    return n;
  }
  unsigned round_rects(skin::Layer layer, const ui::Rgb& rgb) const {
    unsigned n = 0;
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::RoundRectInfo r;
      n += q.round_rect_at(i, r) && r.layer == layer && same(r.rgb, rgb) ? 1u : 0u;
    }
    return n;
  }
  // The tint of the sprite of `layer` and `id` at (x, y).
  bool tint_at(skin::Layer layer, skin_assets::Id id, int x, int y, ui::Rgb& out) const {
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::SpriteInfo sp;
      if (q.sprite_at(i, sp) && sp.layer == layer && sp.id == id && sp.x == x && sp.y == y) {
        out = sp.rgb;
        return true;
      }
    }
    return false;
  }
  // The header's TAKE LED (TRANSPORT, third row): its sprite's tint.
  ui::Rgb take_led() const {
    ui::Rgb t{1, 2, 3};
    REQUIRE(tint_at(skin::Layer::Led, skin_assets::Id::Led, 368, 47, t));
    return t;
  }
  void intact() const {
    CHECK(q.dropped() == 0u);
    CHECK(q.shapes() < skin::Panel::kShapes);
    CHECK(q.texts() < skin::Panel::kTexts);
  }
};

MasterScreen& master(Hands& t) { return *static_cast<MasterScreen*>(t.h->runtime().context().screen()); }
RecordScreen& record(Hands& t) { return *static_cast<RecordScreen*>(t.h->runtime().context().screen()); }
app::Recorder& recorder(Hands& t) { return t.h->runtime().context().recorder(); }

// Runs until the recorder is in `want` (at most `max_ms`).
bool wait_state(Hands& t, hal_state want, unsigned max_ms) {
  for (unsigned ms = 0; ms < max_ms; ms += 20) {
    if (recorder(t).state() == want) {
      return true;
    }
    t.wait(20);
  }
  return recorder(t).state() == want;
}

// F3 until BARS reads `bars` (1, 2, 4, 8 bars or 0 FREE), on RECORD.
void set_bars(Hands& t, unsigned bars) {
  for (unsigned i = 0; i < kBarChoices && record(t).bars() != bars; ++i) {
    t.tap(Button::F3);
  }
  REQUIRE(record(t).bars() == bars);
}

}  // namespace

// ---- meter.h -------------------------------------------------------------------------------------------------------------------

TEST_CASE("skin meter: a segment is lit when the level has passed its place on the scale") {
  CHECK(skin::segments_lit(0, 30) == 0u);
  CHECK(skin::segments_lit(1, 30) == 1u);
  CHECK(skin::segments_lit(kMeterMax / 2, 30) == 15u);
  CHECK(skin::segments_lit(kMeterMax, 30) == 30u);
  CHECK(skin::segments_lit(kMeterMax * 3u, 30) == 30u);  // past full scale: all of them, no more
  CHECK(skin::segments_lit(500, 0) == 0u);
  // Monotonic over the whole scale, for both bars' counts.
  for (unsigned n : {skin::kMasterSegments, skin::kRecordSegments}) {
    unsigned prev = 0;
    for (unsigned level = 0; level <= kMeterMax; ++level) {
      const unsigned lit = skin::segments_lit(level, n);
      REQUIRE(lit >= prev);
      REQUIRE(lit <= n);
      prev = lit;
    }
    CHECK(prev == n);
  }
}

TEST_CASE("skin meter: the top quarter of a bar is amber and the rest green; a held peak adds one segment past the level") {
  for (unsigned n : {skin::kMasterSegments, skin::kRecordSegments}) {
    unsigned amber = 0;
    for (unsigned i = 0; i < n; ++i) {
      amber += skin::segment_amber(i, n) ? 1u : 0u;
      CHECK(skin::segment_amber(i, n) == (i >= n * 3u / 4u));
    }
    CHECK(amber == n - n * 3u / 4u);
  }
  const unsigned n = skin::kMasterSegments;
  const unsigned lit_level = skin::segments_lit(300, n);
  const unsigned top = skin::segments_lit(800, n);
  REQUIRE(top > lit_level + 1u);
  unsigned count = 0;
  for (unsigned i = 0; i < n; ++i) {
    count += skin::segment_lit(i, n, 300, 800) ? 1u : 0u;
  }
  CHECK(count == lit_level + 1u);
  CHECK(skin::segment_lit(top - 1u, n, 300, 800));      // the peak's own segment
  CHECK_FALSE(skin::segment_lit(top - 2u, n, 300, 800));  // and nothing between it and the level
  for (unsigned i = 0; i < n; ++i) {  // a peak at or under the level lights nothing more
    CHECK(skin::segment_lit(i, n, 800, 300) == (i < top));
  }
  // The colours: lit green and amber, dark ones dim, never the lit colour.
  CHECK(same(skin::segment_rgb(false, true), skin::pal::kLedGreen));
  CHECK(same(skin::segment_rgb(true, true), skin::pal::kLedAmber));
  CHECK_FALSE(same(skin::segment_rgb(false, false), skin::pal::kLedGreen));
  CHECK_FALSE(same(skin::segment_rgb(true, false), skin::pal::kLedAmber));
}

TEST_CASE("skin meter: the segments sit in the window, in order, a 2 px gap between them, a 3 px margin round") {
  for (const auto& bar : {std::make_pair(skin::master_bar_window(0), skin::kMasterSegments), std::make_pair(skin::record_bar_window(1, 0), skin::kRecordSegments)}) {
    const ui::Rect win = bar.first;
    const unsigned n = bar.second;
    ui::Rect prev{};
    for (unsigned i = 0; i < n; ++i) {
      const ui::Rect b = skin::segment_box(win, n, i);
      CHECK(b.x >= win.x + 3);
      CHECK(b.x + b.w <= win.x + win.w - 3);
      CHECK(b.y == win.y + 3);
      CHECK(b.h == win.h - 6);
      CHECK(b.w >= 6);
      if (i != 0) {
        CHECK(b.x - (prev.x + prev.w) == 2);
      }
      prev = b;
    }
    CHECK(prev.x + prev.w == win.x + win.w - 3 - 2);  // the last segment ends a gap short of the margin: the pitch divides the window
    CHECK((win.w - 6) % static_cast<int>(n) == 0);
  }
}

// ---- MASTER's geometry -----------------------------------------------------------------------------------------------------

TEST_CASE("skin master: the two bars sit in the OUTPUT box, one over the other, inside the glass above the PADS line") {
  const ui::Rect glass{3, 69, 794, 297};  // the glass inside its rim, above the PADS line
  const ui::Rect l = skin::master_bar_window(0);
  const ui::Rect r = skin::master_bar_window(1);
  CHECK(inside(l, glass));
  CHECK(inside(r, glass));
  CHECK(disjoint(l, r));
  CHECK(r.y > l.y);
  CHECK(l.x == r.x);
  CHECK(l.w == 6 + static_cast<int>(skin::kMasterSegments) * 10);
  CHECK(l.x >= 424 + 40);          // the channel's letter on the left of the bar
  CHECK(l.x + l.w <= 424 + 360);   // the box's right edge
}

TEST_CASE("skin master: the tempo display takes four cells and a ghost 8 for a tempo under 100") {
  char out[6];
  CHECK(std::strcmp(skin::master_tempo_cells("125.0", out), "125.0") == 0);
  CHECK(std::strcmp(skin::master_tempo_cells("95.5", out), " 95.5") == 0);
  CHECK(std::strcmp(skin::master_tempo_cells("20.0", out), " 20.0") == 0);
  CHECK(std::strcmp(skin::master_tempo_cells("300.0", out), "300.0") == 0);
  CHECK(std::strcmp(skin::master_tempo_cells("1000.0", out), "999.9") == 0);  // past the engine's 300 BPM cap (never reached): not a lost digit
  const skin::SegRun run = skin::seg_text(skin::master_tempo_cells("95.5", out));
  CHECK(run.n == 4u);
  CHECK(run.cell[0] == skin::kSegCellBlank);
  CHECK(run.dot == 2);
}

// ---- RECORD's geometry -----------------------------------------------------------------------------------------------------

TEST_CASE("skin record: four bars in the LEVELS box, IN over OUT, left over right, none touching another") {
  const ui::Rect glass{3, 69, 794, 297};
  std::vector<ui::Rect> w;
  for (unsigned g = 0; g < 2; ++g) {
    for (unsigned c = 0; c < 2; ++c) {
      const ui::Rect b = skin::record_bar_window(g, c);
      CHECK(inside(b, glass));
      CHECK(b.x >= 424 + 40);
      CHECK(b.x + b.w <= 424 + 360);
      CHECK(b.w == 6 + static_cast<int>(skin::kRecordSegments) * 12);
      w.push_back(b);
    }
  }
  for (unsigned i = 0; i < w.size(); ++i) {
    for (unsigned j = i + 1; j < w.size(); ++j) {
      CHECK(disjoint(w[i], w[j]));
      CHECK(w[j].y > w[i].y);
    }
  }
}

TEST_CASE("skin record: the envelope's columns share its width, mirror about the centre line and are at least a pixel high each way") {
  int prev_right = 0;
  for (unsigned c = 0; c < skin::kEnvelopeCols; ++c) {
    const ui::Rect lo = skin::envelope_column_box(c, 0);
    const ui::Rect hi = skin::envelope_column_box(c, kMeterMax);
    CHECK(lo.x == hi.x);
    CHECK(lo.w == hi.w);
    CHECK(lo.h == 2);                        // a silent column is a one pixel line each way
    CHECK(lo.y + lo.h / 2 == hi.y + hi.h / 2);  // the same centre
    CHECK(hi.h == 72);
    CHECK(lo.x >= 32);
    CHECK(hi.x + hi.w <= 32 + 736);
    CHECK(lo.x >= prev_right);               // in order, not overlapping
    prev_right = lo.x + lo.w;
    CHECK(skin::envelope_column_box(c, kMeterMax * 2u).h == 72);  // a level past the scale is clamped
  }
  // Louder is taller.
  CHECK(skin::envelope_column_box(3, 400).h < skin::envelope_column_box(3, 600).h);
}

TEST_CASE("skin record: two of the take's peaks make a column, the larger one; a bar's mark is at the column's edge") {
  Envelope env;
  env.reset(Envelope::kCols);  // one frame a column of the take's 96
  std::vector<std::int16_t> pcm(Envelope::kCols);
  for (unsigned i = 0; i < Envelope::kCols; ++i) {
    pcm[i] = static_cast<std::int16_t>(((i * 37u) % 200u) * 100u + 100u);
  }
  env.add(pcm.data(), 1, 0, Envelope::kCols);
  for (unsigned c = 0; c < skin::kEnvelopeCols; ++c) {
    const unsigned a = env.peak(2 * c);
    const unsigned b = env.peak(2 * c + 1);
    CHECK(skin::envelope_column(env, c) == (a > b ? a : b));
  }
  for (unsigned bars : {2u, 4u, 8u}) {
    for (unsigned bar = 2; bar <= bars; ++bar) {
      const unsigned c = skin::kEnvelopeCols * (bar - 1u) / bars;
      CHECK(skin::envelope_mark_x(bar, bars) == skin::envelope_column_box(c, 500).x);  // the take's bar lines fall between two columns
    }
  }
  // How far a take has come: nothing, half, all (the last column partly).
  CHECK(skin::envelope_reached(0, 1000) == 0u);
  CHECK(skin::envelope_reached(500, 1000) == 24u);
  CHECK(skin::envelope_reached(1000, 1000) == skin::kEnvelopeCols);
  CHECK(skin::envelope_reached(10, 0) == 0u);
}

// ---- The screens ---------------------------------------------------------------------------------------------------------------

TEST_CASE("skin screens: MASTER, RECORD and HOOKS take no kit chrome and the obsidian theme with the skin on, the kit's with it off") {
  Hands t;
  for (const Button b : {Button::Master, Button::SampleRec, Button::SampleLoad}) {
    t.tap(b, 30);
    t.wait(60);
    app::Context& ctx = t.h->runtime().context();
    CHECK(ctx.screen()->chrome(ctx) == app::Chrome::Full);
    CHECK(ctx.screen()->theme(ctx) == nullptr);
    t.skin_on();
    CHECK(ctx.screen()->chrome(ctx) == app::Chrome::None);
    CHECK(ctx.screen()->theme(ctx) == &skin::theme());
    t.skin_off();
  }
}

TEST_CASE("skin master: F1 TEMPO, F3 NUDGE and F4 MASTER knobs, F2 TAP an action key, four empty boxes; the tempo, the readouts and the bars") {
  Hands t;
  t.skin_on();
  t.tap(Button::Master, 30);
  t.wait(100);
  const Queue q;
  CHECK(q.sprites(skin::Layer::KnobCap, skin_assets::Id::Knob42Cap) == 3u);
  CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::ActKey) == 1u);
  CHECK(q.round_rects(skin::Layer::WindowFace, 80, 41) == 4u);                 // F5 to F8
  CHECK(q.sprites(skin::Layer::Seg, skin_assets::Id::Seg, 66) == 4u);          // the tempo, in the glass: four cells (the header's are above y 66)
  CHECK(q.rects(skin::Layer::MeterOff) + q.rects(skin::Layer::MeterOn) == 2u * skin::kMasterSegments);
  CHECK(q.rects(skin::Layer::MeterOn) == 0u);                                  // stopped, nothing has played
  CHECK(master(t).focus() == 0u);
  const MasterView& v = master(t).last_view();
  CHECK(std::strcmp(v.cpu, "--") == 0);                                        // the simulator has no CPU meter
  CHECK(std::strcmp(v.usb, "NO PORT") == 0);                                   // and no USB device
  CHECK_FALSE(v.cpu_over);
  q.intact();
}

TEST_CASE("skin master: the bars are lit as the view's levels and peaks say, and the tap tempo's chip is in the glass") {
  Hands t;
  t.skin_on();
  t.tap(Button::Master, 30);
  t.tap(Button::Play);
  t.wait(1500);
  const Queue q;
  const MasterView& v = master(t).last_view();
  unsigned want = 0;
  for (unsigned c = 0; c < 2; ++c) {
    for (unsigned i = 0; i < skin::kMasterSegments; ++i) {
      want += skin::segment_lit(i, skin::kMasterSegments, v.level[c], v.peak[c]) ? 1u : 0u;
    }
  }
  CHECK(want > 0u);  // the factory line is audible
  CHECK(q.rects(skin::Layer::MeterOn) == want);
  CHECK(q.rects(skin::Layer::MeterOff) == 2u * skin::kMasterSegments - want);
  // The segments' colours: green below the top quarter, amber in it.
  CHECK(q.rects(skin::Layer::MeterOn, skin::pal::kLedGreen) > 0u);
  const unsigned amber_on = q.rects(skin::Layer::MeterOn, skin::pal::kLedAmber);
  CHECK(amber_on <= 2u * (skin::kMasterSegments - skin::kMasterSegments * 3u / 4u));
  t.tap(Button::Play);
  // Two taps: the chip "TAP 2" (a Sel round rect on the Tab layer) appears.
  const ui::Rgb sel = skin::theme().rgb(ui::Color::Sel);
  CHECK(q.round_rects(skin::Layer::Tab, sel) == 0u);
  t.tap(Button::F2, 30);
  t.wait(400);
  t.tap(Button::F2, 30);
  t.wait(100);
  CHECK(master(t).taps() == 2u);
  CHECK(q.round_rects(skin::Layer::Tab, sel) == 1u);
  q.intact();
}

TEST_CASE("skin master: the fullest frame (playing, nudged, taps, a toast) is queued whole") {
  Hands t;
  t.skin_on();
  t.tap(Button::Master, 30);
  t.tap(Button::Play);
  t.press(Button::F3);
  t.turn(3);
  t.wait(400);
  t.tap(Button::F2, 30);
  t.wait(300);
  t.tap(Button::F2, 30);  // the toast TAP 200.0 in the PAGE window
  t.wait(60);
  const Queue q;
  CHECK(q.q.dropped() == 0u);
  MESSAGE("MASTER worst frame: " << q.q.shapes() << " shapes of " << skin::Panel::kShapes << ", " << q.q.texts() << " texts of " << skin::Panel::kTexts);
  CHECK(q.q.shapes() + 40u <= skin::Panel::kShapes);
  CHECK(q.q.texts() + 20u <= skin::Panel::kTexts);
  t.release(Button::F3);
}

TEST_CASE("skin record: five action keys and two knobs on the F row, F8 an empty box; ARM is lit red only while a take records; SAVE and DISCARD are dim until one is done") {
  Hands t;
  t.skin_on();
  t.tap(Button::SampleRec, 30);
  t.wait(100);
  const Queue q;
  CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::ActKey) == 5u);       // F1, F2, F3, F6, F7
  CHECK(q.sprites(skin::Layer::KnobCap, skin_assets::Id::Knob42Cap) == 2u);   // F4 GAIN, F5 MONITOR
  CHECK(q.round_rects(skin::Layer::WindowFace, 80, 41) == 1u);                // F8
  ui::Rgb tint{};
  // The F row's action key of F1 is at (8, 405), F6's at (508, 405), F7's at (608, 405).
  REQUIRE(q.tint_at(skin::Layer::ActKey, skin_assets::Id::ActKey, 8, 405, tint));
  CHECK(same(tint, skin::pal::kKey));
  REQUIRE(q.tint_at(skin::Layer::ActKey, skin_assets::Id::ActKey, 508, 405, tint));
  CHECK(same(tint, skin::pal::kKeyDim));
  REQUIRE(q.tint_at(skin::Layer::ActKey, skin_assets::Id::ActKey, 608, 405, tint));
  CHECK(same(tint, skin::pal::kKeyDim));
  CHECK(q.rects(skin::Layer::MeterOff) + q.rects(skin::Layer::MeterOn) == 4u * skin::kRecordSegments);
  CHECK(q.rects(skin::Layer::Wave) == 0u);  // no take: the hint, no columns

  // A one-bar take: ARM turns to a red STOP while it records, the envelope grows in red.
  set_bars(t, 1);
  t.tap(Button::Play);
  t.tap(Button::F1);
  REQUIRE(wait_state(t, hal_state::Recording, 4000));
  t.wait(500);
  REQUIRE(q.tint_at(skin::Layer::ActKey, skin_assets::Id::ActKey, 8, 405, tint));
  CHECK(same(tint, skin::pal::kFocusFill));
  CHECK(q.rects(skin::Layer::Wave) >= 1u);
  CHECK(q.rects(skin::Layer::Wave, skin::theme().rgb(ui::Color::Rec)) == q.rects(skin::Layer::Wave));
  REQUIRE(q.tint_at(skin::Layer::ActKey, skin_assets::Id::ActKey, 508, 405, tint));
  CHECK(same(tint, skin::pal::kKeyDim));  // nothing to save yet

  // Done: ARM is a plain key again, SAVE and DISCARD are lit, the envelope is green.
  REQUIRE(wait_state(t, hal_state::Done, 4000));
  t.wait(100);
  REQUIRE(q.tint_at(skin::Layer::ActKey, skin_assets::Id::ActKey, 8, 405, tint));
  CHECK(same(tint, skin::pal::kKey));
  REQUIRE(q.tint_at(skin::Layer::ActKey, skin_assets::Id::ActKey, 508, 405, tint));
  CHECK(same(tint, skin::pal::kKey));
  REQUIRE(q.tint_at(skin::Layer::ActKey, skin_assets::Id::ActKey, 608, 405, tint));
  CHECK(same(tint, skin::pal::kKey));
  CHECK(q.rects(skin::Layer::Wave, skin::theme().rgb(ui::Color::Play)) == q.rects(skin::Layer::Wave));
  CHECK(q.rects(skin::Layer::Wave) == skin::kEnvelopeCols);  // a finished take fills the box
  q.intact();
}

TEST_CASE("skin record: the envelope's idle hint is the arming one when the bars fit, and says why F1 refuses when they do not") {
  // The hint is the Micro-face text centred in the envelope's box (x 32, 736 wide); its two sentences differ in length, so its place tells which is up.
  const auto hint_x = [](const char* sentence) { return 32 + (736 - static_cast<int>(std::strlen(sentence)) * 6) / 2; };
  const auto micro_at = [](int x) {
    const skin::Panel& q = skin::panel();
    for (unsigned i = 0; i < q.texts(); ++i) {
      skin::Panel::TextInfo ti;
      if (q.text_at(i, ti) && ti.face == ui::Face::Micro && ti.x == x) {
        return true;
      }
    }
    return false;
  };
  const int fits = hint_x("F1 ARMS A TAKE AT THE NEXT BAR . SHIFT + REC DOES IT FROM ANY SCREEN");
  const int too_long = hint_x("THE TAKE IS TOO LONG FOR THE BUFFER . FEWER BARS, OR A FASTER TEMPO");
  REQUIRE(fits != too_long);
  Hands t;
  t.skin_on();
  t.tap(Button::SampleRec, 30);
  set_bars(t, 8);
  t.wait(100);
  CHECK(record(t).last_view().fits);
  CHECK(micro_at(fits));
  CHECK_FALSE(micro_at(too_long));
  t.tap(Button::Master, 30);  // the slowest tempo: 8 bars no longer fit the buffer
  t.turn(-70);
  t.turn(-70);
  t.tap(Button::SampleRec, 30);
  t.wait(100);
  CHECK_FALSE(record(t).last_view().fits);
  CHECK(micro_at(too_long));
  CHECK_FALSE(micro_at(fits));
}

TEST_CASE("skin record: the bars are the view's levels, and IN is dark while the source is the master") {
  Hands t;
  t.skin_on();
  t.tap(Button::SampleRec, 30);
  t.tap(Button::Play);
  t.wait(1500);
  const Queue q;
  const RecordView& v = record(t).last_view();
  CHECK_FALSE(v.input_meters);
  unsigned want = 0;
  for (unsigned c = 0; c < 2; ++c) {
    for (unsigned i = 0; i < skin::kRecordSegments; ++i) {
      want += skin::segment_lit(i, skin::kRecordSegments, v.out[c], v.out_peak[c]) ? 1u : 0u;  // IN is not live: no segment of it is lit
    }
  }
  CHECK(want > 0u);
  CHECK(q.rects(skin::Layer::MeterOn) == want);
  t.tap(Button::Play);
}

TEST_CASE("skin record: the TAKE LED in the header of every skinned screen: red while it records, amber when it is ready") {
  Hands t;
  t.skin_on();
  t.tap(Button::SampleRec, 30);
  t.wait(100);
  const Queue q;
  CHECK(same(q.take_led(), skin::pal::kLedOff));  // no take
  set_bars(t, 0);  // a FREE take records until F1 stops it, so all seven screens are seen while it records
  t.tap(Button::F1);
  REQUIRE(wait_state(t, hal_state::Recording, 4000));
  t.wait(300);
  struct Screen {
    Button button;
    const char* name;
  };
  // HOME, PERFORM, INSTRUMENT, MASTER, RECORD, HOOKS and SONG are the skinned screens that draw the header.
  const Screen screens[] = {{Button::Pattern, "HOME"},  {Button::Performance, "PERFORM"}, {Button::Params, "INSTRUMENT"}, {Button::Master, "MASTER"},
                            {Button::SampleRec, "RECORD"}, {Button::SampleLoad, "HOOKS"},   {Button::Song, "SONG"}};
  for (const Screen& s : screens) {
    t.tap(s.button, 30);
    t.wait(80);
    REQUIRE_MESSAGE(recorder(t).state() == hal_state::Recording, s.name << ": the take is still recording");
    CHECK_MESSAGE(same(q.take_led(), skin::pal::kLedRed), s.name << ": the TAKE LED is red while it records");
  }
  t.tap(Button::SampleRec, 30);
  t.tap(Button::F1);  // stops the take
  REQUIRE(wait_state(t, hal_state::Done, 4000));
  for (const Screen& s : screens) {
    t.tap(s.button, 30);
    t.wait(80);
    REQUIRE_MESSAGE(recorder(t).state() == hal_state::Done, s.name << ": the take is done");
    CHECK_MESSAGE(same(q.take_led(), skin::pal::kLedAmber), s.name << ": the TAKE LED is amber when the take is ready");
  }
  // The take dropped (F7 twice on RECORD), the LED is dark again everywhere.
  t.tap(Button::SampleRec, 30);
  t.tap(Button::F7);
  t.tap(Button::F7);
  t.wait(100);
  t.tap(Button::Pattern, 30);
  t.wait(80);
  CHECK(same(q.take_led(), skin::pal::kLedOff));
}

TEST_CASE("skin record: the fullest frame (an 8 bar take done, a toast in the PAGE window) is queued whole") {
  Hands t;
  t.skin_on();
  t.tap(Button::SampleRec, 30);
  set_bars(t, 8);
  t.tap(Button::Play);
  t.tap(Button::F1);
  REQUIRE(wait_state(t, hal_state::Recording, 4000));
  REQUIRE(wait_state(t, hal_state::Done, 20000));
  t.tap(Button::F7);  // "DISCARD? PRESS F7 AGAIN" in the PAGE window; DISCARD asks, the rest of the frame is as full as it gets
  t.wait(60);
  const Queue q;
  CHECK(q.rects(skin::Layer::Wave) == skin::kEnvelopeCols);
  CHECK(q.rects(skin::Layer::Rule) >= 7u);  // the bar marks (and the boxes' rules)
  CHECK(q.q.dropped() == 0u);
  MESSAGE("RECORD worst frame: " << q.q.shapes() << " shapes of " << skin::Panel::kShapes << ", " << q.q.texts() << " texts of " << skin::Panel::kTexts);
  CHECK(q.q.shapes() + 40u <= skin::Panel::kShapes);
  CHECK(q.q.texts() + 20u <= skin::Panel::kTexts);
  t.tap(Button::F7);
  t.tap(Button::Play);
}

TEST_CASE("skin hooks: 48 dark lamps in four rows of twelve, the line, eight empty boxes and no knob") {
  Hands t;
  t.skin_on();
  t.tap(Button::SampleLoad, 30);
  t.wait(100);
  const Queue q;
  CHECK(std::strcmp(t.title(), "HOOKS") == 0);
  CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::Lamp) == 48u);
  CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::ActKey) == 0u);
  CHECK(q.sprites(skin::Layer::KnobCap, skin_assets::Id::Knob42Cap) == 0u);
  CHECK(q.round_rects(skin::Layer::WindowFace, 80, 41) == 8u);
  for (unsigned row = 0; row < 4; ++row) {
    for (unsigned col = 0; col < 12; ++col) {
      const ui::Rect b = skin::hooks_lamp_box(row, col);
      ui::Rgb tint{};
      REQUIRE(q.tint_at(skin::Layer::ActKey, skin_assets::Id::Lamp, b.x, b.y, tint));
      CHECK(same(tint, skin::lamp_tint(LampTone::Off)));
      CHECK(inside(b, ui::Rect{3, 69, 794, 297}));
    }
  }
  const ui::Rect first = skin::hooks_lamp_box(0, 0);
  const ui::Rect last = skin::hooks_lamp_box(0, 11);
  const int slack = first.x - (ui::kScreenW - (last.x + last.w));
  CHECK(slack >= -1);
  CHECK(slack <= 1);  // centred, to a pixel
  q.intact();
  MESSAGE("HOOKS frame: " << q.q.shapes() << " shapes of " << skin::Panel::kShapes << ", " << q.q.texts() << " texts of " << skin::Panel::kTexts);
}

// ---- Shift on the F row --------------------------------------------------------------------------------------------------------

// The classic MASTER and RECORD draw their knob strip from the view whatever Shift says (their soft_keys() is empty, and the encoder still drives a knob under Shift, coarser), and HOOKS has
// eight empty keys; so the skinned row under Shift is the row without it, slot for slot (FILE, whose classic row is empty under Shift, is in skin_file_test.cpp).
TEST_CASE("skin screens: Shift on MASTER, RECORD and HOOKS leaves the F row's slots as they are, as the classic strip does") {
  Hands t;
  t.skin_on();
  const Queue q;
  struct Row {
    Button button;
    unsigned caps;
    unsigned keys;
    unsigned boxes;
  };
  for (const Row& row : {Row{Button::Master, 3u, 1u, 4u}, Row{Button::SampleRec, 2u, 5u, 1u}, Row{Button::SampleLoad, 0u, 0u, 8u}}) {
    t.tap(row.button, 30);
    t.wait(100);
    CHECK(q.sprites(skin::Layer::KnobCap, skin_assets::Id::Knob42Cap) == row.caps);
    CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::ActKey) == row.keys);
    CHECK(q.round_rects(skin::Layer::WindowFace, 80, 41) == row.boxes);
    t.press(Button::Shift);
    t.wait(100);
    CHECK(q.sprites(skin::Layer::KnobCap, skin_assets::Id::Knob42Cap) == row.caps);
    CHECK(q.sprites(skin::Layer::ActKey, skin_assets::Id::ActKey) == row.keys);
    CHECK(q.round_rects(skin::Layer::WindowFace, 80, 41) == row.boxes);
    t.release(Button::Shift);
    t.wait(60);
  }
}
