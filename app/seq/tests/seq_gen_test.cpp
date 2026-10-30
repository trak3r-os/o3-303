// SPDX-License-Identifier: GPL-3.0-or-later
// GEN and the vault (o3_303/seq/gen.h): the mapping into a Pattern and into the units' parameter values. (The generator itself is
// checked against Replicant's original in seq_gen_oracle_test.cpp.)
#include <doctest/doctest.h>

#include <o3_303/seq/bank.h>
#include <o3_303/seq/gen.h>
#include <o3_303/units/distortion/distortion.h>
#include <o3_303/units/acid_voice/acid_voice.h>

#include <cstring>
#include <memory>
#include <set>
#include <string>

using namespace o3_303;
namespace gen = o3_303::seq::gen;
namespace core = o3sdk::core;

namespace {

bool in_range(const core::ParamDesc& d, float v) { return v >= d.min && v <= d.max; }

}  // namespace

TEST_CASE("slot 0 of a bank is the factory line, with Replicant's default voice") {
  gen::Preset p;
  gen::factory(p, 0, 0);
  CHECK(std::string(p.name) == "Amber Line");
  CHECK(p.source_length == 16);
  CHECK(p.pattern.length == 16);  // the factory lines have 16 steps
  CHECK(p.pattern.gate == kDefaultGate);
  CHECK(p.pattern.swing == 0);
  CHECK(p.pattern.transpose == 0);
  // Amber Line: notes 36 36 48 36 39 36 43 36 36 48 36 39 36 43 36 36, accents on 1, 6, 10, slides on 4 and 8, gates closed on 3, 11.
  const int notes[16] = {36, 36, 48, 36, 39, 36, 43, 36, 36, 48, 36, 39, 36, 43, 36, 36};
  for (unsigned i = 0; i < 16; ++i) {
    CAPTURE(i);
    CHECK(p.pattern.steps[i].note == notes[i]);
    unsigned flags = 0;
    if (i == 1 || i == 6 || i == 10) flags |= kStepAccent;
    if (i == 4 || i == 8) flags |= kStepSlide;
    if (i == 3 || i == 11) flags |= kStepRest;
    CHECK(p.pattern.steps[i].flags == flags);
  }
  for (unsigned i = 16; i < kMaxSteps; ++i) {
    CHECK(p.pattern.steps[i].flags == kStepRest);  // the hidden steps are silent
  }
  CHECK_FALSE(seq::lane_has_motion(p.pattern, 0));

  // The defaults of the patch are the units' defaults (Replicant's).
  const gen::ParamValues v = gen::to_values(p.patch);
  AcidVoice voice;
  Distortion dist;
  const core::ParamDesc* vd = voice.info().params;
  const core::ParamDesc* dd = dist.info().params;
  for (unsigned i = 0; i < gen::kVoiceValues; ++i) {
    CAPTURE(i);
    CHECK(v.voice[i] == doctest::Approx(vd[i].def));
  }
  for (unsigned i = 0; i < gen::kDistValues; ++i) {
    CAPTURE(i);
    CHECK(v.dist[i] == doctest::Approx(dd[i].def));
  }
  CHECK(v.tempo_mbpm == 125'000);
  CHECK(v.swing_pct == 0);
}

TEST_CASE("the other slots are generated, deterministic, named, and all different") {
  std::set<std::string> names;
  for (unsigned bank = 0; bank < gen::kBanks; ++bank) {
    for (unsigned slot = 0; slot < gen::kSlots; ++slot) {
      gen::Preset a;
      gen::Preset b;
      gen::factory(a, bank, slot);
      gen::factory(b, bank, slot);
      CHECK(std::memcmp(&a.pattern, &b.pattern, sizeof(Pattern)) == 0);
      CHECK(std::string(a.name) == b.name);
      names.insert(a.name);
      CHECK(std::strlen(a.name) < gen::kNameChars);
      if (slot != 0) {
        gen::Preset g;
        gen::generate(g, bank, slot, 0);
        CHECK(std::memcmp(&a.pattern, &g.pattern, sizeof(Pattern)) == 0);
        CHECK(a.patch.cutoff == g.patch.cutoff);
      }
    }
  }
  CHECK(names.size() == 512);
  // GEN on slot 0 does not give the factory line back: it is a new line named after it.
  gen::Preset f;
  gen::Preset g;
  gen::factory(f, 2, 0);
  gen::generate(g, 2, 0, 4);
  CHECK(std::string(g.name) == "Neon Run 5");  // the seed picks the suffix: 1 + 4 % 9
  CHECK(std::memcmp(&f.pattern, &g.pattern, sizeof(Pattern)) != 0);
  // A seed changes the line.
  gen::Preset s1;
  gen::Preset s2;
  gen::generate(s1, 5, 20, 100);
  gen::generate(s2, 5, 20, 101);
  CHECK(std::memcmp(&s1.pattern, &s2.pattern, sizeof(Pattern)) != 0);
  // Arguments past the vault are clamped to its last bank / slot.
  gen::Preset last;
  gen::Preset past;
  gen::generate(last, 7, 63, 9);
  gen::generate(past, 99, 999, 9);
  CHECK(std::memcmp(&last.pattern, &past.pattern, sizeof(Pattern)) == 0);
}

TEST_CASE("a generated pattern is a playable Pattern of length 8 or 16: rests where the gate was closed and after the original's steps") {
  for (unsigned bank = 0; bank < gen::kBanks; ++bank) {
    for (unsigned slot = 0; slot < gen::kSlots; ++slot) {
      gen::Preset p;
      gen::generate(p, bank, slot, 77);
      CAPTURE(bank);
      CAPTURE(slot);
      CHECK(valid_length(p.pattern.length));
      CHECK(p.pattern.length == (p.source_length <= 8 ? 8 : 16));
      CHECK(p.source_length >= 7);
      CHECK(p.source_length <= 16);
      CHECK(p.pattern.gate == kDefaultGate);
      CHECK(p.pattern.swing <= kMaxSwing);
      unsigned sounding = 0;
      unsigned accents = 0;
      for (unsigned i = 0; i < kMaxSteps; ++i) {
        const Step& s = p.pattern.steps[i];
        CHECK(s.note >= 24);
        CHECK(s.note <= 48);
        CHECK((s.flags & kStepTie) == 0);
        CHECK((s.flags & ~(kStepAccent | kStepSlide | kStepRest)) == 0);
        if (i >= p.source_length) {
          CHECK(s.flags == kStepRest);  // past the original's steps: silence, hidden steps included
        }
        if ((s.flags & kStepRest) == 0) {
          ++sounding;
          accents += (s.flags & kStepAccent) != 0 ? 1u : 0u;
        } else {
          CHECK((s.flags & (kStepAccent | kStepSlide)) == 0);  // a closed gate carries nothing
        }
      }
      CHECK(sounding >= 4);
      CHECK(accents >= 1);
      CHECK((p.pattern.steps[0].flags & kStepRest) == 0);  // step 0 always sounds
    }
  }
}

TEST_CASE("to_values gives each parameter in its unit's own units, inside the unit's own range, for the whole vault") {
  AcidVoice voice;
  Distortion dist;
  const core::ParamDesc* vd = voice.info().params;
  const core::ParamDesc* dd = dist.info().params;
  unsigned bad = 0;
  unsigned dist_off = 0;
  unsigned square = 0;
  for (const std::uint32_t seed : {0u, 12345u}) {
    for (unsigned bank = 0; bank < gen::kBanks; ++bank) {
      for (unsigned slot = 0; slot < gen::kSlots; ++slot) {
        gen::Preset p;
        gen::generate(p, bank, slot, seed);
        const gen::ParamValues v = gen::to_values(p.patch);
        for (unsigned i = 0; i < gen::kVoiceValues; ++i) {
          bad += in_range(vd[i], v.voice[i]) ? 0u : 1u;
        }
        for (unsigned i = 0; i < gen::kDistValues; ++i) {
          bad += in_range(dd[i], v.dist[i]) ? 0u : 1u;
        }
        // The choices are whole numbers.
        bad += (v.voice[5] == 0.0f || v.voice[5] == 1.0f) ? 0u : 1u;
        bad += (v.dist[0] == 0.0f || v.dist[0] == 1.0f) ? 0u : 1u;
        bad += (v.dist[1] == 0.0f || v.dist[1] == 1.0f || v.dist[1] == 2.0f || v.dist[1] == 3.0f || v.dist[1] == 4.0f) ? 0u : 1u;
        bad += (v.tempo_mbpm >= 40'000 && v.tempo_mbpm <= 300'000) ? 0u : 1u;
        bad += v.swing_pct == p.pattern.swing ? 0u : 1u;
        dist_off += v.dist[0] == 0.0f ? 1u : 0u;
        square += v.voice[5] == 1.0f ? 1u : 0u;
      }
    }
  }
  CHECK(bad == 0);
  CHECK(dist_off > 0);  // the archetypes reach both choices
  CHECK(square > 0);
}

TEST_CASE("to_values: the arithmetic, on a known patch") {
  gen::Patch p{};
  p.waveform = 1;
  p.tuning = -12.0f;
  p.cutoff = 1234.5f;
  p.resonance = 0.8f;
  p.env_mod = 0.25f;
  p.decay = 1.5f;
  p.accent = 0.9f;
  p.tempo = 133.5f;
  p.swing = 0.30f;
  p.dist_active = false;
  p.dist_type = 4;
  p.dist_drive = 0.7f;
  p.dist_tone = 0.2f;
  p.dist_mix = 1.0f;
  const gen::ParamValues v = gen::to_values(p);
  CHECK(v.voice[0] == 1234.5f);               // Hz
  CHECK(v.voice[1] == doctest::Approx(0.8f));  // fractions stay fractions
  CHECK(v.voice[2] == doctest::Approx(0.25f));
  CHECK(v.voice[3] == doctest::Approx(1500.0f));  // seconds -> ms
  CHECK(v.voice[4] == doctest::Approx(0.9f));
  CHECK(v.voice[5] == 1.0f);
  CHECK(v.voice[6] == -12.0f);
  CHECK(v.dist[0] == 0.0f);
  CHECK(v.dist[1] == 4.0f);
  CHECK(v.dist[2] == doctest::Approx(0.7f));
  CHECK(v.dist[3] == doctest::Approx(0.2f));
  CHECK(v.dist[4] == 1.0f);
  CHECK(v.tempo_mbpm == 133'500);
  CHECK(v.swing_pct == 30);
}

TEST_CASE("bank names and letters; a preset loads into a bank pattern as one undoable edit") {
  const char* const names[] = {"AMBER", "SUB", "NEON", "ACID", "BLADE", "ROOFTOP", "OFF WORLD", "LAST"};
  for (unsigned b = 0; b < gen::kBanks; ++b) {
    CHECK(std::string(gen::bank_name(b)) == names[b]);
    CHECK(gen::bank_letter(b) == static_cast<char>('A' + b));
  }
  CHECK(std::string(gen::bank_name(100)) == "LAST");

  auto bank = std::make_unique<seq::Bank>();
  bank->init();
  gen::Preset p;
  gen::generate(p, 3, 9, 5);
  CHECK(bank->replace(4, p.pattern));
  CHECK(std::memcmp(&bank->pattern(4), &p.pattern, sizeof(Pattern)) == 0);
  CHECK(bank->undo() == 4);
  CHECK(bank->pattern(4).steps[0].flags == kStepRest);
}
