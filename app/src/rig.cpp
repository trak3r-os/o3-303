// SPDX-License-Identifier: GPL-3.0-or-later
#include "rig.h"

namespace o3_303 {

namespace {

// The sound a load writes, in one place: the voice's ids 0..kVoiceValues-1 and the distortion's 0..kDistValues-1. load_preset() writes it and LAST
// reads and writes it, so LAST holds exactly what a load replaces (the level, master, delay, growl and mods are not part of it).
void write_sound(o3sdk::dsp::Engine& e, const Rig& rig, const float* voice, const float* dist) noexcept {
  for (unsigned i = 0; i < seq::gen::kVoiceValues; ++i) {
    (void)e.set_param(rig.wiring.voice, static_cast<std::uint16_t>(i), voice[i]);
  }
  for (unsigned i = 0; i < seq::gen::kDistValues; ++i) {
    (void)e.set_param(rig.wiring.dist, static_cast<std::uint16_t>(i), dist[i]);
  }
}

void read_sound(const o3sdk::dsp::Engine& e, const Rig& rig, float* voice, float* dist) noexcept {
  for (unsigned i = 0; i < seq::gen::kVoiceValues; ++i) {
    voice[i] = e.param(rig.wiring.voice, static_cast<std::uint16_t>(i));
  }
  for (unsigned i = 0; i < seq::gen::kDistValues; ++i) {
    dist[i] = e.param(rig.wiring.dist, static_cast<std::uint16_t>(i));
  }
}

// The eight factory snapshots (install_factory_snapshots): slot 0 is the sound as it stands, the others set a few targets of it. `target` is a position in pages.cpp's kSnapTargets (a snapshot's
// own order), `value` is in the units that entry shows (Hz, ms, 0..1 for a percent knob, the enum's number). Every value is inside the parameter's own range and the CUTOFFs are inside the
// 200..2,394 Hz the voice plays on RANGE 303 (F1 of the review); tests/snapshots_app_test.cpp checks both. The sounds are for the maintainer's ear on the unit.
enum SnapTarget : std::uint8_t {
  kTCutoff = 0, kTReso = 1, kTEnvMod = 2, kTDecay = 3, kTAccent = 4, kTWave = 5,
  kTDistActive = 7, kTDistType = 8, kTDrive = 9, kTDistTone = 10, kTDistMix = 11,
  kTDlyTime = 12, kTDlyFeedback = 13, kTDlyTone = 14, kTSend = 17,
  kTVowel = 18, kTFmtQ = 19, kTGrowlMix = 20, kTCombMix = 21,
  kTAccDecay = 22, kTTracking = 23, kTSlide = 24, kTSoftAtk = 25, kTSubOsc = 26, kTFilterFm = 27, kTAccSweep = 28, kTMuffler = 29, kTOverdrive = 31, kTAmpDecay = 32,
  kTRange = 42,
};

struct FactoryValue {
  std::uint8_t slot;
  std::uint8_t target;
  float value;
};

constexpr FactoryValue kFactorySnaps[] = {
    // 2 CLOSED DUB: a dark saw, low cutoff, short, with a quarter-note delay
    {1, kTCutoff, 260.0f}, {1, kTReso, 0.30f}, {1, kTEnvMod, 0.25f}, {1, kTDecay, 160.0f}, {1, kTAccent, 0.45f}, {1, kTWave, 0.0f}, {1, kTDistActive, 0.0f}, {1, kTGrowlMix, 0.0f},
    {1, kTDlyTime, 3.0f}, {1, kTDlyFeedback, 55.0f}, {1, kTDlyTone, 1800.0f}, {1, kTSend, 35.0f},
    // 3 SCREAMER: high resonance, a big envelope, accents, a little tube
    {2, kTCutoff, 900.0f}, {2, kTReso, 0.95f}, {2, kTEnvMod, 0.85f}, {2, kTDecay, 350.0f}, {2, kTAccent, 0.90f}, {2, kTWave, 0.0f},
    {2, kTDistActive, 1.0f}, {2, kTDistType, 1.0f}, {2, kTDrive, 0.45f}, {2, kTDistMix, 0.70f}, {2, kTGrowlMix, 0.0f},
    // 4 HOLLOW: the square wave, middle cutoff, little resonance
    {3, kTCutoff, 700.0f}, {3, kTReso, 0.50f}, {3, kTEnvMod, 0.35f}, {3, kTDecay, 500.0f}, {3, kTAccent, 0.60f}, {3, kTWave, 1.0f}, {3, kTDistActive, 0.0f}, {3, kTGrowlMix, 0.0f},
    // 5 OVERDRIVEN: the voice's overdrive up and the grit on
    {4, kTCutoff, 600.0f}, {4, kTReso, 0.60f}, {4, kTEnvMod, 0.60f}, {4, kTDecay, 300.0f}, {4, kTOverdrive, 18.0f},
    {4, kTDistActive, 1.0f}, {4, kTDistType, 0.0f}, {4, kTDrive, 0.70f}, {4, kTDistTone, 0.55f}, {4, kTDistMix, 0.90f}, {4, kTGrowlMix, 0.0f},
    // 6 LONG: a long decay on both envelopes
    {5, kTCutoff, 450.0f}, {5, kTReso, 0.60f}, {5, kTEnvMod, 0.50f}, {5, kTDecay, 2400.0f}, {5, kTAmpDecay, 3200.0f}, {5, kTAccent, 0.50f}, {5, kTDistActive, 0.0f}, {5, kTGrowlMix, 0.0f}, {5, kTSend, 20.0f},
    // 7 FISH: RANGE FISH with the Devil Fish mods
    {6, kTRange, 1.0f}, {6, kTCutoff, 1200.0f}, {6, kTReso, 0.80f}, {6, kTEnvMod, 0.75f}, {6, kTDecay, 400.0f}, {6, kTSoftAtk, 2.0f}, {6, kTSubOsc, 1.0f}, {6, kTAccSweep, 2.0f},
    {6, kTMuffler, 1.0f}, {6, kTOverdrive, 6.0f}, {6, kTTracking, 50.0f}, {6, kTFilterFm, 0.15f}, {6, kTDistActive, 0.0f}, {6, kTGrowlMix, 0.0f},
    // 8 GROWL: the growl on (the formants and the comb), the sound open
    {7, kTCutoff, 500.0f}, {7, kTReso, 0.70f}, {7, kTEnvMod, 0.60f}, {7, kTDecay, 450.0f}, {7, kTWave, 0.0f}, {7, kTDistActive, 0.0f},
    {7, kTVowel, 0.25f}, {7, kTFmtQ, 6.0f}, {7, kTGrowlMix, 0.60f}, {7, kTCombMix, 0.40f},
};

}  // namespace

void load_preset(o3sdk::dsp::Engine& e, Rig& rig, const seq::gen::Preset& preset, LoadWhat what, bool keep_timing) noexcept {
  const seq::gen::ParamValues v = seq::gen::to_values(preset.patch);
  if (what != LoadWhat::Sound) {
    if (keep_timing) {
      Pattern p = preset.pattern;
      p.swing = rig.bank->pattern(rig.bank->selected()).swing;
      (void)rig.bank->replace(rig.bank->selected(), p);
    } else {
      (void)rig.bank->replace(rig.bank->selected(), preset.pattern);
      (void)e.set_tempo(v.tempo_mbpm);
    }
  }
  if (what != LoadWhat::Pattern) {
    write_sound(e, rig, v.voice, v.dist);
  }
}

void last_capture(const o3sdk::dsp::Engine& e, Rig& rig) noexcept {
  read_sound(e, rig, rig.last_voice, rig.last_dist);
  rig.last_used = true;
}

void install_factory_snapshots(const o3sdk::dsp::Engine& e, Rig& rig) noexcept {
  float base[kSnapValues];
  rig.controls.snap_capture(e, base);
  for (unsigned slot = 0; slot < kSnapSlots; ++slot) {
    float v[kSnapValues];
    std::memcpy(v, base, sizeof v);
    for (const FactoryValue& f : kFactorySnaps) {
      if (f.slot == slot) {
        v[f.target] = f.value;
      }
    }
    rig.wiring.snaps.store(slot, v);
  }
}

bool last_swap(o3sdk::dsp::Engine& e, Rig& rig) noexcept {
  if (!rig.last_used) {
    return false;
  }
  float voice[seq::gen::kVoiceValues];
  float dist[seq::gen::kDistValues];
  read_sound(e, rig, voice, dist);
  write_sound(e, rig, rig.last_voice, rig.last_dist);
  for (unsigned i = 0; i < seq::gen::kVoiceValues; ++i) {
    rig.last_voice[i] = voice[i];
  }
  for (unsigned i = 0; i < seq::gen::kDistValues; ++i) {
    rig.last_dist[i] = dist[i];
  }
  return true;
}

namespace {

bool same_row(const seq::SongRow& a, const seq::SongRow& b) noexcept {
  return a.pattern == b.pattern && a.repeat == b.repeat && a.transpose == b.transpose && a.flags == b.flags;
}

// `n` rows of `a` from `a_from` equal those of `b` from `b_from`.
bool same_rows(const seq::Song& a, unsigned a_from, const seq::Song& b, unsigned b_from, unsigned n) noexcept {
  for (unsigned i = 0; i < n; ++i) {
    if (!same_row(a.rows[a_from + i], b.rows[b_from + i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

SongMove song_move(const seq::Song& before, const seq::Song& after, unsigned& at) noexcept {
  at = 0;
  const unsigned a = before.length < seq::kSongRows ? before.length : seq::kSongRows;
  const unsigned b = after.length < seq::kSongRows ? after.length : seq::kSongRows;
  if (a == b) {
    unsigned differ = 0;
    for (unsigned i = 0; i < a; ++i) {
      differ += same_row(before.rows[i], after.rows[i]) ? 0u : 1u;
    }
    return differ <= 1u ? SongMove::None : SongMove::Replaced;  // one cell edited: the rows stay where they are, the position with them
  }
  if (b == a + 1u || a == b + 1u) {
    const seq::Song& longer = b > a ? after : before;
    const seq::Song& shorter = b > a ? before : after;
    const unsigned n = a < b ? a : b;  // the shorter one's length
    unsigned first = 0;  // the first row that differs, n when the shorter one is a prefix of the longer
    while (first < n && same_row(longer.rows[first], shorter.rows[first])) {
      ++first;
    }
    if (same_rows(longer, first + 1u, shorter, first, n - first)) {
      at = first;
      return b > a ? SongMove::Inserted : SongMove::Deleted;
    }
  }
  return SongMove::Replaced;
}

bool undo_step(Rig& rig, bool redo) noexcept {
  seq::Bank& bank = *rig.bank;
  const seq::Song before = bank.song();  // for the sequencer's place in it
  const int i = redo ? bank.redo() : bank.undo();
  if (i < 0) {
    return false;
  }
  if (bank.undid_song()) {
    if (rig.seq != nullptr) {
      rig.seq->sync_song();
      unsigned at = 0;
      switch (song_move(before, bank.song(), at)) {
        case SongMove::Inserted:
          (void)rig.seq->song_row_inserted(at);
          break;
        case SongMove::Deleted:
          (void)rig.seq->song_row_deleted(at);
          break;
        case SongMove::Replaced:
          (void)rig.seq->song_replaced();
          break;
        case SongMove::None:
          break;
      }
    }
  } else {
    bank.select(static_cast<unsigned>(i));
  }
  return true;
}

void set_freeze(o3sdk::dsp::Engine& e, const Rig& rig, bool on) noexcept {
  (void)e.set_param(rig.wiring.freeze, Freeze::Active, on ? 1.0f : 0.0f);
}

bool freeze_on(const o3sdk::dsp::Engine& e, const Rig& rig) noexcept { return e.param(rig.wiring.freeze, Freeze::Active) >= 0.5f; }

void set_freeze_mode(o3sdk::dsp::Engine& e, const Rig& rig, Freeze::ModeValue mode) noexcept {
  (void)e.set_param(rig.wiring.freeze, Freeze::Mode, static_cast<float>(mode));
}

void set_freeze_length(o3sdk::dsp::Engine& e, const Rig& rig, Freeze::LengthValue length) noexcept {
  (void)e.set_param(rig.wiring.freeze, Freeze::Length, static_cast<float>(length));
}

Freeze::LengthValue freeze_length(const o3sdk::dsp::Engine& e, const Rig& rig) noexcept {
  const float v = e.param(rig.wiring.freeze, Freeze::Length) + 0.5f;
  return v >= 2.0f ? Freeze::Sixteenth : (v >= 1.0f ? Freeze::Eighth : Freeze::Quarter);
}

}  // namespace o3_303
