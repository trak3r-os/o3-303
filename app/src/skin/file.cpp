// SPDX-License-Identifier: GPL-3.0-or-later
#include "file.h"

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

// The glass (docs/SKIN-PLAN.md 2.1: y 66 to 384, inset by its rim): the info line, a rule, the list, the PADS line.
constexpr int kGlassBottom = 384;
constexpr int kPadsY = kGlassBottom - frame::kPadsLineH - 2;  // the PADS line: the glass's last 16 px, above its rim
constexpr int kInfoY = 76;                                     // the info line's text (Data, 20 px high)
constexpr int kRuleY = 102;
constexpr int kListX = 16;
constexpr int kListW = 768;
constexpr int kListY = 106;
constexpr int kRowH = 28;
constexpr int kMarkW = 6;       // the scroll mark's width, a gap of 8 px from the rows
constexpr int kMarkGap = 8;
constexpr int kCursorX = 12;    // from the row's left edge: the ">" of the cursor row
constexpr int kNameX = 34;      // the project's name
constexpr int kStarFromRight = 34;  // the "*" of the current project, from the row's right edge
constexpr int kInfoDirX = 22;
constexpr int kInfoNowX = 232;

// The F row's hints, one for each key (docs/UX-303 5.8): what the key will do. An armed key says "again?".
constexpr const char* kHints[8] = {"selected", "save point", "new name", "new name", "last save", "tap twice", "", "512 lines"};
constexpr const char* kNameHints[8] = {"in focus", "", "", "", "", "", "accept", "drop it"};
constexpr const char* kAgain = "again?";

constexpr int centred_x(int cx, int chars, Face f) noexcept { return cx - chars * face_w(f) / 2; }

// The info line: DIR and the folder, NOW and the current project, the autosave line at the right edge.
void queue_info(Panel& p, const FileView& v) noexcept {
  p.text(kInfoDirX, kInfoY + 2, "DIR", Color::ChromeDim, Face::Label);
  p.text(kInfoDirX + 3 * face_w(Face::Label) + 8, kInfoY, "Apps/o3_303/", Color::Text, Face::Data);
  p.text(kInfoNowX, kInfoY + 2, "NOW", Color::ChromeDim, Face::Label);
  p.text(kInfoNowX + 3 * face_w(Face::Label) + 8, kInfoY, v.current, Color::Text, Face::Data);
  if (v.saved[0] != '\0') {
    const int w = text_len(v.saved) * face_w(Face::Label);
    p.text(kListX + kListW - 6 - w, kInfoY + 2, v.saved, saved_ink(v.saved), Face::Label);
  }
  p.rect(Layer::Rule, Rect{kListX, kRuleY, kListW, 1}, pal::kRule);
}

// The list's rows, or its empty state.
void queue_list(Panel& p, const FileView& v) noexcept {
  if (v.total == 0u) {  // the headline in the Display face (red when the card is the trouble), the hint under it
    const bool trouble = v.card[0] != '\0';
    const int mid = kListY + static_cast<int>(kFileRows) * kRowH / 2;
    const int cx = kListX + kListW / 2;
    p.text(centred_x(cx, text_len(v.empty), Face::Display), mid - 36, v.empty, trouble ? Color::Rec : Color::Text, Face::Display);
    if (v.empty_hint[0] != '\0') {
      p.text(centred_x(cx, text_len(v.empty_hint), Face::Label), mid + 8, v.empty_hint, Color::TextDim, Face::Label);
    }
    return;
  }
  const bool mark = v.total > kFileRows;
  for (unsigned i = 0; i < v.shown && i < kFileRows; ++i) {
    const FileRow& row = v.row[i];
    const Rect box = file_row_box(i, mark);
    if (row.cursor) {
      p.round_rect(Layer::Fill, box, 4 * 16, theme().rgb(Color::Sel));
      p.text(box.x + kCursorX, box.y + (box.h - face_h(Face::Data)) / 2, ">", Color::SelText, Face::Data);
    } else {
      p.rect(Layer::Rule, Rect{box.x + 6, box.y + box.h - 1, box.w - 12, 1}, pal::kBezel);
    }
    const int ty = box.y + (box.h - face_h(Face::Data)) / 2;
    p.text(box.x + kNameX, ty, row.name, row.cursor ? Color::SelText : Color::Text, Face::Data);
    if (row.current) {
      p.text(box.x + box.w - kStarFromRight, ty, "*", row.cursor ? Color::SelText : Color::Accent, Face::Data);
    }
  }
  if (mark) {
    p.rect(Layer::Rule, file_scroll_track(v.shown), pal::kBezel);
    p.rect(Layer::Fill, file_scroll_thumb(v.shown, v.total, v.first, kFileRows), theme().rgb(Color::TextDim));
  }
}

}  // namespace

Rect file_row_box(unsigned row, bool mark) noexcept {
  return Rect{kListX, kListY + static_cast<int>(row) * kRowH, kListW - (mark ? kMarkW + kMarkGap : 0), kRowH};
}

Rect file_scroll_track(unsigned shown) noexcept { return Rect{kListX + kListW - kMarkW, kListY, kMarkW, static_cast<int>(shown) * kRowH}; }

Rect file_scroll_thumb(unsigned shown, unsigned total, unsigned first, unsigned rows) noexcept {
  const Rect track = file_scroll_track(shown);
  if (total <= rows || rows == 0u) {
    return track;
  }
  int h = static_cast<int>(static_cast<unsigned>(track.h) * rows / total);
  h = h < 12 ? 12 : h;
  const unsigned max_first = total - rows;
  const unsigned at = first < max_first ? first : max_first;
  return Rect{track.x, track.y + static_cast<int>(static_cast<unsigned>(track.h - h) * at / max_first), track.w, h};
}

Color saved_ink(const char* saved) noexcept {
  if (std::strncmp(saved, "AUTOSAVED", 9) == 0) {
    return Color::Play;
  }
  if (std::strncmp(saved, "SAVING", 6) == 0) {
    return Color::Accent;
  }
  if (std::strncmp(saved, "NOT SAVED", 9) == 0) {
    return Color::Rec;
  }
  return Color::TextDim;
}

Rect file_keyboard_area() noexcept { return Rect{6, kHeaderH + 4, ui::kScreenW - 12, kPadsY - kHeaderH - 4}; }

void fill_file_frow(FRowView& out, const FileView& v) noexcept {
  static constexpr const char* kFKeys[8] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
  view::Knob knobs[kSlots];
  for (unsigned k = 0; k < static_cast<unsigned>(kSlots); ++k) {
    view::Knob& kn = knobs[k];
    kn.fkey = kFKeys[k];
    const char* label = v.labels[k] != nullptr ? v.labels[k] : "";  // a key the screen gave no label is an empty box
    kn.label = label;
    kn.blank = label[0] == '\0';
    kn.norm = -1.0f;  // an action key
    kn.dim = ((v.dim >> k) & 1u) != 0u;
    const bool hot = ((v.hot >> k) & 1u) != 0u;
    const char* hint = hot ? kAgain : (v.naming ? kNameHints[k] : kHints[k]);
    cut_to(kn.value, sizeof kn.value, hint, kReadoutChars);
  }
  fill_frow(out, knobs, false);
  for (unsigned k = 0; k < static_cast<unsigned>(kSlots); ++k) {
    out.slot[k].lit = out.slot[k].kind == SlotKind::Action && ((v.hot >> k) & 1u) != 0u;  // an armed key waits for its second press: lit red
  }
}

void draw_file(ui::Renderer& r, const FileView& v, const frame::StatusView& status, const HeaderInputs& in, const o3sdk::app::NameEntry* names, const char* pads_line) noexcept {
  Panel& panel = skin::panel();
  panel.clear(in.covered);
  queue_panel(panel, kHeaderH, kGlassBottom);

  HeaderView header;
  fill_header(header, status, in);
  draw_header(panel, header);
  FRowView frow;
  fill_file_frow(frow, v);
  draw_frow(panel, frow);

  if (names == nullptr) {
    queue_info(panel, v);
    queue_list(panel, v);
  }

  ui::plot::Canvas canvas(r);
  canvas.open();
  panel.flush_shapes(canvas);
  canvas.close();
  panel.flush_text(r);

  if (names != nullptr) {  // the kit's keyboard, centred in the glass, in the obsidian theme
    names->draw(r, file_keyboard_area());
  }
  frame::draw_pads_line(r, Rect{6, kPadsY, ui::kScreenW - 12, frame::kPadsLineH}, pads_line);
  draw_ballast(r, ballast());  // `skin ballast N` (SU1): after everything, so the list is that much longer
}

}  // namespace o3_303::skin
