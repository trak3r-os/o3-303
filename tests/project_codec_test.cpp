// SPDX-License-Identifier: GPL-3.0-or-later
// The project codec (app/src/project.h, docs/UX-303.md 8.1 and 8.3, docs/UX-PLAN.md F1): a project is gathered from the session, encoded into one image,
// decoded and applied to another session; a damaged image is refused, a newer major of HEAD refused, other chunks of a newer major or unknown skipped,
// a newer minor read, a missing parameter keeps its default, a SNAP with fewer values is filled from the sound. The session is the app's wiring over a real
// engine (the units attached in the order of o3_303_app.cpp), without the screens.
#include <doctest/doctest.h>

#include "../app/src/project.h"

#include <o3sdk/dsp/engine.h>
#include <o3sdk/formats/chunk_file.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/pages.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/growl/growl.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace o3_303;
using namespace o3sdk;
namespace chunk = o3sdk::formats::chunk;
namespace core = o3sdk::core;
namespace dsp = o3sdk::dsp;

namespace {

// A session: the engine with the five units, the bank and the sequencer, and the Rig the screens share.
struct Session {
  static constexpr std::uint32_t kCap = 4096;
  float l[kCap] = {};
  float r[kCap] = {};
  AcidVoice voice;
  Distortion dist;
  units::Delay delay{l, r, kCap};
  float fl[kCap] = {};
  float fr[kCap] = {};
  Freeze freeze{fl, fr, kCap};
  Growl growl;
  dsp::Engine engine;
  std::unique_ptr<seq::Bank> bank = std::make_unique<seq::Bank>();
  std::unique_ptr<seq::Sequencer> sequencer = std::make_unique<seq::Sequencer>(*bank);
  Rig rig;

  Session() {
    Wiring& w = rig.wiring;
    w.voice = engine.attach(voice);
    w.dist = engine.attach(dist);
    w.delay = engine.attach(delay);
    w.freeze = engine.attach(freeze);
    w.growl = engine.attach(growl);
    (void)engine.set_source(0, w.voice);
    (void)engine.set_insert(0, 0, w.dist);
    (void)engine.set_insert(0, 1, w.growl);
    (void)engine.set_send_effect(0, w.delay);
    bank->init();
    rig.bank = bank.get();
    rig.seq = sequencer.get();
    w.bank = bank.get();
    w.seq = sequencer.get();
    rig.controls.init(w);
    (void)engine.set_tempo(125'000);
  }

  project::ProjectState gather() const {
    project::ProjectState s;
    project::gather(rig, engine, s);
    return s;
  }
};

// The ids of a unit (dense from 0).
unsigned param_count(const dsp::Engine& e, dsp::UnitId u) {
  unsigned n = 0;
  while (e.param_desc(u, static_cast<std::uint16_t>(n)) != nullptr) {
    ++n;
  }
  return n;
}

// The parameters a project carries (project.cpp carried()): not the voice's Volume and Stock, the growl's Note, the freeze's Active, Mode and Mix.
bool carried(const Wiring& w, dsp::UnitId u, unsigned id) {
  if (u == w.voice) {
    return id != AcidVoice::Volume && id != AcidVoice::Stock;
  }
  if (u == w.growl) {
    return id != Growl::Note;
  }
  if (u == w.freeze) {
    return id == Freeze::Length || id == Freeze::RollRate || id == Freeze::Accelerate || id == Freeze::StopTime;
  }
  return true;
}

std::uint32_t lcg(std::uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return s >> 8;
}

// A pattern with every field in range, from a seed.
Pattern random_pattern(std::uint32_t seed) {
  Pattern p;
  clear(p);
  std::uint32_t s = seed * 7919u + 1u;
  for (Step& st : p.steps) {
    st.note = static_cast<std::uint8_t>(24 + lcg(s) % 40);
    st.flags = static_cast<std::uint8_t>(lcg(s) & 0x3Fu);
    st.cond = static_cast<std::uint8_t>(lcg(s) % 4 == 0 ? 0xC0 + lcg(s) % 4 : lcg(s) % 100);
  }
  p.length = kLengths[lcg(s) % 4];
  p.swing = static_cast<std::uint8_t>(lcg(s) % (kMaxSwing + 1u));
  p.transpose = static_cast<std::int8_t>(static_cast<int>(lcg(s) % 49) - 24);
  p.gate = static_cast<std::uint8_t>(5 + lcg(s) % 96);
  for (auto& lane : p.motion) {
    for (std::uint8_t& pt : lane) {
      pt = lcg(s) % 3 == 0 ? static_cast<std::uint8_t>(lcg(s) & 0xFFu) : kNoLock;
    }
  }
  for (LaneRef& ref : p.lanes) {
    ref.target = static_cast<std::uint8_t>(lcg(s) % kLaneTargetCount);
    ref.id = static_cast<std::uint8_t>(lcg(s) % 8);
  }
  return p;
}

// Every field of 8.1 away from its default.
void scramble(Session& t) {
  Wiring& w = t.rig.wiring;
  for (unsigned i = 0; i < kPatternCount; ++i) {
    (void)t.bank->restore(i, random_pattern(i + 1u));
  }
  t.bank->select(17);
  for (unsigned i = 0; i < 9; ++i) {  // a song with a negative transpose, a mute, repeats and the last pattern
    (void)t.bank->song_append(seq::SongRow{static_cast<std::uint8_t>(i == 8 ? kPatternCount - 1u : i * 5u), static_cast<std::uint8_t>(1u + i * 2u % 16u),
                                           static_cast<std::int8_t>(static_cast<int>(i) * 3 - 12), static_cast<std::uint8_t>(i % 3u == 0u ? seq::kSongRowMute : 0u)});
  }
  (void)t.engine.set_tempo(133'500);
  w.scale_set = true;
  w.gen.scale = 7;
  w.gen.root = 5;
  w.gen.low = 30;
  w.gen.high = 55;
  w.gen.density = 61;
  w.gen.accent_pct = 33;
  w.gen.slide_pct = 26;
  for (const dsp::UnitId u : {w.voice, w.dist, w.delay, w.growl, w.freeze}) {
    for (unsigned id = 0; id < param_count(t.engine, u); ++id) {
      if (!carried(w, u, id)) {
        continue;
      }
      const core::ParamDesc& d = *t.engine.param_desc(u, static_cast<std::uint16_t>(id));
      const float f = 0.15f + 0.11f * static_cast<float>((id + u) % 7u);
      float v = d.min + (d.max - d.min) * f;
      if (d.unit == core::ParamUnit::Enum) {
        v = std::floor(v + 0.5f);
      }
      (void)t.engine.set_param(u, static_cast<std::uint16_t>(id), v);
    }
  }
  w.level_db = -12.5f;
  w.send_a_pct = 45.0f;
  (void)t.engine.set_master_level(1.25f);
  t.sequencer->set_fill_variant(seq::FillVariant::Ratchet);
  w.wobble.rate = 4;
  w.wobble.shape = 3;
  w.wobble.depth_pct = 77;
  w.wobble.reset = true;  // away from the default (off since B5a)
  w.wobble.target = kWobbleVowel;
  w.wob_want = 1;         // the WOB latch is saved too (B5a)
  w.perf[0] = static_cast<std::uint8_t>(PerfAction::Reverse);
  w.perf[1] = static_cast<std::uint8_t>(PerfAction::Wobble);
  w.perf[2] = static_cast<std::uint8_t>(PerfAction::Throw);
  w.perf[3] = static_cast<std::uint8_t>(PerfAction::Stop);
  float values[kSnapValues] = {};
  for (const unsigned slot : {0u, 3u, 7u}) {
    for (unsigned i = 0; i < kSnapValues; ++i) {
      values[i] = static_cast<float>(slot) * 10.0f + static_cast<float>(i) * 0.37f - 1.0f;
    }
    w.snaps.store(slot, values);
  }
}

std::vector<std::uint8_t> encode_session(const Session& t, const char* name = "JAM ONE") {
  project::ProjectState s = t.gather();
  std::snprintf(s.name, sizeof s.name, "%s", name);
  std::vector<std::uint8_t> img(project::kProjectBytes);
  const std::size_t n = project::encode(s, *t.bank, img.data(), img.size());
  REQUIRE(n > 0);
  img.resize(n);
  return img;
}

// The image with its chunks rewritten by `fn(id, payload)` (false: dropped) and `extra` chunks appended, re-sealed with a good CRC.
using Mod = std::function<bool(char (&id)[4], std::vector<std::uint8_t>& payload)>;
std::vector<std::uint8_t> rebuild(const std::vector<std::uint8_t>& img, const Mod& fn,
                                  const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& extra = {}) {
  chunk::Reader r;
  REQUIRE(r.open(img.data(), img.size(), "303P") == chunk::Error::None);
  std::vector<std::uint8_t> out(project::kProjectBytes);
  chunk::Writer w(out.data(), out.size(), "303P");
  chunk::Chunk c;
  while (r.next(c)) {
    std::vector<std::uint8_t> payload(c.data, c.data + c.size);
    char id[4] = {c.id[0], c.id[1], c.id[2], c.id[3]};
    if (fn(id, payload)) {
      char id5[5] = {id[0], id[1], id[2], id[3], 0};
      REQUIRE(w.chunk(id5, payload.data(), payload.size()));
    }
  }
  for (const auto& [id, payload] : extra) {
    char id5[5] = {id[0], id[1], id[2], id[3], 0};
    REQUIRE(w.chunk(id5, payload.data(), payload.size()));
  }
  const std::size_t n = w.finish();
  REQUIRE(n > 0);
  out.resize(n);
  return out;
}

bool is(const char (&id)[4], const char* want) { return std::memcmp(id, want, 4) == 0; }

bool same_pattern(const Pattern& a, const Pattern& b) { return std::memcmp(&a, &b, sizeof(Pattern)) == 0; }

project::Result load(const std::vector<std::uint8_t>& img, project::ProjectState& s) { return project::decode(img.data(), img.size(), s); }

}  // namespace

TEST_CASE("project: every field of 8.1 survives encode, decode and apply, bit for bit") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  CHECK(img.size() <= project::kProjectBytes);

  project::ProjectState s;
  const project::Result res = load(img, s);
  REQUIRE(res.status == project::Status::Ok);
  CHECK(res.skipped == 0);
  CHECK(std::strcmp(s.name, "JAM ONE") == 0);
  CHECK(s.selected == 17);
  CHECK(s.tempo_mbpm == 133'500);
  CHECK(s.scale_set);
  CHECK(s.gen.scale == 7);
  CHECK(s.gen.root == 5);
  CHECK(s.gen.low == 30);
  CHECK(s.gen.high == 55);
  CHECK(s.gen.density == 61);
  CHECK(s.gen.accent_pct == 33);
  CHECK(s.gen.slide_pct == 26);
  CHECK(s.perf[0] == static_cast<std::uint8_t>(PerfAction::Reverse));
  CHECK(s.perf[3] == static_cast<std::uint8_t>(PerfAction::Stop));
  CHECK(s.snap_used == ((1u << 0) | (1u << 3) | (1u << 7)));
  CHECK(s.pats_n == kPatternCount);
  CHECK(s.song.length == 9);
  CHECK(std::memcmp(&s.song, &a.bank->song(), sizeof(seq::Song)) == 0);

  // A second session, with other values everywhere, takes the project.
  Session b;
  (void)b.bank->song_append(seq::SongRow{1, 3, 2, 0});  // a song of its own: the project's replaces it
  b.rig.wiring.perf[0] = static_cast<std::uint8_t>(PerfAction::Mute);
  b.rig.wiring.gen.density = 5;
  float junk[kSnapValues] = {};
  b.rig.wiring.snaps.store(5, junk);
  (void)b.engine.set_param(b.rig.wiring.voice, AcidVoice::Cutoff, 123.0f);
  project::apply(b.rig, b.engine, s);

  for (unsigned i = 0; i < kPatternCount; ++i) {
    CHECK(same_pattern(b.bank->pattern(i), a.bank->pattern(i)));
    CHECK((b.bank->version(i) & 1u) == 0u);
  }
  CHECK(b.bank->selected() == 17);
  CHECK(std::memcmp(&b.bank->song(), &a.bank->song(), sizeof(seq::Song)) == 0);
  CHECK_FALSE(b.bank->can_undo());  // the song edit above is gone with the old project's undo steps
  CHECK_FALSE(b.bank->can_redo());
  CHECK(b.engine.own_tempo() == 133'500);
  const Wiring& wa = a.rig.wiring;
  const Wiring& wb = b.rig.wiring;
  CHECK(wb.scale_set);
  CHECK(wb.gen.scale == wa.gen.scale);
  CHECK(wb.gen.density == wa.gen.density);
  CHECK(wb.gen.slide_pct == wa.gen.slide_pct);
  CHECK(wb.level_db == wa.level_db);
  CHECK(wb.send_a_pct == wa.send_a_pct);
  CHECK(b.engine.master_level() == a.engine.master_level());
  CHECK(b.sequencer->fill_variant() == seq::FillVariant::Ratchet);
  CHECK(wb.wobble.rate == 4);
  CHECK(wb.wobble.shape == 3);
  CHECK(wb.wobble.depth_pct == 77);
  CHECK(wb.wobble.reset);
  CHECK(wb.wobble.target == kWobbleVowel);
  CHECK(wb.wob_want == 1);  // the latch comes back as a request: LiveActions::update latches it
  for (unsigned k = 0; k < kPerfButtons; ++k) {
    CHECK(wb.perf[k] == wa.perf[k]);
  }
  for (const dsp::UnitId u : {wa.voice, wa.dist, wa.delay, wa.growl, wa.freeze}) {
    for (unsigned id = 0; id < param_count(a.engine, u); ++id) {
      if (carried(wa, u, id)) {
        CHECK(b.engine.param(u, static_cast<std::uint16_t>(id)) == a.engine.param(u, static_cast<std::uint16_t>(id)));
      }
    }
  }
  for (unsigned slot = 0; slot < kSnapSlots; ++slot) {
    CHECK(wb.snaps.used(slot) == wa.snaps.used(slot));
  }

  // The whole project again, from the second session: the same bytes.
  CHECK(encode_session(b) == img);
}

TEST_CASE("project: what is held now is not part of the project") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);
  Session b;
  const Wiring& w = b.rig.wiring;
  (void)b.engine.set_param(w.freeze, Freeze::Active, 1.0f);
  (void)b.engine.set_param(w.freeze, Freeze::Mode, 2.0f);
  (void)b.engine.set_param(w.voice, AcidVoice::Volume, 0.0f);
  b.rig.wiring.throw_on = true;
  project::apply(b.rig, b.engine, s);
  CHECK(b.engine.param(w.freeze, Freeze::Active) == 1.0f);
  CHECK(b.engine.param(w.freeze, Freeze::Mode) == 2.0f);
  CHECK(b.engine.param(w.voice, AcidVoice::Volume) == 0.0f);
  CHECK(b.rig.wiring.throw_on);
}

TEST_CASE("project: a flipped byte is refused, wherever it is") {
  Session a;
  scramble(a);
  std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);
  std::vector<std::size_t> at = {0, 4, 8, 11, 12, 20, 40, 60, img.size() / 2, img.size() - 15, img.size() - 6, img.size() - 1};
  for (std::size_t i = 100; i < img.size(); i += 997) {
    at.push_back(i);
  }
  for (const std::size_t i : at) {
    std::vector<std::uint8_t> bad = img;
    bad[i] = static_cast<std::uint8_t>(bad[i] ^ 0x01u);
    const project::Result res = load(bad, s);
    CHECK_MESSAGE(res.status == project::Status::Damaged, "byte ", i);
    CHECK_MESSAGE(res.error != chunk::Error::None, "byte ", i);
  }
  // Truncated, empty, another file.
  CHECK(project::decode(img.data(), img.size() - 1, s).status == project::Status::Damaged);
  CHECK(project::decode(img.data(), 0, s).status == project::Status::Damaged);
  CHECK(project::decode(nullptr, 0, s).status == project::Status::Damaged);
  std::vector<std::uint8_t> other(64, 0);
  CHECK(project::decode(other.data(), other.size(), s).status == project::Status::Damaged);
}

TEST_CASE("project: HEAD of a newer major refuses the project; HEAD missing or short is damaged") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;

  const auto newer = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "HEAD")) {
      p[0] = 2;  // major 2
    }
    return true;
  });
  CHECK(load(newer, s).status == project::Status::FromNewer);

  const auto nohead = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>&) { return !is(id, "HEAD"); });
  CHECK(load(nohead, s).status == project::Status::Damaged);

  const auto shorthead = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "HEAD")) {
      p.resize(8);
    }
    return true;
  });
  CHECK(load(shorthead, s).status == project::Status::Damaged);
}

TEST_CASE("project: another chunk of a newer major, an unknown one, a short SONG and a doubled one are skipped, their parts keep their defaults") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;

  // GEN of major 2 (its layout is unknown), a chunk nobody knows, and a SONG that says 4 rows and holds two bytes of them (the real SONG of the image is read first: the
  // doubled one is the fourth skip).
  const auto skipped = rebuild(
      img,
      [](char (&id)[4], std::vector<std::uint8_t>& p) {
        if (is(id, "GEN ") || is(id, "PERF")) {
          p[0] = 2;
        }
        return true;
      },
      {{"ZZZZ", {1, 0, 9, 9, 9}}, {std::string(project::kSongId), {1, 0, 4, 5, 6}}});
  const project::Result res = load(skipped, s);
  REQUIRE(res.status == project::Status::Ok);
  CHECK(res.skipped == 4);
  const seq::GenParams def;
  CHECK(s.gen.scale == def.scale);
  CHECK(s.gen.density == def.density);
  CHECK(s.perf[0] == kDefaultPerf[0]);
  CHECK(s.perf[3] == kDefaultPerf[3]);
  // The rest is read as it was.
  CHECK(s.selected == 17);
  CHECK(s.tempo_mbpm == 133'500);
  CHECK(s.pats_n == kPatternCount);
  CHECK(s.snap_used == ((1u << 0) | (1u << 3) | (1u << 7)));
  CHECK(s.sound_n > 30);

  // A chunk that is there twice: the first counts.
  std::vector<std::uint8_t> gen_payload;
  (void)rebuild(img, [&](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "GEN ")) {
      gen_payload = p;
    }
    return true;
  });
  REQUIRE_FALSE(gen_payload.empty());
  gen_payload[3] = 1;  // another root
  const auto twice = rebuild(img, [](char (&)[4], std::vector<std::uint8_t>&) { return true; }, {{"GEN ", gen_payload}});
  const project::Result r2 = load(twice, s);
  REQUIRE(r2.status == project::Status::Ok);
  CHECK(r2.skipped == 1);
  CHECK(s.gen.root == 5);
}

TEST_CASE("project: a song of 64 rows round-trips, and the project's song replaces the session's, with no undo step left") {
  Session a;
  for (unsigned i = 0; i < seq::kSongRows; ++i) {
    REQUIRE(a.bank->song_append(seq::SongRow{static_cast<std::uint8_t>(i % kPatternCount), static_cast<std::uint8_t>(1u + i % 16u),
                                             static_cast<std::int8_t>(static_cast<int>(i % 25u) - 12), static_cast<std::uint8_t>(i % 2u)}));
  }
  REQUIRE(a.bank->song().length == seq::kSongRows);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  const project::Result res = load(img, s);
  REQUIRE(res.status == project::Status::Ok);
  CHECK(res.skipped == 0);
  CHECK(std::memcmp(&s.song, &a.bank->song(), sizeof(seq::Song)) == 0);

  Session b;
  REQUIRE(b.bank->song_append(seq::SongRow{3, 2, 1, 0}));
  REQUIRE(b.bank->can_undo());
  REQUIRE(project::apply(b.rig, b.engine, s));
  CHECK(std::memcmp(&b.bank->song(), &a.bank->song(), sizeof(seq::Song)) == 0);
  CHECK_FALSE(b.bank->can_undo());
  CHECK(encode_session(b) == img);

  // The sequencer got the song too: Play from the song starts the row the project has first (pattern 0, as `i % 48` says).
  b.sequencer->set_play_song(true, 5);
  CHECK(b.sequencer->play_song());
}

TEST_CASE("project: a project from before SONG loads with an empty song, and an apply empties the session's") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> with_song = encode_session(a);
  // The image as an SG1 build wrote it: the same chunks without SONG.
  const auto older = rebuild(with_song, [](char (&id)[4], std::vector<std::uint8_t>&) { return !is(id, "SONG"); });
  REQUIRE(older.size() < with_song.size());
  project::ProjectState s;
  const project::Result res = load(older, s);
  REQUIRE(res.status == project::Status::Ok);
  CHECK(res.skipped == 0);  // nothing is missing from the reader's point of view: SONG is optional
  CHECK(s.song.length == 0);
  seq::Song empty;
  seq::clear_song(empty);
  CHECK(std::memcmp(&s.song, &empty, sizeof empty) == 0);
  CHECK(s.pats_n == kPatternCount);

  Session b;
  REQUIRE(b.bank->song_append(seq::SongRow{3, 2, 1, 0}));
  REQUIRE(project::apply(b.rig, b.engine, s));
  CHECK(b.bank->song().length == 0);
  CHECK(std::memcmp(&b.bank->song(), &empty, sizeof empty) == 0);
  CHECK_FALSE(b.bank->can_undo());

  // And a song that is empty is an empty chunk: written, read back, the same.
  Session c;
  const std::vector<std::uint8_t> blank = encode_session(c);
  project::ProjectState t;
  REQUIRE(load(blank, t).status == project::Status::Ok);
  CHECK(t.song.length == 0);
}

TEST_CASE("project: a damaged SONG gives nothing it must not: short is skipped, a newer major is skipped, values out of range come in") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  seq::Song empty;
  seq::clear_song(empty);

  const auto payload_of = [&](std::vector<std::uint8_t> payload) {
    return rebuild(img, [&](char (&id)[4], std::vector<std::uint8_t>& p) {
      if (is(id, "SONG")) {
        p = payload;
      }
      return true;
    });
  };
  project::ProjectState s;
  {  // 5 rows said, 3 present
    std::vector<std::uint8_t> p = {1, 0, 5};
    for (unsigned i = 0; i < 12; ++i) {
      p.push_back(1);
    }
    const project::Result res = load(payload_of(p), s);
    REQUIRE(res.status == project::Status::Ok);
    CHECK(res.skipped == 1);
    CHECK(s.song.length == 0);
  }
  {  // no length byte at all
    const project::Result res = load(payload_of({1, 0}), s);
    REQUIRE(res.status == project::Status::Ok);
    CHECK(res.skipped == 1);
    CHECK(s.song.length == 0);
  }
  {  // a newer major
    std::vector<std::uint8_t> p = {2, 0, 1, 0, 1, 0, 0};
    const project::Result res = load(payload_of(p), s);
    REQUIRE(res.status == project::Status::Ok);
    CHECK(res.skipped == 1);
    CHECK(s.song.length == 0);
  }
  {  // two rows, every value out of range: the pattern past the last, repeat 0 and 200, transpose -100 and +100, flags that are not only the mute
    const std::vector<std::uint8_t> p = {1, 0, 2, 99, 0, 0x9C, 0xFF, 7, 200, 100, 0x02};
    const project::Result res = load(payload_of(p), s);
    REQUIRE(res.status == project::Status::Ok);
    CHECK(res.skipped == 0);
    REQUIRE(s.song.length == 2);
    CHECK(s.song.rows[0].pattern == kPatternCount - 1);
    CHECK(s.song.rows[0].repeat == 1);
    CHECK(s.song.rows[0].transpose == -seq::kSongMaxTranspose);
    CHECK(s.song.rows[0].flags == seq::kSongRowMute);
    CHECK(s.song.rows[1].repeat == seq::kSongMaxRepeat);
    CHECK(s.song.rows[1].transpose == seq::kSongMaxTranspose);
    CHECK(s.song.rows[1].flags == 0);
    seq::Song canon = s.song;
    seq::song_normalize(canon);
    CHECK(std::memcmp(&canon, &s.song, sizeof canon) == 0);  // canonical: the rows past the length are the default row
  }
  {  // more rows than a song holds: 65 present, 65 said: the first 64 are kept
    std::vector<std::uint8_t> p = {1, 0, 65};
    for (unsigned i = 0; i < 65; ++i) {
      p.insert(p.end(), {static_cast<std::uint8_t>(i % 48u), 1, 0, 0});
    }
    const project::Result res = load(payload_of(p), s);
    REQUIRE(res.status == project::Status::Ok);
    CHECK(s.song.length == seq::kSongRows);
    CHECK(s.song.rows[63].pattern == 63u % 48u);
  }
}

TEST_CASE("project: a song edit changes the small CRC, so that the autosave sees it") {
  Session a;
  const project::ProjectState before = a.gather();
  const std::uint32_t crc0 = project::small_crc(before);
  REQUIRE(a.bank->song_append(seq::SongRow{2, 4, 0, 0}));
  const std::uint32_t crc1 = project::small_crc(a.gather());
  CHECK(crc1 != crc0);
  REQUIRE(a.bank->song_set_transpose(0, -5));
  CHECK(project::small_crc(a.gather()) != crc1);
  (void)a.bank->undo();
  CHECK(project::small_crc(a.gather()) == crc1);
  (void)a.bank->undo();
  CHECK(project::small_crc(a.gather()) == crc0);
}

TEST_CASE("project: a newer minor is read as far as this build knows it") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  const auto minor = rebuild(img, [](char (&)[4], std::vector<std::uint8_t>& p) {
    p[1] = 7;  // 1.7
    for (int i = 0; i < 13; ++i) {
      p.push_back(0xA5);  // fields this build does not know, an odd count to pad
    }
    return true;
  });
  project::ProjectState s;
  const project::Result res = load(minor, s);
  REQUIRE(res.status == project::Status::Ok);
  CHECK(res.skipped == 0);
  CHECK(s.selected == 17);
  CHECK(s.tempo_mbpm == 133'500);
  CHECK(s.gen.density == 61);
  CHECK(s.perf[1] == static_cast<std::uint8_t>(PerfAction::Wobble));
  CHECK(s.pats_n == kPatternCount);
  CHECK(std::strcmp(s.name, "JAM ONE") == 0);

  // The patterns are where they were: the extra bytes are after them.
  Session b;
  project::apply(b.rig, b.engine, s);
  for (unsigned i = 0; i < kPatternCount; ++i) {
    CHECK(same_pattern(b.bank->pattern(i), a.bank->pattern(i)));
  }
}

TEST_CASE("project: a parameter the file lacks keeps its default; one this build lacks is skipped") {
  Session a;
  scramble(a);
  const Wiring& wa = a.rig.wiring;
  const std::vector<std::uint8_t> img = encode_session(a);

  // SND without the voice's Cutoff and Reso and the growl's Vowel, with two entries this build has no parameter for, a NaN, and an app value of a later build.
  const auto cut = rebuild(img, [&](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (!is(id, "SND ")) {
      return true;
    }
    std::vector<std::uint8_t> out(p.begin(), p.begin() + 4);
    unsigned kept = 0;
    auto put = [&](std::uint8_t target, std::uint16_t pid, float v) {
      std::uint32_t bits;
      std::memcpy(&bits, &v, 4);
      const std::uint8_t e[8] = {target, 0, static_cast<std::uint8_t>(pid >> 8), static_cast<std::uint8_t>(pid), static_cast<std::uint8_t>(bits >> 24),
                                 static_cast<std::uint8_t>(bits >> 16), static_cast<std::uint8_t>(bits >> 8), static_cast<std::uint8_t>(bits)};
      out.insert(out.end(), e, e + 8);
      ++kept;
    };
    const unsigned n = (static_cast<unsigned>(p[2]) << 8) | p[3];
    for (unsigned i = 0; i < n; ++i) {
      const std::uint8_t* e = &p[4 + i * 8];
      const std::uint16_t pid = static_cast<std::uint16_t>((e[2] << 8) | e[3]);
      if ((e[0] == kLaneVoice && (pid == AcidVoice::Cutoff || pid == AcidVoice::Reso)) || (e[0] == kLaneGrowl && pid == Growl::Vowel)) {
        continue;
      }
      out.insert(out.end(), e, e + 8);
      ++kept;
    }
    put(kLaneVoice, 60, 1.0f);                                           // a parameter appended by a later build
    put(99, 0, 1.0f);                                                    // another target
    put(kLaneVoice, AcidVoice::Reso, std::numeric_limits<float>::quiet_NaN());  // junk
    put(project::kSndApp, 77, 3.0f);                                     // an app value of a later build
    out[2] = static_cast<std::uint8_t>(kept >> 8);
    out[3] = static_cast<std::uint8_t>(kept);
    p = out;
    return true;
  });
  project::ProjectState s;
  const project::Result res = load(cut, s);
  REQUIRE(res.status == project::Status::Ok);

  Session b;
  (void)b.engine.set_param(b.rig.wiring.voice, AcidVoice::Cutoff, 777.0f);
  (void)b.engine.set_param(b.rig.wiring.growl, Growl::Vowel, 0.9f);
  project::apply(b.rig, b.engine, s);
  const core::ParamDesc& cutoff = *b.engine.param_desc(b.rig.wiring.voice, AcidVoice::Cutoff);
  const core::ParamDesc& vowel = *b.engine.param_desc(b.rig.wiring.growl, Growl::Vowel);
  const core::ParamDesc& reso = *b.engine.param_desc(b.rig.wiring.voice, AcidVoice::Reso);
  CHECK(b.engine.param(b.rig.wiring.voice, AcidVoice::Cutoff) == cutoff.def);
  CHECK(b.engine.param(b.rig.wiring.growl, Growl::Vowel) == vowel.def);
  CHECK(b.engine.param(b.rig.wiring.voice, AcidVoice::Reso) == reso.def);  // missing, and a NaN entry for it was skipped
  // The others came through.
  CHECK(b.engine.param(b.rig.wiring.voice, AcidVoice::Decay) == a.engine.param(wa.voice, AcidVoice::Decay));
  CHECK(b.engine.param(b.rig.wiring.dist, Distortion::Drive) == a.engine.param(wa.dist, Distortion::Drive));
  CHECK(b.rig.wiring.level_db == wa.level_db);

  // An empty project (no SND at all) is the default sound.
  const auto nosnd = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>&) { return !is(id, "SND "); });
  REQUIRE(load(nosnd, s).status == project::Status::Ok);
  project::apply(a.rig, a.engine, s);
  CHECK(a.engine.param(wa.voice, AcidVoice::Cutoff) == cutoff.def);
  CHECK(a.rig.wiring.level_db == Wiring{}.level_db);
  CHECK(a.rig.wiring.send_a_pct == Wiring{}.send_a_pct);
  CHECK(a.engine.master_level() == 1.0f);
  CHECK(a.sequencer->fill_variant() == seq::FillVariant::Mutate);
  CHECK(a.rig.wiring.wobble.rate == WobbleSettings{}.rate);
}

namespace {

struct SndEntry {
  std::uint8_t target;
  std::uint16_t id;
  float value;
};

// The entries of the image's SND chunk, as written ({u8 target, u8 0, u16 id, f32 value}, big endian, after {u16 version, u16 count}).
std::vector<SndEntry> snd_entries(const std::vector<std::uint8_t>& img) {
  std::vector<SndEntry> out;
  chunk::Reader r;
  REQUIRE(r.open(img.data(), img.size(), "303P") == chunk::Error::None);
  chunk::Chunk c;
  while (r.next(c)) {
    if (std::memcmp(c.id, "SND ", 4) != 0) {
      continue;
    }
    const unsigned n = (static_cast<unsigned>(c.data[2]) << 8) | c.data[3];
    for (unsigned i = 0; i < n; ++i) {
      const std::uint8_t* e = c.data + 4 + i * 8;
      const std::uint32_t bits = (static_cast<std::uint32_t>(e[4]) << 24) | (static_cast<std::uint32_t>(e[5]) << 16) | (static_cast<std::uint32_t>(e[6]) << 8) | e[7];
      float f;
      std::memcpy(&f, &bits, 4);
      out.push_back(SndEntry{e[0], static_cast<std::uint16_t>((e[2] << 8) | e[3]), f});
    }
  }
  return out;
}

unsigned voice_entries(const std::vector<SndEntry>& v, unsigned lo, unsigned hi) {
  unsigned n = 0;
  for (const SndEntry& e : v) {
    n += (e.target == kLaneVoice && e.id >= lo && e.id <= hi) ? 1u : 0u;
  }
  return n;
}

}  // namespace

TEST_CASE("project: the voice's mods (ids 8..18) are carried and round-trip; a project from before them loads with every mod at its stock value") {
  Session a;
  scramble(a);
  const Wiring& wa = a.rig.wiring;
  // Every mod away from its stock value (the far end of its range, the near one where the far one is the stock: AMP DECAY).
  for (unsigned id = AcidVoice::AccDecay; id < AcidVoice::Stock; ++id) {
    const core::ParamDesc& d = *a.engine.param_desc(wa.voice, static_cast<std::uint16_t>(id));
    (void)a.engine.set_param(wa.voice, static_cast<std::uint16_t>(id), d.def == d.max ? d.min : d.max);
    REQUIRE(a.engine.param(wa.voice, static_cast<std::uint16_t>(id)) != d.def);
  }
  const std::vector<std::uint8_t> img = encode_session(a);
  const std::vector<SndEntry> entries = snd_entries(img);
  CHECK(voice_entries(entries, AcidVoice::AccDecay, AcidVoice::AmpDecay) == 11);  // all eleven are written
  CHECK(voice_entries(entries, AcidVoice::Volume, AcidVoice::Volume) == 0);       // as before
  CHECK(voice_entries(entries, AcidVoice::Stock, AcidVoice::Stock) == 0);         // Stock never is

  // The round trip: the same values in another session whose mods were elsewhere.
  Session b;
  for (unsigned id = AcidVoice::AccDecay; id < AcidVoice::Stock; ++id) {
    const core::ParamDesc& d = *b.engine.param_desc(b.rig.wiring.voice, static_cast<std::uint16_t>(id));
    (void)b.engine.set_param(b.rig.wiring.voice, static_cast<std::uint16_t>(id), d.def == d.min ? d.max : d.min);
  }
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);
  project::apply(b.rig, b.engine, s);
  for (unsigned id = AcidVoice::AccDecay; id < AcidVoice::Stock; ++id) {
    INFO("voice parameter " << id);
    CHECK(b.engine.param(b.rig.wiring.voice, static_cast<std::uint16_t>(id)) == a.engine.param(wa.voice, static_cast<std::uint16_t>(id)));
  }
  CHECK(encode_session(b) == img);

  // An image from before the mods: no entries for ids 8..18 (and the 303's own are all there).
  const auto before = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (!is(id, "SND ")) {
      return true;
    }
    std::vector<std::uint8_t> out(p.begin(), p.begin() + 4);
    unsigned kept = 0;
    const unsigned n = (static_cast<unsigned>(p[2]) << 8) | p[3];
    for (unsigned i = 0; i < n; ++i) {
      const std::uint8_t* e = &p[4 + i * 8];
      const unsigned pid = (static_cast<unsigned>(e[2]) << 8) | e[3];
      if (e[0] == kLaneVoice && pid >= AcidVoice::AccDecay) {
        continue;
      }
      out.insert(out.end(), e, e + 8);
      ++kept;
    }
    out[2] = static_cast<std::uint8_t>(kept >> 8);
    out[3] = static_cast<std::uint8_t>(kept);
    p = out;
    return true;
  });
  CHECK(voice_entries(snd_entries(before), AcidVoice::AccDecay, AcidVoice::Stock) == 0);
  Session c;
  for (unsigned id = AcidVoice::AccDecay; id < AcidVoice::Stock; ++id) {
    const core::ParamDesc& d = *c.engine.param_desc(c.rig.wiring.voice, static_cast<std::uint16_t>(id));
    (void)c.engine.set_param(c.rig.wiring.voice, static_cast<std::uint16_t>(id), d.def == d.min ? d.max : d.min);
  }
  REQUIRE(load(before, s).status == project::Status::Ok);
  project::apply(c.rig, c.engine, s);
  for (unsigned id = AcidVoice::AccDecay; id < AcidVoice::Stock; ++id) {
    const core::ParamDesc& d = *c.engine.param_desc(c.rig.wiring.voice, static_cast<std::uint16_t>(id));
    INFO("voice parameter " << id);
    CHECK(c.engine.param(c.rig.wiring.voice, static_cast<std::uint16_t>(id)) == d.def);  // the stock value: it sounds as it did
  }
  CHECK(c.engine.param(c.rig.wiring.voice, AcidVoice::Cutoff) == a.engine.param(wa.voice, AcidVoice::Cutoff));  // the 303's own came through
  CHECK(c.engine.param(c.rig.wiring.voice, AcidVoice::Reso) == a.engine.param(wa.voice, AcidVoice::Reso));
}

TEST_CASE("project: Stock is never written, and an entry for it in a file is not applied") {
  Session a;
  const Wiring& wa = a.rig.wiring;
  (void)a.engine.set_param(wa.voice, AcidVoice::Stock, 1.0f);
  REQUIRE(a.engine.param(wa.voice, AcidVoice::Stock) == 1.0f);
  const std::vector<std::uint8_t> img = encode_session(a);
  CHECK(voice_entries(snd_entries(img), AcidVoice::Stock, AcidVoice::Stock) == 0);

  // A file that names it anyway (written by hand, or by a build that carried it): the entry is not looked up.
  const auto forged = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "SND ")) {
      const std::uint8_t e[8] = {kLaneVoice, 0, 0, static_cast<std::uint8_t>(AcidVoice::Stock), 0x3F, 0x80, 0, 0};  // 1.0f
      p.insert(p.end(), e, e + 8);
      const unsigned n = ((static_cast<unsigned>(p[2]) << 8) | p[3]) + 1u;
      p[2] = static_cast<std::uint8_t>(n >> 8);
      p[3] = static_cast<std::uint8_t>(n);
    }
    return true;
  });
  CHECK(voice_entries(snd_entries(forged), AcidVoice::Stock, AcidVoice::Stock) == 1);
  project::ProjectState s;
  REQUIRE(load(forged, s).status == project::Status::Ok);
  Session b;
  REQUIRE(b.engine.param(b.rig.wiring.voice, AcidVoice::Stock) == 0.0f);
  project::apply(b.rig, b.engine, s);
  CHECK(b.engine.param(b.rig.wiring.voice, AcidVoice::Stock) == 0.0f);
}

TEST_CASE("project: a pattern with a hand-built lane on the voice's Stock loads with that lane dropped, the others kept") {
  Session a;
  Pattern p = random_pattern(5);
  p.lanes[2] = LaneRef{kLaneVoice, AcidVoice::Stock};
  p.lanes[3] = LaneRef{kLaneVoice, AcidVoice::AmpDecay};  // the mod before it is a lane like any other
  p.lanes[4] = LaneRef{kLaneVoice, 7};
  (void)a.bank->restore(0, p);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);
  Session b;
  project::apply(b.rig, b.engine, s);
  const Pattern& got = b.bank->pattern(0);
  CHECK(got.lanes[2].target == kLaneNone);
  CHECK(got.lanes[2].id == 0);
  CHECK(got.lanes[3].target == kLaneVoice);
  CHECK(got.lanes[3].id == AcidVoice::AmpDecay);
  CHECK(got.lanes[4].target == kLaneVoice);
  CHECK(got.lanes[4].id == 7);
  for (unsigned i = 0; i < kMotionLanes; ++i) {
    if (i != 2) {
      CHECK(got.lanes[i].target == p.lanes[i].target);
      CHECK(got.lanes[i].id == p.lanes[i].id);
    }
  }
  CHECK(b.engine.param(b.rig.wiring.voice, AcidVoice::Stock) == 0.0f);  // and nothing moved it
}

TEST_CASE("project: a SNAP with fewer values is filled from the sound as loaded") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  constexpr unsigned kFew = 12;
  const auto few = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (!is(id, "SNAP")) {
      return true;
    }
    // slots 8, values 12, used = slots 1 and 3, then 8 x 12 floats: slot s value i = 100 s + i
    std::vector<std::uint8_t> out = {p[0], p[1], 8, kFew, 0x0A};
    for (unsigned s = 0; s < 8; ++s) {
      for (unsigned i = 0; i < kFew; ++i) {
        const float v = static_cast<float>(100 * s + i);
        std::uint32_t bits;
        std::memcpy(&bits, &v, 4);
        for (int sh = 24; sh >= 0; sh -= 8) {
          out.push_back(static_cast<std::uint8_t>(bits >> sh));
        }
      }
    }
    p = out;
    return true;
  });
  project::ProjectState s;
  REQUIRE(load(few, s).status == project::Status::Ok);
  CHECK(s.snap_values == kFew);
  CHECK(s.snap_used == 0x0A);

  Session b;
  project::apply(b.rig, b.engine, s);
  CHECK_FALSE(b.rig.wiring.snaps.used(0));
  REQUIRE(b.rig.wiring.snaps.used(1));
  REQUIRE(b.rig.wiring.snaps.used(3));
  float live[kSnapValues];
  b.rig.controls.snap_capture(b.engine, live);
  // The values of the slot, then the sound as loaded: read back through the gather.
  const project::ProjectState g = b.gather();
  CHECK(g.snap_used == 0x0A);
  for (unsigned i = 0; i < kSnapTargetCount; ++i) {
    CHECK(g.snap[1][i] == (i < kFew ? static_cast<float>(100 + i) : live[i]));
    CHECK(g.snap[3][i] == (i < kFew ? static_cast<float>(300 + i) : live[i]));
  }
  // And the sound it is filled from is the project's: the loaded voice's cutoff is in the slot's tail only through the targets above kFew.
  CHECK(live[16] == a.rig.wiring.level_db);  // target 16 is the level
}

TEST_CASE("project: a SNAP saved before the mods (22 values a slot) loads, and the mods' and the growl's new targets 22..35 are filled from the sound as loaded") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  constexpr unsigned kOld = 22;  // the targets of kSnapTargetCount before MODS S2 (docs/MODS-PLAN.md 5.9)
  static_assert(kSnapTargetCount == kOld + 14 + 6 + 2, "MODS S2 appended eleven mods and FMT MIX, CMB TUNE, CMB FB, B5a the wobble's five settings and its latch, the voice rewrite RANGE and SUSTAIN");
  const auto old = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (!is(id, "SNAP")) {
      return true;
    }
    // slots 8, values 22, used = slots 0 and 6, then 8 x 22 floats: slot s value i = 100 s + i
    std::vector<std::uint8_t> out = {p[0], p[1], 8, kOld, 0x41};
    for (unsigned s = 0; s < 8; ++s) {
      for (unsigned i = 0; i < kOld; ++i) {
        const float v = static_cast<float>(100 * s + i);
        std::uint32_t bits;
        std::memcpy(&bits, &v, 4);
        for (int sh = 24; sh >= 0; sh -= 8) {
          out.push_back(static_cast<std::uint8_t>(bits >> sh));
        }
      }
    }
    p = out;
    return true;
  });
  project::ProjectState s;
  REQUIRE(load(old, s).status == project::Status::Ok);
  CHECK(s.snap_values == kOld);
  Session b;
  project::apply(b.rig, b.engine, s);
  REQUIRE(b.rig.wiring.snaps.used(0));
  REQUIRE(b.rig.wiring.snaps.used(6));
  float live[kSnapValues];
  b.rig.controls.snap_capture(b.engine, live);
  const project::ProjectState g = b.gather();
  for (unsigned i = 0; i < kSnapTargetCount; ++i) {
    CHECK(g.snap[0][i] == (i < kOld ? static_cast<float>(i) : live[i]));
    CHECK(g.snap[6][i] == (i < kOld ? static_cast<float>(600 + i) : live[i]));
  }
  // The sound the new targets are filled from is the project's: the scrambled voice's ACC DECAY is target 22.
  CHECK(live[22] == doctest::Approx(a.engine.param(a.rig.wiring.voice, AcidVoice::AccDecay)));
}

TEST_CASE("project: a SNAP with more values than this build holds (an image of a later build) keeps the first kSnapValues of every slot and reads on in step") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  constexpr unsigned kMore = kSnapValues + 6;  // what a build with six more targets would write
  const auto more = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (!is(id, "SNAP")) {
      return true;
    }
    std::vector<std::uint8_t> out = {p[0], p[1], 8, kMore, 0xFF};
    for (unsigned s = 0; s < 8; ++s) {
      for (unsigned i = 0; i < kMore; ++i) {
        const float v = static_cast<float>(100 * s + i);
        std::uint32_t bits;
        std::memcpy(&bits, &v, 4);
        for (int sh = 24; sh >= 0; sh -= 8) {
          out.push_back(static_cast<std::uint8_t>(bits >> sh));
        }
      }
    }
    p = out;
    return true;
  });
  project::ProjectState s;
  REQUIRE(load(more, s).status == project::Status::Ok);
  CHECK(s.snap_values == kSnapValues);  // the bound is the build's (an older image's clamp: it keeps what it has room for)
  for (unsigned slot = 0; slot < 8; ++slot) {  // the extra values did not push the next slot out of place
    for (unsigned i = 0; i < kSnapValues; ++i) {
      CHECK(s.snap[slot][i] == static_cast<float>(100 * slot + i));
    }
  }
}

TEST_CASE("project: a SNAP saved before RANGE and SUSTAIN (42 values a slot) loads, and the last two targets, 42 and 43, are filled from the sound as loaded") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  constexpr unsigned kOld = 42;
  static_assert(kSnapTargetCount == kOld + 2, "the voice rewrite appended RANGE and SUSTAIN as targets 42 and 43");
  const auto old = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (!is(id, "SNAP")) {
      return true;
    }
    std::vector<std::uint8_t> out = {p[0], p[1], 8, kOld, 0x41};
    for (unsigned s = 0; s < 8; ++s) {
      for (unsigned i = 0; i < kOld; ++i) {
        const float v = static_cast<float>(100 * s + i);
        std::uint32_t bits;
        std::memcpy(&bits, &v, 4);
        for (int sh = 24; sh >= 0; sh -= 8) {
          out.push_back(static_cast<std::uint8_t>(bits >> sh));
        }
      }
    }
    p = out;
    return true;
  });
  project::ProjectState s;
  REQUIRE(load(old, s).status == project::Status::Ok);
  CHECK(s.snap_values == kOld);
  Session b;
  project::apply(b.rig, b.engine, s);
  REQUIRE(b.rig.wiring.snaps.used(0));
  float live[kSnapValues];
  b.rig.controls.snap_capture(b.engine, live);
  const project::ProjectState g = b.gather();
  for (unsigned i = 0; i < kSnapTargetCount; ++i) {
    CHECK(g.snap[0][i] == (i < kOld ? static_cast<float>(i) : live[i]));
    CHECK(g.snap[6][i] == (i < kOld ? static_cast<float>(600 + i) : live[i]));
  }
  // The sound they are filled from is the project's: RANGE and SUSTAIN as loaded (the scramble leaves them at their defaults).
  CHECK(live[42] == doctest::Approx(a.engine.param(a.rig.wiring.voice, AcidVoice::Range)));
  CHECK(live[43] == doctest::Approx(a.engine.param(a.rig.wiring.voice, AcidVoice::Sustain)));
}

TEST_CASE("project: a project saved without RANGE and SUSTAIN (no SND entry for them) loads them at their defaults") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  Session b;
  b.engine.set_param(b.rig.wiring.voice, AcidVoice::Range, 1.0f);
  b.engine.set_param(b.rig.wiring.voice, AcidVoice::Sustain, 0.8f);
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);
  // The file of the voice before the rewrite: drop the voice's entries for ids 20 and 21 (the voice's target is kLaneVoice).
  unsigned kept = 0;
  for (unsigned i = 0; i < s.sound_n; ++i) {
    const project::SoundEntry& en = s.sound[i];
    if (en.target == kLaneVoice && (en.id == AcidVoice::Range || en.id == AcidVoice::Sustain)) {
      continue;
    }
    s.sound[kept++] = en;
  }
  s.sound_n = kept;
  project::apply(b.rig, b.engine, s);
  CHECK(b.engine.param(b.rig.wiring.voice, AcidVoice::Range) == 0.0f);
  CHECK(b.engine.param(b.rig.wiring.voice, AcidVoice::Sustain) == 0.0f);
}

TEST_CASE("project: a SNAP saved before P2 (18 values a slot) loads, and the growl's four targets are filled from the sound as loaded") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  constexpr unsigned kOld = 18;  // the targets of kSnapTargetCount before P2: SOUND, distortion, delay, level, send
  static_assert(kSnapTargetCount == kOld + 4 + 14 + 6 + 2, "P2 appended VOWEL, FMT Q, GRL MIX and CMB MIX, MODS S2 the eleven mods and FMT MIX, CMB TUNE, CMB FB, B5a the wobble's six, the voice rewrite RANGE and SUSTAIN");
  const auto old = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (!is(id, "SNAP")) {
      return true;
    }
    // slots 8, values 18, used = slots 0 and 6, then 8 x 18 floats: slot s value i = 100 s + i
    std::vector<std::uint8_t> out = {p[0], p[1], 8, kOld, 0x41};
    for (unsigned s = 0; s < 8; ++s) {
      for (unsigned i = 0; i < kOld; ++i) {
        const float v = static_cast<float>(100 * s + i);
        std::uint32_t bits;
        std::memcpy(&bits, &v, 4);
        for (int sh = 24; sh >= 0; sh -= 8) {
          out.push_back(static_cast<std::uint8_t>(bits >> sh));
        }
      }
    }
    p = out;
    return true;
  });
  project::ProjectState s;
  REQUIRE(load(old, s).status == project::Status::Ok);
  CHECK(s.snap_values == kOld);
  CHECK(s.snap_used == 0x41);

  Session b;
  project::apply(b.rig, b.engine, s);
  REQUIRE(b.rig.wiring.snaps.used(0));
  REQUIRE(b.rig.wiring.snaps.used(6));
  CHECK_FALSE(b.rig.wiring.snaps.used(1));
  float live[kSnapValues];
  b.rig.controls.snap_capture(b.engine, live);
  const project::ProjectState g = b.gather();
  for (unsigned i = 0; i < kSnapTargetCount; ++i) {
    // The old targets keep what the file held; the new ones are the sound as loaded (the growl's vowel, Q, mix and comb mix of the project, then the mods').
    CHECK(g.snap[0][i] == (i < kOld ? static_cast<float>(i) : live[i]));
    CHECK(g.snap[6][i] == (i < kOld ? static_cast<float>(600 + i) : live[i]));
  }
  CHECK(live[18] == doctest::Approx(a.engine.param(a.rig.wiring.growl, Growl::Vowel)));  // the saved sound's, scrambled away from the defaults
  CHECK(live[21] == doctest::Approx(a.engine.param(a.rig.wiring.growl, Growl::CombMix)));
  CHECK(live[18] != doctest::Approx(0.0f));
  // Written again, the file has all the targets.
  const std::vector<std::uint8_t> again = encode_session(b);
  project::ProjectState s2;
  REQUIRE(load(again, s2).status == project::Status::Ok);
  CHECK(s2.snap_values == kSnapTargetCount);
}

TEST_CASE("project: B5a, the wobble's settings and its latch round-trip exact, and a project of the format before loads with the new defaults") {
  Session a;
  scramble(a);
  a.rig.wiring.wobble = WobbleSettings{5, 1, 33, true, kWobbleCutoff};
  a.rig.wiring.wob_want = 1;
  const std::vector<std::uint8_t> img = encode_session(a);
  Session b;
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);
  project::apply(b.rig, b.engine, s);
  const WobbleSettings& wb = b.rig.wiring.wobble;
  CHECK(wb.rate == 5);
  CHECK(wb.shape == 1);
  CHECK(wb.depth_pct == 33);
  CHECK(wb.reset);
  CHECK(wb.target == kWobbleCutoff);
  CHECK(b.rig.wiring.wob_want == 1);
  CHECK(encode_session(b) == img);
  // The latch off round-trips as off, and a load takes the old latch away.
  a.rig.wiring.wob_want = 0;
  const std::vector<std::uint8_t> off = encode_session(a);
  REQUIRE(load(off, s).status == project::Status::Ok);
  project::apply(b.rig, b.engine, s);
  CHECK(b.rig.wiring.wob_want == 0);

  // The image of the format before B5a, from real bytes: no SND entry for the latch (app id 9) and 36 values a SNAP slot.
  const auto old = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "SND ")) {
      std::vector<std::uint8_t> out(p.begin(), p.begin() + 4);
      unsigned kept = 0;
      const unsigned n = (static_cast<unsigned>(p[2]) << 8) | p[3];
      for (unsigned i = 0; i < n; ++i) {
        const std::uint8_t* e = &p[4 + i * 8];
        const unsigned pid = (static_cast<unsigned>(e[2]) << 8) | e[3];
        if (e[0] == project::kSndApp && pid == project::kAppWobbleOn) {
          continue;
        }
        out.insert(out.end(), e, e + 8);
        ++kept;
      }
      out[2] = static_cast<std::uint8_t>(kept >> 8);
      out[3] = static_cast<std::uint8_t>(kept);
      p = out;
    } else if (is(id, "SNAP")) {
      constexpr unsigned kBefore = 36;
      const unsigned n = p[3];
      std::vector<std::uint8_t> out = {p[0], p[1], p[2], kBefore, p[4]};
      for (unsigned slot = 0; slot < p[2]; ++slot) {
        const auto first = p.begin() + 5 + static_cast<std::ptrdiff_t>(slot * n * 4);
        out.insert(out.end(), first, first + kBefore * 4);
      }
      p = out;
    }
    return true;
  });
  CHECK(project::kAppWobbleOn == project::kAppValueCount - 1u);
  Session c;
  c.rig.wiring.wob_want = 1;  // a session with the latch on: the old project has none, so it ends off
  REQUIRE(load(old, s).status == project::Status::Ok);
  CHECK(s.snap_values == 36);
  project::apply(c.rig, c.engine, s);
  CHECK(c.rig.wiring.wob_want == 0);
  CHECK(c.rig.wiring.wobble.rate == 5);  // what the file held stays
  float live[kSnapValues];
  c.rig.controls.snap_capture(c.engine, live);
  const project::ProjectState g = c.gather();
  for (unsigned slot : {0u, 3u, 7u}) {
    for (unsigned i = kSnapWobbleFirst; i < kSnapTargetCount; ++i) {
      CHECK(g.snap[slot][i] == live[i]);  // the six new targets are the sound as loaded
    }
  }
  // A project with no SND at all: every default is the new one.
  const auto nosnd = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>&) { return !is(id, "SND "); });
  REQUIRE(load(nosnd, s).status == project::Status::Ok);
  project::apply(c.rig, c.engine, s);
  CHECK_FALSE(c.rig.wiring.wobble.reset);                                  // RESET defaults off
  CHECK(c.engine.param(c.rig.wiring.growl, Growl::CombTune) == 7.0f);       // CMB TUNE defaults to +7 st
  CHECK(c.rig.wiring.wob_want == 0);
  CHECK_FALSE(c.sequencer->wobble().reset_on_note);
}

TEST_CASE("project: gathering a session leaves the snapshots' morph alone") {
  Session a;
  scramble(a);
  float live[kSnapValues] = {};
  REQUIRE(a.rig.wiring.snaps.recall(3, live));
  a.rig.wiring.snaps.turn(-5, false);
  const unsigned pos = a.rig.wiring.snaps.position();
  const int last = a.rig.wiring.snaps.last();
  const float v = a.rig.wiring.snaps.value(2, false);
  const project::ProjectState s = a.gather();
  CHECK(a.rig.wiring.snaps.position() == pos);
  CHECK(a.rig.wiring.snaps.last() == last);
  CHECK(a.rig.wiring.snaps.value(2, false) == v);
  CHECK(s.snap_used == ((1u << 0) | (1u << 3) | (1u << 7)));
  // The slot's own values, not the morph's blend.
  CHECK(s.snap[3][2] == doctest::Approx(30.0f + 2.0f * 0.37f - 1.0f));
}

TEST_CASE("project: patterns of another size or fewer of them; out of range fields are brought into range") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;

  // PATS with another pattern size: the part is skipped, every pattern is the fresh one.
  const auto other = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PATS")) {
      p[4] = static_cast<std::uint8_t>(p[4] + 1);  // bytes a pattern 2,261
    }
    return true;
  });
  const project::Result r1 = load(other, s);
  REQUIRE(r1.status == project::Status::Ok);
  CHECK(r1.skipped == 1);
  CHECK(s.pats == nullptr);
  Session b;
  project::apply(b.rig, b.engine, s);
  Pattern fresh;
  clear(fresh);
  for (unsigned i = 0; i < kPatternCount; ++i) {
    CHECK(same_pattern(b.bank->pattern(i), fresh));
  }

  // Fewer patterns than 48 (here 5): the others are fresh.
  const auto few = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PATS")) {
      p[2] = 5;
      p.resize(5 + 5 * sizeof(Pattern));
    }
    return true;
  });
  REQUIRE(load(few, s).status == project::Status::Ok);
  CHECK(s.pats_n == 5);
  project::apply(b.rig, b.engine, s);
  for (unsigned i = 0; i < kPatternCount; ++i) {
    CHECK(same_pattern(b.bank->pattern(i), i < 5 ? a.bank->pattern(i) : fresh));
  }

  // A pattern chunk shorter than it says is skipped.
  const auto cut = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PATS")) {
      p.resize(p.size() - 10);
    }
    return true;
  });
  const project::Result r3 = load(cut, s);
  REQUIRE(r3.status == project::Status::Ok);
  CHECK(r3.skipped == 1);
  CHECK(s.pats == nullptr);

  // Fields a sequencer indexes with, out of range in a file whose CRC is good: clamped (a length of 7, a note of 200, a lane of target 99, ...).
  Pattern bad = random_pattern(3);
  bad.length = 7;
  bad.steps[2].note = 200;
  bad.steps[3].flags = 0xFF;
  bad.swing = 200;
  bad.transpose = -100;
  bad.gate = 0;
  bad.lanes[1].target = 99;
  (void)a.bank->restore(0, bad);
  const auto hostile = rebuild(encode_session(a), [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "HEAD")) {
      p[2] = 200;  // selected pattern 200
    }
    if (is(id, "GEN ")) {
      p[2] = 200;  // scale 200
      p[4] = 200;  // low above high
      p[5] = 9;
      p[6] = 250;  // density
    }
    return true;
  });
  REQUIRE(load(hostile, s).status == project::Status::Ok);
  CHECK(s.selected == 0);  // a pattern this build has not: the first
  CHECK(s.gen.scale == seq::GenParams{}.scale);
  CHECK(s.gen.density == 100);
  CHECK(s.gen.low <= s.gen.high);
  project::apply(b.rig, b.engine, s);
  const Pattern& p0 = b.bank->pattern(0);
  CHECK(p0.length == kDefaultLength);
  CHECK(p0.steps[2].note == 127);
  CHECK(p0.steps[3].flags == 0x3F);
  CHECK(p0.swing == kMaxSwing);
  CHECK(p0.transpose == -24);
  CHECK(p0.gate == 5);
  CHECK(p0.lanes[1].target == kLaneNone);
  CHECK(b.bank->selected() == 0);
}

TEST_CASE("project: the image fits its buffer, at the fullest too, and an apply leaves no history") {
  Session a;
  scramble(a);
  project::ProjectState s = a.gather();
  const std::size_t typical = project::encode(s, *a.bank, project::buffer(), project::kProjectBytes);
  CHECK(typical > 108'000);
  CHECK(typical < 112'000);

  // SND full: still inside the buffer, with the song's reserve left over.
  s.sound_n = project::kMaxSound;
  const std::size_t full = project::encode(s, *a.bank, project::buffer(), project::kProjectBytes);
  CHECK(full > typical);
  CHECK(full + 4096 <= project::kProjectBytes);
  project::ProjectState back;
  CHECK(project::decode(project::buffer(), full, back).status == project::Status::Ok);
  CHECK(back.sound_n == project::kMaxSound);

  // Too small a buffer is refused whole, not cut.
  std::vector<std::uint8_t> small(typical - 1);
  CHECK(project::encode(s, *a.bank, small.data(), small.size()) == 0);

  // An undo step of the old project is not there after a load.
  Session b;
  CHECK(b.bank->set_note(2, 0, 50));
  REQUIRE(b.bank->can_undo());
  s.sound_n = 0;
  const std::size_t again = project::encode(s, *a.bank, project::buffer(), project::kProjectBytes);
  REQUIRE(project::decode(project::buffer(), again, back).status == project::Status::Ok);
  project::apply(b.rig, b.engine, back);
  CHECK_FALSE(b.bank->can_undo());
}

TEST_CASE("project: a default state is a new project; the name is cut and cleaned") {
  Session a;
  scramble(a);
  const project::ProjectState fresh;  // as NEW makes it
  std::vector<std::uint8_t> img(project::kProjectBytes);
  const std::size_t n = project::encode(fresh, *a.bank, img.data(), img.size());
  REQUIRE(n > 0);
  project::ProjectState s;
  REQUIRE(project::decode(img.data(), n, s).status == project::Status::Ok);
  CHECK(s.sound_n == 0);
  CHECK(s.snap_used == 0);
  CHECK(s.tempo_mbpm == 125'000);
  CHECK(s.perf[0] == kDefaultPerf[0]);
  CHECK(s.name[0] == '\0');

  // A name longer than fifteen characters is cut at fifteen; a name with a control byte reads back printable.
  project::ProjectState named = fresh;
  std::memset(named.name, 'N', sizeof named.name);  // no terminator at all
  const std::size_t n2 = project::encode(named, *a.bank, img.data(), img.size());
  REQUIRE(n2 > 0);
  REQUIRE(project::decode(img.data(), n2, s).status == project::Status::Ok);
  CHECK(std::strlen(s.name) == project::kNameChars - 1u);
  const auto odd = rebuild(std::vector<std::uint8_t>(img.begin(), img.begin() + static_cast<std::ptrdiff_t>(n2)), [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "HEAD")) {
      p[8] = 0x07;  // the first character
    }
    return true;
  });
  REQUIRE(load(odd, s).status == project::Status::Ok);
  CHECK(s.name[0] == '_');
}

TEST_CASE("project: the encode and decode times on the host (the unit's are F2's check)") {
  Session a;
  scramble(a);
  project::ProjectState s = a.gather();
  std::snprintf(s.name, sizeof s.name, "TIMING");
  using Clock = std::chrono::steady_clock;
  constexpr int kReps = 40;
  std::size_t n = 0;
  const auto t0 = Clock::now();
  for (int i = 0; i < kReps; ++i) {
    n = project::encode(s, *a.bank, project::buffer(), project::kProjectBytes);
  }
  const auto t1 = Clock::now();
  project::ProjectState back;
  project::Result res;
  for (int i = 0; i < kReps; ++i) {
    res = project::decode(project::buffer(), n, back);
  }
  const auto t2 = Clock::now();
  Session b;
  for (int i = 0; i < kReps; ++i) {
    project::apply(b.rig, b.engine, back);
  }
  const auto t3 = Clock::now();
  REQUIRE(res.status == project::Status::Ok);
  const auto us = [](Clock::time_point x, Clock::time_point y) { return std::chrono::duration<double, std::micro>(y - x).count() / kReps; };
  std::printf("project codec, %zu bytes: encode %.0f us, decode %.0f us, apply %.0f us (host, mean of %d)\n", n, us(t0, t1), us(t1, t2), us(t2, t3), kReps);
  CHECK(n > 0);
}

// ---- The hardening round: what a file with a good CRC can still say ------------------------------------------------------------------------------

namespace {

void put_be32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
  for (int i = 0; i < 4; ++i) {
    v[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(x >> (24 - 8 * i));
  }
}

void push_f32(std::vector<std::uint8_t>& v, float f) {
  std::uint32_t bits;
  std::memcpy(&bits, &f, 4);
  for (int sh = 24; sh >= 0; sh -= 8) {
    v.push_back(static_cast<std::uint8_t>(bits >> sh));
  }
}

// An SND payload of `n` entries of the voice's Cutoff (and the header's count `claimed`).
std::vector<std::uint8_t> sound_payload(unsigned n, unsigned claimed, float value) {
  std::vector<std::uint8_t> p = {1, 0, static_cast<std::uint8_t>(claimed >> 8), static_cast<std::uint8_t>(claimed)};
  for (unsigned i = 0; i < n; ++i) {
    p.insert(p.end(), {kLaneVoice, 0, 0, static_cast<std::uint8_t>(AcidVoice::Cutoff)});
    push_f32(p, value);
  }
  return p;
}

}  // namespace

TEST_CASE("project: patterns_ok says whether the file's patterns were read") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  CHECK(load(img, s).patterns_ok);

  const auto other = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PATS")) {
      p[4] = static_cast<std::uint8_t>(p[4] + 1);
    }
    return true;
  });
  project::Result r = load(other, s);
  CHECK(r.status == project::Status::Ok);
  CHECK_FALSE(r.patterns_ok);

  const auto newer = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PATS")) {
      p[0] = 2;
    }
    return true;
  });
  r = load(newer, s);
  CHECK(r.status == project::Status::Ok);
  CHECK_FALSE(r.patterns_ok);
  CHECK(r.skipped == 1);

  const auto none = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>&) { return !is(id, "PATS"); });
  CHECK_FALSE(load(none, s).patterns_ok);
}

TEST_CASE("project: an encode between decode and apply makes apply refuse, and touch nothing") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);

  // The live session's patterns are what an apply over the overwritten buffer would restore: it must not.
  Session b;
  CHECK(b.bank->set_note(4, 0, 55));
  const Pattern before = b.bank->pattern(4);
  const std::uint32_t version = b.bank->version(4);
  const float cutoff = b.engine.param(b.rig.wiring.voice, AcidVoice::Cutoff);
  (void)encode_session(b, "OTHER");  // another encode, as an autosave would be
  CHECK_FALSE(project::apply(b.rig, b.engine, s));
  CHECK(same_pattern(b.bank->pattern(4), before));
  CHECK(b.bank->version(4) == version);
  CHECK(b.bank->can_undo());
  CHECK(b.engine.param(b.rig.wiring.voice, AcidVoice::Cutoff) == cutoff);

  // Decoding again makes it good; a state with no patterns (a new project) is never outdated.
  REQUIRE(load(img, s).status == project::Status::Ok);
  CHECK(project::apply(b.rig, b.engine, s));
  CHECK(same_pattern(b.bank->pattern(4), a.bank->pattern(4)));
  const project::ProjectState fresh;
  (void)encode_session(b, "AGAIN");
  CHECK(project::apply(b.rig, b.engine, fresh));
}

TEST_CASE("project: short and oversized payloads skip their part and nothing else") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  const seq::GenParams def;

  // Short GEN, SND, SNAP: skipped, the rest is read.
  const auto shorts = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "GEN ")) {
      p.resize(5);
    } else if (is(id, "SND ")) {
      p.resize(4 + 8 * 3 + 5);  // says 41 entries, holds three and a bit
    } else if (is(id, "SNAP")) {
      p.resize(p.size() - 7);
    }
    return true;
  });
  project::Result r = load(shorts, s);
  REQUIRE(r.status == project::Status::Ok);
  CHECK(r.skipped == 3);
  CHECK(s.gen.density == def.density);
  CHECK(s.sound_n == 0);
  CHECK(s.snap_used == 0);
  CHECK(s.selected == 17);
  CHECK(r.patterns_ok);
  CHECK(s.perf[1] == static_cast<std::uint8_t>(PerfAction::Wobble));

  // A count that the payload cannot hold: SND n = 65535, SNAP 255 slots of 255 values.
  const auto big = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "SND ")) {
      p[2] = 0xFF;
      p[3] = 0xFF;
    } else if (is(id, "SNAP")) {
      p[2] = 255;
      p[3] = 255;
    }
    return true;
  });
  r = load(big, s);
  REQUIRE(r.status == project::Status::Ok);
  CHECK(r.skipped == 2);
  CHECK(s.sound_n == 0);
  CHECK(s.snap_used == 0);

  // PERF: a short one gives the buttons it has and keeps the others' defaults; one with half a version is skipped.
  const auto perf2 = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PERF")) {
      p.resize(4);
    }
    return true;
  });
  r = load(perf2, s);
  REQUIRE(r.status == project::Status::Ok);
  CHECK(s.perf[0] == static_cast<std::uint8_t>(PerfAction::Reverse));
  CHECK(s.perf[1] == static_cast<std::uint8_t>(PerfAction::Wobble));
  CHECK(s.perf[2] == kDefaultPerf[2]);
  CHECK(s.perf[3] == kDefaultPerf[3]);
  const auto perf1 = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PERF")) {
      p.resize(1);
    }
    return true;
  });
  r = load(perf1, s);
  REQUIRE(r.status == project::Status::Ok);
  CHECK(r.skipped == 1);
  CHECK(s.perf[0] == kDefaultPerf[0]);

  // PATS: a count above 48 (here 52) with the bytes for it keeps 48; a count the payload cannot hold is skipped.
  const auto many = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PATS")) {
      p[2] = 52;
      p.resize(p.size() + 4 * sizeof(Pattern), 0x55);
    }
    return true;
  });
  r = load(many, s);
  REQUIRE(r.status == project::Status::Ok);
  CHECK(r.patterns_ok);
  CHECK(s.pats_n == kPatternCount);
  const auto toomany = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PATS")) {
      p[2] = 255;
    }
    return true;
  });
  r = load(toomany, s);
  REQUIRE(r.status == project::Status::Ok);
  CHECK_FALSE(r.patterns_ok);
  CHECK(r.skipped == 1);
}

TEST_CASE("project: a name with no NUL, more entries than a state holds, and values that are not numbers") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;

  const auto name = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "HEAD")) {
      for (std::size_t i = 8; i < 8 + project::kNameChars; ++i) {
        p[i] = 'N';
      }
    }
    return true;
  });
  REQUIRE(load(name, s).status == project::Status::Ok);
  CHECK(std::strlen(s.name) == project::kNameChars - 1u);

  // 200 entries: 128 are kept, 72 counted as dropped.
  const auto many = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "SND ")) {
      p = sound_payload(200, 200, 1234.0f);
    }
    return true;
  });
  project::Result r = load(many, s);
  REQUIRE(r.status == project::Status::Ok);
  CHECK(s.sound_n == project::kMaxSound);
  CHECK(r.skipped == 72);

  // NaN and Inf in SND are dropped by decode (the parameter keeps its default), in SNAP they are NaN: filled from the sound.
  const auto inf = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "SND ")) {
      p = sound_payload(1, 1, std::numeric_limits<float>::infinity());
      p.insert(p.end(), {kLaneVoice, 0, 0, static_cast<std::uint8_t>(AcidVoice::Decay)});
      push_f32(p, -std::numeric_limits<float>::infinity());
      p.insert(p.end(), {project::kSndApp, 0, 0, static_cast<std::uint8_t>(project::kAppSend)});
      push_f32(p, std::numeric_limits<float>::quiet_NaN());
      p[3] = 3;
    } else if (is(id, "SNAP")) {
      // slot 0, value 2 (the first of the file's floats is at 5)
      const float v = std::numeric_limits<float>::infinity();
      std::uint32_t bits;
      std::memcpy(&bits, &v, 4);
      for (int i = 0; i < 4; ++i) {
        p[5 + 2 * 4 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(bits >> (24 - 8 * i));
      }
    }
    return true;
  });
  r = load(inf, s);
  REQUIRE(r.status == project::Status::Ok);
  CHECK(r.skipped == 3);
  CHECK(s.sound_n == 0);
  CHECK(std::isnan(s.snap[0][2]));
  Session b;
  (void)b.engine.set_param(b.rig.wiring.voice, AcidVoice::Cutoff, 900.0f);
  b.rig.wiring.send_a_pct = 77.0f;
  REQUIRE(project::apply(b.rig, b.engine, s));
  CHECK(b.engine.param(b.rig.wiring.voice, AcidVoice::Cutoff) == b.engine.param_desc(b.rig.wiring.voice, AcidVoice::Cutoff)->def);
  CHECK(b.rig.wiring.send_a_pct == Wiring{}.send_a_pct);
  float live[kSnapValues];
  b.rig.controls.snap_capture(b.engine, live);
  const project::ProjectState g = b.gather();
  CHECK(std::isfinite(g.snap[0][2]));
  CHECK(g.snap[0][2] == live[2]);  // the Inf became the sound's value
  CHECK(g.snap[0][3] == doctest::Approx(-1.0f + 3.0f * 0.37f));  // its neighbours are the file's
}

TEST_CASE("project: the tempo and the selected pattern come out in range") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  const auto head = [&](std::uint32_t tempo, std::uint8_t sel) {
    return rebuild(img, [=](char (&id)[4], std::vector<std::uint8_t>& p) {
      if (is(id, "HEAD")) {
        put_be32(p, 3, tempo);
        p[2] = sel;
      }
      return true;
    });
  };
  REQUIRE(load(head(0, 200), s).status == project::Status::Ok);
  CHECK(s.tempo_mbpm == dsp::kMinTempoMilliBpm);
  CHECK(s.selected == 0);
  REQUIRE(load(head(0xFFFFFFFFu, 47), s).status == project::Status::Ok);
  CHECK(s.tempo_mbpm == dsp::kMaxTempoMilliBpm);
  CHECK(s.selected == 47);
  REQUIRE(load(head(125'000, 0), s).status == project::Status::Ok);
  CHECK(s.tempo_mbpm == 125'000);
}

TEST_CASE("project: a container with no CRC chunk, or one that is not last, is damaged") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;

  std::vector<std::uint8_t> nocrc(img.begin(), img.end() - static_cast<std::ptrdiff_t>(chunk::kCrcChunkBytes));
  put_be32(nocrc, 4, static_cast<std::uint32_t>(nocrc.size() - 8));
  CHECK(load(nocrc, s).status == project::Status::Damaged);

  std::vector<std::uint8_t> notlast = img;
  notlast.insert(notlast.end(), {'Z', 'Z', 'Z', 'Z', 0, 0, 0, 2, 0, 0});
  put_be32(notlast, 4, static_cast<std::uint32_t>(notlast.size() - 8));
  CHECK(load(notlast, s).status == project::Status::Damaged);

  // The two CRC chunks (a doubled one) are no better.
  std::vector<std::uint8_t> twice = img;
  twice.insert(twice.end(), img.end() - static_cast<std::ptrdiff_t>(chunk::kCrcChunkBytes), img.end());
  put_be32(twice, 4, static_cast<std::uint32_t>(twice.size() - 8));
  CHECK(load(twice, s).status == project::Status::Damaged);
}

TEST_CASE("project: an autosave gathered during a tempo nudge stores the tempo the nudge started from") {
  Session a;
  (void)a.engine.set_tempo(125'000);
  CHECK(a.gather().tempo_mbpm == 125'000u);
  a.rig.tempo_base_mbpm = 125'000;  // MASTER's nudge: the engine holds the nudged tempo, the rig the base
  (void)a.engine.set_tempo(128'750);
  CHECK(a.gather().tempo_mbpm == 125'000u);
  a.rig.tempo_base_mbpm = 0;  // the nudge ended
  (void)a.engine.set_tempo(125'000);
  CHECK(a.gather().tempo_mbpm == 125'000u);
}

// F3 review: the save counter of HEAD 1.1 (boot and LOAD take the newest of live.o3a and project.o3a by it).
TEST_CASE("project: HEAD 1.1 carries the save counter; a 1.0 HEAD reads as 0; small_crc ignores it") {
  Session a;
  scramble(a);
  project::ProjectState st = a.gather();
  std::snprintf(st.name, sizeof st.name, "%s", "JAM ONE");
  st.save_count = 0xA1B2C3D4u;
  std::vector<std::uint8_t> img(project::kProjectBytes);
  const std::size_t n = project::encode(st, *a.bank, img.data(), img.size());
  REQUIRE(n > 0);
  img.resize(n);
  project::ProjectState back;
  REQUIRE(load(img, back).status == project::Status::Ok);
  CHECK(back.save_count == 0xA1B2C3D4u);

  // A HEAD of version 1.0 ends at the name: the counter is 0 and the rest is read as before.
  const auto old = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "HEAD")) {
      p[1] = 0;  // minor 0
      p.resize(p.size() - 4);
    }
    return true;
  });
  project::ProjectState o;
  REQUIRE(load(old, o).status == project::Status::Ok);
  CHECK(o.save_count == 0u);
  CHECK(o.tempo_mbpm == back.tempo_mbpm);
  CHECK(std::string(o.name) == "JAM ONE");

  // The counter moves with every write: it is no change of the session.
  project::ProjectState other = st;
  other.save_count = 7;
  CHECK(project::small_crc(st) == project::small_crc(other));
}

TEST_CASE("project: the ladder rows round-trip; a PERF chunk of the earlier format (the buttons only) loads with the default rows; bad rows fall back") {
  Session a;
  scramble(a);
  a.rig.wiring.ladder[kLadderHome3] = LaneRef{kLaneDist, 2};
  a.rig.wiring.ladder[kLadderHome4] = LaneRef{kLaneVoice, 12};
  a.rig.wiring.ladder[kLadderPerform] = LaneRef{kLaneDelay, 1};
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);
  CHECK(s.ladder[0].target == kLaneDist);
  CHECK(s.ladder[0].id == 2);
  CHECK(s.ladder[1].target == kLaneVoice);
  CHECK(s.ladder[1].id == 12);
  CHECK(s.ladder[2].target == kLaneDelay);
  CHECK(s.ladder[2].id == 1);
  Session b;
  REQUIRE(project::apply(b.rig, b.engine, s));
  for (unsigned r = 0; r < kLadderRows; ++r) {
    CHECK(b.rig.wiring.ladder[r].target == a.rig.wiring.ladder[r].target);
    CHECK(b.rig.wiring.ladder[r].id == a.rig.wiring.ladder[r].id);
  }

  // The image as the build before B4 wrote it: the PERF chunk is the version and the four buttons, byte for byte, nothing after them.
  std::size_t perf_now = 0;
  const auto old = rebuild(img, [&](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PERF")) {
      perf_now = p.size();
      const std::uint8_t earlier[2 + kPerfButtons] = {p[0], p[1], 1, 2, 3, 0};
      p.assign(earlier, earlier + sizeof earlier);
    }
    return true;
  });
  REQUIRE(perf_now == 2 + kPerfButtons + 2 * kLadderRows);
  project::ProjectState o;
  const project::Result res = load(old, o);
  REQUIRE(res.status == project::Status::Ok);
  CHECK(res.skipped == 0);
  CHECK(o.perf[0] == 1);
  CHECK(o.perf[1] == 2);
  CHECK(o.perf[2] == 3);
  CHECK(o.perf[3] == 0);
  for (unsigned r = 0; r < kLadderRows; ++r) {
    CHECK(o.ladder[r].target == kDefaultLadder[r].target);
    CHECK(o.ladder[r].id == kDefaultLadder[r].id);
  }
  Session c;
  c.rig.wiring.ladder[kLadderHome3] = LaneRef{kLaneDist, 2};
  REQUIRE(project::apply(c.rig, c.engine, o));  // a project without rows puts the rows back to the defaults
  CHECK(c.rig.wiring.ladder[kLadderHome3].target == kLaneVoice);
  CHECK(c.rig.wiring.ladder[kLadderHome3].id == 0);

  // Rows that mean nothing: a HOME row that follows, a target past the table, the voice's Stock: the defaults. PERFORM's row may follow.
  const auto bad = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PERF")) {
      const std::size_t at = 2 + kPerfButtons;
      p[at + 0] = kLaneNone;
      p[at + 1] = 5;
      p[at + 2] = static_cast<std::uint8_t>(kLaneTargetCount);
      p[at + 3] = 0;
      p[at + 4] = kLaneNone;
      p[at + 5] = 0;
    }
    return true;
  });
  project::ProjectState g;
  REQUIRE(load(bad, g).status == project::Status::Ok);
  CHECK(g.ladder[0].target == kLaneVoice);
  CHECK(g.ladder[1].target == kLaneVoice);
  CHECK(g.ladder[2].target == kLaneNone);
  const auto stock = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PERF")) {
      p[2 + kPerfButtons + 4] = kLaneVoice;
      p[2 + kPerfButtons + 5] = kLaneVoiceStock;
    }
    return true;
  });
  REQUIRE(load(stock, g).status == project::Status::Ok);
  CHECK(g.ladder[2].target == kLaneNone);
  // A target the table knows but no row can drive (the mixer's PAN, the sequencer's own values, an id past the knobs): the defaults too, never a dead row.
  const auto dead = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PERF")) {
      const std::size_t at = 2 + kPerfButtons;
      p[at + 0] = kLaneMixer;
      p[at + 1] = 1;  // PAN
      p[at + 2] = kLaneSeq;
      p[at + 3] = 0;
      p[at + 4] = kLaneDelay;
      p[at + 5] = 200;
    }
    return true;
  });
  REQUIRE(load(dead, g).status == project::Status::Ok);
  CHECK(g.ladder[0].target == kLaneVoice);
  CHECK(g.ladder[0].id == 0);
  CHECK(g.ladder[1].target == kLaneVoice);
  CHECK(g.ladder[1].id == 1);
  CHECK(g.ladder[2].target == kLaneNone);
}
