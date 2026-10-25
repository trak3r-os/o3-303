// SPDX-License-Identifier: GPL-3.0-or-later
#include <o3_303/pages.h>

#include <o3sdk/core/scales.h>
#include <o3sdk/units/delay/delay.h>
#include <o3_303/pattern.h>
#include <o3_303/units/distortion/distortion.h>
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
constexpr Entry app_item(Item item, const char* label = nullptr) noexcept { return Entry{item, 0, label}; }

constexpr Page kPages[kPageCount] = {
    {"SOUND",
     {voice(Tb303::Cutoff), voice(Tb303::Reso), voice(Tb303::EnvMod), voice(Tb303::Decay), voice(Tb303::Accent), voice(Tb303::Wave),
      voice(Tb303::Tune), app_item(Item::Level, "VOLUME")}},
    // The design's F6 (the reverb send, W5) and F7 are free this round: F6 is the distortion's Mix, F7 its Active and Type as one key
    // (OFF, GRIT .. FOLD). The reverb send takes F6 when it exists.
    {"FX",
     {dist(Distortion::Drive), dist(Distortion::Tone), delay(units::Delay::Time, "DLY TIME"), delay(units::Delay::Feedback, "DLY FB"),
      app_item(Item::SendA), dist(Distortion::Mix, "DIST MIX"), app_item(Item::DistMode, "DIST"), app_item(Item::Master)}},
    {"SEQ",
     {app_item(Item::Tempo), app_item(Item::Swing), app_item(Item::Length), app_item(Item::Gate), app_item(Item::Transpose, "TRANSP"),
      app_item(Item::Scale), app_item(Item::Density), app_item(Item::AccSlide, "ACC/SLD")}},
};

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
    case Item::Blank:
      break;
  }
}

void Controls::step(dsp::Engine& e, const Entry& entry, int detents, bool coarse) noexcept {
  const core::ParamDesc* d = desc(e, entry);
  if (d == nullptr || detents == 0) {
    return;
  }
  if (entry.item == Item::Voice || entry.item == Item::Dist || entry.item == Item::Delay) {
    const dsp::UnitId unit = entry.item == Item::Voice ? w_->voice : (entry.item == Item::Dist ? w_->dist : w_->delay);
    (void)e.step_param(unit, entry.id, detents, coarse);
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

bool Controls::lane(const Entry& entry, unsigned& out) const noexcept {
  if (entry.item == Item::Voice && entry.id < kMotionLanes) {
    out = entry.id;
    return true;
  }
  return false;
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
