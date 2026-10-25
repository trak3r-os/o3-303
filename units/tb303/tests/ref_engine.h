// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support: the reference voice and the note scripts of the oracle test.
//
// RefVoice is Replicant 303's SynthEngine without JUCE: the same handleNoteOn / handleNoteOff / renderSamples (third_party/replicant303/
// DSP/SynthEngine.cpp, commit d5369be028399f5a403067b58da2936caa070c0e) driving the pinned Oscillator303, DiodeLadderFilter and
// Envelope303 as they are, minus the distortion (a separate unit), the scope tap and the pitch wheel. Host only: it computes with the
// original's pow, tan and exp. The transcription is a derived work of the original, which is distributed under the MIT License:
//
//   MIT License
//
//   Copyright (c) 2026 Daniel Rojas Grasset (DanimanX)
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.

#include <Common303.h>
#include <DiodeLadderFilter.h>
#include <Envelope303.h>
#include <Oscillator303.h>

#include <o3sdk/dsp/event.h>
#include <o3sdk/dsp/unit.h>

#include <o3_303/units/tb303/tb303.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tb303_test {

constexpr double kRate = 44100.0;

// A scripted event at an absolute sample.
struct Ev {
  enum Kind : std::uint8_t { On, Off, Param };
  std::uint32_t at;
  Kind kind;
  std::uint8_t note;
  bool accent;
  std::uint16_t id;
  float value;
  bool slide = false;  // NoteOn flag for the port only (the original glides when a note is held)
};

inline Ev on(std::uint32_t at, int note, bool accent, bool slide = false) {
  return Ev{at, Ev::On, static_cast<std::uint8_t>(note), accent, 0, 0.0f, slide};
}
inline Ev off(std::uint32_t at, int note) { return Ev{at, Ev::Off, static_cast<std::uint8_t>(note), false, 0, 0.0f}; }
inline Ev param(std::uint32_t at, std::uint16_t id, float value) {
  return Ev{at, Ev::Param, 0, false, id, value};
}

struct Step {
  int note;      // MIDI
  bool accent = false;
  bool slide = false;  // held into the next step, which glides
  bool rest = false;
};

// Replicant's sequencer timing (PatternSequencer): a step is a sixteenth, the gate is 50 % of it, a slide sends the next NoteOn before
// this note's NoteOff at the same sample, a rest releases the held note.
inline std::vector<Ev> steps_to_events(const std::vector<Step>& steps, double bpm, std::uint32_t start = 0, double gate = 0.5) {
  const double step_len = kRate * 60.0 / bpm / 4.0;
  std::vector<Ev> ev;
  for (std::size_t i = 0; i < steps.size(); ++i) {
    const Step& s = steps[i];
    const auto at = start + static_cast<std::uint32_t>(std::lround(step_len * static_cast<double>(i)));
    const auto next = start + static_cast<std::uint32_t>(std::lround(step_len * static_cast<double>(i + 1)));
    if (s.rest) {
      continue;
    }
    const bool glide_next = s.slide && i + 1 < steps.size() && !steps[i + 1].rest;
    ev.push_back(on(at, s.note, s.accent, i > 0 && steps[i - 1].slide && !steps[i - 1].rest));
    if (glide_next) {
      // The NoteOff comes after the next NoteOn at the same sample: emitted by the next step (below) via a marker.
      ev.push_back(off(next, s.note));
    } else {
      ev.push_back(off(at + static_cast<std::uint32_t>(std::lround(step_len * gate)), s.note));
    }
  }
  // Order: by sample; at one sample the NoteOn goes before the NoteOff (the slide rule).
  std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) {
    if (a.at != b.at) {
      return a.at < b.at;
    }
    return a.kind == Ev::On && b.kind == Ev::Off;
  });
  return ev;
}

// The defaults of Replicant's SOUND page, as events at sample 0.
inline std::vector<Ev> defaults() {
  using P = o3_303::Tb303;
  return {param(0, P::Cutoff, 350.0f), param(0, P::Reso, 0.65f),  param(0, P::EnvMod, 0.6f), param(0, P::Decay, 750.0f),
          param(0, P::Accent, 0.7f),   param(0, P::Wave, 0.0f),   param(0, P::Tune, 0.0f),   param(0, P::Volume, -3.0f)};
}

// ---- the reference ------------------------------------------------------------------------------------------------------------

class RefVoice {
 public:
  void prepare(double rate) {
    rate_ = rate;
    osc_.prepare(rate);
    filter_.prepare(rate);
    env_.prepare(rate);
    const float dt = 1.0f / static_cast<float>(rate);
    slide_coeff_ = 1.0f - std::exp(-dt / 0.060f);
  }

  void param(std::uint16_t id, float v) {
    using P = o3_303::Tb303;
    switch (id) {
      case P::Cutoff: filter_.setCutoff(v); break;
      case P::Reso: filter_.setResonance(v); break;
      case P::EnvMod: env_mod_ = v; break;
      case P::Decay: env_.setDecayTime(v * 0.001f); break;
      case P::Accent: accent_amount_ = v; break;
      case P::Wave: osc_.setWaveform(v >= 0.5f ? Replicant303::Waveform::Square : Replicant303::Waveform::Sawtooth); break;
      case P::Tune: osc_.setTuningSemi(v); break;
      case P::Volume: master_gain_ = v <= -59.5f ? 0.0f : std::pow(10.0f, v / 20.0f); break;
      default: break;
    }
  }

  // SynthEngine::handleNoteOn / handleNoteOff.
  void note_on(int note, bool accent) {
    const bool legato = !stack_.empty();
    target_ = Replicant303::midiToFreq(static_cast<float>(note));
    if (!legato) {
      current_ = target_;
    }
    env_.noteOn(accent, legato);
    stack_.push_back(note);
  }
  void note_off(int note) {
    for (auto it = stack_.begin(); it != stack_.end(); ++it) {
      if (*it == note) {
        stack_.erase(it);
        break;
      }
    }
    if (stack_.empty()) {
      env_.noteOff();
    } else {
      target_ = Replicant303::midiToFreq(static_cast<float>(stack_.back()));
    }
  }

  // SynthEngine::renderSamples (bend factor 1).
  void render(float* out, std::uint32_t n) {
    for (std::uint32_t i = 0; i < n; ++i) {
      current_ += (target_ - current_) * slide_coeff_;
      osc_.setFrequency(current_);
      const float raw = osc_.process();
      float vcf = 0.0f, vca = 0.0f, acc = 0.0f;
      env_.process(vcf, vca, acc);
      const float filtered = filter_.process(raw, env_mod_, vcf, acc * accent_amount_);
      const float boost = 1.0f + (acc * accent_amount_ * 0.45f);
      out[i] = filtered * vca * boost * master_gain_;
    }
  }

 private:
  double rate_ = 44100.0;
  Replicant303::Oscillator303 osc_;
  Replicant303::DiodeLadderFilter filter_;
  Replicant303::Envelope303 env_;
  std::vector<int> stack_;
  float env_mod_ = 0.5f;
  float accent_amount_ = 0.7f;
  float master_gain_ = 0.8f;
  float target_ = 110.0f;
  float current_ = 110.0f;
  float slide_coeff_ = 0.0f;
};

// Renders the script through the reference, one event at a time (sample accurate, as SynthEngine::processBlock).
inline std::vector<float> render_ref(const std::vector<Ev>& script, std::uint32_t total) {
  RefVoice v;
  v.prepare(kRate);
  std::vector<float> out(total, 0.0f);
  std::uint32_t done = 0;
  for (const Ev& e : script) {
    const std::uint32_t at = e.at < total ? e.at : total;
    if (at > done) {
      v.render(out.data() + done, at - done);
      done = at;
    }
    switch (e.kind) {
      case Ev::On: v.note_on(e.note, e.accent); break;
      case Ev::Off: v.note_off(e.note); break;
      case Ev::Param: v.param(e.id, e.value); break;
    }
  }
  if (done < total) {
    v.render(out.data() + done, total - done);
  }
  return out;
}

// Renders the script through the unit, in blocks of `block` frames, events at their frames (as the engine delivers them). The parameter
// events at sample 0 are delivered first in a process() of zero frames, as Engine::start() delivers the stored values.
inline std::vector<float> render_unit(o3_303::Tb303& unit, const std::vector<Ev>& script, std::uint32_t total, std::uint32_t block = 64) {
  using namespace o3sdk;
  unit.prepare(static_cast<float>(kRate));
  dsp::Transport tr;
  tr.tempo_mbpm = 125'000;
  std::vector<float> out(total, 0.0f);
  std::vector<float> zero(block, 0.0f);
  float r[256];
  std::size_t next = 0;
  {
    dsp::Event evs[64];
    unsigned n = 0;
    while (next < script.size() && script[next].at == 0 && script[next].kind == Ev::Param && n < 64) {
      evs[n++] = dsp::param_change(script[next].id, script[next].value, 0);
      ++next;
    }
    tr.frames = 0;
    dsp::ProcessArgs start{evs, n, zero.data(), zero.data(), out.data(), r, 0, &tr};
    unit.process(start);
  }
  for (std::uint32_t pos = 0; pos < total; pos += block) {
    const std::uint32_t frames = (total - pos) < block ? (total - pos) : block;
    dsp::Event evs[128];
    unsigned n = 0;
    while (next < script.size() && script[next].at < pos + frames) {
      const Ev& e = script[next++];
      const auto frame = static_cast<std::uint16_t>(e.at > pos ? e.at - pos : 0);
      switch (e.kind) {
        case Ev::On: evs[n++] = dsp::note_on(e.note, e.accent ? 110 : 80, static_cast<std::uint8_t>((e.accent ? dsp::kNoteAccent : 0) | (e.slide ? dsp::kNoteSlide : 0)), frame); break;
        case Ev::Off: evs[n++] = dsp::note_off(e.note, frame); break;
        case Ev::Param: evs[n++] = dsp::param_change(e.id, e.value, frame); break;
      }
    }
    tr.frames = frames;
    dsp::ProcessArgs args{evs, n, zero.data(), zero.data(), out.data() + pos, r, frames, &tr};
    unit.process(args);
  }
  return out;
}

struct Agreement {
  double snr_db;     // 10 log10 (sum ref^2 / sum (unit - ref)^2)
  double max_abs;    // largest |unit - ref|
  double ref_rms;
  double ref_peak;
};

inline Agreement compare(const std::vector<float>& ref, const std::vector<float>& got) {
  double sr = 0.0, se = 0.0, mx = 0.0, pk = 0.0;
  for (std::size_t i = 0; i < ref.size(); ++i) {
    const double r = static_cast<double>(ref[i]);
    const double e = static_cast<double>(got[i]) - r;
    sr += r * r;
    se += e * e;
    mx = std::fabs(e) > mx ? std::fabs(e) : mx;
    pk = std::fabs(r) > pk ? std::fabs(r) : pk;
  }
  Agreement a{};
  a.snr_db = se > 0.0 ? 10.0 * std::log10(sr / se) : 200.0;
  a.max_abs = mx;
  a.ref_rms = std::sqrt(sr / static_cast<double>(ref.size()));
  a.ref_peak = pk;
  return a;
}

}  // namespace tb303_test
