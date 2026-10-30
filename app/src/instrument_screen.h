// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// INSTRUMENT (docs/UX-303.md 5.5): the whole sound in view. Params opens it on VOICE, Params again cycles the pages:
//
//   VOICE   the sound knobs (the SOUND page on Rig::sound_keys: PERFORM's latch now, HOME's too from H4, D3)
//   FX      DRIVE TONE MIX MODEL (the distortion), DLY TIME FB SEND TONE (the delay)
//   MODS    VOWEL FMT Q GRL MIX CMB MIX (the growl), WOB RATE SHAPE DEPTH TARGET (the wobble)
//   DEEP    until Config DEEP (C1): FMT MIX, CMB TUNE, CMB FB, WOB RESET, DLY MODE, which no other page has
//
// The eight knobs of the page are drawn large, the sections of the other pages as read-outs, and under them the two lines of the drawer (NOTE and
// GATE) of the pattern's page of 16 steps. The pads, the arrows, Copy / Insert / Delete, Rec and the colour buttons (Note, Instr, Vol, Fx: the drawer rows NOTE, GATE,
// PLAYS and LOCK on the encoder; their captions here are NOTE and GATE, the ENC text names the other two) are the home map's (home_map.h), the very one HOME has (one
// cursor, one keyboard, one Rec): this screen forwards them and draws what it reads. The Shift row is empty until C1
// (MIDI learn).
//
// The view logic is apart from the drawing: InstrumentScreen::fill_view() makes an InstrumentView from the state with the free functions below (fill_cells(),
// fill_sections(), apply_lock(): pure, host-tested), and draw() draws it, so the skin replaces the drawing and not the logic. Main loop only.

#include "frame.h"
#include "home_map.h"
#include "knob_strip.h"
#include "live_actions.h"
#include "rig.h"

#include <o3sdk/app/screen.h>
#include <o3_303/pages.h>

#include <cstdint>

namespace o3_303 {

inline constexpr unsigned kInstrumentPageCount = 4;
enum InstrumentPage : unsigned { kInstVoice = 0, kInstFx = 1, kInstMods = 2, kInstDeep = 3 };

// A read-out section: a title and up to four "NAME value" lines.
struct InstrumentSection {
  static constexpr unsigned kRows = 4;
  const char* title = "";
  unsigned count = 0;
  const char* label[kRows] = {};
  char value[kRows][12] = {};
};

// One step of the drawer's two lines.
struct InstrumentCell {
  char note[5] = "";  // "C2", "D#3", "--" a rest, "~" a tie
  char gate[3] = "";  // ".", "-", "A", "S", "AS", "~"
};

struct InstrumentView {
  static constexpr unsigned kMaxSections = 4;
  view::Knob knob[view::kKnobs];  // the page's eight knobs (a blank one has no box)
  InstrumentSection section[kMaxSections];
  unsigned sections = 0;
  InstrumentCell cell[16];  // the page of 16 steps shown
  unsigned cells = 0;       // how many of them are inside the pattern
  view::Marks marks;        // the cursor, the playhead and the held steps of the page
  char lock_label[16] = "";  // "LOCK 5": the label of the knob the encoder locks on a held step
};

// The drawer's cells of the page of 16 steps that starts at step `first` of `p`: the note ("C2", "D#3", "--" a rest, "~" a tie) and the gate glyph
// ("." rest, "-" note, "A", "S", "AS", "~" tie); `view.cells` is how many are inside the pattern.
void fill_cells(const Pattern& p, unsigned first, InstrumentView& view) noexcept;
// The read-out sections page `page` shows (kInstVoice ..), each line "value" + unit as the knob row shows it; the wobble line is ON or OFF.
void fill_sections(const o3sdk::dsp::Engine& e, const Controls& controls, bool wobble_on, unsigned page, InstrumentView& view) noexcept;
// The knob `kn` as the lock of the held step `step` (0-based): "LOCK 5", the lock's text and position, in Accent. The label is kept in `view`.
void apply_lock(InstrumentView& view, view::Knob& kn, int step, const char* text, float norm) noexcept;

class InstrumentScreen final : public o3sdk::app::Screen {
 public:
  // `home`: the home map HOME has.
  void init(Rig& rig, HomeMap& home) noexcept;

  // The Params button: from another screen it opens VOICE; on this one it goes to the next page and wraps.
  void open_first_page() noexcept;
  void next_page() noexcept;
  unsigned page() const noexcept { return page_; }

  const char* title() const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& keys) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;

  // The F keys of the page shown and the entry the encoder drives (the tests read them).
  const Page& page_table() const noexcept { return keys().page(); }
  unsigned focus() const noexcept { return keys().focus(); }

 private:
  void button_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void turn_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void release_keys() noexcept;
  PageKeys& keys() noexcept { return page_ == kInstVoice ? rig_->sound_keys : own_[page_ - 1u]; }
  const PageKeys& keys() const noexcept { return page_ == kInstVoice ? rig_->sound_keys : own_[page_ - 1u]; }

  void fill_view(o3sdk::app::Context& ctx, InstrumentView& view) noexcept;
  void enc_text(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;
  void pads_line(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;

  Rig* rig_ = nullptr;
  Controls* controls_ = nullptr;
  LiveActions* live_ = nullptr;
  HomeMap* home_ = nullptr;
  PageKeys own_[kInstrumentPageCount - 1u];  // FX, MODS and DEEP each keep their own latch (VOICE has sound_keys)
  unsigned page_ = kInstVoice;
  std::uint32_t tool_mask_ = 0;  // F keys pressed with Shift (their release is not the keys')
};

}  // namespace o3_303
