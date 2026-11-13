// SPDX-License-Identifier: GPL-3.0-or-later
#include "header.h"

#include "seg.h"

#include "../step_view.h"

#include <o3_303/step_model.h>

#include <cstring>

namespace o3_303::skin {

namespace {

using ui::Color;
using ui::Face;
using ui::Rect;
using skin_assets::Id;

constexpr int kMicroW = 6;  // the Micro face (Spleen 6x12): a character's advance, for centring and rules

// The header's grid (800 x 66). The rows of LED centres are shared by TRANSPORT and KEYBOARD.
constexpr int kTitleY = 3;
constexpr int kRuleY = 9;
constexpr int kRowY[3] = {27, 41, 55};

// The MODS chips (PF1): a 2 x 2 grid under the group's title, in the place the PERFORM slots had (x 498 to 612).
constexpr int kLabelW = 8;  // the Label face (8x16): a character's advance and its height
constexpr int kLabelH = 16;
constexpr int kModsX = 502;
constexpr int kModsY = 13;
constexpr int kModW = 53;
constexpr int kModH = 24;
constexpr int kModGap = 3;
static_assert(kModsX + 2 * kModW + kModGap <= 612 && kModsY + 2 * kModH + kModGap <= kHeaderH, "the MODS chips fit the PERFORM group's place");

bool starts_with(const char* s, const char* prefix) noexcept {
  while (*prefix != '\0') {
    if (*s++ != *prefix++) {
      return false;
    }
  }
  return true;
}

// The take chip's words after "TAKE ".
void parse_take(const char* take, HeaderView& v, std::uint64_t now_ms) noexcept {
  v.take = Take::None;
  v.take_lit = false;
  cut_to(v.take_text, sizeof v.take_text, "TAKE", 4);
  if (take == nullptr || take[0] == '\0') {
    return;
  }
  const char* w = starts_with(take, "TAKE ") ? take + 5 : take;
  if (starts_with(w, "ARMED")) {
    v.take = Take::Armed;
    v.take_lit = ((now_ms / 400u) & 1u) == 0u;  // blinking
    cut_to(v.take_text, sizeof v.take_text, "ARMED", 7);
  } else if (starts_with(w, "REC")) {
    v.take = Take::Recording;
    v.take_lit = true;
    const char* t = w + 3;
    while (*t == ' ') {
      ++t;
    }
    cut_to(v.take_text, sizeof v.take_text, t[0] != '\0' ? t : "REC", 7);
  } else {
    v.take = starts_with(w, "SAVING") ? Take::Saving : Take::Ready;
    v.take_lit = true;
    cut_to(v.take_text, sizeof v.take_text, w, 7);
  }
}

// ---- queueing helpers ---------------------------------------------------------------------------------------------------------

// A group's silkscreen title at x and, when it leaves room, a rule from its end to x_end.
void group(Panel& p, const char* title, int x, int x_end) noexcept {
  p.text(x, kTitleY, title, Color::ChromeDim, Face::Micro);
  const int x0 = x + static_cast<int>(std::strlen(title)) * kMicroW + 6;
  if (x_end - x0 > 4) {
    p.rect(Layer::Rule, Rect{x0, kRuleY, x_end - x0, 1}, pal::kRule);
  }
}

// One of the PAGE / ENC windows: the tag in the silkscreen, the value in 8x16.
void value_window(Panel& p, int y, const char* tag, const char* value, Color ink) noexcept {
  window(p, Rect{620, y, 172, 22}, 3);
  p.text(626, y + 5, tag, Color::ChromeDim, Face::Micro);
  p.text(656, y + 3, value, ink, Face::Label);
}

}  // namespace

void seg_display(Panel& p, const Rect& win, const SegRun& run) noexcept {
  constexpr int kPitch = 22;
  const skin_assets::Info& cell = skin_assets::info(Id::Seg);
  window(p, win, 4);
  const int n = static_cast<int>(run.n);
  const int gx = win.x + win.w - 6 - (n - 1) * kPitch - cell.cell_w + 1;
  const int gy = win.y + (win.h - cell.cell_h) / 2;
  for (int i = 0; i < n; ++i) {
    p.sprite(Layer::Seg, Id::Seg, run.cell[i], gx + i * kPitch, gy, pal::kSeg);
  }
  if (run.dot >= 0) {
    p.point(Layer::SegDot, (gx + run.dot * kPitch) * 16 + skin_assets::kSegDotX_q4, gy * 16 + skin_assets::kSegDotY_q4, 2 * 16, pal::kSeg);
  }
}

const char* tempo_display(std::uint32_t mbpm, char (&out)[6]) noexcept {
  std::uint32_t tenths = static_cast<std::uint32_t>((static_cast<std::uint64_t>(mbpm) + 50u) / 100u);
  tenths = tenths > 9999u ? 9999u : tenths;
  const std::uint32_t whole = tenths / 10u;  // at most 999: three cells, then the point and the tenth
  out[0] = whole >= 100u ? static_cast<char>('0' + whole / 100u) : ' ';
  out[1] = whole >= 10u ? static_cast<char>('0' + whole / 10u % 10u) : ' ';
  out[2] = static_cast<char>('0' + whole % 10u);
  out[3] = '.';
  out[4] = static_cast<char>('0' + tenths % 10u);
  out[5] = '\0';
  return out;
}

ui::Face message_face(unsigned length) noexcept { return length <= kMessageLabelMax ? Face::Label : Face::Micro; }

void cut_message(char* out, unsigned cap, const char* s) noexcept {
  const unsigned n = static_cast<unsigned>(std::strlen(s));
  if (n <= kMessageMicroMax) {
    cut_to(out, cap, s, kMessageMicroMax);
    return;
  }
  cut_to(out, cap, s, kMessageMicroMax - 1u);
  const unsigned at = static_cast<unsigned>(std::strlen(out));
  if (at + 1u < cap) {
    out[at] = '.';
    out[at + 1u] = '\0';
  }
}

void enc_with_value(char* out, unsigned cap, const char* label, const char* value, const char* unit) noexcept {
  if (cap == 0u) {
    return;
  }
  constexpr unsigned kWindow = 17;  // what fill_header keeps of the ENC text
  const unsigned window = cap - 1u < kWindow ? cap - 1u : kWindow;
  const unsigned label_len = static_cast<unsigned>(std::strlen(label));
  const unsigned value_len = static_cast<unsigned>(std::strlen(value));
  const unsigned unit_len = static_cast<unsigned>(std::strlen(unit));
  const bool with_unit = unit_len != 0u && label_len + value_len + unit_len + 2u <= window;
  const unsigned tail = value_len + (with_unit ? unit_len + 1u : 0u);
  const unsigned room = tail + 1u < window ? window - tail - 1u : 0u;
  unsigned name = label_len < room ? label_len : room;
  while (name != 0u && label[name - 1u] == ' ') {  // a name cut at a space does not leave two before the value
    --name;
  }
  unsigned n = 0;
  const auto put = [&](const char* s, unsigned len) noexcept {
    for (unsigned i = 0; i < len && n < window; ++i) {
      out[n++] = s[i];
    }
  };
  put(label, name);
  if (name != 0u) {
    put(" ", 1u);
  }
  put(value, value_len);
  if (with_unit) {
    put(" ", 1u);
    put(unit, unit_len);
  }
  out[n] = '\0';
}

void fill_header(HeaderView& out, const frame::StatusView& status, const HeaderInputs& in) noexcept {
  out = HeaderView{};
  pattern_name(in.pattern, out.pattern, sizeof out.pattern);
  (void)tempo_display(in.tempo_mbpm, out.tempo);
  out.playing = in.playing;
  out.song = status.song;
  out.rec = status.rec;
  parse_take(status.take, out, in.now_ms);
  if (status.keys) {
    out.trnsp = status.trnsp;
    out.keys = !status.trnsp;
    frame::octave_text(status.octave, out.octave, sizeof out.octave);
  }
  cut_to(out.page, sizeof out.page, in.title, 17);
  cut_to(out.enc, sizeof out.enc, status.enc, 17);
  // One message at a time in the PAGE window: the screen's toast, else what the take said. Fitted to the window by cut_message().
  if (status.toast != nullptr && status.toast[0] != '\0') {
    cut_message(out.message, sizeof out.message, status.toast);
    out.message_off = status.toast_off;
  } else if (status.take_msg != nullptr && status.take_msg[0] != '\0') {
    cut_message(out.message, sizeof out.message, status.take_msg);
    out.message_off = status.take_msg_off;
  }
  out.save_failed = status.save_failed;
  out.no_autosave = status.no_autosave;
  out.sounds = status.sounds;
}

void draw_header(Panel& p, const HeaderView& v) noexcept {
  // The brand. A failed save (red) is the tagline; autosave off tints the logo in Accent.
  p.sprite(Layer::Logo, Id::Logo, 0, 12, 4, v.no_autosave ? theme().rgb(Color::Accent) : pal::kLogo);
  if (v.save_failed) {
    p.text(14, 52, "SAVE FAILED", Color::Rec, Face::Micro);
  }

  // PATTERN and TEMPO.
  group(p, "PATTERN", 168, 248);
  group(p, "TEMPO", 256, 330);
  p.text(358 - 3 * kMicroW, kTitleY, "BPM", Color::ChromeDim, Face::Micro);
  const char name[4] = {v.pattern[0], v.pattern[1], v.pattern[2], '\0'};
  seg_display(p, Rect{168, 16, 80, 41}, seg_text(name));
  seg_display(p, Rect{256, 16, 102, 41}, seg_text(v.tempo));

  // TRANSPORT: PLAY, REC, and the take.
  group(p, "TRANSPORT", 368, 424);
  led(p, 376, kRowY[0], v.playing, pal::kLedGreen);
  p.text(386, kRowY[0] - 6, v.song ? "SONG" : "PLAY", v.song ? Color::Fx : (v.playing ? Color::ChromeText : Color::ChromeDim), Face::Micro);  // the same four characters: no new primitive
  led(p, 376, kRowY[1], v.rec, pal::kLedRed);
  p.text(386, kRowY[1] - 6, "REC", v.rec ? Color::ChromeText : Color::ChromeDim, Face::Micro);
  const bool recording = v.take == Take::Recording;
  led(p, 376, kRowY[2], v.take_lit, recording ? pal::kLedRed : pal::kLedAmber);
  p.text(386, kRowY[2] - 6, v.take_text, v.take == Take::None ? Color::ChromeDim : (recording ? Color::Rec : Color::Accent), Face::Micro);

  // KEYBOARD: KEYS or TRNSP, and the octave.
  group(p, "KEYBOARD", 432, 490);
  led(p, 442, kRowY[0], v.keys, pal::kLedRed);
  p.text(452, kRowY[0] - 6, "KEYS", v.keys ? Color::ChromeText : Color::ChromeDim, Face::Micro);
  led(p, 442, kRowY[1], v.trnsp, pal::kLedRed);
  p.text(452, kRowY[1] - 6, "TRNSP", v.trnsp ? Color::ChromeText : Color::ChromeDim, Face::Micro);
  if (v.octave[0] != '\0') {
    p.text(436, kRowY[2] - 6, v.octave, Color::ChromeDim, Face::Micro);
  }

  // MODS (PF1): the four sounds MOD GRL TRC WOB, where PERFORM's slots were. A sound that is on is a chip filled in its key's colour with dark ink, one that is off an outline in the dim chrome
  // colour with dim ink, so the state reads at a glance. A 2 x 2 grid in the 8x16 Label face (the colour keys' order: Note, Instr over Vol, Fx).
  group(p, "MODS", 498, 612);
  for (unsigned i = 0; i < frame::kSoundChips; ++i) {
    const Rect box{kModsX + static_cast<int>(i % 2u) * (kModW + kModGap), kModsY + static_cast<int>(i / 2u) * (kModH + kModGap), kModW, kModH};
    const bool on = (v.sounds >> i & 1u) != 0u;
    const char* word = frame::sound_chip_text(i);
    if (on) {
      p.round_rect(Layer::Fill, box, 3 * 16, theme().rgb(frame::sound_chip_colour(i)));
    } else {
      const ui::Rgb dim = theme().rgb(Color::ChromeDim);
      p.rect(Layer::Outline, Rect{box.x, box.y, box.w, 1}, dim);
      p.rect(Layer::Outline, Rect{box.x, box.y + box.h - 1, box.w, 1}, dim);
      p.rect(Layer::Outline, Rect{box.x, box.y, 1, box.h}, dim);
      p.rect(Layer::Outline, Rect{box.x + box.w - 1, box.y, 1, box.h}, dim);
    }
    p.text(box.x + (box.w - 3 * kLabelW) / 2, box.y + (box.h - kLabelH) / 2, word, on ? Color::Chrome : Color::ChromeDim, Face::Label);
  }

  // PAGE and ENC. A message takes the PAGE window for its life: amber, or outlined in red when it says something did not happen.
  if (v.message[0] != '\0') {
    window(p, Rect{620, 6, 172, 22}, 3);
    if (v.message_off) {
      const ui::Rgb red = theme().rgb(Color::Rec);
      p.rect(Layer::Outline, Rect{620, 6, 172, 1}, red);
      p.rect(Layer::Outline, Rect{620, 27, 172, 1}, red);
      p.rect(Layer::Outline, Rect{620, 6, 1, 22}, red);
      p.rect(Layer::Outline, Rect{791, 6, 1, 22}, red);
    }
    const Face face = message_face(static_cast<unsigned>(std::strlen(v.message)));
    p.text(626, face == Face::Label ? 9 : 11, v.message, v.message_off ? Color::Rec : Color::Accent, face);
  } else {
    value_window(p, 6, "PAGE", v.page, Color::Text);
  }
  value_window(p, 36, "ENC", v.enc, Color::Instr);
}

}  // namespace o3_303::skin
