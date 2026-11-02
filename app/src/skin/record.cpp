// SPDX-License-Identifier: GPL-3.0-or-later
#include "record.h"

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

// The glass (docs/SKIN-PLAN.md 2.1: y 66 to 384, inset by its rim): the TAKE and LEVELS boxes side by side, the ENVELOPE under them, the PADS line.
constexpr int kGlassBottom = 384;
constexpr int kPadsY = kGlassBottom - frame::kPadsLineH - 2;  // the PADS line: the glass's last 16 px, above its rim

// TAKE: the state and the clock (Display face, 44 px windows), the bar chip, the sentence, SOURCE and BARS, the file name.
constexpr Rect kTakeBox{16, 92, 392, 140};
constexpr Rect kStateWin{32, 108, 118, 44};
constexpr Rect kClockWin{158, 108, 118, 44};
constexpr int kLineY = 160;
constexpr int kSetY = 178;
constexpr int kSetH = 28;
constexpr Rect kSourceWin{100, kSetY, 108, kSetH};
constexpr Rect kBarsWin{268, kSetY, 82, kSetH};
constexpr int kDestY = 214;
// LEVELS: IN and OUT, two bars each.
constexpr Rect kLevelsBox{424, 92, 360, 140};
constexpr int kBarX = 424 + 50;
constexpr int kBarW = 2 * 3 + static_cast<int>(kRecordSegments) * 12;  // 294: the segments' pitch is 12 px
constexpr int kBarH = 18;
constexpr int kBarY[2][2] = {{108, 130}, {168, 190}};
constexpr int kScaleY = 212;
// ENVELOPE: the box, the columns over its width less 16 px a side, mirrored about the centre line.
constexpr Rect kEnvBox{16, 248, 768, 104};
constexpr int kEnvLeft = 32;
constexpr int kEnvWidth = 736;
constexpr int kEnvMid = 302;
constexpr int kEnvHalf = 36;
constexpr int kEnvMarkTop = 262;
constexpr int kEnvMarkH = 76;

// A window with the text in the Display face.
void queue_display_window(Panel& p, const Rect& win, const char* s, Color ink) noexcept {
  window(p, win, 4);
  p.text(win.x + (win.w - text_len(s) * face_w(Face::Display)) / 2, win.y + (win.h - face_h(Face::Display)) / 2, s, ink, Face::Display);
}

void queue_take(Panel& p, const RecordView& v) noexcept {
  queue_box(p, kTakeBox, "TAKE");
  const bool rec = std::strcmp(v.state, "REC") == 0;
  queue_display_window(p, kStateWin, v.state, rec ? Color::Rec : Color::Text);
  queue_display_window(p, kClockWin, v.clock, Color::Text);
  if (v.bar[0] != '\0') {  // the bar of a BARS take
    const int w = text_len(v.bar) * face_w(Face::Label) + 14;
    const Rect box{kTakeBox.x + kTakeBox.w - 14 - w, kStateWin.y + 11, w, 22};
    p.round_rect(Layer::Tab, box, 4 * 16, theme().rgb(Color::Sel));
    p.text(box.x + 7, box.y + 3, v.bar, Color::SelText, Face::Label);
  }
  p.text(kStateWin.x, kLineY, v.line, Color::Accent, Face::Micro);
  p.text(32, kSetY + (kSetH - face_h(Face::Label)) / 2, "SOURCE", Color::ChromeDim, Face::Label);
  queue_readout_window(p, kSourceWin, v.source, Color::Text);
  p.text(224, kSetY + (kSetH - face_h(Face::Label)) / 2, "BARS", Color::ChromeDim, Face::Label);
  queue_readout_window(p, kBarsWin, v.bars, v.fits ? Color::Text : Color::Rec);
  p.text(kStateWin.x, kDestY, v.dest, v.dest_off ? Color::Rec : Color::TextDim, Face::Micro);
}

void queue_levels(Panel& p, const RecordView& v) noexcept {
  queue_box(p, kLevelsBox, "LEVELS");
  struct Group {
    const char* name;
    const unsigned* level;
    const unsigned* peak;
    bool live;
  };
  const Group groups[2] = {{"IN", v.in, v.in_peak, v.input_meters}, {"OUT", v.out, v.out_peak, true}};
  static constexpr const char* kSide[2] = {"L", "R"};
  for (unsigned g = 0; g < 2u; ++g) {
    const int top = kBarY[g][0];
    const int bottom = kBarY[g][1] + kBarH;
    p.text(kLevelsBox.x + 14, (top + bottom) / 2 - face_h(Face::Label) / 2, groups[g].name, groups[g].live ? Color::Text : Color::TextDim, Face::Label);
    for (unsigned c = 0; c < 2u; ++c) {
      const Rect win = record_bar_window(g, c);
      p.text(kBarX - 14, win.y + (win.h - face_h(Face::Micro)) / 2, kSide[c], Color::TextDim, Face::Micro);
      queue_bar(p, win, kRecordSegments, groups[g].level[c], groups[g].peak[c], groups[g].live);
    }
  }
  p.text(kBarX + 3, kScaleY, "-48", Color::TextDim, Face::Micro);
  p.text(kBarX + kBarW / 2 - 9, kScaleY, "-24", Color::TextDim, Face::Micro);
  p.text(kBarX + kBarW - 3 - 4 * face_w(Face::Micro), kScaleY, "0 dB", Color::TextDim, Face::Micro);
}

// The take's envelope, mirrored about a centre line, red while it records and green once it is done; the bars of a BARS take marked. Without a take, the hint.
void queue_envelope(Panel& p, const RecordView& v) noexcept {
  queue_box(p, kEnvBox, "ENVELOPE");
  p.rect(Layer::Rule, Rect{kEnvLeft, kEnvMid, kEnvWidth, 1}, pal::kRule);
  if (!v.take_any || v.env == nullptr) {
    // F1 refuses while the BARS readout is red (the take would not fit the buffer): then the hint says why.
    static const char* const kHintFits = "F1 ARMS A TAKE AT THE NEXT BAR . SHIFT + REC DOES IT FROM ANY SCREEN";
    static const char* const kHintLong = "THE TAKE IS TOO LONG FOR THE BUFFER . FEWER BARS, OR A FASTER TEMPO";
    const char* const kHint = v.fits ? kHintFits : kHintLong;
    p.text(kEnvLeft + (kEnvWidth - text_len(kHint) * face_w(Face::Micro)) / 2, kEnvMid - 20, kHint, Color::TextDim, Face::Micro);
    return;
  }
  for (unsigned b = 2; b <= v.env_bars; ++b) {
    p.rect(Layer::Rule, Rect{envelope_mark_x(b, v.env_bars), kEnvMarkTop, 1, kEnvMarkH}, pal::kRule);
  }
  const unsigned reached = envelope_reached(v.scanned, v.env->span());
  const ui::Rgb ink = theme().rgb(v.recording ? Color::Rec : Color::Play);
  for (unsigned c = 0; c < kEnvelopeCols && c <= reached; ++c) {
    const unsigned norm = meter_norm(static_cast<float>(envelope_column(*v.env, c)) / 32768.0f);
    p.rect(Layer::Wave, envelope_column_box(c, norm), ink);
  }
}

}  // namespace

Rect record_bar_window(unsigned group, unsigned c) noexcept { return Rect{kBarX, kBarY[group & 1u][c & 1u], kBarW, kBarH}; }

unsigned envelope_column(const Envelope& env, unsigned c) noexcept {
  const unsigned a = env.peak(2u * c);
  const unsigned b = env.peak(2u * c + 1u);
  return a > b ? a : b;
}

unsigned envelope_reached(std::uint32_t scanned, std::uint32_t span) noexcept {
  const std::uint64_t cols96 = span != 0u ? static_cast<std::uint64_t>(scanned) * Envelope::kCols / span : 0u;  // columns of 96 the take has filled (the last one partly)
  return static_cast<unsigned>(cols96 / 2u);
}

Rect envelope_column_box(unsigned c, unsigned norm) noexcept {
  const int x0 = kEnvLeft + static_cast<int>(static_cast<unsigned>(kEnvWidth) * c / kEnvelopeCols);
  const int x1 = kEnvLeft + static_cast<int>(static_cast<unsigned>(kEnvWidth) * (c + 1u) / kEnvelopeCols);
  int h = static_cast<int>(static_cast<std::uint64_t>(norm > kMeterMax ? kMeterMax : norm) * static_cast<unsigned>(kEnvHalf) / kMeterMax);
  h = h < 1 ? 1 : h;
  const int w = x1 - x0;
  return Rect{x0, kEnvMid - h, w > 2 ? w - 1 : w, 2 * h};
}

int envelope_mark_x(unsigned bar, unsigned bars) noexcept {
  return kEnvLeft + static_cast<int>(static_cast<unsigned>(kEnvWidth) * (bar - 1u) / (bars != 0u ? bars : 1u));
}

void draw_record(ui::Renderer& r, const RecordView& v, const frame::StatusView& status, const HeaderInputs& in, const char* pads_line) noexcept {
  Panel& panel = skin::panel();
  panel.clear(in.covered);
  queue_panel(panel, kHeaderH, kGlassBottom);

  HeaderView header;
  fill_header(header, status, in);
  draw_header(panel, header);
  FRowView frow;
  fill_frow(frow, v.knob, false);
  frow.slot[0].lit = std::strcmp(v.state, "REC") == 0;  // ARM / STOP is lit red while the take records
  const bool done = std::strcmp(v.state, "DONE") == 0;   // SAVE and DISCARD have something to do only on a finished take (not while it saves: both do nothing then)
  frow.slot[5].dim = frow.slot[5].kind == SlotKind::Action && !done;
  frow.slot[6].dim = frow.slot[6].kind == SlotKind::Action && !done;
  draw_frow(panel, frow);

  queue_take(panel, v);
  queue_levels(panel, v);
  queue_envelope(panel, v);

  ui::plot::Canvas canvas(r);
  canvas.open();
  panel.flush_shapes(canvas);
  canvas.close();
  panel.flush_text(r);

  frame::draw_pads_line(r, Rect{6, kPadsY, ui::kScreenW - 12, frame::kPadsLineH}, pads_line);
  draw_ballast(r, ballast());  // `skin ballast N` (SU1): after everything, so the list is that much longer
}

}  // namespace o3_303::skin
