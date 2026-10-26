// SPDX-License-Identifier: GPL-3.0-or-later
//
// O3-303: the TB-303 app of docs/DESIGN-303.md. The engine: the one tb303 voice on slot 0 with the distortion as its insert, o3sdk's delay
// on send A, the beat freeze on master insert 0 (off), the sequencer (o3_303_seq) as a clocked hook. The screens are in main_screen.cpp
// (the 303 screen, with the pattern grid it shows while Pattern is held) and vault_screen.cpp (File: GEN and the vault). This file wires
// them and owns the global keys:
//
//   PLAY          play / stop at the press (Shift: tap tempo)
//   PATTERN       the 303 screen (on it: a tap flips the step view and the piano roll; held: the 48 pads are the patterns)
//   FILE          the GEN vault (on it: load the slot shown)
//   CONFIG        the next theme
//   SONG          provisional (M3b R6, until the Fx performance button's FREEZE action exists): hold = the beat freeze on, Shift + tap = its
//                 next Length (1/4, 1/8, 1/16 of a bar). The ui slice removes it when the action lands.
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
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/tb303/tb303.h>

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

#if defined(O3_303_TEST_HOOKS)
// The M3b demo line for the simulator's scenario (app/scenarios/m3b_seq.txt): ratchets x2, x3, x4 (one accented), a slide into a ratcheted
// step and a ratcheted slide step, a condition of each kind (1:2, 50 %, FILL, NOT FILL, FIRST, NOT FIRST), over a C minor line.
void load_m3b_demo(o3_303::seq::Bank& bank, unsigned index) noexcept {
  using namespace o3_303;
  static constexpr std::uint8_t kNotes[16] = {36, 36, 39, 43, 36, 41, 43, 46, 36, 36, 39, 43, 48, 46, 43, 41};
  Pattern p;
  clear(p);
  for (unsigned j = 0; j < 16; ++j) {
    p.steps[j].note = kNotes[j];
    p.steps[j].flags = 0;
  }
  p.steps[0].flags = kStepAccent;
  p.steps[2].flags = with_ratchet(0, 1);                                                       // x2
  p.steps[3].cond = cond_ratio(1, 2);                                                          // every other pass
  p.steps[5].flags = kStepSlide;                                                               // glides into the ratcheted step 6
  p.steps[6].flags = with_ratchet(0, 2);                                                       // x3
  p.steps[7].cond = cond_probability(50);
  p.steps[8].flags = kStepAccent;
  p.steps[9].flags = kStepRest;
  p.steps[10].flags = static_cast<std::uint8_t>(with_ratchet(0, 3) | kStepAccent);             // x4, accented
  p.steps[11].cond = kCondFill;                                                                // only while FILL is held
  p.steps[12].cond = kCondNotFill;                                                             // only while it is not
  p.steps[13].flags = static_cast<std::uint8_t>(with_ratchet(0, 1) | kStepSlide);              // x2, its last hit held into step 14
  p.steps[14].cond = kCondFirst;                                                               // the first pass only
  p.steps[15].cond = kCondNotFirst;                                                            // every other pass
  p.length = 16;
  bank.replace(index, p);
}
#endif

class O3_303App final : public app::App {
 public:
  const app::AppInfo& info() const noexcept override {
    static constexpr app::AppInfo kInfo{"O3-303", "0.0.1"};
    return kInfo;
  }

  void setup(app::Context& ctx) noexcept override {
    dsp::Engine& e = ctx.engine();
    tap_count_ = 0;
    freeze_held_ = false;
    rig_.wiring = o3_303::Wiring{};
    rig_.bank = &o3_303::seq::project_bank();
    rig_.bank->init();
    rig_.seq = &sequencer_;

    const dsp::UnitId voice = e.attach(voice_);
    const dsp::UnitId dist = e.attach(dist_);
    const dsp::UnitId delay = e.attach(delay_);
    const dsp::UnitId freeze = e.attach(freeze_);  // after the others: the units' ids (voice 0, distortion 1, delay 2) are the tests' contract
    (void)e.set_source(0, voice);
    (void)e.set_insert(0, 0, dist);
    (void)e.set_send_effect(0, delay);
    (void)e.set_master_insert(0, freeze);
    (void)sequencer_.setup(e, voice, dist, delay);  // adds its hook to the engine; the lanes may drive the voice, the distortion, the delay
    // A run starts with no performance flag, no live transpose and no chain (the app object outlives a run in the simulator's tests).
    sequencer_.set_perf(0);
    sequencer_.set_fill_variant(o3_303::seq::FillVariant::Mutate);
    sequencer_.set_live_transpose(0);
    (void)sequencer_.clear_queue();
    (void)sequencer_.set_chain_loop(false);

    rig_.wiring.voice = voice;
    rig_.wiring.dist = dist;
    rig_.wiring.delay = delay;
    rig_.wiring.freeze = freeze;
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
#if defined(O3_303_TEST_HOOKS)
    // Host builds only (app/CMakeLists.txt): the scenarios have no control for the ratchet, the condition or FILL until the SEQ page has them.
    //   SampleLoad          the M3b demo line replaces the selected pattern
    //   SamplePlay          held: FILL
    //   Shift + SampleEdit  the next FILL variant (MUTATE, OCT UP, RATCHET, ACC ROTATE)
    if (in.press(Button::SampleLoad)) {
      load_m3b_demo(*rig_.bank, rig_.bank->selected());
      ctx.redraw();
      return true;
    }
    if (in.press(Button::SamplePlay)) {
      sequencer_.set_fill(true);
      return true;
    }
    if (in.release(Button::SamplePlay)) {
      sequencer_.set_fill(false);
      return true;
    }
    if (in.press(Button::SampleEdit) && in.shift) {
      sequencer_.set_fill_variant(static_cast<o3_303::seq::FillVariant>(
          (static_cast<unsigned>(sequencer_.fill_variant()) + 1u) % o3_303::seq::kFillVariantCount));
      return true;
    }
#endif
    if (in.press(Button::Config)) {
      ctx.set_theme(ui::next_theme(ctx.theme()));
      ctx.redraw();
      return true;
    }
    // Provisional (M3b R6): the beat freeze while Song is held, Shift + Song for its next Length. See the header comment.
    if (in.press(Button::Song)) {
      if (in.shift) {
        const o3_303::Freeze::LengthValue now = o3_303::freeze_length(e, rig_);
        o3_303::set_freeze_length(e, rig_, static_cast<o3_303::Freeze::LengthValue>((now + 1u) % o3_303::Freeze::kLengthCount));
      } else {
        freeze_held_ = true;
        o3_303::set_freeze(e, rig_, true);
      }
      return true;
    }
    if (in.release(Button::Song)) {
      if (freeze_held_) {
        freeze_held_ = false;
        o3_303::set_freeze(e, rig_, false);
      }
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
  o3_303::Freeze freeze_{g_freeze_l.v, g_freeze_r.v, kFreezeCapacity};
  o3_303::seq::Sequencer sequencer_{o3_303::seq::project_bank()};
  o3_303::Rig rig_;
  o3_303::MainScreen main_;
  o3_303::VaultScreen vault_;
  std::uint64_t tap_ms_[kTaps] = {};
  unsigned tap_count_ = 0;
  bool freeze_held_ = false;  // the provisional Song hold has the freeze on
};

}  // namespace

o3sdk::app::App& o3sdk_app() noexcept {
  static O3_303App app;
  return app;
}

namespace o3_303 {

seq::Sequencer& app_sequencer() noexcept { return static_cast<O3_303App&>(o3sdk_app()).sequencer(); }

}  // namespace o3_303
