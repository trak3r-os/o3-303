// SPDX-License-Identifier: GPL-3.0-or-later
#include "file_screen.h"

#include "frame.h"

#include <o3sdk/app/context.h>
#include <o3sdk/app/pad_leds.h>
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

constexpr std::uint64_t kConfirmMs = 5000;  // REVERT and DELETE: the second press within this
constexpr std::uint64_t kToastMs = 2500;
constexpr int kInfoH = 34;
constexpr int kGap = 8;

enum Key : unsigned { kLoad, kSave, kSaveAs, kNew, kRevert, kDelete, kSpare, kLibrary };

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

void append_uint(char* out, unsigned cap, std::uint64_t v) noexcept {
  char tmp[24];
  unsigned n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + v % 10u);
    v /= 10u;
  } while (v != 0u && n < sizeof tmp);
  char num[24];
  unsigned len = 0;
  while (n != 0u) {
    num[len++] = tmp[--n];
  }
  num[len] = '\0';
  append(out, cap, num);
}

void copy_text(char* dst, unsigned cap, const char* src) noexcept {
  unsigned i = 0;
  for (; i + 1u < cap && src[i] != '\0'; ++i) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
}

// The notices that say something happened; every other is a refusal or a failure (drawn as an outline).
bool good_notice(const char* t) noexcept {
  static constexpr const char* kGood[] = {"SAVED", "LOADED", "LOADED THE SAVED COPY", "REVERTED", "NEW PROJECT", "DELETED"};
  for (const char* g : kGood) {
    if (std::strcmp(t, g) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

// ---- The view logic -----------------------------------------------------------------------------------------------------------------------------

unsigned ago_text(std::uint64_t ms, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  const std::uint64_t s = ms / 1000u;
  if (s == 0u) {
    append(out, cap, "JUST NOW");
  } else if (s < 60u) {
    append_uint(out, cap, s);
    append(out, cap, " S AGO");
  } else if (s < 3600u) {
    append_uint(out, cap, s / 60u);
    append(out, cap, " MIN AGO");
  } else {
    append_uint(out, cap, s / 3600u);
    append(out, cap, " H AGO");
  }
  return static_cast<unsigned>(std::strlen(out));
}

unsigned saved_text(const SaveState& s, char* out, unsigned cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  if (!s.card) {
    return 0;
  }
  if (s.saving) {
    append(out, cap, "SAVING");
  } else if (s.failed) {
    append(out, cap, "NOT SAVED");
  } else if (!s.autosave) {
    append(out, cap, "NOT ON THE CARD YET");
  } else if (s.ever) {
    append(out, cap, "AUTOSAVED ");
    char ago[16];
    (void)ago_text(s.ago_ms, ago, sizeof ago);
    append(out, cap, ago);
  } else {
    append(out, cap, "NOTHING WRITTEN YET");
  }
  return static_cast<unsigned>(std::strlen(out));
}

// The list's items.
class FileScreen::Source final : public ui::ListSource {
 public:
  explicit Source(const ProjectService& s) noexcept : s_(s) {}
  unsigned count() const noexcept override { return s_.projects(); }
  void name(unsigned index, char* out, unsigned cap) const noexcept override { copy_text(out, cap, s_.project(index)); }
  void value(unsigned index, char* out, unsigned cap) const noexcept override { copy_text(out, cap, s_.is_current(s_.project(index)) ? "*" : ""); }

 private:
  const ProjectService& s_;
};

// ---- Setup and screen hooks ---------------------------------------------------------------------------------------------------------------------

void FileScreen::init(Rig& rig, ProjectService& service, app::Screen& library) noexcept {
  rig_ = &rig;
  svc_ = &service;
  library_ = &library;
  list_ = ui::ListView(12);
  names_.close();
  entry_ = Entry::None;
  sel_[0] = '\0';
  list_version_ = 0;
  revert_at_ = 0;
  delete_at_ = 0;
  delete_name_[0] = '\0';
  seen_notice_ = rig.project.notice_seq;
  toast_[0] = '\0';
  toast_off_ = false;
  toast_until_ms_ = 0;
  shown_secs_ = 0;
  view_ = FileView{};
}

bool FileScreen::card_ready(app::Context& ctx) const noexcept {
  const app::Files& f = ctx.files();
  return f.available() && f.card() == app::Files::Card::Ready;
}

const char* FileScreen::selected() const noexcept { return svc_->projects() != 0u ? svc_->project(list_.cursor()) : ""; }

void FileScreen::enter(app::Context& ctx) noexcept {
  svc_->want_list(true);
  svc_->refresh_list();  // what is on the card now, not what was there last time
  names_.close();
  entry_ = Entry::None;
  disarm();
  seen_notice_ = rig_->project.notice_seq;
  toast_[0] = '\0';
  ctx.redraw();
}

void FileScreen::leave(app::Context& ctx) noexcept {
  (void)ctx;
  svc_->want_list(false);
  names_.close();
  entry_ = Entry::None;
  disarm();
}

void FileScreen::say(app::Context& ctx, const char* text, bool off) noexcept {
  copy_text(toast_, sizeof toast_, text);
  toast_off_ = off;
  toast_until_ms_ = ctx.uptime_ms() + kToastMs;
  ctx.redraw();
}

void FileScreen::disarm() noexcept {
  revert_at_ = 0;
  delete_at_ = 0;
  delete_name_[0] = '\0';
}

// The list was read again (or the first time): the cursor stays on the project it was on, else on the current one, else it is clamped.
bool FileScreen::follow_list() noexcept {
  if (list_version_ == svc_->list_version()) {
    return false;
  }
  list_version_ = svc_->list_version();
  const Source src(*svc_);
  const char* want = sel_[0] != '\0' ? sel_ : svc_->name();
  for (unsigned i = 0; i < src.count(); ++i) {
    if (std::strcmp(svc_->project(i), want) == 0) {
      (void)list_.select(src, i);
      list_.fit(src);
      return true;
    }
  }
  list_.fit(src);
  copy_text(sel_, sizeof sel_, selected());
  return true;
}

void FileScreen::update(app::Context& ctx) noexcept {
  const std::uint64_t now = ctx.uptime_ms();
  if (toast_[0] != '\0' && now >= toast_until_ms_) {
    toast_[0] = '\0';
    ctx.redraw();
  }
  if ((revert_at_ != 0u && now + 1u - revert_at_ >= kConfirmMs) || (delete_at_ != 0u && now + 1u - delete_at_ >= kConfirmMs)) {
    disarm();
    ctx.redraw();
  }
  if (rig_->project.notice_seq != seen_notice_) {  // what the service says (SAVED, LOADED, NO CARD, ...)
    seen_notice_ = rig_->project.notice_seq;
    say(ctx, rig_->project.notice, !good_notice(rig_->project.notice));
  }
  if (follow_list()) {
    ctx.redraw();
  }
  // "autosaved N s ago" changes by the second
  if (svc_->ever_saved()) {
    const std::uint64_t secs = (now - svc_->saved_ms()) / 1000u;
    if (secs != shown_secs_) {
      shown_secs_ = secs;
      ctx.redraw();
    }
  }
}

// ---- Input --------------------------------------------------------------------------------------------------------------------------------------

void FileScreen::input(app::Context& ctx, const Input& in) noexcept {
  if (names_.active()) {
    keyboard_input(ctx, in);
    return;
  }
  const Source src(*svc_);
  if (in.pad_press()) {
    if (list_.pad(src, in.pad())) {
      disarm();
      copy_text(sel_, sizeof sel_, selected());
    }
    ctx.redraw();
    return;
  }
  if (in.turn()) {
    if (in.delta() != 0 && list_.move(src, in.delta())) {
      disarm();
      copy_text(sel_, sizeof sel_, selected());
    }
    ctx.redraw();
    return;
  }
  if (in.type != Input::Type::Press || in.kind != core::InputKind::Button) {
    return;
  }
  if (in.which() == Button::Up || in.which() == Button::Down) {
    if (list_.move(src, in.which() == Button::Up ? -1 : 1)) {
      disarm();
      copy_text(sel_, sizeof sel_, selected());
    }
    ctx.redraw();
    return;
  }
  unsigned k = 0;
  if (in.fkey_press(k) && !in.shift) {  // the Shift row is empty
    key(ctx, k);
  }
}

void FileScreen::key(app::Context& ctx, unsigned k) noexcept {
  if (k == kLibrary) {
    disarm();
    ctx.show(*library_);
    return;
  }
  if (k == kSpare) {
    return;
  }
  if (k != kRevert) {
    revert_at_ = 0;
  }
  if (k != kDelete) {
    delete_at_ = 0;
  }
  if (!card_ready(ctx)) {  // no card, or one that cannot be written: nothing is asked of it
    const app::Files& f = ctx.files();
    say(ctx, ProjectService::card_text(f.available() ? f.card() : app::Files::Card::None), true);
    return;
  }
  if (svc_->request_busy()) {
    say(ctx, "BUSY", true);
    return;
  }
  const char* sel = selected();
  switch (k) {
    case kLoad:
      if (sel[0] == '\0') {
        say(ctx, "NO PROJECT SELECTED", true);
      } else {
        (void)svc_->load(sel);
      }
      break;
    case kSave:
      svc_->save();
      break;
    case kSaveAs:
      names_.open("SAVE AS", svc_->name(), app::NameEntry::Kind::FileName, ProjectInfo::kNameChars - 1u);
      entry_ = Entry::SaveAs;
      break;
    case kNew:
      names_.open("NEW PROJECT", "", app::NameEntry::Kind::FileName, ProjectInfo::kNameChars - 1u);
      entry_ = Entry::New;
      break;
    case kRevert:
      if (!svc_->has_project()) {  // nothing was ever saved: no need to ask twice
        say(ctx, "NOTHING SAVED YET", true);
      } else if (revert_at_ == 0u) {
        revert_at_ = ctx.uptime_ms() + 1u;
        say(ctx, "PRESS REVERT AGAIN: BACK TO THE LAST SAVE", true);
      } else {
        revert_at_ = 0;
        (void)svc_->revert();
      }
      break;
    case kDelete:
      if (sel[0] == '\0') {
        say(ctx, "NO PROJECT SELECTED", true);
      } else if (svc_->is_current(sel)) {  // D13
        say(ctx, "LOAD ANOTHER FIRST", true);
      } else if (delete_at_ == 0u || std::strcmp(delete_name_, sel) != 0) {
        delete_at_ = ctx.uptime_ms() + 1u;
        copy_text(delete_name_, sizeof delete_name_, sel);
        say(ctx, "PRESS DELETE AGAIN", true);
      } else {
        disarm();
        (void)svc_->remove(sel);
      }
      break;
    default:
      break;
  }
  ctx.redraw();
}

void FileScreen::keyboard_input(app::Context& ctx, const Input& in) noexcept {
  const app::NameEntry::Result r = names_.input(in);
  if (r == app::NameEntry::Result::Done) {
    name_done(ctx);
  } else if (r == app::NameEntry::Result::Cancelled) {
    entry_ = Entry::None;
  }
  ctx.redraw();
}

// The name is typed: the service has the last word, but a name it would refuse or another project's is caught here, with the keyboard still holding the text.
void FileScreen::name_done(app::Context& ctx) noexcept {
  const bool saving_as = entry_ == Entry::SaveAs;
  const char* title = saving_as ? "SAVE AS" : "NEW PROJECT";
  char text[ProjectInfo::kNameChars];
  copy_text(text, sizeof text, names_.text());
  if (!ProjectService::valid_name(text)) {
    reopen(ctx, title, text, "NOT A PROJECT NAME");
    return;
  }
  if (!svc_->list_valid()) {  // another project of that name cannot be ruled out yet
    reopen(ctx, title, text, "READING THE CARD: TRY AGAIN");
    return;
  }
  entry_ = Entry::None;
  if (saving_as) {
    if (svc_->is_current(text)) {  // SAVE AS under its own name is SAVE
      svc_->save();
    } else if (!svc_->save_as(text)) {  // the service's own reason (NAME TAKEN, TOO MANY PROJECTS, NAME USED BEFORE ...)
      seen_notice_ = rig_->project.notice_seq;
      reopen(ctx, title, text, rig_->project.notice);
      entry_ = Entry::SaveAs;
    }
  } else if (!svc_->new_project(text)) {
    seen_notice_ = rig_->project.notice_seq;
    reopen(ctx, title, text, rig_->project.notice);
    entry_ = Entry::New;
  }
}

void FileScreen::reopen(app::Context& ctx, const char* title, const char* text, const char* why) noexcept {
  names_.open(title, text, app::NameEntry::Kind::FileName, ProjectInfo::kNameChars - 1u);
  say(ctx, why, true);
}

// ---- Soft keys and LEDs -------------------------------------------------------------------------------------------------------------------------

void FileScreen::fill_view(app::Context& ctx, FileView& view) const noexcept {
  const app::Files& f = ctx.files();
  const app::Files::Card card = f.available() ? f.card() : app::Files::Card::None;
  copy_text(view.current, sizeof view.current, svc_->name());
  view.card = card == app::Files::Card::Ready ? "" : ProjectService::card_text(card);
  view.reading = card != app::Files::Card::None && !svc_->list_valid();
  SaveState ss;
  ss.card = card == app::Files::Card::Ready;
  ss.autosave = rig_->project.autosave;
  ss.saving = rig_->project.saving;
  ss.failed = rig_->project.failed;
  ss.ever = svc_->ever_saved();
  ss.ago_ms = ss.ever ? ctx.uptime_ms() - svc_->saved_ms() : 0u;
  (void)saved_text(ss, view.saved, sizeof view.saved);
  static constexpr const char* kLabels[8] = {"LOAD", "SAVE", "SAVE AS", "NEW", "REVERT", "DELETE", "", "LIBRARY"};
  for (unsigned k = 0; k < 8; ++k) {
    view.labels[k] = kLabels[k];
  }
  view.dim = 0;
  view.hot = 0;
  if (card != app::Files::Card::Ready || svc_->request_busy()) {
    view.dim = 0x3Fu;  // F1..F6
  } else if (svc_->projects() == 0u) {
    view.dim = (1u << kLoad) | (1u << kDelete);
  }
  if (revert_at_ != 0u) {
    view.labels[kRevert] = "REVERT?";
    view.hot |= 1u << kRevert;
  }
  if (delete_at_ != 0u) {
    view.labels[kDelete] = "DELETE?";
    view.hot |= 1u << kDelete;
  }
}

void FileScreen::soft_keys(app::Context& ctx, app::SoftKeys& out) noexcept {
  if (names_.active()) {
    names_.soft_keys(out);
    return;
  }
  if (ctx.panel().shift()) {  // the Shift row is empty
    return;
  }
  FileView v;
  fill_view(ctx, v);
  for (unsigned k = 0; k < 8; ++k) {
    out.label[k] = v.labels[k];
  }
  out.dim = v.dim;
  out.hot = v.hot;
}

void FileScreen::leds(app::Context& ctx, app::PadLeds& leds) noexcept {
  (void)ctx;
  if (names_.active()) {  // the 48 pads are the keyboard's
    for (unsigned pad = 0; pad < app::kPadCount; ++pad) {
      leds.set(pad, app::kLedDim);
    }
    return;
  }
  const unsigned n = svc_->projects();
  for (unsigned row = 0; row < list_.rows() && row < app::kPadCount; ++row) {
    const unsigned item = list_.first() + row;
    if (item >= n) {
      break;
    }
    leds.set(row, item == list_.cursor() ? app::kLedOn : app::kLedDim);
  }
}

// ---- The drawing --------------------------------------------------------------------------------------------------------------------------------

void FileScreen::draw(app::Context& ctx, ui::Renderer& r, const Rect& area) noexcept {
  if (names_.active()) {
    names_.draw(r, area);
    return;
  }
  const ui::Theme& theme = r.theme();
  fill_view(ctx, view_);

  frame::StatusView status;
  status.keys = false;
  std::strcpy(status.enc, "LIST");
  status.save_failed = rig_->project.failed;
  fill_take_status(*rig_, status);
  status.toast = toast_[0] != '\0' ? toast_ : view_.card;  // with no usable card the status line says so
  status.toast_off = toast_[0] != '\0' ? toast_off_ : true;
  frame::draw_status(r, Rect{area.x, area.y, area.w, frame::kStatusH}, status);

  // The project's line: where the projects are, which one this is, when it was last written.
  const Rect info{area.x, area.y + frame::kStatusH, area.w, kInfoH};
  ui::strip(r, info, ui::Edge::Bottom);
  ui::FieldRow row(r, 10, info.y + 23);
  row.field("DIR", Face::Data, "Apps/o3_303/", Color::ChromeDim, Color::ChromeText);
  row.field("CURRENT", Face::Data, view_.current, Color::ChromeDim, Color::ChromeText);
  if (view_.saved[0] != '\0') {
    row.put(Face::Label, view_.saved, Color::ChromeDim);
  }

  // The list, its window as many rows as the area holds (the pads follow it).
  const int top = info.y + kInfoH + kGap;
  const Rect box{area.x + kGap, top, area.w - 2 * kGap, area.y + area.h - frame::kPadsLineH - kGap - top};
  list_.set_rows(ui::ListView::rows_in(theme, box));
  ui::ListEmpty empty{"NO PROJECTS YET", "SAVE AS OR NEW MAKES ONE"};
  if (view_.card[0] != '\0') {
    empty = ui::ListEmpty{view_.card, "PUT A WRITABLE CARD IN"};
  } else if (view_.reading) {
    empty = ui::ListEmpty{"READING THE CARD", ""};
  }
  const Source src(*svc_);
  list_.fit(src);
  list_.draw(r, box, src, empty);

  frame::draw_pads_line(r, Rect{area.x, area.y + area.h - frame::kPadsLineH, area.w, frame::kPadsLineH},
                        view_.card[0] != '\0' ? view_.card : "LIST ROWS . TAP = SELECT");
}

}  // namespace o3_303
