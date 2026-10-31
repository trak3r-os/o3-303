// SPDX-License-Identifier: GPL-3.0-or-later
// The song (o3_303/seq/song.h, bank.h, sequencer.h; UX-303 5.7 and D9): the row edits, the undo stack the song shares with the patterns,
// and the sequencer playing it. The central check renders a song and the same chain made by hand, with a recording stand-in for the voice
// (every event at its frame) and with the real acid voice (the audio sample by sample), and compares them.
#include <doctest/doctest.h>

#include <o3_303/seq/bank.h>
#include <o3_303/seq/ops.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/seq/song.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3sdk/dsp/engine.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace o3_303;
using namespace o3_303::seq;
namespace dsp = o3sdk::dsp;

namespace {

constexpr std::uint32_t kStep = dsp::kTicksPerSixteenth;
constexpr std::uint32_t kTempo = 120'000;

std::uint64_t frame_of(std::uint64_t tick) {
  const std::uint64_t inc = dsp::ticks_per_sample_q32(kTempo);
  return ((tick << 32) + inc - 1u) / inc;
}

// ---- The voice stand-in: every event with its absolute frame -------------------------------------------------------------------

const o3sdk::core::ParamDesc* const kVoiceParams = [] {
  static AcidVoice voice;
  return voice.info().params;
}();

struct Rec {
  std::uint64_t frame;
  dsp::EventKind kind;
  std::uint8_t note;
  std::uint8_t velocity;
  std::uint8_t flags;
  std::uint16_t param;
  float value;
};

bool operator==(const Rec& a, const Rec& b) {
  return a.frame == b.frame && a.kind == b.kind && a.note == b.note && a.velocity == b.velocity && a.flags == b.flags && a.param == b.param &&
         a.value == b.value;
}

class Voice final : public dsp::Unit {
 public:
  Voice() : info_{"VOICE", dsp::UnitKind::Instrument, kVoiceParams, 8, nullptr, 0} {}
  const dsp::UnitInfo& info() const noexcept override { return info_; }
  void prepare(float) noexcept override {}
  void process(const dsp::ProcessArgs& a) noexcept override {
    if (a.frames == 0u) {
      return;
    }
    for (std::uint32_t i = 0; i < a.event_count; ++i) {
      const dsp::Event& e = a.events[i];
      log.push_back(Rec{at + e.frame, e.kind, e.note, e.velocity, e.flags, e.param, e.value});
    }
    for (std::uint32_t i = 0; i < a.frames; ++i) {
      a.out_l[i] = 0.0f;
      a.out_r[i] = 0.0f;
    }
    at += a.frames;
  }
  std::vector<Rec> log;
  std::uint64_t at = 0;

 private:
  dsp::UnitInfo info_;
};

// An engine, a voice (the stand-in, or the real one with `audio`), a bank and the sequencer hook.
template <typename V>
struct BasicRig {
  dsp::Engine engine;
  V voice;
  std::unique_ptr<Bank> bank;
  Sequencer seq;
  std::vector<float> out_l;  // what render() produced (the audio rig keeps it)
  std::vector<float> out_r;
  bool keep = false;

  BasicRig() : bank(std::make_unique<Bank>()), seq(init(*bank)) {}
  static Bank& init(Bank& b) {
    b.init();
    return b;
  }

  void start() {
    const dsp::UnitId id = engine.attach(voice);
    REQUIRE(engine.set_source(0, id));
    REQUIRE(seq.setup(engine, id));
    engine.start();
    engine.set_tempo(kTempo);
  }

  void run(std::uint64_t frames, unsigned block = 64) {
    float l[dsp::kMaxBlockFrames];
    float r[dsp::kMaxBlockFrames];
    while (frames > 0) {
      const auto n = static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, block));
      engine.render(nullptr, nullptr, l, r, n);
      if (keep) {
        out_l.insert(out_l.end(), l, l + n);
        out_r.insert(out_r.end(), r, r + n);
      }
      frames -= n;
    }
  }
};

using Rig = BasicRig<Voice>;

// Events before `frame`, notes and params.
std::vector<Rec> events_before(const Rig& rig, std::uint64_t frame) {
  std::vector<Rec> out;
  for (const Rec& e : rig.voice.log) {
    if (e.frame < frame) {
      out.push_back(e);
    }
  }
  return out;
}

std::vector<Rec> notes_of(const Rig& rig) {
  std::vector<Rec> out;
  for (const Rec& e : rig.voice.log) {
    if (e.kind == dsp::EventKind::NoteOn || e.kind == dsp::EventKind::NoteOff) {
      out.push_back(e);
    }
  }
  return out;
}

// A pattern of `length` steps whose notes start at `base`; an accent, a slide and a rest keep the rules in play.
Pattern make_pat(unsigned base, unsigned length) {
  Pattern p;
  clear(p);
  for (unsigned j = 0; j < kMaxSteps; ++j) {
    p.steps[j].flags = 0;
    p.steps[j].note = static_cast<std::uint8_t>(base + j % 12u);
  }
  p.steps[3].flags = kStepAccent;
  p.steps[5].flags = kStepSlide;
  p.steps[7].flags = kStepRest;
  p.length = static_cast<std::uint8_t>(length);
  return p;
}

SongRow row(unsigned pattern, unsigned repeat = 1, int transpose = 0, bool mute = false) {
  return SongRow{static_cast<std::uint8_t>(pattern), static_cast<std::uint8_t>(repeat), static_cast<std::int8_t>(transpose),
                 static_cast<std::uint8_t>(mute ? kSongRowMute : 0u)};
}

template <typename R>
void make_patterns(R& rig) {
  rig.bank->replace(0, make_pat(40, 16));
  rig.bank->replace(1, make_pat(52, 8));
  rig.bank->replace(2, make_pat(60, 16));
  rig.bank->clear_history();
}

void set_song(Rig& rig, std::initializer_list<SongRow> rows) {
  for (const SongRow& r : rows) {
    REQUIRE(rig.bank->song_append(r));
  }
  rig.seq.sync_song();
}

// ---- Song edits ----------------------------------------------------------------------------------------------------------------

Song fresh() {
  Song s;
  clear_song(s);
  return s;
}

bool same(const Song& a, const Song& b) { return std::memcmp(&a, &b, sizeof(Song)) == 0; }

}  // namespace

TEST_CASE("the song is 64 rows of one word, no padding, and the fresh song is canonical") {
  CHECK(sizeof(Song) == 260);
  const Song s = fresh();
  CHECK(s.length == 0);
  for (const SongRow& r : s.rows) {
    CHECK(r.pattern == 0);
    CHECK(r.repeat == 1);
    CHECK(r.transpose == 0);
    CHECK(r.flags == 0);
  }
  Bank* b = new Bank();
  b->init();
  CHECK(same(b->song(), s));
  delete b;
}

TEST_CASE("row edits clamp their values, refuse rows that do not exist and report a change only when there is one") {
  Song s = fresh();
  CHECK_FALSE(song_set_pattern(s, 0, 3));  // no row yet
  CHECK_FALSE(song_set_repeat(s, 0, 3));
  CHECK_FALSE(song_set_transpose(s, 0, 3));
  CHECK_FALSE(song_set_mute(s, 0, true));
  CHECK_FALSE(song_delete(s, 0));
  CHECK_FALSE(song_clear(s));
  CHECK(song_append(s, row(5, 4, -5)));
  CHECK(s.length == 1);
  CHECK(s.rows[0].pattern == 5);
  CHECK(s.rows[0].repeat == 4);
  CHECK(s.rows[0].transpose == -5);

  CHECK_FALSE(song_set_pattern(s, 0, 5));  // the same value
  CHECK(song_set_pattern(s, 0, 200));      // clamped to the last pattern
  CHECK(s.rows[0].pattern == kPatternCount - 1);
  CHECK(song_set_repeat(s, 0, 0));  // clamped to 1
  CHECK(s.rows[0].repeat == 1);
  CHECK(song_set_repeat(s, 0, 99));
  CHECK(s.rows[0].repeat == kSongMaxRepeat);
  CHECK_FALSE(song_set_repeat(s, 0, 17));  // clamps to the value it has
  CHECK(song_set_transpose(s, 0, -40));
  CHECK(s.rows[0].transpose == -kSongMaxTranspose);
  CHECK(song_set_transpose(s, 0, 40));
  CHECK(s.rows[0].transpose == kSongMaxTranspose);
  CHECK(song_set_mute(s, 0, true));
  CHECK(row_muted(s.rows[0]));
  CHECK_FALSE(song_set_mute(s, 0, true));
  CHECK(song_set_mute(s, 0, false));
  CHECK_FALSE(row_muted(s.rows[0]));

  // A row handed in with garbage is cleaned.
  Song g = fresh();
  CHECK(song_append(g, SongRow{255, 0, 100, 0xFE}));
  CHECK(g.rows[0].pattern == kPatternCount - 1);
  CHECK(g.rows[0].repeat == 1);
  CHECK(g.rows[0].transpose == kSongMaxTranspose);
  CHECK(g.rows[0].flags == 0);
}

TEST_CASE("insert goes above the row, delete closes the gap, the song holds 64 rows, and the rows past the length stay default") {
  Song s = fresh();
  for (unsigned i = 0; i < 4; ++i) {
    REQUIRE(song_append(s, row(i)));
  }
  CHECK(song_insert(s, 1, row(10)));
  REQUIRE(s.length == 5);
  const unsigned want[5] = {0, 10, 1, 2, 3};
  for (unsigned i = 0; i < 5; ++i) {
    CHECK(s.rows[i].pattern == want[i]);
  }
  CHECK(song_insert(s, 99, row(20)));  // past the end: appends
  CHECK(s.rows[5].pattern == 20);
  CHECK(song_delete(s, 0));
  CHECK(s.length == 5);
  CHECK(s.rows[0].pattern == 10);
  CHECK(s.rows[4].pattern == 20);
  CHECK(s.rows[5].pattern == 0);  // the vacated row is the default again
  CHECK(s.rows[5].repeat == 1);
  CHECK_FALSE(song_delete(s, 5));

  while (s.length < kSongRows) {
    REQUIRE(song_append(s, row(7, 3, 2, true)));
  }
  CHECK(s.length == kSongRows);
  CHECK_FALSE(song_append(s, row(1)));
  CHECK_FALSE(song_insert(s, 0, row(1)));
  CHECK(song_clear(s));
  CHECK(same(s, fresh()));
}

TEST_CASE("song_normalize makes a block read from a card canonical") {
  Song s;
  std::memset(&s, 0xFF, sizeof(s));
  song_normalize(s);
  CHECK(s.length == kSongRows);
  CHECK(s.rows[0].pattern == kPatternCount - 1);
  CHECK(s.rows[0].repeat == kSongMaxRepeat);
  CHECK(s.rows[0].transpose == -1);  // 0xFF as a signed byte: in range, kept
  CHECK(s.rows[0].flags == kSongRowMute);
  CHECK(s.reserved[0] == 0);
  Song u = fresh();
  u.length = 1;
  u.rows[0].transpose = 127;
  song_normalize(u);
  CHECK(u.rows[0].transpose == kSongMaxTranspose);
  Song t = fresh();
  t.length = 2;
  t.rows[1] = row(3, 2);
  t.rows[2] = row(9, 9, 9, true);  // beyond the length
  song_normalize(t);
  CHECK(t.rows[2].pattern == 0);
  CHECK(t.rows[2].repeat == 1);
}

TEST_CASE("FROM CHAIN: the first pattern and the chain, one play each; the old rows go; the same chain again changes nothing") {
  Song s = fresh();
  song_append(s, row(9, 5, 3, true));
  const std::uint8_t chain[3] = {4, 5, 4};
  CHECK(song_from_chain(s, 2, chain, 3));
  REQUIRE(s.length == 4);
  const unsigned want[4] = {2, 4, 5, 4};
  for (unsigned i = 0; i < 4; ++i) {
    CHECK(s.rows[i].pattern == want[i]);
    CHECK(s.rows[i].repeat == 1);
    CHECK(s.rows[i].transpose == 0);
    CHECK(s.rows[i].flags == 0);
  }
  CHECK_FALSE(song_from_chain(s, 2, chain, 3));
  CHECK(song_from_chain(s, -1, chain, 3));  // no first pattern
  CHECK(s.length == 3);
  CHECK(s.rows[0].pattern == 4);
  CHECK(song_from_chain(s, -1, nullptr, 0));  // an empty chain empties the song
  CHECK(s.length == 0);
  CHECK_FALSE(song_from_chain(s, -1, nullptr, 0));
}

// ---- Undo ----------------------------------------------------------------------------------------------------------------------

TEST_CASE("song edits are undone and redone on the stack the patterns use, in order") {
  auto bank = std::make_unique<Bank>();
  bank->init();
  bank->select(2);
  CHECK(bank->song_append(row(1)));
  const Song after_append = bank->song();
  CHECK(bank->set_note(2, 0, 50));
  CHECK(bank->song_append(row(3, 2)));
  const Song after_two = bank->song();
  CHECK(bank->song_set_mute(1, true));
  const Song after_mute = bank->song();
  const Pattern pat2 = bank->pattern(2);

  CHECK(bank->undo() == 2);  // the mute: a song step returns selected()
  CHECK(bank->undid_song());
  CHECK(same(bank->song(), after_two));
  CHECK(bank->undo() == 2);
  CHECK(bank->undid_song());
  CHECK(same(bank->song(), after_append));
  CHECK(bank->undo() == 2);  // the pattern edit, which is a pattern step (selected() is 2 as well)
  CHECK_FALSE(bank->undid_song());
  CHECK(bank->pattern(2).steps[0].note != 50);
  CHECK(bank->undo() == 2);  // the first append
  CHECK(bank->undid_song());
  CHECK(bank->song().length == 0);
  CHECK(bank->undo() == -1);
  CHECK_FALSE(bank->undid_song());

  CHECK(bank->redo() == 2);
  CHECK(bank->undid_song());
  CHECK(same(bank->song(), after_append));
  CHECK(bank->redo() == 2);
  CHECK_FALSE(bank->undid_song());
  CHECK(std::memcmp(&bank->pattern(2), &pat2, sizeof(Pattern)) == 0);
  CHECK(bank->redo() == 2);
  CHECK(same(bank->song(), after_two));
  CHECK(bank->redo() == 2);
  CHECK(same(bank->song(), after_mute));
  CHECK(bank->redo() == -1);
}

TEST_CASE("an edit that changes nothing leaves no step; a new song edit ends the redo line") {
  auto bank = std::make_unique<Bank>();
  bank->init();
  CHECK_FALSE(bank->song_clear());  // empty already
  CHECK_FALSE(bank->song_delete(0));
  CHECK_FALSE(bank->song_set_pattern(0, 1));
  CHECK_FALSE(bank->song_from_chain(-1, nullptr, 0));
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->song_append(row(1)));
  CHECK_FALSE(bank->song_set_pattern(0, 1));  // the same value
  CHECK_FALSE(bank->song_set_repeat(0, 1));
  CHECK(bank->undo() >= 0);
  CHECK_FALSE(bank->can_undo());
  CHECK(bank->can_redo());
  CHECK(bank->song_append(row(2)));
  CHECK_FALSE(bank->can_redo());
  CHECK(bank->song().rows[0].pattern == 2);
}

TEST_CASE("turns of one cell join into one step, other cells and other columns do not") {
  auto bank = std::make_unique<Bank>();
  bank->init();
  REQUIRE(bank->song_append(row(0)));
  REQUIRE(bank->song_append(row(1)));
  bank->clear_history();
  for (unsigned r = 2; r <= 9; ++r) {
    CHECK(bank->song_set_repeat(0, r));
  }
  CHECK(bank->song().rows[0].repeat == 9);
  CHECK(bank->undo() >= 0);
  CHECK(bank->song().rows[0].repeat == 1);  // eight turns, one undo
  CHECK_FALSE(bank->can_undo());

  CHECK(bank->song_set_repeat(0, 4));
  CHECK(bank->song_set_repeat(1, 4));  // another row: its own step
  CHECK(bank->song_set_transpose(1, 4));  // another column
  CHECK(bank->song_set_transpose(1, 5));
  CHECK(bank->undo() >= 0);
  CHECK(bank->song().rows[1].transpose == 0);
  CHECK(bank->undo() >= 0);
  CHECK(bank->song().rows[1].repeat == 1);
  CHECK(bank->undo() >= 0);
  CHECK(bank->song().rows[0].repeat == 1);
  CHECK_FALSE(bank->can_undo());
}

TEST_CASE("CLEAR SONG and FROM CHAIN are one undo step each; restore_song is not an edit") {
  auto bank = std::make_unique<Bank>();
  bank->init();
  const std::uint8_t chain[3] = {1, 2, 3};
  CHECK(bank->song_from_chain(0, chain, 3));
  const Song built = bank->song();
  CHECK(built.length == 4);
  CHECK(bank->song_clear());
  CHECK(bank->song().length == 0);
  CHECK(bank->undo() >= 0);
  CHECK(same(bank->song(), built));
  CHECK(bank->redo() >= 0);
  CHECK(bank->song().length == 0);
  CHECK(bank->undo() >= 0);
  CHECK(bank->undo() >= 0);
  CHECK(bank->song().length == 0);

  Song loaded = fresh();
  loaded.length = 70;  // garbage: normalised
  bank->clear_history();
  bank->restore_song(loaded);
  CHECK(bank->song().length == kSongRows);
  CHECK_FALSE(bank->can_undo());
}

TEST_CASE("the history bound drops the oldest song steps like any other") {
  auto bank = std::make_unique<Bank>();
  bank->init();
  for (unsigned i = 0; i < kHistoryDepth + 8; ++i) {
    REQUIRE(bank->song_append(row(i % kPatternCount)));
  }
  unsigned undone = 0;
  while (bank->undo() >= 0) {
    ++undone;
  }
  CHECK(undone == kHistoryDepth);
  CHECK(bank->song().length == 8);
}

// ---- Playing -------------------------------------------------------------------------------------------------------------------

TEST_CASE("a song plays to the event of the same chain made by hand") {
  // The hand chain plays the selected pattern and then its queue; a song of x1 rows is that, to the frame.
  Rig by_hand;
  make_patterns(by_hand);
  by_hand.bank->select(0);
  by_hand.start();
  by_hand.seq.queue(1);
  by_hand.seq.queue(2);
  by_hand.engine.play();

  Rig by_song;
  make_patterns(by_song);
  by_song.start();
  set_song(by_song, {row(0), row(1), row(2)});
  by_song.seq.set_play_song(true);
  by_song.seq.set_song_loop(false);
  by_song.engine.play();

  const std::uint64_t end = frame_of(static_cast<std::uint64_t>(16 + 8 + 16) * kStep);
  by_hand.run(end + 2000);
  by_song.run(end + 2000);
  const auto a = events_before(by_hand, end);
  const auto b = events_before(by_song, end);
  REQUIRE(a.size() > 50);
  REQUIRE(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    CAPTURE(i);
    CHECK(a[i] == b[i]);
  }
}

TEST_CASE("repeats are plays of the pattern: a song of x2 and x3 rows is the chain with the entries written out, over three laps") {
  Rig by_hand;
  make_patterns(by_hand);
  by_hand.bank->select(0);
  by_hand.start();
  // P0 P0 P1 P1 P1, three laps: the selected pattern and fifteen entries (the chain's size).
  const unsigned lap[5] = {0, 0, 1, 1, 1};
  for (unsigned i = 1; i < 16; ++i) {
    by_hand.seq.queue(lap[i % 5]);
  }
  by_hand.engine.play();

  Rig by_song;
  make_patterns(by_song);
  by_song.start();
  set_song(by_song, {row(0, 2), row(1, 3)});
  by_song.seq.set_play_song(true);
  by_song.engine.play();  // the loop is on

  const std::uint64_t lap_ticks = static_cast<std::uint64_t>(2 * 16 + 3 * 8) * kStep;
  const std::uint64_t end = frame_of(3 * lap_ticks);
  by_hand.run(end + 2000);
  by_song.run(end + 2000);
  const auto a = events_before(by_hand, end);
  const auto b = events_before(by_song, end);
  REQUIRE(a.size() > 200);
  REQUIRE(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    CAPTURE(i);
    CHECK(a[i] == b[i]);
  }
}

TEST_CASE("FROM CHAIN: the song the chain gives plays like the chain, in the audio of the real voice, sample by sample") {
  using ARig = BasicRig<AcidVoice>;
  const auto build = [](ARig& rig) {
    rig.bank->replace(0, make_pat(36, 16));
    rig.bank->replace(1, make_pat(48, 8));
    rig.bank->replace(2, make_pat(43, 16));
    rig.bank->clear_history();
    rig.keep = true;
    rig.start();
  };
  ARig by_hand;
  build(by_hand);
  by_hand.bank->select(1);
  by_hand.seq.queue(2);
  by_hand.seq.queue(0);
  by_hand.seq.queue(0);
  by_hand.seq.queue(1);
  by_hand.engine.play();
  by_hand.run(64);  // the chain is on the render side from here
  const ChainView view = by_hand.seq.chain();
  REQUIRE(view.length == 4);

  ARig by_song;
  build(by_song);
  by_song.bank->select(1);
  REQUIRE(by_song.bank->song_from_chain(1, view.pattern, view.length));
  by_song.seq.sync_song();
  by_song.seq.set_play_song(true);
  by_song.seq.set_song_loop(false);
  by_song.engine.play();
  by_song.run(64);

  const std::uint64_t end = frame_of(static_cast<std::uint64_t>(8 + 16 + 16 + 16 + 8) * kStep);
  by_hand.run(end - 64);
  by_song.run(end - 64);
  REQUIRE(by_hand.out_l.size() == by_song.out_l.size());
  REQUIRE(by_hand.out_l.size() == end);
  CHECK(std::memcmp(by_hand.out_l.data(), by_song.out_l.data(), end * sizeof(float)) == 0);
  CHECK(std::memcmp(by_hand.out_r.data(), by_song.out_r.data(), end * sizeof(float)) == 0);
  float peak = 0.0f;
  for (const float v : by_song.out_l) {
    peak = std::max(peak, std::fabs(v));
  }
  CHECK(peak > 0.01f);  // not two silences
}

TEST_CASE("pattern mode is untouched by a song that is there but not played") {
  Rig plain;
  make_patterns(plain);
  plain.bank->select(2);
  plain.start();
  plain.engine.play();

  Rig with_song;
  make_patterns(with_song);
  with_song.bank->select(2);
  with_song.start();
  set_song(with_song, {row(0, 3, 7), row(1)});
  with_song.seq.set_song_loop(false);  // play_song stays off
  with_song.engine.play();

  const std::uint64_t n = frame_of(static_cast<std::uint64_t>(40) * kStep);
  plain.run(n);
  with_song.run(n);
  CHECK(plain.voice.log.size() > 30);
  CHECK(plain.voice.log == with_song.voice.log);
  CHECK_FALSE(with_song.seq.song_position().active);
  CHECK(with_song.seq.playhead().pattern == 2);
}

TEST_CASE("a row's transpose sits on top of the pattern's and the live transpose, and is clamped like them") {
  Rig rig;
  make_patterns(rig);
  Pattern p = make_pat(40, 8);
  p.transpose = 1;
  rig.bank->replace(0, p);
  Pattern high = make_pat(124, 8);
  rig.bank->replace(1, high);
  rig.start();
  rig.seq.set_live_transpose(2);
  set_song(rig, {row(0, 1, 5), row(0, 1, -12), row(1, 1, 12)});
  rig.seq.set_play_song(true);
  rig.engine.play();
  rig.run(frame_of(static_cast<std::uint64_t>(24) * kStep) + 100);
  const auto n = notes_of(rig);
  std::vector<unsigned> on;
  for (const Rec& e : n) {
    if (e.kind == dsp::EventKind::NoteOn) {
      on.push_back(e.note);
    }
  }
  // Step 0 of each row (the pattern's note + 1 + live 2 + the row's transpose). Row 2's first note: 124 + 2 + 12 clamps to 127.
  REQUIRE(on.size() >= 18);
  CHECK(on[0] == 40u + 1 + 2 + 5);
  CHECK(on[7] == 40u + 1 + 2 - 12);   // step 0 of row 2: row 1's seven playing steps came first (step 7 is a rest)
  CHECK(on[14] == 127u);
}

TEST_CASE("muted rows are skipped, the start row too; a song with nothing to play is pattern mode; the loop wraps past muted rows") {
  {
    Rig rig;
    make_patterns(rig);
    rig.start();
    set_song(rig, {row(0, 1, 0, true), row(1), row(2, 1, 0, true)});
    rig.seq.set_play_song(true, 0);  // the start row is muted: row 1 starts
    rig.engine.play();
    rig.run(64);
    CHECK(rig.seq.playhead().pattern == 1);
    CHECK(rig.seq.song_position().row == 1);
    rig.run(frame_of(static_cast<std::uint64_t>(8) * kStep));  // row 1 ends: nothing after it, the loop wraps to the same row
    CHECK(rig.seq.playhead().pattern == 1);
    CHECK(rig.seq.song_position().row == 1);
    CHECK(rig.seq.song_position().repeat == 0);
  }
  {
    Rig rig;
    make_patterns(rig);
    rig.bank->select(2);
    rig.start();
    set_song(rig, {row(0, 1, 0, true), row(1, 1, 0, true)});
    rig.seq.set_play_song(true);
    rig.engine.play();
    rig.run(64);
    CHECK(rig.seq.playhead().pattern == 2);  // the selected pattern
    CHECK_FALSE(rig.seq.song_position().active);
  }
  {
    Rig rig;
    make_patterns(rig);
    rig.bank->select(2);
    rig.start();
    rig.seq.set_play_song(true);  // an empty song
    rig.engine.play();
    rig.run(64);
    CHECK(rig.seq.playhead().pattern == 2);
    CHECK_FALSE(rig.seq.song_position().active);
  }
}

TEST_CASE("the position tells the row and the repeat; the start row starts there") {
  Rig rig;
  make_patterns(rig);
  rig.start();
  set_song(rig, {row(0), row(1, 2), row(2)});
  rig.seq.set_play_song(true, 1);
  rig.engine.play();
  rig.run(64);
  SongPosition p = rig.seq.song_position();
  CHECK(p.active);
  CHECK_FALSE(p.done);
  CHECK(p.row == 1);
  CHECK(p.repeat == 0);
  CHECK(rig.seq.playhead().pattern == 1);
  rig.run(frame_of(static_cast<std::uint64_t>(8) * kStep));
  p = rig.seq.song_position();
  CHECK(p.row == 1);
  CHECK(p.repeat == 1);
  rig.run(frame_of(static_cast<std::uint64_t>(8) * kStep));
  p = rig.seq.song_position();
  CHECK(p.row == 2);
  CHECK(p.repeat == 0);
  CHECK(rig.seq.playhead().pattern == 2);
  rig.run(frame_of(static_cast<std::uint64_t>(16) * kStep));  // the loop: row 0
  CHECK(rig.seq.song_position().row == 0);
  rig.engine.stop();
  rig.run(256);
  CHECK_FALSE(rig.seq.song_position().active);
  CHECK(rig.seq.song_position().row == -1);
}

TEST_CASE("with the loop off the song finishes: the note ends, nothing plays, the transport runs on, Play starts it again") {
  Rig rig;
  make_patterns(rig);
  Pattern tie = make_pat(40, 8);
  tie.steps[7].flags = kStepSlide;  // the last step holds its note into the next one: it must end with the song, not hang
  rig.bank->replace(1, tie);
  rig.start();
  set_song(rig, {row(1)});
  rig.seq.set_play_song(true);
  rig.seq.set_song_loop(false);
  rig.engine.play();
  const std::uint64_t end = frame_of(static_cast<std::uint64_t>(8) * kStep);
  rig.run(end + 2000);
  SongPosition p = rig.seq.song_position();
  CHECK(p.active);
  CHECK(p.done);
  CHECK(p.row == 0);
  CHECK_FALSE(rig.seq.playhead().sounding);
  CHECK(rig.seq.playhead().playing);
  const std::size_t events = rig.voice.log.size();
  rig.run(frame_of(static_cast<std::uint64_t>(32) * kStep));
  CHECK(rig.voice.log.size() == events);  // quiet from here on
  int on = 0;
  int off = 0;
  for (const Rec& e : rig.voice.log) {
    on += e.kind == dsp::EventKind::NoteOn ? 1 : 0;
    off += e.kind == dsp::EventKind::NoteOff ? 1 : 0;
  }
  CHECK(on == off);  // every note that started ended

  rig.engine.play();  // Play again: a new run, the song from its start
  rig.run(256);
  p = rig.seq.song_position();
  CHECK(p.active);
  CHECK_FALSE(p.done);
  CHECK(rig.voice.log.size() > events);
}

TEST_CASE("a song edit is heard at the row that starts next; a jump is not followed while a song plays") {
  Rig rig;
  make_patterns(rig);
  rig.start();
  set_song(rig, {row(0), row(1), row(2)});
  rig.seq.set_play_song(true, 0);
  rig.engine.play();
  rig.run(frame_of(static_cast<std::uint64_t>(4) * kStep));  // inside row 0
  REQUIRE(rig.bank->song_set_pattern(1, 2));                  // row 1 now plays pattern 2
  REQUIRE(rig.bank->song_set_repeat(0, 2));                   // and row 0 plays twice
  rig.seq.sync_song();
  rig.seq.jump(1);  // the song is the order
  rig.run(frame_of(static_cast<std::uint64_t>(12) * kStep) + 64);  // row 0's pattern ended: its second play
  CHECK(rig.seq.song_position().row == 0);
  CHECK(rig.seq.song_position().repeat == 1);
  CHECK(rig.seq.playhead().pattern == 0);
  rig.run(frame_of(static_cast<std::uint64_t>(16) * kStep));
  CHECK(rig.seq.song_position().row == 1);
  CHECK(rig.seq.playhead().pattern == 2);
}

TEST_CASE("the pass counter goes on over a row's repeats: FIRST plays on the first play only; a new row starts it again") {
  Rig rig;
  make_patterns(rig);
  Pattern p = make_pat(40, 8);
  for (unsigned j = 0; j < 8; ++j) {
    p.steps[j].flags = 0;
    p.steps[j].cond = kCondAlways;
  }
  p.steps[0].cond = kCondFirst;
  rig.bank->replace(0, p);
  rig.start();
  set_song(rig, {row(0, 2), row(0, 1)});
  rig.seq.set_play_song(true);
  rig.seq.set_song_loop(false);
  rig.engine.play();
  rig.run(frame_of(static_cast<std::uint64_t>(3 * 8) * kStep) + 100);
  // Step 0 (note 40) sounds in the first play of row 0 and in row 1 (a new row, pass 0), not in row 0's second play.
  std::vector<std::uint64_t> at40;
  for (const Rec& e : rig.voice.log) {
    if (e.kind == dsp::EventKind::NoteOn && e.note == 40) {
      at40.push_back(e.frame);
    }
  }
  REQUIRE(at40.size() == 2);
  CHECK(at40[0] == 0);
  CHECK(at40[1] == frame_of(static_cast<std::uint64_t>(16) * kStep));
}

TEST_CASE("the song costs nothing in the render's state when off: set_song, loop and start row are plain atomics") {
  Rig rig;
  make_patterns(rig);
  rig.start();
  Song s = fresh();
  song_append(s, row(1, 2, 3, true));
  rig.seq.set_song(s);
  CHECK_FALSE(rig.seq.play_song());
  CHECK(rig.seq.song_loop());
  rig.seq.set_song_loop(false);
  CHECK_FALSE(rig.seq.song_loop());
  rig.seq.set_play_song(true, 200);  // a start row past the table is row 0
  CHECK(rig.seq.play_song());
  rig.seq.set_play_song(false);
  CHECK_FALSE(rig.seq.play_song());
  CHECK_FALSE(rig.seq.song_position().active);
  CHECK(rig.seq.song_position().row == -1);
}
