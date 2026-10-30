// SPDX-License-Identifier: GPL-3.0-or-later
//
// O3-303: the TB-303 app of docs/DESIGN-303.md. The engine: the one acid_voice voice on slot 0 with the distortion as its insert, o3sdk's delay
// on send A, the beat freeze on master insert 0 (off), the growl on slot 0's insert 1 (Mix 0: a bit-exact bypass), the sequencer (o3_303_seq) as a clocked hook. The screens are in main_screen.cpp
// (the 303 screen, with the pattern grid it shows while Pattern is held) and vault_screen.cpp (File: GEN and the vault). This file wires
// them and owns the global keys:
//
//   PLAY          play / stop at the press (Shift: restart from step 1)
//   PATTERN       HOME (on it: a tap flips HOME and PATTERN SETUP; held: the 48 pads are the patterns)
//   PARAMS        INSTRUMENT, on VOICE (on it: the next page, VOICE FX MODS DEEP); the pads are the home map's, HOME's own
//   PERFORM       PERFORM, the 48 pads live (on it: PERFORM SETUP, and back); the live actions are LiveActions', shared with the 303 screen
//   MASTER        a tap opens MASTER at the release; held + turn nudges the tempo from any screen; Shift + MASTER taps the tempo (master_screen.h)
//   FILE          FILE PROJECTS (file_screen.h); again FILE LIBRARY, the generated lines (vault_screen.h); again PROJECTS; Shift + FILE: SAVE the project from every screen (project_service.h)
//   SAMPLE REC    RECORD (record_screen.h): bar-aligned takes of the master or the input to Samples/; Shift + REC arms and stops a take from every screen
//   SAMPLE LOAD / PLAY / EDIT   HOOKS, the placeholder until M4 (hooks_screen.h)
//   CONFIG        o3sdk's system Config (FIRMWARE, MIDI, DISPLAY with the theme, INFO); the app never sees the key
//
// Everything else belongs to the screen that is up (the performance buttons' FREEZE, FILL and the rest are the 303 screen's, C4).

#include "file_screen.h"
#include "hooks_screen.h"
#include "instrument_screen.h"
#include "live_actions.h"
#include "main_screen.h"
#include "master_screen.h"
#include "perform_screen.h"
#include "project_service.h"
#include "record_screen.h"
#include "rig.h"
#include "vault_screen.h"

#include <o3_303/app_access.h>
#include <o3sdk/app/app.h>
#include <o3sdk/app/context.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/core/placement.h>
#include <o3sdk/dsp/engine.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/seq/bank.h>
#include <o3_303/seq/gen.h>
#include <o3_303/seq/sequencer.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cstdint>
#include <cstring>

namespace {

using namespace o3sdk;
using app::Button;

constexpr std::uint32_t kDelayCapacity = 88'200;  // 2 s at 44.1 kHz, each side
O3SDK_BIG float g_delay_l[kDelayCapacity];
O3SDK_BIG float g_delay_r[kDelayCapacity];

// The beat freeze's ring (units/freeze): 131,072 frames a side, 1 MiB for the pair, 2.97 s: a whole beat down to 20.3 BPM. A power of two.
constexpr std::uint32_t kFreezeCapacity = 131'072;
struct alignas(8) FreezeRing {
  float v[kFreezeCapacity];
};
static_assert(alignof(FreezeRing) == 8 && sizeof(FreezeRing) == kFreezeCapacity * sizeof(float), "the freeze copies its ring with word loads");
static_assert(2u * sizeof(FreezeRing) <= (1u << 20), "the freeze's ring is at most 1 MiB for both sides");
O3SDK_BIG FreezeRing g_freeze_l;
O3SDK_BIG FreezeRing g_freeze_r;

constexpr std::uint32_t kDefaultTempoMilli = 125'000;  // Replicant's default
constexpr std::uint64_t kSavingToastMs = 1000;         // the SAVING toast is said again this often (a toast lasts a little longer)

class O3_303App final : public app::App {
 public:
  const app::AppInfo& info() const noexcept override {
    static constexpr app::AppInfo kInfo{"O3-303", "0.0.1"};
    return kInfo;
  }

  void setup(app::Context& ctx) noexcept override {
    dsp::Engine& e = ctx.engine();
    rig_.wiring = o3_303::Wiring{};
    rig_.sound_keys = o3_303::PageKeys{};  // the global table's page SOUND, F1 latched
    rig_.sound_keys.set_page(o3_303::kPageSound);
    rig_.live = &live_;
    rig_.bank = &o3_303::seq::project_bank();
    rig_.bank->init();
    rig_.seq = &sequencer_;
    rig_.wiring.seq = &sequencer_;

    const dsp::UnitId voice = e.attach(voice_);
    const dsp::UnitId dist = e.attach(dist_);
    const dsp::UnitId delay = e.attach(delay_);
    const dsp::UnitId freeze = e.attach(freeze_);  // after the others: the units' ids (voice 0, distortion 1, delay 2, freeze 3) are the tests' contract
    const dsp::UnitId growl = e.attach(growl_);    // M3c: slot 0's insert 1, after the distortion; attached last, so the ids above stay
    (void)e.set_source(0, voice);
    (void)e.set_insert(0, 0, dist);
    (void)e.set_insert(0, 1, growl);
    (void)e.set_send_effect(0, delay);
    (void)e.set_master_insert(0, freeze);
    (void)ctx.midi().route(voice);  // MIDI notes on the user's channel (system Config, MIDI) play the voice; no device, no effect
    (void)sequencer_.setup(e, voice, dist, delay);  // adds its hook to the engine; the lanes may drive the voice, the distortion, the delay
    (void)sequencer_.set_unit(o3_303::kLaneGrowl, growl);  // ... and the growl, whose Note the hook sets on every note start (after setup(), before start())
    // A run starts with no performance flag, no live transpose and no chain (the app object outlives a run in the simulator's tests).
    sequencer_.set_perf(0);
    sequencer_.set_fill_variant(o3_303::seq::FillVariant::Mutate);
    sequencer_.set_live_transpose(0);
    sequencer_.wobble_rate_override(o3_303::seq::kWobbleRateNone);
    sequencer_.set_wobble_reset(true);
    (void)sequencer_.set_wobble(false, 0, 0, 0.0f);  // the WOBBLE action switches it on
    (void)sequencer_.clear_queue();
    (void)sequencer_.set_chain_loop(false);

    rig_.wiring.voice = voice;
    rig_.wiring.dist = dist;
    rig_.wiring.delay = delay;
    rig_.wiring.freeze = freeze;
    rig_.wiring.growl = growl;
    rig_.wiring.bank = rig_.bank;
    rig_.controls.init(rig_.wiring);

    // The voice's own volume stays at 0 dB; SOUND's VOLUME is slot 0's level, after the distortion.
    (void)e.set_param(voice, o3_303::AcidVoice::Volume, 0.0f);
    rig_.controls.apply_level(e);
    rig_.controls.apply_send(e);
    (void)e.set_tempo(kDefaultTempoMilli);

    // First boot: the vault's factory line (bank 0, slot 0), pattern and sound, in pattern 0, so Play sounds at once.
    o3_303::seq::gen::Preset first;
    o3_303::seq::gen::factory(first, 0, 0);
    o3_303::load_preset(e, rig_, first, o3_303::LoadWhat::Both);
    rig_.bank->clear_history();  // the boot load is no edit of the user's: the first UNDO must not empty A01

    live_.init(rig_);
    main_.init(rig_);
    perform_.init(rig_, main_);
    instrument_.init(rig_, main_.home_map());  // the home map HOME has: one cursor, one keyboard, one Rec
    master_.init(rig_, main_.home_map());  // the home map HOME has, as INSTRUMENT
    record_.init(rig_, main_.home_map());  // the home map HOME has, as MASTER
    hooks_.init(rig_);
    vault_.init(rig_);
    file_.init(rig_, project_, vault_);
    project_.init(rig_, e);  // last: it records the session as setup leaves it, so that a project loaded from the card knows if the player has edited
    saving_ = false;
    save_asked_ = false;
    saving_toast_ms_ = 0;
    ctx.show(main_);
  }

  bool input(app::Context& ctx, const app::Input& in) noexcept override {
    dsp::Engine& e = ctx.engine();
    // MASTER (UX-303 2.6, D5): the button and, while it is down, the encoder are the app's from every screen, so that none sees them. Another press
    // while it is down (a chord) makes its release no tap.
    if (master_.master_down() && in.type == app::Input::Type::Press && !in.is(Button::Master)) {
      master_.master_chord();
    }
    if (in.turn() && master_.master_turn(ctx, in.delta())) {  // hold + turn: the tempo nudge
      return true;
    }
    if (in.press(Button::Master)) {
      master_.master_press(ctx, in.shift);  // Shift: a tap of the tap tempo; otherwise MASTER opens at the release of a tap
      return true;
    }
    if (in.release(Button::Master)) {
      master_.master_release(ctx);
      return true;
    }
    if (in.press(Button::Play)) {
      if (in.shift) {
        (void)e.play();  // restart from step 1 (resync): play() from the top, the sequencer starts the pattern again while it runs
      } else {
        (void)(e.playing() ? e.stop() : e.play());
      }
      ctx.redraw();
      return true;
    }
    if (in.press(Button::Pattern) && ctx.screen() != &main_) {
      ctx.show(main_);  // back from the vault or PERFORM: the press is taken, the screen's own release does nothing
      return true;
    }
    if (in.press(Button::Performance)) {  // PERFORM; on it, PERFORM SETUP and back; from any other screen always page 1
      if (ctx.screen() == &perform_) {
        perform_.toggle_page();
        ctx.redraw();
      } else {
        perform_.open_first_page();
        ctx.show(perform_);
      }
      return true;
    }
    // PARAMS: INSTRUMENT on VOICE, and on it the next page (UX-303 2.1, D21), from every screen. Shift changes nothing.
    if (in.press(Button::Params)) {
      if (ctx.screen() == &instrument_) {
        instrument_.next_page();
        ctx.redraw();
      } else {
        instrument_.open_first_page();
        ctx.show(instrument_);
      }
      return true;
    }
    // RECORD and HOOKS (UX-303 2.1, 5.10, 5.11): the Sample Recorder button opens RECORD, the other three Sample buttons the HOOKS placeholder; Shift + Rec is RECORD's F1 from every
    // screen, with no change of screen (the plain Rec stays the home map's step record).
    if (in.press(Button::SampleRec)) {
      if (ctx.screen() != &record_) {
        ctx.show(record_);
      }
      return true;
    }
    if (in.press(Button::SampleLoad) || in.press(Button::SamplePlay) || in.press(Button::SampleEdit)) {
      if (ctx.screen() != &hooks_) {
        ctx.show(hooks_);
      }
      return true;
    }
    if (in.press(Button::Rec) && in.shift) {
      record_.shift_rec(ctx);
      return true;
    }
    // FILE (UX-303 2.1): Shift + File is SAVE from every screen (live.o3a and project.o3a, "SAVED", 8.5); File opens FILE PROJECTS, again FILE LIBRARY, again PROJECTS.
    if (in.press(Button::File)) {
      if (in.shift) {
        project_.save();
        save_asked_ = true;  // the press always gets an answer (update()): SAVING then SAVED, or at once why not
        notice_at_press_ = rig_.project.notice_seq;
      } else if (ctx.screen() == &file_) {
        file_.close_keyboard();  // File while the name is being typed drops it, as Cancel does
        ctx.show(vault_);
      } else {
        ctx.show(file_);  // from the LIBRARY and from every other screen
      }
      ctx.redraw();
      return true;
    }
    return false;
  }

  void update(app::Context& ctx) noexcept override {
    sequencer_.update();
    master_.service(ctx);  // a tempo restore that the command ring refused
    live_.update(ctx);  // the live actions and the snapshots' morph go on whatever screen is up
    record_.settle(ctx);   // a WAV job that ended: its result is read before the project's next job can replace it in the files service
    project_.update(ctx);  // the project on the card: the boot load, autosave, SAVE (after the actions, so a morph's last value is the one it sees)
    // The audio take: its chip and messages, its end, its WAV, whatever screen is up. AFTER the project service on purpose: both use the one files service and a job reads its result
    // when the card is free again, so the project settles (and issues) its own first, and the WAV is started only on a pass in which no job runs.
    record_.service(ctx);
    // A SAVE (Shift + File) says SAVING from the press, also while it waits for an autosave that is on, until both files are written; then SAVED, or why not. A
    // press the service refuses within the same pass (no card, a card that is not Ready) never shows SAVING: its answer is the reason, at once. The screens with
    // the home map (HOME, INSTRUMENT, MASTER, PERFORM) show the toast; FILE and the LIBRARY say it on their own lines.
    const bool saving = project_.saving_explicit();
    if (saving && ctx.uptime_ms() >= saving_toast_ms_) {
      main_.toast(ctx, "SAVING");
      saving_toast_ms_ = ctx.uptime_ms() + kSavingToastMs;
    } else if (save_asked_ && !saving) {
      const bool said = rig_.project.notice_seq != notice_at_press_;  // never a notice left over from before the press
      const char* text = said ? rig_.project.notice : "NOT SAVED";
      main_.toast(ctx, text, !said || std::strcmp(text, "SAVED") != 0);
      saving_toast_ms_ = 0;
      save_asked_ = false;
    }
    saving_ = saving;
    // The 303 screen's bookkeeping (the follow of the playing pattern) goes on while another screen is up; when it is the current screen the
    // runtime calls its update() itself.
    if (ctx.screen() != nullptr && ctx.screen() != &main_) {
      main_.update(ctx);
    }
  }

  // A firmware update waits for a take that is recording, armed or being written to the card.
  const char* update_blocker(const app::Context& ctx) const noexcept override { return record_.blocker(ctx); }

  o3_303::seq::Sequencer& sequencer() noexcept { return sequencer_; }
  o3_303::ProjectService& project() noexcept { return project_; }

 private:
  o3_303::AcidVoice voice_;
  o3_303::Distortion dist_;
  units::Delay delay_{g_delay_l, g_delay_r, kDelayCapacity};
  o3_303::Freeze freeze_{g_freeze_l.v, g_freeze_r.v, kFreezeCapacity};
  o3_303::Growl growl_;
  o3_303::seq::Sequencer sequencer_{o3_303::seq::project_bank()};
  o3_303::Rig rig_;
  o3_303::LiveActions live_;
  o3_303::MainScreen main_;
  o3_303::PerformScreen perform_;
  o3_303::InstrumentScreen instrument_;
  o3_303::MasterScreen master_;
  o3_303::RecordScreen record_;
  o3_303::HooksScreen hooks_;
  o3_303::VaultScreen vault_;
  o3_303::FileScreen file_;
  o3_303::ProjectService project_;
  bool saving_ = false;                // a SAVE was on at the last pass
  bool save_asked_ = false;            // Shift + File was pressed and its answer (the toast) is still due
  std::uint32_t notice_at_press_ = 0;  // Rig::project.notice_seq at that press
  std::uint64_t saving_toast_ms_ = 0;  // when SAVING may be said again
};

}  // namespace

o3sdk::app::App& o3sdk_app() noexcept {
  static O3_303App app;
  return app;
}

namespace o3_303 {

seq::Sequencer& app_sequencer() noexcept { return static_cast<O3_303App&>(o3sdk_app()).sequencer(); }

ProjectService& app_project() noexcept { return static_cast<O3_303App&>(o3sdk_app()).project(); }

}  // namespace o3_303
