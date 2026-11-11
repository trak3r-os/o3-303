// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// TRACT: a vocal tract on the 303's line, an effect for slot 0's insert 2 after the growl (docs/TRACT-KNOBS-PLAN.md, section 3). A wrapper of
// the shared DSP in o3-sdk/extras/tract (o3sdk::extras::tract::Processor; the DSP is described in its README.md). Stereo in, stereo out, in
// place or not.
//
// What it does. The wet signal is the mono sum of the input, shaped by the tube and written to both channels; the dry signal stays stereo
// (as the growl: a mono input, as the 303's voice writes it, goes through the wet path exactly as the mono sum; a stereo one keeps its image
// in the dry part only). Mix is a 5 ms linear ramp between the two, landing on its target. The processor itself always runs fully wet.
//
// Mix 0 is the default: a unit that is wired and untouched passes the input bit for bit (no arithmetic touches the samples, in place or
// not) and costs a memcpy at most. The processor is not called while the unit is idle and is reset when Mix leaves 0, so Mix going up starts
// from silence in the tube, at the vowel and the shape that were asked for.
//
// Parameters (the id is the index in the table: append, never insert):
//   0 VOWEL    0..1 (shown as %), default 0: A at 0, E at 0.25, I at 0.5, O at 0.75, U at 1, morphing in between
//   1 TONGUE   -100..+100 (%), default 0: back .. front. Stored in percent: the SDK's display units have no signed percent of a -1..+1 range
//   2 LIPS     -100..+100 (%), default 0: round .. spread. Stored in percent for the same reason
//   3 RES      0..1 (shown as %), default 0.5: from a soft mouth to a ringing one
//   4 GLIDE    5..500 ms (log), default 30: how the mouth slides from one shape to the next
//   5 SIZE     BIG LARGE NORMAL SMALL TINY, default NORMAL: the tube's length; a switch (a 5 ms dip of the wet gain), never a glide
//   6 MIX      0..1 (shown as %), default 0
//
// Memory: the processor's state (under 1.5 KB) and one block of the mono sum, inside the unit; no SDRAM.
// Cost [est]: the processor's (about 256 cycles a frame at Quality::Full, 220 at Lite: extras/tract/README.md, not measured on a unit) plus
// the wrapper's mono sum and mix, about 8 cycles a frame. Idle: a memcpy of the block when the buffers differ, nothing in place.

#include <o3sdk/dsp/engine.h>
#include <o3sdk/dsp/unit.h>
#include <o3sdk/extras/tract/processor.h>

#include <cstdint>

namespace o3_303 {

class TractFx final : public o3sdk::dsp::Unit {
 public:
  enum Param : std::uint16_t { Vowel, Tongue, Lips, Res, Glide, Size, Mix, kParamCount };

  // The quality of the processor: Full (22 sections at fs/2) or Lite (11 at fs/4). One line to change after the unit's CPU read (the 56 % rule).
  static constexpr o3sdk::extras::tract::Processor::Quality kQuality = o3sdk::extras::tract::Processor::Quality::Full;

  TractFx() noexcept;

  const o3sdk::dsp::UnitInfo& info() const noexcept override;
  void prepare(float sample_rate) noexcept override;
  void process(const o3sdk::dsp::ProcessArgs& args) noexcept override;

  // For the tests and the diagnostics (render side): Mix is 0, settled, and the input passes untouched.
  bool idle() const noexcept { return mix_ == 0.0f && mix_target_ == 0.0f; }

 private:
  void set(std::uint16_t id, float v) noexcept;
  void run(const o3sdk::dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept;

  o3sdk::extras::tract::Processor proc_{kQuality};
  alignas(16) float mono_[o3sdk::dsp::kMaxBlockFrames] = {};   // the mono sum of a run, then the wet signal (the processor runs in place)
  float tongue_ = 0.0f;    // -1..+1, the processor's bend
  float lips_ = 0.0f;
  float mix_ = 0.0f;       // the ramp's value now; main-side targets are events, so every field here is the render context's
  float mix_target_ = 0.0f;
  float mix_step_ = 0.0f;  // per frame
};

static_assert(alignof(TractFx) >= 16, "the mono block is declared aligned (alignas(16) in the class)");
static_assert(sizeof(TractFx) < 2048, "the unit is the processor's 1.5 KB and one 256-byte block");

}  // namespace o3_303
