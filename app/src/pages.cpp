// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/pages.h>

#include <o3sdk/core/scales.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/pattern.h>
#include <o3_303/perform.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/freeze/freeze.h>
#include <o3_303/units/growl/growl.h>
#include <o3_303/units/tract/tract.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cmath>

namespace o3_303 {

namespace {

using namespace o3sdk;
using core::ParamCurve;
using core::ParamUnit;

// ---- The pages --------------------------------------------------------------------------------------------------------------

constexpr Entry voice(AcidVoice::Param id) noexcept { return Entry{Item::Voice, static_cast<std::uint16_t>(id), nullptr}; }

// OVERDRIVE at the bottom of its range feeds no oscillator into the filter at all (acid_voice's od_gain, the Devil Fish's pot), so the
// voice is nearly silent: the value reads OFF there, not "-24.0 dB", which looks like a level that still sounds.
bool overdrive_off(const core::ParamDesc& d, const Entry& entry, float value) noexcept {
  return entry.item == Item::Voice && entry.id == AcidVoice::Overdrive && value <= d.min;
}
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

// The global table: HOME's knobs (SOUND, on Rig::sound_keys) and PERFORM SETUP's keys. The INSTRUMENT, MODDED, GROWL and WOBBLE pages are the five
// tables below (pages.h), one each; the screens take their keys from them, in this order.
constexpr Page kPages[kPageCount] = {
    {"SOUND",
     {voice(AcidVoice::Cutoff), voice(AcidVoice::Reso), voice(AcidVoice::EnvMod), voice(AcidVoice::Decay), voice(AcidVoice::Accent), voice(AcidVoice::Wave),
      voice(AcidVoice::Tune), app_item(Item::Level, "VOLUME")}},
    // PERFORM SETUP (UX-303 5.4): F1..F4 the colour buttons' assignments (the button's name, its action as the value), F5 what FILL plays, F6 the beat
    // freeze's length, F7 the ROLL mode (the rate and the accelerate in one list), F8 STOP's time.
    {"PERF SETUP",
     {perf(0, "NOTE"), perf(1, "INSTR"), perf(2, "VOL"), perf(3, "FX"), app_item(Item::FillVariant, "FILL"), freeze(Freeze::Length, "FREEZE"),
      app_item(Item::RollMode, "ROLL"), freeze(Freeze::StopTime, "STOP")}},
};

// PATTERN SETUP (UX-303 5.2): this pattern's four, then the generator's four (project-wide, D1).
constexpr Page kSetupPage = {"SETUP",
                             {app_item(Item::Length), app_item(Item::Swing), app_item(Item::Gate), app_item(Item::Transpose, "TRANSP"), app_item(Item::Scale),
                              app_item(Item::Density), app_item(Item::AccPct), app_item(Item::SldPct)}};

// What a snapshot holds (pages.h): SOUND's seven (the voice's own Volume stays at 0 dB; VOLUME is the level), the distortion's five, the
// delay's four, the level and the send, then (P2) the growl's four, then (MODS S2) the voice's mods in id order and the growl's three others: append, never insert (a saved
// project's SNAP is read by position).
constexpr Entry kSnapTargets[kSnapTargetCount] = {
    voice(AcidVoice::Cutoff), voice(AcidVoice::Reso), voice(AcidVoice::EnvMod), voice(AcidVoice::Decay), voice(AcidVoice::Accent), voice(AcidVoice::Wave), voice(AcidVoice::Tune),
    dist(Distortion::Active), dist(Distortion::Type), dist(Distortion::Drive), dist(Distortion::Tone), dist(Distortion::Mix),
    delay(units::Delay::Time, nullptr), delay(units::Delay::Feedback, nullptr), delay(units::Delay::Tone, nullptr), delay(units::Delay::Mode, nullptr),
    app_item(Item::Level, "VOLUME"), app_item(Item::SendA),
    growl(Growl::Vowel, "VOWEL"), growl(Growl::FormantQ, "FMT Q"), growl(Growl::Mix, "GRL MIX"), growl(Growl::CombMix, "CMB MIX"),
    voice(AcidVoice::AccDecay), voice(AcidVoice::Tracking), voice(AcidVoice::Slide), voice(AcidVoice::SoftAtk), voice(AcidVoice::SubOsc), voice(AcidVoice::FilterFm),
    voice(AcidVoice::AccSweep), voice(AcidVoice::Muffler), voice(AcidVoice::SwpSpeed), voice(AcidVoice::Overdrive), voice(AcidVoice::AmpDecay),
    growl(Growl::FormantMix, "FMT MIX"), growl(Growl::CombTune, "CMB TUNE"), growl(Growl::CombFeedback, "CMB FB"),
    wobble(kWobbleRate, nullptr), wobble(kWobbleShape, nullptr), wobble(kWobbleDepth, nullptr), wobble(kWobbleTarget, nullptr), wobble(kWobbleReset, nullptr),
    app_item(Item::WobOn, "WOB"),
    voice(AcidVoice::Range), voice(AcidVoice::Sustain)};
static_assert(kSnapWobbleFirst == 36, "B5a's six targets follow the first 36");
static_assert(AcidVoice::AmpDecay - AcidVoice::AccDecay == 10, "the snapshot holds the eleven mods, ids 8 to 18, in order");

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
constexpr core::ParamDesc kAccPct = {"ACC %", "%", nullptr, nullptr, 0.0f, 100.0f, 25.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kSldPct = {"SLD %", "%", nullptr, nullptr, 0.0f, 100.0f, 20.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kSendA = {"DLY SEND", "%", nullptr, nullptr, 0.0f, 100.0f, 0.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};
constexpr const char* kDistModeNames[] = {"OFF", "GRIT", "TUBE", "HARD", "FUZZ", "FOLD"};
constexpr core::ParamDesc kDistMode = {"DIST", "", kDistModeNames, nullptr, 0.0f, 5.0f, 1.0f, 5, 1, 6, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kLevel = {"VOLUME", "dB", nullptr, nullptr, -60.0f, 3.5f, -6.0f, 127, 6, 0, ParamUnit::Fixed1, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kMaster = {"MASTER", "%", nullptr, nullptr, 0.0f, 150.0f, 100.0f, 150, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0};

constexpr const char* kFillNames[seq::kFillVariantCount] = {"MUTATE", "OCT UP", "RATCHET", "ACC ROT"};
constexpr core::ParamDesc kFillVariant = {"FILL", "", kFillNames, nullptr, 0.0f, 3.0f, 0.0f, 3, 1, seq::kFillVariantCount, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
// ROLL on PERFORM SETUP: the freeze's Roll Rate and Accelerate as one list. 0..3 are the rates with the accelerate off; the two BUILD places roll
// from 1/8 and halve the rate every beat or bar (Freeze::Accelerate). "BLD" because a value is eight characters wide in the knob box.
constexpr unsigned kRollModeCount = 6;
constexpr unsigned kRollBuildBeat = 4;  // the places 4 and 5 are BUILD BEAT and BUILD BAR
constexpr const char* kRollModeNames[kRollModeCount] = {"1/8", "1/16", "1/32", "1/64", "BLD BEAT", "BLD BAR"};
constexpr core::ParamDesc kRollMode = {"ROLL", "", kRollModeNames, nullptr, 0.0f, static_cast<float>(kRollModeCount - 1u), 1.0f,
                                       static_cast<std::uint16_t>(kRollModeCount - 1u), 1, static_cast<std::uint8_t>(kRollModeCount), ParamUnit::Enum,
                                       ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kPerfAssign = {"ACTION", "", kPerfActionNames, nullptr, 0.0f, static_cast<float>(kPerfActionCount - 1u), 0.0f,
                                         static_cast<std::uint16_t>(kPerfActionCount - 1u), 1, static_cast<std::uint8_t>(kPerfActionCount),
                                         ParamUnit::Enum, ParamCurve::Linear, 0, 0};

// The wobble's settings (M3c W); the order is WobbleField's. Rate and shape are the sequencer's own tables (seq/wobble.h).
constexpr const char* kOffOnNames[] = {"OFF", "ON"};
constexpr const char* kWobbleTargetNames[kWobbleTargetCount] = {"CUTOFF", "VOWEL"};
// WOBBLE's F8 key (Item::WobOn): the action's name and its two places; the value is the screen's (the latch), not a setting.
constexpr core::ParamDesc kGrowlOn = {"GROWL", "", kOffOnNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kTractOn = {"TRACT", "", kOffOnNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kModsKey = {"MODS", "", kOffOnNames, nullptr, 0.0f, 1.0f, 1.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kWobOn = {"WOB", "", kOffOnNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0};
constexpr core::ParamDesc kWobbleDescs[kWobbleFieldCount] = {
    {"RATE", "", seq::kWobbleRateNames, nullptr, 0.0f, static_cast<float>(seq::kWobbleRateCount - 1u), static_cast<float>(seq::kWobble1_8),
     static_cast<std::uint16_t>(seq::kWobbleRateCount - 1u), 1, static_cast<std::uint8_t>(seq::kWobbleRateCount), ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"SHAPE", "", seq::kWobbleShapeNames, nullptr, 0.0f, static_cast<float>(seq::kWobbleShapeCount - 1u), static_cast<float>(seq::kWobbleSine),
     static_cast<std::uint16_t>(seq::kWobbleShapeCount - 1u), 1, static_cast<std::uint8_t>(seq::kWobbleShapeCount), ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"DEPTH", "%", nullptr, nullptr, 0.0f, 100.0f, 50.0f, 100, 10, 0, ParamUnit::Int, ParamCurve::Linear, 0, 0},
    {"RESET", "", kOffOnNames, nullptr, 0.0f, 1.0f, 0.0f, 1, 1, 2, ParamUnit::Enum, ParamCurve::Linear, 0, 0},
    {"TARGET", "", kWobbleTargetNames, nullptr, 0.0f, static_cast<float>(kWobbleTargetCount - 1u), 0.0f, static_cast<std::uint16_t>(kWobbleTargetCount - 1u), 1,
     static_cast<std::uint8_t>(kWobbleTargetCount), ParamUnit::Enum, ParamCurve::Linear, 0, 0},
};

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

// The tables of the INSTRUMENT screen's groups (MODS-PLAN 5): each page is one constexpr table, declared in pages.h. VOICE has none (it is SOUND, above).
// DLY MODE is MASTER's F5; the growl's FMT MIX, CMB TUNE and CMB FB and the wobble's RESET, which were Config's page DEEP, are keys of GROWL and WOBBLE
// (docs/MODS-PLAN.md 5.4, 5.5, 7).
constexpr Page kFxPage = {"FX",
                          {dist(Distortion::Drive), dist(Distortion::Tone), dist(Distortion::Mix, "MIX"), app_item(Item::DistMode, "MODEL"), delay(units::Delay::Time, "DLY TIME"),
                           delay(units::Delay::Feedback, "DLY FB"), app_item(Item::SendA, "DLY SEND"), delay(units::Delay::Tone, "DLY TONE")}};
// MODDED (5.3): NOTES is how each note moves (DECAY, the SOUND knob itself, sits beside ACC DECAY), TONE what the filter is fed and gives. F8 is the MODS action key on both pages (lit while the mods are heard; off is the stock voice).
constexpr Page kNotesPage = {"NOTES",
                             {voice(AcidVoice::SoftAtk), voice(AcidVoice::Decay), voice(AcidVoice::AccDecay), voice(AcidVoice::AmpDecay), voice(AcidVoice::Slide), voice(AcidVoice::AccSweep),
                              voice(AcidVoice::SwpSpeed), app_item(Item::Mods, "MODS")}};
constexpr Page kTonePage = {"TONE",
                            {voice(AcidVoice::Tracking), voice(AcidVoice::FilterFm), voice(AcidVoice::Overdrive), voice(AcidVoice::Muffler), voice(AcidVoice::SubOsc), voice(AcidVoice::Range), voice(AcidVoice::Sustain),
                             app_item(Item::Mods, "MODS")}};
// The growl in signal order: the formant, the comb, then the growl's mix. F8 is the GROWL key: Mix to 0 and back to its last non-zero value (B5a).
constexpr Page kGrowlPage = {"GROWL",
                             {growl(Growl::Vowel, "VOWEL"), growl(Growl::FormantQ, "FMT Q"), growl(Growl::FormantMix, "FMT MIX"), growl(Growl::CombTune, "CMB TUNE"),
                              growl(Growl::CombFeedback, "CMB FB"), growl(Growl::CombMix, "CMB MIX"), growl(Growl::Mix, "GRL MIX"), app_item(Item::GrowlOn, "GROWL")}};
// The wobble's five settings (the label is TARGET: the big knob row draws nine characters). F6 and F7 are free; F8 is the WOB action key (5.5).
constexpr Page kWobblePage = {"WOBBLE",
                              {wobble(kWobbleRate, "RATE"), wobble(kWobbleShape, "SHAPE"), wobble(kWobbleDepth, "DEPTH"), wobble(kWobbleTarget, "TARGET"), wobble(kWobbleReset, "RESET"),
                               Entry{}, Entry{}, app_item(Item::WobOn, "WOB")}};

const Page& page_at(unsigned index) noexcept { return kPages[index < kPageCount ? index : 0u]; }
const Page& setup_page() noexcept { return kSetupPage; }
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
    case Item::AccPct:
      return &kAccPct;
    case Item::SldPct:
      return &kSldPct;
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
    case Item::FillVariant:
      return &kFillVariant;
    case Item::Perf:
      return entry.id < kPerfButtons ? &kPerfAssign : nullptr;
    case Item::RollMode:
      return &kRollMode;
    case Item::Growl:
      return e.param_desc(w_->growl, entry.id);
    case Item::Wobble:
      return entry.id < kWobbleFieldCount ? &kWobbleDescs[entry.id] : nullptr;
    case Item::Mods:
      return &kModsKey;
    case Item::GrowlOn:
      return &kGrowlOn;
    case Item::Tract:
      return e.param_desc(w_->tract, entry.id);
    case Item::TractOn:
      return &kTractOn;
    case Item::WobOn:
      return &kWobOn;
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
      return static_cast<float>(e.own_tempo()) * 0.001f;  // the app's own: while following a MIDI clock the header shows the master's
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
    case Item::AccPct:
      return static_cast<float>(w_->gen.accent_pct);
    case Item::SldPct:
      return static_cast<float>(w_->gen.slide_pct);
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
    case Item::FillVariant:
      return w_->seq != nullptr ? static_cast<float>(w_->seq->fill_variant()) : 0.0f;
    case Item::Perf:
      return entry.id < kPerfButtons ? static_cast<float>(w_->perf[entry.id]) : 0.0f;
    case Item::RollMode: {
      const int accel = round_to_int(e.param(w_->freeze, Freeze::Accelerate));
      if (accel > 0) {  // BUILD BEAT (Accelerate BEAT) or BUILD BAR
        return static_cast<float>(kRollBuildBeat + static_cast<unsigned>(accel > 2 ? 1 : accel - 1));
      }
      const int rate = round_to_int(e.param(w_->freeze, Freeze::RollRate));
      return static_cast<float>(rate < 0 ? 0 : (rate > 3 ? 3 : rate));
    }
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
    case Item::Mods:
      return w_->stock ? 0.0f : 1.0f;  // the mods are heard when Stock is off
    case Item::GrowlOn:
      return e.param(w_->growl, Growl::Mix) > 0.0f ? 1.0f : 0.0f;
    case Item::Tract:
      return e.param(w_->tract, entry.id);
    case Item::TractOn:
      return e.param(w_->tract, TractFx::Mix) > 0.0f ? 1.0f : 0.0f;
    case Item::WobOn:
      return w_->seq != nullptr && w_->seq->wobble().on ? 1.0f : 0.0f;
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
      (void)w_->bank->resize(sel, kLengths[static_cast<unsigned>(round_to_int(v)) % kLengthCount]);  // a step up copies the line into the new part (one undo)
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
    case Item::AccPct:
      w_->gen.accent_pct = static_cast<std::uint8_t>(round_to_int(v));
      break;
    case Item::SldPct:
      w_->gen.slide_pct = static_cast<std::uint8_t>(round_to_int(v));
      break;
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
    case Item::RollMode: {
      const unsigned mode = static_cast<unsigned>(round_to_int(v));
      if (mode >= kRollBuildBeat) {  // BUILD starts at 1/8 and speeds up every beat or bar
        (void)e.set_param(w_->freeze, Freeze::RollRate, static_cast<float>(Freeze::kRoll8));
        (void)e.set_param(w_->freeze, Freeze::Accelerate, static_cast<float>(mode == kRollBuildBeat ? Freeze::kAccelBeat : Freeze::kAccelBar));
      } else {
        (void)e.set_param(w_->freeze, Freeze::RollRate, static_cast<float>(mode));
        (void)e.set_param(w_->freeze, Freeze::Accelerate, static_cast<float>(Freeze::kAccelOff));
      }
      break;
    }
    case Item::Growl:
      (void)e.set_param(w_->growl, entry.id, v);  // GROWL's F8 reads the Mix from the engine when it turns the growl off
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
    case Item::Mods:
      if (e.set_param(w_->voice, AcidVoice::Stock, v >= 0.5f ? 0.0f : 1.0f)) {  // the mirror follows only what the engine took
        w_->stock = v < 0.5f;
      }
      break;
    case Item::GrowlOn: {
      const float mix = e.param(w_->growl, Growl::Mix);
      if (v >= 0.5f) {
        (void)e.set_param(w_->growl, Growl::Mix, mix > 0.0f ? mix : w_->growl_last_mix);
      } else {
        if (mix > 0.0f) {
          w_->growl_last_mix = mix;
        }
        (void)e.set_param(w_->growl, Growl::Mix, 0.0f);
      }
      break;
    }
    case Item::Tract:
      (void)e.set_param(w_->tract, entry.id, v);
      break;
    case Item::TractOn: {
      const float mix = e.param(w_->tract, TractFx::Mix);
      if (v >= 0.5f) {
        (void)e.set_param(w_->tract, TractFx::Mix, mix > 0.0f ? mix : w_->tract_last_mix);
      } else {
        if (mix > 0.0f) {
          w_->tract_last_mix = mix;
        }
        (void)e.set_param(w_->tract, TractFx::Mix, 0.0f);
      }
      break;
    }
    case Item::WobOn:  // the latch is LiveActions'; a load or a recall asks for it (LiveActions::update takes the request), the key itself uses toggle_latch
      w_->wob_want = v >= 0.5f ? 1 : 0;
      break;
    case Item::Blank:
      break;
  }
}

int press_detents(const core::ParamDesc& d) noexcept {
  if (d.names != nullptr || d.detents < 20u) {
    return 1;
  }
  return static_cast<int>(d.detents / 20u);
}

int Controls::press_detents(const dsp::Engine& e, const Entry& entry) const noexcept {
  const core::ParamDesc* d = desc(e, entry);
  return d != nullptr ? o3_303::press_detents(*d) : 1;
}

bool Controls::continuous(const dsp::Engine& e, const Entry& entry) const noexcept {
  const core::ParamDesc* d = desc(e, entry);
  return d != nullptr && d->names == nullptr && d->detents >= 20u;
}

bool Controls::live_span(const dsp::Engine& e, const Entry& entry, KnobSpan& out) const noexcept {
  if (entry.item != Item::Voice || entry.id != AcidVoice::Cutoff) {
    return false;
  }
  if (!w_->stock && e.param(w_->voice, AcidVoice::Range) >= 0.5f) {  // FISH: the knob's whole range sounds
    return false;
  }
  out.lo = AcidVoice::kCutoffLiveLoHz;
  out.hi = AcidVoice::kCutoffLiveHiHz;
  return true;
}

float Controls::shown(const dsp::Engine& e, const Entry& entry) const noexcept {
  const float v = get(e, entry);
  KnobSpan s;
  if (!live_span(e, entry, s)) {
    return v;
  }
  return v < s.lo ? s.lo : (v > s.hi ? s.hi : v);
}

float Controls::shown_norm(const dsp::Engine& e, const Entry& entry) const noexcept {
  const core::ParamDesc* d = desc(e, entry);
  if (d == nullptr) {
    return 0.0f;
  }
  KnobSpan s;
  if (!live_span(e, entry, s)) {
    return core::param_norm(*d, get(e, entry));
  }
  const float n0 = core::param_norm(*d, s.lo);
  const float n1 = core::param_norm(*d, s.hi);
  const float n = (core::param_norm(*d, shown(e, entry)) - n0) / (n1 - n0);
  return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

void Controls::step(dsp::Engine& e, const Entry& entry, int detents, bool coarse) noexcept {
  const core::ParamDesc* d = desc(e, entry);
  if (d == nullptr || detents == 0) {
    return;
  }
  if (entry.item == Item::Voice || entry.item == Item::Dist || entry.item == Item::Delay || entry.item == Item::Freeze || entry.item == Item::Growl || entry.item == Item::Tract) {
    const dsp::UnitId unit = entry.item == Item::Voice
                                 ? w_->voice
                                 : (entry.item == Item::Dist ? w_->dist
                                                             : (entry.item == Item::Delay ? w_->delay : (entry.item == Item::Freeze ? w_->freeze : (entry.item == Item::Growl ? w_->growl : w_->tract))));
    KnobSpan span;
    const bool bounded = live_span(e, entry, span);  // F1: the travel ends where the voice stops answering
    const float before = bounded ? get(e, entry) : 0.0f;
    (void)e.step_param(unit, entry.id, detents, coarse);
    if (bounded) {
      float v = get(e, entry);
      if (detents > 0) {  // up: not past the top (a stored value above it stays), and a value below the floor comes to it
        const float cap = before > span.hi ? before : span.hi;
        v = v > cap ? cap : (v < span.lo ? span.lo : v);
      } else {
        const float floor = before < span.lo ? before : span.lo;
        v = v < floor ? floor : (v > span.hi ? span.hi : v);
      }
      set(e, entry, v);
    }
    return;
  }
  const int n = coarse ? detents * static_cast<int>(d->coarse) : detents;
  set(e, entry, core::param_step(*d, get(e, entry), n));
}

unsigned Controls::format(const dsp::Engine& e, const Entry& entry, char* out, unsigned cap) const noexcept {
  const core::ParamDesc* d = desc(e, entry);
  if (d == nullptr || cap == 0u) {
    if (cap != 0u) {
      out[0] = '\0';
    }
    return 0;
  }
  if (overdrive_off(*d, entry, get(e, entry))) {
    static constexpr char kOff[] = "OFF";
    unsigned n = 0;
    for (; n + 1u < cap && kOff[n] != '\0'; ++n) {
      out[n] = kOff[n];
    }
    out[n] = '\0';
    return n;
  }
  return core::param_format(*d, shown(e, entry), out, cap);
}

const char* Controls::unit_text(const dsp::Engine& e, const Entry& entry) const noexcept {
  const core::ParamDesc* d = desc(e, entry);
  if (d == nullptr || overdrive_off(*d, entry, get(e, entry))) {
    return "";
  }
  return d->unit_text != nullptr ? d->unit_text : "";
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
      return entry.id < seq::kKnobs && entry.id != kLaneVoiceStock;  // Stock (the switch over the mods) is no lane's target
    case Item::Dist:
      out = LaneRef{kLaneDist, static_cast<std::uint8_t>(entry.id)};
      return entry.id < seq::kKnobs;
    case Item::Delay:
      out = LaneRef{kLaneDelay, static_cast<std::uint8_t>(entry.id)};
      return entry.id < seq::kKnobs;
    case Item::Growl:
      out = LaneRef{kLaneGrowl, static_cast<std::uint8_t>(entry.id)};
      return entry.id < seq::kKnobs;
    case Item::Tract:
      out = LaneRef{kLaneTract, static_cast<std::uint8_t>(entry.id)};
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
    case kLaneTract:
      return Entry{Item::Tract, ref.id, nullptr};
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
                                           : LaneRef{kLaneVoice, static_cast<std::uint8_t>(AcidVoice::Cutoff)};
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

void PageKeys::set_table(const Page* table, unsigned count) noexcept {
  table_ = count != 0u ? table : nullptr;
  count_ = count;
  page_ = 0;
  latched_ = 0;
  before_hold_ = 0;
  held_ = 0;
  last_held_ = 0;
  turned_ = false;
}

void PageKeys::set_page(unsigned index) noexcept {
  if (index >= page_count()) {
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

bool PageKeys::arrow_turn(const app::Input& in, app::Input& out) noexcept {
  if (held_ == 0u || in.shift || in.type != app::Input::Type::Press || !(in.is(app::Button::Left) || in.is(app::Button::Right))) {
    return false;
  }
  out = in;
  out.type = app::Input::Type::Turn;
  out.kind = core::InputKind::Encoder;
  out.id = 0;
  out.value = in.is(app::Button::Right) ? 1 : -1;
  out.shift = false;
  press_ = true;
  return true;
}

int PageKeys::lock_detents(app::Context& ctx, const Controls& controls, const Entry& entry, int detents, bool coarse) noexcept {
  if (press_) {
    return (detents < 0 ? -1 : 1) * controls.press_detents(ctx.engine(), entry);
  }
  if (coarse || detents == 0 || !controls.continuous(ctx.engine(), entry)) {
    return detents;  // an enum, Shift's coarse steps: the curve is not asked
  }
  return scaled(ctx, detents);
}

int PageKeys::scaled(app::Context& ctx, int detents) noexcept {
  if (!(have_fast_ && fast_raw_ == detents && fast_at_ == ctx.now())) {  // once per turn event: the run tracker counts each detent report once
    fast_ = ctx.enc_scale(detents);
    fast_raw_ = detents;
    fast_at_ = ctx.now();
    have_fast_ = true;
  }
  return fast_;
}

void PageKeys::turn(app::Context& ctx, Controls& controls, int detents, bool coarse) noexcept {
  if (detents == 0) {
    return;
  }
  const auto apply = [&](const Entry& entry) {
    const int n = press_ ? (detents < 0 ? -1 : 1) * controls.press_detents(ctx.engine(), entry) : (!coarse && controls.continuous(ctx.engine(), entry) ? scaled(ctx, detents) : detents);
    controls.step(ctx.engine(), entry, n, coarse);
  };
  if (held_ != 0u) {
    turned_ = true;
    for (unsigned k = 0; k < kKeys; ++k) {
      if ((held_ >> k & 1u) != 0u) {
        apply(page().entry[k]);
      }
    }
  } else if (page().entry[latched_].item != Item::Blank) {
    apply(page().entry[latched_]);
  }
  ctx.redraw();
}

}  // namespace o3_303
