// SPDX-License-Identifier: GPL-3.0-or-later
//
// O3-303: the TB-303 app of docs/DESIGN-303.md. The engine: the tb303 voice on slot 0 with the distortion as its insert, o3sdk's delay
// on send A, the sequencer (o3_303_seq) as a clocked hook. The screens are in main_screen.cpp (the 303 screen, with the pattern grid
// it shows while Pattern is held) and vault_screen.cpp (File: GEN and the vault). This file wires them and owns the global keys:
//
//   PLAY          play / stop at the press (Shift: tap tempo)
//   PATTERN       the 303 screen (on it: a tap flips the step view and the piano roll; held: the 48 pads are the patterns)
//   FILE          the GEN vault (on it: load the slot shown)
//   CONFIG        the next theme
//
// Everything else belongs to the screen that is up.

#include "main_screen.h"
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
#include <o3_303/units/tb303/tb303.h>

#include <cstdint>

namespace {

using namespace o3sdk;
using app::Button;

constexpr std::uint32_t kDelayCapacity = 88'200;  // 2 s at 44.1 kHz, each side
O3SDK_BIG float g_delay_l[kDelayCapacity];
O3SDK_BIG float g_delay_r[kDelayCapacity];

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
    rig_.bank = &o3_303::seq::project_bank();
    rig_.bank->init();
    rig_.seq = &sequencer_;

    const dsp::UnitId voice = e.attach(voice_);
    const dsp::UnitId dist = e.attach(dist_);
    const dsp::UnitId delay = e.attach(delay_);
    (void)e.set_source(0, voice);
    (void)e.set_insert(0, 0, dist);
    (void)e.set_send_effect(0, delay);
    (void)sequencer_.setup(e, voice);  // adds its hook to the engine
    // A run starts with no performance flag, no live transpose and no chain (the app object outlives a run in the simulator's tests).
    sequencer_.set_perf(0);
    sequencer_.set_live_transpose(0);
    (void)sequencer_.clear_queue();
    (void)sequencer_.set_chain_loop(false);

    rig_.wiring.voice = voice;
    rig_.wiring.dist = dist;
    rig_.wiring.delay = delay;
    rig_.wiring.bank = rig_.bank;
    rig_.controls.init(rig_.wiring);

    // The voice's own volume stays at 0 dB; SOUND's VOLUME is slot 0's level, after the distortion.
    (void)e.set_param(voice, o3_303::Tb303::Volume, 0.0f);
    rig_.controls.apply_level(e);
    rig_.controls.apply_send(e);
    (void)e.set_tempo(kDefaultTempoMilli);

    // First boot: the vault's factory line (bank 0, slot 0), pattern and sound, in pattern 0, so Play sounds at once.
    o3_303::seq::gen::Preset first;
    o3_303::seq::gen::factory(first, 0, 0);
    o3_303::load_preset(e, rig_, first, o3_303::LoadWhat::Both);
    rig_.bank->clear_history();  // the boot load is no edit of the user's: the first UNDO must not empty A01

    main_.init(rig_);
    vault_.init(rig_, main_);
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
      ctx.show(main_);  // back from the vault: the press is taken, the screen's own release does nothing
      return true;
    }
    if (in.press(Button::File) && ctx.screen() == &main_) {
      ctx.show(vault_);
      return true;
    }
    if (in.press(Button::Config)) {
      ctx.set_theme(ui::next_theme(ctx.theme()));
      ctx.redraw();
      return true;
    }
    return false;
  }

  void update(app::Context& ctx) noexcept override {
    sequencer_.update();
    // The 303 screen's bookkeeping (the live transpose that ends with the run, the follow of the playing pattern) goes on while another
    // screen is up; when it is the current screen the runtime calls its update() itself.
    if (ctx.screen() != nullptr && ctx.screen() != &main_) {
      main_.update(ctx);
    }
  }

  o3_303::seq::Sequencer& sequencer() noexcept { return sequencer_; }

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

  o3_303::Tb303 voice_;
  o3_303::Distortion dist_;
  units::Delay delay_{g_delay_l, g_delay_r, kDelayCapacity};
  o3_303::seq::Sequencer sequencer_{o3_303::seq::project_bank()};
  o3_303::Rig rig_;
  o3_303::MainScreen main_;
  o3_303::VaultScreen vault_;
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

}  // namespace o3_303
