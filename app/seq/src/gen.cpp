// SPDX-License-Identifier: GPL-3.0-or-later
//
// GEN and the vault: a port of Replicant 303's preset generator (third_party/replicant303/Presets/PresetBank.cpp: generatePreset and
// makeFactoryPreset with their name tables and FastRandom; DSP/Pattern.h: makeDefaultPattern), commit
// d5369be028399f5a403067b58da2936caa070c0e, by DanimanX. Replicant 303 is distributed under the MIT License:
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
//
// The random stream, the order of the draws and the arithmetic are the original's, so the same (bank, slot, seed) gives the same
// preset (app/seq/tests/seq_gen_oracle_test.cpp compares all of them). Changes from the original: no JUCE (fixed char buffers instead
// of juce::String, plain structs), the preset is mapped into a Pattern and into parameter values (gen.h).
#include <o3_303/seq/gen.h>

#include <o3_303/seq/rng.h>

#include <cmath>

namespace o3_303::seq::gen {

namespace {

// ---- Replicant's own types (what the generator fills before it is mapped) -----------------------------------------------------

constexpr int kLowestNote = 24;   // C1: Pattern::lowestNote
constexpr int kHighestNote = 48;  // C3: Pattern::highestNote
constexpr int kLineSteps = 16;    // Pattern::maxSteps

struct RStep {
  bool gate;
  int note;
  bool accent;
  bool slide;
};

struct RPreset {
  char name[kNameChars];
  RStep steps[kLineSteps];
  int length;
  Patch patch;
};

template <typename T>
T jlimit(T lower, T upper, T value) noexcept {
  return value < lower ? lower : (upper < value ? upper : value);
}

// ---- Name material ------------------------------------------------------------------------------------------------------------

const char kBankLetters[] = "ABCDEFGH";

const char* const kBankNames[] = {"AMBER", "SUB", "NEON", "ACID", "BLADE", "ROOFTOP", "OFF WORLD", "LAST"};

const char* const kFactoryNames[] = {"Amber Line", "Sub Level", "Neon Run", "Acid Rain", "Blade Pulse", "Rooftop Chase", "Off World", "Last Replicant"};

const char* const kAdjectives[] = {
    "Amber",   "Sub",     "Neon",     "Acid",    "Blade",   "Rooftop", "Off",       "Last",     "Chrome", "Static",   "Velvet",
    "Rust",    "Ghost",   "Silver",   "Crimson", "Electric", "Hollow", "Bright",    "Dark",     "Fever",  "Glass",    "Iron",
    "Lunar",   "Magnetic", "Midnight", "Mirror", "Molten",  "Obsidian", "Phantom",   "Plastic",  "Prism",  "Quartz",   "Radiant",
    "Reactor", "Rebel",   "Redline",  "Satin",   "Scarlet", "Secret",  "Shadow",    "Silent",   "Solar",  "Sonic",    "Spectral",
    "Steel",   "Storm",   "Subzero",  "Sunken",  "Tidal",   "Toxic",   "Turbo",     "Twilight", "Ultraviolet", "Vacuum", "Vapor",
    "Velour",  "Violet",  "Voltage",  "Wired",   "Wraith",  "Zero",    "Zenith",    "Cobalt",   "Carbon"};

const char* const kNouns[] = {
    "Line",    "Level",   "Run",     "Rain",     "Pulse",   "Chase",   "World",   "Replicant", "Circuit", "Signal",  "Engine",
    "Wire",    "Grid",    "Vault",   "Machine",  "Protocol", "Sequence", "District", "Corridor", "Frequency", "Reactor", "Tempo",
    "Voltage", "Current", "Motion",  "Shadow",   "Mirror",  "Ladder",  "Filter",  "Oscillator", "Groove",  "Pattern", "Sector",
    "Tunnel",  "Chamber", "Beacon",  "Antenna",  "Archive", "Bishop",  "Cipher",  "Column",  "Compass",  "Console", "Cortex",
    "Cradle",  "Cursor",  "Delta",   "Divider",  "Driver",  "Echo",    "Ember",   "Envelope", "Fabric",  "Factory", "Frame",
    "Horizon", "Kernel",  "Lantern", "Lattice",  "Matrix",  "Meridian", "Monolith", "Neuron",  "Orbit"};

static_assert(sizeof(kAdjectives) / sizeof(kAdjectives[0]) == 64, "64 adjectives");
static_assert(sizeof(kNouns) / sizeof(kNouns[0]) == 64, "64 nouns");

// Musical material per bank. Every line stays inside C1..C3.
const int kScales[kBanks][8] = {
    {0, 3, 5, 7, 10, 12, 15, 19},  // A minor pentatonic
    {0, 2, 3, 5, 7, 8, 10, 12},    // B natural minor
    {0, 2, 4, 7, 9, 12, 14, 16},   // C major pentatonic
    {0, 1, 3, 5, 6, 8, 10, 12},    // D phrygian
    {0, 3, 5, 6, 7, 10, 12, 15},   // E blues
    {0, 2, 3, 5, 7, 9, 10, 12},    // F dorian
    {0, 2, 3, 5, 7, 8, 11, 12},    // G harmonic minor
    {0, 1, 2, 4, 6, 7, 10, 12},    // H chromatic acid
};

const int kLineLengths[] = {16, 16, 16, 16, 16, 12, 8, 7};

// The factory lines (makeDefaultPattern): notes, then the step indices that accent, slide and have the gate closed (-1 ends a list).
struct DefaultLine {
  int notes[kLineSteps];
  int accents[kLineSteps];
  int slides[kLineSteps];
  int muted[kLineSteps];
};

const DefaultLine kLines[kBanks] = {
    // Amber Line
    {{36, 36, 48, 36, 39, 36, 43, 36, 36, 48, 36, 39, 36, 43, 36, 36},
     {1, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {4, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // Sub Level
    {{24, 24, 36, 24, 27, 24, 31, 24, 24, 36, 24, 27, 31, 24, 24, 24},
     {1, 5, 9, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {7, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // Neon Run
    {{36, 48, 36, 43, 36, 48, 39, 36, 36, 43, 48, 36, 39, 36, 43, 36},
     {1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1},
     {2, 6, 10, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // Acid Rain
    {{24, 36, 24, 31, 24, 36, 27, 24, 31, 36, 24, 27, 24, 31, 36, 24},
     {0, 4, 8, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {1, 5, 9, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {6, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // Blade Pulse
    {{36, 36, 39, 36, 43, 36, 46, 43, 36, 36, 39, 43, 36, 36, 31, 36},
     {3, 7, 11, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {4, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // Rooftop Chase
    {{48, 36, 48, 39, 48, 43, 48, 36, 46, 36, 46, 39, 43, 39, 36, 36},
     {0, 2, 6, 8, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {2, 6, 10, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {4, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // Off World
    {{24, 27, 24, 36, 24, 31, 27, 24, 36, 24, 27, 31, 24, 27, 24, 24},
     {1, 5, 9, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {3, 7, 11, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // Last Replicant
    {{36, 31, 36, 43, 36, 31, 39, 36, 36, 31, 36, 43, 46, 43, 39, 36},
     {0, 3, 6, 9, 12, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {1, 5, 9, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {7, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
};

// ---- Small helpers ------------------------------------------------------------------------------------------------------------

std::uint32_t make_seed(int bank, int slot, std::uint32_t seed) noexcept {
  return seed ^ (static_cast<std::uint32_t>(bank + 1) * 0x9e3779b9u) ^ (static_cast<std::uint32_t>(slot + 1) * 0x85ebca6bu) ^
         0x52443303u;  // "RD303"
}

float log_between(Rng& rng, float low, float high) noexcept { return std::exp(rng.between(std::log(low), std::log(high))); }

// Appends to a NUL-terminated buffer of `cap` bytes (truncating).
void append(char* dst, unsigned cap, const char* text) noexcept {
  unsigned n = 0;
  while (n < cap && dst[n] != '\0') {
    ++n;
  }
  for (unsigned i = 0; text[i] != '\0' && n + 1u < cap; ++i) {
    dst[n++] = text[i];
  }
  if (n < cap) {
    dst[n] = '\0';
  }
}

bool same_text(const char* a, const char* b) noexcept {
  unsigned i = 0;
  while (a[i] != '\0' && a[i] == b[i]) {
    ++i;
  }
  return a[i] == b[i];
}

bool is_factory_name(const char* name) noexcept {
  for (const char* factory_name : kFactoryNames) {
    if (same_text(name, factory_name)) {
      return true;
    }
  }
  return false;
}

// Both multipliers are odd, so for a fixed bank the 64 slots walk the two 64-entry tables without ever repeating a pair.
void make_generated_name(char* out, int bank, int slot) noexcept {
  const auto adjective = static_cast<unsigned>((static_cast<std::uint32_t>(slot) * 7u + static_cast<std::uint32_t>(bank) * 11u) % 64u);
  const auto noun = static_cast<unsigned>((static_cast<std::uint32_t>(slot) * 17u + static_cast<std::uint32_t>(bank) * 23u) % 64u);
  out[0] = '\0';
  append(out, kNameChars, kAdjectives[adjective]);
  append(out, kNameChars, " ");
  append(out, kNameChars, kNouns[noun]);
  if (is_factory_name(out)) {
    append(out, kNameChars, " II");
  }
}

// The defaults of a Replicant preset (the members' initialisers in PresetBank.h).
void default_patch(Patch& p) noexcept {
  p.waveform = 0;
  p.tuning = 0.0f;
  p.cutoff = 350.0f;
  p.resonance = 0.65f;
  p.env_mod = 0.60f;
  p.decay = 0.75f;
  p.accent = 0.70f;
  p.tempo = 125.0f;
  p.swing = 0.0f;
  p.dist_active = true;
  p.dist_type = 0;
  p.dist_drive = 0.50f;
  p.dist_tone = 0.60f;
  p.dist_mix = 0.85f;
}

void default_steps(RPreset& r) noexcept {
  for (RStep& s : r.steps) {
    s = RStep{true, 36, false, false};
  }
  r.length = kLineSteps;
  r.name[0] = '\0';
  default_patch(r.patch);
}

// Preset::sanitise.
void sanitise(RPreset& r) noexcept {
  r.length = jlimit(1, kLineSteps, r.length);
  for (RStep& s : r.steps) {
    s.note = jlimit(kLowestNote, kHighestNote, s.note);
  }
  Patch& p = r.patch;
  p.waveform = jlimit(0, 1, p.waveform);
  p.tuning = jlimit(-12.0f, 12.0f, p.tuning);
  p.cutoff = jlimit(25.0f, 4500.0f, p.cutoff);
  p.resonance = jlimit(0.0f, 1.0f, p.resonance);
  p.env_mod = jlimit(0.0f, 1.0f, p.env_mod);
  p.decay = jlimit(0.20f, 2.50f, p.decay);
  p.accent = jlimit(0.0f, 1.0f, p.accent);
  p.tempo = jlimit(40.0f, 300.0f, p.tempo);
  p.swing = jlimit(0.0f, 0.66f, p.swing);
  p.dist_type = jlimit(0, 4, p.dist_type);
  p.dist_drive = jlimit(0.0f, 1.0f, p.dist_drive);
  p.dist_tone = jlimit(0.0f, 1.0f, p.dist_tone);
  p.dist_mix = jlimit(0.0f, 1.0f, p.dist_mix);
}

// makeDefaultPattern + the factory names, with Replicant's default voice (makeFactoryPreset, slot 0).
void make_factory_line(RPreset& r, int bank) noexcept {
  const int b = jlimit(0, static_cast<int>(kBanks) - 1, bank);
  const DefaultLine& line = kLines[b];
  default_steps(r);
  for (int i = 0; i < kLineSteps; ++i) {
    r.steps[i] = RStep{true, line.notes[i], false, false};
  }
  for (int i = 0; i < kLineSteps; ++i) {
    if (line.accents[i] >= 0) {
      r.steps[line.accents[i]].accent = true;
    }
  }
  for (int i = 0; i < kLineSteps; ++i) {
    if (line.slides[i] >= 0) {
      r.steps[line.slides[i]].slide = true;
    }
  }
  for (int i = 0; i < kLineSteps; ++i) {
    if (line.muted[i] >= 0) {
      r.steps[line.muted[i]].gate = false;
    }
  }
  r.name[0] = '\0';
  append(r.name, kNameChars, kFactoryNames[b]);
  sanitise(r);
}

// PresetBank::generatePreset.
void make_generated(RPreset& r, int bank, int slot, std::uint32_t seed) noexcept {
  const int b = jlimit(0, static_cast<int>(kBanks) - 1, bank);
  const int s = jlimit(0, static_cast<int>(kSlots) - 1, slot);

  Rng rng(make_seed(b, s, seed));

  default_steps(r);
  make_generated_name(r.name, b, s);
  if (s == 0) {
    r.name[0] = '\0';
    append(r.name, kNameChars, kFactoryNames[b]);
    append(r.name, kNameChars, " ");
    const char digit[2] = {static_cast<char>('0' + 1 + static_cast<int>(seed % 9u)), '\0'};
    append(r.name, kNameChars, digit);
  }

  // ---- Acid line
  const int* scale = kScales[b];
  const int root = 24 + rng.next_int(0, 12);
  const int length = kLineLengths[rng.next_int(0, static_cast<int>(sizeof(kLineLengths) / sizeof(kLineLengths[0])) - 1)];

  r.length = length;

  const float density = rng.between(0.62f, 0.94f);
  const float accent_chance = rng.between(0.16f, 0.38f);
  const float slide_chance = rng.between(0.10f, 0.32f);

  int previous_note = root + scale[0];

  for (int i = 0; i < kLineSteps; ++i) {
    RStep& step = r.steps[i];

    step.gate = (i == 0) || rng.chance(density);

    const int degree = rng.next_int(0, 7);
    int octave = 0;

    if (rng.chance(0.16f)) {
      octave = 12;
    }
    if (rng.chance(0.08f)) {
      octave = -12;
    }

    int note = root + scale[degree] + octave;

    // Repeated notes are the backbone of a 303 line; they also give slides something meaningful to glide from.
    if (rng.chance(0.12f)) {
      note = previous_note;
    }

    step.note = jlimit(kLowestNote, kHighestNote, note);
    previous_note = step.note;

    step.accent = step.gate && rng.chance(accent_chance);
    step.slide = step.gate && rng.chance(slide_chance);
  }

  // A generated line must always have enough material to be a pattern.
  int gates = 0;
  for (int i = 0; i < length; ++i) {
    if (r.steps[i].gate) {
      ++gates;
    }
  }

  for (int guard = 0; guard < 64 && gates < 4; ++guard) {
    const int i = rng.next_int(0, length - 1);
    if (!r.steps[i].gate) {
      r.steps[i].gate = true;
      ++gates;
    }
  }

  bool all_same_note = true;
  for (int i = 1; i < length; ++i) {
    if (r.steps[i].note != r.steps[0].note) {
      all_same_note = false;
    }
  }

  if (all_same_note) {
    r.steps[length / 2].note = jlimit(kLowestNote, kHighestNote, r.steps[0].note + scale[3]);
    r.steps[length - 1].note = jlimit(kLowestNote, kHighestNote, r.steps[0].note + scale[5]);
  }

  bool any_accent = false;
  for (int i = 0; i < length; ++i) {
    any_accent = any_accent || r.steps[i].accent;
  }

  if (!any_accent) {
    r.steps[0].accent = true;
  }

  // ---- Voice archetypes. The bank/slot pair walks eight distinct characters so a bank never feels like 64 variations of the same
  // patch.
  Patch& p = r.patch;
  const int archetype = (b + s) % 8;

  switch (archetype) {
    case 0:  // Classic acid
      p.waveform = 0;
      p.cutoff = log_between(rng, 170.0f, 900.0f);
      p.resonance = rng.between(0.55f, 0.84f);
      p.env_mod = rng.between(0.50f, 0.82f);
      p.decay = rng.between(0.38f, 0.90f);
      p.accent = rng.between(0.60f, 0.85f);
      p.dist_active = true;
      p.dist_type = 0;
      p.dist_drive = rng.between(0.28f, 0.62f);
      p.dist_tone = rng.between(0.45f, 0.75f);
      p.dist_mix = rng.between(0.62f, 0.90f);
      break;

    case 1:  // Sub pressure
      p.waveform = 0;
      p.cutoff = log_between(rng, 70.0f, 380.0f);
      p.resonance = rng.between(0.30f, 0.68f);
      p.env_mod = rng.between(0.30f, 0.68f);
      p.decay = rng.between(0.65f, 1.45f);
      p.accent = rng.between(0.55f, 0.82f);
      p.dist_active = true;
      p.dist_type = rng.chance(0.70f) ? 0 : 2;
      p.dist_drive = rng.between(0.18f, 0.48f);
      p.dist_tone = rng.between(0.25f, 0.55f);
      p.dist_mix = rng.between(0.55f, 0.82f);
      break;

    case 2:  // Bright / open
      p.waveform = rng.chance(0.75f) ? 1 : 0;
      p.cutoff = log_between(rng, 800.0f, 2600.0f);
      p.resonance = rng.between(0.35f, 0.78f);
      p.env_mod = rng.between(0.68f, 0.95f);
      p.decay = rng.between(0.20f, 0.52f);
      p.accent = rng.between(0.50f, 0.80f);
      p.dist_active = true;
      p.dist_type = rng.chance(0.65f) ? 1 : 0;
      p.dist_drive = rng.between(0.15f, 0.42f);
      p.dist_tone = rng.between(0.62f, 0.90f);
      p.dist_mix = rng.between(0.35f, 0.68f);
      break;

    case 3:  // Screamer
      p.waveform = 0;
      p.cutoff = log_between(rng, 280.0f, 1300.0f);
      p.resonance = rng.between(0.68f, 0.94f);
      p.env_mod = rng.between(0.58f, 0.90f);
      p.decay = rng.between(0.22f, 0.62f);
      p.accent = rng.between(0.65f, 0.92f);
      p.dist_active = true;
      p.dist_type = rng.chance(0.60f) ? 2 : 0;
      p.dist_drive = rng.between(0.58f, 0.92f);
      p.dist_tone = rng.between(0.40f, 0.78f);
      p.dist_mix = rng.between(0.72f, 1.0f);
      break;

    case 4:  // Germanium fuzz
      p.waveform = rng.chance(0.70f) ? 1 : 0;
      p.cutoff = log_between(rng, 200.0f, 1000.0f);
      p.resonance = rng.between(0.45f, 0.80f);
      p.env_mod = rng.between(0.48f, 0.86f);
      p.decay = rng.between(0.30f, 0.85f);
      p.accent = rng.between(0.55f, 0.85f);
      p.dist_active = true;
      p.dist_type = 3;
      p.dist_drive = rng.between(0.48f, 0.90f);
      p.dist_tone = rng.between(0.30f, 0.68f);
      p.dist_mix = rng.between(0.60f, 0.95f);
      break;

    case 5:  // Wavefolder
      p.waveform = 0;
      p.cutoff = log_between(rng, 380.0f, 1900.0f);
      p.resonance = rng.between(0.42f, 0.82f);
      p.env_mod = rng.between(0.62f, 0.94f);
      p.decay = rng.between(0.25f, 0.72f);
      p.accent = rng.between(0.55f, 0.88f);
      p.dist_active = true;
      p.dist_type = 4;
      p.dist_drive = rng.between(0.38f, 0.86f);
      p.dist_tone = rng.between(0.42f, 0.82f);
      p.dist_mix = rng.between(0.50f, 0.90f);
      break;

    case 6:  // Clean / dry
      p.waveform = rng.chance(0.55f) ? 0 : 1;
      p.cutoff = log_between(rng, 650.0f, 2200.0f);
      p.resonance = rng.between(0.18f, 0.55f);
      p.env_mod = rng.between(0.38f, 0.78f);
      p.decay = rng.between(0.28f, 0.72f);
      p.accent = rng.between(0.42f, 0.75f);
      p.dist_active = rng.chance(0.35f);
      p.dist_type = rng.next_int(0, 4);
      p.dist_drive = rng.between(0.10f, 0.38f);
      p.dist_tone = rng.between(0.50f, 0.85f);
      p.dist_mix = rng.between(0.20f, 0.55f);
      break;

    default:  // Hard / industrial
      p.waveform = 0;
      p.cutoff = log_between(rng, 140.0f, 720.0f);
      p.resonance = rng.between(0.60f, 0.94f);
      p.env_mod = rng.between(0.70f, 0.97f);
      p.decay = rng.between(0.20f, 0.48f);
      p.accent = rng.between(0.72f, 0.96f);
      p.dist_active = true;
      p.dist_type = rng.chance(0.68f) ? 0 : 2;
      p.dist_drive = rng.between(0.70f, 1.0f);
      p.dist_tone = rng.between(0.32f, 0.70f);
      p.dist_mix = rng.between(0.78f, 1.0f);
      break;
  }

  p.tempo = std::round(rng.between(116.0f, 150.0f) * 2.0f) * 0.5f;
  p.swing = rng.chance(0.55f) ? 0.0f : rng.between(0.04f, 0.30f);
  p.tuning = rng.chance(0.10f) ? (rng.chance(0.5f) ? -12.0f : 12.0f) : 0.0f;

  sanitise(r);
}

// Replicant's preset as ours (gen.h): the steps of its length, in a Pattern of length 8 or 16; every step past that a rest.
void map_preset(Preset& out, const RPreset& r) noexcept {
  clear(out.pattern);  // every step a rest with C2 under it
  const unsigned source = static_cast<unsigned>(jlimit(1, kLineSteps, r.length));
  for (unsigned i = 0; i < source; ++i) {
    const RStep& s = r.steps[i];
    out.pattern.steps[i].note = static_cast<std::uint8_t>(s.note);
    out.pattern.steps[i].flags = static_cast<std::uint8_t>((s.accent ? kStepAccent : 0u) | (s.slide ? kStepSlide : 0u) | (s.gate ? 0u : kStepRest));
  }
  out.pattern.length = source <= 8u ? 8 : 16;
  const int swing = static_cast<int>(r.patch.swing * 100.0f + 0.5f);
  out.pattern.swing = static_cast<std::uint8_t>(jlimit(0, static_cast<int>(kMaxSwing), swing));
  out.patch = r.patch;
  out.source_length = static_cast<std::uint8_t>(r.length);
  out.name[0] = '\0';
  append(out.name, kNameChars, r.name);
}

}  // namespace

void generate(Preset& out, unsigned bank, unsigned slot, std::uint32_t seed) noexcept {
  RPreset r;
  make_generated(r, static_cast<int>(bank > kBanks ? kBanks : bank), static_cast<int>(slot > kSlots ? kSlots : slot), seed);
  map_preset(out, r);
}

void factory(Preset& out, unsigned bank, unsigned slot) noexcept {
  const int b = jlimit(0, static_cast<int>(kBanks) - 1, static_cast<int>(bank > kBanks ? kBanks : bank));
  const int s = jlimit(0, static_cast<int>(kSlots) - 1, static_cast<int>(slot > kSlots ? kSlots : slot));
  RPreset r;
  if (s == 0) {
    make_factory_line(r, b);
  } else {
    make_generated(r, b, s, 0u);
  }
  map_preset(out, r);
}

const char* bank_name(unsigned bank) noexcept { return kBankNames[bank < kBanks ? bank : kBanks - 1u]; }

char bank_letter(unsigned bank) noexcept { return kBankLetters[bank < kBanks ? bank : kBanks - 1u]; }

ParamValues to_values(const Patch& patch) noexcept {
  ParamValues v{};
  // Each value in its parameter's own units (the descriptors of units/tb303 and units/distortion): the app applies them as they are.
  v.voice[0] = patch.cutoff;                        // Hz
  v.voice[1] = patch.resonance;                     // 0..1
  v.voice[2] = patch.env_mod;                       // 0..1
  v.voice[3] = patch.decay * 1000.0f;               // ms
  v.voice[4] = patch.accent;                        // 0..1
  v.voice[5] = static_cast<float>(patch.waveform);  // choice index: 0 SAW, 1 SQR
  v.voice[6] = patch.tuning;                        // semitones
  v.dist[0] = patch.dist_active ? 1.0f : 0.0f;      // choice index: 0 OFF, 1 ON
  v.dist[1] = static_cast<float>(patch.dist_type);  // choice index: GRIT, TUBE, HARD, FUZZ, FOLD
  v.dist[2] = patch.dist_drive;                     // 0..1
  v.dist[3] = patch.dist_tone;                      // 0..1
  v.dist[4] = patch.dist_mix;                       // 0..1
  v.tempo_mbpm = static_cast<std::uint32_t>(patch.tempo * 1000.0f + 0.5f);
  v.swing_pct = static_cast<std::uint8_t>(jlimit(0, static_cast<int>(kMaxSwing), static_cast<int>(patch.swing * 100.0f + 0.5f)));
  return v;
}

}  // namespace o3_303::seq::gen
