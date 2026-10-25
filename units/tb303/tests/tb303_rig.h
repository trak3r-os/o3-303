// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support: the voice in a dsp::Engine, notes and parameter changes placed at absolute samples by a Clocked hook (as the sequencer
// of the app does), and a few measurements.

#include <o3sdk/dsp/engine.h>

#include <o3_303/units/tb303/tb303.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace tb303_test {

namespace dsp = o3sdk::dsp;
using o3_303::Tb303;

class Script final : public dsp::Clocked {
 public:
  void set_unit(dsp::UnitId u) { unit_ = u; }
  void add(std::uint32_t at, const dsp::Event& e) { items_.push_back({at, e}); }
  void on(std::uint32_t at, int note, std::uint8_t flags = 0) { add(at, dsp::note_on(static_cast<std::uint8_t>(note), (flags & dsp::kNoteAccent) != 0 ? 110 : 80, flags)); }
  void off(std::uint32_t at, int note) { add(at, dsp::note_off(static_cast<std::uint8_t>(note))); }
  void param(std::uint32_t at, std::uint16_t id, float v) { add(at, dsp::param_change(id, v)); }
  void all_off(std::uint32_t at) { add(at, dsp::all_notes_off()); }

  void block(dsp::BlockContext& ctx) noexcept override {
    const std::uint32_t frames = ctx.transport().frames;
    for (const Item& it : items_) {
      if (it.at >= pos_ && it.at < pos_ + frames) {
        dsp::Event e = it.e;
        e.frame = static_cast<std::uint16_t>(it.at - pos_);
        (void)ctx.emit(unit_, e);
      }
    }
    pos_ += frames;
  }

 private:
  struct Item {
    std::uint32_t at;
    dsp::Event e;
  };
  std::vector<Item> items_;
  dsp::UnitId unit_ = dsp::kNoUnit;
  std::uint32_t pos_ = 0;
};

// One voice in slot 0 of an engine, with its script. Parameters set before start() are delivered as the engine does at start().
struct Rig {
  dsp::Engine engine;
  Tb303 voice;
  Script script;
  dsp::UnitId id;

  Rig() : id(engine.attach(voice)) {
    script.set_unit(id);
    (void)engine.set_source(0, id);
    (void)engine.add_hook(script);
  }
  void set(std::uint16_t p, float v) { (void)engine.set_param(id, p, v); }

  // Renders `total` samples in blocks of `block` frames; the left channel.
  std::vector<float> render(std::uint32_t total, std::uint32_t block = 64) {
    if (!engine.started()) {
      engine.start();
    }
    std::vector<float> out;
    out.reserve(total);
    float l[dsp::kMaxBlockFrames];
    float r[dsp::kMaxBlockFrames];
    for (std::uint32_t done = 0; done < total; done += block) {
      const std::uint32_t n = (total - done) < block ? (total - done) : block;
      engine.render(nullptr, nullptr, l, r, n);
      out.insert(out.end(), l, l + n);
    }
    return out;
  }
};

inline double rms(const std::vector<float>& v, std::size_t from, std::size_t to) {
  double s = 0.0;
  for (std::size_t i = from; i < to && i < v.size(); ++i) {
    s += static_cast<double>(v[i]) * static_cast<double>(v[i]);
  }
  return std::sqrt(s / static_cast<double>(to - from));
}
inline double peak(const std::vector<float>& v, std::size_t from, std::size_t to) {
  double p = 0.0;
  for (std::size_t i = from; i < to && i < v.size(); ++i) {
    p = std::fabs(static_cast<double>(v[i])) > p ? std::fabs(static_cast<double>(v[i])) : p;
  }
  return p;
}
// RMS of the first difference: a measure of brightness.
inline double bright(const std::vector<float>& v, std::size_t from, std::size_t to) {
  double s = 0.0;
  for (std::size_t i = from + 1; i < to && i < v.size(); ++i) {
    const double d = static_cast<double>(v[i]) - static_cast<double>(v[i - 1]);
    s += d * d;
  }
  return std::sqrt(s / static_cast<double>(to - from));
}
// Upward zero crossings per second.
inline double crossings_hz(const std::vector<float>& v, std::size_t from, std::size_t to, double rate = 44100.0) {
  unsigned n = 0;
  for (std::size_t i = from + 1; i < to && i < v.size(); ++i) {
    n += (v[i - 1] < 0.0f && v[i] >= 0.0f) ? 1u : 0u;
  }
  return static_cast<double>(n) * rate / static_cast<double>(to - from);
}

// Writes interleaved 16-bit stereo PCM to $O3_303_WAV_DIR/<name> when the variable is set. Returns whether it wrote.
inline bool write_wav(const char* name, const std::vector<float>& l, const std::vector<float>& r, std::uint32_t rate = 44100) {
  const char* dir = std::getenv("O3_303_WAV_DIR");
  if (dir == nullptr || dir[0] == '\0') {
    return false;
  }
  char path[512];
  std::snprintf(path, sizeof(path), "%s/%s", dir, name);
  std::FILE* f = std::fopen(path, "wb");
  if (f == nullptr) {
    return false;
  }
  const std::uint32_t frames = static_cast<std::uint32_t>(l.size());
  const std::uint32_t bytes = frames * 4u;
  auto u32 = [&](std::uint32_t v) {
    const unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
    std::fwrite(b, 1, 4, f);
  };
  auto u16 = [&](std::uint16_t v) {
    const unsigned char b[2] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)};
    std::fwrite(b, 1, 2, f);
  };
  std::fwrite("RIFF", 1, 4, f);
  u32(36u + bytes);
  std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16);
  u16(1);
  u16(2);
  u32(rate);
  u32(rate * 4u);
  u16(4);
  u16(16);
  std::fwrite("data", 1, 4, f);
  u32(bytes);
  for (std::uint32_t i = 0; i < frames; ++i) {
    for (const float s : {l[i], r[i]}) {
      const float c = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
      u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(c * 32767.0f))));
    }
  }
  std::fclose(f);
  return true;
}

}  // namespace tb303_test
