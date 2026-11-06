// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support: note scripts (events at absolute samples, a step line as the sequencer plays it) and a renderer that drives the unit
// directly in blocks, as the engine delivers events. The oracle test's reference is Open303 itself (third_party/open303), not this file.

#include <o3sdk/dsp/event.h>
#include <o3sdk/dsp/unit.h>

#include <o3_303/units/acid_voice/acid_voice.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace acid_voice_test {

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

// The sequencer's timing: a step is a sixteenth, the gate is 50 % of it, a slide sends the next NoteOn before
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

// The voice's own defaults as events at sample 0 (Cutoff 350 Hz, Reso 0.65, Env Mod 0.6, Decay 750 ms, Accent 0.7, Volume -3 dB).
inline std::vector<Ev> defaults() {
  using P = o3_303::AcidVoice;
  return {param(0, P::Cutoff, 350.0f), param(0, P::Reso, 0.65f),  param(0, P::EnvMod, 0.6f), param(0, P::Decay, 750.0f),
          param(0, P::Accent, 0.7f),   param(0, P::Wave, 0.0f),   param(0, P::Tune, 0.0f),   param(0, P::Volume, -3.0f)};
}

// Renders the script through the unit, in blocks of `block` frames, events at their frames (as the engine delivers them). The parameter
// events at sample 0 are delivered first in a process() of zero frames, as Engine::start() delivers the stored values.
inline std::vector<float> render_unit(o3_303::AcidVoice& unit, const std::vector<Ev>& script, std::uint32_t total, std::uint32_t block = 64) {
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

}  // namespace acid_voice_test
