// SPDX-License-Identifier: GPL-3.0-or-later
//
// O3-303: the TB-303 app of docs/DESIGN-303.md. The engine: the one acid_voice voice on slot 0 with the distortion as its insert, o3sdk's delay
// on send A, the beat freeze on master insert 0 (off), the growl on slot 0's insert 1 (Mix 0: a bit-exact bypass), the sequencer (o3_303_seq) as a clocked hook. The screens are in main_screen.cpp
// (the 303 screen, with the pattern grid it shows while Pattern is held) and vault_screen.cpp (File: GEN and the vault). This file wires
// them and owns the global keys:
//
//   PLAY          play / stop at the press (Shift: tap tempo)
//   PATTERN       the 303 screen (on it: a tap flips the step view and the piano roll; held: the 48 pads are the patterns)
//   PERFORM       PERFORM, the 48 pads live (on it: PERFORM SETUP, and back); the live actions are LiveActions', shared with the 303 screen
//   FILE          the GEN vault (on it: load the slot shown); Shift + FILE: SAVE the project (project_service.h)
//   CONFIG        o3sdk's system Config (FIRMWARE, MIDI, DISPLAY with the theme, INFO); the app never sees the key
//
// Everything else belongs to the screen that is up (the performance buttons' FREEZE, FILL and the rest are the 303 screen's, C4).

#include "live_actions.h"
#include "main_screen.h"
#include "perform_screen.h"
#include "project_service.h"
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
constexpr std::uint64_t kTapResetMs = 2000;            // a longer gap between two taps starts a new measure
constexpr unsigned kTaps = 4;

class O3_303App final : public app::App {
 public:
  const app::AppInfo& info() const noexcept override {
    static constexpr app::AppInfo kInfo{"O3-303", "0.0.1"};
    return kInfo;
  }

  void setup(app::Context& ctx) noexcept override {
    dsp::Engine& e = ctx.engine();
    tap_count_ = 0;
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
    vault_.init(rig_, main_);
    project_.init(rig_, e);  // last: it records the session as setup leaves it, so that a project loaded from the card knows if the player has edited
    ctx.show(main_);
  }

  bool input(app::Context& ctx, const app::Input& in) noexcept override {
    dsp::Engine& e = ctx.engine();
    if (in.press(Button::Play)) {
      if (in.shift) {
        tap_tempo(ctx);
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
    // SAVE everywhere but on the vault, where Shift + File still loads the shifted slot (until F3's FILE screen takes the key over).
    if (in.press(Button::File) && in.shift && ctx.screen() != &vault_) {  // (UX-303 8.5): live.o3a and project.o3a, "SAVED"
      project_.save();
      ctx.redraw();
      return true;
    }
    if (in.press(Button::File) && ctx.screen() == &main_) {
      ctx.show(vault_);
      return true;
    }
    return false;
  }

  void update(app::Context& ctx) noexcept override {
    sequencer_.update();
    live_.update(ctx);  // the live actions and the snapshots' morph go on whatever screen is up
    project_.update(ctx);  // the project on the card: the boot load, autosave, SAVE (after the actions, so a morph's last value is the one it sees)
    // The 303 screen's bookkeeping (the follow of the playing pattern) goes on while another screen is up; when it is the current screen the
    // runtime calls its update() itself.
    if (ctx.screen() != nullptr && ctx.screen() != &main_) {
      main_.update(ctx);
    }
  }

  o3_303::seq::Sequencer& sequencer() noexcept { return sequencer_; }
  o3_303::ProjectService& project() noexcept { return project_; }

 private:
  // Shift + Play: the tempo from the gaps between the last taps (the last four), 20..300 BPM.
  void tap_tempo(app::Context& ctx) noexcept {
    const std::uint64_t now = ctx.uptime_ms();
    if (tap_count_ != 0u && now - tap_ms_[(tap_count_ - 1u) % kTaps] > kTapResetMs) {
      tap_count_ = 0;
    }
    tap_ms_[tap_count_ % kTaps] = now;
    ++tap_count_;
    if (tap_count_ < 2u) {
      return;
    }
    const unsigned n = tap_count_ < kTaps ? tap_count_ : kTaps;
    const std::uint64_t first = tap_ms_[(tap_count_ - n) % kTaps];
    const std::uint64_t span = now - first;
    if (span == 0u) {
      return;
    }
    // n taps are n - 1 gaps: a gap is span / (n - 1) ms, a tempo 60000 / gap BPM, in milli-BPM.
    std::uint64_t mbpm = 60'000'000ull * (n - 1u) / span;
    mbpm = mbpm < dsp::kMinTempoMilliBpm ? dsp::kMinTempoMilliBpm : (mbpm > dsp::kMaxTempoMilliBpm ? dsp::kMaxTempoMilliBpm : mbpm);
    (void)ctx.engine().set_tempo(static_cast<std::uint32_t>(mbpm));
  }

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
  o3_303::VaultScreen vault_;
  o3_303::ProjectService project_;
  std::uint64_t tap_ms_[kTaps] = {};
  unsigned tap_count_ = 0;
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
