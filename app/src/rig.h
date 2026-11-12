// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the screens share: the sequencer and its bank, the units' ids, the pages' controls, the live actions and the SOUND latch. The app owns one and hands every screen a
// pointer; nothing in it is touched by the render context (the sequencer's own atomics and rings are the way across).

#include "frame.h"

#include <o3sdk/app/context.h>
#include <o3sdk/dsp/engine.h>
#include <o3_303/pages.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/gen.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/acid_voice/acid_voice.h>
#include <o3_303/units/freeze/freeze.h>

#include <cstdint>
#include <cstring>

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
  // LAST (library load): the sound a load that replaced it took away, exactly the voice and distortion values a load writes (rig.cpp write_sound), kept in RAM only (not in the project).
  // last_used: it holds one. Main loop only. last_capture() fills it, last_swap() trades it with the sound on the unit.
  float last_voice[seq::gen::kVoiceValues] = {};
  float last_dist[seq::gen::kDistValues] = {};
  bool last_used = false;
  // The audio take's chip for every screen's status line (RecordScreen::service writes it every pass, the screens copy it into their StatusView): "TAKE ARMED", "TAKE REC 0:05",
  // "TAKE READY", "TAKE SAVING"; "" when no take is held. Main loop only.
  char take[20] = "";
  // The four sounds that are on, a bit per colour key (sound_bits(): bit 0 MODS, 1 GROWL, 2 TRACT, 3 WOBBLE), for the chips of every screen's header (E1c). O3_303App::update writes it every pass; a rig that has not been
  // updated yet shows the mods on, as a fresh voice. Main loop only.
  std::uint8_t sounds = 0x01;
  // What the take last said, for every screen's status line ("8 BARS TOO LONG AT 55.0", "SAVED", "NO CARD"): RecordScreen::say() writes it, service() clears it once `take_msg_until_ms`
  // (Context::uptime_ms) has passed. `take_msg_off`: it says something did not happen. Main loop only.
  char take_msg[40] = "";
  bool take_msg_off = false;
  std::uint64_t take_msg_until_ms = 0;
};

// A word on every screen's status line for a moment (Rig::take_msg): what the take says (RecordScreen::say) and STOCK's "STOCK ON" / "STOCK OFF". `off`: something did not happen.
inline constexpr std::uint64_t kStatusMessageMs = 2500;  // as the toasts last
inline void say_on_status(Rig& rig, o3sdk::app::Context& ctx, const char* text, bool off) noexcept {
  std::strncpy(rig.take_msg, text, sizeof rig.take_msg - 1u);
  rig.take_msg[sizeof rig.take_msg - 1u] = '\0';
  rig.take_msg_off = off;
  rig.take_msg_until_ms = ctx.uptime_ms() + kStatusMessageMs;
  ctx.redraw();
}

// The eleven mods (voice ids 8..18), RANGE and SUSTAIN: turning one (or tapping a ladder row that holds one) while STOCK is on ends STOCK. DECAY is the 303's own knob and STOCK itself is not one.
constexpr bool is_mod(const Entry& entry) noexcept { return entry.item == Item::Voice && entry.id >= AcidVoice::kFirstMod && entry.id < AcidVoice::kParamCount && entry.id != AcidVoice::Stock; }

// MODS (docs/MODS-PLAN.md 4.1, 5.11; B5a): the voice's Stock switch flips, and the status line says which way ("MODS OFF": the eleven mods are out and the voice is the plain 303; lit while heard). the Note key
// (the app) and MODDED's F8 are this one gesture. If the engine's command ring refused the write the mirror did not move and the message says so.
inline void toggle_stock(Rig& rig, o3sdk::app::Context& ctx) noexcept {
  const bool before = rig.wiring.stock;
  rig.controls.set(ctx.engine(), Entry{Item::Mods, 0, nullptr}, before ? 1.0f : 0.0f);  // Stock on: the mods are not heard, the flip hears them
  if (rig.wiring.stock == before) {
    say_on_status(rig, ctx, "MODS: TRY AGAIN", true);
    return;
  }
  say_on_status(rig, ctx, rig.wiring.stock ? "MODS OFF" : "MODS ON", false);
}

// The take's chip and message, the song mode and the sound chips, into a screen's StatusView (every screen that draws a status line calls it, so the audio take speaks and Play's mode and
// the sounds show wherever the player is).
inline void fill_take_status(const Rig& rig, frame::StatusView& status) noexcept {
  status.song = rig.seq != nullptr && rig.seq->play_song();  // PLAYS: SONG is the sequencer's, whatever screen is up (SONG's F1)
  status.sounds = rig.sounds;
  status.take = rig.take;
  status.take_msg = rig.take_msg;
  status.take_msg_off = rig.take_msg_off;
}

// What a vault load takes from a preset.
enum class LoadWhat : std::uint8_t {
  Both,     // the pattern with its tempo, and the sound
  Pattern,  // the pattern (swing is part of it) and the tempo (both kept while the transport runs: see load_preset())
  Sound,    // the patch: the voice's parameters and the distortion's
};

// Loads `preset` (docs/M3-PLAN.md "Contracts": a 16-step pattern, a patch, tempo and swing): the pattern replaces the selected pattern
// (one undoable edit), the patch goes to the voice and the distortion as parameter values, the tempo to the engine. Master and the
// level are not part of a preset: loading never jumps the volume of a set.
// `keep_timing` (the library while the transport runs, REVIEW S7) keeps the tempo and the swing the pattern had: the line is loaded and
// the engine's tempo and the selected pattern's swing stay as they are.
void load_preset(o3sdk::dsp::Engine& e, Rig& rig, const seq::gen::Preset& preset, LoadWhat what, bool keep_timing = false) noexcept;

// LAST: the sound as it is now goes into LAST.
void last_capture(const o3sdk::dsp::Engine& e, Rig& rig) noexcept;
// A new project's eight snapshots (REVIEW I4, docs/TRACT-KNOBS-PLAN.md 12.6 R10): call it after the sound is the project's own (a NEW, RESET PROJECT and a first boot, after the factory line is
// loaded). Slot 1 holds the sound as it is, the others are named sounds: a closed dubby one, a screaming high-reso one, a hollow square, an overdriven one, a long decay, FISH (RANGE FISH with
// the Devil Fish mods) and the growl on. Each is the sound as it stands with a few targets set from a constant table (rig.cpp); a project that is loaded is never given them, so its empty slots stay empty.
void install_factory_snapshots(const o3sdk::dsp::Engine& e, Rig& rig) noexcept;

// Swaps the sound on the unit and LAST (the values a load writes); false, nothing changed, when LAST is empty.
bool last_swap(o3sdk::dsp::Engine& e, Rig& rig) noexcept;

// How a song changed from `before` to `after`, for the sequencer's place in it (Sequencer::song_row_inserted() and friends): one row inserted at `at`, one row deleted at `at`, the
// same length and rows (None: a cell edit or nothing, the position stays), or anything else (Replaced: CLEAR, FROM CHAIN, several rows).
enum class SongMove : std::uint8_t { None, Inserted, Deleted, Replaced };
SongMove song_move(const seq::Song& before, const seq::Song& after, unsigned& at) noexcept;

// UNDO (`redo` false) or REDO on the project's one stack (the patterns' and the song's, seq::History), for every screen that has the keys: HOME's Shift row, PERFORM's pads and
// SONG's F8. A pattern step selects the pattern it changed; a song step hands the song back to the sequencer (Sequencer::sync_song(), or it would go on playing the song from
// before the step) and tells it how the rows moved (song_move(): the position of a song that plays follows an undone Insert or Delete, and drops the row it was on over a replaced
// song, sequencer.h "SG2"). false: there was nothing to undo or redo.
bool undo_step(Rig& rig, bool redo) noexcept;

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
