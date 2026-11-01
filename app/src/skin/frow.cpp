// SPDX-License-Identifier: GPL-3.0-or-later
#include "frow.h"

#include "knob.h"

#include <cstring>

namespace o3_303::skin {

namespace {

using ui::Color;
using ui::Face;
using ui::Rect;
using skin_assets::Id;

// The row's grid. A slot's centre is 50 + 100 i; the label line is the top 16 px, the knob or key the middle, the readout window the bottom 22.
constexpr int kLabelY = 387;
constexpr int kTabW = 22;
constexpr int kTabH = 16;
constexpr int kNameX = 26;        // from the tab's left edge: the tab, a gap of four
constexpr int kKnobCy = 432;
constexpr int kSwitchCy = 430;
constexpr int kLedDx = -37;       // the latch LED, left of the knob and under its label
constexpr int kLedCy = 446;
constexpr int kKeyY = 405;
constexpr int kKeyW = 84;
constexpr int kKeyH = 36;         // the cap's face (the bitmap is 40 high with its lower lip)
constexpr int kReadoutY = 457;
constexpr int kReadoutW = 92;
constexpr int kReadoutH = 22;
constexpr int kEmptyW = 80;
constexpr int kEmptyY = 406;
constexpr int kEmptyH = 40;
constexpr unsigned kPointerW_q4 = 18;  // the line's half width: 2.25 px across

constexpr int char_w(Face f) noexcept { return f == Face::Data ? 10 : (f == Face::Label ? 8 : 6); }
constexpr int char_h(Face f) noexcept { return f == Face::Data ? 20 : (f == Face::Label ? 16 : 12); }

int cx_of(unsigned slot) noexcept { return kSlotW / 2 + kSlotW * static_cast<int>(slot); }

int len_of(const char* s) noexcept { return static_cast<int>(std::strlen(s)); }

// Text centred on cx in a box whose top is `top` and height `h`.
void centred(Panel& p, int cx, int top, int h, const char* s, Color c, Face f) noexcept {
  p.text(cx - len_of(s) * char_w(f) / 2, top + (h - char_h(f)) / 2, s, c, f);
}

}  // namespace

void queue_label(Panel& p, int cx, int y, const char* fkey, const char* name, Color name_ink, Color tab, Color tab_ink, Face face) noexcept {
  const int total = kNameX + len_of(name) * char_w(face);
  const int x0 = cx - total / 2;
  p.round_rect(Layer::Tab, Rect{x0, y, kTabW, kTabH}, 3 * 16, theme().rgb(tab));
  p.text(x0 + (kTabW - len_of(fkey) * char_w(Face::Label)) / 2, y, fkey, tab_ink, Face::Label);
  p.text(x0 + kNameX, y + (kTabH - char_h(face)) / 2, name, name_ink, face);
}

// The readout window under a slot with its state: the amber rim of a held key, the red fill of the focused knob (a lock is drawn in the accent colour instead), the text.
void queue_readout(Panel& p, int cx, int y, int w, const FSlot& s) noexcept {
  const Rect win{cx - w / 2, y, w, kReadoutH};
  window(p, win, 3);
  if (s.held) {
    p.round_rect(Layer::HeldRim, Rect{win.x - 2, win.y - 3, win.w + 4, win.h + 5}, 5 * 16, theme().rgb(Color::Accent));
  }
  Color ink = s.kind == SlotKind::Action ? Color::TextDim : Color::Text;
  if (s.locked) {
    ink = Color::Accent;
  } else if (s.focus && s.kind != SlotKind::Action) {
    p.round_rect(Layer::Fill, Rect{win.x + 1, win.y, win.w - 2, win.h - 1}, 2 * 16, pal::kFocusFill);
    ink = Color::SelText;
  }
  const unsigned n = static_cast<unsigned>(len_of(s.readout));
  const Face face = s.kind == SlotKind::Action ? hint_face(n) : readout_face(n);
  centred(p, cx, win.y, win.h, s.readout, ink, face);
}

// A slide switch with its two names either side; the lever is at the side that is on.
void queue_switch(Panel& p, int cx, int cy, const FSlot& s) noexcept {
  const bool on = s.norm >= 0.5f;
  p.text(cx - 20 - len_of(s.off_name) * char_w(Face::Micro), cy - 6, s.off_name, on ? Color::ChromeDim : Color::ChromeText, Face::Micro);
  p.text(cx + 20, cy - 6, s.on_name, on ? Color::ChromeText : Color::ChromeDim, Face::Micro);
  p.round_rect(Layer::Switch, Rect{cx - 17, cy - 5, 34, 12}, 88, pal::kWindowLip, 120);  // the lit lip under the track
  p.round_rect(Layer::Switch, Rect{cx - 17, cy - 6, 34, 12}, 88, ui::Rgb{0, 0, 0});
  const int lx = on ? cx + 8 : cx - 8;
  p.round_rect(Layer::Switch, Rect{lx - 7, cy - 7, 14, 18}, 48, ui::Rgb{0, 0, 0}, 120);  // its shadow
  p.round_rect(Layer::Switch, Rect{lx - 7, cy - 9, 14, 18}, 48, pal::kLever);
  p.round_rect(Layer::Switch, Rect{lx - 6, cy - 8, 12, 5}, 32, ui::Rgb{255, 255, 255}, 90);  // its light
  for (int k = 0; k < 3; ++k) {  // the grip
    p.rect(Layer::Switch, Rect{lx - 4, cy - 1 + 3 * k, 9, 1}, ui::Rgb{0, 0, 0}, 110);
  }
}

namespace {

// "Fn NAME": the red tab with the F-number, then the name, the pair centred on the slot. The Shift row's tabs are amber.
void label_line(Panel& p, int cx, const FSlot& s, bool shift) noexcept {
  queue_label(p, cx, kLabelY, s.fkey, s.name, s.focus && !shift ? Color::ChromeText : Color::ChromeDim, shift ? Color::Accent : Color::Rec,
              shift ? Color::AccentText : Color::SelText, Face::Label);
}

void knob(Panel& p, int cx, const FSlot& s) noexcept {
  const skin_assets::Info& scale = skin_assets::info(Id::Knob42Scale);
  const skin_assets::Info& body = skin_assets::info(Id::Knob42Body);
  const skin_assets::Info& cap = skin_assets::info(Id::Knob42Cap);
  p.sprite(Layer::KnobScale, Id::Knob42Scale, 0, cx - scale.cell_w / 2, kKnobCy - scale.cell_h / 2, pal::kScale);
  p.sprite(Layer::KnobBody, Id::Knob42Body, 0, cx - body.cell_w / 2, kKnobCy - body.cell_h / 2, pal::kSkirt);
  p.sprite(Layer::KnobCap, Id::Knob42Cap, 0, cx - cap.cell_w / 2, kKnobCy - cap.cell_h / 2, pal::kCap);
  const Pointer pt = pointer(s.norm);
  p.line(Layer::Pointer, cx * 16 + pt.x0_q4, kKnobCy * 16 + pt.y0_q4, cx * 16 + pt.x1_q4, kKnobCy * 16 + pt.y1_q4, kPointerW_q4, pal::kPointer);
}

// A raised key with its name in it. The name takes the biggest face that fits the cap's 76 px.
void action_key(Panel& p, int cx, const FSlot& s) noexcept {
  p.sprite(Layer::ActKey, Id::ActKey, 0, cx - kKeyW / 2, kKeyY, s.dim ? pal::kKeyDim : pal::kKey);
  const int n = len_of(s.name);
  const Face face = n * 10 <= 76 ? Face::Data : (n * 8 <= 76 ? Face::Label : Face::Micro);
  centred(p, cx, kKeyY, kKeyH, s.name, s.dim ? Color::TextDim : Color::ChromeText, face);
}

}  // namespace

void fill_frow(FRowView& out, const view::Knob (&knobs)[kSlots], bool shift) noexcept {
  out = FRowView{};
  out.shift = shift;
  for (unsigned i = 0; i < static_cast<unsigned>(kSlots); ++i) {
    const view::Knob& k = knobs[i];
    FSlot& s = out.slot[i];
    s.kind = slot_kind(k);
    cut_to(s.fkey, sizeof s.fkey, k.fkey, 2);
    if (s.kind == SlotKind::Empty) {
      continue;
    }
    cut_to(s.name, sizeof s.name, k.label, kNameChars);
    s.focus = k.focus;
    s.held = k.held;
    s.locked = k.locked;
    s.dim = k.dim;
    if (s.kind == SlotKind::Action) {  // the hint under the key
      cut_to(s.readout, sizeof s.readout, k.value, kReadoutChars);
      continue;
    }
    // The value and its unit ("350" "Hz") when both fit the window, else the value alone, cut.
    cut_to(s.readout, sizeof s.readout, k.value, kReadoutChars);
    if (s.kind == SlotKind::Knob) {
      const unsigned used = static_cast<unsigned>(std::strlen(s.readout));
      const char* unit = k.unit != nullptr ? k.unit : "";
      if (used + std::strlen(unit) <= kReadoutChars) {
        cut_to(s.readout + used, static_cast<unsigned>(sizeof s.readout) - used, unit, kReadoutChars - used);
      }
      s.norm = k.norm;
    } else {
      cut_to(s.off_name, sizeof s.off_name, k.off_name, kCaptionChars);
      cut_to(s.on_name, sizeof s.on_name, k.on_name, kCaptionChars);
      s.norm = k.norm >= 0.5f ? 1.0f : 0.0f;
    }
  }
}

void draw_frow(Panel& p, const FRowView& v) noexcept {
  for (unsigned i = 0; i < static_cast<unsigned>(kSlots); ++i) {
    const FSlot& s = v.slot[i];
    const int cx = cx_of(i);
    if (s.kind == SlotKind::Empty) {
      window(p, Rect{cx - kEmptyW / 2, kEmptyY, kEmptyW, kEmptyH}, 4);
      centred(p, cx, kEmptyY, kEmptyH, s.fkey, Color::TextDim, Face::Label);
      continue;
    }
    label_line(p, cx, s, v.shift);
    switch (s.kind) {
      case SlotKind::Knob:
        knob(p, cx, s);
        led(p, cx + kLedDx, kLedCy, s.focus, s.locked ? pal::kLedAmber : pal::kLedRed);
        break;
      case SlotKind::Switch:
        queue_switch(p, cx, kSwitchCy, s);
        led(p, cx + kLedDx, kLedCy, s.focus, s.locked ? pal::kLedAmber : pal::kLedRed);
        break;
      case SlotKind::Action:
        action_key(p, cx, s);
        break;
      case SlotKind::Empty:
        break;
    }
    if (s.readout[0] != '\0') {
      queue_readout(p, cx, kReadoutY, kReadoutW, s);
    }
  }
}

}  // namespace o3_303::skin
