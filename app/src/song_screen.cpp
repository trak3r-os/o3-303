// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_screen.h"

#include "frame.h"
#include "help_texts.h"
#include "knob_strip.h"
#include "live_actions.h"
#include "skin/header.h"
#include "skin/panel.h"
#include "skin/skin.h"
#include "skin/song.h"
#include "step_view.h"

#include <o3_303/step_model.h>
#include <o3sdk/app/context.h>
#include <o3sdk/app/widgets.h>
#include <o3sdk/hal/cycles.h>
#include <o3sdk/ui/components.h>

#include <cstring>

namespace o3_303 {

namespace {

using namespace o3sdk;
using app::Button;
using app::Input;
using ui::Color;
using ui::Face;
using ui::Rect;

constexpr std::uint64_t kToastMs = 2500;        // how long a message stays on the status line (as the other screens')
constexpr std::uint64_t kClearAskMs = 5000;     // CLEAR SONG asks for a second press within this (UX-303 2.4)
constexpr unsigned kPadPatterns = kPatternCount;  // every pad is a pattern: pad n is pattern n
static_assert(kPatternCount <= app::kPadCount, "the 48 pads are the 48 patterns");

// ---- Text -----------------------------------------------------------------------------------------------------------------------------

void append(char* out, unsigned cap, const char* s) noexcept {
  unsigned n = 0;
  while (n < cap && out[n] != '\0') {
    ++n;
  }
  for (; s != nullptr && *s != '\0' && n + 1u < cap; ++s) {
    out[n++] = *s;
  }
  if (n < cap) {
    out[n] = '\0';
  }
}

void set(char* out, unsigned cap, const char* s) noexcept {
  if (cap != 0u) {
    out[0] = '\0';
    append(out, cap, s);
  }
}

// Decimal digits of `v` appended to `out`.
void append_uint(char* out, unsigned cap, std::uint32_t v) noexcept {
  char digits[12];
  unsigned n = 0;
  do {
    digits[n++] = static_cast<char>('0' + v % 10u);
    v /= 10u;
  } while (v != 0u && n < sizeof digits);
  char text[12];
  for (unsigned i = 0; i < n; ++i) {
    text[i] = digits[n - 1u - i];
  }
  text[n] = '\0';
  append(out, cap, text);
}

// The colour of each column's button: Note teal, Instr amber, Vol purple, Fx cyan (the drawer rows' colours, UX-303 2.3).
constexpr Color kColumnColour[kSongColumns] = {Color::Vol, Color::Accent, Color::Fx, Color::Instr};
constexpr const char* kColumnName[kSongColumns] = {"PATTERN", "REPEAT", "TRANSP", "MUTE"};

}  // namespace

unsigned song_pattern_text(unsigned pattern, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  if (cap >= 4u) {
    pattern_name(pattern < kPatternCount ? pattern : kPatternCount - 1u, out, cap);
  }
  return static_cast<unsigned>(std::strlen(out));
}

unsigned song_repeat_text(unsigned repeat, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  append(out, cap, "x");
  append_uint(out, cap, repeat);
  return static_cast<unsigned>(std::strlen(out));
}

unsigned song_transpose_text(int semitones, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  append(out, cap, semitones < 0 ? "-" : "+");
  append_uint(out, cap, static_cast<std::uint32_t>(semitones < 0 ? -semitones : semitones));
  return static_cast<unsigned>(std::strlen(out));
}

std::uint32_t song_steps(const seq::Song& song, const std::uint8_t* pattern_steps) noexcept {
  std::uint32_t steps = 0;
  const unsigned n = song.length < seq::kSongRows ? song.length : seq::kSongRows;
  for (unsigned i = 0; i < n; ++i) {
    const seq::SongRow& r = song.rows[i];
    if (seq::row_muted(r)) {
      continue;
    }
    unsigned len = pattern_steps != nullptr && r.pattern < kPatternCount ? pattern_steps[r.pattern] : 0u;
    len = len == 0u ? kDefaultLength : len;
    steps += len * (r.repeat != 0u ? r.repeat : 1u);
  }
  return steps;
}

unsigned song_duration_text(std::uint32_t steps, std::uint32_t tempo_mbpm, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  // Bars of 16 sixteenths: whole when they are, else to a tenth.
  const std::uint32_t tenths = (steps * 10u + 8u) / 16u;
  append_uint(out, cap, tenths / 10u);
  if (steps % 16u != 0u) {
    const char frac[3] = {'.', static_cast<char>('0' + tenths % 10u), '\0'};
    append(out, cap, frac);
  }
  append(out, cap, steps == 16u ? " BAR" : " BARS");
  if (steps != 0u && tempo_mbpm != 0u) {
    // A sixteenth lasts 15 / BPM seconds.
    const std::uint32_t secs = static_cast<std::uint32_t>((static_cast<std::uint64_t>(steps) * 15'000u + tempo_mbpm / 2u) / tempo_mbpm);
    append(out, cap, " . ");
    append_uint(out, cap, secs / 60u);
    append(out, cap, secs % 60u < 10u ? ":0" : ":");
    append_uint(out, cap, secs % 60u);
  }
  return static_cast<unsigned>(std::strlen(out));
}

void fill_song_view(SongView& out, const SongInputs& in) noexcept {
  out = SongView{};
  seq::Song none;  // no song given: an empty one
  seq::clear_song(none);
  const seq::Song& song = in.song != nullptr ? *in.song : none;
  const unsigned length = song.length < seq::kSongRows ? song.length : seq::kSongRows;
  const unsigned lines = length + (length < seq::kSongRows ? 1u : 0u);  // the END line, unless the song is full
  const unsigned last = lines != 0u ? lines - 1u : 0u;
  const unsigned cursor = in.cursor < last ? in.cursor : last;

  out.length = length;
  out.cursor = cursor;
  out.column = in.column < kSongColumns ? in.column : 0u;
  out.empty = length == 0u;
  out.plays_song = in.plays_song;
  out.loop_on = in.loop;
  out.playing = in.transport;
  out.song_playing = in.transport && in.song_active;
  out.shift = in.shift;
  out.page = cursor / kSongPageRows;
  out.pages = lines != 0u ? (lines + kSongPageRows - 1u) / kSongPageRows : 1u;

  // The list's page.
  const unsigned first = out.page * kSongPageRows;
  for (unsigned i = 0; i < kSongPageRows && first + i < lines; ++i) {
    SongLine& l = out.line[out.lines++];
    const unsigned row = first + i;
    l.row = row;
    l.cursor = row == cursor;
    char n[4] = "";
    append_uint(n, sizeof n, row + 1u);
    if (row >= length) {  // the END line
      l.end = true;
      set(l.number, sizeof l.number, "+");
      continue;
    }
    const seq::SongRow& r = song.rows[row];
    set(l.number, sizeof l.number, n);
    (void)song_pattern_text(r.pattern, l.pattern, sizeof l.pattern);
    (void)song_repeat_text(r.repeat, l.repeat, sizeof l.repeat);
    (void)song_transpose_text(r.transpose, l.transpose, sizeof l.transpose);
    l.muted = seq::row_muted(r);
    l.playing = out.song_playing && in.playing_row == static_cast<int>(row);
    if (l.playing && r.repeat > 1u) {  // the play of its repeats now: "2/4"
      l.repeat[0] = '\0';
      append_uint(l.repeat, sizeof l.repeat, in.playing_repeat + 1u);
      append(l.repeat, sizeof l.repeat, "/");
      append_uint(l.repeat, sizeof l.repeat, r.repeat);
    }
  }

  // The readouts.
  if (length == 0u) {
    set(out.song, sizeof out.song, "EMPTY");
  } else {
    append_uint(out.song, sizeof out.song, length);
    append(out.song, sizeof out.song, length == 1u ? " ROW" : " ROWS");
  }
  (void)song_duration_text(song_steps(song, in.pattern_steps), in.tempo_mbpm, out.duration, sizeof out.duration);
  set(out.loop, sizeof out.loop, in.loop ? "ON" : "OFF");
  set(out.plays, sizeof out.plays, in.plays_song ? "SONG" : "PATTERN");
  if (out.song_playing && in.playing_row >= 0) {
    set(out.now, sizeof out.now, "ROW ");
    append_uint(out.now, sizeof out.now, static_cast<std::uint32_t>(in.playing_row) + 1u);
    const seq::SongRow& r = song.rows[static_cast<unsigned>(in.playing_row) < seq::kSongRows ? static_cast<unsigned>(in.playing_row) : 0u];
    if (r.repeat > 1u) {
      append(out.now, sizeof out.now, " . PLAY ");
      append_uint(out.now, sizeof out.now, in.playing_repeat + 1u);
      append(out.now, sizeof out.now, " OF ");
      append_uint(out.now, sizeof out.now, r.repeat);
    }
  }
  set(out.enc, sizeof out.enc, cursor < length ? kColumnName[out.column] : "NO CELL");

  // The F row. The Shift row has the two undo keys of HOME's.
  const auto key = [&](unsigned k, const char* label, const char* hint, const char* value, bool dim = false, bool hot = false) {
    SongKey& s = out.key[k];
    s.label = label;
    set(s.hint, sizeof s.hint, hint);
    set(s.value, sizeof s.value, value);
    s.dim = dim;
    s.hot = hot;
  };
  if (in.shift) {
    key(6, "REDO", "step fwd", "FWD", !in.can_redo);
    key(7, "UNDO", "step back", "BACK", !in.can_undo);
    return;
  }
  key(0, "PLAYS", out.plays, out.plays);
  key(1, "LOOP", out.loop, out.loop);
  key(2, "CHAIN", "to song", "TO SONG", in.chain_length == 0u);
  key(6, "CLEAR", in.clear_armed ? "again?" : "tap twice", in.clear_armed ? "SURE?" : "TWICE", length == 0u, in.clear_armed);
  key(7, "UNDO", "step back", "BACK", !in.can_undo);
}

// ---- The list, both drawings --------------------------------------------------------------------------------------------------

void draw_song_list(ui::Renderer& r, const Rect& area, const SongView& v, int row_h) noexcept {
  const ui::Theme& theme = r.theme();
  // Columns by share of the width: #, PATTERN, REPEAT, TRANSP, MUTE.
  const int w = area.w;
  const int col_w[5] = {w * 11 / 100, w * 26 / 100, w * 23 / 100, w * 23 / 100, w - w * 11 / 100 - w * 26 / 100 - w * 23 / 100 * 2};
  int col_x[5];
  int x = area.x;
  for (int c = 0; c < 5; ++c) {
    col_x[c] = x;
    x += col_w[c];
  }
  constexpr int kHeadH = 20;
  constexpr int kHeadGap = 4;
  const auto centred = [&](int c, int y, int h, const char* s, Color ink, Face face) {
    r.text(col_x[c] + (col_w[c] - r.text_width(s, face)) / 2, ui::caps_y(theme, face, y, h), s, ink, face);
  };

  // The column chips, in the colour of the button that picks the column; the picked one is filled.
  r.text(col_x[0] + (col_w[0] - r.text_width("ROW", Face::Label)) / 2, ui::caps_y(theme, Face::Label, area.y, kHeadH), "ROW", Color::TextDim, Face::Label);
  for (unsigned c = 0; c < kSongColumns; ++c) {
    const Rect chip{col_x[c + 1] + 3, area.y, col_w[c + 1] - 6, kHeadH};
    const bool picked = c == v.column && v.cursor < v.length;
    if (picked) {
      r.panel(chip, kColumnColour[c]);
    } else {
      r.frame(chip, kColumnColour[c]);
    }
    centred(static_cast<int>(c) + 1, area.y, kHeadH, kColumnName[c], picked ? Color::AccentText : kColumnColour[c], Face::Label);
  }

  const int top = area.y + kHeadH + kHeadGap;
  // The cursor row's band and the playing row's bar, under the text.
  for (unsigned i = 0; i < v.lines; ++i) {
    const SongLine& l = v.line[i];
    const Rect band{area.x, top + static_cast<int>(i) * row_h, area.w, row_h - 2};
    if (l.cursor) {
      view::translucent(r, 110, [&] { r.panel(band, Color::Sel); });
      r.frame(band, Color::Sel);
    }
    if (l.playing) {
      r.panel(Rect{band.x, band.y, 5, band.h}, Color::Play);
    }
  }
  for (unsigned i = 0; i < v.lines; ++i) {
    const SongLine& l = v.line[i];
    const int y = top + static_cast<int>(i) * row_h;
    const int h = row_h - 2;
    if (l.end) {
      centred(0, y, h, "+", l.cursor ? Color::Text : Color::TextDim, Face::Data);
      r.text(col_x[1] + 12, ui::caps_y(theme, Face::Label, y, h), v.empty ? "(END)  TAP A PAD TO ADD A ROW" : "(END)  TAP A PAD TO ADD ANOTHER", l.cursor ? Color::Text : Color::TextDim, Face::Label);
      continue;
    }
    const Color base = l.muted ? Color::TextDim : Color::Text;
    centred(0, y, h, l.number, l.playing ? Color::Play : Color::TextDim, Face::Data);
    const char* cell[kSongColumns] = {l.pattern, l.repeat, l.transpose, l.muted ? "MUTED" : "-"};
    for (unsigned c = 0; c < kSongColumns; ++c) {
      const bool picked = l.cursor && c == v.column;
      if (picked) {  // the cell the encoder edits
        r.panel(Rect{col_x[c + 1] + 3, y, col_w[c + 1] - 6, h}, kColumnColour[c]);
      }
      Color ink = picked ? Color::AccentText : base;
      if (!picked && c == 3u) {
        ink = l.muted ? Color::Rec : Color::Empty;
      }
      centred(static_cast<int>(c) + 1, y, h, cell[c], ink, Face::Data);
    }
  }

  // What to do with an empty song, and the page.
  const int below = top + static_cast<int>(v.lines) * row_h + 4;
  if (v.empty) {
    static constexpr const char* kHow[4] = {
        "A SONG PLAYS ITS ROWS IN ORDER.",
        "EACH ROW IS A PATTERN, PLAYED REPEAT TIMES.",
        "TAP A PAD TO ADD ITS PATTERN AS A ROW, OR PRESS F3",
        "TO COPY THE CHAIN YOU MADE WITH PATTERN HELD.",
    };
    for (int i = 0; i < 4; ++i) {
      r.text(area.x + 8, below + i * 20, kHow[i], Color::TextDim, Face::Label);
    }
  } else if (v.pages > 1u) {
    char page[40] = "PAGE ";
    append_uint(page, sizeof page, v.page + 1u);
    append(page, sizeof page, " OF ");
    append_uint(page, sizeof page, v.pages);
    append(page, sizeof page, "   LEFT / RIGHT: PAGE");
    r.text(area.x + 8, below, page, Color::TextDim, Face::Micro);
  }
}

// ---- Setup and screen hooks ---------------------------------------------------------------------------------------------------

void SongScreen::init(Rig& rig) noexcept {
  rig_ = &rig;
  cursor_ = 0;
  column_ = 0;
  plays_song_ = false;
  clear_until_ms_ = 0;
  toast_[0] = '\0';
  toast_off_ = false;
  toast_until_ms_ = 0;
  seen_pos_ = 0;
  view_ = SongView{};
  // The app object outlives a run in the simulator's tests: the sequencer starts in pattern mode with the loop on, and plays the Bank's song.
  rig.seq->set_play_song(false, 0);
  rig.seq->set_song_loop(true);
  rig.seq->sync_song();
}

void SongScreen::clamp() noexcept {
  const unsigned length = rig_->bank->song().length;
  const unsigned last = length < seq::kSongRows ? length : length - 1u;
  cursor_ = cursor_ < last ? cursor_ : last;
}

void SongScreen::enter(app::Context& ctx) noexcept {
  clamp();
  clear_until_ms_ = 0;
  toast_[0] = '\0';
  apply_play_mode();
  ctx.redraw();
}

void SongScreen::leave(app::Context& ctx) noexcept {
  (void)ctx;
  clear_until_ms_ = 0;
}

bool SongScreen::clear_armed(const app::Context& ctx) const noexcept { return clear_until_ms_ != 0u && ctx.uptime_ms() < clear_until_ms_; }

void SongScreen::say(app::Context& ctx, const char* text, bool off) noexcept {
  set(toast_, sizeof toast_, text);
  toast_off_ = off;
  toast_until_ms_ = ctx.uptime_ms() + kToastMs;
  ctx.redraw();
}

// Play's mode and start row, which the sequencer reads when Play starts a run: the song from the cursor row (the first row when the cursor is on END).
void SongScreen::apply_play_mode() noexcept {
  const unsigned length = rig_->bank->song().length;
  rig_->seq->set_play_song(plays_song_, cursor_ < length ? cursor_ : 0u);
}

void SongScreen::hand_over() noexcept {
  rig_->seq->sync_song();
  apply_play_mode();
}

// A song that ended with its loop off: the sequencer is quiet and the transport runs on; the transport stops here, which is what the player expects of "play the song once".
void SongScreen::service(app::Context& ctx) noexcept {
  if (rig_->seq->song_position().done && ctx.engine().playing()) {
    (void)ctx.engine().stop();
    if (ctx.screen() == this) {
      say(ctx, "SONG ENDED");
    }
    ctx.redraw();
  }
}

// ---- Input ------------------------------------------------------------------------------------------------------------------------

void SongScreen::input(app::Context& ctx, const Input& in) noexcept {
  if (in.kind == core::InputKind::Pad) {
    if (in.pad_press() && in.pad() < kPadPatterns) {
      pad(ctx, in.pad());
    }
  } else if (in.kind == core::InputKind::Encoder) {
    if (in.delta() != 0) {
      turn(ctx, in.delta(), in.shift);
    }
  } else if (in.kind == core::InputKind::Button && in.type == Input::Type::Press) {
    button(ctx, in);
  }
}

void SongScreen::move_cursor(app::Context& ctx, int rows) noexcept {
  const int target = static_cast<int>(cursor_) + rows;
  cursor_ = target < 0 ? 0u : static_cast<unsigned>(target);
  clamp();
  apply_play_mode();
  ctx.redraw();
}

void SongScreen::button(app::Context& ctx, const Input& in) noexcept {
  const Button b = in.which();
  if (app::is_fkey(b)) {
    fkey(ctx, app::fkey_index(b), in.shift);
    return;
  }
  switch (b) {
    case Button::Up:
      move_cursor(ctx, -1);
      break;
    case Button::Down:
      move_cursor(ctx, 1);
      break;
    case Button::Left:
      move_cursor(ctx, -static_cast<int>(kSongPageRows));
      break;
    case Button::Right:
      move_cursor(ctx, static_cast<int>(kSongPageRows));
      break;
    case Button::Note:
      column_ = 0;
      ctx.redraw();
      break;
    case Button::Instr:
      column_ = 1;
      ctx.redraw();
      break;
    case Button::Vol:
      column_ = 2;
      ctx.redraw();
      break;
    case Button::Fx:
      column_ = 3;
      ctx.redraw();
      break;
    case Button::Insert:
      insert_row(ctx);
      break;
    case Button::Delete:
      delete_row(ctx);
      break;
    default:
      break;
  }
}

void SongScreen::fkey(app::Context& ctx, unsigned k, bool shift) noexcept {
  if (shift) {  // the Shift row: REDO and UNDO, as on HOME
    if (k == 6u) {
      undo(ctx, true);
    } else if (k == 7u) {
      undo(ctx, false);
    }
    return;
  }
  switch (k) {
    case 0:  // PLAYS: what Play plays
      plays_song_ = !plays_song_;
      apply_play_mode();
      say(ctx, plays_song_ ? "PLAY PLAYS THE SONG" : "PLAY PLAYS PATTERN");
      break;
    case 1:
      rig_->seq->set_song_loop(!rig_->seq->song_loop());
      say(ctx, rig_->seq->song_loop() ? "SONG LOOP ON" : "LOOP OFF: PLAYS ONCE");
      break;
    case 2:
      from_chain(ctx);
      break;
    case 6:
      clear_song(ctx);
      break;
    case 7:
      undo(ctx, false);
      break;
    default:
      break;
  }
}

void SongScreen::turn(app::Context& ctx, int delta, bool shift) noexcept {
  seq::Bank& bank = *rig_->bank;
  const seq::Song& song = bank.song();
  if (cursor_ >= song.length) {
    say(ctx, "TAP A PAD TO ADD", true);
    return;
  }
  const seq::SongRow& row = song.rows[cursor_];
  bool changed = false;
  switch (column_) {
    case 0: {
      const int step = shift ? 12 : 1;
      int p = static_cast<int>(row.pattern) + delta * step;
      p = p < 0 ? 0 : (p >= static_cast<int>(kPatternCount) ? static_cast<int>(kPatternCount) - 1 : p);
      changed = bank.song_set_pattern(cursor_, static_cast<unsigned>(p));
      break;
    }
    case 1: {
      int n = static_cast<int>(row.repeat) + delta * (shift ? 4 : 1);
      n = n < 1 ? 1 : (n > static_cast<int>(seq::kSongMaxRepeat) ? static_cast<int>(seq::kSongMaxRepeat) : n);
      changed = bank.song_set_repeat(cursor_, static_cast<unsigned>(n));
      break;
    }
    case 2: {
      int t = static_cast<int>(row.transpose) + delta * (shift ? 12 : 1);
      t = t < -seq::kSongMaxTranspose ? -seq::kSongMaxTranspose : (t > seq::kSongMaxTranspose ? seq::kSongMaxTranspose : t);
      changed = bank.song_set_transpose(cursor_, t);
      break;
    }
    default:
      changed = bank.song_set_mute(cursor_, delta > 0);
      break;
  }
  if (changed) {
    hand_over();
  }
  ctx.redraw();
}

void SongScreen::pad(app::Context& ctx, unsigned pattern) noexcept {
  seq::Bank& bank = *rig_->bank;
  const unsigned length = bank.song().length;
  if (cursor_ < length) {
    if (bank.song_set_pattern(cursor_, pattern)) {
      hand_over();
    }
  } else {
    seq::SongRow r = seq::default_row();
    r.pattern = static_cast<std::uint8_t>(pattern);
    if (!bank.song_append(r, true)) {  // a run of taps is one undo step
      say(ctx, "SONG FULL: 64 ROWS", true);
      return;
    }
    cursor_ = length + 1u;  // the cursor stays on END, one line down, ready for the next pattern
    clamp();
    hand_over();
  }
  ctx.redraw();
}

void SongScreen::insert_row(app::Context& ctx) noexcept {
  seq::Bank& bank = *rig_->bank;
  const seq::Song& song = bank.song();
  const unsigned length = song.length;
  if (length >= seq::kSongRows) {
    say(ctx, "SONG FULL: 64 ROWS", true);
    return;
  }
  const unsigned at = cursor_ < length ? cursor_ : length;
  // The new row copies the one under the cursor (on END, the last one): a repeated pattern is one key press.
  seq::SongRow r = seq::default_row();
  if (cursor_ < length) {
    r = song.rows[cursor_];
  } else if (length != 0u) {
    r = song.rows[length - 1u];
  }
  if (!bank.song_insert(at, r)) {
    return;
  }
  rig_->seq->sync_song();
  if (at < length) {  // rows moved down: the sequencer's place in the song moves with them
    (void)rig_->seq->song_row_inserted(at);
  }
  cursor_ = at;
  apply_play_mode();
  ctx.redraw();
}

void SongScreen::delete_row(app::Context& ctx) noexcept {
  seq::Bank& bank = *rig_->bank;
  if (cursor_ >= bank.song().length) {
    say(ctx, "NO ROW TO DELETE", true);
    return;
  }
  const unsigned at = cursor_;
  if (!bank.song_delete(at)) {
    return;
  }
  rig_->seq->sync_song();
  (void)rig_->seq->song_row_deleted(at);
  clamp();
  apply_play_mode();
  ctx.redraw();
}

// FROM CHAIN: the song becomes the chain made with Pattern held, behind the pattern Play starts (or the one that plays, when the chain's own entry is not it): one undo step.
void SongScreen::from_chain(app::Context& ctx) noexcept {
  const seq::ChainView chain = rig_->seq->chain();
  if (chain.length == 0u) {
    say(ctx, "NO CHAIN TO COPY", true);
    return;
  }
  const seq::Playhead ph = rig_->seq->playhead();
  // The pattern that starts the chain is outside it: the selected one when stopped, the playing one while it is not itself an entry of the chain.
  int first = ph.playing ? ph.pattern : static_cast<int>(rig_->bank->selected());
  if (ph.playing && chain.position >= 0) {
    first = -1;
  }
  if (!rig_->bank->song_from_chain(first, chain.pattern, chain.length)) {
    say(ctx, "SAME AS THE CHAIN", true);
    return;
  }
  cursor_ = 0;
  hand_over();
  (void)rig_->seq->song_replaced();  // a playing song loses its place: the new rows are not the old ones
  char text[32] = "CHAIN: ";
  append_uint(text, sizeof text, rig_->bank->song().length);
  append(text, sizeof text, rig_->bank->song().length == 1u ? " ROW" : " ROWS");
  say(ctx, text);
}

void SongScreen::clear_song(app::Context& ctx) noexcept {
  seq::Bank& bank = *rig_->bank;
  if (bank.song().length == 0u) {
    clear_until_ms_ = 0;
    say(ctx, "THE SONG IS EMPTY", true);
    return;
  }
  const std::uint64_t now = ctx.uptime_ms();
  if (!clear_armed(ctx)) {  // the first press asks, the second within 5 s clears
    clear_until_ms_ = now + kClearAskMs;
    say(ctx, "PRESS F7 AGAIN", true);
    return;
  }
  clear_until_ms_ = 0;
  (void)bank.song_clear();
  cursor_ = 0;
  hand_over();
  (void)rig_->seq->song_replaced();
  say(ctx, "SONG CLEARED");
}

void SongScreen::undo(app::Context& ctx, bool redo) noexcept {
  const bool done = undo_step(*rig_, redo);
  clamp();
  apply_play_mode();
  say(ctx, done ? (redo ? "REDO" : "UNDO") : (redo ? "NOTHING TO REDO" : "NOTHING TO UNDO"), !done);
}

// ---- Update, soft keys, LEDs and HELP ---------------------------------------------------------------------------------------

void SongScreen::update(app::Context& ctx) noexcept {
  const std::uint64_t now = ctx.uptime_ms();
  if (toast_[0] != '\0' && now >= toast_until_ms_) {
    toast_[0] = '\0';
    ctx.redraw();
  }
  if (clear_until_ms_ != 0u && now >= clear_until_ms_) {  // the ask lapsed
    clear_until_ms_ = 0;
    ctx.redraw();
  }
  // The place of the song, the tempo and the transport: a change is a new picture.
  const seq::SongPosition pos = rig_->seq->song_position();
  const std::uint32_t key = (static_cast<std::uint32_t>(pos.row + 1) & 0xFFu) | (pos.repeat & 0xFFu) << 8 | (pos.active ? 1u << 16 : 0u) |
                            (rig_->seq->playhead().playing ? 1u << 17 : 0u) | ((ctx.engine().tempo() / 100u) & 0x3FFFu) << 18;
  if (key != seen_pos_) {
    seen_pos_ = key;
    ctx.redraw();
  }
}

void SongScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  // The knob row is the F row (as HOME's): the key row carries labels only for the Shift row.
  if (!ctx.panel().shift()) {
    return;
  }
  out.label[6] = "REDO";
  out.label[7] = "UNDO";
  if (!rig_->bank->can_redo()) {
    out.dim = static_cast<std::uint8_t>(out.dim | (1u << 6));
  }
  if (!rig_->bank->can_undo()) {
    out.dim = static_cast<std::uint8_t>(out.dim | (1u << 7));
  }
}

// The 48 pads are the 48 patterns, as with Pattern held: an empty one dark, one that has notes dim, one the song uses low, the cursor row's pattern mid, the one that plays on.
void SongScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  (void)ctx;
  const seq::Song& song = rig_->bank->song();
  const seq::SongPosition pos = rig_->seq->song_position();
  const bool playing = rig_->seq->playhead().playing;
  for (unsigned i = 0; i < kPatternCount; ++i) {
    leds.set(i, pattern_is_empty(rig_->bank->pattern(i)) ? app::kLedOff : app::kLedDim);
  }
  for (unsigned row = 0; row < song.length && row < seq::kSongRows; ++row) {
    const unsigned p = song.rows[row].pattern;
    if (p < kPatternCount) {
      leds.set(p, app::kLedLow);
    }
  }
  if (cursor_ < song.length) {
    leds.set(song.rows[cursor_].pattern, app::kLedMid);
  }
  if (playing && pos.active && pos.row >= 0 && static_cast<unsigned>(pos.row) < song.length) {
    leds.set(song.rows[static_cast<unsigned>(pos.row)].pattern, app::kLedOn);
  } else if (playing && !pos.active) {
    const seq::Playhead ph = rig_->seq->playhead();
    if (ph.pattern >= 0 && static_cast<unsigned>(ph.pattern) < kPatternCount) {
      leds.set(static_cast<unsigned>(ph.pattern), app::kLedOn);
    }
  }
}

void SongScreen::help(const app::Context& ctx, app::Help& h) const noexcept {
  (void)ctx;
  help_texts::fill(h, help_texts::text(help_texts::Id::Song));
  help_texts::fill_song_pads(h);
}

// ---- The view ---------------------------------------------------------------------------------------------------------------------

void SongScreen::fill_view(app::Context& ctx, SongView& view) noexcept {
  clamp();
  std::uint8_t steps[kPatternCount];
  for (unsigned i = 0; i < kPatternCount; ++i) {
    steps[i] = rig_->bank->pattern(i).length;
  }
  const seq::SongPosition pos = rig_->seq->song_position();
  SongInputs in;
  in.song = &rig_->bank->song();
  in.cursor = cursor_;
  in.column = column_;
  in.playing_row = pos.row;
  in.playing_repeat = pos.repeat;
  in.transport = rig_->seq->playhead().playing;
  in.song_active = pos.active;
  in.plays_song = plays_song_;
  in.loop = rig_->seq->song_loop();
  in.tempo_mbpm = ctx.engine().tempo();
  in.pattern_steps = steps;
  in.chain_length = rig_->seq->chain().length;
  in.can_undo = rig_->bank->can_undo();
  in.can_redo = rig_->bank->can_redo();
  in.clear_armed = clear_armed(ctx);
  in.shift = ctx.panel().shift();
  fill_song_view(view, in);
}

// The F row's eight boxes from the view's keys (classic strip and obsidian F row take the same knobs).
// `skin`: the obsidian F row shows each key's hint, the classic box its value.
static void song_knobs(const SongView& v, view::Knob (&knobs)[view::kKnobs], bool skin) noexcept {
  static constexpr const char* kFKeys[view::kKnobs] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
  for (unsigned i = 0; i < static_cast<unsigned>(view::kKnobs); ++i) {
    view::Knob& k = knobs[i];
    k = view::Knob{};
    k.fkey = kFKeys[i];
    const SongKey& s = v.key[i];
    if (s.label[0] == '\0') {
      k.blank = true;
      continue;
    }
    k.label = s.label;
    k.norm = -1.0f;  // an action key
    k.dim = s.dim;
    k.held = s.hot;  // CLEAR waiting for its second press: the amber rim
    std::strncpy(k.value, skin ? s.hint : s.value, sizeof k.value - 1u);
  }
}

app::Chrome SongScreen::chrome(const app::Context& ctx) const noexcept { return skin::active(ctx) ? app::Chrome::None : app::Chrome::Full; }

const ui::Theme* SongScreen::theme(const app::Context& ctx) const noexcept { return skin::active(ctx) ? &skin::theme() : nullptr; }

void SongScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  if (skin::active(ctx)) {
    draw_skinned(ctx, r);
  } else {
    draw_classic(ctx, r, area);
  }
}

void SongScreen::draw_skinned(app::Context& ctx, ui::Renderer& r) noexcept {
  const std::uint32_t t0 = o3sdk::hal::cycles();
  fill_view(ctx, view_);
  frame::StatusView status;
  status.keys = false;
  status.actions = rig_->live->on();
  fill_take_status(*rig_, status);
  status.song = false;  // this screen says it in its own readout (PLAYS)
  status.toast = toast_;
  status.toast_off = toast_off_;
  status.save_failed = rig_->project.failed;
  set(status.enc, sizeof status.enc, view_.enc);
  skin::HeaderInputs in;
  in.title = title();
  in.pattern = rig_->bank->selected();
  in.tempo_mbpm = ctx.engine().tempo();
  in.playing = rig_->seq->playhead().playing;
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    in.perf[b] = rig_->wiring.perf[b];
  }
  in.now_ms = ctx.uptime_ms();
  in.covered = ctx.strip_area();
  view::Knob knobs[view::kKnobs];
  song_knobs(view_, knobs, true);
  char line[96];
  set(line, sizeof line, "48 PATTERNS . TAP = SET THIS ROW");
  if (view_.cursor >= view_.length) {
    set(line, sizeof line, "48 PATTERNS . TAP = ADD A ROW WITH THAT PATTERN");
  }
  skin::draw_song(r, view_, status, in, knobs, line);
  constexpr std::uint32_t kCyclesPerUs = 180;  // the MK66's clock; the host's hal::cycles() is scaled to it
  skin::note_frame(static_cast<std::uint32_t>(r.words()), (o3sdk::hal::cycles() - t0) / kCyclesPerUs, ctx.uptime_ms());
}

// Classic: the status line, the info strip (SONG, LENGTH, LOOP, PLAYS, where it plays), the list, the PADS line and the F row (the knob strip).
void SongScreen::draw_classic(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  fill_view(ctx, view_);
  const ui::Theme& theme = r.theme();
  frame::StatusView status;
  status.keys = false;
  status.actions = rig_->live->on();
  fill_take_status(*rig_, status);
  status.song = false;  // this screen says it in its own readout (PLAYS)
  status.toast = toast_;
  status.toast_off = toast_off_;
  status.save_failed = rig_->project.failed;
  set(status.enc, sizeof status.enc, view_.enc);
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);

  // The info strip: a small caption over each value.
  constexpr int kInfoH = 34;
  const Rect info{area.x, area.y + frame::kStatusH, area.w, kInfoH};
  view::translucent(r, 72, [&] { r.panel(info, Color::Empty); });
  struct Field {
    int x;
    const char* caption;
    const char* value;
    Color ink;
  };
  const bool now = view_.now[0] != '\0';
  const Field fields[5] = {
      {area.x + 12, "SONG", view_.song, Color::Text},
      {area.x + 150, "LENGTH", view_.duration, Color::Text},
      {area.x + 400, "LOOP", view_.loop, view_.loop_on ? Color::Text : Color::Accent},
      {area.x + 480, "PLAYS", view_.plays, view_.plays_song ? Color::Fx : Color::Text},
      {area.x + 600, now ? "PLAYING" : "NOW", now ? view_.now : (view_.playing ? "PATTERN" : "STOPPED"), now ? Color::Play : Color::TextDim},
  };
  for (const Field& f : fields) {
    r.text(f.x, info.y + 3, f.caption, Color::TextDim, Face::Micro);
    r.text(f.x, ui::caps_y(theme, Face::Data, info.y + 14, 20), f.value, f.ink, Face::Data);
  }

  const Rect strip{area.x, area.y + area.h - app::kKnobStripH, area.w, app::kKnobStripH};
  const Rect list{area.x + 40, info.y + kInfoH + 8, area.w - 80, strip.y - (info.y + kInfoH + 8)};
  draw_song_list(r, list, view_, 26);

  char line[96];
  set(line, sizeof line, view_.cursor >= view_.length ? "48 PATTERNS . TAP = ADD A ROW WITH THAT PATTERN" : "48 PATTERNS . TAP = SET THIS ROW'S PATTERN");
  frame::draw_pads_line(r, Rect{strip.x, strip.y, strip.w, frame::kPadsLineH}, line);
  view::Knob knobs[view::kKnobs];
  song_knobs(view_, knobs, false);
  view::draw_knobs(r, strip, knobs);
}

}  // namespace o3_303
