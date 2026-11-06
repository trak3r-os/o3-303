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

}  // namespace

void load_preset(o3sdk::dsp::Engine& e, Rig& rig, const seq::gen::Preset& preset, LoadWhat what) noexcept {
  const seq::gen::ParamValues v = seq::gen::to_values(preset.patch);
  if (what != LoadWhat::Sound) {
    (void)rig.bank->replace(rig.bank->selected(), preset.pattern);
    (void)e.set_tempo(v.tempo_mbpm);
  }
  if (what != LoadWhat::Pattern) {
    write_sound(e, rig, v.voice, v.dist);
  }
}

void last_capture(const o3sdk::dsp::Engine& e, Rig& rig) noexcept {
  read_sound(e, rig, rig.last_voice, rig.last_dist);
  rig.last_used = true;
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
