// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The live actions (docs/UX-303.md 5.3, docs/M3B-PLAN.md C4 and R2, docs/M3C-PLAN.md R and W), one state for the two screens that run them: the
// 303 screen (its four colour buttons, until slice H3) and PERFORM (its 48 pads and the same four buttons). It holds what is held and latched,
// puts it on the sequencer, the delay send, the live transpose, the beat freeze (through FreezeOwner) and the wobble, and runs the snapshots (recall,
// store, the morph's slews). The keyboard's live transposition and its mode (TRNSP: transpose the pattern, else play the voice) live here too:
// PERFORM's key row and the home map's keyboard are one keyboard. Main loop only; the app owns one and `Rig::live` points at it.
//
//   held        a colour button or an action's pad is down: the action is on until it is let go (a screen's leave() lets every one go)
//   latched     Shift + the button or the pad: on until it is tapped again or ALL OFF; a latched action stays on when the screen changes (D4)

#include "rig.h"

#include <o3sdk/app/context.h>
#include <o3_303/perform.h>

#include <cstdint>

namespace o3_303 {

class LiveActions {
 public:
  // What a snapshot gesture says (the screen shows it as its toast); `off`: it says something did not happen.
  struct Message {
    char text[24] = "";
    bool off = false;
  };

  // Every piece of state starts afresh: the app object outlives a run in the simulator's tests.
  void init(Rig& rig) noexcept;
  // Every pass of the main loop (the app calls it, whatever screen is up): the keyboard's transposition ends with the run, a button the
  // PERF page gave another action lets go of its latch, the morph's slews are stored once they are over.
  void update(o3sdk::app::Context& ctx) noexcept;
  // A screen is left: the actions that were held end (their releases would reach another screen); the latched ones stay.
  void leave(o3sdk::app::Context& ctx) noexcept;

  // ---- The four colour buttons (Note, Instr, Vol, Fx = 0..3): each does its assigned action (Wiring::perf) while held; Shift latches.
  void button_press(o3sdk::app::Context& ctx, unsigned button, bool shift) noexcept;
  void button_release(o3sdk::app::Context& ctx, unsigned button) noexcept;
  unsigned buttons_held() const noexcept { return button_held_; }
  unsigned buttons_latched() const noexcept { return button_latched_; }

  // ---- PERFORM's action pads. A press holds the action while the pad is down; Shift + a press latches it; a press on a latched action
  // unlatches it. ALL OFF lets every latched action go (pads and buttons) and the wobble's rate override with them (UX-303 D27).
  void action_press(o3sdk::app::Context& ctx, PerfAction a, bool shift) noexcept;
  void action_release(o3sdk::app::Context& ctx, PerfAction a) noexcept;
  void all_off(o3sdk::app::Context& ctx) noexcept;

  // The actions latched on a pad (a bit per PerfAction).
  std::uint16_t pads_latched() const noexcept { return pad_latched_; }
  // The actions that are on now (a bit per PerfAction), as applied.
  std::uint16_t on() const noexcept { return applied_; }
  bool is_on(PerfAction a) const noexcept { return (applied_ & perf_bit(a)) != 0u; }
  // The action is latched, by its own pad or by a colour button that carries it.
  bool latched(PerfAction a) const noexcept;
  bool wobble_on() const noexcept { return is_on(PerfAction::Wobble); }
  // The wobble's rate (a seq::WobbleRate) until the action lets go (PERFORM's row 3); the 303 screen sets it through the sequencer itself.
  void wobble_rate(o3sdk::app::Context& ctx, int rate) noexcept;

  // ---- The keyboard. TRNSP (UX-303 6.2): the keys transpose the running pattern, the 303's track mode; off (the default) they play the voice, stopped
  // or playing. The live transposition is in semitones (-48..48 with OCT UP on top of it), set by a key and ended with the run.
  bool trnsp() const noexcept { return trnsp_; }
  void set_trnsp(bool on) noexcept { trnsp_ = on; }
  int live_transpose() const noexcept { return live_trn_; }
  void set_live_transpose(int semitones) noexcept;

  // ---- Snapshots (R2): `slot` 0..7. Shift stores the sound now; else a recall (an empty slot says so) glides there in 30 ms and the encoder
  // morphs from the snapshot recalled before. The messages are the screens' toasts.
  void snap_press(o3sdk::app::Context& ctx, unsigned slot, bool shift, Message& msg) noexcept;
  // false (and a message) when no snapshot was recalled yet.
  bool snap_turn(o3sdk::app::Context& ctx, int detents, bool coarse, Message& msg) noexcept;
  // The morph's blend is stored in the engine: once its slews are over, or now.
  void snap_finish(o3sdk::app::Context& ctx, bool at_once) noexcept;

  // A seed that differs from press to press and from boot to boot (NEW, VARY, the RANDOM popup).
  std::uint32_t next_seed(o3sdk::app::Context& ctx) noexcept;

 private:
  void apply(o3sdk::app::Context& ctx) noexcept;
  void unlatch(PerfAction a) noexcept;
  void sync_assignments(o3sdk::app::Context& ctx) noexcept;
  void push_live_transpose() noexcept;
  void snap_apply(o3sdk::app::Context& ctx, float ms) noexcept;

  Rig* rig_ = nullptr;
  Controls* controls_ = nullptr;
  // The four colour buttons (bit b = button b) and the pads (a bit per PerfAction), held and latched; the actions on now (applied).
  unsigned button_held_ = 0;
  unsigned button_latched_ = 0;
  std::uint16_t pad_held_ = 0;
  std::uint16_t pad_latched_ = 0;
  std::uint16_t applied_ = 0;
  FreezeOwner freeze_owner_;  // which of FREEZE, ROLL, STOP and REV has the beat freeze now (M3c R)
  std::uint8_t seen_[kPerfButtons] = {};  // the assignment each button had at the last update: a reassigned button drops its latch
  bool trnsp_ = false;
  int live_trn_ = 0;
  bool was_playing_ = false;
  std::uint32_t seed_ = 0x303u;
  // The morph's slews are running (R2): at `snap_deadline_ms_` the blend is stored in the engine (sequencer.h, slew(): store = false).
  bool snap_pending_ = false;
  std::uint64_t snap_deadline_ms_ = 0;
};

}  // namespace o3_303
