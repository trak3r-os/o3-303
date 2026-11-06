// SPDX-License-Identifier: GPL-3.0-or-later
// The obsidian skin's INSTRUMENT (docs/SKIN-PLAN.md 2.4, SK5): the pure rules (which LED a step lights, the key's tint, where each part of the strip sits, the face a value takes) and the screen
// end to end in the simulator: with the skin on, the frame INSTRUMENT queues has what its view says (the eight knobs of the page, the sixteen keys and LEDs, the cursor's ring, a held
// step's ring, the page tabs, Shift's LEARN labels, a lock in the readout), and the Panel never drops a shape or a text, in the fullest frame there is. The pictures are skin_instrument.txt's.
#include <doctest/doctest.h>

#include "hands.h"
#include "../app/src/instrument_screen.h"
#include "../app/src/skin/instrument.h"
#include "../app/src/project_service.h"
#include "../app/src/skin/panel.h"

#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/platform/host/fake_midi.h>
#include <o3_303/app_access.h>
#include <o3_303/pages.h>
#include <o3_303/seq/bank.h>
#include <o3_303/step_model.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace hands;
using namespace o3_303;

namespace {

constexpr int kTabsY = 428;         // the page tabs' row (skin/instrument.cpp kTabY)
constexpr int kLabelY = 70;         // the label line of a knob slot: its tab is a round rect at this y
constexpr int kReadoutFaceY = 163;  // a readout window's face, one px above the window (y 164)
constexpr int kEmptyFaceY = 105;    // a blank slot's well face, one px above the well (y 106)
constexpr int kTaglineX = 14;    // the brand's line under the wordmark (skin/header.cpp)
constexpr int kTaglineY = 52;

bool same(const ui::Rgb& a, const ui::Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
bool same(const ui::Rect& a, const ui::Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

InstrumentCell cell(bool on, bool accent, bool slide) {
  InstrumentCell c;
  c.on = on;
  c.accent = accent;
  c.slide = slide;
  return c;
}

// The four skinned shapes of the one screen class: INSTRUMENT's pages VOICE and FX, then GROWL and WOBBLE (the Sample buttons', one page each).
enum class Where : std::uint8_t { Voice, Fx, Notes, Tone, Growl, Wobble };
constexpr Where kAllScreens[6] = {Where::Voice, Where::Fx, Where::Notes, Where::Tone, Where::Growl, Where::Wobble};
// The screen's title, which is the name too (INSTRUMENT's two pages share the word, so these two are their tabs).
const char* where_name(Where w) {
  switch (w) {
    case Where::Voice: return "VOICE";
    case Where::Fx: return "FX";
    case Where::Notes: return "MODDED NOTES";
    case Where::Tone: return "MODDED TONE";
    case Where::Growl: return "GROWL";
    case Where::Wobble: return "WOBBLE";
  }
  return "";
}
// The page of the group the screen shows (GROWL and WOBBLE have the one): the second tab for FX and TONE.
unsigned where_page(Where w) { return w == Where::Fx || w == Where::Tone ? 1u : 0u; }
// Opens the screen from the 303 screen the way a finger does.
void go_to(Hands& t, Where w) {
  switch (w) {
    case Where::Voice: t.to_instrument(kInstVoice); break;
    case Where::Fx: t.to_instrument(kInstFx); break;
    case Where::Notes: t.to_modded(0); break;
    case Where::Tone: t.to_modded(1); break;
    case Where::Growl: t.to_growl(); break;
    case Where::Wobble: t.to_wobble(); break;
  }
}

// INSTRUMENT, GROWL or WOBBLE on the skin, with the last frame's queue to look at.
struct OnInstrument {
  Hands t;
  InstrumentScreen* s = nullptr;
  explicit OnInstrument(Where w = Where::Voice) { start(w); }
  // On a card (the simulator has none by itself): the harness is made again with it.
  OnInstrument(Where w, platform::host::HostCard& card) {
    t.h.reset();
    t.h = std::make_unique<sim::Harness>(o3sdk_app(), nullptr, nullptr, &card, "o3_303");
    t.boot();
    start(w);
  }
  void start(Where w) {
    t.skin_on();
    go_to(t, w);
    settle();
  }
  void settle() {
    t.wait(60);
    s = static_cast<InstrumentScreen*>(t.h->runtime().context().screen());
  }

  const skin::Panel& q() const { return skin::panel(); }
  unsigned sprites(skin::Layer layer, skin_assets::Id id) const {
    unsigned n = 0;
    for (unsigned i = 0; i < q().shapes(); ++i) {
      skin::Panel::SpriteInfo sp;
      if (q().sprite_at(i, sp) && sp.layer == layer && sp.id == id) {
        ++n;
      }
    }
    return n;
  }
  // The HeldRim round rects at `box` in `rgb`, and all of them.
  unsigned rings_at(const ui::Rect& box, const ui::Rgb& rgb) const {
    unsigned n = 0;
    for (unsigned i = 0; i < q().shapes(); ++i) {
      skin::Panel::RoundRectInfo rr;
      if (q().round_rect_at(i, rr) && rr.layer == skin::Layer::HeldRim && same(rr.box, box) && same(rr.rgb, rgb)) {
        ++n;
      }
    }
    return n;
  }
  unsigned rings() const {
    unsigned n = 0;
    for (unsigned i = 0; i < q().shapes(); ++i) {
      skin::Panel::RoundRectInfo rr;
      n += q().round_rect_at(i, rr) && rr.layer == skin::Layer::HeldRim ? 1u : 0u;
    }
    return n;
  }
  // The texts queued with a y in [y0, y1) and in colour `c`. (What a text says is not read back: the Panel keeps a pointer, and the frame's buffers are gone once it is drawn.)
  unsigned texts_in(int y0, int y1, ui::Color c) const {
    unsigned n = 0;
    for (unsigned i = 0; i < q().texts(); ++i) {
      skin::Panel::TextInfo tx;
      n += q().text_at(i, tx) && tx.y >= y0 && tx.y < y1 && tx.color == c ? 1u : 0u;
    }
    return n;
  }
  // The tint of the LED sprite at the centre of the strip's (row, step k).
  bool led_tint_at(skin::StripRow row, unsigned k, ui::Rgb& out) const {
    const skin::Pt c = skin::led_centre(row, k);
    for (unsigned i = 0; i < q().shapes(); ++i) {
      skin::Panel::SpriteInfo sp;
      if (q().sprite_at(i, sp) && sp.layer == skin::Layer::Led && sp.id == skin_assets::Id::Led && sp.x == c.x - 8 && sp.y == c.y - 8) {
        out = sp.rgb;
        return true;
      }
    }
    return false;
  }
  void intact() const {
    CHECK(q().dropped() == 0u);
    CHECK(q().shapes() < skin::Panel::kShapes);
    CHECK(q().texts() < skin::Panel::kTexts);
  }
};

}  // namespace

TEST_CASE("skin instrument: the strip is sixteen columns of 45 px, a key and three LEDs in each, inside the screen") {
  for (unsigned k = 0; k < 16; ++k) {
    CHECK(skin::step_cx(k) == skin::step_cx(0) + 45 * static_cast<int>(k));
    const ui::Rect kb = skin::key_box(k);
    CHECK(kb.x >= 0);
    CHECK(kb.x + kb.w <= 800);
    CHECK(kb.y + kb.h <= 480);
    CHECK(kb.x + kb.w / 2 == skin::step_cx(k));
    // The rings are inside the key's box, the held one a pixel outside the cursor's, and both leave the neighbour's key alone.
    const ui::Rect cur = skin::ring_box(k, false);
    const ui::Rect held = skin::ring_box(k, true);
    CHECK(held.x < cur.x);
    CHECK(held.x + held.w > cur.x + cur.w);
    CHECK(held.y < cur.y);
    CHECK(held.x >= kb.x);
    CHECK(held.x + held.w <= kb.x + kb.w);
    CHECK(held.y + held.h <= kb.y + kb.h);
    if (k > 0) {
      const ui::Rect prev = skin::key_box(k - 1);
      CHECK(prev.x + prev.w <= kb.x);
    }
    // The three LEDs of the column are over the key, in order, in its column, and 8 px from the sprite's box to the centre fits the screen.
    const skin::Pt a = skin::led_centre(skin::StripRow::Accent, k);
    const skin::Pt sl = skin::led_centre(skin::StripRow::Slide, k);
    const skin::Pt st = skin::led_centre(skin::StripRow::Step, k);
    CHECK(a.x == skin::step_cx(k));
    CHECK(sl.x == a.x);
    CHECK(st.x == a.x);
    CHECK(a.y < sl.y);
    CHECK(sl.y < st.y);
    CHECK(st.y + 8 <= kb.y);
  }
}

TEST_CASE("skin instrument: a step lights ACCENT and SLIDE as its gate says and STEP for a note, a tie or the playhead") {
  // A rest lights nothing; a note is dim on STEP; a tie too; the playhead is bright whatever the step holds.
  const skin::StepLeds rest = skin::step_leds(cell(false, false, false), false);
  CHECK(!rest.accent);
  CHECK(!rest.slide);
  CHECK(rest.step == skin::StripLed::Off);
  const skin::StepLeds note = skin::step_leds(cell(true, false, false), false);
  CHECK(note.step == skin::StripLed::Dim);
  const skin::StepLeds as = skin::step_leds(cell(true, true, true), false);
  CHECK(as.accent);
  CHECK(as.slide);
  CHECK(as.step == skin::StripLed::Dim);
  CHECK(skin::step_leds(cell(false, false, false), true).step == skin::StripLed::Lit);
  CHECK(skin::step_leds(cell(true, true, false), true).step == skin::StripLed::Lit);
  CHECK(skin::step_leds(cell(true, true, false), true).accent);
}

TEST_CASE("skin instrument: an LED's tint says its row and its state, and the key's tint alternates by groups of four") {
  const skin::StepLeds off = skin::step_leds(cell(false, false, false), false);
  const skin::StepLeds dim = skin::step_leds(cell(true, false, false), false);
  const skin::StepLeds all = skin::step_leds(cell(true, true, true), true);
  // Off is the same socket colour in every row; lit is a colour of its row; a dim STEP is neither.
  CHECK(same(skin::led_tint(skin::StripRow::Accent, off), skin::led_tint(skin::StripRow::Slide, off)));
  CHECK(same(skin::led_tint(skin::StripRow::Slide, off), skin::led_tint(skin::StripRow::Step, off)));
  const ui::Rgb a = skin::led_tint(skin::StripRow::Accent, all);
  const ui::Rgb s = skin::led_tint(skin::StripRow::Slide, all);
  const ui::Rgb p = skin::led_tint(skin::StripRow::Step, all);
  const ui::Rgb d = skin::led_tint(skin::StripRow::Step, dim);
  const ui::Rgb o = skin::led_tint(skin::StripRow::Step, off);
  CHECK(!same(a, s));
  CHECK(!same(a, p));
  CHECK(!same(s, p));
  CHECK(!same(d, p));
  CHECK(!same(d, o));
  CHECK(!same(a, o));
  CHECK(!same(s, o));
  CHECK(!same(p, o));
  // The key: steps 1..4 and 9..12 one tint, 5..8 and 13..16 the other.
  for (unsigned k = 0; k < 16; ++k) {
    const bool first = (k / 4u) % 2u == 0u;
    CHECK(same(skin::key_tint(k), skin::key_tint(first ? 0u : 4u)));
    CHECK(!same(skin::key_tint(k), skin::key_tint(first ? 4u : 0u)));
  }
}

TEST_CASE("skin instrument: a note's ink is the playhead's over the cursor's over the text, dim on a rest") {
  CHECK(skin::note_ink(true, true, true) == ui::Color::Play);
  CHECK(skin::note_ink(true, false, false) == ui::Color::Play);
  CHECK(skin::note_ink(false, true, true) == ui::Color::Instr);
  CHECK(skin::note_ink(false, false, false) == ui::Color::Text);
  CHECK(skin::note_ink(false, false, true) == ui::Color::TextDim);
}

TEST_CASE("skin instrument: a value takes the biggest face that fits its window with a margin") {
  CHECK(skin::value_face(1, 58) == ui::Face::Data);
  CHECK(skin::value_face(5, 58) == ui::Face::Data);   // 50 px in 52
  CHECK(skin::value_face(6, 58) == ui::Face::Label);  // 48 px in 52
  CHECK(skin::value_face(7, 58) == ui::Face::Micro);  // 56 px in 52 does not fit the Label: the Micro, 42 px
  for (unsigned n = 1; n < 12; ++n) {
    // Whatever the face, it fits its window, down to the Micro's width; and it never grows with the text.
    const ui::Face f = skin::value_face(n, 90);
    const int cw = f == ui::Face::Data ? 10 : (f == ui::Face::Label ? 8 : 6);
    CHECK(static_cast<int>(n) * cw <= 90 - 6);
    const ui::Face g = skin::value_face(n + 1, 90);
    CHECK((g == f || (f == ui::Face::Data) || (f == ui::Face::Label && g == ui::Face::Micro)));
  }
}

TEST_CASE("skin instrument: the panel at full height has a base over the whole screen and no glass") {
  skin::Panel p;
  p.clear();
  skin::queue_panel_full(p, 66);
  CHECK(p.shapes() > 0u);
  CHECK(p.dropped() == 0u);
  for (unsigned i = 0; i < p.shapes(); ++i) {
    skin::Panel::RoundRectInfo rr;
    CHECK_FALSE((p.round_rect_at(i, rr) && (rr.layer == skin::Layer::GlassRim || rr.layer == skin::Layer::GlassFace)));
  }
}

TEST_CASE("skin instrument: VOICE queues the eight knobs, sixteen keys, forty-eight LEDs and the tabs") {
  OnInstrument o;
  REQUIRE(std::strncmp(o.t.title(), "INSTRUMENT", 10) == 0);
  InstrumentView v;
  o.s->fill_view(o.t.h->runtime().context(), v);
  unsigned knobs = 0;
  for (unsigned i = 0; i < view::kKnobs; ++i) {
    knobs += (!v.knob[i].blank && v.knob[i].norm >= 0.0f && !v.knob[i].toggle) ? 1u : 0u;
  }
  CHECK(knobs > 0u);
  CHECK(o.sprites(skin::Layer::KnobCap, skin_assets::Id::Knob58Cap) == knobs);
  CHECK(o.sprites(skin::Layer::KeyBody, skin_assets::Id::Key) == v.cells);
  CHECK(o.sprites(skin::Layer::KeyLight, skin_assets::Id::Key) == v.cells);
  CHECK(o.sprites(skin::Layer::KeyDark, skin_assets::Id::Key) == v.cells);
  CHECK(o.sprites(skin::Layer::Led, skin_assets::Id::Led) >= 3u * v.cells);  // the strip's, and the knobs' latch LEDs
  // The tabs: the group's two (VOICE, FX) on the row, the shown page's in the Rec colour and no other.
  unsigned tabs = 0;
  unsigned red = 0;
  const ui::Rgb rec = skin::theme().rgb(ui::Color::Rec);
  for (unsigned i = 0; i < o.q().shapes(); ++i) {
    skin::Panel::RoundRectInfo rr;
    if (o.q().round_rect_at(i, rr) && rr.layer == skin::Layer::Tab && rr.box.y == kTabsY) {
      ++tabs;
      red += same(rr.rgb, rec) ? 1u : 0u;
    }
  }
  CHECK(tabs == 2u);
  CHECK(red == 1u);
  // The tabs' names: the shown page's in the selected ink, the other dim.
  CHECK(o.texts_in(kTabsY, kTabsY + 4, ui::Color::SelText) == 1u);
  CHECK(o.texts_in(kTabsY, kTabsY + 4, ui::Color::TextDim) == 1u);
  o.intact();
}

TEST_CASE("skin instrument: each group draws its own tabs (two on INSTRUMENT and MODDED, one on GROWL and WOBBLE), the shown page's red and its name lit, and the sections of the page are boxes of read-outs") {
  for (const Where w : kAllScreens) {
    const unsigned page = where_page(w);
    const unsigned tabs_expected = w == Where::Growl || w == Where::Wobble ? 1u : 2u;
    OnInstrument o(w);
    if (w == Where::Voice || w == Where::Fx) {
      REQUIRE(std::strncmp(o.t.title(), "INSTRUMENT", 10) == 0);
    } else {
      REQUIRE(std::strcmp(o.t.title(), where_name(w)) == 0);
    }
    unsigned tabs = 0;
    unsigned red = 0;
    int red_x = -1;
    int last_x = -1;
    const ui::Rgb rec = skin::theme().rgb(ui::Color::Rec);
    for (unsigned i = 0; i < o.q().shapes(); ++i) {
      skin::Panel::RoundRectInfo rr;
      if (!o.q().round_rect_at(i, rr) || rr.layer != skin::Layer::Tab || rr.box.y != kTabsY) {
        continue;
      }
      CHECK_MESSAGE(rr.box.x > last_x, where_name(w));  // left to right, in page order
      last_x = rr.box.x;
      ++tabs;
      if (same(rr.rgb, rec)) {
        ++red;
        red_x = rr.box.x;
      }
    }
    CHECK_MESSAGE(tabs == tabs_expected, where_name(w));
    CHECK_MESSAGE(red == 1u, where_name(w));
    // The red one is the page's: the first tab, or the second after VOICE's (56 px and a gap of 8).
    CHECK_MESSAGE(red_x == (page == 0u ? 60 : 60 + 5 * 8 + 16 + 8), where_name(w));
    CHECK_MESSAGE(o.texts_in(kTabsY, kTabsY + 4, ui::Color::SelText) == 1u, where_name(w));  // the shown page's name; the others' are dim
    CHECK_MESSAGE(o.texts_in(kTabsY, kTabsY + 4, ui::Color::TextDim) == tabs_expected - 1u, where_name(w));
    InstrumentView v;
    o.s->fill_view(o.t.h->runtime().context(), v);
    CHECK_MESSAGE(v.page == page, where_name(w));
    // One title in the legend of each section's box, one window text for each read-out.
    unsigned values = 0;
    for (unsigned sct = 0; sct < v.sections; ++sct) {
      values += v.section[sct].count;
    }
    CHECK_MESSAGE(o.texts_in(190, 191, ui::Color::ChromeDim) == v.sections, where_name(w));
    CHECK_MESSAGE(o.texts_in(220, 250, ui::Color::Text) == values, where_name(w));
    o.intact();
  }
}

TEST_CASE("skin instrument: F8 of MODDED and WOBBLE is a key where a knob would be (no pointer), dark while off and lit red while on; GROWL's is dark while the Mix is 0") {
  const auto keys = [](const OnInstrument& o, bool lit) {
    unsigned n = 0;
    for (unsigned i = 0; i < o.q().shapes(); ++i) {
      skin::Panel::SpriteInfo sp;
      if (o.q().sprite_at(i, sp) && sp.layer == skin::Layer::ActKey && sp.id == skin_assets::Id::ActKey) {
        n += same(sp.rgb, lit ? skin::pal::kFocusFill : skin::pal::kKey) ? 1u : 0u;
      }
    }
    return n;
  };
  for (const Where w : {Where::Notes, Where::Tone}) {  // STOCK
    OnInstrument o(w);
    INFO(where_name(w));
    CHECK(keys(o, true) == 1u);  // MODS: lit while the mods are heard, which they are at boot
    CHECK(keys(o, false) == 0u);
    // Seven knobs on NOTES, five on TONE: the key and the empty boxes have no cap (the sprites of the knob row are the 58 px caps).
    CHECK(o.sprites(skin::Layer::KnobCap, skin_assets::Id::Knob58Cap) == (w == Where::Notes ? 7u : 5u));
    o.t.tap(Button::F8);  // MODS off: the key goes dark
    o.t.wait(60);
    CHECK(keys(o, false) == 1u);
    CHECK(keys(o, true) == 0u);
    o.t.tap(Button::F8);
    o.t.wait(60);
    CHECK(keys(o, true) == 1u);
    o.intact();
  }
  {  // WOB: the latch of PERFORM's wobble action
    OnInstrument o(Where::Wobble);
    CHECK(keys(o, false) == 1u);
    o.t.tap(Button::F8);
    o.t.wait(60);
    CHECK(keys(o, true) == 1u);
    o.intact();
  }
  {
    OnInstrument o(Where::Growl);  // F8 is the growl's key since B5a: dark while the Mix is 0, lit once it is heard
    CHECK(keys(o, false) == 1u);
    CHECK(keys(o, true) == 0u);
    o.t.tap(Button::F8);
    o.t.wait(60);
    CHECK(keys(o, true) == 1u);
    CHECK(keys(o, false) == 0u);
  }
}

TEST_CASE("skin instrument: a blank slot is an empty well with its F number, and no label, latch LED or readout; the other slots have all three") {
  struct Blanks {
    Where w;
    unsigned count;  // F6 and F7 on TONE and WOBBLE; INSTRUMENT, NOTES and GROWL have none (F8 of NOTES is the MODS key, GROWL's the growl's)
  };
  constexpr Blanks kBlanks[6] = {{Where::Voice, 0}, {Where::Fx, 0}, {Where::Notes, 0}, {Where::Tone, 2}, {Where::Growl, 0}, {Where::Wobble, 2}};
  for (const Blanks& expect : kBlanks) {
    OnInstrument o(expect.w);
    INFO(where_name(expect.w));
    unsigned wells = 0;
    unsigned labels = 0;
    unsigned readouts = 0;
    for (unsigned i = 0; i < o.q().shapes(); ++i) {
      skin::Panel::RoundRectInfo rr;
      if (!o.q().round_rect_at(i, rr)) {
        continue;
      }
      wells += rr.layer == skin::Layer::WindowFace && rr.box.y == kEmptyFaceY && rr.box.w == 80 && rr.box.h == 41 ? 1u : 0u;
      labels += rr.layer == skin::Layer::Tab && rr.box.y == kLabelY ? 1u : 0u;
      readouts += rr.layer == skin::Layer::WindowFace && rr.box.y == kReadoutFaceY && rr.box.w == 92 ? 1u : 0u;
    }
    unsigned leds = 0;
    for (unsigned i = 0; i < o.q().shapes(); ++i) {
      skin::Panel::SpriteInfo sp;
      leds += o.q().sprite_at(i, sp) && sp.layer == skin::Layer::Led && sp.y == 146 ? 1u : 0u;  // the latch LEDs' row (the strip's are lower)
    }
    CHECK(wells == expect.count);
    CHECK(o.texts_in(118, 119, ui::Color::TextDim) == expect.count);  // each well's "Fn", centred in it
    CHECK(labels == 8u - expect.count);
    CHECK(readouts == 8u - expect.count);
    CHECK(leds == 8u - expect.count);
    o.intact();
  }
}

TEST_CASE("skin instrument: the ON under a lit key is read at full strength, the OFF under a dark one is quiet") {
  // The hint window of F8 (x 700..800, y 164..186): its text's colour.
  const auto hint = [](const OnInstrument& o) {
    unsigned n = 0;
    ui::Color c = ui::Color::Empty;
    for (unsigned i = 0; i < o.q().texts(); ++i) {
      skin::Panel::TextInfo tx;
      if (o.q().text_at(i, tx) && tx.x >= 700 && tx.y >= 164 && tx.y < 186) {
        c = tx.color;
        ++n;
      }
    }
    REQUIRE(n == 1u);
    return c;
  };
  for (const Where w : {Where::Notes, Where::Tone, Where::Wobble}) {  // MODS on both pages of MODDED (heard at boot), WOB on WOBBLE (off at boot)
    OnInstrument o(w);
    INFO(where_name(w));
    const ui::Color boot = w == Where::Wobble ? ui::Color::TextDim : ui::Color::Text;
    const ui::Color other = w == Where::Wobble ? ui::Color::Text : ui::Color::TextDim;
    CHECK(hint(o) == boot);
    o.t.tap(Button::F8);
    o.t.wait(60);
    CHECK(hint(o) == other);
    o.t.tap(Button::F8);
    o.t.wait(60);
    CHECK(hint(o) == boot);
  }
}

TEST_CASE("skin instrument: the tagline says STOCK . MODS OFF in amber while STOCK is on, and SAVE FAILED in red wins over it") {
  // The colour of the brand's line (the one text at x 14, y 52) in the last frame.
  const auto tagline = [](const OnInstrument& o) {
    unsigned n = 0;
    ui::Color c = ui::Color::Empty;
    for (unsigned i = 0; i < o.q().texts(); ++i) {
      skin::Panel::TextInfo tx;
      if (o.q().text_at(i, tx) && tx.x == kTaglineX && tx.y == kTaglineY) {
        c = tx.color;
        ++n;
      }
    }
    REQUIRE(n == 1u);
    return c;
  };
  // A card whose UNTITLED folder is marked deleted cannot autosave: the project service says FAILED (project_service_test.cpp makes the same card).
  platform::host::ScratchDir dir{"o3303-s3"};
  platform::host::DirCard card{dir.path()};
  REQUIRE(dir.ok());
  const std::filesystem::path folder = std::filesystem::path(dir.path()) / "Apps" / "o3_303" / "UNTITLED";
  std::filesystem::create_directories(folder);
  { std::ofstream out(folder / ".deleted"); }
  OnInstrument o(Where::Notes, card);
  CHECK(tagline(o) == ui::Color::ChromeDim);  // ACID BASS VOICE
  o.t.shift_tap(Button::SampleLoad);          // the same switch as F8, from any screen
  o.t.wait(60);
  CHECK(tagline(o) == ui::Color::Accent);
  app_project().set_autosave_period(1'000);
  REQUIRE(seq::project_bank().set_note(0, 3, seq::project_bank().pattern(0).steps[3].note == 40u ? 41u : 40u));
  o.t.wait(2'500);
  REQUIRE(app_project().info().failed);
  go_to(o.t, Where::Tone);
  o.t.wait(60);
  CHECK(o.t.param(kVoice, AcidVoice::Stock) >= 0.5f);
  CHECK(tagline(o) == ui::Color::Rec);
  o.intact();
}

TEST_CASE("skin instrument: the cursor's key is ringed in white, a held step's in amber, both on a held cursor") {
  OnInstrument o;
  const ui::Rgb white = skin::theme().rgb(ui::Color::SelText);
  const ui::Rgb amber = skin::theme().rgb(ui::Color::Accent);
  InstrumentView v;
  o.s->fill_view(o.t.h->runtime().context(), v);
  const unsigned cur = static_cast<unsigned>(v.marks.cursor) % 16u;
  CHECK(o.rings() == 1u);
  CHECK(o.rings_at(skin::ring_box(cur, false), white) == 1u);
  // A key tapped moves the cursor: the ring follows it.
  const unsigned other = (cur + 6u) % 16u;
  o.t.pad(step_pad_of(other));
  o.settle();
  CHECK(o.rings() == 1u);
  CHECK(o.rings_at(skin::ring_box(other, false), white) == 1u);
  // Held: the step's amber ring joins the cursor's white one (a held pad is not a tap: the cursor stays where it was).
  o.t.down(step_pad_of(9));
  o.settle();
  CHECK(o.rings() == 2u);
  CHECK(o.rings_at(skin::ring_box(9, true), amber) == 1u);
  CHECK(o.rings_at(skin::ring_box(other, false), white) == 1u);
  o.t.down(step_pad_of(2));
  o.settle();
  CHECK(o.rings_at(skin::ring_box(2, true), amber) == 1u);
  CHECK(o.rings_at(skin::ring_box(9, true), amber) == 1u);
  o.t.up(step_pad_of(2));
  o.t.up(step_pad_of(9));
  o.settle();
  CHECK(o.rings() == 1u);
  o.intact();
}

TEST_CASE("skin instrument: the LEDs of the strip are the steps' gates, and the playhead's STEP is lit while it plays") {
  OnInstrument o;
  InstrumentView v;
  o.s->fill_view(o.t.h->runtime().context(), v);
  for (unsigned k = 0; k < v.cells; ++k) {
    const skin::StepLeds l = skin::step_leds(v.cell[k], false);
    ui::Rgb got{};
    REQUIRE(o.led_tint_at(skin::StripRow::Accent, k, got));
    CHECK_MESSAGE(same(got, skin::led_tint(skin::StripRow::Accent, l)), "accent " << k);
    REQUIRE(o.led_tint_at(skin::StripRow::Slide, k, got));
    CHECK_MESSAGE(same(got, skin::led_tint(skin::StripRow::Slide, l)), "slide " << k);
    REQUIRE(o.led_tint_at(skin::StripRow::Step, k, got));
    CHECK_MESSAGE(same(got, skin::led_tint(skin::StripRow::Step, l)), "step " << k);
  }
  // Playing: the playhead's STEP is the bright one, in the column of the step the sequencer is on (the frame follows by a frame or two).
  o.t.tap(Button::Play);
  unsigned checked = 0;
  for (unsigned i = 0; i < 1500 && checked < 3u; i += 33) {
    o.t.wait(33);
    const seq::Playhead ph = app_sequencer().playhead();
    InstrumentView pv;
    o.s->fill_view(o.t.h->runtime().context(), pv);
    if (!ph.playing || pv.marks.playhead < 0) {
      continue;
    }
    const unsigned k = static_cast<unsigned>(pv.marks.playhead) % 16u;
    ui::Rgb got{};
    REQUIRE(o.led_tint_at(skin::StripRow::Step, k, got));
    // The frame drawn is of a moment a little before: the lit STEP is somewhere on the page, and exactly one is.
    unsigned lit = 0;
    for (unsigned j = 0; j < 16; ++j) {
      ui::Rgb c{};
      REQUIRE(o.led_tint_at(skin::StripRow::Step, j, c));
      lit += same(c, skin::led_tint(skin::StripRow::Step, skin::StepLeds{false, false, skin::StripLed::Lit})) ? 1u : 0u;
    }
    CHECK(lit == 1u);
    ++checked;
  }
  CHECK(checked == 3u);
  o.t.tap(Button::Play);
  o.intact();
}

TEST_CASE("skin instrument: a held step shows its lock in the readout, in the accent colour") {
  OnInstrument o;
  o.t.tap(Button::F4);  // the knob the encoder will drive
  o.t.down(step_pad_of(5));
  o.t.turn(10);
  o.settle();
  InstrumentView v;
  o.s->fill_view(o.t.h->runtime().context(), v);
  CHECK(std::strncmp(v.lock_label, "LOCK ", 5) == 0);
  bool locked = false;
  for (unsigned i = 0; i < view::kKnobs; ++i) {
    locked = locked || std::strncmp(v.knob[i].label, "LOCK ", 5) == 0;
  }
  CHECK(locked);
  // The lock's value, drawn in the accent colour in the readout row, under the knobs.
  unsigned accent = 0;
  for (unsigned i = 0; i < o.q().texts(); ++i) {
    skin::Panel::TextInfo tx;
    if (o.q().text_at(i, tx) && tx.color == ui::Color::Accent && tx.y >= 164 && tx.y < 190) {
      ++accent;
    }
  }
  CHECK(accent == 1u);
  o.t.up(step_pad_of(5));
  o.intact();
}

TEST_CASE("skin instrument: Shift relabels the knobs LEARN or '-' and the F-number tabs turn amber, the knobs stay up") {
  OnInstrument o;
  InstrumentView v0;
  o.s->fill_view(o.t.h->runtime().context(), v0);
  const unsigned before = o.texts_in(70, 86, ui::Color::TextDim);
  o.t.press(Button::Shift);
  o.settle();
  InstrumentView v;
  o.s->fill_view(o.t.h->runtime().context(), v);
  unsigned can = 0;
  unsigned no = 0;
  for (unsigned i = 0; i < view::kKnobs; ++i) {
    can += v.learn[i] == Learn::Can ? 1u : 0u;
    no += v.learn[i] == Learn::No ? 1u : 0u;
    CHECK(v.learn[i] != Learn::None);
  }
  CHECK(can + no == static_cast<unsigned>(view::kKnobs));
  CHECK(can > 0u);
  CHECK(o.texts_in(70, 86, ui::Color::ChromeText) == can);  // LEARN: bright; "-": dim
  CHECK(o.texts_in(70, 86, ui::Color::TextDim) == no);
  CHECK(no >= before);
  // The knobs and their readouts are still there, as with Shift up.
  CHECK(o.sprites(skin::Layer::KnobCap, skin_assets::Id::Knob58Cap) > 0u);
  o.t.release(Button::Shift);
  o.settle();
  CHECK(o.texts_in(70, 86, ui::Color::TextDim) == before);
  o.intact();
}

TEST_CASE("skin instrument: LEARN waits in the accent colour on the knob that is armed") {
  platform::host::FakeMidi midi;
  Hands t(midi);
  t.skin_on();
  t.to_instrument();
  t.wait(60);
  auto* s = static_cast<InstrumentScreen*>(t.h->runtime().context().screen());
  t.press(Button::Shift);
  t.tap(Button::F1);
  t.wait(60);
  InstrumentView v;
  s->fill_view(t.h->runtime().context(), v);
  CHECK(v.learn[0] == Learn::Armed);
  unsigned armed = 0;
  for (unsigned i = 0; i < view::kKnobs; ++i) {
    armed += v.learn[i] == Learn::Armed ? 1u : 0u;
  }
  CHECK(armed == 1u);
  // LEARN of the armed knob is the one name on the label line in the accent colour.
  unsigned accent_label = 0;
  const skin::Panel& q = skin::panel();
  for (unsigned i = 0; i < q.texts(); ++i) {
    skin::Panel::TextInfo tx;
    accent_label += (q.text_at(i, tx) && tx.y >= 70 && tx.y < 86 && tx.color == ui::Color::Accent) ? 1u : 0u;
  }
  CHECK(accent_label == 1u);
  CHECK(q.dropped() == 0u);
  t.release(Button::Shift);
}

TEST_CASE("skin instrument: the worst frame (every step a note with accent and slide, every step held, playing, a long toast) is queued whole on every screen") {
  platform::host::FakeMidi midi;
  for (const Where w : kAllScreens) {
    Hands t(midi);
    t.skin_on();
    bank().edit(t.selected(), [](Pattern& p) {
      for (unsigned k = 0; k < 16; ++k) {
        p.steps[k].note = static_cast<std::uint8_t>(40 + k);
        p.steps[k].flags = static_cast<std::uint8_t>(kStepAccent | kStepSlide);
      }
    });
    go_to(t, w);
    t.wait(60);
    t.tap(Button::Play);
    t.press(Button::Shift);
    for (unsigned k = 0; k < 16; ++k) {
      t.down(step_pad_of(k));
    }
    t.tap(Button::F1);  // MIDI learn waits: its toast ("LEARN CUTOFF: MOVE A KNOB" on VOICE) fills the PAGE window
    t.wait(120);
    const skin::Panel& q = skin::panel();
    unsigned rings = 0;
    unsigned leds_on = 0;
    unsigned message = 0;
    for (unsigned i = 0; i < q.shapes(); ++i) {
      skin::Panel::RoundRectInfo rr;
      rings += q.round_rect_at(i, rr) && rr.layer == skin::Layer::HeldRim ? 1u : 0u;
      skin::Panel::SpriteInfo sp;
      if (q.sprite_at(i, sp) && sp.layer == skin::Layer::Led && sp.y < 370 && sp.y > 310 && !same(sp.rgb, skin::led_tint(skin::StripRow::Step, skin::StepLeds{false, false, skin::StripLed::Off}))) {
        ++leds_on;
      }
    }
    for (unsigned i = 0; i < q.texts(); ++i) {
      skin::Panel::TextInfo tx;
      message += q.text_at(i, tx) && tx.x == 626 && tx.color == ui::Color::Accent ? 1u : 0u;
    }
    CHECK_MESSAGE(rings >= 16u, where_name(w));
    CHECK_MESSAGE(leds_on >= 32u + 15u, where_name(w));  // ACCENT and SLIDE of the 16 and the dim STEP of the 15 off the playhead (the playhead's is lit: also counted, so 48)
    CHECK_MESSAGE(message == (w == Where::Wobble ? 0u : 1u), where_name(w) << ": the toast is in the PAGE window (this counts the accent-coloured LEARN wait in the PAGE window, which only an armed knob has: WOBBLE's keys cannot be armed, they show the CANNOT LEARN toast, which is not it)");
    CHECK_MESSAGE(q.dropped() == 0u, where_name(w));
    MESSAGE("worst frame, " << where_name(w) << ": " << q.shapes() << " shapes of " << skin::Panel::kShapes << ", " << q.texts() << " texts of " << skin::Panel::kTexts);
    CHECK(q.shapes() + 40u <= skin::Panel::kShapes);  // room to spare: a longer name does not drop
    CHECK(q.texts() + 20u <= skin::Panel::kTexts);
    for (unsigned k = 0; k < 16; ++k) {
      t.up(step_pad_of(k));
    }
    t.release(Button::Shift);
  }
}
