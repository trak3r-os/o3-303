// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// GEN and the vault: Replicant 303's preset generator and its factory bank (8 banks x 64 slots), ported.
//
// Source: third_party/replicant303/Presets/PresetBank.cpp (generatePreset, makeFactoryPreset, the name tables) and DSP/Pattern.h
// (makeDefaultPattern), DanimanX, MIT, pinned at d5369be. The same (bank, slot, seed) gives the same pattern and patch as the
// original (app/seq/tests/seq_gen_oracle_test.cpp checks all 512 slots against it). The patch floats can differ in the last bit from
// another platform's libm (exp / log); the pattern cannot.
//
// A preset is a pattern plus the voice that plays it. Replicant's line has 16 steps and a length of its own, 7, 8, 12 or 16, and
// plays only that many steps. The Pattern's lengths are 8, 16, 32 and 64, so the line is mapped by its length:
//     source 16: length 16, the 16 steps;      source 12: length 16, steps 12..15 rests;
//     source 8:  length 8, the 8 steps;        source 7:  length 8, step 7 (the eighth) a rest.
// Steps past `source_length` are rests, in the Pattern's hidden steps too (a longer length starts from silence). Lengths 7 and 12 are
// the one deviation from Replicant: its loop of 7 or 12 sixteenths becomes 8 or 16 (the last steps rest), so the loop period differs.
// `source_length` keeps the original's number. Gate false -> kStepRest, accent -> kStepAccent, slide -> kStepSlide, the swing as a
// percent. The patch keeps Replicant's own units (0..1 knobs, seconds, BPM) so it can be compared exactly; to_values() turns it into
// the values of the units' parameters.

#include <o3_303/pattern.h>

#include <cstdint>

namespace o3_303::seq::gen {

inline constexpr unsigned kBanks = 8;
inline constexpr unsigned kSlots = 64;
inline constexpr unsigned kNameChars = 32;  // a name with its NUL

struct Patch {
  int waveform;       // 0 saw, 1 square
  float tuning;       // semitones, -12..12
  float cutoff;       // Hz, 25..4500
  float resonance;    // 0..1
  float env_mod;      // 0..1
  float decay;        // seconds, 0.2..2.5
  float accent;       // 0..1
  float tempo;        // BPM, 40..300
  float swing;        // 0..0.66
  bool dist_active;
  int dist_type;      // 0..4: PocoRat, TubeDrive, HardClip, GermaniumFuzz, Wavefolder = the distortion unit's GRIT, TUBE, HARD, FUZZ, FOLD
  float dist_drive;   // 0..1
  float dist_tone;    // 0..1
  float dist_mix;     // 0..1
};

struct Preset {
  Pattern pattern;
  Patch patch;
  std::uint8_t source_length;  // Replicant's pattern length: 16 for the factory lines, 7, 8, 12 or 16 for generated ones
  char name[kNameChars];
};

// Replicant's GEN button: slot 0 as well is generated (named "<Factory name> n"), as in the original.
void generate(Preset& out, unsigned bank, unsigned slot, std::uint32_t seed) noexcept;
// The vault's content: slot 0 of a bank is the factory line of Pattern.h (with Replicant's default voice), slots 1..63 are
// generate(bank, slot, 0).
void factory(Preset& out, unsigned bank, unsigned slot) noexcept;

const char* bank_name(unsigned bank) noexcept;  // "AMBER", "SUB", "NEON", "ACID", "BLADE", "ROOFTOP", "OFF WORLD", "LAST"
char bank_letter(unsigned bank) noexcept;        // 'A'..'H'

// The patch as the values of the units' parameters, each in the NATIVE units of its descriptor, so the app applies them with set_param
// and no scaling (docs/M3-PLAN.md, "Contracts"). Master volume is not part of a preset.
inline constexpr unsigned kVoiceValues = 7;  // tb303 ids 0..6: Cutoff Hz, Reso 0..1, Env Mod 0..1, Decay ms, Accent 0..1, Wave (0 SAW, 1 SQR), Tune st
inline constexpr unsigned kDistValues = 5;   // distortion ids 0..4: Active (0 OFF, 1 ON), Type (0..4), Drive 0..1, Tone 0..1, Mix 0..1

struct ParamValues {
  float voice[kVoiceValues];
  float dist[kDistValues];
  std::uint32_t tempo_mbpm;  // for engine.set_tempo()
  std::uint8_t swing_pct;    // 0..66, also in the pattern
};
ParamValues to_values(const Patch& patch) noexcept;

}  // namespace o3_303::seq::gen
