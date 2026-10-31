// SPDX-License-Identifier: GPL-3.0-or-later
#include "project.h"

#include <o3sdk/core/placement.h>
#include <o3sdk/core/scales.h>
#include <o3sdk/formats/chunk_file.h>
#include <o3sdk/formats/crc32.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/pattern.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/growl/growl.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace o3_303::project {

namespace chunk = o3sdk::formats::chunk;
namespace dsp = o3sdk::dsp;
namespace core = o3sdk::core;

namespace {

// ---- The layout (the contract: a change of one of these bumps its chunk's major) ----------------------------------------------------------------

static_assert(sizeof(Pattern) == 2260 && alignof(Pattern) == 4, "PATS stores a pattern byte for byte: a change of Pattern bumps the chunk's major");
static_assert(offsetof(Pattern, lanes) + sizeof(Pattern::lanes) == sizeof(Pattern), "a Pattern has no padding: every field is one byte");
static_assert(std::is_trivially_copyable<Pattern>::value, "a pattern is copied as bytes");
static_assert(kPatternCount == 48, "PATS holds 48 patterns");
static_assert(kSnapSlots == 8, "SNAP holds 8 slots");

constexpr unsigned kHeadPayload = 2 + 1 + 4 + 1 + kNameChars + 4;  // version, selected, tempo, flags, name, the save counter (HEAD 1.1)
constexpr unsigned kGenPayload = 2 + 7;
constexpr unsigned kSoundEntryBytes = 8;  // target u8, 0 u8, id u16, value f32
constexpr unsigned kSnapHeader = 2 + 3;
constexpr unsigned kPerfPayload = 2 + kPerfButtons;
constexpr unsigned kSongReserve = 4096;  // room kept for SG1's chunk

// A chunk with its header and its pad byte.
constexpr std::size_t chunk_bytes(std::size_t payload) noexcept { return 8 + payload + (payload & 1u); }

// The largest image encode() can make (SND full, the song reserve included): it must fit the buffer, so that a save cannot fail for want of room.
constexpr std::size_t kMaxImageBytes =
    chunk::kHeaderBytes + chunk_bytes(kHeadPayload) + chunk_bytes(kGenPayload) + chunk_bytes(2 + 1 + 2 + std::size_t{kPatternCount} * sizeof(Pattern)) +
    chunk_bytes(2 + 2 + std::size_t{kMaxSound} * kSoundEntryBytes) + chunk_bytes(kSnapHeader + std::size_t{kSnapSlots} * kSnapTargetCount * 4) +
    chunk_bytes(kPerfPayload) + kSongReserve + chunk::kCrcChunkBytes;
static_assert(kMaxImageBytes <= kProjectBytes, "the project image must fit its 128 KiB buffer");

constexpr std::uint8_t kFlagScaleSet = 1u << 0;

O3SDK_BIG std::uint8_t g_buffer[kProjectBytes];

// Counts the encodes (main loop only): decode() records it with the patterns' pointer, apply() refuses a state an encode has outdated.
std::uint32_t g_generation = 0;

// ---- The sound ---------------------------------------------------------------------------------------------------------------------------------

// The units a project carries parameters of, as SND targets.
constexpr std::uint8_t kUnitTargets[] = {kLaneVoice, kLaneDist, kLaneDelay, kLaneGrowl, kSndFreeze};

dsp::UnitId unit_of(const Wiring& w, std::uint8_t target) noexcept {
  switch (target) {
    case kLaneVoice:
      return w.voice;
    case kLaneDist:
      return w.dist;
    case kLaneDelay:
      return w.delay;
    case kLaneGrowl:
      return w.growl;
    case kSndFreeze:
      return w.freeze;
    default:
      break;
  }
  return dsp::kNoUnit;
}

// Which of a unit's parameters are the sound. Not: the voice's own Volume (the app keeps it at 0 dB, the level is slot 0's), the growl's Note (the
// sequencer sets it at every note), the freeze's Active, Mode and Mix (what is held now, not a setting).
bool carried(std::uint8_t target, std::uint16_t id) noexcept {
  switch (target) {
    case kLaneVoice:
      return id != AcidVoice::Volume;
    case kLaneGrowl:
      return id != Growl::Note;
    case kSndFreeze:
      return id == Freeze::Length || id == Freeze::RollRate || id == Freeze::Accelerate || id == Freeze::StopTime;
    default:
      break;
  }
  return true;
}

// Every carried parameter of every unit, in id order: fn(target, unit, id, desc). The ids of a unit are 0..count-1, so a parameter appended to a unit
// is carried from its first build.
template <typename Fn>
void for_each_sound(const Wiring& w, const dsp::Engine& e, Fn&& fn) noexcept {
  for (const std::uint8_t target : kUnitTargets) {
    const dsp::UnitId unit = unit_of(w, target);
    for (unsigned id = 0; id < dsp::kMaxParamsPerUnit; ++id) {
      const core::ParamDesc* d = e.param_desc(unit, static_cast<std::uint16_t>(id));
      if (d == nullptr) {
        break;
      }
      if (carried(target, static_cast<std::uint16_t>(id))) {
        fn(target, unit, static_cast<std::uint16_t>(id), *d);
      }
    }
  }
}

float clampf(float v, float lo, float hi, float fallback) noexcept {
  if (!(v == v)) {  // NaN
    return fallback;
  }
  return v < lo ? lo : (v > hi ? hi : v);
}

// The app's values as the pages show them (pages.cpp): the session's now.
float app_value(const Rig& rig, const dsp::Engine& e, unsigned id) noexcept {
  const Wiring& w = rig.wiring;
  switch (id) {
    case kAppLevel:
      return w.level_db;
    case kAppSend:
      return w.send_a_pct;
    case kAppMaster:
      return e.master_level() * 100.0f;
    case kAppFill:
      return w.seq != nullptr ? static_cast<float>(w.seq->fill_variant()) : 0.0f;
    case kAppWobbleRate:
      return static_cast<float>(w.wobble.rate);
    case kAppWobbleShape:
      return static_cast<float>(w.wobble.shape);
    case kAppWobbleDepth:
      return static_cast<float>(w.wobble.depth_pct);
    case kAppWobbleReset:
      return w.wobble.reset ? 1.0f : 0.0f;
    case kAppWobbleTarget:
      return static_cast<float>(w.wobble.target);
    default:
      break;
  }
  return 0.0f;
}

// The values of the app a project starts from (and a missing entry keeps): Wiring's own defaults (checked against Wiring{} in the test), the master
// at 100 %, FILL's first variant, the wobble's own defaults.
float app_default(unsigned id) noexcept {
  const WobbleSettings wob;
  switch (id) {
    case kAppLevel:
      return -6.0f;
    case kAppSend:
      return 30.0f;
    case kAppMaster:
      return 100.0f;
    case kAppFill:
      return static_cast<float>(seq::FillVariant::Mutate);
    case kAppWobbleRate:
      return static_cast<float>(wob.rate);
    case kAppWobbleShape:
      return static_cast<float>(wob.shape);
    case kAppWobbleDepth:
      return static_cast<float>(wob.depth_pct);
    case kAppWobbleReset:
      return wob.reset ? 1.0f : 0.0f;
    case kAppWobbleTarget:
      return static_cast<float>(wob.target);
    default:
      break;
  }
  return 0.0f;
}

// One value of the app, clamped to its range; a value that is not finite leaves it as it is.
void set_app_value(Wiring& w, dsp::Engine& e, unsigned id, float v) noexcept {
  if (!std::isfinite(v)) {
    return;
  }
  switch (id) {
    case kAppLevel:
      w.level_db = clampf(v, -60.0f, 3.5f, w.level_db);
      break;
    case kAppSend:
      w.send_a_pct = clampf(v, 0.0f, 100.0f, w.send_a_pct);
      break;
    case kAppMaster:
      (void)e.set_master_level(clampf(v, 0.0f, 150.0f, 100.0f) * 0.01f);
      break;
    case kAppFill:
      if (w.seq != nullptr) {
        w.seq->set_fill_variant(static_cast<seq::FillVariant>(static_cast<unsigned>(clampf(v, 0.0f, static_cast<float>(seq::kFillVariantCount - 1u), 0.0f) + 0.5f)));
      }
      break;
    case kAppWobbleRate:
      w.wobble.rate = static_cast<std::uint8_t>(clampf(v, 0.0f, static_cast<float>(seq::kWobbleRateCount - 1u), 0.0f) + 0.5f);
      break;
    case kAppWobbleShape:
      w.wobble.shape = static_cast<std::uint8_t>(clampf(v, 0.0f, static_cast<float>(seq::kWobbleShapeCount - 1u), 0.0f) + 0.5f);
      break;
    case kAppWobbleDepth:
      w.wobble.depth_pct = static_cast<std::uint8_t>(clampf(v, 0.0f, 100.0f, 50.0f) + 0.5f);
      break;
    case kAppWobbleReset:
      w.wobble.reset = v >= 0.5f;
      break;
    case kAppWobbleTarget:
      w.wobble.target = static_cast<std::uint8_t>(clampf(v, 0.0f, static_cast<float>(kWobbleTargetCount - 1u), 0.0f) + 0.5f);
      break;
    default:
      break;  // a value a later build has
  }
}

// ---- The patterns ------------------------------------------------------------------------------------------------------------------------------

// A pattern from a file that passed its CRC but may come from anywhere: every field the sequencer indexes with is brought into its range.
void sanitize(Pattern& p) noexcept {
  for (Step& s : p.steps) {
    s.note = s.note > 127u ? std::uint8_t{127} : s.note;
    s.flags = static_cast<std::uint8_t>(s.flags & 0x3Fu);  // bits 6-7 are reserved and stay 0
  }
  if (!valid_length(p.length)) {
    p.length = kDefaultLength;
  }
  p.swing = p.swing > kMaxSwing ? kMaxSwing : p.swing;
  p.transpose = p.transpose < -24 ? std::int8_t{-24} : (p.transpose > 24 ? std::int8_t{24} : p.transpose);
  p.gate = p.gate < 5u ? std::uint8_t{5} : (p.gate > 100u ? std::uint8_t{100} : p.gate);
  for (LaneRef& l : p.lanes) {
    if (l.target >= kLaneTargetCount) {
      l = LaneRef{kLaneNone, 0};
    }
  }
}

// ---- Writing -----------------------------------------------------------------------------------------------------------------------------------

bool write_sound(chunk::Writer& w, const ProjectState& s) noexcept {
  const unsigned n = s.sound_n < kMaxSound ? s.sound_n : kMaxSound;
  bool ok = w.begin("SND ") && w.u16(kVersion) && w.u16(static_cast<std::uint16_t>(n));
  for (unsigned i = 0; i < n; ++i) {
    ok = w.u8(s.sound[i].target) && w.u8(0) && w.u16(s.sound[i].id) && w.f32(s.sound[i].value) && ok;
  }
  return w.end() && ok;
}

// ---- Reading -----------------------------------------------------------------------------------------------------------------------------------

enum Seen : unsigned { kSeenHead = 1, kSeenGen = 2, kSeenPats = 4, kSeenSnd = 8, kSeenSnap = 16, kSeenPerf = 32 };

// The payload of a chunk past its version: the version's major, 0 when there is none.
unsigned major_of(chunk::Fields& f, std::uint16_t& version) noexcept {
  version = f.u16();
  return f.ok() ? (static_cast<unsigned>(version) >> 8) : 0u;
}

void read_head(chunk::Fields& f, ProjectState& out) noexcept {
  const unsigned sel = f.u8();
  out.selected = static_cast<std::uint8_t>(sel < kPatternCount ? sel : 0u);  // a pattern this build has not: the first
  const std::uint32_t tempo = f.u32();
  out.tempo_mbpm = tempo < dsp::kMinTempoMilliBpm ? dsp::kMinTempoMilliBpm : (tempo > dsp::kMaxTempoMilliBpm ? dsp::kMaxTempoMilliBpm : tempo);
  out.scale_set = (f.u8() & kFlagScaleSet) != 0u;
  char name[kNameChars];
  for (char& c : name) {
    c = static_cast<char>(f.u8());
  }
  bool end = false;
  for (unsigned i = 0; i < kNameChars; ++i) {
    end = end || name[i] == '\0' || i == kNameChars - 1u;
    const bool printable = name[i] >= 0x20 && name[i] <= 0x7E;
    out.name[i] = end ? '\0' : (printable ? name[i] : '_');
  }
  out.save_count = f.left() >= 4u ? f.u32() : 0u;  // HEAD 1.1 and later; a 1.0 file ends at the name
}

// false: the chunk is short; `out` is as it was.
bool read_gen(chunk::Fields& f, ProjectState& out) noexcept {
  seq::GenParams g;
  g.scale = f.u8();
  g.root = f.u8();
  g.low = f.u8();
  g.high = f.u8();
  g.density = f.u8();
  g.accent_pct = f.u8();
  g.slide_pct = f.u8();
  if (!f.ok()) {
    return false;
  }
  g.scale = g.scale < core::kScaleCount ? g.scale : seq::GenParams{}.scale;
  g.root = g.root < 12u ? g.root : std::uint8_t{0};
  g.low = g.low > 127u ? std::uint8_t{127} : g.low;
  g.high = g.high > 127u ? std::uint8_t{127} : g.high;
  if (g.high < g.low) {
    g.high = g.low;
  }
  g.density = g.density > 100u ? std::uint8_t{100} : g.density;
  g.accent_pct = g.accent_pct > 100u ? std::uint8_t{100} : g.accent_pct;
  g.slide_pct = g.slide_pct > 100u ? std::uint8_t{100} : g.slide_pct;
  out.gen = g;
  return true;
}

// false: the chunk is not usable as a whole (a pattern of another size, or fewer bytes than it says).
bool read_pats(chunk::Fields& f, const chunk::Chunk& c, ProjectState& out) noexcept {
  const unsigned count = f.u8();
  const unsigned bytes = f.u16();
  if (!f.ok() || bytes != sizeof(Pattern) || f.left() < std::size_t{count} * bytes) {
    return false;
  }
  out.pats = c.data + (c.size - f.left());
  out.pats_n = count < kPatternCount ? count : kPatternCount;
  return true;
}

// false: short or inconsistent; `out` is as it was. Entries past kMaxSound are dropped (`dropped` counts them).
bool read_sound(chunk::Fields& f, ProjectState& out, unsigned& dropped) noexcept {
  const unsigned n = f.u16();
  if (!f.ok() || f.left() < std::size_t{n} * kSoundEntryBytes) {
    return false;
  }
  unsigned kept = 0;
  for (unsigned i = 0; i < n; ++i) {
    SoundEntry e;
    e.target = f.u8();
    f.skip(1);
    e.id = f.u16();
    e.value = f.f32();
    if (!std::isfinite(e.value)) {
      ++dropped;  // NaN and Inf are no value: the parameter keeps its default
    } else if (kept < kMaxSound) {
      out.sound[kept++] = e;
    } else {
      ++dropped;
    }
  }
  out.sound_n = kept;
  return true;
}

bool read_snap(chunk::Fields& f, ProjectState& out) noexcept {
  const unsigned slots = f.u8();
  const unsigned values = f.u8();
  const unsigned used = f.u8();
  if (!f.ok() || f.left() < std::size_t{slots} * values * 4u) {
    return false;
  }
  for (unsigned s = 0; s < slots; ++s) {
    for (unsigned v = 0; v < values; ++v) {
      const float x = f.f32();
      if (s < kSnapSlots && v < kSnapValues) {
        out.snap[s][v] = std::isfinite(x) ? x : std::numeric_limits<float>::quiet_NaN();  // NaN: filled from the sound by apply()
      }
    }
  }
  out.snap_values = static_cast<std::uint8_t>(values < kSnapValues ? values : kSnapValues);
  out.snap_used = static_cast<std::uint8_t>(slots < kSnapSlots ? (used & ((1u << slots) - 1u)) : used);
  return true;
}

void read_perf(chunk::Fields& f, ProjectState& out) noexcept {
  for (unsigned b = 0; b < kPerfButtons && f.left() > 0; ++b) {
    const std::uint8_t a = f.u8();
    out.perf[b] = a < kPerfActionCount ? a : kDefaultPerf[b];
  }
}

}  // namespace

std::uint8_t* buffer() noexcept { return g_buffer; }

// ---- gather / encode ---------------------------------------------------------------------------------------------------------------------------

void gather(const Rig& rig, const dsp::Engine& e, ProjectState& out) noexcept {
  const Wiring& w = rig.wiring;
  out.selected = static_cast<std::uint8_t>(rig.bank != nullptr ? rig.bank->selected() : 0u);
  out.tempo_mbpm = rig.tempo_base_mbpm != 0u ? rig.tempo_base_mbpm : e.own_tempo();  // a tempo nudge on: the tempo it started from
  out.scale_set = w.scale_set;
  out.gen = w.gen;

  out.sound_n = 0;
  for_each_sound(w, e, [&](std::uint8_t target, dsp::UnitId unit, std::uint16_t id, const core::ParamDesc&) {
    if (out.sound_n < kMaxSound) {
      out.sound[out.sound_n++] = SoundEntry{target, id, e.param(unit, id)};
    }
  });
  for (unsigned id = 0; id < kAppValueCount && out.sound_n < kMaxSound; ++id) {
    out.sound[out.sound_n++] = SoundEntry{kSndApp, static_cast<std::uint16_t>(id), app_value(rig, e, id)};
  }

  // The snapshots' values: a copy of the bank is asked for each used slot's end of the morph (a discrete read of the recalled one is that slot's
  // value exactly), so that the live morph state is not touched.
  out.snap_values = static_cast<std::uint8_t>(kSnapTargetCount);
  out.snap_used = 0;
  SnapBank copy = w.snaps;
  for (unsigned s = 0; s < kSnapSlots; ++s) {
    if (copy.recall(s, nullptr)) {
      out.snap_used = static_cast<std::uint8_t>(out.snap_used | (1u << s));
      for (unsigned i = 0; i < kSnapValues; ++i) {
        out.snap[s][i] = copy.value(i, true);
      }
    } else {
      for (unsigned i = 0; i < kSnapValues; ++i) {
        out.snap[s][i] = 0.0f;
      }
    }
  }

  for (unsigned b = 0; b < kPerfButtons; ++b) {
    out.perf[b] = w.perf[b];
  }
  out.pats = nullptr;
  out.pats_n = 0;
}

namespace {

// The chunks before PATS: HEAD and GEN.
bool write_head_gen(chunk::Writer& w, const ProjectState& s, std::uint32_t save_count) noexcept {
  char name[kNameChars] = {};
  for (unsigned i = 0; i + 1u < kNameChars && s.name[i] != '\0'; ++i) {
    name[i] = s.name[i];
  }
  bool ok = w.begin("HEAD") && w.u16(kHeadVersion) && w.u8(s.selected) && w.u32(s.tempo_mbpm) && w.u8(s.scale_set ? kFlagScaleSet : 0u) &&
            w.bytes(name, kNameChars) && w.u32(save_count) && w.end();

  return ok && w.begin("GEN ") && w.u16(kVersion) && w.u8(s.gen.scale) && w.u8(s.gen.root) && w.u8(s.gen.low) && w.u8(s.gen.high) && w.u8(s.gen.density) &&
         w.u8(s.gen.accent_pct) && w.u8(s.gen.slide_pct) && w.end();
}

// The chunks after PATS (SG1 adds SONG here, and only here).
bool write_tail(chunk::Writer& w, const ProjectState& s) noexcept {
  bool ok = write_sound(w, s);

  ok = ok && w.begin("SNAP") && w.u16(kVersion) && w.u8(static_cast<std::uint8_t>(kSnapSlots)) && w.u8(static_cast<std::uint8_t>(kSnapTargetCount)) &&
       w.u8(s.snap_used);
  for (unsigned slot = 0; slot < kSnapSlots && ok; ++slot) {
    for (unsigned i = 0; i < kSnapTargetCount && ok; ++i) {
      ok = w.f32(s.snap[slot][i]);
    }
  }
  ok = ok && w.end();

  ok = ok && w.begin("PERF") && w.u16(kVersion);
  for (unsigned b = 0; b < kPerfButtons && ok; ++b) {
    ok = w.u8(s.perf[b]);
  }
  return ok && w.end();
}

bool write_pats_header(chunk::Writer& w) noexcept {
  return w.begin("PATS") && w.u16(kVersion) && w.u8(static_cast<std::uint8_t>(kPatternCount)) && w.u16(static_cast<std::uint16_t>(sizeof(Pattern)));
}

// The most the small chunks take (everything but PATS and the container's own bytes), with SG1's reserve.
constexpr std::size_t kSmallMax = chunk::kHeaderBytes + chunk_bytes(kHeadPayload) + chunk_bytes(kGenPayload) + chunk_bytes(2 + 2 + std::size_t{kMaxSound} * kSoundEntryBytes) +
                                  chunk_bytes(kSnapHeader + std::size_t{kSnapSlots} * kSnapTargetCount * 4) + chunk_bytes(kPerfPayload) + kSongReserve +
                                  chunk::kCrcChunkBytes;
O3SDK_BIG std::uint8_t g_small[kSmallMax];

void put_be32(std::uint8_t* p, std::uint32_t v) noexcept {
  p[0] = static_cast<std::uint8_t>(v >> 24);
  p[1] = static_cast<std::uint8_t>(v >> 16);
  p[2] = static_cast<std::uint8_t>(v >> 8);
  p[3] = static_cast<std::uint8_t>(v);
}

}  // namespace

std::size_t encode(const ProjectState& s, const seq::Bank& bank, std::uint8_t* out, std::size_t cap) noexcept {
  ++g_generation;  // whatever a decode() left pointing into a buffer is not to be trusted from now on
  chunk::Writer w(out, cap, "303P");

  bool ok = write_head_gen(w, s, s.save_count) && write_pats_header(w);
  for (unsigned i = 0; i < kPatternCount && ok; ++i) {
    ok = w.bytes(&bank.pattern(i), sizeof(Pattern));
  }
  ok = ok && w.end() && write_tail(w, s);

  const std::size_t n = w.finish();
  return ok ? n : 0u;
}

std::uint32_t small_crc(const ProjectState& s) noexcept {
  chunk::Writer w(g_small, sizeof g_small, "303P");
  const bool ok = write_head_gen(w, s, 0) && write_tail(w, s);  // (the save counter moves with every write: it is no change of the session)
  // A state that does not fit (it cannot: kSmallMax holds the largest) still has a value, different from every image that did.
  return ok ? o3sdk::formats::crc32(g_small, w.size()) : 0u;
}

// ---- Encoder -----------------------------------------------------------------------------------------------------------------------------------

void Encoder::begin(const ProjectState& s, const seq::Bank& bank, std::uint8_t* out, std::size_t cap) noexcept {
  ++g_generation;
  bank_ = &bank;
  out_ = out;
  w_ = chunk::Writer(out, cap, "303P");
  size_ = 0;
  next_ = 0;
  body_ = 0;
  crc_at_ = 0;
  s_ = &s;
  stage_ = (write_head_gen(w_, s, s.save_count) && write_pats_header(w_)) ? Stage::Patterns : Stage::Failed;
}

bool Encoder::step(unsigned patterns, std::size_t crc_bytes) noexcept {
  switch (stage_) {
    case Stage::Patterns: {
      bool ok = true;
      for (unsigned n = 0; n < patterns && next_ < kPatternCount && ok; ++n, ++next_) {
        ok = w_.bytes(&bank_->pattern(next_), sizeof(Pattern));
      }
      if (ok && next_ >= kPatternCount) {
        ok = w_.end() && write_tail(w_, *s_);
        if (ok) {
          body_ = w_.size();
          // What Writer::finish() does, in slices: the FORM length first (the CRC covers it), then the CRC over the body, then the CRC chunk.
          put_be32(out_ + 4, static_cast<std::uint32_t>(body_ + chunk::kCrcChunkBytes - 8u));
          crc_.reset();
          stage_ = Stage::Crc;
        }
      }
      if (!ok) {
        stage_ = Stage::Failed;
      }
      break;
    }
    case Stage::Crc: {
      const std::size_t n = body_ - crc_at_ < crc_bytes ? body_ - crc_at_ : crc_bytes;
      crc_.update(out_ + crc_at_, n);
      crc_at_ += n;
      if (crc_at_ >= body_) {
        std::uint8_t* t = out_ + body_;
        std::memcpy(t, "CRC ", 4);
        put_be32(t + 4, 6);
        t[8] = static_cast<std::uint8_t>(chunk::kCrcVersion >> 8);
        t[9] = static_cast<std::uint8_t>(chunk::kCrcVersion);
        put_be32(t + 10, crc_.value());
        size_ = body_ + chunk::kCrcChunkBytes;
        stage_ = Stage::Done;
      }
      break;
    }
    case Stage::Idle:
    case Stage::Done:
    case Stage::Failed:
      break;
  }
  return stage_ == Stage::Done || stage_ == Stage::Failed || stage_ == Stage::Idle;
}

// ---- decode ------------------------------------------------------------------------------------------------------------------------------------

Result decode(const std::uint8_t* data, std::size_t n, ProjectState& out) noexcept {
  out = ProjectState{};
  out.pats_generation = g_generation;
  Result res;
  chunk::Reader r;
  res.error = r.open(data, n, "303P");
  if (res.error != chunk::Error::None) {
    return res;  // Damaged
  }

  unsigned seen = 0;
  chunk::Chunk c;
  while (r.next(c)) {
    unsigned bit = 0;
    if (chunk::id_is(c, "HEAD")) {
      bit = kSeenHead;
    } else if (chunk::id_is(c, "GEN ")) {
      bit = kSeenGen;
    } else if (chunk::id_is(c, "PATS")) {
      bit = kSeenPats;
    } else if (chunk::id_is(c, "SND ")) {
      bit = kSeenSnd;
    } else if (chunk::id_is(c, "SNAP")) {
      bit = kSeenSnap;
    } else if (chunk::id_is(c, "PERF")) {
      bit = kSeenPerf;
    }
    chunk::Fields f(c);
    std::uint16_t version = 0;
    const unsigned major = major_of(f, version);

    if (bit == kSeenHead && (seen & kSeenHead) == 0u) {
      if (major > kMajor) {
        res.status = Status::FromNewer;
        return res;
      }
      if (major != kMajor) {
        return res;  // Damaged: HEAD with no version, or a major this build never wrote
      }
      read_head(f, out);
      if (!f.ok()) {
        return res;  // Damaged: HEAD is short
      }
      seen |= kSeenHead;
      continue;
    }
    if (bit == 0u || (seen & bit) != 0u || major != kMajor) {
      ++res.skipped;  // unknown (SONG too, for now), twice, or of a major this build cannot read: its part keeps its defaults
      continue;
    }
    bool good = false;
    switch (bit) {
      case kSeenGen:
        good = read_gen(f, out);
        break;
      case kSeenPats:
        good = read_pats(f, c, out);
        break;
      case kSeenSnd:
        good = read_sound(f, out, res.skipped);
        break;
      case kSeenSnap:
        good = read_snap(f, out);
        break;
      case kSeenPerf:
        read_perf(f, out);
        good = true;
        break;
      default:
        break;
    }
    if (good) {
      seen |= bit;
    } else {
      ++res.skipped;
    }
  }
  if ((seen & kSeenHead) == 0u) {
    return res;  // Damaged: no HEAD
  }
  res.patterns_ok = (seen & kSeenPats) != 0u;
  res.status = Status::Ok;
  return res;
}

// ---- apply -------------------------------------------------------------------------------------------------------------------------------------

// The last finite entry of (target, id): the value a parameter is set to, else its default.
bool find_entry(const ProjectState& s, std::uint8_t target, std::uint16_t id, float& value) noexcept {
  bool found = false;
  for (unsigned i = 0; i < s.sound_n && i < kMaxSound; ++i) {
    const SoundEntry& en = s.sound[i];
    if (en.target == target && en.id == id && std::isfinite(en.value)) {
      value = en.value;
      found = true;
    }
  }
  return found;
}

bool apply(Rig& rig, dsp::Engine& e, const ProjectState& s, bool patterns) noexcept {
  if (patterns && s.pats != nullptr && s.pats_generation != g_generation) {
    return false;  // an encode() since decode(): the buffer holds another image
  }
  Wiring& w = rig.wiring;
  (void)e.stop();

  // The patterns: the project's, else the fresh pattern; no history entry, and the undo of the old project goes.
  if (patterns && rig.bank != nullptr) {
    alignas(4) Pattern p;
    for (unsigned i = 0; i < kPatternCount; ++i) {
      if (s.pats != nullptr && i < s.pats_n) {
        std::memcpy(&p, s.pats + std::size_t{i} * sizeof(Pattern), sizeof(Pattern));
        sanitize(p);
      } else {
        clear(p);
      }
      (void)rig.bank->restore(i, p);
    }
    rig.bank->select(s.selected < kPatternCount ? s.selected : 0u);
    rig.bank->clear_history();
  }

  // The sound: every parameter the project carries is written once, with the entry if the file has it, else its default (entries for parameters this
  // build has not are never looked at), so no block plays a default in between.
  for_each_sound(w, e, [&](std::uint8_t target, dsp::UnitId unit, std::uint16_t id, const core::ParamDesc& d) {
    float v = d.def;
    (void)find_entry(s, target, id, v);
    (void)e.set_param(unit, id, v);  // clamped by the engine
  });
  for (unsigned id = 0; id < kAppValueCount; ++id) {
    float v = app_default(id);
    (void)find_entry(s, kSndApp, static_cast<std::uint16_t>(id), v);
    set_app_value(w, e, id, v);
  }
  rig.controls.apply_level(e);
  rig.controls.apply_send(e);
  rig.controls.apply_wobble(false);

  (void)e.set_tempo(s.tempo_mbpm);
  w.gen = s.gen;
  w.scale_set = s.scale_set;
  for (unsigned b = 0; b < kPerfButtons; ++b) {
    w.perf[b] = s.perf[b] < kPerfActionCount ? s.perf[b] : kDefaultPerf[b];
  }

  // The snapshots, after the sound: a slot with fewer values than the targets of today is filled from the sound as it is now.
  w.snaps.clear();
  float live[kSnapValues];
  rig.controls.snap_capture(e, live);
  for (unsigned slot = 0; slot < kSnapSlots; ++slot) {
    if ((s.snap_used & (1u << slot)) == 0u) {
      continue;
    }
    float v[kSnapValues];
    for (unsigned i = 0; i < kSnapValues; ++i) {
      const float x = s.snap[slot][i];
      v[i] = (i < s.snap_values && i < kSnapTargetCount && std::isfinite(x)) ? x : live[i];
    }
    w.snaps.store(slot, v);
  }
  return true;
}

}  // namespace o3_303::project
