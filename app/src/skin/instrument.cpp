// SPDX-License-Identifier: GPL-3.0-or-later
#include "instrument.h"

#include "frow.h"
#include "knob.h"
#include "panel.h"
#include "skin.h"

#include "../frame.h"

#include <o3sdk/ui/plot/canvas.h>

#include <cstring>

namespace o3_303::skin {

namespace {

using ui::Color;
using ui::Face;
using ui::Rect;
using skin_assets::Id;

constexpr int char_w(Face f) noexcept { return f == Face::Data ? 10 : (f == Face::Label ? 8 : 6); }
int len_of(const char* s) noexcept { return static_cast<int>(std::strlen(s)); }

// The knobs (docs/SKIN-PLAN.md 2.4): eight slots of 100 px under the header, the same grid as the F row's. A slot's label line is the top 16 px, the knob58 (76 px) the middle, the readout
// window under it, the latch LED left of the knob and under its label.
constexpr int kLabelY = 70;
constexpr int kKnobCy = 124;
constexpr int kLedDx = -40;
constexpr int kLedCy = 154;
constexpr int kReadoutY = 164;
constexpr int kReadoutW = 92;
constexpr int kEmptyW = 80;
constexpr int kEmptyY = 106;
constexpr int kEmptyH = 40;
constexpr unsigned kNameMax = 12;                       // a name of up to twelve 6x12 characters fits the slot beside its tab
constexpr unsigned kPointerW_q4 = 24;                   // knob58's pointer: 3 px across
constexpr int kKnob58InnerQ4 = 77;                      // from 0.16 of the cap's 30 px radius ...
constexpr int kKnob58OuterQ4 = 451;                     // ... to 1.8 px short of it
constexpr int kSlot = view::kKnobs;

// The sections' boxes (a legend on the top rule): margin, gap, and the rows inside.
constexpr int kBoxY = 198;
constexpr int kBoxH = 64;
constexpr int kBoxMargin = 16;
constexpr int kBoxGap = 12;
constexpr int kBoxPad = 8;
constexpr int kLegendX = 10;      // the legend starts this far in from the box's corner
constexpr int kCellLabelY = 11;   // from the box's top: the 6x12 name, then the window
constexpr int kCellWinY = 26;
constexpr int kCellWinH = 22;
constexpr int kCellGap = 6;

// The step strip.
constexpr int kStripX0 = 66;      // the first column's left edge; the columns are 45 px
constexpr int kPitch = 45;
constexpr int kNoteY = 290;       // the note text's top (10x20)
constexpr int kLedY[3] = {326, 342, 359};
constexpr int kKeyY = 370;
constexpr int kNumberY = 408;
constexpr int kCaptionX = 14;
constexpr int kTabY = 428;
constexpr int kTabH = 20;
constexpr int kPadsY = ui::kScreenH - frame::kPadsLineH - 2;  // the PADS line: the screen's last 16 px

constexpr ui::Rgb kKeyB{36, 32, 34};     // the second tint of the keys (the first is pal::kKey)
constexpr ui::Rgb kLedDimRed{158, 31, 21};  // 0.62 of the STEP LED's red: a step that holds a note
constexpr ui::Rgb kTabIdle{30, 22, 24};

int cx_of(unsigned slot) noexcept { return kSlotW / 2 + kSlotW * static_cast<int>(slot); }

void queue_sections(Panel& p, const InstrumentView& v) noexcept {
  if (v.sections == 0u) {
    return;
  }
  const int n = static_cast<int>(v.sections);
  const int w = (ui::kScreenW - 2 * kBoxMargin - (n - 1) * kBoxGap) / n;
  for (int s = 0; s < n; ++s) {
    const InstrumentSection& sec = v.section[s];
    const Rect box{kBoxMargin + s * (w + kBoxGap), kBoxY, w, kBoxH};
    const int tw = len_of(sec.title) * char_w(Face::Label);
    rule_box(p, box, box.x + kLegendX - 4, box.x + kLegendX + tw + 4);
    p.text(box.x + kLegendX, box.y - 8, sec.title, Color::ChromeDim, Face::Label);
    const int count = static_cast<int>(sec.count);
    if (count == 0) {
      continue;
    }
    const int pitch = (w - 2 * kBoxPad) / count;
    for (int i = 0; i < count; ++i) {
      const int cx = box.x + kBoxPad + pitch * i + pitch / 2;
      p.text(cx - len_of(sec.label[i]) * char_w(Face::Micro) / 2, box.y + kCellLabelY, sec.label[i], Color::ChromeDim, Face::Micro);
      const int ww = pitch - kCellGap;
      window(p, Rect{cx - ww / 2, box.y + kCellWinY, ww, kCellWinH}, 3);
      const unsigned len = static_cast<unsigned>(len_of(sec.value[i]));
      const Face face = value_face(len, ww);
      p.text(cx - static_cast<int>(len) * char_w(face) / 2, box.y + kCellWinY + (kCellWinH - (face == Face::Data ? 20 : (face == Face::Label ? 16 : 12))) / 2, sec.value[i], Color::Text, face);
    }
  }
}

// One knob: the scale, the skirt, the cap, the pointer.
void queue_knob(Panel& p, int cx, float norm) noexcept {
  const skin_assets::Info& scale = skin_assets::info(Id::Knob58Scale);
  const skin_assets::Info& body = skin_assets::info(Id::Knob58Body);
  const skin_assets::Info& cap = skin_assets::info(Id::Knob58Cap);
  p.sprite(Layer::KnobScale, Id::Knob58Scale, 0, cx - scale.cell_w / 2, kKnobCy - scale.cell_h / 2, pal::kScale);
  p.sprite(Layer::KnobBody, Id::Knob58Body, 0, cx - body.cell_w / 2, kKnobCy - body.cell_h / 2, pal::kSkirt);
  p.sprite(Layer::KnobCap, Id::Knob58Cap, 0, cx - cap.cell_w / 2, kKnobCy - cap.cell_h / 2, pal::kCap);
  const Pointer pt = pointer(norm, kKnob58InnerQ4, kKnob58OuterQ4);
  p.line(Layer::Pointer, cx * 16 + pt.x0_q4, kKnobCy * 16 + pt.y0_q4, cx * 16 + pt.x1_q4, kKnobCy * 16 + pt.y1_q4, kPointerW_q4, pal::kPointer);
}

// The label line of slot `k`: "Fn NAME", or while Shift is held "Fn LEARN" (bright when armed) and "Fn -" (dim), on an amber tab.
void queue_knob_label(Panel& p, int cx, const view::Knob& kn, const FSlot& slot, Learn learn, char (&name)[kNameMax + 1]) noexcept {
  const bool shift = learn != Learn::None;
  const char* text = learn == Learn::No ? "-" : (shift ? "LEARN" : kn.label);
  cut_to(name, sizeof name, text, kNameMax);
  Color ink = slot.focus ? Color::ChromeText : Color::ChromeDim;
  if (learn == Learn::Armed) {
    ink = Color::Accent;
  } else if (learn == Learn::No) {
    ink = Color::TextDim;
  } else if (learn == Learn::Can) {
    ink = Color::ChromeText;
  }
  queue_label(p, cx, kLabelY, slot.fkey, name, ink, shift ? Color::Accent : Color::Rec, shift ? Color::AccentText : Color::SelText, len_of(name) <= 8 ? Face::Label : Face::Micro);
}

void queue_knobs(Panel& p, const InstrumentView& v, const FRowView& row, char (&names)[kSlot][kNameMax + 1]) noexcept {
  for (unsigned i = 0; i < static_cast<unsigned>(kSlot); ++i) {
    const FSlot& s = row.slot[i];
    const int cx = cx_of(i);
    if (s.kind == SlotKind::Empty) {
      window(p, Rect{cx - kEmptyW / 2, kEmptyY, kEmptyW, kEmptyH}, 4);
      p.text(cx - len_of(s.fkey) * char_w(Face::Label) / 2, kEmptyY + (kEmptyH - 16) / 2, s.fkey, Color::TextDim, Face::Label);
      continue;
    }
    const Learn learn = v.learn[i];
    queue_knob_label(p, cx, v.knob[i], s, learn, names[i]);
    if (s.kind == SlotKind::Switch) {
      queue_switch(p, cx, kKnobCy - 2, s);
    } else {
      queue_knob(p, cx, s.norm);
    }
    // The latch LED: lit for the knob the encoder drives (amber on a lock); on the Shift row it is the learn that is waiting.
    const bool on = learn != Learn::None ? learn == Learn::Armed : s.focus;
    led(p, cx + kLedDx, kLedCy, on, s.locked || learn == Learn::Armed ? pal::kLedAmber : pal::kLedRed);
    queue_readout(p, cx, kReadoutY, kReadoutW, s);
  }
}

// The strip's LEDs: one pass for each colour, so the chip's colour is set five times and not once an LED.
enum Tone : unsigned { kOff, kDim, kAmber, kGreen, kRed, kTones };

Tone tone_of(StripRow row, const StepLeds& l) noexcept {
  switch (row) {
    case StripRow::Accent:
      return l.accent ? kAmber : kOff;
    case StripRow::Slide:
      return l.slide ? kGreen : kOff;
    case StripRow::Step:
      return l.step == StripLed::Lit ? kRed : (l.step == StripLed::Dim ? kDim : kOff);
  }
  return kOff;
}

constexpr ui::Rgb kToneColour[kTones] = {pal::kLedOff, kLedDimRed, pal::kLedAmber, pal::kLedGreen, pal::kLedRed};

void queue_leds(Panel& p, const InstrumentView& v, unsigned playhead_k) noexcept {
  constexpr StripRow kRows[3] = {StripRow::Accent, StripRow::Slide, StripRow::Step};
  for (unsigned t = 0; t < kTones; ++t) {
    for (unsigned k = 0; k < v.cells; ++k) {
      const StepLeds l = step_leds(v.cell[k], k == playhead_k);
      for (const StripRow row : kRows) {
        if (tone_of(row, l) != static_cast<Tone>(t)) {
          continue;
        }
        const Pt c = led_centre(row, k);
        if (t >= kAmber) {
          p.point(Layer::Glow, c.x * 16, c.y * 16, 12 * 16, kToneColour[t], pal::kGlowAlpha);
        }
        p.sprite(Layer::Led, Id::Led, 0, c.x - 8, c.y - 8, kToneColour[t]);
      }
    }
  }
}

// The keys: the rings under them (a held step's amber, then the cursor's white), the bodies in their two tints, the light, the shadow.
void queue_keys(Panel& p, const InstrumentView& v, unsigned cursor_k) noexcept {
  for (unsigned k = 0; k < v.cells; ++k) {
    if ((v.marks.held >> k & 1u) != 0u) {
      p.round_rect(Layer::HeldRim, ring_box(k, true), 7 * 16, theme().rgb(Color::Accent));
    }
  }
  if (cursor_k < v.cells) {
    p.round_rect(Layer::HeldRim, ring_box(cursor_k, false), 6 * 16, theme().rgb(Color::SelText));
  }
  for (unsigned g = 0; g < 2; ++g) {
    for (unsigned k = 0; k < v.cells; ++k) {
      if ((k / 4u) % 2u == g) {
        const Rect b = key_box(k);
        p.sprite(Layer::KeyBody, Id::Key, 0, b.x, b.y, key_tint(k));
      }
    }
  }
  for (unsigned k = 0; k < v.cells; ++k) {
    const Rect b = key_box(k);
    p.sprite(Layer::KeyLight, Id::Key, 1, b.x, b.y, ui::Rgb{255, 255, 255});
  }
  for (unsigned k = 0; k < v.cells; ++k) {
    const Rect b = key_box(k);
    p.sprite(Layer::KeyDark, Id::Key, 2, b.x, b.y, ui::Rgb{0, 0, 0}, 230);
  }
}

// The note text and the number of each step, and the captions of the rows.
void queue_strip_text(Panel& p, const InstrumentView& v, unsigned cursor_k, unsigned playhead_k, char (&numbers)[16][4]) noexcept {
  const bool note_owned = v.marks.row == DrawerRow::Note;
  const bool gate_owned = v.marks.row == DrawerRow::Gate;
  static constexpr const char* kCaption[4] = {"NOTE", "ACCENT", "SLIDE", "STEP"};
  const int cy[4] = {kNoteY + 10, kLedY[0], kLedY[1], kLedY[2]};
  for (int i = 0; i < 4; ++i) {
    const bool owned = i == 0 ? note_owned : gate_owned;  // the colour button that owns the row gives its colour: NOTE teal, GATE amber
    p.text(kCaptionX, cy[i] - 6, kCaption[i], owned ? (i == 0 ? Color::Vol : Color::Accent) : Color::ChromeDim, Face::Micro);
  }
  for (unsigned k = 0; k < v.cells; ++k) {
    const InstrumentCell& c = v.cell[k];
    const bool rest = !c.on;
    const Color ink = note_ink(k == playhead_k, k == cursor_k, rest);
    const int cx = step_cx(k);
    p.text(cx - len_of(c.note) * char_w(Face::Data) / 2, kNoteY, c.note, ink, Face::Data);
    view::fmt_uint(static_cast<unsigned>(v.marks.page * 16) + k + 1u, numbers[k]);
    const bool special = k == playhead_k || k == cursor_k;
    p.text(cx - len_of(numbers[k]) * char_w(Face::Micro) / 2, kNumberY, numbers[k], special ? ink : (k % 4u == 0u ? Color::ChromeText : Color::ChromeDim), Face::Micro);
  }
}

// PAGES: a tab for each page of the group (the page shown is red), and what the buttons do on it. A group of one page (GROWL, WOBBLE) has one tab.
void queue_tabs(Panel& p, const InstrumentView& v) noexcept {
  p.text(16, kTabY + 4, "PAGES", Color::ChromeDim, Face::Micro);
  int x = 60;
  for (unsigned i = 0; i < v.tab_count; ++i) {
    const int w = len_of(v.tabs[i]) * char_w(Face::Label) + 16;
    const bool on = i == v.page;
    p.round_rect(Layer::Tab, Rect{x, kTabY, w, kTabH}, 3 * 16, on ? theme().rgb(Color::Rec) : kTabIdle);
    p.text(x + 8, kTabY + 2, v.tabs[i], on ? Color::SelText : Color::TextDim, Face::Label);
    x += w + 8;
  }
  p.text(x + 8, kTabY + 4, v.hint, Color::TextDim, Face::Micro);
}

}  // namespace

int step_cx(unsigned k) noexcept { return kStripX0 + kPitch * static_cast<int>(k) + kPitch / 2; }

Rect key_box(unsigned k) noexcept { return Rect{step_cx(k) - 21, kKeyY, 42, 36}; }

Rect ring_box(unsigned k, bool held) noexcept {
  const Rect b = key_box(k);  // the key's body is 34 x 26, four px in: the cursor's ring is two px round it, a held step's three
  return held ? Rect{b.x + 1, b.y + 1, 40, 32} : Rect{b.x + 2, b.y + 2, 38, 30};
}

Pt led_centre(StripRow row, unsigned k) noexcept { return Pt{step_cx(k), kLedY[static_cast<unsigned>(row)]}; }

StepLeds step_leds(const InstrumentCell& c, bool playhead) noexcept {
  return StepLeds{c.accent, c.slide, playhead ? StripLed::Lit : (c.on ? StripLed::Dim : StripLed::Off)};
}

ui::Rgb led_tint(StripRow row, const StepLeds& leds) noexcept { return kToneColour[tone_of(row, leds)]; }

ui::Rgb key_tint(unsigned k) noexcept { return (k / 4u) % 2u == 0u ? pal::kKey : kKeyB; }

Color note_ink(bool playhead, bool cursor, bool rest) noexcept {
  if (playhead) {
    return Color::Play;
  }
  if (cursor) {
    return Color::Instr;
  }
  return rest ? Color::TextDim : Color::Text;
}

Face value_face(unsigned n, int w) noexcept {
  const int room = w - 6;
  const int len = static_cast<int>(n);
  return len * char_w(Face::Data) <= room ? Face::Data : (len * char_w(Face::Label) <= room ? Face::Label : Face::Micro);
}

void draw_instrument(ui::Renderer& r, const InstrumentView& v, const frame::StatusView& status, const HeaderInputs& in, const char* pads_line) noexcept {
  Panel& panel = skin::panel();
  panel.clear();
  queue_panel_full(panel, kHeaderH);

  HeaderView header;
  fill_header(header, status, in);
  draw_header(panel, header);

  // The page's eight knobs: the F row's slots (kind, readout, state), drawn at the top.
  FRowView row;
  fill_frow(row, v.knob, false);  // the slots only: this screen draws its own label line (queue_knob_label), so FRowView::shift is not read
  char names[kSlot][kNameMax + 1];
  queue_knobs(panel, v, row, names);

  queue_sections(panel, v);

  const unsigned page_k = static_cast<unsigned>(v.marks.page) * 16u;
  const unsigned cursor_k = v.marks.cursor >= static_cast<int>(page_k) ? static_cast<unsigned>(v.marks.cursor) - page_k : 16u;  // 16: not on this page
  const unsigned playhead_k = v.marks.playhead >= static_cast<int>(page_k) ? static_cast<unsigned>(v.marks.playhead) - page_k : 16u;
  queue_leds(panel, v, playhead_k);
  queue_keys(panel, v, cursor_k);
  char numbers[16][4];
  queue_strip_text(panel, v, cursor_k, playhead_k, numbers);
  queue_tabs(panel, v);

  ui::plot::Canvas canvas(r);
  canvas.open();
  panel.flush_shapes(canvas);
  canvas.close();
  panel.flush_text(r);

  frame::draw_pads_line(r, Rect{6, kPadsY, ui::kScreenW - 12, frame::kPadsLineH}, pads_line);
  draw_ballast(r, ballast());  // `skin ballast N` (SU1): after everything, so the list is that much longer
}

}  // namespace o3_303::skin
