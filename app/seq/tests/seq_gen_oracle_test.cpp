// SPDX-License-Identifier: GPL-3.0-or-later
//
// GEN against its original. The oracle is Replicant 303's own Presets/PresetBank.cpp (third_party/replicant303, pinned d5369be),
// compiled unchanged against the minimal JUCE stand-in in juce_shim/: every one of the 512 slots, for several seeds, must give the
// same pattern, patch and name as the port in src/gen.cpp.
#include <doctest/doctest.h>

#include "DSP/Pattern.h"
#include "Presets/PresetBank.h"

#include <o3_303/seq/gen.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>

namespace {

namespace rep = Replicant303;
namespace gen = o3_303::seq::gen;

// Compares one preset field by field; returns the number of differences (and says the first one in `first`).
unsigned compare(const rep::Preset& o, const gen::Preset& m, std::string& first) {
  unsigned diffs = 0;
  const auto fail = [&](const char* what) {
    if (diffs++ == 0) {
      first = what;
    }
  };
  // Replicant's line plays `length` of its 16 steps (7, 8, 12 or 16); ours is a Pattern of length 8 or 16 (gen.h): the same steps,
  // and rests after them.
  const unsigned source = static_cast<unsigned>(o.pattern.length);
  if (m.source_length != source) fail("source length");
  if (m.pattern.length != (source <= 8u ? 8u : 16u)) fail("pattern length");
  for (unsigned i = 0; i < source; ++i) {
    const rep::Step& s = o.pattern.steps[i];
    const o3_303::Step& t = m.pattern.steps[i];
    if (t.note != s.note) fail("note");
    if (((t.flags & o3_303::kStepRest) == 0u) != s.gate) fail("gate");
    if (((t.flags & o3_303::kStepAccent) != 0u) != s.accent) fail("accent");
    if (((t.flags & o3_303::kStepSlide) != 0u) != s.slide) fail("slide");
    if ((t.flags & o3_303::kStepTie) != 0u) fail("tie");
  }
  for (unsigned i = source; i < o3_303::kMaxSteps; ++i) {
    if (m.pattern.steps[i].flags != o3_303::kStepRest) fail("step past the source length is not a rest");
  }
  if (m.pattern.swing != static_cast<unsigned>(std::lround(o.swing * 100.0f))) fail("pattern swing");
  if (m.pattern.gate != o3_303::kDefaultGate || m.pattern.transpose != 0) fail("pattern gate/transpose");
  const gen::Patch& p = m.patch;
  if (p.waveform != o.waveform) fail("waveform");
  if (p.tuning != o.tuning) fail("tuning");
  if (p.cutoff != o.cutoff) fail("cutoff");
  if (p.resonance != o.resonance) fail("resonance");
  if (p.env_mod != o.envMod) fail("env mod");
  if (p.decay != o.decay) fail("decay");
  if (p.accent != o.accent) fail("accent amount");
  if (p.tempo != o.tempo) fail("tempo");
  if (p.swing != o.swing) fail("swing");
  if (p.dist_active != o.distActive) fail("dist active");
  if (p.dist_type != o.distType) fail("dist type");
  if (p.dist_drive != o.distDrive) fail("dist drive");
  if (p.dist_tone != o.distTone) fail("dist tone");
  if (p.dist_mix != o.distMix) fail("dist mix");
  if (std::string(m.name) != o.name.toRawUTF8()) fail("name");
  return diffs;
}

}  // namespace

TEST_CASE("GEN: every slot of every bank matches Replicant's generator, for several seeds") {
  const std::uint32_t seeds[] = {0u, 1u, 0xC0FFEEu, 0xDEADBEEFu, 20261025u};
  unsigned compared = 0;
  unsigned mismatches = 0;
  std::string first;
  for (const std::uint32_t seed : seeds) {
    for (unsigned bank = 0; bank < gen::kBanks; ++bank) {
      for (unsigned slot = 0; slot < gen::kSlots; ++slot) {
        const rep::Preset original = rep::PresetBank::generatePreset(static_cast<int>(bank), static_cast<int>(slot), seed);
        gen::Preset mine;
        gen::generate(mine, bank, slot, seed);
        std::string what;
        const unsigned diffs = compare(original, mine, what);
        ++compared;
        if (diffs != 0u) {
          if (mismatches == 0u) {
            first = what + " (bank " + std::to_string(bank) + ", slot " + std::to_string(slot) + ", seed " + std::to_string(seed) + ")";
          }
          ++mismatches;
        }
      }
    }
  }
  INFO("first mismatch: " << first);
  MESSAGE("GEN oracle: compared " << compared << " presets (" << gen::kBanks * gen::kSlots << " slots x " << sizeof(seeds) / sizeof(seeds[0])
                                  << " seeds), mismatches " << mismatches);
  CHECK(compared == 512u * 5u);
  CHECK(mismatches == 0u);
}

TEST_CASE("GEN: the vault's factory content matches Replicant's makeFactoryPreset (slot 0 = the factory line)") {
  unsigned mismatches = 0;
  std::string first;
  for (unsigned bank = 0; bank < gen::kBanks; ++bank) {
    for (unsigned slot = 0; slot < gen::kSlots; ++slot) {
      const rep::Preset original = rep::PresetBank::makeFactoryPreset(static_cast<int>(bank), static_cast<int>(slot));
      gen::Preset mine;
      gen::factory(mine, bank, slot);
      std::string what;
      if (compare(original, mine, what) != 0u) {
        if (mismatches == 0u) {
          first = what + " (bank " + std::to_string(bank) + ", slot " + std::to_string(slot) + ")";
        }
        ++mismatches;
      }
    }
  }
  INFO("first mismatch: " << first);
  CHECK(mismatches == 0u);
}

TEST_CASE("GEN: bank names and letters are Replicant's") {
  for (unsigned bank = 0; bank < gen::kBanks; ++bank) {
    CHECK(std::string(gen::bank_name(bank)) == rep::PresetBank::getBankName(static_cast<int>(bank)).toRawUTF8());
    CHECK(std::string(1, gen::bank_letter(bank)) == rep::PresetBank::getBankLetter(static_cast<int>(bank)).toRawUTF8());
  }
}

TEST_CASE("GEN: the oracle comparison is not vacuous (the vault has variety, a tampered port would be caught)") {
  std::set<std::string> lines;
  std::set<std::string> names;
  unsigned lengths[17] = {};
  for (unsigned bank = 0; bank < gen::kBanks; ++bank) {
    for (unsigned slot = 0; slot < gen::kSlots; ++slot) {
      gen::Preset p;
      gen::factory(p, bank, slot);
      std::string line;
      for (unsigned i = 0; i < 16; ++i) {
        line += static_cast<char>(p.pattern.steps[i].note);
        line += static_cast<char>(p.pattern.steps[i].flags);
      }
      lines.insert(line);
      names.insert(p.name);
      ++lengths[p.source_length];
    }
  }
  CHECK(lines.size() > 500u);  // 512 slots, nearly all different lines
  CHECK(names.size() == 512u);
  CHECK(lengths[7] > 0u);
  CHECK(lengths[8] > 0u);
  CHECK(lengths[12] > 0u);
  CHECK(lengths[16] > 0u);

  // Another seed gives another preset; the same seed the same one.
  gen::Preset a;
  gen::Preset b;
  gen::Preset c;
  gen::generate(a, 3, 17, 1u);
  gen::generate(b, 3, 17, 2u);
  gen::generate(c, 3, 17, 1u);
  CHECK(std::string(reinterpret_cast<const char*>(a.pattern.steps), 32) == std::string(reinterpret_cast<const char*>(c.pattern.steps), 32));
  CHECK(a.patch.cutoff == c.patch.cutoff);
  CHECK((a.patch.cutoff != b.patch.cutoff || a.patch.resonance != b.patch.resonance));
}

TEST_CASE("GEN: Replicant's lengths 7, 8, 12 and 16 become Pattern lengths 8, 8, 16 and 16, with rests after the original's steps") {
  unsigned seen[17] = {};
  for (unsigned bank = 0; bank < gen::kBanks; ++bank) {
    for (unsigned slot = 0; slot < gen::kSlots; ++slot) {
      gen::Preset p;
      gen::generate(p, bank, slot, 3);
      const unsigned source = p.source_length;
      REQUIRE((source == 7 || source == 8 || source == 12 || source == 16));
      ++seen[source];
      CHECK(p.pattern.length == (source <= 8 ? 8u : 16u));
      CHECK(o3_303::valid_length(p.pattern.length));
      for (unsigned i = source; i < o3_303::kMaxSteps; ++i) {
        CHECK(p.pattern.steps[i].flags == o3_303::kStepRest);
      }
      // The original's own steps are all there (a closed gate is a rest, an open one is not).
      const rep::Preset o = rep::PresetBank::generatePreset(static_cast<int>(bank), static_cast<int>(slot), 3);
      for (unsigned i = 0; i < source; ++i) {
        CHECK(((p.pattern.steps[i].flags & o3_303::kStepRest) == 0u) == o.pattern.steps[i].gate);
      }
    }
  }
  CHECK(seen[7] > 0);
  CHECK(seen[8] > 0);
  CHECK(seen[12] > 0);
  CHECK(seen[16] > 0);
}
