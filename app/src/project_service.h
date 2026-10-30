// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The project service (docs/UX-303.md 8.2 and 8.4 to 8.6, docs/UX-PLAN.md F2): the session's project on the card. It keeps `Apps/o3_303/<NAME>/live.o3a`
// current while the player works (autosave), writes `project.o3a` on SAVE (Shift + File), loads the last project at boot, and tells the screens what it
// is doing through Rig::project. It owns the project image buffer (project::buffer(), 128 KiB of SDRAM) and the two small states it gathers into (about
// 7 KB, in the SDRAM): one service per image, like the buffer.
//
// The card is never waited for. Every file call is the kit's queued job (app::Files: a call records a request, the runtime runs it 2 ms a pass, one
// job at a time); the service holds a small state machine over that and only ever asks when `Files::busy()` is false. The save is spread over passes
// at both ends: the ENCODE is project::Encoder's, kPatternsPerPass patterns (or kCrcBytesPerPass of the CRC) a pass, about 1 to 2.5 ms each on the
// MK66 by the estimate (the whole image is 8 to 20 ms in one pass, a frame or more: U2 measures it, and a larger constant here is one line), and the
// WRITE is the kit's. A pattern edited between two steps of an encode lands in the image as it was when copied; the change detection runs every pass and
// sees the edit as a change after the save started, so the next autosave carries it. Nothing is lost, only delayed by one period.
//
// What counts as a change (no edit counter is threaded through the code): the sum of Bank::version(i) moved (any edit, undo, redo or load of a
// pattern), or, checked once a second, the CRC of everything but PATS (project::small_crc: the sound, the snapshots, the assignments, the generator,
// the tempo, the selected pattern, the name) differs from the last one seen. AutosaveClock (app/include/o3_303/autosave.h) turns changes into "write
// now": AUTOSAVE seconds after the last change, at most 60 s after the first unsaved one.
//
// The card, D11, D25 (UX-303 8.6):
//   - No card at boot (or none that the files service can use): the factory line, the project UNTITLED, no file call at all, autosave off. A card put
//     in later changes nothing by itself: the session gets a project on it by SAVE (here, SAVE AS under the first free name UNTITLED, UNTITLED 2, ...,
//     so an existing project is never overwritten by surprise), or by F3's SAVE AS / LOAD / NEW.
//   - A card at boot: `last.txt` names the project; its live.o3a (then project.o3a) is loaded and applied, but only if the player has not edited anything
//     and the sequencer is not running when it arrives; otherwise it is dropped and the session goes on as with no card. No last.txt: UNTITLED if it is
//     there, else the session is a new project called UNTITLED that autosaves there (and writes last.txt with its first save).
//   - A file that is damaged, from a newer o3-303 or too big is never written over: the session has no project on the card (autosave off, a notice)
//     until SAVE. So is a boot where any read fails for another reason than NOT FOUND (an I/O error says nothing about what the card holds), and a
//     boot load dropped because the player had already edited or played. A project whose patterns were skipped (Result::patterns_ok false) is loaded
//     and autosaves once the player has changed anything.
//   - A card that goes out, comes in or goes to a computer (Files::generation() moves) may not be the card the project is on, and a swapped one can hold
//     a project of the same name: autosave goes off ("CARD CHANGED: SAVE TO KEEP THE PROJECT") until SAVE (under a free name) or a load gives the session
//     a project on this card. The same card back after a pull also needs a SAVE: the service cannot tell the cards apart.
//   - Write protected, on a computer or in use by the firmware update without the card having changed: no file call. An autosave that is due waits and
//     shows SAVE FAILED with the reason; it runs by itself when the card is writable again. A write that fails (card full: the kit's atomic save leaves
//     the old file or the new one) shows SAVE FAILED once, until a save works, and is tried again at the next change.
//   - A SAVE writes live.o3a first, then project.o3a, then last.txt: boot loads live.o3a first, so a card pulled mid-SAVE still holds the newest image there.
//
// Loss bound: without a power-off notice from the kit (o3-sdk need N1) the work lost at a power cut is the changes of the last AUTOSAVE period
// (10 s by default) plus the time of one save in flight, and at most 60 s while edits never pause. With the notice a last autosave starts there.
//
// Contexts: main loop only (update() runs after the sequencer's, once a pass).

#include "project.h"
#include "rig.h"

#include <o3_303/autosave.h>
#include <o3sdk/app/context.h>
#include <o3sdk/app/files.h>
#include <o3sdk/dsp/engine.h>

#include <cstddef>
#include <cstdint>

namespace o3_303 {

class ProjectService {
  enum class Op : std::uint8_t { None, LoadLast, LoadProject, SaveProject, SaveLive, SaveLast, List };

 public:
  static constexpr const char* kDefaultName = "UNTITLED";
  static constexpr unsigned kPatternsPerPass = 6;                // the encode's slice: 13.5 KB copied
  static constexpr std::size_t kCrcBytesPerPass = 16u * 1024u;   // and the CRC's
  static constexpr std::uint32_t kCheckMs = 1000;                // the small chunks' CRC is looked at this often
  static constexpr unsigned kListMax = 48;                       // folders SAVE AS looks through for a free name
  static_assert(ProjectInfo::kNameChars == project::kNameChars, "the name the screens see is the name the file holds");

  enum class Phase : std::uint8_t {
    Start,     // before the first pass
    BootLast,  // reading last.txt
    BootLoad,  // reading a project's file
    Idle,
    Listing,   // SAVE AS: reading the folders to find a free name
    Encoding,  // the image is being built (no card call)
    Writing,   // live.o3a, project.o3a (a SAVE), last.txt (when it does not say the name yet), one job each
  };

  // setup(): the session is the factory line under the name UNTITLED, with no project on the card yet. Call it last, after the session's own setup
  // (it records what the session is then, so that a boot load knows whether the player has edited anything).
  void init(Rig& rig, const o3sdk::dsp::Engine& e) noexcept;

  // Once a pass. Boots (first pass), watches for changes, runs the save in progress or starts the next one.
  void update(o3sdk::app::Context& ctx) noexcept;

  // SAVE (Shift + File, anywhere but the vault): writes live.o3a, then project.o3a, as soon as no other card job runs. A session with no project on the card saves as SAVE AS
  // under the first free UNTITLED name (D11). Taken at the next pass.
  void save() noexcept { save_req_ = true; as_valid_ = false; }
  // SAVE AS under `name` (a valid project name: 1..15 characters of letters, digits, space, '_' and '-'): the session becomes that project. false: refused,
  // nothing asked. For F3's name entry.
  bool save_as(const char* name) noexcept;

  // AUTOSAVE: the period in milliseconds, 0 is OFF (Config, C1). Changes made while it is off are saved when it is turned on.
  void set_autosave_period(std::uint32_t ms) noexcept { clock_.set_period(ms); }
  std::uint32_t autosave_period() const noexcept { return clock_.period(); }

  // ---- What the screens and the tests read --------------------------------------------------------------------------------------------------
  const ProjectInfo& info() const noexcept { return rig_->project; }  // what the screens see (after init())
  const char* name() const noexcept { return name_; }
  bool has_project() const noexcept { return home_; }  // the session has a project on the card: autosave is on
  Phase phase() const noexcept { return phase_; }
  bool idle() const noexcept { return phase_ == Phase::Idle; }
  bool changes_waiting() const noexcept { return clock_.pending(); }
  // The number of saves of live.o3a that ended well since init() (tests: "an autosave happened").
  std::uint32_t saves() const noexcept { return saves_; }

 private:
  // boot
  void boot_start(o3sdk::app::Context& ctx) noexcept;
  void boot_issue(o3sdk::app::Context& ctx) noexcept;
  void boot_settle(o3sdk::app::Context& ctx, Op ended, bool done, const char* text) noexcept;
  void boot_next(o3sdk::app::Context& ctx) noexcept;
  void boot_apply(o3sdk::app::Context& ctx, const project::Result& r) noexcept;
  void boot_end(o3sdk::app::Context& ctx, bool project_on_card) noexcept;
  // changes
  std::uint32_t version_sum() const noexcept;
  bool edited(const o3sdk::dsp::Engine& e) noexcept;
  void rebaseline(const o3sdk::dsp::Engine& e, std::uint64_t now_ms) noexcept;
  void observe(o3sdk::app::Context& ctx) noexcept;
  // saves
  void idle_update(o3sdk::app::Context& ctx) noexcept;
  void start_save(o3sdk::app::Context& ctx) noexcept;
  void begin_encode(o3sdk::app::Context& ctx, const char* target, bool explicit_save) noexcept;
  void encode_step(o3sdk::app::Context& ctx) noexcept;
  void write_issue(o3sdk::app::Context& ctx) noexcept;
  void write_settle(o3sdk::app::Context& ctx, Op ended) noexcept;
  void choose_name(o3sdk::app::Context& ctx) noexcept;
  void pick_name(o3sdk::app::Context& ctx, unsigned first) noexcept;
  void card_watch(o3sdk::app::Context& ctx) noexcept;
  void save_ended(o3sdk::app::Context& ctx) noexcept;
  void undo_rename() noexcept;
  void fail(o3sdk::app::Context& ctx, const char* text) noexcept;
  // reporting
  void notice(const char* text) noexcept;
  void show_failed(const char* text) noexcept;
  void publish() noexcept;
  static const char* card_text(o3sdk::app::Files::Card c) noexcept;

  Rig* rig_ = nullptr;
  Phase phase_ = Phase::Start;
  Op op_ = Op::None;     // the kit's job this service has in flight (or none)
  unsigned step_ = 0;    // BootLoad: the attempt; Writing: 0 project.o3a, 1 live.o3a, 2 last.txt
  AutosaveClock clock_;
  project::Encoder enc_;

  char name_[project::kNameChars] = "UNTITLED";       // the session's project
  char prev_name_[project::kNameChars] = "UNTITLED";  // what name_ goes back to when a SAVE AS fails
  char last_name_[project::kNameChars] = "";          // what last.txt holds on the card ("" none)
  char as_name_[project::kNameChars] = "";            // SAVE AS: the name asked for
  bool as_valid_ = false;
  bool last_dirty_ = false;  // last.txt does not say name_ yet
  bool home_ = false;        // name_ is a project on the card that autosave writes
  bool save_req_ = false;
  bool auto_name_ = false;   // the name was picked by pick_name() (a SAVE with no project), so a refused name can go on to the next
  unsigned cand_ = 0;        // its number: 1 UNTITLED, 2 UNTITLED 2, ...
  unsigned list_n_ = 0;      // entries of big_->list in use
  bool renamed_ = false;     // the save in progress gives the session a name (a SAVE AS, or the first save of a session with no project)
  bool explicit_ = false;    // the save in progress is a SAVE (project.o3a too), not an autosave
  bool failed_ = false;
  bool blocked_ = false;     // boot: a file that must not be written over was found
  bool damaged_ = false;
  std::uint32_t saves_ = 0;
  std::uint32_t gen_ = 0;    // Files::generation() at the last look

  // boot: the session as setup left it, and the projects to try (last.txt's, then UNTITLED)
  std::uint32_t boot_sum_ = 0;
  std::uint32_t boot_crc_ = 0;
  char names_[2][project::kNameChars] = {};
  unsigned n_names_ = 0;

  // change detection: what was seen last
  std::uint32_t seen_sum_ = 0;
  std::uint32_t seen_crc_ = 0;
  std::uint64_t next_check_ms_ = 0;
  std::size_t image_bytes_ = 0;

  // The big scratch (about 7 KB) lives in the SDRAM, built by init() in zero-filled O3SDK_BIG storage (a ProjectState has non-zero defaults, so it cannot be
  // an O3SDK_BIG object itself): SRAM_U has only the budget the linker script keeps for the stack.
  struct Scratch {
    project::ProjectState state;  // the image being built (the Encoder reads it until the encode is done)
    project::ProjectState probe;  // what change detection gathers: never the one the Encoder holds
    o3sdk::app::Files::Entry list[kListMax] = {};
    char last[32] = {};           // last.txt, read or to write
  };
  Scratch* big_ = nullptr;
};

// The running app's service, for the tests (defined in o3_303_app.cpp).
ProjectService& app_project() noexcept;

}  // namespace o3_303
