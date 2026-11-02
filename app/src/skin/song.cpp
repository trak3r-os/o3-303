// SPDX-License-Identifier: GPL-3.0-or-later
#include "song.h"

#include "frow.h"
#include "panel.h"
#include "skin.h"

#include "../frame.h"
#include "../step_view.h"

#include <o3sdk/ui/components.h>
#include <o3sdk/ui/plot/canvas.h>

#include <cstring>

namespace o3_303::skin {

namespace {

using ui::Color;
using ui::Face;
using ui::Rect;

// The glass (docs/SKIN-PLAN.md 2.1: y 66 to 384, inset by its rim) split into the list, the readout column and the PADS line.
constexpr int kGlassBottom = 384;
constexpr int kListX = 14;
constexpr int kListY = kHeaderH + 6;
constexpr int kListW = 556;
constexpr int kRowH = 24;
constexpr int kColX = 586;   // the readout column
constexpr int kColW = 203;
constexpr int kBlockH = 40;  // a caption (Micro, 12 px) and, under it, a window of kWinH
constexpr int kWinH = 24;
constexpr int kBlockY = kHeaderH + 6;
constexpr int kBlocks = 5;
constexpr int kHelpY = kBlockY + kBlocks * kBlockH + 6;
constexpr int kHelpLineH = 14;
constexpr int kPadsY = kGlassBottom - frame::kPadsLineH - 2;  // the PADS line: the glass's last 16 px, above its rim
constexpr int kWinTextPad = 6;

// What the buttons and the pads do here, in a few short lines for someone who has never seen the screen (Micro, 33 characters at most).
constexpr const char* kHelpLines[] = {
    "TAP A PAD: SET THE ROW'S PATTERN",
    "ON THE + LINE IT ADDS A ROW",
    "TURN: CHANGE THE PICKED CELL",
    "NOTE INSTR VOL FX: PICK COLUMN",
    "INSERT / DELETE: ADD / REMOVE",
};

// A window's caption and its value, the value in the biggest face that fits.
struct Block {
  const char* caption;
  const char* value;
  Color ink;
};

Face value_face(const char* s) noexcept {
  const int n = static_cast<int>(std::strlen(s));
  return n * 10 <= kColW - 2 * kWinTextPad ? Face::Data : Face::Label;
}

}  // namespace

void draw_song(ui::Renderer& r, const SongView& v, const frame::StatusView& status, const HeaderInputs& in, const view::Knob (&knobs)[view::kKnobs], const char* pads_line) noexcept {
  Panel& panel = skin::panel();
  panel.clear(in.covered);
  queue_panel(panel, kHeaderH, kGlassBottom);

  HeaderView header;
  fill_header(header, status, in);
  draw_header(panel, header);
  FRowView frow;
  fill_frow(frow, knobs, v.shift);
  draw_frow(panel, frow);

  const bool now = v.now[0] != '\0';
  const Block blocks[kBlocks] = {
      {"SONG", v.song, Color::Text},
      {"LENGTH", v.duration, Color::Text},
      {"LOOP", v.loop, v.loop_on ? Color::Text : Color::Accent},
      {"PLAY PLAYS", v.plays, v.plays_song ? Color::Fx : Color::Text},
      {"NOW", now ? v.now : "STOPPED", now ? Color::Play : Color::TextDim},
  };
  for (int i = 0; i < kBlocks; ++i) {
    window(panel, Rect{kColX, kBlockY + i * kBlockH + 13, kColW, kWinH}, 3);
  }

  ui::plot::Canvas canvas(r);
  canvas.open();
  panel.flush_shapes(canvas);
  canvas.close();
  panel.flush_text(r);

  // The glass: the list, the readouts, the help, the PADS line.
  draw_song_list(r, Rect{kListX, kListY, kListW, kPadsY - kListY}, v, kRowH);
  const ui::Theme& theme = r.theme();
  for (int i = 0; i < kBlocks; ++i) {
    const int y = kBlockY + i * kBlockH;
    r.text(kColX + 2, y, blocks[i].caption, Color::TextDim, Face::Micro);
    const Face face = value_face(blocks[i].value);
    r.text(kColX + kWinTextPad, ui::caps_y(theme, face, y + 13, kWinH), blocks[i].value, blocks[i].ink, face);
  }
  for (unsigned i = 0; i < sizeof kHelpLines / sizeof kHelpLines[0]; ++i) {
    r.text(kColX + 2, kHelpY + static_cast<int>(i) * kHelpLineH, kHelpLines[i], Color::TextDim, Face::Micro);
  }
  frame::draw_pads_line(r, Rect{kListX - 6, kPadsY, ui::kScreenW - 2 * (kListX - 6), frame::kPadsLineH}, pads_line);
  draw_ballast(r, ballast());  // `skin ballast N` (SU1): after everything, so the list is that much longer
}

}  // namespace o3_303::skin
