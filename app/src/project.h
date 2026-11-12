// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The project codec (docs/UX-303.md 8.1 and 8.3, docs/UX-PLAN.md F1): everything needed to come back to the jam, as bytes. No file I/O, no screen:
// the project service (F2) moves the image to and from the card. Format 3.0 (2.0 was docs/PADS-TRACT-PLAN.md P; 3.0 dropped PERF's colour-key bytes, PF1): a chunk file (o3sdk/formats/chunk_file.h) of FORM
// type "303P", every chunk's payload starting with a u16 version (major high byte, minor low, kVersion = 3.0 for all), then the CRC chunk. Seven chunks, each
// present once:
//
//   HEAD  selected pattern u8, tempo u32 (milli-BPM), flags u8 (0), name char[16], the save counter u32: it counts the writes of one project (the service's), so
//         that boot and LOAD take the newest of live.o3a, its .bak and project.o3a
//   GEN   the generator's settings, seven u8 (seq::GenParams in order)
//   PATS  count u8 (48), bytes a pattern u16 (sizeof(Pattern)), then the patterns byte for byte in struct order, the TRACT line of each included
//   SND   n u16; n entries of {target u8, 0 u8, id u16, value f32}: the units' parameters by id (targets 1 voice, 2 distortion, 3 delay, 5 growl, 7 tract,
//         16 the freeze's four settings) and the app's values (target 32, ids AppValue)
//   SNAP  slots u8 (8), values a slot u8 (the snapshot targets), used mask u8, then slots x values f32
//   PERF  the KNOBS set in use u8; every KNOBS set (kKnobSets x kKnobsPerSet rows, target u8 and id u8 each)
//   SONG  length u8 (0..64), then length rows of {pattern u8, repeat u8, transpose i8, flags u8} (seq::SongRow; the song is the Bank's, on the undo stack)
//
// The image is built in one buffer of kProjectBytes (buffer(), O3SDK_BIG), the only one: a save and a load never overlap. The patterns are never copied
// into a ProjectState (112 KB): encode() reads them from the Bank and decode() leaves a pointer into the image, which apply() restores from.
//
// Reading (decode): the container and its CRC are checked first; a damaged image gives nothing. HEAD of an older major (a project of format 1 or 2) is refused
// whole as FromOlder, one of a newer major as FromNewer: nothing of such a file is ever read or applied, and there is no converter. Of a 3.x file all seven
// chunks must be there once, whole and with this build's record sizes, or the file is Damaged (the service then tries the .bak): there is no half-load.
// An unknown chunk, a repeated one or an entry past kMaxSound is skipped (counted in Result::skipped), a newer minor is read as far as this build knows
// it, an SND entry this build has no parameter for is skipped when applied, and a parameter the file does not have keeps its default. A ProjectState that
// decode() says Ok for is always in range.
//
// Contexts: main loop only (apply() reaches the engine, the bank and the sequencer as the screens do).

#include "rig.h"

#include <o3sdk/dsp/engine.h>
#include <o3sdk/formats/chunk_file.h>
#include <o3sdk/formats/crc32.h>
#include <o3_303/pages.h>
#include <o3_303/perform.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/ops.h>
#include <o3_303/snapshot.h>

#include <cstddef>
#include <cstdint>

namespace o3_303::project {

inline constexpr std::size_t kProjectBytes = 128u * 1024u;  // the one image buffer
inline constexpr unsigned kNameChars = 16;                  // with the terminating NUL (a valid_name is at most 15)
inline constexpr unsigned kMaxSound = 128;                  // SND entries a ProjectState holds (about 40 are in use)

inline constexpr std::uint16_t kVersion = 0x0300;  // every chunk's version: major 3, minor 0
inline constexpr unsigned kMajor = 3;
inline constexpr char kSongId[5] = "SONG";

// SND's targets: the lane targets (pattern.h) by parameter id, the freeze, the app's own values.
inline constexpr std::uint8_t kSndFreeze = 16;
inline constexpr std::uint8_t kSndApp = 32;
// The app's values of SND (target kSndApp, id = this), in the units the entries show: dB, percent, percent (0..150), FILL's variant, the wobble's
// rate, shape, depth in percent, reset (0 or 1), target, the WOB latch (0 or 1), MODS heard (0 or 1) and the growl's last mix in percent. Append, never insert.
enum AppValue : std::uint16_t {
  kAppLevel,
  kAppSend,
  kAppMaster,
  kAppFill,
  kAppWobbleRate,
  kAppWobbleShape,
  kAppWobbleDepth,
  kAppWobbleReset,
  kAppWobbleTarget,
  kAppWobbleOn,  // B5a: the WOB latch (0 or 1)
  kAppModsOn,    // F13a: MODS heard (1) or off, the STOCK view (0)
  kAppGrowlLastMix,  // F13a: GROWL F8's "last mix", percent (1..100)
  kAppTractLastMix,  // A1: TRACT's "last mix", percent (1..100)
  kAppTractLine,     // PLAY TRACT on (1, the default) or stopped (0): the tract pattern (vowel line, TONGUE / LIPS locks) drives the mouth
  kAppValueCount,
};

struct SoundEntry {
  std::uint8_t target = 0;
  std::uint16_t id = 0;
  float value = 0.0f;
};

// The small parts of a project (about 2.3 KB). A default one is a new project's: the default sound (no SND entry), no snapshot, the
// generator's defaults, 125 BPM, pattern 0, no patterns (apply() then gives every pattern the fresh one).
struct ProjectState {
  // HEAD
  std::uint8_t selected = 0;
  std::uint32_t tempo_mbpm = 125'000;
  char name[kNameChars] = {};  // informational; gather() leaves it alone: the service owns the name
  std::uint32_t save_count = 0;  // the write this image is (the service sets it before an encode; gather() leaves it alone). small_crc() ignores it
  // GEN
  seq::GenParams gen;
  // SND
  unsigned sound_n = 0;
  SoundEntry sound[kMaxSound] = {};
  // SNAP: kSnapTargetCount values a slot (a NaN, from a damaged file, is filled from the sound by apply())
  std::uint8_t snap_used = 0;  // bit s: slot s holds a snapshot
  float snap[kSnapSlots][kSnapValues] = {};
  // PERF
  // KNOBS: every set of four rows, and the set in use (PERF chunk). Set 1 is `Wiring::knob_sets.row[0]`, the store; `Wiring::ladder` is only the live copy of the set in use.
  KnobSets sets = kKnobFactory;
  std::uint8_t knob_set = 0;
  // SONG (canonical: seq::song_normalize() has been applied)
  seq::Song song = [] {
    seq::Song s;
    seq::clear_song(s);
    return s;
  }();
  // PATS (decode only): kPatternCount patterns of sizeof(Pattern) bytes at `pats`, inside the image decode() was given (null: a new project, apply() gives every pattern the fresh
  // one). The image buffer is shared with encode(), so encode() counts a generation and decode() records it in `pats_generation`: apply() refuses a state whose patterns may have
  // been overwritten by an encode in between (apply() before the next encode, or decode again).
  const std::uint8_t* pats = nullptr;
  std::uint32_t pats_generation = 0;
};

// The image buffer: kProjectBytes of SDRAM (the only one; the service owns it).
std::uint8_t* buffer() noexcept;

// What the session holds now, into `out` (everything but the name and `pats`). The snapshots come from Wiring::snaps, the sound from the engine.
void gather(const Rig& rig, const o3sdk::dsp::Engine& e, ProjectState& out) noexcept;

// The image of `s` and the bank's 48 patterns into `out` (`cap` bytes, kProjectBytes at most useful): its size, 0 when it does not fit. The bank is
// read as it stands (the main loop is its only writer).
std::size_t encode(const ProjectState& s, const seq::Bank& bank, std::uint8_t* out, std::size_t cap) noexcept;

// CRC-32 of the small chunks of `s` as encode() writes them (HEAD, GEN, SND, SNAP, PERF: everything but PATS, about 1.2 KB): the service's change
// detection for all that is not a pattern (the patterns have the bank's versions). Cheap enough for once a second; main loop only (one static buffer).
std::uint32_t small_crc(const ProjectState& s) noexcept;

// encode() in slices, so that no pass of the main loop carries all of it (F2: about 110 KB copied and checksummed, 8 to 20 ms estimated on the MK66, a
// frame or more; U2 measures it). The image is byte for byte the one encode() makes (project_service_test). `s` must stay as it is until done().
//   begin(): HEAD, GEN and PATS's header. step(): one slice of the work, either `patterns` patterns copied (the last slice also writes the small
//   chunks after PATS and closes the container) or `crc_bytes` of the CRC; false while more steps are needed. size() is the image's bytes once done,
//   0 when it failed (or is not finished). The bank is read as it stands at each step: a pattern edited between two steps is in the image as it
//   was when copied, so the caller treats an edit after begin() as a change after the save (the service does: its change detection runs every pass).
// Counts as an encode() for apply()'s generation check. Main loop only.
class Encoder {
 public:
  void begin(const ProjectState& s, const seq::Bank& bank, std::uint8_t* out, std::size_t cap) noexcept;
  bool step(unsigned patterns, std::size_t crc_bytes) noexcept;
  bool done() const noexcept { return stage_ == Stage::Done; }
  bool failed() const noexcept { return stage_ == Stage::Failed; }
  bool running() const noexcept { return stage_ == Stage::Patterns || stage_ == Stage::Crc; }
  std::size_t size() const noexcept { return size_; }
  void abandon() noexcept { stage_ = Stage::Idle; }

 private:
  enum class Stage : std::uint8_t { Idle, Patterns, Crc, Done, Failed };
  Stage stage_ = Stage::Idle;
  const ProjectState* s_ = nullptr;
  const seq::Bank* bank_ = nullptr;
  std::uint8_t* out_ = nullptr;
  o3sdk::formats::chunk::Writer w_{nullptr, 0, "303P"};
  o3sdk::formats::Crc32 crc_;
  unsigned next_ = 0;      // the next pattern to copy
  std::size_t body_ = 0;   // the bytes the CRC covers
  std::size_t crc_at_ = 0; // how far the CRC has got
  std::size_t size_ = 0;
};

enum class Status : std::uint8_t {
  Ok,
  Damaged,    // the container, its CRC, HEAD or a required chunk is bad or missing ("PROJECT DAMAGED"; the .bak is the service's next try)
  FromNewer,  // HEAD's major is newer than this build's ("PROJECT FROM A NEWER O3-303")
  FromOlder,  // HEAD's major is older (a project of format 1 or 2: no converter; "PROJECT FROM AN OLDER O3-303"): refused whole, the file is left as it is
};

struct Result {
  Status status = Status::Damaged;
  o3sdk::formats::chunk::Error error = o3sdk::formats::chunk::Error::None;  // why the container was refused (Damaged only)
  unsigned skipped = 0;  // unknown or repeated chunks skipped, and SND entries dropped: "PART OF THE PROJECT SKIPPED" when not 0
};

// Checks the image and reads it into `out` (reset to a default ProjectState first). `out` is only to be used when the status is Ok, and then every
// field of it is in range (the tempo, the selected pattern, the generator, the assignments, the name; a non-finite SND value is dropped and a
// non-finite SNAP value is NaN, "fill from the sound"). `data` must stay as it is until apply(): `out.pats` points into it.
Result decode(const std::uint8_t* data, std::size_t n, ProjectState& out) noexcept;

// Puts `s` on the session: the transport stops first; the patterns replace the bank's without a history entry (the history is cleared) and the
// selected one is set; the sound goes to the units (every parameter the project carries starts from its default, then the entries are set), to the
// level, the send, the master, the wobble and FILL; the tempo, the generator, the assignments and the snapshots follow. An entry this build has no
// parameter for is skipped. Each parameter is written once, with its final value (no block plays a default in between). Does not touch the live
// actions, the keyboard, the cursor or the undo of anything but the bank. The caller makes sure the sequencer is not playing (the stop here is only
// asked for: F2 waits for !playing before it applies). false, and nothing touched: `s.pats` was invalidated by an encode() since decode().
// `patterns` false (C1's RESET PROJECT, which edits the patterns itself, undoably): the bank, its selection and its history are left alone and `s.pats` is not read.
bool apply(Rig& rig, o3sdk::dsp::Engine& e, const ProjectState& s, bool patterns = true) noexcept;

}  // namespace o3_303::project
