// SPDX-License-Identifier: GPL-3.0-or-later
// The project codec (app/src/project.h, docs/UX-303.md 8.1 and 8.3, docs/UX-PLAN.md F1): a project is gathered from the session, encoded into one image,
// decoded and applied to another session; a damaged image is refused, a newer major of HEAD refused, other chunks of a newer major or unknown skipped,
// a newer minor read, a missing parameter keeps its default, a SNAP with fewer values is filled from the sound. The session is the app's wiring over a real
// engine (the units attached in the order of o3_303_app.cpp), without the screens.
#include <doctest/doctest.h>

#include "../app/src/ladder_rows.h"
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
#include <o3_303/units/tract/tract.h>

#include <algorithm>
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

// The format's major as the first byte of a chunk's version (the minor is 0).
constexpr std::uint8_t kMajor8 = static_cast<std::uint8_t>(project::kMajor);

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
  TractFx tract;
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
    w.tract = engine.attach(tract);
    (void)engine.set_source(0, w.voice);
    (void)engine.set_insert(0, 0, w.dist);
    (void)engine.set_insert(0, 1, w.growl);
    (void)engine.set_insert(0, 2, w.tract);
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
  p.length = static_cast<std::uint8_t>(lcg(s) % 64 + 1);
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
  static const std::uint8_t kTractLens[6] = {kTractSame, 4, 8, 16, 32, 64};  // the TRACT line (A1b): every step, GLD and length away from empty
  for (std::uint8_t& st : p.tract.step) {
    st = tract_make(static_cast<std::uint8_t>(lcg(s) % (kTractVowels + 1u)), lcg(s) % 4 == 0);
  }
  p.tract.len = kTractLens[lcg(s) % 6];
  p.chance = static_cast<std::uint8_t>(1u + lcg(s) % 100u);  // CHANCE (G2), drawn last so the draws above are what they were
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
  t.sequencer->set_tract_line(true);  // LINE on (A1b), away from the default
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
  CHECK(s.gen.scale == 7);
  CHECK(s.gen.root == 5);
  CHECK(s.gen.low == 30);
  CHECK(s.gen.high == 55);
  CHECK(s.gen.density == 61);
  CHECK(s.gen.accent_pct == 33);
  CHECK(s.gen.slide_pct == 26);
  CHECK(s.snap_used == ((1u << 0) | (1u << 3) | (1u << 7)));
  CHECK(s.pats != nullptr);
  CHECK(s.song.length == 9);
  CHECK(std::memcmp(&s.song, &a.bank->song(), sizeof(seq::Song)) == 0);

  // A second session, with other values everywhere, takes the project.
  Session b;
  (void)b.bank->song_append(seq::SongRow{1, 3, 2, 0});  // a song of its own: the project's replaces it
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
  CHECK(b.sequencer->tract_line_on());  // LINE (A1b)
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

TEST_CASE("project: HEAD of a newer major or an older one refuses the project; HEAD missing, short or of major 0 is damaged") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;

  const auto with_major = [&](std::uint8_t major) {
    return rebuild(img, [major](char (&id)[4], std::vector<std::uint8_t>& p) {
      if (is(id, "HEAD")) {
        p[0] = major;
      }
      return true;
    });
  };
  CHECK(load(with_major(4), s).status == project::Status::FromNewer);
  CHECK(load(with_major(3), s).status == project::Status::Ok);
  // An earlier format (1.x, or 2.x of v0.9.0) is refused whole: the state stays as it was, and there is no converter.
  const project::ProjectState untouched;
  for (const std::uint8_t major : {std::uint8_t{1}, std::uint8_t{2}}) {
    s = untouched;
    const project::Result older = load(with_major(major), s);
    CHECK(older.status == project::Status::FromOlder);
    CHECK(s.pats == nullptr);
    CHECK(s.sound_n == 0);
    CHECK(std::strcmp(s.name, untouched.name) == 0);
  }
  CHECK(load(with_major(0), s).status == project::Status::Damaged);

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

TEST_CASE("project: a chunk of the format missing, or of another major, makes the file damaged; unknown and doubled chunks are skipped") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;

  // Format 3.0 has seven chunks, each once: HEAD, GEN, PATS, SND, SNAP, PERF, SONG. Each one is needed.
  for (const char* gone : {"HEAD", "GEN ", "PATS", "SND ", "SNAP", "PERF", "SONG"}) {
    const auto cut = rebuild(img, [gone](char (&id)[4], std::vector<std::uint8_t>&) { return !is(id, gone); });
    INFO("chunk " << gone);
    CHECK(load(cut, s).status == project::Status::Damaged);
  }
  // A chunk of another major is not read, so it is a chunk that is missing.
  for (const char* other : {"GEN ", "PATS", "SND ", "SNAP", "PERF", "SONG"}) {
    const auto bad = rebuild(img, [other](char (&id)[4], std::vector<std::uint8_t>& p) {
      if (is(id, other)) {
        p[0] = static_cast<std::uint8_t>(project::kMajor + 1u);
      }
      return true;
    });
    INFO("chunk " << other);
    CHECK(load(bad, s).status == project::Status::Damaged);
  }

  // A chunk nobody knows and a second GEN: skipped and counted, the first GEN counts.
  std::vector<std::uint8_t> gen_payload;
  (void)rebuild(img, [&](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "GEN ")) {
      gen_payload = p;
    }
    return true;
  });
  REQUIRE_FALSE(gen_payload.empty());
  gen_payload[3] = 1;  // another root
  const auto extra = rebuild(img, [](char (&)[4], std::vector<std::uint8_t>&) { return true; }, {{"ZZZZ", {1, 0, 9, 9, 9}}, {"GEN ", gen_payload}});
  const project::Result res = load(extra, s);
  REQUIRE(res.status == project::Status::Ok);
  CHECK(res.skipped == 2);
  CHECK(s.gen.root == 5);
  CHECK(s.selected == 17);
  CHECK(s.tempo_mbpm == 133'500);
  CHECK(s.snap_used == ((1u << 0) | (1u << 3) | (1u << 7)));
  CHECK(s.sound_n > 30);
}

TEST_CASE("project: a record size this build does not have makes the chunk, and so the file, damaged") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;

  // PATS: 5 + 48 x sizeof(Pattern) (the record size is in bytes 3 and 4); PERF: the buttons, the set in use and 4 sets of 4 (target, id).
  bool pats_seen = false;
  bool perf_seen = false;
  (void)rebuild(img, [&](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PATS")) {
      pats_seen = true;
      CHECK(p.size() == 5 + kPatternCount * sizeof(Pattern));
      CHECK(p[2] == kPatternCount);
      CHECK(((static_cast<unsigned>(p[3]) << 8) | p[4]) == sizeof(Pattern));
    }
    if (is(id, "PERF")) {
      perf_seen = true;
      CHECK(p.size() == 2 + 1 + kKnobSets * kKnobsPerSet * 2);
    }
    return true;
  });
  CHECK((pats_seen && perf_seen));

  const auto pats_with = [&](const std::function<void(std::vector<std::uint8_t>&)>& fn) {
    return rebuild(img, [&](char (&id)[4], std::vector<std::uint8_t>& p) {
      if (is(id, "PATS")) {
        fn(p);
      }
      return true;
    });
  };
  CHECK(load(pats_with([](std::vector<std::uint8_t>& p) { p[4] = static_cast<std::uint8_t>(p[4] - 4); }), s).status == project::Status::Damaged);  // another record size
  CHECK(load(pats_with([](std::vector<std::uint8_t>& p) { p[2] = kPatternCount - 1; }), s).status == project::Status::Damaged);                    // fewer patterns
  CHECK(load(pats_with([](std::vector<std::uint8_t>& p) { p.resize(p.size() - 100); }), s).status == project::Status::Damaged);                     // cut short
  CHECK(load(pats_with([](std::vector<std::uint8_t>& p) { p.resize(5); }), s).status == project::Status::Damaged);

  const auto perf_cut = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PERF")) {
      p.resize(p.size() - 2);
    }
    return true;
  });
  CHECK(load(perf_cut, s).status == project::Status::Damaged);
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

TEST_CASE("project: an empty song is a SONG chunk of its own; a damaged SONG is refused or brought into range") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);

  const auto payload_of = [&](std::vector<std::uint8_t> payload) {
    return rebuild(img, [&](char (&id)[4], std::vector<std::uint8_t>& p) {
      if (is(id, "SONG")) {
        p = payload;
      }
      return true;
    });
  };
  project::ProjectState s;
  {  // an empty song is written, read back, and empties the session's
    Session e;
    const std::vector<std::uint8_t> empty = encode_session(e);
    bool seen = false;
    (void)rebuild(empty, [&](char (&id)[4], std::vector<std::uint8_t>& p) {
      if (is(id, "SONG")) {
        seen = true;
        CHECK(p.size() == 3);  // major, minor, length 0
      }
      return true;
    });
    CHECK(seen);
    REQUIRE(load(empty, s).status == project::Status::Ok);
    CHECK(s.song.length == 0);
    Session b;
    scramble(b);
    REQUIRE(b.bank->song().length > 0);
    REQUIRE(project::apply(b.rig, b.engine, s));
    CHECK(b.bank->song().length == 0);
  }
  {  // 5 rows said, 3 present: the file is damaged
    std::vector<std::uint8_t> p = {kMajor8, 0, 5};
    for (unsigned i = 0; i < 12; ++i) {
      p.push_back(1);
    }
    CHECK(load(payload_of(p), s).status == project::Status::Damaged);
  }
  CHECK(load(payload_of({kMajor8, 0}), s).status == project::Status::Damaged);  // no length byte at all
  {  // two rows, every value out of range: the pattern past the last, repeat 0 and 200, transpose -100 and +100, flags that are not only the mute
    const std::vector<std::uint8_t> p = {kMajor8, 0, 2, 99, 0, 0x9C, 0xFF, 7, 200, 100, 0x02};
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
    std::vector<std::uint8_t> p = {kMajor8, 0, 65};
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
    p[1] = 7;  // 2.7
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
  CHECK(s.pats != nullptr);
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

  // A project whose SND holds no entry is the default sound.
  const auto nosnd = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "SND ")) {
      p = {kMajor8, 0, 0, 0};  // an SND chunk with no entries
    }
    return true;
  });
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

TEST_CASE("project: the voice's mods (ids 8..18) are carried and round-trip; an entry the file lacks gives the stock value") {
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

  // An image with no entries for ids 8..18 (and the 303's own all there): the mods come in at their stock value.
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

TEST_CASE("project: B5a, the wobble's settings and its latch round-trip exact, and a project without the latch's entry has it off") {
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

  // An image with no SND entry for the latch (app id 9): the latch ends off.
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
    }
    return true;
  });
  CHECK(project::kAppModsOn == project::kAppWobbleOn + 1u);
  CHECK(project::kAppGrowlLastMix == project::kAppValueCount - 3u);  // A1 appended the tract's last mix after it (kAppGrowlLastMix is the one before that)
  CHECK(project::kAppTractLastMix == project::kAppValueCount - 2u);
  CHECK(project::kAppTractLine == project::kAppValueCount - 1u);  // A1b appended LINE after it
  Session c;
  c.rig.wiring.wob_want = 1;  // a session with the latch on: the old project has none, so it ends off
  REQUIRE(load(old, s).status == project::Status::Ok);
  project::apply(c.rig, c.engine, s);
  CHECK(c.rig.wiring.wob_want == 0);
  CHECK(c.rig.wiring.wobble.rate == 5);  // what the file held stays
  // A project whose SND holds no entry: every default is the new one.
  const auto nosnd = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "SND ")) {
      p = {kMajor8, 0, 0, 0};  // an SND chunk with no entries
    }
    return true;
  });
  REQUIRE(load(nosnd, s).status == project::Status::Ok);
  project::apply(c.rig, c.engine, s);
  CHECK_FALSE(c.rig.wiring.wobble.reset);                                  // RESET defaults off
  CHECK(c.engine.param(c.rig.wiring.growl, Growl::CombTune) == 7.0f);       // CMB TUNE defaults to +7 st
  CHECK(c.rig.wiring.wob_want == 0);
  CHECK_FALSE(c.sequencer->wobble().reset_on_note);
}

TEST_CASE("project: F13a, MODS on or off and the growl's last mix are saved, and a project with neither entry loads with the mods heard and 50 %") {
  Session a;
  scramble(a);
  a.rig.controls.set(a.engine, Entry{Item::Mods, 0, nullptr}, 0.0f);  // MODS off: STOCK on
  REQUIRE(a.rig.wiring.stock);
  a.rig.wiring.growl_last_mix = 0.3f;
  const std::vector<std::uint8_t> img = encode_session(a);
  Session b;
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);
  project::apply(b.rig, b.engine, s);
  CHECK(b.rig.wiring.stock);
  CHECK(b.engine.param(b.rig.wiring.voice, AcidVoice::Stock) == 1.0f);
  CHECK(b.rig.wiring.growl_last_mix == doctest::Approx(0.3f));
  CHECK(encode_session(b) == img);
  // A load into a session that has STOCK on, of a project saved with the mods heard, ends it.
  Session c;
  c.rig.controls.set(c.engine, Entry{Item::Mods, 0, nullptr}, 0.0f);
  a.rig.controls.set(a.engine, Entry{Item::Mods, 0, nullptr}, 1.0f);
  REQUIRE_FALSE(a.rig.wiring.stock);
  const std::vector<std::uint8_t> heard = encode_session(a);
  REQUIRE(load(heard, s).status == project::Status::Ok);
  project::apply(c.rig, c.engine, s);
  CHECK_FALSE(c.rig.wiring.stock);
  CHECK(c.engine.param(c.rig.wiring.voice, AcidVoice::Stock) == 0.0f);

  // No SND entry for either (app ids 10 and 11). The session's STOCK ends and the mix is 50 %.
  const auto old = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "SND ")) {
      std::vector<std::uint8_t> out(p.begin(), p.begin() + 4);
      unsigned kept = 0;
      const unsigned n = (static_cast<unsigned>(p[2]) << 8) | p[3];
      for (unsigned i = 0; i < n; ++i) {
        const std::uint8_t* en = &p[4 + i * 8];
        const unsigned pid = (static_cast<unsigned>(en[2]) << 8) | en[3];
        if (en[0] == project::kSndApp && (pid == project::kAppModsOn || pid == project::kAppGrowlLastMix)) {
          continue;
        }
        out.insert(out.end(), en, en + 8);
        ++kept;
      }
      out[2] = static_cast<std::uint8_t>(kept >> 8);
      out[3] = static_cast<std::uint8_t>(kept);
      p = out;
    }
    return true;
  });
  Session d;
  d.rig.controls.set(d.engine, Entry{Item::Mods, 0, nullptr}, 0.0f);
  d.rig.wiring.growl_last_mix = 0.9f;
  REQUIRE(load(old, s).status == project::Status::Ok);
  project::apply(d.rig, d.engine, s);
  CHECK_FALSE(d.rig.wiring.stock);
  CHECK(d.rig.wiring.growl_last_mix == doctest::Approx(0.5f));
}

TEST_CASE("project: A1, the tract's settings, its last mix and a tract lane round-trip; a project with no entry for the tract loads with TRACT at Mix 0 and a last mix of 50 %") {
  Session a;
  const Wiring& wa = a.rig.wiring;
  CHECK(static_cast<unsigned>(kLaneTract) == 7u);
  CHECK(a.engine.param(wa.tract, TractFx::Mix) == 0.0f);  // silent by default
  (void)a.engine.set_param(wa.tract, TractFx::Vowel, 0.7f);
  (void)a.engine.set_param(wa.tract, TractFx::Tongue, 40.0f);
  (void)a.engine.set_param(wa.tract, TractFx::Lips, -30.0f);
  (void)a.engine.set_param(wa.tract, TractFx::Size, 3.0f);
  (void)a.engine.set_param(wa.tract, TractFx::Mix, 0.8f);
  a.rig.wiring.tract_last_mix = 0.25f;
  Pattern p = random_pattern(9);
  p.lanes[1] = LaneRef{kLaneTract, TractFx::Vowel};
  p.lanes[6] = LaneRef{kLaneTract, TractFx::Mix};
  (void)a.bank->restore(0, p);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);
  Session b;
  project::apply(b.rig, b.engine, s);
  const Wiring& wb = b.rig.wiring;
  CHECK(b.engine.param(wb.tract, TractFx::Vowel) == 0.7f);
  CHECK(b.engine.param(wb.tract, TractFx::Tongue) == 40.0f);
  CHECK(b.engine.param(wb.tract, TractFx::Lips) == -30.0f);
  CHECK(b.engine.param(wb.tract, TractFx::Size) == 3.0f);
  CHECK(b.engine.param(wb.tract, TractFx::Mix) == doctest::Approx(0.8f));
  CHECK(wb.tract_last_mix == doctest::Approx(0.25f));
  const Pattern& got = b.bank->pattern(0);
  CHECK(got.lanes[1].target == kLaneTract);
  CHECK(got.lanes[1].id == TractFx::Vowel);
  CHECK(got.lanes[6].target == kLaneTract);
  CHECK(got.lanes[6].id == TractFx::Mix);
  CHECK(encode_session(b) == img);

  // No SND entry of target 7 (the tract's parameters) and none for the app value kAppTractLastMix.
  const auto old = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& pl) {
    if (is(id, "SND ")) {
      std::vector<std::uint8_t> out(pl.begin(), pl.begin() + 4);
      unsigned kept = 0;
      const unsigned n = (static_cast<unsigned>(pl[2]) << 8) | pl[3];
      for (unsigned i = 0; i < n; ++i) {
        const std::uint8_t* en = &pl[4 + i * 8];
        const unsigned pid = (static_cast<unsigned>(en[2]) << 8) | en[3];
        if (en[0] == kLaneTract || (en[0] == project::kSndApp && pid == project::kAppTractLastMix)) {
          continue;
        }
        out.insert(out.end(), en, en + 8);
        ++kept;
      }
      out[2] = static_cast<std::uint8_t>(kept >> 8);
      out[3] = static_cast<std::uint8_t>(kept);
      pl = out;
    }
    return true;
  });
  Session d;
  (void)d.engine.set_param(d.rig.wiring.tract, TractFx::Mix, 0.9f);  // a session that had the tract heard: the load takes it back to the default
  d.rig.wiring.tract_last_mix = 0.9f;
  REQUIRE(load(old, s).status == project::Status::Ok);
  project::apply(d.rig, d.engine, s);
  CHECK(d.engine.param(d.rig.wiring.tract, TractFx::Mix) == 0.0f);
  CHECK(d.engine.param(d.rig.wiring.tract, TractFx::Vowel) == a.engine.param_desc(wa.tract, TractFx::Vowel)->def);
  CHECK(d.rig.wiring.tract_last_mix == doctest::Approx(0.5f));
}

bool same_line(const TractLine& a, const TractLine& b) { return std::memcmp(&a, &b, sizeof(TractLine)) == 0; }

TEST_CASE("project: A1b, the TRACT lines and LINE round-trip bit for bit, inside PATS") {
  Session a;
  scramble(a);  // random lines (steps, GLD, lengths) in all 48 patterns, LINE on
  bool any_vowel = false;
  bool any_glide = false;
  bool any_len = false;
  for (unsigned i = 0; i < kPatternCount; ++i) {
    const TractLine& t = a.bank->pattern(i).tract;
    any_vowel = any_vowel || tract_has_vowels(t);
    any_len = any_len || t.len != kTractSame;
    for (const std::uint8_t st : t.step) {
      any_glide = any_glide || tract_glide(st);
    }
  }
  REQUIRE((any_vowel && any_glide && any_len));
  const std::vector<std::uint8_t> img = encode_session(a);

  project::ProjectState s;
  const project::Result res = load(img, s);
  REQUIRE(res.status == project::Status::Ok);
  CHECK(res.skipped == 0);
  Session b;
  REQUIRE(project::apply(b.rig, b.engine, s));
  for (unsigned i = 0; i < kPatternCount; ++i) {
    CHECK(same_line(b.bank->pattern(i).tract, a.bank->pattern(i).tract));
  }
  CHECK(b.sequencer->tract_line_on());
  CHECK(encode_session(b) == img);

  // LINE off is saved as off, whatever the lines hold.
  a.sequencer->set_tract_line(false);
  const std::vector<std::uint8_t> off = encode_session(a);
  REQUIRE(load(off, s).status == project::Status::Ok);
  Session c;
  c.sequencer->set_tract_line(true);
  REQUIRE(project::apply(c.rig, c.engine, s));
  CHECK_FALSE(c.sequencer->tract_line_on());
  CHECK(same_line(c.bank->pattern(5).tract, a.bank->pattern(5).tract));
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

TEST_CASE("project: out of range fields of a file with a good CRC are brought into range") {
  Session a;
  scramble(a);
  project::ProjectState s;
  Session b;

  // Fields a sequencer indexes with, out of range in a file whose CRC is good: clamped (a length of 65, a note of 200, a lane of target 99, ...).
  Pattern bad = random_pattern(3);
  bad.length = 65;
  bad.steps[2].note = 200;
  bad.steps[3].flags = 0xFF;
  bad.swing = 200;
  bad.transpose = -100;
  bad.gate = 0;
  bad.chance = 0;  // a pattern that never plays is no CHANCE: back to 100
  bad.reserved[1] = 7;
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
  CHECK(p0.chance == kDefaultChance);
  CHECK(p0.reserved[1] == 0);
  CHECK(p0.lanes[1].target == kLaneNone);
  CHECK(b.bank->selected() == 0);
  // 101 or more is out of range too; 1 and 100 are kept.
  Pattern hi = random_pattern(4);
  hi.chance = 1;
  (void)a.bank->restore(1, hi);
  hi.chance = 100;
  (void)a.bank->restore(2, hi);
  hi.chance = 200;
  (void)a.bank->restore(3, hi);
  project::ProjectState s2;
  Session c;
  REQUIRE(load(encode_session(a), s2).status == project::Status::Ok);
  project::apply(c.rig, c.engine, s2);
  CHECK(c.bank->pattern(1).chance == 1);
  CHECK(c.bank->pattern(2).chance == 100);
  CHECK(c.bank->pattern(3).chance == kDefaultChance);
}

TEST_CASE("project: the image fits its buffer, at the fullest too, and an apply leaves no history") {
  Session a;
  scramble(a);
  project::ProjectState s = a.gather();
  const std::size_t typical = project::encode(s, *a.bank, project::buffer(), project::kProjectBytes);
  CHECK(typical > 108'000);
  CHECK(typical < 116'000);  // PATS is 48 x sizeof(Pattern)

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
  std::vector<std::uint8_t> p = {kMajor8, 0, static_cast<std::uint8_t>(claimed >> 8), static_cast<std::uint8_t>(claimed)};
  for (unsigned i = 0; i < n; ++i) {
    p.insert(p.end(), {kLaneVoice, 0, 0, static_cast<std::uint8_t>(AcidVoice::Cutoff)});
    push_f32(p, value);
  }
  return p;
}

}  // namespace

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

TEST_CASE("project: a short payload, or a count the payload cannot hold, makes the file damaged") {
  Session a;
  scramble(a);
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;

  const auto with = [&](const char* chunk_id, const std::function<void(std::vector<std::uint8_t>&)>& fn) {
    return rebuild(img, [&](char (&id)[4], std::vector<std::uint8_t>& p) {
      if (is(id, chunk_id)) {
        fn(p);
      }
      return true;
    });
  };
  // Short GEN, SND, SNAP, PERF: each is a chunk of the format that does not read.
  CHECK(load(with("GEN ", [](std::vector<std::uint8_t>& p) { p.resize(5); }), s).status == project::Status::Damaged);
  CHECK(load(with("SND ", [](std::vector<std::uint8_t>& p) { p.resize(4 + 8 * 3 + 5); }), s).status == project::Status::Damaged);  // says many entries, holds three and a bit
  CHECK(load(with("SNAP", [](std::vector<std::uint8_t>& p) { p.resize(p.size() - 7); }), s).status == project::Status::Damaged);
  CHECK(load(with("PERF", [](std::vector<std::uint8_t>& p) { p.resize(4); }), s).status == project::Status::Damaged);
  CHECK(load(with("PERF", [](std::vector<std::uint8_t>& p) { p.resize(1); }), s).status == project::Status::Damaged);

  // A count that the payload cannot hold: SND n = 65535, SNAP 255 slots of 255 values, PATS 255 patterns.
  CHECK(load(with("SND ", [](std::vector<std::uint8_t>& p) { p[2] = 0xFF; p[3] = 0xFF; }), s).status == project::Status::Damaged);
  CHECK(load(with("SNAP", [](std::vector<std::uint8_t>& p) { p[2] = 255; p[3] = 255; }), s).status == project::Status::Damaged);
  CHECK(load(with("PATS", [](std::vector<std::uint8_t>& p) { p[2] = 255; }), s).status == project::Status::Damaged);
  // More patterns than this build has, even with the bytes for them: another layout, so damaged too.
  CHECK(load(with("PATS", [](std::vector<std::uint8_t>& p) { p[2] = 52; p.resize(p.size() + 4 * sizeof(Pattern), 0x55); }), s).status == project::Status::Damaged);
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
TEST_CASE("project: HEAD carries the save counter; small_crc ignores it") {
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

  // The counter moves with every write: it is no change of the session.
  project::ProjectState other = st;
  other.save_count = 7;
  CHECK(project::small_crc(st) == project::small_crc(other));
}

TEST_CASE("project: KNOBS' four sets and the set in use round-trip; a damaged row falls back to its factory knob") {
  Session a;
  scramble(a);
  Wiring& w = a.rig.wiring;
  w.knob_sets.row[1][0] = LaneRef{kLaneGrowl, Growl::Mix};  // set 2, row 1 re-pointed
  w.knob_sets.row[3][3] = LaneRef{kLaneDelay, 1};           // set 4, row 4 re-pointed
  w.knob_sets.row[kKnobSetTract][1] = LaneRef{kLaneTract, TractFx::Mix};  // F1: the TRACT set (index 4), row 2 re-pointed
  knob_set_use(w, kKnobSetTract);                           // the TRACT set in use: saved as index 4
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);
  CHECK(s.knob_set == kKnobSetTract);
  CHECK(s.sets.row[kKnobSetTract][1].id == TractFx::Mix);
  CHECK(s.sets.row[kKnobSetTract][0].id == TractFx::Vowel);
  CHECK(s.sets.row[1][0].target == kLaneGrowl);
  CHECK(s.sets.row[1][0].id == Growl::Mix);
  CHECK(s.sets.row[3][3].target == kLaneDelay);
  CHECK(s.sets.row[3][3].id == 1);
  Session b;
  REQUIRE(project::apply(b.rig, b.engine, s));
  CHECK(b.rig.wiring.knob_set == kKnobSetTract);
  for (unsigned set = 0; set < kKnobSets; ++set) {
    for (unsigned pos = 0; pos < kKnobsPerSet; ++pos) {
      CHECK(b.rig.wiring.knob_sets.row[set][pos].target == w.knob_sets.row[set][pos].target);
      CHECK(b.rig.wiring.knob_sets.row[set][pos].id == w.knob_sets.row[set][pos].id);
    }
  }
  for (unsigned r = 0; r < kLadderRows; ++r) {  // the live rows are the set in use's
    const LaneRef want = b.rig.wiring.knob_sets.row[kKnobSetTract][knob_row_pos(r)];
    CHECK(b.rig.wiring.ladder[r].target == want.target);
    CHECK(b.rig.wiring.ladder[r].id == want.id);
  }
  CHECK(!knob_set_is_factory(b.rig.wiring, 1));
  CHECK(knob_set_is_factory(b.rig.wiring, 2));
  CHECK(!knob_set_is_factory(b.rig.wiring, kKnobSetTract));

  // A damaged row (a target past the table, then the voice's Stock) and a set in use past the last: the factory knob, and set 1.
  const auto bad = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PERF")) {
      const std::size_t at = 2;
      p[at] = 9;                                                       // the set in use
      p[at + 1] = static_cast<std::uint8_t>(kLaneTargetCount);         // set 1 row 1
      p[at + 2] = 0;
      p[at + 3] = kLaneVoice;                                          // set 1 row 2
      p[at + 4] = kLaneVoiceStock;
    }
    return true;
  });
  project::ProjectState g;
  REQUIRE(load(bad, g).status == project::Status::Ok);
  CHECK(g.knob_set == 0);
  CHECK(g.sets.row[0][0].target == kKnobFactory.row[0][0].target);
  CHECK(g.sets.row[0][0].id == kKnobFactory.row[0][0].id);
  CHECK(g.sets.row[0][1].target == kKnobFactory.row[0][1].target);
  CHECK(g.sets.row[0][1].id == kKnobFactory.row[0][1].id);
}

TEST_CASE("project: KN4, a row-only knob (WOB RATE, MODS, ...) in a KNOBS set round-trips; an unknown row-only target or one with an id falls back to the factory knob") {
  Session a;
  scramble(a);
  Wiring& w = a.rig.wiring;
  CHECK(w.knob_sets.row[3][0].target == kRowWobRate);                   // the factory set 4 already holds them
  w.knob_sets.row[1][1] = LaneRef{kRowMods, 0};                          // set 2 row 2: MODS
  w.knob_sets.row[2][3] = LaneRef{kRowWobOn, 0};                         // set 3 row 4: WOB
  const std::vector<std::uint8_t> img = encode_session(a);
  project::ProjectState s;
  REQUIRE(load(img, s).status == project::Status::Ok);
  CHECK(s.sets.row[3][0].target == kRowWobRate);
  CHECK(s.sets.row[3][0].id == 0);
  CHECK(s.sets.row[1][1].target == kRowMods);
  CHECK(s.sets.row[1][1].id == 0);
  CHECK(s.sets.row[2][3].target == kRowWobOn);
  Session b;
  REQUIRE(project::apply(b.rig, b.engine, s));
  CHECK(b.rig.wiring.knob_sets.row[1][1].target == kRowMods);
  CHECK(b.rig.wiring.knob_sets.row[2][3].target == kRowWobOn);
  CHECK(b.rig.wiring.knob_sets.row[3][0].target == kRowWobRate);
  CHECK(encode_session(b) == img);

  // A target past the row-only ones, and a row-only target with an id: the factory knob of that place.
  const auto bad = rebuild(img, [](char (&id)[4], std::vector<std::uint8_t>& p) {
    if (is(id, "PERF")) {
      const std::size_t at = 2;
      p[at + 1] = static_cast<std::uint8_t>(kRowOnlyEnd);  // set 1 row 1: past the table
      p[at + 2] = 0;
      p[at + 3] = static_cast<std::uint8_t>(kRowWobRate);  // set 1 row 2: a row-only target does not take an id
      p[at + 4] = 3;
    }
    return true;
  });
  project::ProjectState g;
  REQUIRE(load(bad, g).status == project::Status::Ok);
  CHECK(g.sets.row[0][0].target == kKnobFactory.row[0][0].target);
  CHECK(g.sets.row[0][0].id == kKnobFactory.row[0][0].id);
  CHECK(g.sets.row[0][1].target == kKnobFactory.row[0][1].target);
  CHECK(g.sets.row[0][1].id == kKnobFactory.row[0][1].id);
}
