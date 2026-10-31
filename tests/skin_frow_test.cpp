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
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>

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
  in[4].on_name = "SQUARE";
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
  CHECK(str(v.slot[4].on_name) == "SQUAR");  // cut to the five characters beside the switch
  CHECK(v.slot[4].norm == 1.0f);
  CHECK(v.slot[5].norm == 0.0f);
  CHECK(v.slot[6].norm == 1.0f);
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
    for (unsigned b = 0; b < kPerfButtons; ++b) {
      hi.perf[b] = kDefaultPerf[b];
    }
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
  const std::string line = console_says({"stats"});
  CHECK(line == "skin off frames=1 words=1234 draw_us=567 peak_us=567 ballast=0\n");
  CHECK(console_says({}) == line);  // no word is stats
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
