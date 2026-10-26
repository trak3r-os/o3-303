// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/units/freeze/freeze.h>

#include <o3sdk/dsp/engine.h>
#include <o3sdk/dsp/transport.h>

#include <cmath>
#include <cstring>

// Cost [est], not measured on a unit (the unit's header shows the render's CPU %; the measurement is `diag cpu` with the freeze held and
// a pattern playing). A block is 64 frames, 261,224 cycles. The ring is in the SDRAM, which the render reaches uncached
// (track3r-os docs/architecture/cpu-budget.md: 128 halfword stores about 1,500 cycles, 12 a store); taken here as 12 cycles for a word
// store and 20 for a word load (two beats of the 16 bit bus) against the 2 an SRAM access costs. The instruction counts are from the cross
// build's disassembly (arm-gnu 15.3, -O2):
//   Idle, the all-the-time case: two memcpy of the block into the ring, 128 word stores = 1.5k cycles, plus the call and the split of the
//     ring's wrap, about 1.6k a block = 0.6 %. The pass-through is free (in place).
//   Hold, steady: 25 instructions a frame in the loop (two SDRAM loads, two SDRAM stores, two input loads and two output stores in the
//     SRAM, seven FP operations): about 36 cycles of core work plus 2 x 18 + 2 x 10 for the SDRAM = 92 a frame, 5.9k a block = 2.3 %.
//   Entry: the ramp (221 frames, 4 blocks) is the same loop with four more instructions, 2.4 %. The baking of the loop's tail (32 frames a
//     block, over the first 8 blocks) costs, a frame, four SDRAM loads, two stores and about 30 instructions = 145 cycles: 4.6k = 1.8 %.
//     The worst block is an entry block: about 11k = 4.2 % above the same block without the freeze.
//   Exit (221 frames, 4 blocks): the loop plus a second crossfade, 2.5 %. After the shortest freeze (5 ms in) the baking is not over, and
//     it goes on at its own pace through the exit (an exit does not finish it in one block, as a first version did: about 13k cycles on
//     top of the exit's 6.4k); the block where the two overlap is 6.4k + 4.6k = 11k, 4.2 % again, the same worst block.
// Against the 39.8 % of a block the unit measured with one voice, the distortion and the delay: about 40.5 % with the freeze off
// (recording), 42.1 % held, 44 % in the worst block, all under the governor's 60 %.
namespace o3_303 {

namespace {

namespace dsp = o3sdk::dsp;
using o3sdk::core::ParamCurve;
using o3sdk::core::ParamUnit;

constexpr const char* kActiveNames[] = {"OFF", "ON"};
constexpr const char* kLengthNames[] = {"1/4", "1/8", "1/16"};
constexpr const char* kModeNames[] = {"FREEZE", "ROLL", "STOP", "REVERSE"};
constexpr const char* kRollNames[] = {"1/8", "1/16", "1/32", "1/64"};
constexpr const char* kAccelNames[] = {"OFF", "BEAT", "BAR"};
constexpr const char* kStopNames[] = {"1/8", "1/4", "1/2", "1", "2"};
constexpr std::uint8_t kSmooth = o3sdk::core::kParamSmooth;

// name, unit text, names, knots, min, max, default, detents, coarse, list count, display, curve, flags, page
constexpr o3sdk::core::ParamDesc kParams[Freeze::kParamCount] = {
    {"ACTIVE", "", kActiveNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"LENGTH", "", kLengthNames, nullptr, 0.0f, 2.0f, 1.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"MIX", "%", nullptr, nullptr, 0.0f, 1.0f, 1.0f, 100, 10, 0, ParamUnit::NormPct, ParamCurve::Linear, kSmooth, 0},
    {"MODE", "", kModeNames, nullptr, 0.0f, 3.0f, 0.0f, 3, 1, 4, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"ROLL RATE", "", kRollNames, nullptr, 0.0f, 3.0f, 1.0f, 3, 1, 4, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"ACCELERATE", "", kAccelNames, nullptr, 0.0f, 2.0f, 0.0f, 2, 1, 3, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"STOP TIME", "bar", kStopNames, nullptr, 0.0f, 4.0f, 2.0f, 4, 1, 5, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
};

constexpr dsp::UnitInfo kInfo{"FREEZE", dsp::UnitKind::Effect, kParams, Freeze::kParamCount, nullptr, 0};

constexpr float kSixteenths[Freeze::kLengthCount] = {4.0f, 2.0f, 1.0f};  // 1/4, 1/8, 1/16 of a bar
constexpr float kRollSixteenths[Freeze::kRollRateCount] = {2.0f, 1.0f, 0.5f, 0.25f};          // 1/8, 1/16, 1/32, 1/64 note
constexpr float kStopSixteenths[Freeze::kStopTimeCount] = {2.0f, 4.0f, 8.0f, 16.0f, 32.0f};  // 1/8, 1/4, 1/2, 1, 2 bars
constexpr float kFadeSeconds = 0.005f;                                     // the crossfades
constexpr float kMixSeconds = 0.005f;                                      // the Mix smoother

// The tail of the loop is baked in the first blocks after the entry (half a frame a frame), and the ring still holds what the baking reads
// until the write head has gone round: the baking must be over before the first loop reaches the tail (kMinLoop - kMaxFade frames after the
// entry) and before the write head overwrites what it reads (kHeadroom - kMaxFade frames after the entry). Its longest span is the
// fade at half a frame a frame, plus a block that is already running at the entry and one that is yet to start.
constexpr std::uint32_t kBakeSpan = 2u * Freeze::kMaxFade + 2u * dsp::kMaxBlockFrames;
static_assert(Freeze::kMinLoop - Freeze::kMaxFade > kBakeSpan, "the baking must end before the first loop reaches its tail");
static_assert(Freeze::kHeadroom - Freeze::kMaxFade > kBakeSpan, "the baking must end before the write head reaches what it reads");
static_assert(Freeze::kMinCapacity >= Freeze::kMinLoop + Freeze::kHeadroom, "a ring holds at least the shortest loop and the headroom");
// The same for the shortest ROLL loop, whose fade is a quarter of it: the baking (twice the fade and two blocks) ends before the loop point.
static_assert(Freeze::kMinRoll - Freeze::kMinRoll / 4u > 2u * (Freeze::kMinRoll / 4u) + 2u * dsp::kMaxBlockFrames,
              "the baking of the shortest roll must end before the first loop reaches its tail");
static_assert(Freeze::kMinRoll > Freeze::kMaxFade && Freeze::kMinSpin > 4u * Freeze::kMaxFade, "the roll and the spin-up are longer than their fades");

// An Enum value (the descriptor's index) as a table index: rounded, clamped (a NaN is 0).
std::uint8_t enum_index(float v, unsigned count) noexcept {
  if (!(v > 0.0f)) {
    return 0;
  }
  const float n = v + 0.5f;
  return n >= static_cast<float>(count) ? static_cast<std::uint8_t>(count - 1u) : static_cast<std::uint8_t>(n);
}

constexpr std::uint32_t floor_pow2(std::uint32_t n) noexcept {
  std::uint32_t p = 1;
  while (p <= n / 2u) {
    p <<= 1;
  }
  return n == 0u ? 0u : p;
}

}  // namespace

Freeze::Freeze(float* left, float* right, std::uint32_t capacity) noexcept
    : l_(left),
      r_(right),
      mask_(floor_pow2(capacity) != 0u ? floor_pow2(capacity) - 1u : 0u),
      usable_(left != nullptr && right != nullptr && floor_pow2(capacity) >= kMinCapacity) {}

const dsp::UnitInfo& Freeze::info() const noexcept { return kInfo; }

std::uint32_t Freeze::loop_frames(unsigned length, std::uint32_t tempo_mbpm, std::uint32_t capacity) noexcept {
  const unsigned i = length < kLengthCount ? length : kLengthCount - 1u;
  const std::uint32_t longest = capacity > kHeadroom + kMinLoop ? capacity - kHeadroom : kMinLoop;
  const float exact = kSixteenths[i] * dsp::samples_per_sixteenth(tempo_mbpm) + 0.5f;
  if (!(exact < static_cast<float>(longest))) {  // also a NaN
    return longest;
  }
  const auto n = static_cast<std::uint32_t>(exact);
  return n < kMinLoop ? kMinLoop : n;
}

std::uint32_t Freeze::fade_frames(float sample_rate, std::uint32_t loop) noexcept {
  const float f = kFadeSeconds * sample_rate + 0.5f;
  std::uint32_t n = f >= static_cast<float>(kMaxFade) ? kMaxFade : (f > 1.0f ? static_cast<std::uint32_t>(f) : 1u);
  if (n > loop / 4u) {
    n = loop / 4u;
  }
  return n != 0u ? n : 1u;
}

std::uint32_t Freeze::roll_frames(unsigned rate, std::uint32_t tempo_mbpm, std::uint32_t capacity) noexcept {
  const unsigned i = rate < kRollRateCount ? rate : kRollRateCount - 1u;
  const std::uint32_t longest = capacity > kHeadroom + kMinRoll ? capacity - kHeadroom : kMinRoll;
  const float exact = kRollSixteenths[i] * dsp::samples_per_sixteenth(tempo_mbpm) + 0.5f;
  if (!(exact < static_cast<float>(longest))) {  // also a NaN
    return longest;
  }
  const auto n = static_cast<std::uint32_t>(exact);
  return n < kMinRoll ? kMinRoll : n;
}

std::uint32_t Freeze::accel_frames(unsigned accel, std::uint32_t tempo_mbpm) noexcept {
  if (accel != kAccelBeat && accel != kAccelBar) {
    return 0;
  }
  const float exact = (accel == kAccelBeat ? 4.0f : 16.0f) * dsp::samples_per_sixteenth(tempo_mbpm) + 0.5f;
  return exact < 1.0e7f ? static_cast<std::uint32_t>(exact) : 10'000'000u;
}

std::uint32_t Freeze::stop_frames(unsigned time, std::uint32_t tempo_mbpm, std::uint32_t capacity) noexcept {
  const unsigned i = time < kStopTimeCount ? time : kStopTimeCount - 1u;
  const std::uint32_t room = capacity > kHeadroom ? capacity - kHeadroom : 0u;
  const float exact = kStopSixteenths[i] * dsp::samples_per_sixteenth(tempo_mbpm) + 0.5f;
  std::uint32_t n = exact < 1.0e7f ? static_cast<std::uint32_t>(exact) : 10'000'000u;
  if (n > 2u * room) {
    n = 2u * room;
  }
  return n < kMinSpin ? kMinSpin : n;
}

std::uint32_t Freeze::reverse_span(std::uint32_t capacity) noexcept { return capacity > kHeadroom ? (capacity - kHeadroom) / 2u : 0u; }

void Freeze::prepare(float sample_rate) noexcept {
  fs_ = sample_rate > 1000.0f ? sample_rate : 44100.0f;
  mix_k_ = 1.0f - std::exp(-1.0f / (kMixSeconds * fs_));
  if (usable_) {
    for (std::uint32_t i = 0; i <= mask_; ++i) {
      l_[i] = 0.0f;
      r_[i] = 0.0f;
    }
  }
  w_ = 0;
  phase_ = Phase::Idle;
  loop_ = 0;
  fade_ = 1;
  inv_fade_ = 1.0f;
  fp_ = 0;
  bake_origin_ = 0;
  bake_next_ = fade_;  // nothing to bake
  baked_ = 0;
  cur_mode_ = kModeFreeze;
  lag_ = 0;
  roll_cur_ = kRoll16;
  roll_want_ = kRoll16;
  roll_dirty_ = false;
  roll_restart_ = false;
  relatch_ = false;
  roll_pos_ = 0;
  roll_tempo_ = 120'000;
  accel_period_ = 0;
  accel_left_ = 0;
  xfade_ = 1;
  inv_xfade_ = 1.0f;
  xp_ = 0;
  sp_ = 1.0f;
  dec_ = 0.0f;
  inc_ = 0.0f;
  lf_ = 0.0f;
  li_ = 0;
  lag_max_ = 0;
  fading_ = false;
  rev_org2_ = 0;
  rev_k_ = 0;
  rev_span_ = 0;
  rev_forced_ = false;
  for (unsigned p = 0; p < kParamCount; ++p) {
    set(static_cast<std::uint16_t>(p), kParams[p].def);
  }
  mix_ = mix_target_;
}

void Freeze::set(std::uint16_t id, float v) noexcept {
  switch (id) {
    case Active:
      want_ = v >= 0.5f;
      break;
    case Length: {
      const float n = v + 0.5f;
      length_ = n >= 2.0f ? static_cast<std::uint8_t>(Sixteenth) : (n >= 1.0f ? static_cast<std::uint8_t>(Eighth) : static_cast<std::uint8_t>(Quarter));
      break;
    }
    case Mix:
      mix_target_ = v > 1.0f ? 1.0f : (v > 0.0f ? v : 0.0f);
      break;
    case Mode:
      mode_ = enum_index(v, kModeCount);
      break;
    case RollRate:
      roll_rate_ = enum_index(v, kRollRateCount);
      roll_dirty_ = true;  // a ROLL that is on takes it as the request at its next loop boundary
      break;
    case Accelerate:
      accel_ = enum_index(v, kAccelCount);
      accel_period_ = accel_frames(accel_, roll_tempo_);
      accel_left_ = accel_period_;
      break;
    case StopTime:
      stop_time_ = enum_index(v, kStopTimeCount);
      break;
    default:
      break;
  }
}

// ---- the freeze ---------------------------------------------------------------------------------------------------------------

void Freeze::enter(std::uint32_t tempo_mbpm) noexcept {
  cur_mode_ = mode_;
  if (cur_mode_ == kModeStop) {
    enter_stop(tempo_mbpm);
    return;
  }
  if (cur_mode_ == kModeReverse) {
    enter_reverse();
    return;
  }
  if (cur_mode_ == kModeRoll) {
    if (!roll_restart_) {
      roll_want_ = roll_rate_;  // a roll that starts over to go longer takes the request it left for
    }
    roll_restart_ = false;
    roll_dirty_ = false;
    roll_cur_ = roll_want_;
    roll_tempo_ = tempo_mbpm;
    roll_pos_ = 0;
    relatch_ = false;
    accel_period_ = accel_frames(accel_, tempo_mbpm);
    accel_left_ = accel_period_;
    loop_ = roll_frames(roll_cur_, tempo_mbpm, mask_ + 1u);
  } else {
    loop_ = loop_frames(length_, tempo_mbpm, mask_ + 1u);
  }
  lag_ = loop_;
  fade_ = fade_frames(fs_, loop_);
  inv_fade_ = 1.0f / static_cast<float>(fade_);
  fp_ = 0;
  mix_ = mix_target_;  // the share of the loop starts at 0, so a jump of Mix while idle cannot click
  bake_origin_ = w_;
  bake_next_ = 0;
  phase_ = Phase::Hold;
}

// The tail of the loop, [origin - fade, origin) in the ring, becomes a fade from the recorded tail to the material before the loop's
// start ([origin - loop - fade, origin - loop)): the sample before the loop's first one in the original is then the loop's last, so the
// loop point is a join of consecutive samples. Frame i of the tail takes a = (i + 1) / fade of the earlier material, 1 at the last.
void Freeze::bake(std::uint32_t count) noexcept {
  const std::uint32_t end = bake_next_ + count < fade_ ? bake_next_ + count : fade_;
  baked_ += end - bake_next_;
  for (std::uint32_t i = bake_next_; i < end; ++i) {
    const float a = (i + 1u == fade_) ? 1.0f : static_cast<float>(i + 1u) * inv_fade_;
    const std::uint32_t dst = (bake_origin_ - fade_ + i) & mask_;
    const std::uint32_t src = (bake_origin_ - loop_ - fade_ + i) & mask_;
    l_[dst] = l_[dst] * (1.0f - a) + l_[src] * a;
    r_[dst] = r_[dst] * (1.0f - a) + r_[src] * a;
  }
  bake_next_ = end;
}

// ---- the sample loops ---------------------------------------------------------------------------------------------------------

void Freeze::passthrough(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  if (to > from) {
    if (args.out_l != args.in_l) {
      std::memcpy(args.out_l + from, args.in_l + from, (to - from) * sizeof(float));
    }
    if (args.out_r != args.in_r) {
      std::memcpy(args.out_r + from, args.in_r + from, (to - from) * sizeof(float));
    }
  }
}

// Not frozen: the input into the ring (at most two contiguous pieces: the ring wraps), the output is the input.
void Freeze::idle(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  std::uint32_t w = w_;
  std::uint32_t done = from;
  while (done < to) {
    const std::uint32_t room = mask_ + 1u - w;
    const std::uint32_t n = to - done < room ? to - done : room;
    std::memcpy(l_ + w, args.in_l + done, n * sizeof(float));
    std::memcpy(r_ + w, args.in_r + done, n * sizeof(float));
    w = (w + n) & mask_;
    done += n;
  }
  w_ = w;
  passthrough(args, from, to);
}

// Frozen: the loop is the ring read `loop_` frames behind the write head, and the ring is rewritten with what was read, so the loop
// goes on. The output is the input and the loop crossfaded: the share of the loop is Mix times the fade position when kRamp (entry),
// Mix after it.
template <bool kRamp>
void Freeze::hold(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  float* const lr = l_;
  float* const rr = r_;
  const std::uint32_t mask = mask_;
  const std::uint32_t d = lag_;
  const float target = mix_target_;
  const float k = mix_k_;
  const float inv = inv_fade_;
  std::uint32_t w = w_;
  std::uint32_t fp = fp_;
  float m = mix_;
  for (std::uint32_t i = from; i < to; ++i) {
    const std::uint32_t rd = (w - d) & mask;
    const float pl = lr[rd];
    const float pr = rr[rd];
    lr[w] = pl;
    rr[w] = pr;
    m += k * (target - m);
    float c = m;
    if (kRamp) {
      c *= static_cast<float>(fp) * inv;
      ++fp;
    }
    const float il = args.in_l[i];
    const float ir = args.in_r[i];
    args.out_l[i] = il * (1.0f - c) + pl * c;
    args.out_r[i] = ir * (1.0f - c) + pr * c;
    w = (w + 1u) & mask;
  }
  w_ = w;
  fp_ = fp;
  mix_ = std::fabs(target - m) < 1e-4f ? target : m;
}

// The exit: the share of the loop falls from where it was (fp_ / fade_) to 0 in fp_ frames. The ring records the fade itself (the
// loop and the input crossfaded, without Mix), so that what follows the loop in the ring is continuous with it. The caller passes
// `to - from` <= fp_.
void Freeze::leave(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  float* const lr = l_;
  float* const rr = r_;
  const std::uint32_t mask = mask_;
  const std::uint32_t d = lag_;
  const float target = mix_target_;
  const float k = mix_k_;
  const float inv = inv_fade_;
  std::uint32_t w = w_;
  std::uint32_t fp = fp_;
  float m = mix_;
  for (std::uint32_t i = from; i < to; ++i) {
    const std::uint32_t rd = (w - d) & mask;
    const float pl = lr[rd];
    const float pr = rr[rd];
    const float g = fp >= fade_ ? 1.0f : static_cast<float>(fp) * inv;
    m += k * (target - m);
    const float c = m * g;
    const float il = args.in_l[i];
    const float ir = args.in_r[i];
    lr[w] = il * (1.0f - g) + pl * g;
    rr[w] = ir * (1.0f - g) + pr * g;
    args.out_l[i] = il * (1.0f - c) + pl * c;
    args.out_r[i] = ir * (1.0f - c) + pr * c;
    w = (w + 1u) & mask;
    --fp;
  }
  w_ = w;
  fp_ = fp;
  mix_ = std::fabs(target - m) < 1e-4f ? target : m;
}

// One run between two events: Active, Length and Mix do not change inside it, so the phase changes only where it says.
void Freeze::run(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  if (!usable_) {
    passthrough(args, from, to);
    return;
  }
  while (from < to) {
    if (phase_ == Phase::Idle) {
      // An entry waits for the previous loop's tail to be baked: the baking is paced across blocks and goes on through an exit, ending
      // within kBakeSpan frames of its entry, so after the shortest freeze (5 ms in, 5 ms out) the wait is a few frames. A new loop's own
      // baking and the loop itself read what the old baking is still replacing, and the baking has one state.
      if (!want_ || bake_next_ < fade_) {
        if (!want_) {
          roll_restart_ = false;  // a roll that left to start over, and was released meanwhile, starts from the Roll Rate next time
        }
        idle(args, from, to);
        return;
      }
      enter(args.transport->tempo_mbpm);
    } else if (phase_ == Phase::Hold && !want_ && fp_ >= fade_) {
      // The exit waits for the entry crossfade to finish (5 ms at most): the ring records the loop itself during the entry, and a fade
      // out that began before the loop had fully come in would leave a step in the ring, which a freeze soon after would loop. The baking
      // of the tail, if it is not over, goes on at its own pace through the exit (nothing reads the tail before kMinLoop - kMaxFade
      // frames after the entry, and the exit reads the loop's head), so an exit adds no work to its block.
      phase_ = Phase::Leave;
    }
    if (phase_ == Phase::Stop) {
      from = run_stop(args, from, to);
      continue;
    }
    if (phase_ == Phase::Reverse) {
      from = run_reverse(args, from, to);
      continue;
    }
    if (phase_ == Phase::Hold && cur_mode_ == kModeRoll) {
      from = run_roll(args, from, to);
      continue;
    }
    if (phase_ == Phase::Hold) {
      if (fp_ < fade_) {
        const std::uint32_t n = to - from < fade_ - fp_ ? to - from : fade_ - fp_;
        hold<true>(args, from, from + n);
        from += n;
      } else {
        hold<false>(args, from, to);
        from = to;
      }
    } else {  // Phase::Leave
      const std::uint32_t n = to - from < fp_ ? to - from : fp_;
      leave(args, from, from + n);
      from += n;
      if (fp_ == 0u) {
        phase_ = Phase::Idle;  // a want_ that came back during the exit enters again from here, on the next turn of the loop
      }
    }
  }
}


// ---- ROLL ---------------------------------------------------------------------------------------------------------------------

// A ROLL is a FREEZE whose loop is the Roll Rate; its entry is enter()'s and run()'s (the same crossfade, the same baking). What it adds
// is the change of rate at a loop boundary, and the Accelerate steps that ask for it.

// `frames` of the roll went by: they count toward the next Accelerate step. A step only moves the request; the loop changes at its boundary.
void Freeze::roll_tick(std::uint32_t frames) noexcept {
  if (accel_period_ == 0u) {
    return;
  }
  while (frames >= accel_left_) {
    frames -= accel_left_;
    accel_left_ = accel_period_;
    if (roll_want_ + 1u < kRollRateCount) {
      ++roll_want_;
    }
  }
  accel_left_ -= frames;
}

// A repeat is over (roll_pos_ == loop_). The head of the loop comes next, so a loop of the head's first n frames goes on without a join.
void Freeze::roll_boundary() noexcept {
  roll_pos_ = 0;
  if (relatch_) {
    relatch_ = false;
    lag_ = loop_;  // the short repeat is in the ring: from here the loop is read one repeat behind
  }
  if (roll_want_ == roll_cur_) {
    return;
  }
  const std::uint32_t len = roll_frames(roll_want_, roll_tempo_, mask_ + 1u);
  if (len == loop_) {  // both rates are clamped to the same length
    roll_cur_ = roll_want_;
    return;
  }
  if (len > loop_) {
    roll_restart_ = true;  // longer than what the ring holds as the loop: leave and capture again
    phase_ = Phase::Leave;
    return;
  }
  // Shorter: the head of the old loop. The first repeat copies it from the old repeat (lag_ stays the old length) and blends its tail
  // with the end of the old loop in relatch_tail(); no baking is pending, so the fade is the new loop's.
  roll_cur_ = roll_want_;
  loop_ = len;
  fade_ = fade_frames(fs_, len);
  inv_fade_ = 1.0f / static_cast<float>(fade_);
  fp_ = fade_;
  bake_next_ = fade_;
  relatch_ = true;
}

// The last `fade_` frames of the first repeat of a shorter loop: the copy of the old loop's head, blended into the end of the old loop
// (ring[w - loop_] there), so that the new loop point joins the old loop's end to the old loop's head, which is what the ring had.
void Freeze::relatch_tail(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  float* const lr = l_;
  float* const rr = r_;
  const std::uint32_t mask = mask_;
  const std::uint32_t d = lag_;
  const std::uint32_t nl = loop_;
  const std::uint32_t fade = fade_;
  const float inv = inv_fade_;
  const float target = mix_target_;
  const float k = mix_k_;
  std::uint32_t w = w_;
  std::uint32_t t = roll_pos_ - (nl - fade);
  float m = mix_;
  for (std::uint32_t i = from; i < to; ++i) {
    const float a = (t + 1u == fade) ? 1.0f : static_cast<float>(t + 1u) * inv;
    const std::uint32_t rd = (w - d) & mask;
    const std::uint32_t re = (w - nl) & mask;
    const float cl = lr[rd] * (1.0f - a) + lr[re] * a;
    const float cr = rr[rd] * (1.0f - a) + rr[re] * a;
    lr[w] = cl;
    rr[w] = cr;
    m += k * (target - m);
    const float il = args.in_l[i];
    const float ir = args.in_r[i];
    args.out_l[i] = il * (1.0f - m) + cl * m;
    args.out_r[i] = ir * (1.0f - m) + cr * m;
    w = (w + 1u) & mask;
    ++t;
  }
  w_ = w;
  mix_ = std::fabs(target - m) < 1e-4f ? target : m;
}

std::uint32_t Freeze::run_roll(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  if (roll_dirty_) {
    roll_dirty_ = false;
    roll_want_ = roll_rate_;
    accel_left_ = accel_period_;
  }
  if (fp_ < fade_) {  // the entry crossfade, as FREEZE has it
    const std::uint32_t n = to - from < fade_ - fp_ ? to - from : fade_ - fp_;
    hold<true>(args, from, from + n);
    roll_pos_ += n;
    roll_tick(n);
    return from + n;
  }
  if (roll_pos_ >= loop_) {
    roll_boundary();
    if (phase_ != Phase::Hold) {
      return from;  // restarting: the caller leaves, then enters again
    }
  }
  std::uint32_t n = to - from < loop_ - roll_pos_ ? to - from : loop_ - roll_pos_;
  if (relatch_ && roll_pos_ >= loop_ - fade_) {
    relatch_tail(args, from, from + n);
  } else {
    if (relatch_ && n > loop_ - fade_ - roll_pos_) {
      n = loop_ - fade_ - roll_pos_;  // up to where the tail's blend begins
    }
    hold<false>(args, from, from + n);
  }
  roll_pos_ += n;
  roll_tick(n);
  return from + n;
}

// ---- STOP ---------------------------------------------------------------------------------------------------------------------

void Freeze::enter_stop(std::uint32_t tempo_mbpm) noexcept {
  const std::uint32_t cap = mask_ + 1u;
  const std::uint32_t len = stop_frames(stop_time_, tempo_mbpm, cap);
  const std::uint32_t spin = len / 4u > kMinSpin ? len / 4u : kMinSpin;
  dec_ = 1.0f / static_cast<float>(len);
  inc_ = 1.0f / static_cast<float>(spin);
  sp_ = 1.0f;
  li_ = 0;
  lf_ = 0.0f;
  lag_max_ = cap - kHeadroom;
  xfade_ = fade_frames(fs_, len);
  inv_xfade_ = 1.0f / static_cast<float>(xfade_);
  xp_ = xfade_;
  fading_ = false;
  mix_ = mix_target_;  // at entry the output is the input whatever Mix is, so it may jump
  phase_ = Phase::Stop;
}

// One piece of a STOP, K: 0 the speed falls (Active on), 1 it rises (released), 2 the final crossfade to the input. The input is recorded
// first, so the read at a lag of 0 is the input itself. The read is between the frames `lag` and `lag + 1` behind the write head (the lag's
// fraction weighs the older one). Returns the frame it got to: a spin-up ends the piece the frame the speed is back to 1, and the crossfade
// ends it with the share at 0 (the phase is then Idle).
template <int K>
std::uint32_t Freeze::stop_loop(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  float* const lr = l_;
  float* const rr = r_;
  const std::uint32_t mask = mask_;
  const std::uint32_t lag_max = lag_max_;
  const float target = mix_target_;
  const float k = mix_k_;
  const float dec = dec_;
  const float inc = inc_;
  const float inv = inv_xfade_;
  const std::uint32_t xfade = xfade_;
  std::uint32_t w = w_;
  std::uint32_t li = li_;
  std::uint32_t xp = xp_;
  float s = sp_;
  float lf = lf_;
  float m = mix_;
  bool done = false;
  std::uint32_t i = from;
  for (; i < to && !done; ++i) {
    const float il = args.in_l[i];
    const float ir = args.in_r[i];
    lr[w] = il;
    rr[w] = ir;
    if constexpr (K == 0) {
      s -= dec;
      s = s > 0.0f ? s : 0.0f;
    } else if constexpr (K == 1) {
      s += inc;
      s = s < 1.0f ? s : 1.0f;
    }
    lf += 1.0f - s;
    if (lf >= 1.0f) {
      lf -= 1.0f;
      ++li;
    }
    if (li >= lag_max) {
      li = lag_max;
      lf = 0.0f;
    }
    const std::uint32_t b = (w - li) & mask;
    const std::uint32_t a = (b - 1u) & mask;
    const float gain = s * 4.0f < 1.0f ? s * 4.0f : 1.0f;  // the level follows the speed: the stop ends in silence
    const float wl = (lr[a] * lf + lr[b] * (1.0f - lf)) * gain;
    const float wr = (rr[a] * lf + rr[b] * (1.0f - lf)) * gain;
    m += k * (target - m);
    float share = 1.0f;
    if constexpr (K == 2) {
      share = xp >= xfade ? 1.0f : static_cast<float>(xp) * inv;
      --xp;
      done = xp == 0u;
    }
    const float c = m * share;
    args.out_l[i] = il * (1.0f - c) + wl * c;
    args.out_r[i] = ir * (1.0f - c) + wr * c;
    w = (w + 1u) & mask;
    if constexpr (K == 1) {
      done = s >= 1.0f;
    }
  }
  w_ = w;
  li_ = li;
  lf_ = lf;
  sp_ = s;
  xp_ = xp;
  mix_ = std::fabs(target - m) < 1e-4f ? target : m;
  if constexpr (K == 1) {
    if (done) {
      fading_ = true;  // back at full speed: the crossfade into the input, from the full share
      xp_ = xfade_;
    }
  } else if constexpr (K == 2) {
    if (done) {
      fading_ = false;
      phase_ = Phase::Idle;
    }
  }
  return i;
}

std::uint32_t Freeze::run_stop(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  if (fading_) {
    return stop_loop<2>(args, from, to);
  }
  return want_ ? stop_loop<0>(args, from, to) : stop_loop<1>(args, from, to);
}

// ---- REVERSE ------------------------------------------------------------------------------------------------------------------

void Freeze::enter_reverse() noexcept {
  rev_span_ = reverse_span(mask_ + 1u);
  xfade_ = fade_frames(fs_, rev_span_);
  inv_xfade_ = 1.0f / static_cast<float>(xfade_);
  xp_ = 0;
  rev_k_ = 0;
  rev_forced_ = false;
  rev_org2_ = 2u * w_;
  mix_ = mix_target_;  // the share of the reversal starts at 0
  phase_ = Phase::Reverse;
}

// K: +1 the share of the reversal rises, 0 it is full, -1 it falls. The read is the ring `2 k` frames behind the write head, k frames since
// the entry: org - k, where org is the ring index of the entry (org2 = 2 org, modulo the ring). The input is recorded first.
template <int K>
void Freeze::reverse_loop(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  float* const lr = l_;
  float* const rr = r_;
  const std::uint32_t mask = mask_;
  const std::uint32_t org2 = rev_org2_;
  const float target = mix_target_;
  const float k = mix_k_;
  const float inv = inv_xfade_;
  const std::uint32_t xfade = xfade_;
  std::uint32_t w = w_;
  std::uint32_t xp = xp_;
  float m = mix_;
  for (std::uint32_t i = from; i < to; ++i) {
    const float il = args.in_l[i];
    const float ir = args.in_r[i];
    lr[w] = il;
    rr[w] = ir;
    const std::uint32_t rd = (org2 - w) & mask;
    const float pl = lr[rd];
    const float pr = rr[rd];
    m += k * (target - m);
    float share = 1.0f;
    if constexpr (K != 0) {
      share = xp >= xfade ? 1.0f : static_cast<float>(xp) * inv;
      xp = K > 0 ? xp + 1u : xp - 1u;
    }
    const float c = m * share;
    args.out_l[i] = il * (1.0f - c) + pl * c;
    args.out_r[i] = ir * (1.0f - c) + pr * c;
    w = (w + 1u) & mask;
  }
  w_ = w;
  xp_ = xp;
  rev_k_ += to - from;
  mix_ = std::fabs(target - m) < 1e-4f ? target : m;
}

std::uint32_t Freeze::run_reverse(const dsp::ProcessArgs& args, std::uint32_t from, std::uint32_t to) noexcept {
  const std::uint32_t limit = rev_span_ - xfade_;  // after this many frames the reversal must leave (the ring behind it ends)
  if (!rev_forced_ && rev_k_ >= limit) {
    rev_forced_ = true;
  }
  if (rev_forced_ || !want_) {
    if (xp_ == 0u) {
      phase_ = Phase::Idle;  // nothing of the reversal is in the output: the input records and passes
      return from;
    }
    const std::uint32_t n = to - from < xp_ ? to - from : xp_;
    reverse_loop<-1>(args, from, from + n);
    if (xp_ == 0u) {
      phase_ = Phase::Idle;
    }
    return from + n;
  }
  std::uint32_t n = to - from < limit - rev_k_ ? to - from : limit - rev_k_;
  if (xp_ < xfade_) {
    n = n < xfade_ - xp_ ? n : xfade_ - xp_;
    reverse_loop<1>(args, from, from + n);
  } else {
    reverse_loop<0>(args, from, from + n);
  }
  return from + n;
}

void Freeze::process(const dsp::ProcessArgs& args) noexcept {
  baked_ = 0;
  if (bake_next_ < fade_) {
    bake((args.frames + 1u) / 2u);  // half a frame a frame at least, whatever the block size
  }
  dsp::for_each_run(
      args, [&](std::uint32_t from, std::uint32_t to) { run(args, from, to); },
      [&](const dsp::Event& e) {
        if (e.kind == dsp::EventKind::Param) {
          set(e.param, e.value);
        }
      });
}

}  // namespace o3_303
