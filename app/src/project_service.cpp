// SPDX-License-Identifier: GPL-3.0-or-later
#include "project_service.h"

#include "live_actions.h"

#include <o3sdk/core/placement.h>
#include <o3sdk/hal/save_guard.h>
#include <o3_303/seq/gen.h>

#include <cstring>
#include <new>

namespace o3_303 {

namespace {

namespace guard = o3sdk::hal::storage::save;
using o3sdk::app::Context;
using o3sdk::app::Files;

constexpr char kLiveFile[] = "live.o3a";
constexpr char kProjectFile[] = "project.o3a";
// The files a project load picks the newest of: project.o3a first, then live.o3a, which wins a tie (the later one wins on >=): the common case, live.o3a as new as or
// newer than project.o3a, needs no third read. (The kit's live.o3a.bak cannot be asked for by name: its guard says BAD NAME. A load of live.o3a that finds the file
// gone reads the .bak by itself, and that is the candidate then.)
constexpr unsigned kCands = 2;
constexpr char kLastFile[] = "last.txt";
constexpr unsigned kSaveSteps = 3;  // Writing: live.o3a, project.o3a, last.txt

const char* cand_file(unsigned c) noexcept { return c == 0u ? kProjectFile : kLiveFile; }

bool same_ci(const char* a, const char* b) noexcept {
  for (;; ++a, ++b) {
    const char x = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + ('a' - 'A')) : *a;
    const char y = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + ('a' - 'A')) : *b;
    if (x != y) {
      return false;
    }
    if (x == '\0') {
      return true;
    }
  }
}

void copy_name(char (&dst)[project::kNameChars], const char* src) noexcept {
  unsigned i = 0;
  for (; i + 1u < project::kNameChars && src[i] != '\0'; ++i) {
    dst[i] = src[i];
  }
  for (; i < project::kNameChars; ++i) {
    dst[i] = '\0';
  }
}

void copy_text(char* dst, std::size_t cap, const char* src) noexcept {
  std::size_t i = 0;
  for (; i + 1u < cap && src[i] != '\0'; ++i) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
}

// A project's folder name: what the card's guard takes as a folder (a valid name with no dot), of at most 15 characters, and not the card's own Samples.
bool project_name_ok(const char* s) noexcept {
  const std::size_t n = std::strlen(s);
  if (n == 0u || n >= project::kNameChars || !guard::valid_name(s) || same_ci(s, "Samples")) {
    return false;
  }
  return std::strchr(s, '.') == nullptr;
}

Files::Card card_of(const Files& f) noexcept { return f.available() ? f.card() : Files::Card::None; }

}  // namespace

const char* ProjectService::card_text(Files::Card c) noexcept {
  switch (c) {
    case Files::Card::Ready: return "";
    case Files::Card::None: return "NO CARD";
    case Files::Card::ReadOnly: return "CARD WRITE PROTECTED";
    case Files::Card::Unsupported: return "CARD NOT SUPPORTED";
    case Files::Card::OnComputer: return "CARD ON COMPUTER";
    case Files::Card::InUse: return "CARD IN USE";
  }
  return "NO CARD";
}

// ---- init, update -------------------------------------------------------------------------------------------------------------------------------

void ProjectService::init(Rig& rig, const o3sdk::dsp::Engine& e) noexcept {
  alignas(Scratch) O3SDK_BIG static unsigned char storage[sizeof(Scratch)];  // SDRAM, zero-filled at boot
  big_ = new (storage) Scratch();
  rig_ = &rig;
  phase_ = Phase::Start;
  op_ = Op::None;
  step_ = 0;
  const std::uint32_t period = clock_.period();  // a setting (Config's AUTOSAVE, C1): it outlives the session
  clock_ = AutosaveClock{};
  clock_.set_period(period);
  enc_ = project::Encoder{};
  copy_name(name_, kDefaultName);
  copy_name(prev_name_, kDefaultName);
  last_name_[0] = '\0';
  as_name_[0] = '\0';
  as_valid_ = false;
  last_dirty_ = false;
  home_ = false;
  save_req_ = false;
  reset_pending_ = false;
  reset_deadline_ms_ = 0;
  explicit_ = false;
  renamed_ = false;
  auto_name_ = false;
  failed_ = false;
  blocked_ = false;
  damaged_ = false;
  saves_ = 0;
  seq_ = 0;
  boot_reads_ = 0;
  pick_best_ = -1;
  pick_count_ = 0;
  pick_reread_ = false;
  ever_saved_ = false;
  saved_ms_ = 0;
  req_ = Req::None;
  req_name_[0] = '\0';
  flush_ = false;
  new_pending_ = false;
  load_damaged_ = false;
  load_result_ = project::Result{};
  done_notice_ = nullptr;
  apply_deadline_ = 0;
  del_req_ = false;
  del_name_[0] = '\0';
  deleted_n_ = 0;
  list_stale_ = true;
  list_valid_ = false;
  list_truncated_ = false;
  proj_n_ = 0;
  gen_ = 0;
  n_names_ = 0;
  next_check_ms_ = 0;
  image_bytes_ = 0;
  rig.project = ProjectInfo{};

  // What the session is as setup leaves it: a boot load applies only while this is still true.
  project::gather(rig, e, big_->probe);
  copy_name(big_->probe.name, name_);
  boot_sum_ = version_sum();
  boot_crc_ = project::small_crc(big_->probe);
  seen_sum_ = boot_sum_;
  seen_crc_ = boot_crc_;
}

void ProjectService::update(Context& ctx) noexcept {
  if (rig_ == nullptr) {
    return;
  }
  Files& f = ctx.files();
  if (phase_ == Phase::Start) {
    boot_start(ctx);
  }
  if (phase_ != Phase::Start && phase_ != Phase::BootLast && phase_ != Phase::BootLoad) {
    card_watch(ctx);
    observe(ctx);
  }

  if (op_ != Op::None && !f.busy()) {  // the job this service had in flight has ended
    const Op ended = op_;
    op_ = Op::None;
    const Files::Status st = f.status();
    switch (ended) {
      case Op::LoadLast:
      case Op::LoadProject: boot_settle(ctx, ended, st.phase == Files::Status::Done, st.text); break;
      case Op::List: choose_name(ctx); break;
      case Op::LoadTarget: load_settle(ctx, st.phase == Files::Status::Done, st.text); break;
      case Op::ListView: list_settle(ctx); break;
      case Op::Delete:
        f.set_quiet(false);
        if (st.phase == Files::Status::Done) {
          notice("DELETED");
          list_stale_ = true;
          if (deleted_n_ < kDeletedMax) {
            copy_name(deleted_[deleted_n_++], del_name_);
          }
        } else {
          notice(st.text[0] != '\0' ? st.text : "DELETE FAILED");
        }
        break;
      case Op::SaveProject:
      case Op::SaveLive:
      case Op::SaveLast: write_settle(ctx, ended); break;
      case Op::None: break;
    }
  }

  if (reset_pending_ && op_ == Op::None) {
    reset_step(ctx);  // before idle_update(): an autosave that is due does not start an encode over the reset's turn
  }
  if (op_ == Op::None) {
    switch (phase_) {
      case Phase::BootLast:
      case Phase::BootLoad: boot_issue(ctx); break;
      case Phase::Idle: idle_update(ctx); break;
      case Phase::Encoding: encode_step(ctx); break;
      case Phase::Writing: write_issue(ctx); break;
      case Phase::Loading: load_issue(ctx); break;
      case Phase::Applying: apply_request(ctx); break;
      case Phase::Start:
      case Phase::Listing: break;
    }
  }
  publish();
}

// ---- RESET PROJECT --------------------------------------------------------------------------------------------------------------------------------

constexpr std::uint64_t kResetStopMs = 500;  // how long RESET PROJECT asks the command ring to stop the transport

// The kit's config_run() has no way to say "refused" (it shows DONE after the second F1), so the reset is never refused: it waits until the service has no encode, load or
// request in progress and then stops the transport (reset_step), and the kit's DONE is true. A wait is bounded by what the service is doing (a boot, a load, a save end).
bool ProjectService::reset_session(Context& ctx) noexcept {
  (void)ctx;
  if (rig_ == nullptr || reset_pending_) {
    return false;
  }
  reset_pending_ = true;
  reset_deadline_ms_ = 0;  // set when the stop is first asked
  return true;
}

void ProjectService::reset_step(Context& ctx) noexcept {
  o3sdk::dsp::Engine& e = ctx.engine();
  // An encode in progress reads the bank slice by slice: a reset in that window would write a torn image (the old sound over half-cleared patterns). The write of an image
  // already built is no obstacle, and any other job or request goes first.
  if (!(phase_ == Phase::Idle || phase_ == Phase::Writing) || request_busy()) {
    return;
  }
  if (e.playing()) {
    if (reset_deadline_ms_ == 0u) {
      reset_deadline_ms_ = ctx.uptime_ms() + kResetStopMs;
    }
    if (ctx.uptime_ms() >= reset_deadline_ms_) {
      reset_pending_ = false;
      notice("COULD NOT STOP THE TRANSPORT");
    } else {
      (void)e.stop();  // asked again until the command ring takes it
    }
    return;
  }
  reset_pending_ = false;
  if (rig_->live != nullptr) {
    rig_->live->all_off(ctx);
  }
  big_->probe = project::ProjectState{};  // a new project's: the default sound, no snapshots, the default assignments, 125 BPM (change detection gathers into it again)
  (void)project::apply(*rig_, e, big_->probe, false);
  seq::Bank& bank = *rig_->bank;
  for (unsigned i = 1; i < kPatternCount; ++i) {  // pattern 0 comes below, as one edit; a pattern that is fresh already records nothing
    (void)bank.edit(i, [](Pattern& p) { clear(p); });
  }
  bank.select(0);
  seq::gen::Preset first;  // the factory line in A01, as a first boot and NEW have it
  seq::gen::factory(first, 0, 0);
  load_preset(e, *rig_, first, LoadWhat::Both);
  notice("PROJECT RESET");
}

void ProjectService::settings_moved(const o3sdk::dsp::Engine& e) noexcept {
  if (phase_ != Phase::Start || rig_ == nullptr || big_ == nullptr) {
    return;
  }
  project::gather(*rig_, e, big_->probe);
  copy_name(big_->probe.name, name_);
  boot_crc_ = project::small_crc(big_->probe);
  seen_crc_ = boot_crc_;
}

bool ProjectService::save_as(const char* name) noexcept {
  if (name == nullptr || !project_name_ok(name)) {
    return false;
  }
  if (const char* why = name_problem(name, true)) {  // another project's name, or one that cannot be told apart from it: never overwritten by surprise
    notice(why);
    return false;
  }
  copy_name(as_name_, name);
  as_valid_ = true;
  save_req_ = true;
  return true;
}

// ---- Boot ---------------------------------------------------------------------------------------------------------------------------------------

void ProjectService::boot_start(Context& ctx) noexcept {
  Files& f = ctx.files();
  gen_ = f.generation();
  if (card_of(f) != Files::Card::Ready) {
    boot_end(ctx, false);  // no card (or none to use): the factory line, no file call, autosave off (D11)
    return;
  }
  if (f.busy()) {
    return;  // the kit's own job; the decision waits a pass
  }
  phase_ = Phase::BootLast;
  step_ = 0;
}

void ProjectService::boot_issue(Context& ctx) noexcept {
  Files& f = ctx.files();
  if (card_of(f) != Files::Card::Ready) {
    boot_end(ctx, false);
    return;
  }
  if (f.busy()) {
    return;
  }
  f.set_quiet(true);
  if (phase_ == Phase::BootLast) {
    big_->last[0] = '\0';
    if (f.load("", kLastFile, big_->last, sizeof big_->last - 1u)) {
      op_ = Op::LoadLast;
    } else {
      boot_settle(ctx, Op::LoadLast, false, f.status().text);  // refused (no card, a name): the text says why
    }
    return;
  }
  ++boot_reads_;
  if (f.load(names_[step_ / kCands], cand_file(step_ % kCands), project::buffer(), project::kProjectBytes)) {
    op_ = Op::LoadProject;
  } else {
    boot_settle(ctx, Op::LoadProject, false, f.status().text);
  }
}

// A job of the boot has ended (or was refused, which is the same as failed): last.txt is read, or a project file is.
void ProjectService::boot_settle(Context& ctx, Op ended, bool done, const char* text) noexcept {
  Files& f = ctx.files();
  // Only "NOT FOUND" says a file is not there. Any other failure (an I/O error, the card changed, no card) says nothing about what the card holds, so the
  // session must not go on to autosave over it: no project on the card until SAVE.
  if (!done && std::strcmp(text, "NOT FOUND") != 0) {
    if (std::strcmp(text, "FILE TOO BIG") == 0) {
      notice("PROJECT TOO BIG");
    } else {
      notice("CARD READ FAILED: AUTOSAVE OFF");
    }
    blocked_ = true;
    boot_end(ctx, false);
    return;
  }
  if (ended == Op::LoadLast) {
    // last.txt: one line, the project's name. Anything that is not a name is as if there were none.
    last_name_[0] = '\0';
    if (done && f.loaded() > 0u) {
      std::size_t n = f.loaded() < sizeof big_->last - 1u ? f.loaded() : sizeof big_->last - 1u;
      big_->last[n] = '\0';
      for (std::size_t i = 0; i < n; ++i) {
        if (big_->last[i] == '\r' || big_->last[i] == '\n' || big_->last[i] == '\0') {
          big_->last[i] = '\0';
          n = i;
          break;
        }
      }
      while (n != 0u && big_->last[n - 1u] == ' ') {
        big_->last[--n] = '\0';
      }
      if (project_name_ok(big_->last)) {
        copy_name(last_name_, big_->last);
      }
    }
    n_names_ = 0;
    if (last_name_[0] != '\0') {
      copy_name(names_[n_names_++], last_name_);
    }
    if (n_names_ == 0u || !same_ci(names_[0], kDefaultName)) {
      copy_name(names_[n_names_++], kDefaultName);
    }
    phase_ = Phase::BootLoad;
    step_ = 0;
    pick_best_ = -1;
    pick_reread_ = false;
    return;
  }

  // The newest valid file, read again because it was not the last one read: that read must be good. If it is not (the card changed, an I/O error, the file is
  // gone or damaged since), the project is not loaded and nothing is written over it, as for any read that fails at boot.
  if (pick_reread_) {
    pick_reread_ = false;
    if (done) {
      const project::Result again = project::decode(project::buffer(), f.loaded(), big_->state);
      if (again.status == project::Status::Ok) {
        boot_apply(ctx, again);
        return;
      }
    }
    blocked_ = true;
    notice("CARD READ FAILED: AUTOSAVE OFF");
    boot_end(ctx, false);
    return;
  }

  // A project's files: project.o3a, then live.o3a, each read and checked; the newest valid one (the highest save counter, live.o3a on a tie) is the project. It is
  // read once more when it is not the last one read.
  const unsigned cand = step_ % kCands;
  bool ok_now = false;
  project::Result r;
  if (done) {
    r = project::decode(project::buffer(), f.loaded(), big_->state);
    if (r.status == project::Status::Ok) {
      ok_now = true;
      if (pick_best_ < 0 || big_->state.save_count >= pick_count_) {  // >=: the later file, live.o3a, wins a tie
        pick_best_ = static_cast<int>(cand);
        pick_count_ = big_->state.save_count;
      }
    } else if (r.status == project::Status::FromNewer) {
      blocked_ = true;
      notice("PROJECT FROM A NEWER O3-303");
      boot_end(ctx, false);
      return;
    } else {
      damaged_ = true;  // the container or its CRC is bad: the others are still tried
      notice("PROJECT DAMAGED");
    }
  }
  if (cand + 1u < kCands) {
    ++step_;  // the next file, asked at the next pass
    return;
  }
  if (pick_best_ >= 0) {
    if (pick_best_ == static_cast<int>(cand) && ok_now) {
      boot_apply(ctx, r);  // the last one read is the newest
    } else {
      step_ = (step_ / kCands) * kCands + static_cast<unsigned>(pick_best_);
      pick_reread_ = true;
    }
    return;
  }
  boot_next(ctx);
}

void ProjectService::boot_next(Context& ctx) noexcept {
  step_ = (step_ / kCands + 1u) * kCands;  // the next project's first file
  pick_best_ = -1;
  pick_reread_ = false;
  if (step_ < n_names_ * kCands) {
    return;  // asked at the next pass
  }
  // Nothing was there to load. A card whose files are all missing is a new project called UNTITLED that autosaves there (D25); one with a file that
  // must not be written over (damaged, too big) leaves the session without a project until SAVE.
  copy_name(name_, kDefaultName);
  last_dirty_ = true;
  boot_end(ctx, !blocked_ && !damaged_);
}

void ProjectService::boot_apply(Context& ctx, const project::Result& r) noexcept {
  o3sdk::dsp::Engine& e = ctx.engine();
  // The load is for a session the player has not touched, with the sequencer stopped: otherwise the player's work wins and the file stays as it is.
  if (e.playing() || edited(e)) {
    notice(e.playing() ? "AUTOSAVE OFF: PLAYING BEFORE THE LOAD" : "AUTOSAVE OFF: EDITED BEFORE THE LOAD");
    boot_end(ctx, false);
    return;
  }
  if (rig_->live != nullptr) {
    rig_->live->all_off(ctx);
  }
  if (!project::apply(*rig_, e, big_->state)) {
    notice("AUTOSAVE OFF: PROJECT NOT LOADED");
    boot_end(ctx, false);
    return;
  }
  copy_name(name_, names_[step_ / kCands]);
  seq_ = big_->state.save_count;
  last_dirty_ = !same_ci(name_, last_name_);
  if (r.skipped != 0u || !r.patterns_ok) {
    notice("PART OF THE PROJECT SKIPPED");
  }
  boot_end(ctx, true);
}

void ProjectService::boot_end(Context& ctx, bool project_on_card) noexcept {
  Files& f = ctx.files();
  f.set_quiet(false);
  op_ = Op::None;
  step_ = 0;
  phase_ = Phase::Idle;
  gen_ = f.generation();
  home_ = project_on_card && card_of(f) == Files::Card::Ready;
  if (!home_) {
    last_dirty_ = false;
  }
  rebaseline(ctx.engine(), ctx.uptime_ms());
}

// ---- Change detection ---------------------------------------------------------------------------------------------------------------------------

std::uint32_t ProjectService::version_sum() const noexcept {
  std::uint32_t sum = 0;
  if (rig_ != nullptr && rig_->bank != nullptr) {
    for (unsigned i = 0; i < kPatternCount; ++i) {
      sum += rig_->bank->version(i);
    }
  }
  return sum;
}

bool ProjectService::edited(const o3sdk::dsp::Engine& e) noexcept {
  if (version_sum() != boot_sum_) {
    return true;
  }
  project::gather(*rig_, e, big_->probe);
  copy_name(big_->probe.name, name_);
  return project::small_crc(big_->probe) != boot_crc_;
}

// The session is what it is now: nothing waits to be saved (a load applied, or the boot decided).
void ProjectService::rebaseline(const o3sdk::dsp::Engine& e, std::uint64_t now_ms) noexcept {
  project::gather(*rig_, e, big_->probe);
  copy_name(big_->probe.name, name_);
  seen_sum_ = version_sum();
  seen_crc_ = project::small_crc(big_->probe);
  next_check_ms_ = now_ms + kCheckMs;
  clock_.reset();
}

void ProjectService::observe(Context& ctx) noexcept {
  const std::uint64_t now = ctx.uptime_ms();
  bool changed = false;
  const std::uint32_t sum = version_sum();
  if (sum != seen_sum_) {
    seen_sum_ = sum;
    changed = true;
  }
  if (now >= next_check_ms_) {
    next_check_ms_ = now + kCheckMs;
    project::gather(*rig_, ctx.engine(), big_->probe);
    copy_name(big_->probe.name, name_);
    const std::uint32_t crc = project::small_crc(big_->probe);
    if (crc != seen_crc_) {
      seen_crc_ = crc;
      changed = true;
    }
  }
  if (changed) {
    clock_.changed(now);
  }
}

// ---- Saving -------------------------------------------------------------------------------------------------------------------------------------

// A card out, in or on a computer is a card that may not be the one the project is on (a swapped card can hold a project of the same name, which an autosave
// would replace): autosave goes off until SAVE or a load gives the session a project on this card. A save in the making is let go; one running ends by itself
// (the kit fails it with CARD CHANGED).
void ProjectService::card_watch(Context& ctx) noexcept {
  Files& f = ctx.files();
  if (f.generation() == gen_) {
    return;
  }
  gen_ = f.generation();
  deleted_n_ = 0;       // the folders hidden on another card are not this card's
  list_valid_ = false;  // a list of another card (or none)
  list_stale_ = true;
  proj_n_ = 0;
  if (op_ == Op::None && (phase_ == Phase::Encoding || phase_ == Phase::Writing)) {
    enc_.abandon();
    f.set_quiet(false);
    undo_rename();
    if (explicit_) {
      notice("SAVE FAILED: CARD CHANGED");  // the SAVE the player asked for did not happen, with a project on the card or not
    }
    explicit_ = false;
    phase_ = Phase::Idle;
    step_ = 0;
    if (new_pending_) {
      new_pending_ = false;
      notice("NEW PROJECT NOT SAVED");
    }
    if (flush_) {
      flush_ = false;  // the request ends below
    }
  }
  if (req_ != Req::None && op_ == Op::None) {  // (a job in flight fails by itself, with CARD CHANGED, and ends the request in its settle)
    abort_request(ctx, "CARD CHANGED");
  }
  if (home_) {
    home_ = false;
    clock_.reset();
    failed_ = false;
    notice("CARD CHANGED: SAVE TO KEEP THE PROJECT");
  }
}

void ProjectService::idle_update(Context& ctx) noexcept {
  Files& f = ctx.files();
  const std::uint64_t now = ctx.uptime_ms();
  if (save_req_) {  // a SAVE (or SAVE AS) asked before a LOAD, NEW or REVERT is served before it
    start_save(ctx);
    return;
  }
  if (req_ != Req::None) {
    request_begin(ctx);
    return;
  }
  if (del_req_) {
    delete_issue(ctx);
    return;
  }
  if (home_ && clock_.due(now)) {
    const Files::Card card = card_of(f);
    if (card != Files::Card::Ready) {  // no file call: it waits, and the player is told once
      show_failed(card_text(card));
      return;
    }
    if (!f.busy()) {
      begin_encode(ctx, name_, false);
    }
    return;
  }
  maybe_list(ctx);
}

void ProjectService::start_save(Context& ctx) noexcept {
  Files& f = ctx.files();
  const Files::Card card = card_of(f);
  if (card != Files::Card::Ready) {
    save_req_ = false;
    as_valid_ = false;
    notice(card_text(card));
    return;
  }
  if (f.busy()) {
    return;  // taken at a later pass
  }
  save_req_ = false;
  if (as_valid_) {
    as_valid_ = false;
    begin_encode(ctx, as_name_, true);
    return;
  }
  if (home_) {
    begin_encode(ctx, name_, true);
    return;
  }
  // A session with no project on the card saves as SAVE AS (D11): under the first free UNTITLED name.
  f.set_quiet(true);
  if (f.list("", big_->list, kListMax)) {
    op_ = Op::List;
    phase_ = Phase::Listing;
    explicit_ = true;  // this is a SAVE all the way (saving_explicit())
  } else {
    fail(ctx, f.status().text);
  }
}

void ProjectService::choose_name(Context& ctx) noexcept {
  Files& f = ctx.files();
  const Files::Status st = f.status();
  if (st.phase != Files::Status::Done) {
    fail(ctx, st.text);
    return;
  }
  if (f.list_truncated()) {
    // The list holds kListMax (48) entries, folders first, the files after them; one more and a free-looking name might be taken beyond what was read, so no
    // name is guessed. (Folders hidden by DELETE are not in the list at all: the kit refuses a save into one, and pick_name goes on to the next name.)
    fail(ctx, "TOO MANY PROJECTS");
    return;
  }
  list_n_ = static_cast<unsigned>(f.listed() < kListMax ? f.listed() : kListMax);
  pick_name(ctx, 1);
}

// The first free name from candidate `first` on (1 is "UNTITLED", 2 "UNTITLED 2", up to 99) and the save under it.
void ProjectService::pick_name(Context& ctx, unsigned first) noexcept {
  char cand[project::kNameChars];
  for (unsigned i = first; i <= 99u; ++i) {
    copy_name(cand, kDefaultName);
    if (i > 1u) {  // "UNTITLED 2" .. "UNTITLED 99"
      unsigned at = static_cast<unsigned>(std::strlen(cand));
      cand[at++] = ' ';
      if (i >= 10u) {
        cand[at++] = static_cast<char>('0' + i / 10u);
      }
      cand[at++] = static_cast<char>('0' + i % 10u);
      cand[at] = '\0';
    }
    bool taken = false;
    for (unsigned k = 0; k < list_n_ && !taken; ++k) {
      taken = same_ci(big_->list[k].name, cand);  // a folder or a file of that name: either way the name is not free
    }
    if (!taken) {
      cand_ = i;
      begin_encode(ctx, cand, true);
      auto_name_ = phase_ == Phase::Encoding;  // (an encode that failed outright has ended the save)
      return;
    }
  }
  fail(ctx, "TOO MANY PROJECTS");
}

void ProjectService::begin_encode(Context& ctx, const char* target, bool explicit_save) noexcept {
  auto_name_ = false;
  renamed_ = !home_ || !same_ci(target, name_);
  copy_name(prev_name_, name_);
  copy_name(name_, target);
  explicit_ = explicit_save;
  project::gather(*rig_, ctx.engine(), big_->state);
  copy_name(big_->state.name, name_);
  big_->state.save_count = ++seq_;  // HEAD's: boot and LOAD take the newest file by it
  // The state this image holds is the one change detection compares with from here on; what changes after it is a new unsaved change.
  seen_sum_ = version_sum();
  seen_crc_ = project::small_crc(big_->state);
  next_check_ms_ = ctx.uptime_ms() + kCheckMs;
  clock_.started();
  enc_.begin(big_->state, *rig_->bank, project::buffer(), project::kProjectBytes);
  phase_ = Phase::Encoding;
  if (enc_.failed()) {
    fail(ctx, "IMAGE TOO BIG");
  }
}

void ProjectService::encode_step(Context& ctx) noexcept {
  Files& f = ctx.files();
  const Files::Card card = card_of(f);
  if (card != Files::Card::Ready) {  // the card went away between two steps: nothing was asked of it; the save starts over when it is back
    enc_.abandon();
    f.set_quiet(false);
    phase_ = Phase::Idle;
    undo_rename();
    if (home_) {
      clock_.changed(ctx.uptime_ms());
    }
    if (new_pending_) {
      new_pending_ = false;
      notice("NEW PROJECT NOT SAVED");
    }
    if (flush_) {
      flush_ = false;
      req_ = Req::None;
      notice("SAVE FAILED: REQUEST DROPPED");
    }
    if (explicit_) {
      notice(card_text(card));
    } else {
      show_failed(card_text(card));
    }
    return;
  }
  (void)enc_.step(kPatternsPerPass, kCrcBytesPerPass);
  if (enc_.failed()) {
    fail(ctx, "IMAGE TOO BIG");
  } else if (enc_.done()) {
    image_bytes_ = enc_.size();
    phase_ = Phase::Writing;
    step_ = 0;
    write_issue(ctx);
  }
}

void ProjectService::write_issue(Context& ctx) noexcept {
  Files& f = ctx.files();
  const Files::Card card = card_of(f);
  if (card != Files::Card::Ready) {
    fail(ctx, card_text(card));
    return;
  }
  if (f.busy()) {
    return;  // the kit's settings are being written: the image waits in the buffer
  }
  bool ok = false;
  switch (step_) {
    case 0:  // live.o3a first: boot loads it first, so a card pulled before project.o3a is written still holds the newest image there
      f.set_quiet(true);
      ok = f.save(name_, kLiveFile, project::buffer(), image_bytes_);
      op_ = Op::SaveLive;
      break;
    case 1:  // the strip says SAVED once, for this file
      f.set_quiet(false);
      ok = f.save(name_, kProjectFile, project::buffer(), image_bytes_);
      op_ = Op::SaveProject;
      break;
    default: {
      std::size_t n = 0;  // "<name>\n": the name is at most 15 characters, the buffer 32
      for (; n + 1u < sizeof big_->last && name_[n] != '\0'; ++n) {
        big_->last[n] = name_[n];
      }
      big_->last[n++] = '\n';
      f.set_quiet(true);
      ok = f.save("", kLastFile, big_->last, n);
      op_ = Op::SaveLast;
      break;
    }
  }
  if (!ok) {
    op_ = Op::None;
    fail(ctx, f.status().text);
  }
}

void ProjectService::write_settle(Context& ctx, Op ended) noexcept {
  Files& f = ctx.files();
  const Files::Status st = f.status();
  if (st.phase != Files::Status::Done) {
    if (auto_name_ && renamed_ && !home_ && std::strcmp(st.text, "FOLDER DELETED") == 0 && cand_ < 99u) {
      undo_rename();  // the kit hides a deleted project's folder from the list but refuses a save into it: the next free name
      pick_name(ctx, cand_ + 1u);
      return;
    }
    if (home_ && !renamed_ && std::strcmp(st.text, "FOLDER DELETED") == 0) {
      // The session's own project folder was deleted (a new UNTITLED on a card that hides one): autosave cannot go on there.
      home_ = false;
      clock_.reset();
      notice("PROJECT FOLDER DELETED: SAVE TO KEEP");
    }
    const char* why = st.text[0] != '\0' ? st.text : "FAILED";
    if (renamed_ && std::strcmp(why, "FOLDER DELETED") == 0) {  // a SAVE AS or NEW under a name the player deleted before: the kit hides it for good
      why = "NAME USED BEFORE: PICK ANOTHER";
    }
    fail(ctx, why);
    return;
  }
  switch (ended) {
    case Op::SaveLive:
      ++saves_;
      ever_saved_ = true;
      saved_ms_ = ctx.uptime_ms();
      home_ = true;
      if (renamed_) {
        list_stale_ = true;  // a folder was made
      }
      renamed_ = false;  // the session is this project now
      auto_name_ = false;
      if (!same_ci(name_, last_name_)) {
        last_dirty_ = true;
      }
      step_ = explicit_ ? 1u : (last_dirty_ ? 2u : kSaveSteps);
      break;
    case Op::SaveProject:
      step_ = last_dirty_ ? 2u : kSaveSteps;
      break;
    default:
      copy_name(last_name_, name_);
      last_dirty_ = false;
      step_ = kSaveSteps;
      break;
  }
  if (step_ >= kSaveSteps) {
    save_ended(ctx);
  }
}

void ProjectService::save_ended(Context& ctx) noexcept {
  ctx.files().set_quiet(false);
  phase_ = Phase::Idle;
  step_ = 0;
  failed_ = false;
  copy_name(prev_name_, name_);
  new_pending_ = false;
  if (done_notice_ != nullptr) {
    notice(done_notice_);
    done_notice_ = nullptr;
  } else if (explicit_) {
    notice("SAVED");
  }
  explicit_ = false;
  if (flush_) {  // the current project is on the card: the LOAD or NEW goes on
    flush_ = false;
    if (req_ == Req::New && same_ci(req_name_, name_)) {  // a session with no project was saved under the first free UNTITLED n: the name typed is that one now
      abort_request(ctx, "NAME TAKEN");
      return;
    }
    request_after_flush(ctx);
  }
}

// A SAVE AS that did not take leaves the session under the name it had.
void ProjectService::undo_rename() noexcept {
  if (renamed_) {
    copy_name(name_, prev_name_);
    renamed_ = false;
  }
}

void ProjectService::fail(Context& ctx, const char* text) noexcept {
  Files& f = ctx.files();
  enc_.abandon();
  op_ = Op::None;
  step_ = 0;
  phase_ = Phase::Idle;
  f.set_quiet(false);
  undo_rename();
  auto_name_ = false;
  if (explicit_) {
    notice(text);
  }
  explicit_ = false;
  done_notice_ = nullptr;
  if (flush_) {  // the flush a LOAD or NEW waited for failed: nothing was loaded, the player's work is as it was
    flush_ = false;
    notice(req_ == Req::New ? "SAVE FAILED: NO NEW PROJECT" : "SAVE FAILED: NOT LOADED");
    req_ = Req::None;
  }
  if (new_pending_) {  // the new project could not be written: the session is it, with no project on the card
    new_pending_ = false;
    notice(std::strcmp(text, "NAME USED BEFORE: PICK ANOTHER") == 0 ? text : "NEW PROJECT NOT SAVED: SAVE TO KEEP");
  }
  if (std::strcmp(text, "CARD CHANGED") != 0) {  // (card_watch has told the player: autosave is off, not failing)
    show_failed(text);
  }
}

// ---- The FILE screen's requests -----------------------------------------------------------------------------------------------------------------

bool ProjectService::valid_name(const char* name) noexcept { return name != nullptr && project_name_ok(name); }

bool ProjectService::is_current(const char* name) const noexcept { return home_ && name != nullptr && same_ci(name, name_); }

const char* ProjectService::project(unsigned i) const noexcept { return list_valid_ && i < proj_n_ && big_ != nullptr ? big_->projects[i].name : ""; }

bool ProjectService::name_taken(const char* name) const noexcept {
  if (!list_valid_ || name == nullptr || big_ == nullptr) {
    return false;
  }
  for (unsigned i = 0; i < proj_n_; ++i) {
    if (same_ci(big_->projects[i].name, name)) {
      return true;
    }
  }
  return false;
}

// Why `name` must not be written as a new project (nullptr: it may). Only known while the screen keeps the list (list_valid()): the screen asks for it. The list
// holds the first kListMax folders: a name that is not in a list that was cut may still be a project, so it is refused too. A project this session deleted is
// hidden by the kit and refused for a save (FOLDER DELETED): said before anything is wiped.
const char* ProjectService::name_problem(const char* name, bool allow_current) const noexcept {
  for (unsigned i = 0; i < deleted_n_; ++i) {
    if (same_ci(deleted_[i], name)) {
      return "NAME USED BEFORE: PICK ANOTHER";
    }
  }
  if (!list_valid_) {
    return nullptr;
  }
  if (is_current(name)) {
    return allow_current ? nullptr : "NAME TAKEN";
  }
  if (name_taken(name)) {
    return "NAME TAKEN";
  }
  return list_truncated_ ? "TOO MANY PROJECTS" : nullptr;
}

// A request is taken only when the service is booted and no other one is on; a refusal says why.
bool ProjectService::request_ok(const char* name) noexcept {
  if (rig_ == nullptr || phase_ == Phase::Start || phase_ == Phase::BootLast || phase_ == Phase::BootLoad || req_ != Req::None || del_req_ ||
      op_ == Op::Delete || new_pending_ || save_req_) {
    notice("BUSY");
    return false;
  }
  if (name != nullptr && !project_name_ok(name)) {
    notice("NOT A PROJECT NAME");
    return false;
  }
  return true;
}

bool ProjectService::load(const char* name) noexcept {
  if (!request_ok(name) || name == nullptr) {
    return false;
  }
  copy_name(req_name_, name);
  req_ = Req::Load;
  return true;
}

bool ProjectService::new_project(const char* name) noexcept {
  if (!request_ok(name) || name == nullptr) {
    return false;
  }
  if (const char* why = name_problem(name, false)) {  // never over another project (or the current one): the screen asks for another name
    notice(why);
    return false;
  }
  copy_name(req_name_, name);
  req_ = Req::New;
  return true;
}

bool ProjectService::revert() noexcept {
  if (!request_ok(nullptr)) {
    return false;
  }
  if (!home_) {  // a session with no project on the card has nothing saved
    notice("NOTHING SAVED YET");
    return false;
  }
  copy_name(req_name_, name_);
  req_ = Req::Revert;
  return true;
}

bool ProjectService::remove(const char* name) noexcept {
  if (!request_ok(name) || name == nullptr) {
    return false;
  }
  if (is_current(name)) {  // D13
    notice("LOAD ANOTHER FIRST");
    return false;
  }
  copy_name(del_name_, name);
  del_req_ = true;
  return true;
}

// The request ends here, whatever way: the session is as it is, the text tells the player. (Never called for a save in flight: fail() ends that one.)
void ProjectService::abort_request(Context& ctx, const char* text) noexcept {
  req_ = Req::None;
  flush_ = false;
  load_damaged_ = false;
  done_notice_ = nullptr;
  if (phase_ == Phase::Loading || phase_ == Phase::Applying) {
    phase_ = Phase::Idle;
    step_ = 0;
    ctx.files().set_quiet(false);
  }
  notice(text);
}

// The first step of LOAD, NEW and REVERT, from Idle: the card must be there and free, the transport is stopped, and the current project goes to the card first
// if anything is unsaved.
void ProjectService::request_begin(Context& ctx) noexcept {
  Files& f = ctx.files();
  const Files::Card card = card_of(f);
  if (card != Files::Card::Ready) {
    abort_request(ctx, card_text(card));
    return;
  }
  if (f.busy()) {
    return;
  }
  o3sdk::dsp::Engine& e = ctx.engine();
  if (e.playing()) {
    (void)e.stop();  // D12 (the wait for it to take is in apply_request())
  }
  if (req_ == Req::Revert) {
    phase_ = Phase::Loading;
    step_ = 0;  // project.o3a only
    pick_last_ = 0;
    pick_best_ = -1;
    pick_reread_ = false;
    load_damaged_ = false;
    return;
  }
  // Is anything unsaved? A knob moved less than a second ago is not seen yet: look now. A failed autosave counts too (the changes never reached the card).
  next_check_ms_ = 0;
  observe(ctx);
  if (!clock_.pending() && !failed_) {
    request_after_flush(ctx);
    return;
  }
  flush_ = true;
  if (home_) {
    begin_encode(ctx, name_, false);
  } else {
    start_save(ctx);  // no project on the card: the work goes to the first free UNTITLED name (D11), not into the void
  }
}

void ProjectService::request_after_flush(Context& ctx) noexcept {
  load_damaged_ = false;
  apply_deadline_ = ctx.uptime_ms() + kStopWaitMs;
  if (req_ == Req::Load) {
    phase_ = Phase::Loading;
    step_ = 0;
    pick_last_ = kCands - 1u;
    pick_best_ = -1;
    pick_reread_ = false;
  } else {
    phase_ = Phase::Applying;
  }
}

// Reads the next file of the project asked for: live.o3a (step 0, LOAD only), then project.o3a (step 1).
void ProjectService::load_issue(Context& ctx) noexcept {
  Files& f = ctx.files();
  const Files::Card card = card_of(f);
  if (card != Files::Card::Ready) {
    abort_request(ctx, card_text(card));
    return;
  }
  if (f.busy()) {
    return;
  }
  f.set_quiet(true);
  if (f.load(req_name_, cand_file(step_), project::buffer(), project::kProjectBytes)) {
    op_ = Op::LoadTarget;
  } else {
    load_settle(ctx, false, f.status().text);  // refused (a name): the text says why
  }
}

// A file of the project asked for is read (LOAD: live.o3a and project.o3a, the newest valid one wins as at boot; REVERT: project.o3a).
void ProjectService::load_settle(Context& ctx, bool done, const char* text) noexcept {
  Files& f = ctx.files();
  bool ok_now = false;
  project::Result r;
  if (!done) {
    if (std::strcmp(text, "NOT FOUND") != 0 && std::strcmp(text, "NO SUCH FOLDER") != 0) {
      abort_request(ctx, std::strcmp(text, "FILE TOO BIG") == 0 ? "PROJECT TOO BIG" : (text[0] != '\0' ? text : "LOAD FAILED"));
      return;
    }
  } else {
    r = project::decode(project::buffer(), f.loaded(), big_->state);
    if (r.status == project::Status::Ok) {
      ok_now = true;
      if (pick_reread_ || pick_best_ < 0 || big_->state.save_count >= pick_count_) {  // >=: live.o3a, read after project.o3a, wins a tie
        pick_best_ = static_cast<int>(step_);
        pick_count_ = big_->state.save_count;
      }
    } else if (r.status == project::Status::FromNewer) {
      abort_request(ctx, "PROJECT FROM A NEWER O3-303");
      return;
    } else {
      load_damaged_ = true;  // the container or its CRC is bad: the others are still tried
    }
  }
  const auto go = [&] {
    load_result_ = r;
    phase_ = Phase::Applying;
    apply_deadline_ = ctx.uptime_ms() + kStopWaitMs;
    f.set_quiet(false);
  };
  if (pick_reread_) {  // the newest, read again: it is what is applied
    pick_reread_ = false;
    if (ok_now) {
      go();
    } else {
      abort_request(ctx, "PROJECT DAMAGED");
    }
    return;
  }
  if (step_ < pick_last_) {
    ++step_;
    return;
  }
  if (pick_best_ < 0) {
    abort_request(ctx, load_damaged_ ? "PROJECT DAMAGED" : (req_ == Req::Revert ? "NOTHING SAVED YET" : "PROJECT HAS NO FILES"));
  } else if (pick_best_ == static_cast<int>(step_) && ok_now) {
    go();
  } else {
    step_ = static_cast<unsigned>(pick_best_);
    pick_reread_ = true;
  }
}

// The transport has stopped (or the wait is over): the project goes on the session.
void ProjectService::apply_request(Context& ctx) noexcept {
  Files& f = ctx.files();
  const Files::Card card = card_of(f);
  if (card != Files::Card::Ready) {
    abort_request(ctx, card_text(card));
    return;
  }
  o3sdk::dsp::Engine& e = ctx.engine();
  if (e.playing()) {
    if (ctx.uptime_ms() >= apply_deadline_) {
      abort_request(ctx, "COULD NOT STOP THE TRANSPORT");
    } else {
      (void)e.stop();  // asked again until the command ring takes it
    }
    return;
  }
  if (rig_->live != nullptr) {
    rig_->live->all_off(ctx);
  }
  const Req what = req_;
  if (what == Req::New) {
    big_->state = project::ProjectState{};
    copy_name(big_->state.name, req_name_);
  }
  if (!project::apply(*rig_, e, big_->state)) {
    abort_request(ctx, "PROJECT NOT LOADED");
    return;
  }
  req_ = Req::None;
  phase_ = Phase::Idle;
  step_ = 0;
  f.set_quiet(false);
  clock_.reset();
  if (what == Req::New) {  // the factory line in A01, as a first boot has it
    seq::gen::Preset first;
    seq::gen::factory(first, 0, 0);
    load_preset(e, *rig_, first, LoadWhat::Both);
    rig_->bank->clear_history();
    copy_name(name_, req_name_);
    copy_name(prev_name_, req_name_);
    seq_ = 0;  // a new project counts its own writes
    home_ = false;  // the folder does not exist yet: its first write makes the session this project
    rebaseline(e, ctx.uptime_ms());
    new_pending_ = true;
    done_notice_ = "NEW PROJECT";
    begin_encode(ctx, name_, false);
    return;
  }
  const bool whole = load_result_.patterns_ok;
  seq_ = what == Req::Load ? big_->state.save_count : (seq_ > big_->state.save_count ? seq_ : big_->state.save_count);  // monotone: a REVERT goes on counting
  if (what == Req::Load) {
    copy_name(name_, req_name_);
    copy_name(prev_name_, req_name_);
    home_ = true;
    last_dirty_ = !same_ci(name_, last_name_);
    done_notice_ = load_damaged_ ? "LOADED THE SAVED COPY" : (load_result_.skipped != 0u || !whole ? "PART OF THE PROJECT SKIPPED" : "LOADED");
    rebaseline(e, ctx.uptime_ms());
    load_damaged_ = false;
    if (last_dirty_) {  // last.txt names this project from now on
      phase_ = Phase::Writing;
      step_ = 2;
      explicit_ = false;
    } else {
      notice(done_notice_);
      done_notice_ = nullptr;
    }
    return;
  }
  // REVERT: the session is project.o3a. live.o3a must say so too, or a reboot brings the discarded work back; a project whose patterns were skipped is not
  // written over (the player acts first, as at boot).
  rebaseline(e, ctx.uptime_ms());
  load_damaged_ = false;
  if (whole && load_result_.skipped == 0u) {
    done_notice_ = "REVERTED";
    begin_encode(ctx, name_, false);
  } else {
    notice("PART OF THE PROJECT SKIPPED");
  }
}

void ProjectService::delete_issue(Context& ctx) noexcept {
  Files& f = ctx.files();
  const Files::Card card = card_of(f);
  if (card != Files::Card::Ready) {
    del_req_ = false;
    notice(card_text(card));
    return;
  }
  if (f.busy()) {
    return;
  }
  del_req_ = false;
  f.set_quiet(true);
  if (f.remove_folder(del_name_)) {
    op_ = Op::Delete;
  } else {
    f.set_quiet(false);
    notice(f.status().text[0] != '\0' ? f.status().text : "DELETE FAILED");
  }
}

// The project list for the screen that wants it: one listing whenever it is stale, a listing works on a write protected card too.
void ProjectService::maybe_list(Context& ctx) noexcept {
  if (!list_wanted_ || !list_stale_) {
    return;
  }
  Files& f = ctx.files();
  const Files::Card card = card_of(f);
  if (card != Files::Card::Ready && card != Files::Card::ReadOnly) {
    list_valid_ = false;
    return;  // no file call without a card; it is looked at again every pass (cheap), and a card put in moves the generation
  }
  if (f.busy()) {
    return;
  }
  f.set_quiet(true);
  if (f.list("", big_->projects, kListMax)) {
    op_ = Op::ListView;
    list_stale_ = false;
    list_valid_ = false;
  } else {
    f.set_quiet(false);
    list_stale_ = false;  // refused: not asked again until something changes
    list_valid_ = false;
  }
}

void ProjectService::list_settle(Context& ctx) noexcept {
  Files& f = ctx.files();
  f.set_quiet(false);
  if (f.status().phase != Files::Status::Done) {
    list_valid_ = false;
    proj_n_ = 0;
    return;
  }
  unsigned n = 0;
  const unsigned got = static_cast<unsigned>(f.listed() < kListMax ? f.listed() : kListMax);
  for (unsigned i = 0; i < got; ++i) {
    const Files::Entry& en = big_->projects[i];
    if (en.folder && project_name_ok(en.name)) {  // the card's Samples, files and folders that are not a project's name stay out
      if (n != i) {
        big_->projects[n] = en;
      }
      ++n;
    }
  }
  proj_n_ = n;
  list_truncated_ = f.list_truncated();
  list_valid_ = true;
  ++list_version_;
}

// ---- Reporting ----------------------------------------------------------------------------------------------------------------------------------

void ProjectService::notice(const char* text) noexcept {
  rig_->project.notice = text;
  ++rig_->project.notice_seq;
}

// SAVE FAILED, once: the flag stays up until a save works; the reason is kept for the screens.
void ProjectService::show_failed(const char* text) noexcept {
  failed_ = true;
  copy_text(rig_->project.fail, sizeof rig_->project.fail, text);
}

void ProjectService::publish() noexcept {
  ProjectInfo& p = rig_->project;
  copy_name(p.name, name_);
  p.autosave = home_;
  p.saving = phase_ == Phase::Listing || phase_ == Phase::Encoding || phase_ == Phase::Writing || save_req_;
  p.failed = failed_;
  if (!failed_) {
    p.fail[0] = '\0';
  }
}

}  // namespace o3_303
