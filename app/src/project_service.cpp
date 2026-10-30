// SPDX-License-Identifier: GPL-3.0-or-later
#include "project_service.h"

#include "live_actions.h"

#include <o3sdk/core/placement.h>
#include <o3sdk/hal/save_guard.h>

#include <cstring>
#include <new>

namespace o3_303 {

namespace {

namespace guard = o3sdk::hal::storage::save;
using o3sdk::app::Context;
using o3sdk::app::Files;

constexpr char kLiveFile[] = "live.o3a";
constexpr char kProjectFile[] = "project.o3a";
constexpr char kLastFile[] = "last.txt";
constexpr unsigned kSaveSteps = 3;  // Writing: live.o3a, project.o3a, last.txt

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
  explicit_ = false;
  renamed_ = false;
  auto_name_ = false;
  failed_ = false;
  blocked_ = false;
  damaged_ = false;
  saves_ = 0;
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
      case Op::SaveProject:
      case Op::SaveLive:
      case Op::SaveLast: write_settle(ctx, ended); break;
      case Op::None: break;
    }
  }

  if (op_ == Op::None) {
    switch (phase_) {
      case Phase::BootLast:
      case Phase::BootLoad: boot_issue(ctx); break;
      case Phase::Idle: idle_update(ctx); break;
      case Phase::Encoding: encode_step(ctx); break;
      case Phase::Writing: write_issue(ctx); break;
      case Phase::Start:
      case Phase::Listing: break;
    }
  }
  publish();
}

bool ProjectService::save_as(const char* name) noexcept {
  if (name == nullptr || !project_name_ok(name)) {
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
  const char* file = (step_ & 1u) != 0u ? kProjectFile : kLiveFile;
  if (f.load(names_[step_ / 2u], file, project::buffer(), project::kProjectBytes)) {
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
    return;
  }

  if (done) {
    const project::Result r = project::decode(project::buffer(), f.loaded(), big_->state);
    if (r.status == project::Status::Ok) {
      boot_apply(ctx, r);
      return;
    }
    if (r.status == project::Status::FromNewer) {
      blocked_ = true;
      notice("PROJECT FROM A NEWER O3-303");
      boot_end(ctx, false);
      return;
    }
    damaged_ = true;  // the container or its CRC is bad: the next file is tried (project.o3a after live.o3a)
    notice("PROJECT DAMAGED");
  }
  boot_next(ctx);
}

void ProjectService::boot_next(Context& ctx) noexcept {
  ++step_;
  if (step_ < n_names_ * 2u) {
    return;  // the next file, asked at the next pass
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
  copy_name(name_, names_[step_ / 2u]);
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
  if (op_ == Op::None && (phase_ == Phase::Encoding || phase_ == Phase::Writing)) {
    enc_.abandon();
    f.set_quiet(false);
    undo_rename();
    explicit_ = false;
    phase_ = Phase::Idle;
    step_ = 0;
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
  if (save_req_) {
    start_save(ctx);
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
  }
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
    fail(ctx, st.text[0] != '\0' ? st.text : "FAILED");
    return;
  }
  switch (ended) {
    case Op::SaveLive:
      ++saves_;
      home_ = true;
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
  if (explicit_) {
    notice("SAVED");
  }
  explicit_ = false;
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
  if (std::strcmp(text, "CARD CHANGED") != 0) {  // (card_watch has told the player: autosave is off, not failing)
    show_failed(text);
  }
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
  p.saving = phase_ == Phase::Listing || phase_ == Phase::Encoding || phase_ == Phase::Writing;
  p.failed = failed_;
  if (!failed_) {
    p.fail[0] = '\0';
  }
}

}  // namespace o3_303
