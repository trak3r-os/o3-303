// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The live actions (docs/UX-303.md 5.3, docs/M3B-PLAN.md C4 and R2, docs/M3C-PLAN.md R and W), one state for the screen that runs them (PERFORM:
// its 48 pads and the four colour buttons; on HOME, SETUP and INSTRUMENT the colour buttons are the drawer's rows since H3). It holds what is held and latched,
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

inline constexpr std::uint32_t kButtonTapMs = 300;  // a Shift + colour press shorter than this is a tap (it latches), a longer one a hold

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

  // ---- The four colour buttons (Note, Instr, Vol, Fx = 0..3) with Shift (E1b; the plain press is the sound's, sound_buttons.h): each does its assigned action (Wiring::perf). A tap under
  // kButtonTapMs latches it (a tap on a latched one lets it go); a hold is the action while the button is down. `now_ms` is Context::uptime_ms().
  void action_button_press(o3sdk::app::Context& ctx, unsigned button, std::uint64_t now_ms) noexcept;
  void action_button_release(o3sdk::app::Context& ctx, unsigned button, std::uint64_t now_ms) noexcept;
  unsigned buttons_held() const noexcept { return button_held_; }
  unsigned buttons_latched() const noexcept { return button_latched_; }

  // ---- PERFORM's action pads. A press holds the action while the pad is down; Shift + a press latches it; a press on a latched action
  // unlatches it. ALL OFF lets every latched action go (pads and buttons) and the wobble's rate override with them (UX-303 D27).
  void action_press(o3sdk::app::Context& ctx, PerfAction a, bool shift) noexcept;
  void action_release(o3sdk::app::Context& ctx, PerfAction a) noexcept;
  // The latch of `a` flips, as Shift + its pad would (docs/MODS-PLAN.md 5.5, WOBBLE's F8): a latched action lets go of every latch source (its pad bit and any colour button that carries it), else it is
  // latched on the pad bit; then it is applied. An action held down is not touched here, and the latch survives a screen change (D4); ALL OFF still lets it go.
  void toggle_latch(o3sdk::app::Context& ctx, PerfAction a) noexcept;
  void all_off(o3sdk::app::Context& ctx) noexcept;

  // The actions latched on a pad (a bit per PerfAction).
  std::uint16_t pads_latched() const noexcept { return pad_latched_; }
  // The actions that are on now (a bit per PerfAction), as applied.
  std::uint16_t on() const noexcept { return applied_; }
  bool is_on(PerfAction a) const noexcept { return (applied_ & perf_bit(a)) != 0u; }
  // The action is latched, by its own pad or by a colour button that carries it.
  bool latched(PerfAction a) const noexcept;
  bool wobble_on() const noexcept { return is_on(PerfAction::Wobble); }

  // ---- PERFORM's rate pads (H1). Row 2 (kRepeatPads): a pad is the beat freeze in a mode at a rate: FRZ 1/4, RPT 1/8..1/64, BLD, STOP, REV. Row 3 (kWobblePadRates): a pad runs the wobble at a rate.
  // The gestures are an action pad's (a press holds, Shift + a press latches, a tap on a latched one lets it go, ALL OFF lets every latch go). Of a row's pads the last one pressed wins, and when it
  // goes the one below it takes over. The freeze unit has one owner at a time (FreezeOwner): a pad of another action does nothing while it owns the unit. Pad effects are not recorded.
  void repeat_press(o3sdk::app::Context& ctx, unsigned col, bool shift) noexcept;
  void repeat_release(o3sdk::app::Context& ctx, unsigned col) noexcept;
  void wobble_press(o3sdk::app::Context& ctx, unsigned col, bool shift) noexcept;
  void wobble_release(o3sdk::app::Context& ctx, unsigned col) noexcept;
  const PadGroup& repeat() const noexcept { return rpt_; }
  const PadGroup& wobble_pads() const noexcept { return wob_; }
  // The action that has the beat freeze now (a PerfAction), -1 when none.
  int freeze_owner() const noexcept { return freeze_owner_.owner(); }

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

  // LADDER SLEW (Config 303, UX-303 5.12): the glide of a ladder tap on HOME's and PERFORM's pads, in milliseconds (10..100, 30 by default: kLadderSlewMs). Set by the app from the
  // Config value; the screens read it at each tap. Main loop only.
  float ladder_slew_ms() const noexcept { return ladder_slew_ms_; }
  void set_ladder_slew_ms(float ms) noexcept { ladder_slew_ms_ = ms; }

  // A seed that differs from press to press and from boot to boot (NEW, VARY, the RANDOM popup).
  std::uint32_t next_seed(o3sdk::app::Context& ctx) noexcept;

 private:
  void apply(o3sdk::app::Context& ctx) noexcept;
  void unlatch(PerfAction a) noexcept;
  void sync_assignments(o3sdk::app::Context& ctx) noexcept;
  void push_live_transpose() noexcept;
  void snap_apply(o3sdk::app::Context& ctx, float ms) noexcept;
  void apply_punches(std::uint16_t on) noexcept;

  Rig* rig_ = nullptr;
  Controls* controls_ = nullptr;
  // The four colour buttons (bit b = button b) and the pads (a bit per PerfAction), held and latched; the actions on now (applied).
  unsigned button_held_ = 0;
  unsigned button_latched_ = 0;
  unsigned button_pending_ = 0;  // a button pressed with Shift whose release is still to come (a screen's leave() ends it: no tap)
  std::uint32_t button_down_ms_[kPerfButtons] = {};  // Context::uptime_ms() at the press, wrapped to 32 bits: the difference is what is read
  std::uint16_t pad_held_ = 0;
  std::uint16_t pad_latched_ = 0;
  std::uint16_t applied_ = 0;
  FreezeOwner freeze_owner_;  // which of FREEZE, ROLL, STOP and REV has the beat freeze now (M3c R)
  PadGroup rpt_;              // PERFORM's row 2 (a column per kRepeatPads), held and latched (H1)
  PadGroup wob_;              // PERFORM's row 3 (a column per kWobblePadRates)
  int rpt_col_ = -1;          // the row-2 column whose rate the freeze unit has now (-1: none owns it)
  int wob_col_ = -1;          // the row-3 column whose rate is the sequencer's override now
  // H2's punches (Sequencer::set_punch: slot 0 and 1 DIST's Active and Mix, 2 the cutoff, 3 the resonance): what has been handed to the sequencer. A punch is held by the
  // sequencer, not stored in the engine, and not recorded.
  bool punch_dist_ = false;
  int punch_cut_ = -1;                      // -1 none, 0 CUT LO, 1 CUT HI
  bool punch_reso_ = false;
  PerfAction cut_last_ = PerfAction::CutLo;  // of CUT LO and CUT HI, the one pressed last wins while both are on
  std::uint8_t seen_[kPerfButtons] = {};  // the assignment each button had at the last update: a reassigned button drops its latch
  bool trnsp_ = false;
  float ladder_slew_ms_ = kLadderSlewMs;
  int live_trn_ = 0;
  bool was_playing_ = false;
  std::uint32_t seed_ = 0x303u;
  // The morph's slews are running (R2): at `snap_deadline_ms_` the blend is stored in the engine (sequencer.h, slew(): store = false).
  bool snap_pending_ = false;
  std::uint64_t snap_slewing_ = 0;  // a bit for each snapshot target (< 64: kSnapTargetCount) that this morph set gliding; main loop only
  std::uint64_t snap_deadline_ms_ = 0;
};

}  // namespace o3_303
