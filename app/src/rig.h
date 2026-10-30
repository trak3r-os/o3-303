// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the screens share: the sequencer and its bank, the units' ids, the pages' controls, the live actions and the SOUND latch. The app owns one and hands every screen a
// pointer; nothing in it is touched by the render context (the sequencer's own atomics and rings are the way across).

#include "frame.h"

#include <o3sdk/dsp/engine.h>
#include <o3_303/pages.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/gen.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/freeze/freeze.h>

#include <cstdint>

namespace o3_303 {

class LiveActions;

// What the project service tells the screens (project_service.h; the service writes it from the main loop, the screens only read): the name, whether a
// save is under way, whether the last one failed and why, and a one-off message. A screen shows `failed` as "SAVE FAILED" on its status line (frame.h) and
// `fail` after it; it toasts `notice` when `notice_seq` has moved since it last looked.
struct ProjectInfo {
  static constexpr unsigned kNameChars = 16;  // with the NUL (a valid_name is at most 15)
  char name[kNameChars] = "UNTITLED";
  bool autosave = false;  // the session has a project on the card: autosave is on (off: no card at boot, or a project that was not loaded)
  bool saving = false;    // an autosave or a SAVE is being encoded or written (the quiet strip: no kit message; a screen may show a dot)
  bool failed = false;    // the last save failed and none has worked since (UX-303 D23)
  char fail[32] = "";     // why, the kit's words: "CARD IS FULL", "NO CARD"
  const char* notice = "";  // a message for the player, once: "SAVED", "NO CARD", "PROJECT DAMAGED" (static text)
  std::uint32_t notice_seq = 0;
};

struct Rig {
  seq::Bank* bank = nullptr;
  seq::Sequencer* seq = nullptr;
  Wiring wiring;
  Controls controls;
  LiveActions* live = nullptr;  // the live actions, the snapshots and the keyboard's transposition, shared by the 303 screen and PERFORM
  // The SOUND page's latch (which of the eight sound knobs the encoder drives), shared by the screens that show the sound knobs: PERFORM now,
  // INSTRUMENT VOICE and HOME later (UX-303 D3). Main loop only.
  PageKeys sound_keys;
  ProjectInfo project;  // the project service's state (project_service.h)
  // The tempo (milli-BPM) a tempo nudge started from, while one is on or its restore is pending, else 0 (MasterScreen writes it, main loop only): the engine holds the
  // nudged tempo then, and a project saved meanwhile (project.cpp gather()) stores this one.
  std::uint32_t tempo_base_mbpm = 0;
  // The audio take's chip for every screen's status line (RecordScreen::service writes it every pass, the screens copy it into their StatusView): "TAKE ARMED", "TAKE REC 0:05",
  // "TAKE READY", "TAKE SAVING"; "" when no take is held. Main loop only.
  char take[20] = "";
  // What the take last said, for every screen's status line ("8 BARS TOO LONG AT 55.0", "SAVED", "NO CARD"): RecordScreen::say() writes it, service() clears it once `take_msg_until_ms`
  // (Context::uptime_ms) has passed. `take_msg_off`: it says something did not happen. Main loop only.
  char take_msg[40] = "";
  bool take_msg_off = false;
  std::uint64_t take_msg_until_ms = 0;
};

// The take's chip and message into a screen's StatusView (every screen that draws a status line calls it, so the audio take speaks wherever the player is).
inline void fill_take_status(const Rig& rig, frame::StatusView& status) noexcept {
  status.take = rig.take;
  status.take_msg = rig.take_msg;
  status.take_msg_off = rig.take_msg_off;
}

// What a vault load takes from a preset.
enum class LoadWhat : std::uint8_t {
  Both,     // the pattern with its tempo, and the sound
  Pattern,  // the pattern (swing is part of it) and the tempo
  Sound,    // the patch: the voice's parameters and the distortion's
};

// Loads `preset` (docs/M3-PLAN.md "Contracts": a 16-step pattern, a patch, tempo and swing): the pattern replaces the selected pattern
// (one undoable edit), the patch goes to the voice and the distortion as parameter values, the tempo to the engine. Master and the
// level are not part of a preset: loading never jumps the volume of a set.
void load_preset(o3sdk::dsp::Engine& e, Rig& rig, const seq::gen::Preset& preset, LoadWhat what) noexcept;

// The beat freeze (units/freeze, docs/M3B-PLAN.md R6) on master insert 0, from the main loop: engine parameters, applied at frame 0 of the
// next block. Off is the default and is invisible (the unit passes the master through bit for bit). The Fx performance button's FREEZE
// action calls these. `set_freeze_length` takes effect at the next entry (the loop is latched when it starts); `freeze_on` is what the
// main loop last asked for, not whether the loop is audible yet.
void set_freeze(o3sdk::dsp::Engine& e, const Rig& rig, bool on) noexcept;
bool freeze_on(const o3sdk::dsp::Engine& e, const Rig& rig) noexcept;
// The freeze's Mode (M3c R: FREEZE, ROLL, STOP, REVERSE), latched by the unit at its next entry: written just before Active when an action goes on.
void set_freeze_mode(o3sdk::dsp::Engine& e, const Rig& rig, Freeze::ModeValue mode) noexcept;
void set_freeze_length(o3sdk::dsp::Engine& e, const Rig& rig, Freeze::LengthValue length) noexcept;
Freeze::LengthValue freeze_length(const o3sdk::dsp::Engine& e, const Rig& rig) noexcept;

}  // namespace o3_303
