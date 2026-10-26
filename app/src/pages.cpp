// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/pages.h>

#include <o3sdk/core/scales.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/pattern.h>
#include <o3_303/perform.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/tb303/tb303.h>

#include <cmath>

namespace o3_303 {

namespace {

using namespace o3sdk;
using core::ParamCurve;
using core::ParamUnit;

// ---- The pages --------------------------------------------------------------------------------------------------------------

constexpr Entry voice(Tb303::Param id) noexcept { return Entry{Item::Voice, static_cast<std::uint16_t>(id), nullptr}; }
constexpr Entry dist(Distortion::Param id, const char* label = nullptr) noexcept {
  return Entry{Item::Dist, static_cast<std::uint16_t>(id), label};
}
constexpr Entry delay(units::Delay::Param id, const char* label) noexcept {
  return Entry{Item::Delay, static_cast<std::uint16_t>(id), label};
}
constexpr Entry freeze(Freeze::Param id, const char* label) noexcept { return Entry{Item::Freeze, static_cast<std::uint16_t>(id), label}; }
constexpr Entry growl(Growl::Param id, const char* label) noexcept { return Entry{Item::Growl, static_cast<std::uint16_t>(id), label}; }
constexpr Entry wobble(WobbleField field, const char* label) noexcept { return Entry{Item::Wobble, static_cast<std::uint16_t>(field), label}; }
constexpr Entry app_item(Item item, const char* label = nullptr) noexcept { return Entry{item, 0, label}; }
constexpr Entry perf(unsigned button, const char* label) noexcept { return Entry{Item::Perf, static_cast<std::uint16_t>(button), label}; }
constexpr Entry snap(unsigned slot, const char* label) noexcept { return Entry{Item::Snap, static_cast<std::uint16_t>(slot), label}; }

constexpr Page kPages[kPageCount] = {
    {"SOUND",
     {voice(Tb303::Cutoff), voice(Tb303::Reso), voice(Tb303::EnvMod), voice(Tb303::Decay), voice(Tb303::Accent), voice(Tb303::Wave),
      voice(Tb303::Tune), app_item(Item::Level, "VOLUME")}},
    // The design's F6 (the reverb send, W5) and F7 are free this round: F6 is the distortion's Mix, F7 its Active and Type as one key
    // (OFF, GRIT .. FOLD). The reverb send takes F6 when it exists.
    {"FX",
     {dist(Distortion::Drive), dist(Distortion::Tone), delay(units::Delay::Time, "DLY TIME"), delay(units::Delay::Feedback, "DLY FB"),
      app_item(Item::SendA), dist(Distortion::Mix, "DIST MIX"), app_item(Item::DistMode, "DIST"), app_item(Item::Master)}},
    // The growl (M3c G, slot 0's insert 1): the formant filter's vowel, Q and mix, the tuned comb's tune, feedback and mix, and the wet / dry of the whole
    // unit (0 is the default: a bit-exact bypass). F8 is free.
    {"GROWL",
     {growl(Growl::Vowel, "VOWEL"), growl(Growl::FormantQ, "FMT Q"), growl(Growl::FormantMix, "FMT MIX"), growl(Growl::CombTune, "CMB TUNE"),
      growl(Growl::CombFeedback, "CMB FB"), growl(Growl::CombMix, "CMB MIX"), growl(Growl::Mix, "GRL MIX"), Entry{}}},
    // The wobble (M3c W): what the WOBBLE performance action runs while it is on. F6..F8 are free.
    {"WOBBLE",
     {wobble(kWobbleRate, "RATE"), wobble(kWobbleShape, "SHAPE"), wobble(kWobbleDepth, "DEPTH"), wobble(kWobbleReset, "RESET"),
      wobble(kWobbleTarget, "TARGET"), Entry{}, Entry{}, Entry{}}},
    // The transport and the step: the generator's settings are the RANDOM popup's (random_entry, Shift + F1). F6..F8 were the PERF page's.
    {"SEQ",
     {app_item(Item::Tempo), app_item(Item::Swing), app_item(Item::Length), app_item(Item::Gate), app_item(Item::Transpose, "TRANSP"),
      app_item(Item::FillVariant, "FILL"), app_item(Item::Ratchet), app_item(Item::Cond)}},
    // The performance buttons' assignments (C4), the beat freeze's length and the roll rate, accelerate and stop time of ROLL and STOP (M3c R), until
    // the kit's system Config exists. F1..F4: the button's name, its action as the value.
    {"PERF",
     {perf(0, "NOTE"), perf(1, "INSTR"), perf(2, "VOL"), perf(3, "FX"), freeze(Freeze::Length, "FRZ LEN"), freeze(Freeze::RollRate, "ROLL RT"),
      freeze(Freeze::Accelerate, "ROLL ACC"), freeze(Freeze::StopTime, "STP TIME")}},
    // The snapshots (R2): the keys recall and (with Shift) store, the encoder morphs; the page's own strip draws them.
    {"SNAP",
     {snap(0, "SNAP 1"), snap(1, "SNAP 2"), snap(2, "SNAP 3"), snap(3, "SNAP 4"), snap(4, "SNAP 5"), snap(5, "SNAP 6"), snap(6, "SNAP 7"),
      snap(7, "SNAP 8")}},
};

constexpr Entry kRandomEntries3[kRandomEntries] = {app_item(Item::Scale), app_item(Item::Density), app_item(Item::AccSlide, "ACC/SLD")};

// What a snapshot holds (pages.h): SOUND's seven (the voice's own Volume stays at 0 dB; VOLUME is the level), the distortion's five, the
// delay's four, the level and the send.
constexpr Entry kSnapTargets[kSnapTargetCount] = {
    voice(Tb303::Cutoff), voice(Tb303::Reso), voice(Tb303::EnvMod), voice(Tb303::Decay), voice(Tb303::Accent), voice(Tb303::Wave), voice(Tb303::Tune),
    dist(Distortion::Active), dist(Distortion::Type), dist(Distortion::Drive), dist(Distortion::Tone), dist(Distortion::Mix),
    delay(units::Delay::Time, nullptr), delay(units::Delay::Feedback, nullptr), delay(units::Delay::Tone, nullptr), delay(units::Delay::Mode, nullptr),
    app_item(Item::Level, "VOLUME"), app_item(Item::SendA)};

// ---- Descriptors of the app-level values (name, unit text, names, knots, min, max, default, detents, coarse, list count, display,
// curve, flags, page) ---------------------------------------------------------------------------------------------------------

constexpr const char* kLengthNames[] = {"8", "16", "32", "64"};

// The scale names as pointers (core::kScaleNames holds fixed-width rows).
struct ScaleNames {
  const char* p[core::kScaleCount];
  constexpr ScaleNames() noexcept : p() {
    for (unsigned i = 0; i < core::kScaleCount; ++i) {
      p[i] = core::kScaleNames[i];
    }
  }
};
constexpr ScaleNames kScaleNamePtrs{};

constexpr core::ParamDesc kTempo = {"TEMPO", "BPM", nullptr, nullptr, 20.0f, 300.0f, 125.0f, 560, 10, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kSwing = {"SWING", "%", nullptr, nullptr, 0.0f, static_cast<float>(kMaxSwing), 0.0f, kMaxSwing, 5, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kLength = {"LENGTH", "", kLengthNames, nullptr, 0.0f, 3.0f, 1.0f, 3, 1, 4, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kGate = {"GATE", "%", nullptr, nullptr, 5.0f, 100.0f, static_cast<float>(kDefaultGate), 95, 5, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kTranspose = {"TRANSPOSE", "st", nullptr, nullptr, -24.0f, 24.0f, 0.0f, 48, 12, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kScale = {"SCALE", "", kScaleNamePtrs.p, nullptr, 0.0f, static_cast<float>(core::kScaleCount - 1u), 18.0f,
                                    static_cast<std::uint16_t>(core::kScaleCount - 1u), 4, static_cast<std::uint8_t>(core::kScaleCount),
                                    ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kDensity = {"DENSITY", "%", nullptr, nullptr, 0.0f, 100.0f, 75.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kAccSlide = {"ACC/SLIDE", "%", nullptr, nullptr, 0.0f, 100.0f, 25.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kSendA = {"DLY SEND", "%", nullptr, nullptr, 0.0f, 100.0f, 30.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
constexpr const char* kDistModeNames[] = {"OFF", "GRIT", "TUBE", "HARD", "FUZZ", "FOLD"};
constexpr core::ParamDesc kDistMode = {"DIST", "", kDistModeNames, nullptr, 0.0f, 5.0f, 1.0f, 5, 1, 6, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kLevel = {"VOLUME", "dB", nullptr, nullptr, -60.0f, 3.5f, -6.0f, 127, 6, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kMaster = {"MASTER", "%", nullptr, nullptr, 0.0f, 150.0f, 100.0f, 150, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};

constexpr const char* kFillNames[seq::kFillVariantCount] = {"MUTATE", "OCT UP", "RATCHET", "ACC ROT"};
constexpr core::ParamDesc kFillVariant = {"FILL", "", kFillNames, nullptr, 0.0f, 3.0f, 0.0f, 3, 1, seq::kFillVariantCount, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kPerfAssign = {"ACTION", "", kPerfActionNames, nullptr, 0.0f, static_cast<float>(kPerfActionCount - 1u), 0.0f,
                                         static_cast<std::uint16_t>(kPerfActionCount - 1u), 1, static_cast<std::uint8_t>(kPerfActionCount),
                                         ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr const char* kRatchetNames[kMaxRatchet + 1u] = {"x1", "x2", "x3", "x4"};
constexpr core::ParamDesc kRatchet = {"RATCHET", "", kRatchetNames, nullptr, 0.0f, static_cast<float>(kMaxRatchet), 0.0f, kMaxRatchet, 1,
                                      kMaxRatchet + 1u, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
// The condition is a place on the encoder's walk (ops.h step_cond): 0 always, 1..9 the 10..90 %, 10..44 the ratios, 45..48 FILL .. NOT FIRST.
// Its text is cond_text(), not the descriptor's.
constexpr core::ParamDesc kCond = {"COND", "", nullptr, nullptr, 0.0f, static_cast<float>(seq::kCondStepCount - 1u), 0.0f,
                                   static_cast<std::uint16_t>(seq::kCondStepCount - 1u), 5, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};

// The wobble's settings (M3c W); the order is WobbleField's. Rate and shape are the sequencer's own tables (seq/wobble.h).
constexpr const char* kOffOnNames[] = {"OFF", "ON"};
constexpr const char* kWobbleTargetNames[kWobbleTargetCount] = {"CUTOFF", "VOWEL"};
constexpr core::ParamDesc kWobbleDescs[kWobbleFieldCount] = {
    {"RATE", "", seq::kWobbleRateNames, nullptr, 0.0f, static_cast<float>(seq::kWobbleRateCount - 1u), static_cast<float>(seq::kWobble1_8),
     static_cast<std::uint16_t>(seq::kWobbleRateCount - 1u), 1, static_cast<std::uint8_t>(seq::kWobbleRateCount), ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"SHAPE", "", seq::kWobbleShapeNames, nullptr, 0.0f, static_cast<float>(seq::kWobbleShapeCount - 1u), static_cast<float>(seq::kWobbleSine),
     static_cast<std::uint16_t>(seq::kWobbleShapeCount - 1u), 1, static_cast<std::uint8_t>(seq::kWobbleShapeCount), ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"DEPTH", "%", nullptr, nullptr, 0.0f, 100.0f, 50.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"RESET", "", kOffOnNames, nullptr, 0.0f, 1.0f, 1.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"TARGET", "", kWobbleTargetNames, nullptr, 0.0f, static_cast<float>(kWobbleTargetCount - 1u), 0.0f, static_cast<std::uint16_t>(kWobbleTargetCount - 1u), 1,
     static_cast<std::uint8_t>(kWobbleTargetCount), ParamUnit::Enum, ParamCurve::Linear, 0, 0},
};

constexpr const char* kSnapNames[] = {"EMPTY", "SET"};
constexpr core::ParamDesc kSnapSlot = {"SNAP", "", kSnapNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};

constexpr unsigned kLengthCount = sizeof(kLengths) / sizeof(kLengths[0]);

unsigned length_index(unsigned length) noexcept {
  for (unsigned i = 0; i < kLengthCount; ++i) {
    if (kLengths[i] == length) {
      return i;
    }
  }
  return 1;
}

int round_to_int(float v) noexcept { return static_cast<int>(v < 0.0f ? v - 0.5f : v + 0.5f); }

}  // namespace

const Page& page_at(unsigned index) noexcept { return kPages[index < kPageCount ? index : 0u]; }
const Entry& random_entry(unsigned index) noexcept { return kRandomEntries3[index < kRandomEntries ? index : 0u]; }
const Entry& snap_target(unsigned index) noexcept { return kSnapTargets[index < kSnapTargetCount ? index : 0u]; }

// ---- Controls ---------------------------------------------------------------------------------------------------------------

const core::ParamDesc* Controls::desc(const dsp::Engine& e, const Entry& entry) const noexcept {
  switch (entry.item) {
    case Item::Voice:
      return e.param_desc(w_->voice, entry.id);
    case Item::Dist:
      return e.param_desc(w_->dist, entry.id);
    case Item::Delay:
      return e.param_desc(w_->delay, entry.id);
    case Item::Tempo:
      return &kTempo;
    case Item::Swing:
      return &kSwing;
    case Item::Length:
      return &kLength;
    case Item::Gate:
      return &kGate;
    case Item::Transpose:
      return &kTranspose;
    case Item::Scale:
      return &kScale;
    case Item::Density:
      return &kDensity;
    case Item::AccSlide:
      return &kAccSlide;
    case Item::SendA:
      return &kSendA;
    case Item::Master:
      return &kMaster;
    case Item::Level:
      return &kLevel;
    case Item::DistMode:
      return &kDistMode;
    case Item::Freeze:
      return e.param_desc(w_->freeze, entry.id);
    case Item::Ratchet:
      return &kRatchet;
    case Item::Cond:
      return &kCond;
    case Item::FillVariant:
      return &kFillVariant;
    case Item::Perf:
      return entry.id < kPerfButtons ? &kPerfAssign : nullptr;
    case Item::Snap:
      return entry.id < kSnapSlots ? &kSnapSlot : nullptr;
    case Item::Growl:
      return e.param_desc(w_->growl, entry.id);
    case Item::Wobble:
      return entry.id < kWobbleFieldCount ? &kWobbleDescs[entry.id] : nullptr;
    case Item::Blank:
      break;
  }
  return nullptr;
}

float Controls::get(const dsp::Engine& e, const Entry& entry) const noexcept {
  const Pattern& p = w_->bank->pattern(w_->bank->selected());
  switch (entry.item) {
    case Item::Voice:
      return e.param(w_->voice, entry.id);
    case Item::Dist:
      return e.param(w_->dist, entry.id);
    case Item::Delay:
      return e.param(w_->delay, entry.id);
    case Item::Tempo:
      return static_cast<float>(e.tempo()) * 0.001f;
    case Item::Swing:
      return static_cast<float>(p.swing);
    case Item::Length:
      return static_cast<float>(length_index(p.length));
    case Item::Gate:
      return static_cast<float>(p.gate);
    case Item::Transpose:
      return static_cast<float>(p.transpose);
    case Item::Scale:
      return static_cast<float>(w_->gen.scale);
    case Item::Density:
      return static_cast<float>(w_->gen.density);
    case Item::AccSlide:
      return static_cast<float>(w_->gen.accent_pct);
    case Item::SendA:
      return w_->send_a_pct;
    case Item::Master:
      return e.master_level() * 100.0f;
    case Item::Level:
      return w_->level_db;
    case Item::DistMode:
      // OFF when the insert is inactive, else 1 + its model.
      return e.param(w_->dist, Distortion::Active) < 0.5f ? 0.0f : 1.0f + std::floor(e.param(w_->dist, Distortion::Type) + 0.5f);
    case Item::Freeze:
      return e.param(w_->freeze, entry.id);
    case Item::Ratchet:
      return static_cast<float>(w_->bank->get_ratchet(w_->bank->selected(), w_->step_focus));
    case Item::Cond:
      return static_cast<float>(seq::cond_index(w_->bank->get_cond(w_->bank->selected(), w_->step_focus)));
    case Item::FillVariant:
      return w_->seq != nullptr ? static_cast<float>(w_->seq->fill_variant()) : 0.0f;
    case Item::Perf:
      return entry.id < kPerfButtons ? static_cast<float>(w_->perf[entry.id]) : 0.0f;
    case Item::Snap:
      return w_->snaps.used(entry.id) ? 1.0f : 0.0f;
    case Item::Growl:
      return e.param(w_->growl, entry.id);
    case Item::Wobble:
      switch (entry.id) {
        case kWobbleRate:
          return static_cast<float>(w_->wobble.rate);
        case kWobbleShape:
          return static_cast<float>(w_->wobble.shape);
        case kWobbleDepth:
          return static_cast<float>(w_->wobble.depth_pct);
        case kWobbleReset:
          return w_->wobble.reset ? 1.0f : 0.0f;
        case kWobbleTarget:
          return static_cast<float>(w_->wobble.target);
        default:
          break;
      }
      return 0.0f;
    case Item::Blank:
      break;
  }
  return 0.0f;
}

void Controls::set(dsp::Engine& e, const Entry& entry, float value) noexcept {
  const core::ParamDesc* d = desc(e, entry);
  if (d == nullptr) {
    return;
  }
  const float v = core::param_clamp(*d, value);
  const unsigned sel = w_->bank->selected();
  switch (entry.item) {
    case Item::Voice:
      (void)e.set_param(w_->voice, entry.id, v);
      break;
    case Item::Dist:
      (void)e.set_param(w_->dist, entry.id, v);
      break;
    case Item::Delay:
      (void)e.set_param(w_->delay, entry.id, v);
      break;
    case Item::Tempo:
      (void)e.set_tempo(static_cast<std::uint32_t>(v * 1000.0f + 0.5f));
      break;
    case Item::Swing:
      (void)w_->bank->set_swing(sel, round_to_int(v));
      break;
    case Item::Length:
      (void)w_->bank->set_length(sel, kLengths[static_cast<unsigned>(round_to_int(v)) % kLengthCount]);
      break;
    case Item::Gate:
      (void)w_->bank->set_gate(sel, round_to_int(v));
      break;
    case Item::Transpose:
      (void)w_->bank->set_transpose(sel, round_to_int(v));
      break;
    case Item::Scale:
      w_->gen.scale = static_cast<std::uint8_t>(round_to_int(v));
      w_->scale_set = true;  // the player chose it: the keyboard is locked to it (R8)
      break;
    case Item::Density:
      w_->gen.density = static_cast<std::uint8_t>(round_to_int(v));
      break;
    case Item::AccSlide: {
      const int acc = round_to_int(v);
      w_->gen.accent_pct = static_cast<std::uint8_t>(acc);
      w_->gen.slide_pct = static_cast<std::uint8_t>(acc * 4 / 5);
      break;
    }
    case Item::SendA:
      w_->send_a_pct = v;
      apply_send(e);
      break;
    case Item::Master:
      (void)e.set_master_level(v * 0.01f);
      break;
    case Item::Level:
      w_->level_db = v;
      apply_level(e);
      break;
    case Item::DistMode: {
      const int mode = round_to_int(v);
      (void)e.set_param(w_->dist, Distortion::Active, mode == 0 ? 0.0f : 1.0f);
      if (mode > 0) {
        (void)e.set_param(w_->dist, Distortion::Type, static_cast<float>(mode - 1));
      }
      break;
    }
    case Item::Freeze:
      (void)e.set_param(w_->freeze, entry.id, v);
      break;
    case Item::Ratchet:
      (void)w_->bank->set_ratchet(sel, w_->step_focus, static_cast<unsigned>(round_to_int(v)));
      break;
    case Item::Cond:
      (void)w_->bank->set_cond(sel, w_->step_focus, seq::cond_at(static_cast<unsigned>(round_to_int(v))));
      break;
    case Item::FillVariant:
      if (w_->seq != nullptr) {
        w_->seq->set_fill_variant(static_cast<seq::FillVariant>(round_to_int(v)));
      }
      break;
    case Item::Perf:
      if (entry.id < kPerfButtons) {
        w_->perf[entry.id] = static_cast<std::uint8_t>(round_to_int(v));
      }
      break;
    case Item::Growl:
      (void)e.set_param(w_->growl, entry.id, v);
      break;
    case Item::Wobble: {
      WobbleSettings& ws = w_->wobble;
      const int n = round_to_int(v);
      switch (entry.id) {
        case kWobbleRate:
          ws.rate = static_cast<std::uint8_t>(n);
          break;
        case kWobbleShape:
          ws.shape = static_cast<std::uint8_t>(n);
          break;
        case kWobbleDepth:
          ws.depth_pct = static_cast<std::uint8_t>(n);
          break;
        case kWobbleReset:
          ws.reset = n != 0;
          break;
        case kWobbleTarget:
          ws.target = static_cast<std::uint8_t>(n);
          break;
        default:
          break;
      }
      if (w_->seq != nullptr) {  // a running wobble takes the change at once; otherwise only the note reset has a meaning of its own
        if (w_->seq->wobble().on) {
          apply_wobble(true);
        } else {
          w_->seq->set_wobble_reset(ws.reset);
        }
      }
      break;
    }
    case Item::Snap:  // a snapshot is recalled and stored by the screen's keys, not set
    case Item::Blank:
      break;
  }
}

void Controls::step(dsp::Engine& e, const Entry& entry, int detents, bool coarse) noexcept {
  const core::ParamDesc* d = desc(e, entry);
  if (d == nullptr || detents == 0) {
    return;
  }
  if (entry.item == Item::Voice || entry.item == Item::Dist || entry.item == Item::Delay || entry.item == Item::Freeze || entry.item == Item::Growl) {
    const dsp::UnitId unit = entry.item == Item::Voice
                                 ? w_->voice
                                 : (entry.item == Item::Dist ? w_->dist
                                                             : (entry.item == Item::Delay ? w_->delay : (entry.item == Item::Freeze ? w_->freeze : w_->growl)));
    (void)e.step_param(unit, entry.id, detents, coarse);
    return;
  }
  if (is_step_entry(entry)) {
    step_at(e, entry, w_->step_focus, detents, coarse);
    return;
  }
  const int n = coarse ? detents * static_cast<int>(d->coarse) : detents;
  set(e, entry, core::param_step(*d, get(e, entry), n));
}

void Controls::step_at(dsp::Engine& e, const Entry& entry, unsigned step, int detents, bool coarse) noexcept {
  const core::ParamDesc* d = desc(e, entry);
  if (d == nullptr || detents == 0 || !is_step_entry(entry)) {
    return;
  }
  const int n = coarse ? detents * static_cast<int>(d->coarse) : detents;
  const unsigned sel = w_->bank->selected();
  if (entry.item == Item::Ratchet) {
    const int r = static_cast<int>(w_->bank->get_ratchet(sel, step)) + n;
    (void)w_->bank->set_ratchet(sel, step, static_cast<unsigned>(r < 0 ? 0 : (r > static_cast<int>(kMaxRatchet) ? static_cast<int>(kMaxRatchet) : r)));
  } else {
    (void)w_->bank->set_cond(sel, step, seq::step_cond(w_->bank->get_cond(sel, step), n));
  }
}

unsigned Controls::format(const dsp::Engine& e, const Entry& entry, char* out, unsigned cap) const noexcept {
  const core::ParamDesc* d = desc(e, entry);
  if (d == nullptr || cap == 0u) {
    if (cap != 0u) {
      out[0] = '\0';
    }
    return 0;
  }
  if (entry.item == Item::AccSlide && cap >= 8u) {
    // "25/20": the accent chance and the slide chance it brings.
    unsigned n = 0;
    const unsigned acc = w_->gen.accent_pct;
    const unsigned sld = w_->gen.slide_pct;
    if (acc >= 100u) {
      out[n++] = '1';
      out[n++] = '0';
      out[n++] = '0';
    } else {
      if (acc >= 10u) {
        out[n++] = static_cast<char>('0' + acc / 10u);
      }
      out[n++] = static_cast<char>('0' + acc % 10u);
    }
    out[n++] = '/';
    if (sld >= 100u) {
      out[n++] = '1';
      out[n++] = '0';
      out[n++] = '0';
    } else {
      if (sld >= 10u) {
        out[n++] = static_cast<char>('0' + sld / 10u);
      }
      out[n++] = static_cast<char>('0' + sld % 10u);
    }
    out[n] = '\0';
    return n;
  }
  if (entry.item == Item::Cond) {
    return cond_text(w_->bank->get_cond(w_->bank->selected(), w_->step_focus), out, cap);
  }
  return core::param_format(*d, get(e, entry), out, cap);
}

const char* Controls::unit_text(const dsp::Engine& e, const Entry& entry) const noexcept {
  const core::ParamDesc* d = desc(e, entry);
  return d != nullptr && d->unit_text != nullptr ? d->unit_text : "";
}

const char* Controls::label(const dsp::Engine& e, const Entry& entry) const noexcept {
  if (entry.label != nullptr) {
    return entry.label;
  }
  const core::ParamDesc* d = desc(e, entry);
  return d != nullptr && d->name != nullptr ? d->name : "";
}

// ---- Motion lanes -----------------------------------------------------------------------------------------------------------

bool Controls::lane_ref(const Entry& entry, LaneRef& out) const noexcept {
  switch (entry.item) {
    case Item::Voice:
      out = LaneRef{kLaneVoice, static_cast<std::uint8_t>(entry.id)};
      return entry.id < seq::kKnobs;
    case Item::Dist:
      out = LaneRef{kLaneDist, static_cast<std::uint8_t>(entry.id)};
      return entry.id < seq::kKnobs;
    case Item::Delay:
      out = LaneRef{kLaneDelay, static_cast<std::uint8_t>(entry.id)};
      return entry.id < seq::kKnobs;
    case Item::Growl:
      out = LaneRef{kLaneGrowl, static_cast<std::uint8_t>(entry.id)};
      return entry.id < seq::kKnobs;
    case Item::Level:
      out = LaneRef{kLaneMixer, kMixerLevel};
      return true;
    case Item::SendA:
      out = LaneRef{kLaneMixer, kMixerSendA};
      return true;
    default:
      break;
  }
  return false;
}

Entry Controls::lane_entry(const LaneRef& ref) const noexcept {
  switch (ref.target) {
    case kLaneVoice:
      return Entry{Item::Voice, ref.id, nullptr};
    case kLaneDist:
      return Entry{Item::Dist, ref.id, nullptr};
    case kLaneDelay:
      return Entry{Item::Delay, ref.id, nullptr};
    case kLaneGrowl:
      return Entry{Item::Growl, ref.id, nullptr};
    case kLaneMixer:
      if (ref.id == kMixerLevel) {
        return Entry{Item::Level, 0, nullptr};
      }
      if (ref.id == kMixerSendA) {
        return Entry{Item::SendA, 0, nullptr};
      }
      break;
    default:
      break;
  }
  return Entry{};
}

const char* Controls::lane_name(const dsp::Engine& e, const LaneRef& ref) const noexcept {
  static constexpr const char* kMixerNames[kMixerFieldCount] = {"LEVEL", "PAN", "SEND A", "SEND B", "MUTE"};
  if (ref.target == kLaneNone || ref.target >= kLaneTargetCount) {
    return "";
  }
  if (ref.target == kLaneMixer) {
    return ref.id < kMixerFieldCount ? kMixerNames[ref.id] : "";
  }
  const core::ParamDesc* d = desc(e, lane_entry(ref));
  return d != nullptr && d->name != nullptr ? d->name : "";
}

float Controls::to_lane(const Entry& entry, float v) const noexcept {
  switch (entry.item) {
    case Item::Level:  // the dB the page shows, as the linear gain apply_level() writes
      return v <= -59.5f ? 0.0f : std::fmin(1.5f, std::pow(10.0f, v * 0.05f));
    case Item::SendA:
      return v * 0.01f;
    default:
      break;
  }
  return v;
}

float Controls::from_lane(const Entry& entry, float v) const noexcept {
  switch (entry.item) {
    case Item::Level:
      return v <= 0.001f ? -60.0f : std::fmax(-60.0f, std::fmin(3.5f, 20.0f * std::log10(v)));
    case Item::SendA:
      return v * 100.0f;
    default:
      break;
  }
  return v;
}

std::uint8_t Controls::to_point(const dsp::Engine& e, const Entry& entry, float v) const noexcept {
  LaneRef ref{kLaneNone, 0};
  if (!lane_ref(entry, ref)) {
    return 0;
  }
  if (ref.target == kLaneMixer) {
    return mixer_value_to_point(ref.id, to_lane(entry, v));
  }
  const core::ParamDesc* d = desc(e, entry);
  return d != nullptr ? seq::value_to_point(*d, v) : std::uint8_t{0};
}

float Controls::from_point(const dsp::Engine& e, const Entry& entry, std::uint8_t point) const noexcept {
  LaneRef ref{kLaneNone, 0};
  if (!lane_ref(entry, ref)) {
    return 0.0f;
  }
  if (ref.target == kLaneMixer) {
    return from_lane(entry, mixer_point_to_value(ref.id, point));
  }
  const core::ParamDesc* d = desc(e, entry);
  return d != nullptr ? seq::point_to_value(*d, point) : 0.0f;
}

void Controls::snap_capture(const dsp::Engine& e, float* out) const noexcept {
  for (unsigned i = 0; i < kSnapValues; ++i) {
    out[i] = i < kSnapTargetCount ? get(e, kSnapTargets[i]) : 0.0f;
  }
}

bool Controls::snap_discrete(const dsp::Engine& e, unsigned index) const noexcept {
  const core::ParamDesc* d = index < kSnapTargetCount ? desc(e, kSnapTargets[index]) : nullptr;
  return d != nullptr && d->unit == ParamUnit::Enum;
}

LaneRef Controls::wobble_target() const noexcept {
  return w_->wobble.target == kWobbleVowel ? LaneRef{kLaneGrowl, static_cast<std::uint8_t>(Growl::Vowel)}
                                           : LaneRef{kLaneVoice, static_cast<std::uint8_t>(Tb303::Cutoff)};
}

void Controls::apply_wobble(bool on) const noexcept {
  if (w_->seq == nullptr) {
    return;
  }
  const WobbleSettings& ws = w_->wobble;
  w_->seq->set_wobble_reset(ws.reset);
  (void)w_->seq->set_wobble(on, ws.rate, ws.shape, static_cast<float>(ws.depth_pct) * 0.01f, wobble_target());
}

void Controls::apply_send(dsp::Engine& e) const noexcept {
  // Throwing the delay opens the send to unity (or leaves it where it is, if it is already higher).
  float gain = w_->send_a_pct * 0.01f;
  if (w_->throw_on && gain < 1.0f) {
    gain = 1.0f;
  }
  (void)e.set_mix(0, dsp::MixField::SendA, gain);
}

void Controls::apply_level(dsp::Engine& e) const noexcept {
  const float db = w_->level_db;
  float gain = db <= -59.5f ? 0.0f : std::pow(10.0f, db * 0.05f);
  gain = gain > 1.5f ? 1.5f : gain;
  (void)e.set_mix(0, dsp::MixField::Level, gain);
}

// ---- PageKeys ---------------------------------------------------------------------------------------------------------------

void PageKeys::set_page(unsigned index) noexcept {
  if (index >= kPageCount) {
    return;
  }
  page_ = index;
  if (page().entry[latched_].item == Item::Blank) {
    latched_ = 0;
  }
}

void PageKeys::release_all() noexcept {
  if (held_ != 0u && turned_) {
    latched_ = before_hold_;
  }
  held_ = 0;
  turned_ = false;
}

bool PageKeys::input(app::Context& ctx, const app::Input& in, Controls& controls) noexcept {
  if (in.button() && app::is_fkey(in.which())) {
    const unsigned k = app::fkey_index(in.which());
    if (page().entry[k].item == Item::Blank) {
      return true;  // a blank key: taken, does nothing
    }
    if (in.type == app::Input::Type::Press) {
      if (held_ == 0u) {
        before_hold_ = latched_;
        turned_ = false;
      }
      held_ |= 1u << k;
      last_held_ = k;
      latched_ = k;  // a tap latches at the press: the strip shows the new focus at once
      ctx.redraw();
    } else if (in.type == app::Input::Type::Release) {
      held_ &= ~(1u << k);
      if (held_ == 0u && turned_) {
        latched_ = before_hold_;  // it was a hold-and-turn: the latched key comes back
      } else if (held_ != 0u) {
        for (unsigned i = 0; i < kKeys; ++i) {
          if ((held_ >> i & 1u) != 0u) {
            last_held_ = i;
            break;
          }
        }
      }
      ctx.redraw();
    }
    return true;
  }
  if (in.turn()) {
    turn(ctx, controls, in.delta(), in.shift);
    return true;
  }
  return false;
}

void PageKeys::turn(app::Context& ctx, Controls& controls, int detents, bool coarse) noexcept {
  if (detents == 0) {
    return;
  }
  if (held_ != 0u) {
    turned_ = true;
    for (unsigned k = 0; k < kKeys; ++k) {
      if ((held_ >> k & 1u) != 0u) {
        controls.step(ctx.engine(), page().entry[k], detents, coarse);
      }
    }
  } else if (page().entry[latched_].item != Item::Blank) {
    controls.step(ctx.engine(), page().entry[latched_], detents, coarse);
  }
  ctx.redraw();
}

}  // namespace o3_303
