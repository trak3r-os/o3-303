// SPDX-License-Identifier: GPL-3.0-or-later
// TB303 against dsp::Engine (as o3sdk's tests/units_test.cpp does for the kit's units): the parameter table, events at their frames,
// accent and slide, the note stack, the volume, silence after the release, and no NaN or denormals at the extremes.
#include <doctest/doctest.h>

#include "support/need.h"
#include "support/rng.h"
#include "tb303_rig.h"

#include <cfenv>
#include <cstring>

using namespace tb303_test;
using o3_303::Tb303;

namespace {

constexpr std::uint32_t kRate = 44100;
constexpr std::uint32_t ms(std::uint32_t t) { return t * kRate / 1000u; }

bool all_finite_normal(const std::vector<float>& v) {
  for (float s : v) {
    const int c = std::fpclassify(s);
    if (c == FP_NAN || c == FP_INFINITE || c == FP_SUBNORMAL) {
      return false;
    }
  }
  return true;
}

}  // namespace

TEST_CASE("tb303: the parameter table is valid, in the contract's order, with Replicant's defaults") {
  Tb303 v;
  const auto& info = v.info();
  CHECK(info.kind == o3sdk::dsp::UnitKind::Instrument);
  REQUIRE(info.param_count == Tb303::kParamCount);
  for (unsigned p = 0; p < info.param_count; ++p) {
    INFO(info.params[p].name);
    CHECK(o3sdk::core::param_desc_valid(info.params[p]));
  }
  CHECK(Tb303::Cutoff == 0);
  CHECK(Tb303::Reso == 1);
  CHECK(Tb303::EnvMod == 2);
  CHECK(Tb303::Decay == 3);
  CHECK(Tb303::Accent == 4);
  CHECK(Tb303::Wave == 5);
  CHECK(Tb303::Tune == 6);
  CHECK(Tb303::Volume == 7);
  CHECK(Tb303::kParamCount == 8);
  const float defaults[] = {350.0f, 0.65f, 0.6f, 750.0f, 0.7f, 0.0f, 0.0f, -3.0f};
  const float lo[] = {25.0f, 0.0f, 0.0f, 200.0f, 0.0f, 0.0f, -12.0f, -60.0f};
  const float hi[] = {4500.0f, 1.0f, 1.0f, 2500.0f, 1.0f, 1.0f, 12.0f, 6.0f};
  for (unsigned p = 0; p < 8; ++p) {
    CHECK(info.params[p].def == defaults[p]);
    CHECK(info.params[p].min == lo[p]);
    CHECK(info.params[p].max == hi[p]);
  }
  // Text as the screen shows it.
  char text[16];
  (void)o3sdk::core::param_format(info.params[Tb303::Cutoff], 350.0f, text, sizeof(text));
  CHECK(std::strcmp(text, "350") == 0);
  (void)o3sdk::core::param_format(info.params[Tb303::Reso], 0.65f, text, sizeof(text));
  CHECK(std::strcmp(text, "65") == 0);
  (void)o3sdk::core::param_format(info.params[Tb303::Wave], 1.0f, text, sizeof(text));
  CHECK(std::strcmp(text, "SQR") == 0);
  (void)o3sdk::core::param_format(info.params[Tb303::Tune], -3.5f, text, sizeof(text));
  CHECK(std::strcmp(text, "-3.5") == 0);
}

TEST_CASE("tb303: the cutoff curve is Replicant's skew (25 + 4475 p^(1/0.35)) to within 3.5 % of the position") {
  const auto& d = Tb303().info().params[Tb303::Cutoff];
  double worst = 0.0;
  for (int i = 0; i <= 200; ++i) {
    const double p = i / 200.0;
    const double want = 25.0 + 4475.0 * std::pow(p, 1.0 / 0.35);
    const double got = static_cast<double>(o3sdk::core::param_denorm(d, static_cast<float>(p)));
    // The position error: where the table puts this value, against where the skew does.
    const double pos = static_cast<double>(o3sdk::core::param_norm(d, static_cast<float>(want)));
    worst = std::fabs(pos - p) > worst ? std::fabs(pos - p) : worst;
    CHECK(got >= 25.0);
    CHECK(got <= 4500.0);
  }
  CHECK(worst < 0.035);
  CHECK(o3sdk::core::param_denorm(d, 0.0f) == 25.0f);
  CHECK(o3sdk::core::param_denorm(d, 1.0f) == 4500.0f);
  // The default sits where the slider of Replicant puts 350 Hz (p = 0.38).
  CHECK(o3sdk::core::param_norm(d, 350.0f) == doctest::Approx(0.395f).epsilon(0.05));
}

TEST_CASE("tb303: a note sounds from its frame, bounded and finite, both waves; the release ends in exact silence") {
  for (int wave = 0; wave < 2; ++wave) {
    INFO("wave " << wave);
    Rig r;
    r.set(Tb303::Wave, static_cast<float>(wave));
    r.script.on(100, 36);
    r.script.off(ms(400), 36);
    const auto out = r.render(ms(2500));
    for (std::uint32_t i = 0; i < 100; ++i) {
      REQUIRE(out[i] == 0.0f);  // nothing before the NoteOn's frame
    }
    CHECK(rms(out, 200, ms(300)) > 0.05);
    CHECK(peak(out, 0, out.size()) < 1.0);  // the engine's soft clip keeps it under 1 whatever the voice does
    CHECK(all_finite_normal(out));
    // 25 ms release: -80 dB after 0.25 s, nothing at all (flushed) well before the end.
    CHECK(rms(out, ms(400 + 300), ms(400 + 400)) < 1e-3);
    CHECK(peak(out, ms(2000), out.size()) == 0.0);
  }
}

TEST_CASE("tb303: events land at their frame, whatever the block size") {
  Rig a;
  a.script.on(333, 40);
  a.script.off(ms(300), 40);
  a.script.param(ms(150), Tb303::Cutoff, 1500.0f);
  const auto big = a.render(ms(600), 64);
  Rig b;
  b.script.on(333, 40);
  b.script.off(ms(300), 40);
  b.script.param(ms(150), Tb303::Cutoff, 1500.0f);
  const auto small = b.render(ms(600), 7);
  REQUIRE(big.size() == small.size());
  double worst = 0.0;
  for (std::size_t i = 0; i < big.size(); ++i) {
    worst = std::fabs(static_cast<double>(big[i] - small[i])) > worst ? std::fabs(static_cast<double>(big[i] - small[i])) : worst;
  }
  CHECK(worst < 1e-6);
  for (std::uint32_t i = 0; i < 333; ++i) {
    REQUIRE(big[i] == 0.0f);
  }
  CHECK(big[333 + 20] != 0.0f);
}

TEST_CASE("tb303: an accented note is louder and brighter") {
  float level[2];
  double hf[2];
  for (int acc = 0; acc < 2; ++acc) {
    Rig r;
    r.script.on(0, 36, acc != 0 ? o3sdk::dsp::kNoteAccent : 0);
    r.script.off(ms(300), 36);
    const auto out = r.render(ms(400));
    level[acc] = static_cast<float>(peak(out, 0, ms(100)));
    hf[acc] = bright(out, 0, ms(100));
  }
  CHECK(level[1] > level[0] * 1.1f);
  CHECK(hf[1] > hf[0] * 1.3);
}

TEST_CASE("tb303: a slide glides the pitch in about 60 ms and does not retrigger the envelopes") {
  // Resonance 0, no env mod: the output is a low-passed saw, so its zero crossings follow the pitch.
  auto make = [](Rig& r) {
    r.set(Tb303::Reso, 0.0f);
    r.set(Tb303::EnvMod, 0.0f);
    r.set(Tb303::Cutoff, 900.0f);
  };
  Rig slide;
  make(slide);
  slide.script.on(0, 36);
  slide.script.on(ms(500), 48, o3sdk::dsp::kNoteSlide);  // before the old note's NoteOff, as the sequencer sends it
  slide.script.off(ms(500), 36);
  slide.script.off(ms(1200), 48);
  const auto s = slide.render(ms(1400));
  const double early = crossings_hz(s, ms(500), ms(500) + ms(40));
  const double late = crossings_hz(s, ms(500) + ms(400), ms(500) + ms(500));
  CHECK(early > 65.0 * 0.8);
  CHECK(early < late * 0.85);                      // still on its way up
  CHECK(late == doctest::Approx(130.8).epsilon(0.04));  // C3 reached
  CHECK(crossings_hz(s, ms(100), ms(480)) == doctest::Approx(65.4).epsilon(0.05));

  // A retriggered note (the old one released first) starts the filter envelope again; the slide does not.
  Rig retrig;
  make(retrig);
  retrig.set(Tb303::EnvMod, 1.0f);
  slide.set(Tb303::EnvMod, 1.0f);
  retrig.script.on(0, 36);
  retrig.script.off(ms(500) - 1, 36);
  retrig.script.on(ms(500), 48);
  retrig.script.off(ms(1200), 48);
  const auto t = retrig.render(ms(1400));
  Rig slide2;
  make(slide2);
  slide2.set(Tb303::EnvMod, 1.0f);
  slide2.script.on(0, 36);
  slide2.script.on(ms(500), 48, o3sdk::dsp::kNoteSlide);
  slide2.script.off(ms(500), 36);
  slide2.script.off(ms(1200), 48);
  const auto u = slide2.render(ms(1400));
  CHECK(bright(t, ms(500) + ms(20), ms(500) + ms(60)) > bright(u, ms(500) + ms(20), ms(500) + ms(60)) * 1.5);
}

TEST_CASE("tb303: the note stack: an old note's NoteOff after a slide's NoteOn keeps the gate; a stranger's NoteOff changes nothing") {
  Rig r;
  r.script.on(0, 36);
  r.script.on(ms(200), 43, o3sdk::dsp::kNoteSlide);
  r.script.off(ms(200), 36);  // the same frame, after the slide's NoteOn
  r.script.off(ms(300), 50);  // a note that is not held
  r.script.off(ms(500), 43);  // closes it
  const auto out = r.render(ms(1000));
  CHECK(rms(out, ms(250), ms(290)) > 0.02);
  CHECK(rms(out, ms(350), ms(450)) > 0.01);  // still open after the stranger's NoteOff
  CHECK(rms(out, ms(500) + ms(300), ms(500) + ms(400)) < 1e-3);
  CHECK(r.voice.sounding() == false);
}

TEST_CASE("tb303: a slide onto the same note keeps the envelopes and the later NoteOff releases it") {
  // The sequencer sends a slide onto the same note as a NoteOn with kNoteSlide and no NoteOff.
  Rig single;
  single.script.on(0, 36);
  single.script.off(ms(900), 36);
  const auto a = single.render(ms(1400));
  Rig same;
  same.script.on(0, 36);
  same.script.on(ms(450), 36, o3sdk::dsp::kNoteSlide);
  same.script.off(ms(900), 36);
  const auto b = same.render(ms(1400));
  double e = 0.0;
  for (std::size_t i = ms(460); i < ms(890); ++i) {
    const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
    e += d * d;
  }
  const double err = std::sqrt(e / static_cast<double>(ms(890) - ms(460)));
  CHECK(err < 0.005 * rms(a, ms(460), ms(890)));  // the same sound, no retrigger (a retrigger would be a jump of the cutoff)
  CHECK(rms(b, ms(900) + ms(300), ms(900) + ms(400)) < 1e-3);  // one NoteOff is enough
  CHECK(same.voice.sounding() == false);

  // Also an accented slide onto the same note: still released by one NoteOff.
  Rig acc;
  acc.script.on(0, 36);
  acc.script.on(ms(450), 36, o3sdk::dsp::kNoteSlide | o3sdk::dsp::kNoteAccent);
  acc.script.off(ms(900), 36);
  const auto c = acc.render(ms(1400));
  CHECK(rms(c, ms(900) + ms(300), ms(900) + ms(400)) < 1e-3);
}

TEST_CASE("tb303: AllNotesOff releases; a NoteOn with the slide flag and nothing sounding is an ordinary note") {
  Rig r;
  r.script.on(0, 36);
  r.script.all_off(ms(300));
  r.script.on(ms(900), 40, o3sdk::dsp::kNoteSlide);  // nothing sounds any more: it is retriggered, not glided
  r.script.off(ms(1100), 40);
  const auto out = r.render(ms(1500));
  CHECK(rms(out, ms(300) + ms(300), ms(300) + ms(400)) < 1e-3);
  CHECK(rms(out, ms(910), ms(1000)) > 0.05);
  // Its pitch is the note's from the first cycle (no glide from the old pitch).
  CHECK(crossings_hz(out, ms(920), ms(1050)) > 70.0);
}

TEST_CASE("tb303: tune, wave and volume") {
  // Tune +12 st doubles the pitch.
  auto pitch = [](float tune, float wave) {
    Rig r;
    r.set(Tb303::Reso, 0.0f);
    r.set(Tb303::EnvMod, 0.0f);
    r.set(Tb303::Cutoff, 500.0f);
    r.set(Tb303::Tune, tune);
    r.set(Tb303::Wave, wave);
    r.script.on(0, 36);
    const auto o = r.render(ms(500));
    return crossings_hz(o, ms(100), ms(450));
  };
  const double base = pitch(0.0f, 0.0f);
  CHECK(base == doctest::Approx(65.4).epsilon(0.03));
  CHECK(pitch(12.0f, 0.0f) == doctest::Approx(2.0 * base).epsilon(0.03));
  CHECK(pitch(-12.0f, 0.0f) == doctest::Approx(0.5 * base).epsilon(0.05));

  // Volume: 6 dB is twice the level (measured below the engine's soft clip, which starts at 0.8); at or below -59.5 dB it is silence
  // from the first sample (the engine delivers the stored value at start(), no glide from the default).
  auto level = [](float db) {
    Rig r;
    r.set(Tb303::Volume, db);
    r.script.on(0, 36);
    const auto o = r.render(ms(300));
    return std::make_pair(rms(o, 0, 64), rms(o, ms(100), ms(250)));
  };
  const auto l12 = level(-12.0f);
  const auto l6 = level(-6.0f);
  CHECK(l6.second / l12.second == doctest::Approx(1.995).epsilon(0.02));
  CHECK(level(-32.0f).second / l12.second == doctest::Approx(0.1).epsilon(0.02));
  CHECK(level(-59.5f).second == 0.0);
  CHECK(level(-60.0f).first == 0.0);
}

TEST_CASE("tb303: a volume or accent jump in mid-note does not click") {
  Rig r;
  r.set(Tb303::Reso, 0.0f);
  r.set(Tb303::EnvMod, 0.0f);
  r.set(Tb303::Cutoff, 900.0f);
  r.set(Tb303::Volume, 0.0f);
  r.set(Tb303::Accent, 0.0f);
  r.script.on(0, 36, o3sdk::dsp::kNoteAccent);
  r.script.param(ms(100), Tb303::Volume, -40.0f);
  r.script.param(ms(100), Tb303::Accent, 1.0f);  // the amplifier lift jumps from 1 to 1.45 at full accent envelope: smoothed
  const auto o = r.render(ms(200));
  double before = 0.0, after = 0.0;
  for (std::size_t i = ms(60); i < ms(100); ++i) {
    before = std::max(before, static_cast<double>(std::fabs(o[i] - o[i - 1])));
  }
  for (std::size_t i = ms(100); i < ms(110); ++i) {
    after = std::max(after, static_cast<double>(std::fabs(o[i] - o[i - 1])));
  }
  CHECK(after < before * 1.1);  // the largest step after the jump is no larger than before it (the gain only went down)
}

TEST_CASE("tb303: extreme settings and random notes: finite, bounded, no denormals") {
  o3sdk::test::Rng rng(0x303);
  for (int wave = 0; wave < 2; ++wave) {
    for (int trial = 0; trial < 6; ++trial) {
      INFO("wave " << wave << " trial " << trial);
      Rig r;
      r.set(Tb303::Wave, static_cast<float>(wave));
      r.set(Tb303::Reso, trial % 2 == 0 ? 1.0f : 0.0f);
      r.set(Tb303::EnvMod, trial < 3 ? 1.0f : 0.0f);
      r.set(Tb303::Accent, 1.0f);
      r.set(Tb303::Cutoff, trial % 3 == 0 ? 25.0f : (trial % 3 == 1 ? 4500.0f : 800.0f));
      r.set(Tb303::Decay, trial % 2 == 0 ? 200.0f : 2500.0f);
      r.set(Tb303::Tune, trial % 2 == 0 ? 12.0f : -12.0f);
      r.set(Tb303::Volume, trial == 5 ? 6.0f : -3.0f);
      std::uint32_t at = 0;
      int held = -1;
      for (int n = 0; n < 40; ++n) {
        const int note = 12 + static_cast<int>(rng.below(100));
        const std::uint8_t flags = static_cast<std::uint8_t>((rng.below(2) != 0 ? o3sdk::dsp::kNoteAccent : 0) | (rng.below(3) == 0 ? o3sdk::dsp::kNoteSlide : 0));
        r.script.on(at, note, flags);
        if (held >= 0 && rng.below(2) != 0) {
          r.script.off(at, held);
        }
        held = note;
        at += 100 + rng.below(4000);
        if (rng.below(3) == 0) {
          r.script.off(at, held);
          held = -1;
          at += rng.below(3000);
        }
        if (rng.below(8) == 0) {
          const std::uint32_t pick = rng.below(3);
          r.script.param(at, static_cast<std::uint16_t>(rng.below(8)), pick == 0 ? 1.0e9f : (pick == 1 ? -1.0e9f : std::nanf("")));  // far outside the range, and not a number
        }
      }
      r.script.all_off(at + 10);
      const auto out = r.render(at + ms(2500));
      CHECK(all_finite_normal(out));
      CHECK(peak(out, 0, out.size()) <= 1.0);
      CHECK(peak(out, out.size() - ms(500), out.size()) == 0.0);  // silence at the end: the envelopes are flushed
    }
  }
}

TEST_CASE("tb303: other sample rates and a prepare() in the middle of a life") {
  for (float rate : {22050.0f, 48000.0f, 96000.0f}) {
    Tb303 v;
    v.prepare(rate);
    o3sdk::dsp::Transport tr;
    tr.frames = 64;
    float z[64] = {};
    float l[64];
    float rr[64];
    o3sdk::dsp::Event ev[1] = {o3sdk::dsp::note_on(36, 100)};
    double e = 0.0;
    for (int b = 0; b < 400; ++b) {
      o3sdk::dsp::ProcessArgs a{b == 0 ? ev : nullptr, b == 0 ? 1u : 0u, z, z, l, rr, 64, &tr};
      v.process(a);
      for (float s : l) {
        REQUIRE(std::isfinite(s));
        e += static_cast<double>(s) * static_cast<double>(s);
      }
    }
    CHECK(e > 1.0);
    v.prepare(rate);  // a reset: silent again
    o3sdk::dsp::ProcessArgs a{nullptr, 0, z, z, l, rr, 64, &tr};
    v.process(a);
    for (float s : l) {
      CHECK(s == 0.0f);
    }
  }
}

TEST_CASE("tb303: a release tail raises no underflow (no subnormal intermediates), both waves, accented and plain") {
  // The host's SSE flags say whether any operation produced a subnormal result (FE_UNDERFLOW); nothing in o3-sdk sets flush-to-zero on
  // the M4F, so a subnormal there costs what it costs. The envelopes are flushed at 1e-9, the filter and the oscillator keep running on a
  // signal that is never tiny, and the output is exactly zero once the amplifier is flushed.
  for (int wave = 0; wave < 2; ++wave) {
    for (int acc = 0; acc < 2; ++acc) {
      INFO("wave " << wave << " accent " << acc);
      Rig r;
      r.set(Tb303::Wave, static_cast<float>(wave));
      r.script.on(100, 36, acc != 0 ? o3sdk::dsp::kNoteAccent : 0);
      r.script.off(ms(300), 36);
      r.engine.start();
      std::feclearexcept(FE_ALL_EXCEPT);
      const auto out = r.render(ms(4000));
      const bool underflow = std::fetestexcept(FE_UNDERFLOW) != 0;
      CHECK_FALSE(underflow);
      CHECK(all_finite_normal(out));
      CHECK(peak(out, ms(3000), out.size()) == 0.0);
    }
  }
}
