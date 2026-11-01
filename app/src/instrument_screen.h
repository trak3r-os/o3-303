// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// INSTRUMENT (docs/UX-303.md 5.5): the whole sound in view. Params opens it on VOICE, Params again cycles the pages:
//
//   VOICE   the sound knobs (the SOUND page on Rig::sound_keys: PERFORM's latch now, HOME's too from H4, D3)
//   FX      DRIVE TONE MIX MODEL (the distortion), DLY TIME FB SEND TONE (the delay)
//   MODS    VOWEL FMT Q GRL MIX CMB MIX (the growl), WOB RATE SHAPE DEPTH TARGET (the wobble)
// (FMT MIX, CMB TUNE, CMB FB, WOB RESET and DLY MODE, which no page has, are Config's page DEEP: config_pages.h.)
//
// The eight knobs of the page are drawn large, the sections of the other pages as read-outs, and under them the two lines of the drawer (NOTE and
// GATE) of the pattern's page of 16 steps. The pads, the arrows, Copy / Insert / Delete, Rec and the colour buttons (Note, Instr, Vol, Fx: the drawer rows NOTE, GATE,
// PLAYS and LOCK on the encoder; their captions here are NOTE and GATE, the ENC text names the other two) are the home map's (home_map.h), the very one HOME has (one
// cursor, one keyboard, one Rec): this screen forwards them and draws what it reads. The Shift row is MIDI learn (UX-303 5.5, D15): Shift + an F key arms the next
// control change for that knob (`Context::midi().learn(unit, param)`; the kit's strip confirms LEARNED CC n), again on the same key cancels; a knob that is not a unit
// parameter (VOLUME, DLY SEND, MODEL, the WOB knobs) shows "-" and cannot be armed. Leaving the screen or the page cancels an arm of this screen's.
//
// The view logic is apart from the drawing: InstrumentScreen::fill_view() makes an InstrumentView (instrument_view.h) from the state with the free functions below (fill_cells(),
// fill_sections(), apply_lock(): pure, host-tested), and draw() draws it, with the classic drawing or, while the obsidian skin is on, with skin/instrument.cpp (mockup 06: the panel at full
// height, the page's eight knobs on top, the sections, the step strip with its keys and LEDs, the page tabs): the skin replaces the drawing and not the logic. Main loop only.

#include "frame.h"
#include "home_map.h"
#include "instrument_view.h"
#include "knob_strip.h"
#include "live_actions.h"
#include "rig.h"

#include <o3sdk/app/screen.h>
#include <o3_303/pages.h>

#include <cstdint>

namespace o3_303 {

inline constexpr unsigned kInstrumentPageCount = 3;
enum InstrumentPage : unsigned { kInstVoice = 0, kInstFx = 1, kInstMods = 2 };

// The drawer's cells of the page of 16 steps that starts at step `first` of `p`: the note ("C2", "D#3", "--" a rest, "~" a tie) and the gate glyph
// ("." rest, "-" note, "A", "S", "AS", "~" tie); `view.cells` is how many are inside the pattern.
void fill_cells(const Pattern& p, unsigned first, InstrumentView& view) noexcept;
// The read-out sections page `page` shows (kInstVoice ..), each line "value" + unit as the knob row shows it; the wobble line is ON or OFF.
void fill_sections(const o3sdk::dsp::Engine& e, const Controls& controls, bool wobble_on, unsigned page, InstrumentView& view) noexcept;
// The knob `kn` as the lock of the held step `step` (0-based): "LOCK 5", the lock's text and position, in Accent. The label is kept in `view`.
void apply_lock(InstrumentView& view, view::Knob& kn, int step, const char* text, float norm) noexcept;

// The unit parameter an entry is, for MIDI learn: false for the entries that are app values (VOLUME, DLY SEND, MODEL, the wobble, the tempo ...) and the blank.
bool learn_target(const Wiring& wiring, const Entry& entry, o3sdk::dsp::UnitId& unit, std::uint16_t& param) noexcept;

class InstrumentScreen final : public o3sdk::app::Screen {
 public:
  // `home`: the home map HOME has.
  void init(Rig& rig, HomeMap& home) noexcept;

  // The Params button: from another screen it opens VOICE; on this one it goes to the next page and wraps.
  void open_first_page() noexcept;
  void next_page() noexcept;
  unsigned page() const noexcept { return page_; }

  const char* title() const noexcept override;
  // The obsidian skin (skin/skin.h): no kit chrome and the obsidian theme while it is active; otherwise the kit's.
  o3sdk::app::Chrome chrome(const o3sdk::app::Context& ctx) const noexcept override;
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context& ctx) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void leave(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& keys) noexcept override;
  void leds(o3sdk::app::Context& ctx, o3sdk::app::PadLeds& leds) noexcept override;
  // HELP (Shift + Config, help_texts.h): the page shown, the Shift row (LEARN where the knob is a unit parameter), and the home map's pads.
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& h) const noexcept override;

  // What the screen shows now, as both drawings take it (public for the tests, which read the view and the skin's queue against it).
  void fill_view(o3sdk::app::Context& ctx, InstrumentView& view) noexcept;

  // The F keys of the page shown and the entry the encoder drives (the tests read them).
  const Page& page_table() const noexcept { return keys().page(); }
  unsigned focus() const noexcept { return keys().focus(); }

 private:
  void button_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void turn_input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept;
  void release_keys() noexcept;
  void learn_key(o3sdk::app::Context& ctx, unsigned k) noexcept;
  void learn_drop(o3sdk::app::Context& ctx) noexcept;
  PageKeys& keys() noexcept { return page_ == kInstVoice ? rig_->sound_keys : own_[page_ - 1u]; }
  const PageKeys& keys() const noexcept { return page_ == kInstVoice ? rig_->sound_keys : own_[page_ - 1u]; }

  void draw_classic(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept;
  void draw_skinned(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r) noexcept;
  // What the Shift row says of key `k` (soft_keys() and the skin's knob labels): Learn::None while Shift is up.
  Learn learn_of(const o3sdk::app::Context& ctx, unsigned k) const noexcept;
  void enc_text(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;
  void pads_line(o3sdk::app::Context& ctx, char* out, unsigned cap) const noexcept;

  Rig* rig_ = nullptr;
  Controls* controls_ = nullptr;
  LiveActions* live_ = nullptr;
  HomeMap* home_ = nullptr;
  PageKeys own_[kInstrumentPageCount - 1u];  // FX and MODS each keep their own latch (VOICE has sound_keys)
  unsigned page_ = kInstVoice;
  bool learn_armed_ = false;   // this screen armed MIDI learn (the kit's armed state may have ended since: a controller bound it)
  unsigned learn_page_ = 0;    // ... on this page's key
  unsigned learn_key_ = 0;
  std::uint32_t tool_mask_ = 0;  // F keys pressed with Shift (their release is not the keys')
};

}  // namespace o3_303
