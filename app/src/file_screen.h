// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FILE PROJECTS (docs/UX-303.md 5.8 and 8.5, D10 to D13, docs/UX-PLAN.md F3): the projects on the card and what to do with them, over the ProjectService
// (project_service.h), which does every card job in turn and never waits for one. The File button opens it (File again: FILE LIBRARY, vault_screen.h, and back).
//
//   encoder, UP / DOWN    the cursor on the list        pad of a visible row   select it (ui::ListView::pad)
//   F1 LOAD               the selected project (the transport stops, the current one is autosaved first)
//   F2 SAVE               the same as Shift + File      F3 SAVE AS, F4 NEW    ask for a name on the kit's keyboard (app::NameEntry on the 48 pads)
//   F5 REVERT             back to the last SAVE, pressed twice within 5 s        F6 DELETE   the selected project, twice within 5 s (never the current: D13)
//   F7 empty              F8 LIBRARY
//
// With no card (or one that cannot be written) the list says so, F1..F6 are dim and press nothing, and the screen makes no file call at all; the list is read
// by the service while FILE is up (want_list()), never here. The list shows no dates or sizes (D10): the kit lists folders without them. The current project is
// marked "*".
//
// The view logic is apart from the drawing: the free functions below make the texts (host-tested), fill_view() makes a FileView from the state, draw() draws it
// (the skin replaces the drawing, not the logic). Main loop only.

#include "project_service.h"
#include "rig.h"

#include <o3sdk/app/name_entry.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/ui/list_view.h>

#include <cstdint>

namespace o3_303 {

// "JUST NOW", "12 S AGO", "5 MIN AGO", "2 H AGO".
unsigned ago_text(std::uint64_t ms, char* out, unsigned cap) noexcept;

// What the project's line under the status line says about the card.
struct SaveState {
  bool card = false;       // the card is there and can be written
  bool autosave = false;   // the session is a project on the card (ProjectInfo::autosave)
  bool saving = false;
  bool failed = false;
  bool ever = false;       // a write ended well since the boot
  std::uint64_t ago_ms = 0;
};
// "AUTOSAVED 12 S AGO", "SAVING", "NOT SAVED" (the last write failed), "NOT ON THE CARD YET" (no project on it: SAVE), "NOTHING WRITTEN YET"; "" with no card.
unsigned saved_text(const SaveState& s, char* out, unsigned cap) noexcept;

// What the screen shows besides the list.
struct FileView {
  char current[ProjectInfo::kNameChars] = "";
  char saved[40] = "";
  const char* card = "";      // why there is no list: "NO CARD", "CARD WRITE PROTECTED", ...; "" when the card is usable
  bool reading = false;       // the card is there, the list is not read yet
  const char* labels[8] = {};
  std::uint8_t dim = 0;       // F keys that do nothing now
  std::uint8_t hot = 0;       // F keys that wait for their second press
};

class FileScreen final : public o3sdk::app::Screen {
 public:
  // `library`: the screen F8 (and File again) goes to.
  void init(Rig& rig, ProjectService& service, o3sdk::app::Screen& library) noexcept;

  const char* title() const noexcept override { return "FILE PROJECTS"; }
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& keys) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;

  // The File button while this screen is up: the keyboard (if open) is dropped.
  void close_keyboard() noexcept { names_.close(); entry_ = Entry::None; }

  // What the tests read.
  bool naming() const noexcept { return names_.active(); }
  unsigned cursor() const noexcept { return list_.cursor(); }
  const char* selected() const noexcept;
  bool revert_armed() const noexcept { return revert_at_ != 0u; }
  bool delete_armed() const noexcept { return delete_at_ != 0u; }
  const char* toast() const noexcept { return toast_; }
  const FileView& last_view() const noexcept { return view_; }

 private:
  enum class Entry : std::uint8_t { None, SaveAs, New };

  bool card_ready(o3sdk::app::Context& ctx) const noexcept;
  void say(o3sdk::app::Context& ctx, const char* text, bool off = false) noexcept;
  void disarm() noexcept;
  void key(o3sdk::app::Context& ctx, unsigned k) noexcept;
  void keyboard_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void name_done(o3sdk::app::Context& ctx) noexcept;
  void reopen(o3sdk::app::Context& ctx, const char* title, const char* text, const char* why) noexcept;
  bool follow_list() noexcept;  // true: the list is another one
  void fill_view(o3sdk::app::Context& ctx, FileView& view) const noexcept;

  class Source;  // the list's items: the service's project list

  Rig* rig_ = nullptr;
  ProjectService* svc_ = nullptr;
  o3sdk::app::Screen* library_ = nullptr;
  o3sdk::ui::ListView list_{12};
  o3sdk::app::NameEntry names_;
  Entry entry_ = Entry::None;
  char sel_[ProjectInfo::kNameChars] = "";  // the selected project's name: the cursor stays on it when the list is read again
  std::uint32_t list_version_ = 0;
  // F5 and F6 want a second press within kConfirmMs: the time of the first (0: not armed), and for DELETE the name it was for.
  std::uint64_t revert_at_ = 0;
  std::uint64_t delete_at_ = 0;
  char delete_name_[ProjectInfo::kNameChars] = "";
  std::uint32_t seen_notice_ = 0;
  char toast_[48] = "";
  bool toast_off_ = false;
  std::uint64_t toast_until_ms_ = 0;
  std::uint64_t shown_secs_ = 0;  // the "autosaved N s ago" seconds last drawn (a change redraws)
  FileView view_;
};

}  // namespace o3_303
