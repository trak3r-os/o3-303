// SPDX-License-Identifier: GPL-3.0-or-later
// SONG (docs/UX-303.md 5.7, docs/UX-PLAN.md SG2): the pure view (fill_song_view and its text pieces: the lines, the page, the readouts, the F row), and the screen end to end in the simulator: the
// cursor and the pages, the columns and the encoder's clamps, the pads (set a row, append on END), Insert, Delete, F1 PLAYS, F2 LOOP, F3 CHAIN, F7 CLEAR's second press, F8 UNDO and the Shift row,
// the LEDs, a song that ends with its loop off (the transport stops from any screen), an edit of a song that plays (the position follows the rows), and UNDO from HOME and PERFORM reaching the
// sequencer (SG1's owed item). The pictures are song.txt's and skin_song.txt's.
#include <doctest/doctest.h>

#include "hands.h"
#include "../app/src/help_texts.h"
#include "../app/src/song_screen.h"
#include "../app/src/skin/panel.h"

#include <o3_303/app_access.h>
#include <o3_303/seq/sequencer.h>

#include <cstdint>
#include <cstring>
#include <string>

using namespace hands;
using namespace o3_303;

namespace {

std::string str(unsigned (*fn)(unsigned, char*, unsigned), unsigned v) {
  char s[16];
  (void)fn(v, s, sizeof s);
  return s;
}

std::string duration(std::uint32_t steps, std::uint32_t mbpm = 125'000) {
  char s[32];
  (void)song_duration_text(steps, mbpm, s, sizeof s);
  return s;
}

seq::Song song_of(unsigned rows) {
  seq::Song s;
  seq::clear_song(s);
  for (unsigned i = 0; i < rows; ++i) {
    seq::SongRow r = seq::default_row();
    r.pattern = static_cast<std::uint8_t>(i % kPatternCount);
    (void)seq::song_append(s, r);
  }
  return s;
}

SongInputs inputs(const seq::Song& s) {
  SongInputs in;
  in.song = &s;
  return in;
}

// SONG, opened with the Song button on the app's own hands.
struct OnSong {
  Hands t;
  SongScreen* s = nullptr;
  OnSong() {
    t.tap(Button::Song);
    t.wait(50);
    REQUIRE(std::strcmp(t.title(), "SONG") == 0);
    s = static_cast<SongScreen*>(t.h->runtime().context().screen());
    REQUIRE(bank().song().length == 0u);
  }
  const seq::Song& song() const { return bank().song(); }
  void pads(std::initializer_list<unsigned> list) {
    for (unsigned p : list) {
      t.pad(p, 10);
    }
  }
  void up(unsigned n = 1) {
    for (unsigned i = 0; i < n; ++i) {
      t.tap(Button::Up, 10);
    }
  }
  void down(unsigned n = 1) {
    for (unsigned i = 0; i < n; ++i) {
      t.tap(Button::Down, 10);
    }
  }
  void turn(int d) { t.turn(d); }
  void shift_turn(int d) {
    t.press(Button::Shift);
    t.turn(d);
    t.release(Button::Shift);
  }
  void toast_gone() { t.wait(2600); }
  seq::Sequencer& seq() { return app_sequencer(); }
  const SongView& view() { return s->last_view(); }
};

}  // namespace

// ---- The pure view ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("song view: the pieces of text") {
  CHECK(str(song_pattern_text, 0) == "A01");
  CHECK(str(song_pattern_text, 11) == "A12");
  CHECK(str(song_pattern_text, 12) == "B01");
  CHECK(str(song_pattern_text, 47) == "D12");
  CHECK(str(song_pattern_text, 99) == "D12");  // clamped
  CHECK(str(song_repeat_text, 1) == "x1");
  CHECK(str(song_repeat_text, 16) == "x16");
  char s[8];
  (void)song_transpose_text(0, s, sizeof s);
  CHECK(std::string(s) == "+0");
  (void)song_transpose_text(-5, s, sizeof s);
  CHECK(std::string(s) == "-5");
  (void)song_transpose_text(12, s, sizeof s);
  CHECK(std::string(s) == "+12");
  (void)song_transpose_text(-12, s, sizeof s);
  CHECK(std::string(s) == "-12");
  char tiny[3];
  (void)song_repeat_text(16, tiny, sizeof tiny);  // cut, terminated
  CHECK(std::strlen(tiny) < sizeof tiny);
  CHECK(song_pattern_text(0, tiny, 0) == 0u);
}

TEST_CASE("song view: the length in bars and time") {
  CHECK(duration(0) == "0 BARS");
  CHECK(duration(16) == "1 BAR . 0:02");           // 16 sixteenths at 125 BPM: 1.92 s
  CHECK(duration(64) == "4 BARS . 0:08");          // 7.68 s
  CHECK(duration(8) == "0.5 BARS . 0:01");
  CHECK(duration(24) == "1.5 BARS . 0:03");
  CHECK(duration(16 * 64, 60'000) == "64 BARS . 4:16");  // 64 bars at 60 BPM: 256 s
  CHECK(duration(16, 0) == "1 BAR");                // no tempo: no time
  CHECK(duration(65'536) == "4096 BARS . 131:04");  // the longest song: 64 rows of 64 steps x16
  char cut[6];
  (void)song_duration_text(64, 125'000, cut, sizeof cut);  // cut, terminated
  CHECK(std::strlen(cut) < sizeof cut);
}

TEST_CASE("song view: the steps of a song count the rows that play") {
  seq::Song s = song_of(3);
  s.rows[0].repeat = 4;
  s.rows[1].repeat = 2;
  s.rows[2].flags = seq::kSongRowMute;
  CHECK(song_steps(s, nullptr) == 16u * 4u + 16u * 2u);  // the muted row adds none
  std::uint8_t lens[kPatternCount] = {};
  lens[0] = 8;
  lens[1] = 64;
  CHECK(song_steps(s, lens) == 8u * 4u + 64u * 2u);       // a pattern of length 0 is the default 16
  seq::clear_song(s);
  CHECK(song_steps(s, nullptr) == 0u);
}

TEST_CASE("song view: an empty song is the END line") {
  seq::Song s;
  seq::clear_song(s);
  SongView v;
  fill_song_view(v, inputs(s));
  CHECK(v.empty);
  CHECK(v.length == 0u);
  REQUIRE(v.lines == 1u);
  CHECK(v.line[0].end);
  CHECK(v.line[0].cursor);
  CHECK(std::string(v.line[0].number) == "+");
  CHECK(std::string(v.song) == "EMPTY");
  CHECK(std::string(v.duration) == "0 BARS");
  CHECK(std::string(v.enc) == "NO CELL");
  CHECK(v.pages == 1u);
  SongInputs none;  // no song given: the same
  fill_song_view(v, none);
  CHECK(v.empty);
}

TEST_CASE("song view: the cells of a row, the cursor and the page") {
  seq::Song s = song_of(25);
  s.rows[3] = seq::SongRow{13, 4, -5, seq::kSongRowMute};
  SongInputs in = inputs(s);
  in.cursor = 3;
  in.column = 2;
  SongView v;
  fill_song_view(v, in);
  CHECK(v.length == 25u);
  CHECK(v.lines == 10u);
  CHECK(v.page == 0u);
  CHECK(v.pages == 3u);  // 25 rows and the END line: 26 lines
  CHECK(v.cursor == 3u);
  const SongLine& l = v.line[3];
  CHECK(l.cursor);
  CHECK(std::string(l.number) == "4");
  CHECK(std::string(l.pattern) == "B02");
  CHECK(std::string(l.repeat) == "x4");
  CHECK(std::string(l.transpose) == "-5");
  CHECK(l.muted);
  CHECK_FALSE(v.line[2].cursor);
  CHECK(std::string(v.enc) == "TRANSP");
  CHECK(std::string(v.song) == "25 ROWS");

  in.cursor = 12;  // the second page
  fill_song_view(v, in);
  CHECK(v.page == 1u);
  CHECK(v.line[0].row == 10u);
  CHECK(v.line[2].cursor);

  in.cursor = 40;  // past the end: the END line, the last page
  fill_song_view(v, in);
  CHECK(v.cursor == 25u);
  CHECK(v.page == 2u);
  REQUIRE(v.lines == 6u);  // rows 21..25 and END
  CHECK(v.line[5].end);
  CHECK(v.line[5].cursor);
  CHECK(std::string(v.enc) == "NO CELL");
}

TEST_CASE("song view: a full song has no END line and the cursor stays on a row") {
  seq::Song s = song_of(64);
  SongInputs in = inputs(s);
  in.cursor = 64;
  SongView v;
  fill_song_view(v, in);
  CHECK(v.cursor == 63u);
  CHECK(v.pages == 7u);
  CHECK(v.page == 6u);
  REQUIRE(v.lines == 4u);  // rows 61..64
  CHECK_FALSE(v.line[3].end);
  CHECK(v.line[3].cursor);
  CHECK(std::string(v.line[3].number) == "64");
}

TEST_CASE("song view: the playing row and its repeats") {
  seq::Song s = song_of(3);
  s.rows[1].repeat = 4;
  SongInputs in = inputs(s);
  in.playing_row = 1;
  in.playing_repeat = 1;
  in.transport = true;
  in.song_active = true;
  in.plays_song = true;
  SongView v;
  fill_song_view(v, in);
  CHECK(v.song_playing);
  CHECK(v.line[1].playing);
  CHECK(std::string(v.line[1].repeat) == "2/4");  // the play of its repeats now
  CHECK_FALSE(v.line[0].playing);
  CHECK(std::string(v.line[0].repeat) == "x1");
  CHECK(std::string(v.now) == "ROW 2 . PLAY 2 OF 4");
  CHECK(std::string(v.plays) == "SONG");

  in.playing_row = 0;  // a row played once says no count
  fill_song_view(v, in);
  CHECK(std::string(v.now) == "ROW 1");
  CHECK(std::string(v.line[0].repeat) == "x1");

  in.song_active = false;  // the transport runs a pattern: nothing plays in the list
  fill_song_view(v, in);
  CHECK_FALSE(v.song_playing);
  CHECK_FALSE(v.line[0].playing);
  CHECK(v.now[0] == '\0');

  in.song_active = true;
  in.transport = false;  // a stopped transport plays no row
  fill_song_view(v, in);
  CHECK_FALSE(v.line[0].playing);

  in.transport = true;
  in.playing_row = -1;  // the row that played was deleted: no row is the playing one
  fill_song_view(v, in);
  CHECK_FALSE(v.line[0].playing);
  CHECK_FALSE(v.line[1].playing);
}

TEST_CASE("song view: the F row") {
  seq::Song s = song_of(2);
  SongInputs in = inputs(s);
  SongView v;
  fill_song_view(v, in);
  CHECK(std::string(v.key[0].label) == "PLAYS");
  CHECK(std::string(v.key[0].value) == "PATTERN");
  CHECK(std::string(v.key[1].label) == "LOOP");
  CHECK(std::string(v.key[1].value) == "ON");
  CHECK(std::string(v.key[2].label) == "CHAIN");
  CHECK(v.key[2].dim);  // no chain to copy
  CHECK(v.key[3].label[0] == '\0');
  CHECK(v.key[5].label[0] == '\0');
  CHECK(std::string(v.key[6].label) == "CLEAR");
  CHECK_FALSE(v.key[6].dim);
  CHECK_FALSE(v.key[6].hot);
  CHECK(v.key[7].dim);  // nothing to undo

  in.plays_song = true;
  in.loop = false;
  in.chain_length = 3;
  in.can_undo = true;
  in.clear_armed = true;
  fill_song_view(v, in);
  CHECK(std::string(v.key[0].value) == "SONG");
  CHECK(std::string(v.key[1].value) == "OFF");
  CHECK_FALSE(v.key[2].dim);
  CHECK(v.key[6].hot);
  CHECK(std::string(v.key[6].value) == "SURE?");
  CHECK_FALSE(v.key[7].dim);

  seq::Song empty;
  seq::clear_song(empty);
  in.song = &empty;
  fill_song_view(v, in);
  CHECK(v.key[6].dim);  // nothing to clear

  in.song = &s;
  in.shift = true;  // the Shift row: REDO and UNDO
  in.can_redo = true;
  fill_song_view(v, in);
  CHECK(v.shift);
  CHECK(std::string(v.key[6].label) == "REDO");
  CHECK_FALSE(v.key[6].dim);
  CHECK(std::string(v.key[7].label) == "UNDO");
  CHECK(v.key[0].label[0] == '\0');
}

TEST_CASE("song view: every text fits the boxes that show it") {
  // The classic box shows the value in 8 characters and the label in 9; the skin's key names 8 and its hint 10.
  seq::Song s = song_of(2);
  for (unsigned mask = 0; mask < 64; ++mask) {
    SongInputs in = inputs(s);
    in.plays_song = (mask & 1u) != 0u;
    in.loop = (mask & 2u) != 0u;
    in.chain_length = (mask & 4u) != 0u ? 2u : 0u;
    in.clear_armed = (mask & 8u) != 0u;
    in.can_undo = (mask & 16u) != 0u;
    in.shift = (mask & 32u) != 0u;
    SongView v;
    fill_song_view(v, in);
    for (const SongKey& k : v.key) {
      CHECK(std::strlen(k.label) <= 8u);
      CHECK(std::strlen(k.hint) <= 10u);
      CHECK(std::strlen(k.value) <= 8u);
    }
  }
}

// ---- The screen ------------------------------------------------------------------------------------------------------------------------

TEST_CASE("song screen: the Song button opens it from every screen, and the pads build a song") {
  OnSong o;
  CHECK(o.view().empty);
  o.pads({0, 1, 12});  // A01 A02 B01: each appends, the cursor stays on END
  REQUIRE(o.song().length == 3u);
  CHECK(o.song().rows[0].pattern == 0u);
  CHECK(o.song().rows[1].pattern == 1u);
  CHECK(o.song().rows[2].pattern == 12u);
  CHECK(o.song().rows[2].repeat == 1u);
  CHECK(o.s->cursor() == 3u);
  o.t.wait(50);
  CHECK(o.view().length == 3u);
  CHECK(o.view().line[3].end);

  o.up();  // on a row a pad sets its pattern and the song does not grow
  o.pads({20});
  CHECK(o.song().length == 3u);
  CHECK(o.song().rows[2].pattern == 20u);
  CHECK(o.s->cursor() == 2u);

  o.t.tap(Button::Pattern);  // from HOME and back: the song and the cursor are kept
  CHECK(std::strcmp(o.t.title(), "SONG") != 0);
  o.t.tap(Button::Song);
  CHECK(std::strcmp(o.t.title(), "SONG") == 0);
  CHECK(o.s->cursor() == 2u);
  o.t.tap(Button::Song);  // again: nothing
  CHECK(std::strcmp(o.t.title(), "SONG") == 0);
  for (Button b : {Button::Performance, Button::Params, Button::Master, Button::SampleRec, Button::File}) {
    o.t.tap(b);
    CHECK(std::strcmp(o.t.title(), "SONG") != 0);
    o.t.tap(Button::Song);
    CHECK(std::strcmp(o.t.title(), "SONG") == 0);
  }
}

TEST_CASE("song screen: the cursor, the pages and the columns") {
  OnSong o;
  for (unsigned i = 0; i < 25; ++i) {
    o.pads({i % 48});
  }
  REQUIRE(o.song().length == 25u);
  CHECK(o.s->cursor() == 25u);  // END
  o.down();                     // clamps on END
  CHECK(o.s->cursor() == 25u);
  o.t.tap(Button::Left, 10);
  CHECK(o.s->cursor() == 15u);
  o.t.tap(Button::Left, 10);
  o.t.tap(Button::Left, 10);
  CHECK(o.s->cursor() == 0u);
  o.up();  // clamps on the first row
  CHECK(o.s->cursor() == 0u);
  o.t.tap(Button::Right, 10);
  CHECK(o.s->cursor() == 10u);
  o.t.tap(Button::Right, 10);
  o.t.tap(Button::Right, 10);
  CHECK(o.s->cursor() == 25u);  // clamps on END

  CHECK(o.s->column() == 0u);
  o.t.tap(Button::Instr, 10);
  CHECK(o.s->column() == 1u);
  o.t.tap(Button::Vol, 10);
  CHECK(o.s->column() == 2u);
  o.t.tap(Button::Fx, 10);
  CHECK(o.s->column() == 3u);
  o.t.tap(Button::Note, 10);
  CHECK(o.s->column() == 0u);
}

TEST_CASE("song screen: the encoder edits the cell and keeps it in range") {
  OnSong o;
  o.pads({0});
  o.up();
  REQUIRE(o.s->cursor() == 0u);

  o.turn(3);  // PATTERN
  CHECK(o.song().rows[0].pattern == 3u);
  o.turn(-9);
  CHECK(o.song().rows[0].pattern == 0u);  // clamps
  o.turn(200);
  CHECK(o.song().rows[0].pattern == 47u);
  o.shift_turn(-1);
  CHECK(o.song().rows[0].pattern == 35u);  // a row of 12 pads

  o.t.tap(Button::Instr, 10);  // REPEAT
  o.turn(3);
  CHECK(o.song().rows[0].repeat == 4u);
  o.turn(-9);
  CHECK(o.song().rows[0].repeat == 1u);
  o.turn(99);
  CHECK(o.song().rows[0].repeat == 16u);
  o.shift_turn(-1);
  CHECK(o.song().rows[0].repeat == 12u);

  o.t.tap(Button::Vol, 10);  // TRANSP
  o.turn(5);
  CHECK(o.song().rows[0].transpose == 5);
  o.turn(99);
  CHECK(o.song().rows[0].transpose == 12);
  o.turn(-99);
  CHECK(o.song().rows[0].transpose == -12);
  o.shift_turn(1);
  CHECK(o.song().rows[0].transpose == 0);

  o.t.tap(Button::Fx, 10);  // MUTE: right is muted, left is not
  o.turn(1);
  CHECK(seq::row_muted(o.song().rows[0]));
  o.turn(1);
  CHECK(seq::row_muted(o.song().rows[0]));
  o.turn(-1);
  CHECK_FALSE(seq::row_muted(o.song().rows[0]));

  o.down();  // END: the encoder edits nothing and says so
  const seq::Song before = o.song();
  o.turn(1);
  CHECK(std::memcmp(&before, &o.song(), sizeof before) == 0);
  CHECK(std::string(o.s->toast()) == "TAP A PAD TO ADD");
}

TEST_CASE("song screen: a run of turns on one cell is one undo step") {
  OnSong o;
  o.pads({0});
  o.up();
  o.t.tap(Button::Instr, 10);
  o.turn(1);
  o.turn(1);
  o.turn(1);
  CHECK(o.song().rows[0].repeat == 4u);
  o.t.tap(Button::F8, 10);  // UNDO
  CHECK(o.song().rows[0].repeat == 1u);
  CHECK(o.song().length == 1u);
  o.t.tap(Button::F8, 10);  // the pad's append
  CHECK(o.song().length == 0u);
  o.t.tap(Button::F8, 10);  // nothing more
  CHECK(std::string(o.s->toast()) == "NOTHING TO UNDO");
}

TEST_CASE("song screen: Insert copies the row, Delete removes it, and both refuse politely") {
  OnSong o;
  o.pads({0, 1, 2});
  o.up(2);  // row 2 (index 1)
  o.t.tap(Button::Instr, 10);
  o.turn(3);  // x4
  o.t.tap(Button::Insert, 10);
  REQUIRE(o.song().length == 4u);
  CHECK(o.s->cursor() == 1u);  // the cursor stays on the new row, the old one moved down
  CHECK(o.song().rows[1].pattern == 1u);
  CHECK(o.song().rows[1].repeat == 4u);  // a copy of the row under the cursor
  CHECK(o.song().rows[2].pattern == 1u);
  CHECK(o.song().rows[3].pattern == 2u);

  o.t.tap(Button::Delete, 10);
  REQUIRE(o.song().length == 3u);
  CHECK(o.song().rows[1].pattern == 1u);
  CHECK(o.s->cursor() == 1u);
  o.down(2);
  REQUIRE(o.s->cursor() == 3u);  // END
  o.t.tap(Button::Delete, 10);
  CHECK(o.song().length == 3u);
  CHECK(std::string(o.s->toast()) == "NO ROW TO DELETE");
  o.toast_gone();
  o.t.tap(Button::Insert, 10);  // on END: a copy of the last row, appended
  REQUIRE(o.song().length == 4u);
  CHECK(o.song().rows[3].pattern == 2u);
  CHECK(o.s->cursor() == 3u);

  o.t.tap(Button::Delete, 10);  // the last row: the cursor moves to END
  CHECK(o.song().length == 3u);
  CHECK(o.s->cursor() == 3u);

  // Empty: Insert on END with nothing to copy is a default row.
  OnSong e;
  e.t.tap(Button::Insert, 10);
  REQUIRE(e.song().length == 1u);
  const seq::SongRow fresh = seq::default_row();
  CHECK(std::memcmp(&e.song().rows[0], &fresh, sizeof fresh) == 0);
}

TEST_CASE("song screen: 64 rows are the most") {
  OnSong o;
  for (unsigned i = 0; i < 64; ++i) {
    o.pads({i % 48});
  }
  REQUIRE(o.song().length == 64u);
  CHECK(o.s->cursor() == 63u);  // no END line: the cursor stays on the last row
  o.t.tap(Button::Insert, 10);
  CHECK(o.song().length == 64u);
  CHECK(std::string(o.s->toast()) == "SONG FULL: 64 ROWS");
  const std::uint8_t last = o.song().rows[63].pattern;
  o.pads({5});  // on a row a pad sets its pattern even when the song is full
  CHECK(o.song().length == 64u);
  CHECK(o.song().rows[63].pattern == 5u);
  CHECK(last != 5u);
  o.t.wait(50);
  CHECK(o.view().lines == 4u);  // rows 61 to 64, no END line
  o.up(63);
  o.t.tap(Button::Delete, 10);
  CHECK(o.song().length == 63u);
}

TEST_CASE("song screen: F1 chooses what Play plays, from the cursor row") {
  OnSong o;
  o.pads({0, 1, 2});
  CHECK_FALSE(o.s->plays_song());
  CHECK_FALSE(o.seq().play_song());
  o.up(2);  // row 2 (index 1)
  o.t.tap(Button::F1, 10);
  CHECK(o.s->plays_song());
  CHECK(o.seq().play_song());
  o.t.tap(Button::Play, 10);
  o.t.wait(200);
  const seq::SongPosition pos = o.seq().song_position();
  CHECK(pos.active);
  CHECK(pos.row == 1);  // the song starts at the cursor row
  CHECK(o.seq().playhead().pattern == 1);
  o.t.tap(Button::Play, 10);  // stop
  o.t.wait(50);
  o.up();
  o.t.tap(Button::Play, 10);
  o.t.wait(200);
  CHECK(o.seq().song_position().row == 0);
  o.t.tap(Button::Play, 10);
  o.t.wait(50);

  o.t.tap(Button::F1, 10);  // back to the pattern
  CHECK_FALSE(o.s->plays_song());
  o.t.tap(Button::Play, 10);
  o.t.wait(200);
  CHECK_FALSE(o.seq().song_position().active);
  o.t.tap(Button::Play, 10);
  o.t.wait(50);

  // The cursor on END starts at the first row.
  o.t.tap(Button::F1, 10);
  o.down(5);
  o.t.tap(Button::Play, 10);
  o.t.wait(200);
  CHECK(o.seq().song_position().row == 0);
  o.t.tap(Button::Play, 10);
}

TEST_CASE("song screen: F2 LOOP, and a song that ends once stops the transport from any screen") {
  OnSong o;
  o.pads({0});
  CHECK(o.seq().song_loop());
  o.t.tap(Button::F2, 10);
  CHECK_FALSE(o.seq().song_loop());
  CHECK(std::string(o.s->toast()) == "LOOP OFF: PLAYS ONCE");
  o.t.tap(Button::F1, 10);
  o.up();
  o.t.tap(Button::Play, 10);
  o.t.wait(500);
  CHECK(o.t.h->engine().playing());
  o.t.wait(2200);  // A01 is 16 steps: 1.92 s at 125 BPM
  CHECK_FALSE(o.t.h->engine().playing());
  CHECK(std::string(o.s->toast()) == "SONG ENDED");

  // From HOME the same.
  o.t.tap(Button::Pattern);
  o.t.tap(Button::Play, 10);
  o.t.wait(500);
  CHECK(o.t.h->engine().playing());
  o.t.wait(2200);
  CHECK_FALSE(o.t.h->engine().playing());

  // And again with the loop on: it goes round.
  o.t.tap(Button::Song);
  o.t.tap(Button::F2, 10);
  CHECK(o.seq().song_loop());
  o.t.tap(Button::Play, 10);
  o.t.wait(4500);
  CHECK(o.t.h->engine().playing());
  o.t.tap(Button::Play, 10);
}

TEST_CASE("song screen: F3 copies the chain made with Pattern held") {
  OnSong o;
  o.t.tap(Button::F3, 10);  // no chain
  CHECK(o.song().length == 0u);
  CHECK(std::string(o.s->toast()) == "NO CHAIN TO COPY");
  CHECK(o.view().key[2].dim);

  o.t.tap(Button::Pattern);  // the chain, in one hold of Pattern: the pattern Play starts (5), then 6 and 7
  o.t.press(Button::Pattern);
  o.t.wait(350);
  o.t.pad(5, 10);
  o.t.pad(6, 10);
  o.t.pad(7, 10);
  o.t.release(Button::Pattern);
  o.t.wait(20);
  o.t.tap(Button::Song);
  o.t.wait(50);
  REQUIRE(o.seq().chain().length == 2u);
  CHECK_FALSE(o.view().key[2].dim);
  o.t.tap(Button::F3, 10);
  REQUIRE(o.song().length == 3u);
  CHECK(o.song().rows[0].pattern == 5u);
  CHECK(o.song().rows[1].pattern == 6u);
  CHECK(o.song().rows[2].pattern == 7u);
  CHECK(o.s->cursor() == 0u);
  CHECK(std::string(o.s->toast()) == "CHAIN: 3 ROWS");

  o.toast_gone();
  o.t.tap(Button::F3, 10);  // the song is the chain already
  CHECK(o.song().length == 3u);
  CHECK(std::string(o.s->toast()) == "SAME AS THE CHAIN");

  o.t.tap(Button::F8, 10);  // one undo step brings the empty song back
  CHECK(o.song().length == 0u);
}

TEST_CASE("song screen: F7 CLEAR wants a second press within five seconds") {
  OnSong o;
  o.t.tap(Button::F7, 10);  // nothing to clear
  CHECK(std::string(o.s->toast()) == "THE SONG IS EMPTY");
  CHECK_FALSE(o.s->clear_armed(o.t.h->runtime().context()));

  o.pads({0, 1, 2});
  o.t.tap(Button::F7, 10);
  CHECK(o.song().length == 3u);  // the first press only asks
  CHECK(o.s->clear_armed(o.t.h->runtime().context()));
  CHECK(std::string(o.s->toast()) == "PRESS F7 AGAIN");
  o.t.wait(50);
  CHECK(o.view().key[6].hot);

  o.t.wait(5200);  // the ask lapses
  CHECK_FALSE(o.s->clear_armed(o.t.h->runtime().context()));
  o.t.tap(Button::F7, 10);
  CHECK(o.song().length == 3u);
  o.t.tap(Button::F7, 10);  // the second press clears
  CHECK(o.song().length == 0u);
  CHECK(o.s->cursor() == 0u);
  CHECK(std::string(o.s->toast()) == "SONG CLEARED");

  o.t.tap(Button::F8, 10);  // one UNDO brings it all back
  CHECK(o.song().length == 3u);
  o.t.tap(Button::F8, 10);
  CHECK(o.song().length == 0u);  // the next step back is the run of three pads, one step
}

TEST_CASE("song screen: the Shift row redoes and undoes, as HOME's") {
  OnSong o;
  o.pads({0});
  o.t.tap(Button::Insert, 10);  // a second step (a run of pads would be one): the copy of the row
  CHECK(o.song().length == 2u);
  o.t.shift_tap(Button::F8);
  CHECK(o.song().length == 1u);
  CHECK(std::string(o.s->toast()) == "UNDO");
  o.t.shift_tap(Button::F7);
  CHECK(o.song().length == 2u);
  CHECK(std::string(o.s->toast()) == "REDO");
  o.toast_gone();
  o.t.shift_tap(Button::F7);
  CHECK(std::string(o.s->toast()) == "NOTHING TO REDO");

  app::Context& ctx = o.t.h->runtime().context();
  o.t.press(Button::Shift);
  app::SoftKeys keys;
  o.s->soft_keys(ctx, keys);
  CHECK(std::string(keys.label[6]) == "REDO");
  CHECK(std::string(keys.label[7]) == "UNDO");
  CHECK(keys.label[0] == nullptr);
  CHECK(keys.dim == (1u << 6));  // nothing to redo
  o.t.release(Button::Shift);
  app::SoftKeys plain;
  o.s->soft_keys(ctx, plain);
  CHECK(plain.label[6] == nullptr);
}

TEST_CASE("song screen: the pads show the patterns, the cursor's pattern and the one that plays") {
  OnSong o;
  o.pads({5, 0, 7});  // the song: A06 A01 A08; pattern 0 has the factory line, the others are empty
  o.up(2);            // the cursor on row 2 (A01)
  o.t.wait(60);
  CHECK(o.t.led(0) == app::kLedMid);   // the cursor row's pattern
  CHECK(o.t.led(5) == app::kLedLow);   // used by the song
  CHECK(o.t.led(7) == app::kLedLow);
  CHECK(o.t.led(20) == app::kLedOff);  // an empty pattern the song does not use
  o.up();
  o.t.wait(60);
  CHECK(o.t.led(0) == app::kLedLow);
  CHECK(o.t.led(5) == app::kLedMid);

  o.t.tap(Button::F1, 10);
  o.t.tap(Button::Play, 10);
  o.t.wait(300);
  CHECK(o.t.led(5) == app::kLedOn);  // the playing row's pattern: row 1 (the cursor's), A06
  o.t.tap(Button::Play, 10);
}

// ---- Editing a song that plays -------------------------------------------------------------------------------------------------------

TEST_CASE("song screen: the position follows the rows while the song plays") {
  OnSong o;
  o.pads({0, 1, 2});  // A01 A02 A03
  o.up(2);
  o.t.tap(Button::Instr, 10);
  o.turn(3);  // row 2 plays 4 times (7.7 s), so it is the one playing for the whole test
  o.t.tap(Button::F1, 10);
  o.t.tap(Button::Play, 10);
  o.t.wait(500);
  REQUIRE(o.seq().song_position().row == 1);

  o.up();  // the cursor on row 1: Insert above the playing row moves it down
  o.t.tap(Button::Insert, 10);
  o.t.wait(100);
  CHECK(o.song().length == 4u);
  CHECK(o.seq().song_position().row == 2);  // the same musical row (A02 x4)
  CHECK(o.seq().playhead().pattern == 1);

  o.t.tap(Button::Delete, 10);  // a row above it goes: it moves up again
  o.t.wait(100);
  CHECK(o.seq().song_position().row == 1);
  CHECK(o.seq().playhead().pattern == 1);

  o.down();  // delete the playing row itself: its pattern plays out and the row that moved up is next
  o.t.tap(Button::Delete, 10);
  o.t.wait(100);
  CHECK(o.song().length == 2u);
  CHECK(o.seq().song_position().row == -1);  // it belongs to no row
  CHECK(o.seq().playhead().pattern == 1);

  // The cell edits of the playing row are read at the next pass; the position stays.
  o.t.tap(Button::F8, 10);  // undo the delete: three rows again
  o.t.wait(100);
  CHECK(o.song().length == 3u);
  CHECK(o.seq().playhead().playing);

  // CLEAR under the position: the song is empty, the sequencer plays on (pattern mode) and nothing is out of range.
  o.t.tap(Button::F7, 10);
  o.t.tap(Button::F7, 10);
  o.t.wait(2500);
  CHECK(o.song().length == 0u);
  CHECK(o.t.h->engine().playing());
  const seq::SongPosition pos = o.seq().song_position();
  CHECK((pos.row < 0 || static_cast<unsigned>(pos.row) < o.song().length));
  o.t.tap(Button::Play, 10);
}

TEST_CASE("song screen: UNDO from HOME and PERFORM hands the song back to the sequencer") {
  // Two rows, the loop off: the song ends after 2 x 1.92 s. An UNDO of the second row (from another screen) must reach the sequencer, or it would still play it.
  for (unsigned from = 0; from < 2; ++from) {
    OnSong o;
    o.pads({0});
    o.t.tap(Button::Insert, 10);  // the second row, a step of its own (a run of pads would be one)
    o.t.tap(Button::F2, 10);  // the loop off
    o.t.tap(Button::F1, 10);  // Play plays the song
    o.up(2);
    CHECK(o.song().length == 2u);
    if (from == 0u) {
      o.t.tap(Button::Pattern);  // HOME's Shift + F8
      o.t.shift_tap(Button::F8);
    } else {
      o.t.tap(Button::Performance);  // PERFORM's UNDO pad (row 2, column 10)
      o.t.pad(app::pad_at(1, 10), 10);
    }
    CHECK(o.song().length == 1u);
    o.t.tap(Button::Play, 10);
    o.t.wait(500);
    CHECK(o.seq().song_position().active);
    o.t.wait(2000);  // one row is played by now
    CHECK_FALSE(o.t.h->engine().playing());  // service() stopped the transport at the end of the one row (two rows would play until 3.84 s)
  }
}

TEST_CASE("song screen: a cell of the playing row is read at the next pass") {
  OnSong o;
  o.pads({0, 1});
  o.up(2);
  o.t.tap(Button::F1, 10);
  o.t.tap(Button::Play, 10);
  o.t.wait(300);
  REQUIRE(o.seq().song_position().row == 0);
  o.t.tap(Button::Instr, 10);  // REPEAT x3: it plays 3 times now
  o.turn(2);
  o.t.wait(2300);  // A01 once (1.92 s) and into its second play
  CHECK(o.seq().song_position().row == 0);
  CHECK(o.seq().song_position().repeat == 1u);
  o.t.tap(Button::Fx, 10);  // MUTE: the row is skipped when its pattern ends
  o.turn(1);
  o.t.wait(2000);
  CHECK(o.seq().song_position().row == 1);
  o.t.tap(Button::Play, 10);
}

// ---- The drawings ------------------------------------------------------------------------------------------------------------------

TEST_CASE("song screen: song_move tells an inserted or deleted row from a replaced song") {
  const seq::Song abc = song_of(3);
  unsigned at = 99;
  seq::Song with = abc;
  REQUIRE(seq::song_insert(with, 0, seq::SongRow{20, 1, 0, 0}));
  CHECK(song_move(abc, with, at) == SongMove::Inserted);
  CHECK(at == 0u);
  CHECK(song_move(with, abc, at) == SongMove::Deleted);
  CHECK(at == 0u);
  with = abc;
  REQUIRE(seq::song_insert(with, 2, seq::SongRow{21, 3, 1, 0}));
  CHECK(song_move(abc, with, at) == SongMove::Inserted);
  CHECK(at == 2u);
  CHECK(song_move(with, abc, at) == SongMove::Deleted);
  CHECK(at == 2u);
  with = abc;
  REQUIRE(seq::song_append(with, seq::SongRow{22, 1, 0, 0}));  // at the end: the rows above did not move
  CHECK(song_move(abc, with, at) == SongMove::Inserted);
  CHECK(at == 3u);
  seq::Song twin = abc;  // a row with the same content as its neighbour: the first differing place is the same musical edit
  twin.rows[1] = twin.rows[0];
  with = twin;
  REQUIRE(seq::song_insert(with, 1, twin.rows[0]));
  CHECK(song_move(twin, with, at) == SongMove::Inserted);
  CHECK(at == 2u);  // rows 0 and 1 are equal: the insert is told after them (the same song either way)
  seq::Song cell = abc;
  cell.rows[1].repeat = 5;
  CHECK(song_move(abc, cell, at) == SongMove::None);
  CHECK(song_move(abc, abc, at) == SongMove::None);
  seq::Song empty;
  seq::clear_song(empty);
  CHECK(song_move(abc, empty, at) == SongMove::Replaced);
  CHECK(song_move(empty, abc, at) == SongMove::Replaced);
  seq::Song other = song_of(3);
  other.rows[0].pattern = 30;
  other.rows[1].pattern = 31;
  CHECK(song_move(abc, other, at) == SongMove::Replaced);  // FROM CHAIN to three other rows
  seq::Song longer = song_of(4);
  longer.rows[1].pattern = 40;
  CHECK(song_move(abc, longer, at) == SongMove::Replaced);  // one row more, but the others are not the same
}

TEST_CASE("song screen: UNDO and REDO of an Insert or a Delete move the position of a song that plays") {
  // A01 A02 A03, the second row repeated 4 times (7.7 s): it is the one playing for the whole test.
  OnSong o;
  o.pads({0, 1, 2});
  o.up(2);
  o.t.tap(Button::Instr, 10);
  o.turn(3);
  o.t.tap(Button::F1, 10);
  o.t.tap(Button::Play, 10);
  o.t.wait(500);
  REQUIRE(o.seq().song_position().row == 1);

  o.up();  // the cursor on row 0: Insert above the playing row (it follows to row 2)
  o.t.tap(Button::Insert, 10);
  o.t.wait(100);
  REQUIRE(o.song().length == 4u);
  CHECK(o.seq().song_position().row == 2);
  o.t.tap(Button::F8, 10);  // UNDO the Insert: A01 A02 A03 again, the playing row is row 1 again
  o.t.wait(100);
  REQUIRE(o.song().length == 3u);
  CHECK(o.seq().song_position().row == 1);
  CHECK(o.seq().playhead().pattern == 1);
  o.t.press(Button::Shift);  // REDO the Insert
  o.t.tap(Button::F7, 10);
  o.t.release(Button::Shift);
  o.t.wait(100);
  REQUIRE(o.song().length == 4u);
  CHECK(o.seq().song_position().row == 2);
  o.t.tap(Button::F8, 10);
  o.t.wait(100);
  REQUIRE(o.song().length == 3u);
  CHECK(o.seq().song_position().row == 1);

  // The reverse: a Delete above the playing row, undone, moves the position back down.
  o.t.tap(Button::Delete, 10);
  o.t.wait(100);
  REQUIRE(o.song().length == 2u);
  CHECK(o.seq().song_position().row == 0);
  o.t.tap(Button::F8, 10);  // UNDO the Delete: the row comes back above it
  o.t.wait(100);
  REQUIRE(o.song().length == 3u);
  CHECK(o.seq().song_position().row == 1);
  CHECK(o.seq().playhead().pattern == 1);
  o.t.press(Button::Shift);  // REDO the Delete
  o.t.tap(Button::F7, 10);
  o.t.release(Button::Shift);
  o.t.wait(100);
  REQUIRE(o.song().length == 2u);
  CHECK(o.seq().song_position().row == 0);
  o.t.tap(Button::Play, 10);
}

TEST_CASE("song screen: a song cleared or replaced under the position starts again from its first row") {
  // Loop on, the second of three rows playing; CLEAR, then three other pads: the position does not name a row of the new song, and at the end of the pass the song starts at row 0.
  OnSong o;
  o.pads({0, 1, 2});
  o.up(2);
  o.t.tap(Button::Instr, 10);
  o.turn(3);
  o.t.tap(Button::F1, 10);
  o.t.tap(Button::Play, 10);
  o.t.wait(500);
  REQUIRE(o.seq().song_position().row == 1);
  o.t.tap(Button::F7, 10);
  o.t.tap(Button::F7, 10);
  o.t.wait(100);
  REQUIRE(o.song().length == 0u);
  o.pads({10, 11, 12});
  o.t.wait(100);
  REQUIRE(o.song().length == 3u);
  CHECK(o.seq().song_position().row == -1);  // not a phantom row 2 (or 1)
  CHECK(o.seq().playhead().pattern == 1);   // A02 plays out
  o.t.wait(2500);
  CHECK(o.seq().song_position().row == 0);  // then the new song's first row
  CHECK(o.seq().playhead().pattern == 10);
  o.t.tap(Button::Play, 10);
}

TEST_CASE("song screen: a run of pads on the END line is one undo step") {
  OnSong o;
  const unsigned note = bank().pattern(0).steps[0].note;  // a pattern edit under the song's: the stack is shared
  REQUIRE(bank().set_note(0, 0, note == 61u ? 62u : 61u));
  CHECK(bank().pattern(0).steps[0].note != note);
  for (unsigned i = 0; i < 40; ++i) {  // more than the stack is deep (32)
    o.pads({i % 48});
  }
  REQUIRE(o.song().length == 40u);
  o.t.tap(Button::F8, 10);
  CHECK(o.song().length == 0u);  // the whole run in one step
  o.t.tap(Button::F8, 10);       // and the pattern's edit is still there to undo
  CHECK(bank().pattern(0).steps[0].note == note);

  // Anything in between makes a new step: two pads, a cell edit, another pad.
  o.pads({3, 4});
  o.up(2);
  o.t.tap(Button::Instr, 10);
  o.turn(2);
  o.down(2);  // END
  o.pads({5});
  REQUIRE(o.song().length == 3u);
  o.t.tap(Button::F8, 10);
  CHECK(o.song().length == 2u);
  CHECK(o.song().rows[0].repeat == 3u);
  o.t.tap(Button::F8, 10);
  CHECK(o.song().rows[0].repeat == 1u);
  o.t.tap(Button::F8, 10);
  CHECK(o.song().length == 0u);
}

TEST_CASE("song screen: the skinned frame queues everything it draws, with a full song too") {
  OnSong o;
  for (unsigned i = 0; i < 64; ++i) {
    o.pads({i % 48});
  }
  o.t.skin_on();
  app::Context& ctx = o.t.h->runtime().context();
  REQUIRE(std::strcmp(o.t.title(), "SONG") == 0);
  CHECK(o.s->chrome(ctx) == app::Chrome::None);
  CHECK(o.s->theme(ctx) != nullptr);
  o.t.tap(Button::F7, 10);  // the armed key, the toast in the header
  o.t.wait(60);
  CHECK(skin::panel().dropped() == 0u);
  CHECK(skin::panel().shapes() < skin::Panel::kShapes);
  CHECK(skin::panel().texts() < skin::Panel::kTexts);
  o.t.press(Button::Shift);
  o.t.wait(60);
  CHECK(skin::panel().dropped() == 0u);
  o.t.release(Button::Shift);
  o.t.skin_off();
  CHECK(o.s->chrome(ctx) == app::Chrome::Full);
  CHECK(o.s->theme(ctx) == nullptr);
}

TEST_CASE("song screen: HELP opens over it with its own texts") {
  OnSong o;
  o.t.press(Button::Shift);
  o.t.tap(Button::Config, 10);
  o.t.release(Button::Shift);
  o.t.wait(50);
  REQUIRE(o.t.h->runtime().help_open());
  const app::Help& h = o.t.h->runtime().help_overlay().help();
  CHECK(h.summary == help_texts::text(help_texts::Id::Song).summary);
  CHECK(std::strcmp(h.pad_label[0], "A01") == 0);
  CHECK(std::strcmp(h.pad_label[47], "D12") == 0);
  o.t.tap(Button::Config, 10);
  o.t.wait(50);
  CHECK_FALSE(o.t.h->runtime().help_open());
}
