// SPDX-License-Identifier: GPL-3.0-or-later
#include "master.h"

#include "frow.h"
#include "meter.h"
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

// The glass (docs/SKIN-PLAN.md 2.1: y 66 to 384, inset by its rim): two columns of boxes, titles on the top rule, over the PADS line.
constexpr int kGlassBottom = 384;
constexpr int kPadsY = kGlassBottom - frame::kPadsLineH - 2;  // the PADS line: the glass's last 16 px, above its rim
constexpr int kLeftX = 16;
constexpr int kLeftW = 392;
constexpr int kRightX = 424;
constexpr int kRightW = 360;

// TEMPO: the display's window (as the header's, 102 x 41, a little higher), "BPM", the tap chip.
constexpr Rect kTempoBox{kLeftX, 92, kLeftW, 84};
constexpr Rect kSegWin{kLeftX + 20, 112, 102, 46};
constexpr int kBpmX = kLeftX + 20 + 102 + 14;
// TIMING: three rows of a label, a readout window and a hint.
constexpr Rect kTimingBox{kLeftX, 196, kLeftW, 154};
constexpr int kRowY0 = 214;
constexpr int kRowPitch = 44;
constexpr int kRowWinH = 30;
constexpr int kLabelX = kLeftX + 20;
constexpr int kWinX = kLeftX + 112;
constexpr int kWinW = 140;
constexpr int kHintX = kWinX + kWinW + 12;
// OUTPUT: L and R bars, the scale, a hint.
constexpr Rect kOutputBox{kRightX, 92, kRightW, 160};
constexpr int kBarX = kRightX + 42;
constexpr int kBarW = 2 * 3 + static_cast<int>(kMasterSegments) * 10;  // 306: the segments' pitch is 10 px
constexpr int kBarY0 = 116;
constexpr int kBarPitch = 42;
constexpr int kBarH = 30;
constexpr int kScaleY = 194;
constexpr int kPeakHintY = 216;
// SYSTEM: CPU and USB.
constexpr Rect kSystemBox{kRightX, 268, kRightW, 82};
constexpr int kSysY0 = 286;
constexpr int kSysPitch = 34;
constexpr int kSysWinH = 26;
constexpr int kSysLabelX = kRightX + 20;
constexpr int kSysWinX = kRightX + 76;
constexpr int kSysWinW = 130;
constexpr int kSysHintX = kSysWinX + kSysWinW + 12;

void queue_tempo(Panel& p, const MasterView& v, char (&cells)[6], char (&chip)[12]) noexcept {
  queue_box(p, kTempoBox, "TEMPO");
  seg_display(p, kSegWin, seg_text(master_tempo_cells(v.tempo, cells)));
  p.text(kBpmX, kSegWin.y + (kSegWin.h - face_h(Face::Display)) / 2, "BPM", Color::ChromeDim, Face::Display);
  if (v.taps[0] != '\0') {  // the tap tempo's measure: how many taps it has so far
    chip[0] = '\0';
    cut_to(chip, sizeof chip, "TAP ", 4);
    cut_to(chip + 4, sizeof chip - 4, v.taps, 3);
    const int w = text_len(chip) * face_w(Face::Label) + 14;
    const Rect box{kTempoBox.x + kTempoBox.w - 16 - w, kSegWin.y + 4, w, 22};
    p.round_rect(Layer::Tab, box, 4 * 16, theme().rgb(Color::Sel));
    p.text(box.x + 7, box.y + 3, chip, Color::SelText, Face::Label);
  }
}

void queue_timing(Panel& p, const MasterView& v, char (&tap)[12]) noexcept {
  queue_box(p, kTimingBox, "TIMING");
  struct Row {
    const char* label;
    const char* value;
    Color ink;
    const char* hint;
  };
  if (v.taps[0] != '\0') {
    tap[0] = '\0';
    cut_to(tap, sizeof tap, v.taps, 3);
    cut_to(tap + text_len(tap), sizeof tap - static_cast<unsigned>(text_len(tap)), std::strcmp(v.taps, "1") == 0 ? " TAP" : " TAPS", 5);
  } else {
    cut_to(tap, sizeof tap, "--", 2);
  }
  const Row rows[3] = {
      {"NUDGE", v.nudge, v.nudged ? Color::Accent : Color::Text, "HOLD F3 + TURN"},
      {"TAP", tap, v.taps[0] != '\0' ? Color::Text : Color::TextDim, "F2 IN TIME"},
      {"CLOCK", v.clock, Color::Text, "(MIDI: CONFIG)"},
  };
  for (int i = 0; i < 3; ++i) {
    const int y = kRowY0 + i * kRowPitch;
    p.text(kLabelX, y + (kRowWinH - face_h(Face::Label)) / 2, rows[i].label, Color::ChromeDim, Face::Label);
    queue_readout_window(p, Rect{kWinX, y, kWinW, kRowWinH}, rows[i].value, rows[i].ink);
    p.text(kHintX, y + (kRowWinH - face_h(Face::Micro)) / 2, rows[i].hint, Color::TextDim, Face::Micro);
  }
}

void queue_output(Panel& p, const MasterView& v) noexcept {
  queue_box(p, kOutputBox, "OUTPUT");
  static constexpr const char* kSide[2] = {"L", "R"};
  for (unsigned c = 0; c < 2u; ++c) {
    const Rect win = master_bar_window(c);
    p.text(kRightX + 20, win.y + (win.h - face_h(Face::Data)) / 2, kSide[c], Color::Text, Face::Data);
    queue_bar(p, win, kMasterSegments, v.level[c], v.peak[c], true);
  }
  // The scale under them: -48, -24 and 0 dB at their places.
  p.text(kBarX + 3, kScaleY, "-48", Color::TextDim, Face::Micro);
  p.text(kBarX + kBarW / 2 - 9, kScaleY, "-24", Color::TextDim, Face::Micro);
  p.text(kBarX + kBarW - 3 - 4 * face_w(Face::Micro), kScaleY, "0 dB", Color::TextDim, Face::Micro);
  p.text(kBarX + 3, kPeakHintY, "THE TICK HOLDS THE PEAK, THEN FALLS", Color::TextDim, Face::Micro);
}

void queue_system(Panel& p, const MasterView& v) noexcept {
  queue_box(p, kSystemBox, "SYSTEM");
  struct Row {
    const char* label;
    const char* value;
    Color ink;
    const char* hint;
  };
  const Row rows[2] = {
      {"CPU", v.cpu, v.cpu_over ? Color::Rec : (std::strcmp(v.cpu, "--") == 0 ? Color::TextDim : Color::Text), "100 % = OVERRUN"},
      {"USB", v.usb, v.usb_streaming ? Color::Play : (std::strcmp(v.usb, "NO PORT") == 0 ? Color::TextDim : Color::Text), "AUDIO PORT"},
  };
  for (int i = 0; i < 2; ++i) {
    const int y = kSysY0 + i * kSysPitch;
    p.text(kSysLabelX, y + (kSysWinH - face_h(Face::Label)) / 2, rows[i].label, Color::ChromeDim, Face::Label);
    queue_readout_window(p, Rect{kSysWinX, y, kSysWinW, kSysWinH}, rows[i].value, rows[i].ink);
    p.text(kSysHintX, y + (kSysWinH - face_h(Face::Micro)) / 2, rows[i].hint, Color::TextDim, Face::Micro);
  }
}

}  // namespace

Rect master_bar_window(unsigned c) noexcept { return Rect{kBarX, kBarY0 + static_cast<int>(c) * kBarPitch, kBarW, kBarH}; }

const char* master_tempo_cells(const char* tempo, char (&out)[6]) noexcept {
  // The engine caps the tempo at dsp::kMaxTempoMilliBpm, 300 BPM ("300.0": five cells); a longer text (never reached) shows as "999.9" rather than losing its last digit.
  if (std::strlen(tempo) > 5u) {
    tempo = "999.9";
  }
  const unsigned n = static_cast<unsigned>(std::strlen(tempo));
  const unsigned pad = n < 5u ? 5u - n : 0u;
  unsigned at = 0;
  for (; at < pad; ++at) {
    out[at] = ' ';
  }
  for (unsigned i = 0; i < n && at < 5u; ++i) {
    out[at++] = tempo[i];
  }
  out[at] = '\0';
  return out;
}

void draw_master(ui::Renderer& r, const MasterView& v, const frame::StatusView& status, const HeaderInputs& in, const char* pads_line) noexcept {
  Panel& panel = skin::panel();
  panel.clear(in.covered);
  queue_panel(panel, kHeaderH, kGlassBottom);

  HeaderView header;
  fill_header(header, status, in);
  draw_header(panel, header);
  FRowView frow;
  fill_frow(frow, v.knob, false);
  draw_frow(panel, frow);

  char cells[6];
  char chip[12];
  char tap[12];
  queue_tempo(panel, v, cells, chip);
  queue_timing(panel, v, tap);
  queue_output(panel, v);
  queue_system(panel, v);

  ui::plot::Canvas canvas(r);
  canvas.open();
  panel.flush_shapes(canvas);
  canvas.close();
  panel.flush_text(r);

  frame::draw_pads_line(r, Rect{6, kPadsY, ui::kScreenW - 12, frame::kPadsLineH}, pads_line);
  draw_ballast(r, ballast());  // `skin ballast N` (SU1): after everything, so the list is that much longer
}

}  // namespace o3_303::skin
