// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame.h"

#include "step_view.h"

#include <o3sdk/core/param.h>
#include <o3sdk/ui/components.h>
#include <o3_303/perform.h>

namespace o3_303::frame {

namespace {

using namespace o3sdk;
using ui::Color;
using ui::Face;
using ui::Rect;

void cat(char* out, unsigned cap, const char* s) noexcept {
  unsigned n = 0;
  while (n < cap && out[n] != '\0') {
    ++n;
  }
  for (; *s != '\0' && n + 1u < cap; ++s) {
    out[n++] = *s;
  }
  if (n < cap) {
    out[n] = '\0';
  }
}

// "OCT 2-3": the keyboard's two octaves (the bottom row is octave 2 at shift 0).
void octave_text(int shift, char* out, unsigned cap) noexcept {
  out[0] = '\0';
  cat(out, cap, "OCT ");
  for (int i = 0; i < 2; ++i) {
    const int o = 2 + shift + i;
    char num[4];
    unsigned n = 0;
    if (o < 0) {
      num[n++] = '-';
    }
    n += view::fmt_uint(static_cast<unsigned>(o < 0 ? -o : o), num + n);
    num[n] = '\0';
    if (i == 1) {
      cat(out, cap, "-");
    }
    cat(out, cap, num);
  }
}

unsigned popcount16(std::uint16_t v) noexcept {
  unsigned n = 0;
  for (; v != 0u; v = static_cast<std::uint16_t>(v & (v - 1u))) {
    ++n;
  }
  return n;
}

// A chip and the gap after it, as FieldRow::chip and skip(6) lay them out.
int chip_pitch(const ui::Renderer& r, const char* text) noexcept { return r.text_width(text, Face::Label) + 2 * r.theme().metrics.gap + 6; }

void more_text(unsigned n, char* out) noexcept {
  out[0] = '+';
  const unsigned len = view::fmt_uint(n, out + 1);
  out[1u + len] = '\0';
}

// How many of the chips of `actions` are drawn in `room` (with the "+N" chip when some are not).
unsigned chips_that_fit(const ui::Renderer& r, std::uint16_t actions, int room) noexcept {
  const unsigned total = popcount16(actions);
  int used = 0;
  unsigned shown = 0;
  for (unsigned a = 0; a < kPerfActionCount && shown < total; ++a) {
    if ((actions >> a & 1u) == 0u) {
      continue;
    }
    const unsigned left = total - (shown + 1u);
    char more[8];
    if (left > 0u) {
      more_text(left, more);
    }
    if (used + chip_pitch(r, kPerfActionChips[a]) + (left > 0u ? chip_pitch(r, more) : 0) > room) {
      break;
    }
    used += chip_pitch(r, kPerfActionChips[a]);
    ++shown;
  }
  return shown;
}

}  // namespace

int action_chips_width(ui::Renderer& r, std::uint16_t actions, int room) noexcept {
  const unsigned total = popcount16(actions);
  if (total == 0u || room <= 0) {
    return 0;
  }
  const unsigned shown = chips_that_fit(r, actions, room);
  int w = 0;
  unsigned n = 0;
  for (unsigned a = 0; a < kPerfActionCount && n < shown; ++a) {
    if ((actions >> a & 1u) != 0u) {
      w += chip_pitch(r, kPerfActionChips[a]);
      ++n;
    }
  }
  if (shown < total) {
    char more[8];
    more_text(total - shown, more);
    const int mw = chip_pitch(r, more);
    if (w + mw > room) {
      return w;  // not even the "+N" fits after the chips that did not make it; nothing is added
    }
    w += mw;
  }
  return w;
}

int draw_action_chips(ui::Renderer& r, int x, int x_limit, int baseline, std::uint16_t actions) noexcept {
  const unsigned total = popcount16(actions);
  const int room = x_limit - x;
  if (total == 0u || room <= 0) {
    return x;
  }
  const unsigned shown = chips_that_fit(r, actions, room);
  ui::FieldRow row(r, x, baseline);
  unsigned n = 0;
  for (unsigned a = 0; a < kPerfActionCount && n < shown; ++a) {
    if ((actions >> a & 1u) != 0u) {
      row.chip(Face::Label, kPerfActionChips[a], Color::Sel, Color::SelText);
      row.skip(6);
      ++n;
    }
  }
  if (shown < total) {
    char more[8];
    more_text(total - shown, more);
    if (row.x() + chip_pitch(r, more) <= x_limit) {
      row.chip(Face::Label, more, Color::ChromeDim, Color::Chrome);
      row.skip(6);
    }
  }
  return row.x();
}

void draw_status(ui::Renderer& r, const Rect& line, const StatusView& view) noexcept {
  ui::strip(r, line, ui::Edge::Bottom);
  const int baseline = line.y + 15;
  ui::FieldRow row(r, line.x + 8, baseline);
  row.put(Face::Label, "ENC >", Color::ChromeDim);
  row.skip(8);
  row.put(Face::Label, view.enc, Color::ChromeText);
  row.skip(24);
  if (view.keys) {
    if (view.trnsp) {  // the mode that is on is a filled chip, the other an outline
      row.chip(Face::Label, "TRNSP", Color::ChromeText, Color::Chrome);
    } else {
      row.chip(Face::Label, "KEYS", Color::ChromeText, Color::Chrome);
    }
    row.skip(8);
    char oct[16];
    octave_text(view.octave, oct, sizeof oct);
    row.put(Face::Label, oct, Color::ChromeDim);
    row.skip(14);
  }
  if (view.rec) {
    row.chip(Face::Label, "REC", Color::Rec, Color::SelText);
    row.skip(8);
  }
  if (view.take[0] != '\0') {
    row.chip(Face::Label, view.take, Color::Rec, Color::SelText);
    row.skip(8);
  }
  // Right aligned: the failed autosave, and the toast after it (their room is reserved first: no chip is drawn over them).
  int x = line.x + line.w - 8;
  const auto width_of = [&](const char* s) { return r.text_width(s, Face::Label) + 12 + 8; };
  int reserved = 0;
  if (view.save_failed) {
    reserved += width_of("SAVE FAILED");
  }
  if (view.toast[0] != '\0') {
    reserved += width_of(view.toast);
  }
  if (view.take_msg[0] != '\0') {
    reserved += width_of(view.take_msg);
  }
  (void)draw_action_chips(r, row.x(), x - reserved, baseline, view.actions);
  const auto put_right = [&](const char* s, Color fill, Color ink, bool outline) {
    const int w = r.text_width(s, Face::Label) + 12;
    x -= w;
    if (outline) {
      r.frame(Rect{x, line.y + 3, w, 14}, ink);
    } else {
      r.panel(Rect{x, line.y + 3, w, 14}, fill);
    }
    r.text(x + 6, ui::caps_y(r.theme(), Face::Label, line.y + 3, 14), s, ink, Face::Label);
    x -= 8;
  };
  if (view.save_failed) {
    put_right("SAVE FAILED", Color::Rec, Color::SelText, false);
  }
  if (view.take_msg[0] != '\0') {  // the audio take's word, from whatever screen asked for it
    put_right(view.take_msg, Color::Sel, view.take_msg_off ? Color::ChromeText : Color::SelText, view.take_msg_off);
  }
  if (view.toast[0] != '\0') {  // a message that says something did not happen is an outline, as the 303 screen's
    put_right(view.toast, Color::Sel, view.toast_off ? Color::ChromeText : Color::SelText, view.toast_off);
  }
}

void draw_pads_line(ui::Renderer& r, const Rect& line, const char* text) noexcept {
  r.panel(line, Color::Pane);
  r.rule(line.x, line.y, line.w, ui::Axis::Horizontal, Color::Empty);
  r.text(line.x + 6, line.y + 2, "PADS", Color::Text, Face::Micro);
  r.text(line.x + 42, line.y + 2, text, Color::TextDim, Face::Micro);
}

void fill_knob(app::Context& ctx, Controls& controls, unsigned k, const Entry& entry, view::Knob& out) noexcept {
  static constexpr const char* kFKeys[kKeys] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
  dsp::Engine& e = ctx.engine();
  const core::ParamDesc* d = controls.desc(e, entry);
  out = view::Knob{};
  out.fkey = kFKeys[k];
  if (d == nullptr) {
    out.blank = true;
    return;
  }
  out.label = controls.label(e, entry);
  (void)controls.format(e, entry, out.value, sizeof out.value);
  out.unit = controls.unit_text(e, entry);
  out.norm = core::param_norm(*d, controls.get(e, entry));
}

}  // namespace o3_303::frame
