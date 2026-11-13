// SPDX-License-Identifier: GPL-3.0-or-later
// The obsidian skin's F row (docs/SKIN-PLAN.md 2.3, slice SK3): the slot kind of a key (one pure rule), the knob pointer's angle and end points, the readout texts, the states
// copied into a slot, the queue the whole row needs beside the header's, and the console's `skin` command. The pictures are app/scenarios/skin_home.txt.
#include <doctest/doctest.h>

#include "../app/src/skin/frow.h"
#include "../app/src/skin/header.h"
#include "../app/src/skin/knob.h"
#include "../app/src/skin/panel.h"
#include "../app/src/skin/skin.h"

#include "app_hands.h"

#include <o3sdk/app/app.h>
#include <o3sdk/app/diag.h>
#include <o3sdk/core/param.h>
#include <o3sdk/sim/harness.h>
#include <o3_303/skin_assets.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using namespace o3_303;
using namespace o3_303::skin;

namespace {

std::string str(const char* s) { return std::string(s); }

view::Knob knob(const char* label, const char* value, const char* unit, float norm) {
  view::Knob k;
  k.fkey = "F1";
  k.label = label;
  std::strcpy(k.value, value);
  k.unit = unit;
  k.norm = norm;
  return k;
}

// What `skin <words...>` says.
std::string console_says(std::initializer_list<const char*> words) {
  std::string text;
  const char* argv[8] = {"skin"};
  unsigned argc = 1;
  for (const char* w : words) {
    argv[argc++] = w;
  }
  o3sdk::app::DiagOut out(o3sdk::app::DiagSink{
      [](void* context, const char* data, std::size_t n) { static_cast<std::string*>(context)->append(data, n); }, &text});
  console(o3sdk::app::DiagArgs{argc, argv}, out);
  return text;
}

}  // namespace

// ---- the slot rule --------------------------------------------------------------------------------------------------------------------

TEST_CASE("frow: one slot kind for each key: blank is empty, a negative norm an action, a toggle a switch, the rest a knob") {
  view::Knob k;
  k.norm = 0.5f;
  CHECK(slot_kind(k) == SlotKind::Knob);
  k.norm = 0.0f;  // the bottom of a knob's range is a knob, not an action
  CHECK(slot_kind(k) == SlotKind::Knob);
  k.norm = 1.0f;
  CHECK(slot_kind(k) == SlotKind::Knob);
  k.toggle = true;
  CHECK(slot_kind(k) == SlotKind::Switch);
  k.norm = 0.0f;
  CHECK(slot_kind(k) == SlotKind::Switch);
  k.norm = -1.0f;  // an action wins over a toggle: a negative norm says there is no value to show
  CHECK(slot_kind(k) == SlotKind::Action);
  k.toggle = false;
  CHECK(slot_kind(k) == SlotKind::Action);
  k.blank = true;  // and a blank key is none of them
  CHECK(slot_kind(k) == SlotKind::Empty);
  k.norm = 0.5f;
  k.toggle = true;
  CHECK(slot_kind(k) == SlotKind::Empty);
  CHECK(slot_kind(view::Knob{}) == SlotKind::Action);  // a default Knob has norm -1: an action key with no name
}

TEST_CASE("frow, U6: the classic strip and the skin take their kind of key from one rule (view::key_kind)") {
  const float norms[] = {-1.0f, 0.0f, 0.5f, 1.0f};
  for (const bool blank : {false, true}) {
    for (const bool toggle : {false, true}) {
      for (const float norm : norms) {
        view::Knob k;
        k.blank = blank;
        k.toggle = toggle;
        k.norm = norm;
        const KeyKind want = blank ? KeyKind::Blank : (norm < 0.0f ? KeyKind::Action : (toggle ? KeyKind::Switch : KeyKind::Knob));
        CHECK(view::key_kind(k) == want);
        const SlotKind slot = slot_kind(k);
        CHECK(slot == (want == KeyKind::Blank ? SlotKind::Empty : (want == KeyKind::Action ? SlotKind::Action : (want == KeyKind::Switch ? SlotKind::Switch : SlotKind::Knob))));
      }
    }
  }
}

TEST_CASE("frow: the readout's face is the biggest that fits the window, the hint's the quieter Label") {
  CHECK(readout_face(1) == ui::Face::Data);
  CHECK(readout_face(8) == ui::Face::Data);
  CHECK(readout_face(9) == ui::Face::Label);
  CHECK(readout_face(10) == ui::Face::Label);
  CHECK(readout_face(11) == ui::Face::Micro);
  CHECK(readout_face(kReadoutChars) == ui::Face::Micro);
  CHECK(hint_face(3) == ui::Face::Label);
  CHECK(hint_face(10) == ui::Face::Label);
  CHECK(hint_face(11) == ui::Face::Micro);
}

// ---- the pointer ------------------------------------------------------------------------------------------------------------------------

TEST_CASE("knob: the pointer sweeps 300 degrees from 7 o'clock to 5 o'clock, through straight up at the middle") {
  CHECK(pointer_half_degrees(0.0f) == -300);
  CHECK(pointer_half_degrees(0.5f) == 0);
  CHECK(pointer_half_degrees(1.0f) == 300);
  CHECK(pointer_half_degrees(0.25f) == -150);
  CHECK(pointer_half_degrees(-3.0f) == -300);  // clamped
  CHECK(pointer_half_degrees(7.0f) == 300);
  CHECK(pointer_half_degrees(std::nanf("")) == -300);

  // At 0.5: straight up, from 54 to 307 sixteenths above the centre.
  const Pointer mid = pointer(0.5f);
  CHECK(mid.x0_q4 == 0);
  CHECK(mid.x1_q4 == 0);
  CHECK(mid.y0_q4 == -kKnob42InnerQ4);
  CHECK(mid.y1_q4 == -kKnob42OuterQ4);
  // At 0: 150 degrees anticlockwise from up: left and below the centre, sin 150 = 0.5, cos 150 = -0.866.
  const Pointer lo = pointer(0.0f);
  CHECK(lo.x1_q4 == -154);   // -0.5 x 307
  CHECK(lo.y1_q4 == 266);    // +0.866 x 307
  CHECK(lo.x0_q4 == -27);    // -0.5 x 54
  CHECK(lo.y0_q4 == 47);     // +0.866 x 54
  // At 1: the mirror image.
  const Pointer hi = pointer(1.0f);
  CHECK(hi.x1_q4 == 154);
  CHECK(hi.y1_q4 == 266);
  CHECK(hi.x0_q4 == 27);
  CHECK(hi.y0_q4 == 47);
  // A quarter of the way (-75 degrees): nearly level to the left, a little above the centre.
  const Pointer q = pointer(0.25f);
  CHECK(q.x1_q4 < -290);
  CHECK(q.y1_q4 < 0);
  CHECK(q.y1_q4 > -90);
}

TEST_CASE("knob: the sine table is a sine: quarter symmetry, no wrap, and every pointer is its radius long") {
  CHECK(sin_q15(0) == 0);
  CHECK(sin_q15(180) == 32767);   // 90 degrees
  CHECK(sin_q15(360) == 0);       // 180 degrees
  CHECK(sin_q15(540) == -32767);  // 270 degrees
  CHECK(sin_q15(720) == 0);
  CHECK(sin_q15(-180) == -32767);
  CHECK(sin_q15(60) == 16384);    // 30 degrees
  CHECK(sin_q15(1) == (0 + 572) / 2);  // half a degree is half way between two table entries
  for (int h = -720; h <= 720; ++h) {
    CHECK(sin_q15(h) == -sin_q15(-h));
    CHECK(sin_q15(h) == sin_q15(360 - h));
  }
  // Against the library's sine at every half degree: within 0.0001 (Q15 3.3 units).
  for (int h = 0; h < 720; ++h) {
    const double want = std::sin(h * 3.14159265358979323846 / 360.0) * 32768.0;
    CHECK(std::fabs(static_cast<double>(sin_q15(h)) - want) < 4.0);
  }
  // The pointer's length is its radius (to a sixteenth of a pixel) all along the sweep, and it moves clockwise as the value grows.
  int last_x = 100000;
  for (int i = 0; i <= 100; ++i) {
    const Pointer p = pointer(static_cast<float>(i) / 100.0f);
    const double len = std::sqrt(static_cast<double>(p.x1_q4) * p.x1_q4 + static_cast<double>(p.y1_q4) * p.y1_q4);
    CHECK(std::fabs(len - kKnob42OuterQ4) < 1.5);
    CHECK(p.y1_q4 <= 266);  // never below 5 o'clock
    if (i <= 20) {
      CHECK(p.x1_q4 <= last_x + 1);  // from 7 to 9 o'clock (the first fifth of the sweep) the tip moves left
    } else if (i <= 50) {
      CHECK(p.x1_q4 >= last_x - 1);  // from 9 to 12 o'clock it moves right
    }
    last_x = p.x1_q4;
  }
}

// ---- fill_frow --------------------------------------------------------------------------------------------------------------------------

TEST_CASE("frow: a knob's readout is its value and unit when both fit the window, the value cut to it when not") {
  view::Knob in[view::kKnobs];
  in[0] = knob("CUTOFF", "350", "Hz", 0.3f);
  in[1] = knob("RESO", "65", "%", 0.65f);
  in[2] = knob("A", "-12.5", "dB", 0.1f);
  in[3] = knob("B", "123456789", "ms", 0.1f);      // nine and two: eleven fit
  in[4] = knob("C", "1234567890123", "ms", 0.1f);  // thirteen and two: fifteen do not; the value alone
  in[5] = knob("D", "12345678901234567890", "", 0.1f);  // cut to the window's fourteen
  in[6] = knob("E", "7", nullptr, 0.1f);           // no unit text at all
  FRowView v;
  fill_frow(v, in, false);
  CHECK(v.slot[0].kind == SlotKind::Knob);
  CHECK(str(v.slot[0].readout) == "350Hz");
  CHECK(str(v.slot[1].readout) == "65%");
  CHECK(str(v.slot[2].readout) == "-12.5dB");
  CHECK(str(v.slot[3].readout) == "123456789ms");
  CHECK(str(v.slot[4].readout) == "1234567890123");
  CHECK(str(v.slot[5].readout) == "12345678901234");
  CHECK(str(v.slot[6].readout) == "7");
  CHECK(v.slot[0].norm == doctest::Approx(0.3f));
  CHECK_FALSE(v.shift);
  // A default Knob is an action with no words: no window.
  CHECK(v.slot[7].kind == SlotKind::Action);
  CHECK(str(v.slot[7].readout) == "");
}

TEST_CASE("frow: names are cut to the label line, the F-number to two characters, an empty box has neither name nor readout") {
  view::Knob in[view::kKnobs];
  in[0] = knob("ENVELOPE MOD", "60", "%", 0.6f);
  in[0].fkey = "F10";
  in[1].blank = true;
  in[1].fkey = "F2";
  in[1].label = "NOPE";
  std::strcpy(in[1].value, "NOPE");
  FRowView v;
  fill_frow(v, in, false);
  CHECK(str(v.slot[0].name) == "ENVELOPE");
  CHECK(std::strlen(v.slot[0].name) == kNameChars);
  CHECK(str(v.slot[0].fkey) == "F1");
  CHECK(v.slot[1].kind == SlotKind::Empty);
  CHECK(str(v.slot[1].fkey) == "F2");
  CHECK(str(v.slot[1].name) == "");
  CHECK(str(v.slot[1].readout) == "");
}

TEST_CASE("frow, G1: every label of HOME's and PATTERN SETUP's Shift row fits the label line whole") {
  // The strings of main_screen.cpp's kHomeKeyLabels and kSetupKeyLabels; a longer one (RANDOMIZE) would be cut to kNameChars, so the key says RANDOM.
  static constexpr const char* kLabels[] = {"CLEAR", "VARY", "RHYTHM", "NOTES", "NOTE", "SCALE", "REDO", "UNDO",
                                            "COPY PAT", "PASTE", "DUP>NEXT", "CLR LOCK", "REVERSE", "RANDOM"};
  for (const char* label : kLabels) {
    view::Knob in[view::kKnobs];
    in[0] = knob(label, "x", "", -1.0f);
    FRowView v;
    fill_frow(v, in, false);
    CHECK(str(v.slot[0].name) == label);
    CHECK(std::strlen(label) <= kNameChars);
  }
}

TEST_CASE("frow: the states are copied: focus, held, locked and dim, and a switch is on from half way") {
  view::Knob in[view::kKnobs];
  in[0] = knob("CUTOFF", "350", "Hz", 0.5f);
  in[0].focus = true;
  in[1] = knob("RESO", "65", "%", 0.5f);
  in[1].held = true;
  in[2] = knob("LOCK 5", "63", "%", 0.63f);
  in[2].focus = true;
  in[2].locked = true;
  in[3] = knob("NEW", "new line", "", -1.0f);
  in[3].dim = true;
  in[4] = knob("WAVE", "SQR", "x", 1.0f);
  in[4].toggle = true;
  in[4].off_name = "SAW";
  in[4].on_name = "SQUARES";
  in[7] = knob("STOCK", "ON", "", -1.0f);
  in[7].lit = true;  // MODDED's F8 while STOCK is on
  in[5] = knob("WAVE", "SAW", "", 0.49f);
  in[5].toggle = true;
  in[5].off_name = "SAW";
  in[5].on_name = "SQR";
  in[6] = knob("WAVE", "SQR", "", 0.5f);
  in[6].toggle = true;
  in[6].off_name = "SAW";
  in[6].on_name = "SQR";
  FRowView v;
  fill_frow(v, in, true);
  CHECK(v.shift);
  CHECK(v.slot[0].focus);
  CHECK_FALSE(v.slot[0].held);
  CHECK(v.slot[1].held);
  CHECK_FALSE(v.slot[1].focus);
  CHECK(v.slot[2].locked);
  CHECK(v.slot[2].focus);
  CHECK(str(v.slot[2].name) == "LOCK 5");
  CHECK(v.slot[3].kind == SlotKind::Action);
  CHECK(v.slot[3].dim);
  CHECK(str(v.slot[3].readout) == "new line");  // an action's hint is the value, with no unit
  CHECK_FALSE(v.slot[0].dim);
  CHECK(v.slot[4].kind == SlotKind::Switch);
  CHECK(str(v.slot[4].readout) == "SQR");  // a switch's readout is the value alone: its unit is never shown
  CHECK(str(v.slot[4].off_name) == "SAW");
  CHECK(str(v.slot[4].on_name) == "SQUARE");  // cut to the six characters beside the switch
  CHECK(v.slot[4].norm == 1.0f);
  CHECK(v.slot[5].norm == 0.0f);
  CHECK(v.slot[6].norm == 1.0f);
  CHECK(v.slot[7].kind == SlotKind::Action);
  CHECK(v.slot[7].lit);  // an action key that is on is lit; the others are not
  CHECK_FALSE(v.slot[3].lit);
  CHECK_FALSE(v.slot[0].lit);
}

TEST_CASE("frow: the voice's WAVE is a two-place list, so HOME's fill_knob makes it a switch") {
  Hands t;
  const o3sdk::core::ParamDesc* d = t.h->engine().param_desc(kVoice, AcidVoice::Wave);
  REQUIRE(d != nullptr);
  REQUIRE(d->names != nullptr);
  CHECK(d->list_count == 2);
  CHECK(str(d->names[0]) == "SAW");
  CHECK(str(d->names[1]) == "SQR");
  // And none of the other voice knobs on HOME's page is: they are knobs.
  for (std::uint16_t id : {AcidVoice::Cutoff, AcidVoice::Reso, AcidVoice::EnvMod, AcidVoice::Decay, AcidVoice::Accent, AcidVoice::Tune}) {
    const o3sdk::core::ParamDesc* k = t.h->engine().param_desc(kVoice, id);
    REQUIRE(k != nullptr);
    CHECK(k->list_count != 2);
  }
}

// ---- drawing ----------------------------------------------------------------------------------------------------------------------------

TEST_CASE("frow: the header and a row of every kind and state fit the queue with room to spare") {
  view::Knob in[view::kKnobs];
  in[0] = knob("CUTOFF", "350", "Hz", 0.3f);
  in[0].focus = true;
  in[1] = knob("LOCK 12", "65", "%", 0.65f);
  in[1].focus = true;
  in[1].locked = true;
  in[1].held = true;
  in[2] = knob("ENV MOD", "100", "%", 1.0f);
  in[3] = knob("DECAY", "750", "ms", 0.0f);
  in[4] = knob("REVERSE", "backwards", "", -1.0f);
  in[5] = knob("WAVE", "SAW", "", 0.0f);
  in[5].toggle = true;
  in[5].off_name = "SAW";
  in[5].on_name = "SQR";
  in[6].blank = true;
  in[7] = knob("UNDO", "step back", "", -1.0f);
  in[7].dim = true;
  for (unsigned i = 0; i < view::kKnobs; ++i) {
    static constexpr const char* kF[view::kKnobs] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
    in[i].fkey = kF[i];
  }
  for (bool shift : {false, true}) {
    Panel p;
    p.clear();
    queue_panel(p, kHeaderH, 384);
    frame::StatusView st;
    HeaderInputs hi;
    hi.title = "STEPS";
    std::strcpy(st.enc, "F1 CUTOFF");
    st.keys = true;
    st.rec = true;
    st.take = "TAKE REC 0:05";
    st.toast = "NO CHANGE";
    st.toast_off = true;
    st.actions = 0x7FF;
    HeaderView hv;
    fill_header(hv, st, hi);
    hv.playing = true;
    draw_header(p, hv);
    FRowView v;
    fill_frow(v, in, shift);
    draw_frow(p, v);
    CHECK(p.dropped() == 0);
    CHECK(p.shapes() < Panel::kShapes * 3 / 4);
    CHECK(p.texts() < Panel::kTexts * 3 / 4);
  }
}

// ---- under the kit's system strip -------------------------------------------------------------------------------------------------------

namespace {

constexpr ui::Rect kStrip{0, 452, 800, 28};  // Context::strip_area() under Chrome::None while a strip is up

bool inside_strip(int x, int y, int w, int h) {
  const int y1 = y + h < 480 ? y + h : 480;
  const int x1 = x + w < 800 ? x + w : 800;
  return x >= kStrip.x && y >= kStrip.y && x1 <= kStrip.x + kStrip.w && y1 <= kStrip.y + kStrip.h;
}

}  // namespace

TEST_CASE("strip: a panel cleared with the strip's rect queues nothing wholly inside it, cut to the screen, and everything else whole") {
  Panel p;
  p.clear(kStrip);
  // Rects and round rects: wholly inside, the exact rect, one pixel above it, one past its right edge, and a window whose lip runs a pixel off the screen's bottom.
  CHECK(p.rect(Layer::Rule, ui::Rect{10, 460, 50, 10}, pal::kRule));
  CHECK(p.rect(Layer::Rule, ui::Rect{0, 452, 800, 28}, pal::kRule));
  CHECK(p.skipped() == 2);
  CHECK(p.shapes() == 0);
  p.round_rect(Layer::WindowLip, ui::Rect{100, 457, 94, 24}, 64, pal::kWindowLip);  // y 457 to 481: the last row is off the screen
  CHECK(p.skipped() == 3);
  p.rect(Layer::Rule, ui::Rect{10, 451, 50, 10}, pal::kRule);   // a row above the strip
  p.rect(Layer::Rule, ui::Rect{760, 460, 41, 10}, pal::kRule);  // a column past it, off the screen: still wholly inside as far as it is seen
  CHECK(p.skipped() == 4);
  CHECK(p.shapes() == 1);
  // Sprites by their drawn size: the 16 x 16 LED in the strip is out, the same LED straddling its top is kept.
  p.sprite(Layer::Led, skin_assets::Id::Led, 0, 100, 460, pal::kLedRed);
  p.sprite(Layer::Led, skin_assets::Id::Led, 0, 100, 440, pal::kLedRed);
  CHECK(p.skipped() == 5);
  CHECK(p.shapes() == 2);
  // A point and a line count their reach: a glow of 12 px radius centred 4 px below the strip's top spills out of it, a small point and a short thin line in the middle do not.
  p.point(Layer::Glow, 400 * 16, 456 * 16, 12 * 16, pal::kLedRed, 50);
  CHECK(p.shapes() == 3);
  p.point(Layer::Glow, 400 * 16, 466 * 16, 4 * 16, pal::kLedRed, 50);
  p.line(Layer::Pointer, 400 * 16, 460 * 16, 420 * 16, 470 * 16, 18, pal::kPointer);
  CHECK(p.skipped() == 7);
  CHECK(p.shapes() == 3);
  // Texts by the face's cell: a Data text in the strip is out, one that starts above it is kept; the string is read, not kept.
  const char* const word = "LEARNED";
  p.text(10, 456, word, ui::Color::Text, ui::Face::Data);
  CHECK(p.skipped() == 8);
  p.text(10, 440, word, ui::Color::Text, ui::Face::Data);
  CHECK(p.texts() == 1);
  // A null string has no box: queued under a strip as without one, as Renderer::text takes it, and never read.
  CHECK(p.text(10, 456, nullptr, ui::Color::Text, ui::Face::Data));
  CHECK(p.texts() == 2);
  CHECK(p.skipped() == 8);
  // clear() with no rect drops nothing, and forgets the strip.
  p.clear();
  CHECK(p.text(10, 456, nullptr, ui::Color::Text, ui::Face::Data));
  CHECK(p.texts() == 1);
  p.clear();
  CHECK(p.skipped() == 0);
  p.rect(Layer::Rule, ui::Rect{10, 460, 50, 10}, pal::kRule);
  p.text(10, 456, word, ui::Color::Text, ui::Face::Data);
  CHECK(p.shapes() == 1);
  CHECK(p.texts() == 1);
  CHECK(p.skipped() == 0);
}

TEST_CASE("strip: the F row under a strip leaves out what the strip covers and queues the rest as it is") {
  view::Knob in[view::kKnobs];
  in[0] = knob("CUTOFF", "350", "Hz", 0.3f);
  in[0].focus = true;
  in[1] = knob("LOCK 12", "65", "%", 0.65f);
  in[1].locked = true;
  in[1].held = true;
  in[2] = knob("ENV MOD", "100", "%", 1.0f);
  in[4] = knob("REVERSE", "backwards", "", -1.0f);
  in[5] = knob("WAVE", "SAW", "", 0.0f);
  in[5].toggle = true;
  in[5].off_name = "SAW";
  in[5].on_name = "SQR";
  in[6].blank = true;
  in[7] = knob("UNDO", "step back", "", -1.0f);
  for (unsigned i = 0; i < view::kKnobs; ++i) {
    static constexpr const char* kF[view::kKnobs] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
    in[i].fkey = kF[i];
  }
  FRowView v;
  fill_frow(v, in, false);
  Panel plain;
  plain.clear();
  draw_frow(plain, v);
  Panel under;
  under.clear(kStrip);
  draw_frow(under, v);
  CHECK(plain.skipped() == 0);
  CHECK(under.skipped() > 16);  // the eight readout windows alone are two shapes and a text each
  CHECK(under.shapes() + under.texts() + under.skipped() == plain.shapes() + plain.texts());
  CHECK(under.dropped() == 0);
  // What is left is not wholly inside the strip, and what was left out is: every text and rect of the plain queue that is in the strip is gone.
  for (unsigned i = 0; i < under.shapes(); ++i) {
    Panel::RectInfo r;
    Panel::RoundRectInfo rr;
    if (under.rect_at(i, r)) {
      CHECK_FALSE(inside_strip(r.box.x, r.box.y, r.box.w, r.box.h));
    }
    if (under.round_rect_at(i, rr)) {
      CHECK_FALSE(inside_strip(rr.box.x, rr.box.y, rr.box.w, rr.box.h));
    }
  }
  for (unsigned i = 0; i < under.texts(); ++i) {
    Panel::TextInfo t;
    REQUIRE(under.text_at(i, t));
    CHECK_FALSE(inside_strip(t.x, t.y, static_cast<int>(std::strlen(t.s)) * face_w(t.face), face_h(t.face)));
  }
}

namespace {

// A skinned screen of its own (the obsidian panel and the F row, drawn with the skin's bitmaps and flushed as the real screens do), whose Panel is cleared with whatever rect the test says,
// under the kit's real strip, drawn by the runtime over it: what the pixels are with the skin leaving the strip's area out, against what they are with it all queued.
class StripProbeScreen final : public o3sdk::app::Screen {
 public:
  enum class Cover { Nothing, Reported, Taller, Always };
  const char* title() const noexcept override { return "STRIP"; }
  o3sdk::app::Chrome chrome(const o3sdk::app::Context&) const noexcept override { return o3sdk::app::Chrome::None; }
  const ui::Theme* theme(const o3sdk::app::Context&) const noexcept override { return &skin::theme(); }
  void draw(o3sdk::app::Context& ctx, ui::Renderer& r, const ui::Rect&) noexcept override {
    Panel& p = skin::panel();
    switch (cover) {
      case Cover::Nothing:  // everything queued, the strip hides what it hides
        p.clear();
        break;
      case Cover::Reported:  // what the skin does: the kit's strip_area()
        p.clear(ctx.strip_area());
        break;
      case Cover::Taller:  // as if the strip were drawn smaller than the rect the kit reports: the skin leaves out 84 px, the keys above the readouts too
        p.clear(ctx.strip_area().w > 0 ? ui::Rect{0, 396, 800, 84} : ui::Rect{0, 0, 0, 0});
        break;
      case Cover::Always:  // as if the strip were not drawn at all: the rect is skipped whether a strip is up or not
        p.clear(ui::Rect{0, 452, 800, 28});
        break;
    }
    queue_panel(p, kHeaderH, 384);
    view::Knob in[view::kKnobs];
    in[0] = knob("CUTOFF", "350", "Hz", 0.3f);
    in[0].focus = true;
    in[1] = knob("LOCK 12", "65", "%", 0.65f);
    in[1].held = true;
    in[3] = knob("REVERSE", "backwards", "", -1.0f);
    in[4] = knob("WAVE", "SAW", "", 0.0f);
    in[4].toggle = true;
    in[4].off_name = "SAW";
    in[4].on_name = "SQR";
    in[5].blank = true;
    in[6] = knob("UNDO", "step back", "", -1.0f);
    in[6].dim = true;
    in[2] = knob("DECAY", "750", "ms", 0.8f);
    in[7] = knob("RES", "12", "%", 0.1f);
    for (unsigned i = 0; i < view::kKnobs; ++i) {
      static constexpr const char* kF[view::kKnobs] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
      in[i].fkey = kF[i];
    }
    FRowView v;
    fill_frow(v, in, false);
    draw_frow(p, v);
    skipped = p.skipped();
    ui::plot::Canvas canvas(r);
    canvas.open();
    p.flush_shapes(canvas);
    canvas.close();
    p.flush_text(r);
  }
  Cover cover = Cover::Nothing;
  unsigned skipped = 0;
};

class StripProbeApp final : public o3sdk::app::App {
 public:
  const o3sdk::app::AppInfo& info() const noexcept override {
    static constexpr o3sdk::app::AppInfo kInfo{"STRIP", "1"};
    return kInfo;
  }
  std::size_t assets(const o3sdk::app::AppAsset*& rows) const noexcept override {
    rows = skin_assets::kRows;
    return skin_assets::kCount;
  }
  void setup(o3sdk::app::Context& ctx) noexcept override { ctx.show(screen); }
  StripProbeScreen screen;
};

}  // namespace

namespace {

std::vector<std::uint8_t> pixels_of(const o3sdk::sim::Harness& h) {
  const std::uint8_t* p = h.raster().rgba();
  return std::vector<std::uint8_t>(p, p + 800u * 480u * 4u);
}

}  // namespace

TEST_CASE("strip: the picture under the kit's strip is the same whether the skin leaves out what it covers or queues it, and it is not when the rect is wrong") {
  StripProbeApp app;
  o3sdk::sim::Harness h(app);
  REQUIRE(h.start());
  h.run_ms(100);
  auto frame = [&](StripProbeScreen::Cover cover) {
    app.screen.cover = cover;
    h.runtime().context().redraw();
    h.run_ms(80);
    return pixels_of(h);
  };
  for (const char* notice : {"LEARNED CC 75 CHANNEL 1", "THE USB PORT CANNOT CHANGE NOW (THE CARD IS OUT OR A CHANGE RUNS)"}) {
    INFO(notice);
    h.runtime().system_strip(notice, -1, 3000);
    const auto all_queued = frame(StripProbeScreen::Cover::Nothing);
    REQUIRE(h.runtime().system().strip_active());
    CHECK(app.screen.skipped == 0);
    const auto left_out = frame(StripProbeScreen::Cover::Reported);
    REQUIRE(h.runtime().system().strip_active());
    CHECK(app.screen.skipped > 16);                  // the skin really did leave things out
    CHECK(left_out == all_queued);                   // and the opaque strip hid every one of them: the same 384,000 pixels
    // A strip drawn smaller than the rect the kit reports would show the hole: the pixels differ.
    CHECK(frame(StripProbeScreen::Cover::Taller) != all_queued);
    h.runtime().system_strip(nullptr);
    h.run_ms(80);
    REQUIRE_FALSE(h.runtime().system().strip_active());
  }
  // And a strip that is not drawn at all (the rect skipped with none up) shows the F row's missing readouts.
  const auto no_strip = frame(StripProbeScreen::Cover::Nothing);
  CHECK(frame(StripProbeScreen::Cover::Reported) == no_strip);  // no strip, no rect: the skin leaves nothing out
  CHECK(app.screen.skipped == 0);
  CHECK(frame(StripProbeScreen::Cover::Always) != no_strip);
}

// ---- the console ------------------------------------------------------------------------------------------------------------------------

TEST_CASE("console: skin on and off, ballast N and stats, one line each; anything else is the usage") {
  const bool was_on = skin::on();
  const std::uint32_t was_ballast = skin::ballast();
  CHECK(console_says({"on"}).rfind("skin on ", 0) == 0);
  CHECK(skin::on());
  CHECK(console_says({"off"}).rfind("skin off ", 0) == 0);
  CHECK_FALSE(skin::on());
  CHECK(console_says({"ballast", "200"}).find("ballast=200\n") != std::string::npos);
  CHECK(skin::ballast() == 200);
  CHECK(console_says({"ballast", "99999"}).find("ballast=1900\n") != std::string::npos);  // clamped: the display list holds 2,048 words
  CHECK(skin::ballast() == kBallastMax);
  CHECK(console_says({"ballast", "0"}).find("ballast=0\n") != std::string::npos);
  reset_stats();
  note_frame(1234, 567, 5000);
  skin::panel().clear();
  const std::string line = console_says({"stats"});
  CHECK(line == "skin off frames=1 words=1234 draw_us=567 peak_us=567 dropped=0 ballast=0\n");
  CHECK(console_says({}) == line);  // no word is stats
  for (unsigned i = 0; i < Panel::kShapes + 3; ++i) {  // a frame that overflowed the queue: the reply says how many shapes and texts were lost
    skin::panel().rect(Layer::Base, ui::Rect{0, 0, 1, 1}, pal::kBase);
  }
  CHECK(console_says({"stats"}).find(" dropped=3 ") != std::string::npos);
  skin::panel().clear();
  for (const char* bad : {"ballast", "nonsense", "onn", "o"}) {
    CHECK(console_says({bad}).rfind("usage: ", 0) == 0);
  }
  CHECK(console_says({"ballast", "x"}).rfind("usage: ", 0) == 0);
  CHECK(console_says({"ballast", "-5"}).rfind("usage: ", 0) == 0);
  CHECK(console_says({"ballast", "12x"}).rfind("usage: ", 0) == 0);
  CHECK(skin::ballast() == 0);  // a refused word changed nothing
  skin::set_on(was_on);
  skin::set_ballast(was_ballast);
}

TEST_CASE("console: the app registers it under the console's rules (a 2 to 7 letter name, help up to 70 characters)") {
  const o3sdk::app::DiagCommand* rows = nullptr;
  const std::size_t n = o3sdk_app().diag_commands(rows);
  bool found = false;
  for (std::size_t i = 0; i < n; ++i) {
    const std::string name = rows[i].name;
    CHECK(name.size() >= 2);
    CHECK(name.size() <= 7);
    CHECK(std::strlen(rows[i].help) <= 70);
    CHECK(rows[i].run != nullptr);
    found = found || name == "skin";
  }
  CHECK(found);
}

TEST_CASE("console: stats keep the last frame and the peak draw time of the last full second") {
  reset_stats();
  const FrameStats before = stats();
  CHECK(before.frames == 0);
  CHECK(before.peak_us == 0);
  note_frame(1000, 500, 10'000);   // a new second begins
  note_frame(1100, 900, 10'200);
  note_frame(900, 300, 10'400);
  FrameStats s = stats();
  CHECK(s.frames == before.frames + 3);
  CHECK(s.last_words == 900);
  CHECK(s.last_us == 300);
  CHECK(s.peak_us == 900);          // the running second's peak
  note_frame(800, 200, 11'100);     // the next second: the one that ended had 900
  s = stats();
  CHECK(s.peak_us == 900);          // still the ended second's, until this one beats it
  CHECK(s.last_us == 200);
  note_frame(800, 1200, 11'300);
  CHECK(stats().peak_us == 1200);
  note_frame(800, 100, 12'200);     // and a quiet second after: the peak is the last full one's
  CHECK(stats().peak_us == 1200);
  note_frame(800, 100, 13'300);
  CHECK(stats().peak_us == 100);
}
