// SPDX-License-Identifier: GPL-3.0-or-later
// Oracle: the port against Replicant 303's own oscillator, ladder and envelopes (third_party/replicant303, commit d5369be) on the host.
// Both render the same note scripts at 44.1 kHz. Two figures (metrics.h): the sample-exact error, 10 log10 (sum ref^2 / sum err^2), and a
// spectral one (octave-band energies and window levels, in dB). Each case also prints the original against ITSELF with a 1-ulp change of
// the resonance, which is the floor for any port where the ladder is chaotic (accent and resonance).
//
// Why the two differ: the port computes the filter coefficient on a 16-sample grid and ramps between (the original recomputes it every
// sample from pow and tan), uses exp2_fast (2^x, 1e-7 relative) and a rational tan (1.6e-6), reads the phase increment as pitch x (1/fs)
// where the original divides (1 ulp: with a division in the port the steady cases agree 9 dB better, -96 to -105 dB instead of -87 to
// -96, for 14 cycles a sample), and smooths Volume and the accent's lift of the amplifier over 5 ms (the original applies them at once).
// Same maths otherwise: the cached tanh of the ladder states is exactly the value the original recomputes.
//
// What guards what. Where the original is a steady system (it holds 80 dB against its own 1-ulp change) the sample-exact bar is real:
// 75 dB, against 82 to 98 dB measured. Where it is chaotic (accent, slides, resonance of 0.5 and more, the ladder's feedback gain being
// 11 to 17) the sample-exact bar is nearly vacuous: it is the original's own floor less 10 dB (for the accent line, about -15 dB needed
// against a -25 dB self-floor), and it is the SPECTRAL bars, band energies and window levels within 0.1 dB plus twice what the original
// shows against itself, that guard those cases.
#include <doctest/doctest.h>

#include "metrics.h"
#include "ref_engine.h"

#include <cstdio>

using namespace tb303_test;
using o3_303::Tb303;

namespace {

std::vector<Step> plain_line() {
  // A 16-step acid line, no accents or slides (C2 = 36).
  const int n[] = {36, 36, 48, 36, 39, 36, 51, 36, 34, 34, 46, 34, 37, 38, 41, 36};
  std::vector<Step> s;
  for (int v : n) {
    s.push_back({v});
  }
  return s;
}

std::vector<Step> accent_line() {
  auto s = plain_line();
  for (std::size_t i = 0; i < s.size(); i += 3) {
    s[i].accent = true;
  }
  return s;
}

std::vector<Step> slide_line() {
  auto s = accent_line();
  for (std::size_t i = 1; i < s.size(); i += 4) {
    s[i].slide = true;
  }
  s[6].slide = true;
  s[7].slide = true;
  return s;
}

std::vector<Step> rest_line() {
  auto s = slide_line();
  for (std::size_t i : {2u, 5u, 6u, 10u, 11u, 12u}) {
    s[i].rest = true;
  }
  return s;
}

std::vector<Ev> with_defaults(std::vector<Ev> extra, const std::vector<Ev>& notes) {
  auto ev = defaults();
  ev.insert(ev.end(), extra.begin(), extra.end());
  ev.insert(ev.end(), notes.begin(), notes.end());
  return ev;
}

// A 3 s cutoff sweep, 80 Hz to 3 kHz, in steps of 64 samples (one engine block), over held notes with a slide and an accent.
std::vector<Ev> sweep_events() {
  std::vector<Ev> ev;
  const std::uint32_t total = 3 * 44100;
  for (std::uint32_t at = 0; at < total; at += 64) {
    const double f = static_cast<double>(at) / total;
    ev.push_back(param(at, Tb303::Cutoff, static_cast<float>(80.0 * std::pow(3000.0 / 80.0, f))));
  }
  const auto notes = steps_to_events({{36, false, true}, {43, false, true}, {36}, {48}, {36, true}, {39}, {36}, {41, false, false, true}}, 100.0, 0, 0.9);
  ev.insert(ev.end(), notes.begin(), notes.end());
  std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.at < b.at; });
  return ev;
}

std::uint32_t line_length(const std::vector<Step>& s, double bpm) {
  return static_cast<std::uint32_t>(kRate * 60.0 / bpm / 4.0 * static_cast<double>(s.size())) + 20000;  // + the release tail
}

// The same script with the resonance changed by one ulp (what the original cannot hold constant against).
std::vector<Ev> nudged(std::vector<Ev> script) {
  for (Ev& e : script) {
    if (e.kind == Ev::Param && e.id == Tb303::Reso) {
      e.value = std::nextafter(e.value, e.value >= 1.0f ? 0.0f : 2.0f);  // at 1 the way up is clamped away: go down
    }
  }
  return script;
}

struct Result {
  Agreement sample;
  Spectral spec;
  Agreement floor_sample;
  Spectral floor_spec;
};

Result run_case(const char* name, const std::vector<Ev>& script, std::uint32_t total) {
  Tb303 unit;
  const auto ref = render_ref(script, total);
  const auto got = render_unit(unit, script, total);
  const auto alt = render_ref(nudged(script), total);
  Result r{compare(ref, got), spectral(ref, got, kRate), compare(ref, alt), spectral(ref, alt, kRate)};
  std::printf("[oracle tb303] %-30s rms %.3f | port: sample %6.1f dB, band %.3f/%.3f dB (mean/p95), level %.3f dB | original vs itself: sample %6.1f dB, band %.3f/%.3f, level %.3f\n",
              name, r.sample.ref_rms, -r.sample.snr_db, r.spec.band_mean_db, r.spec.band_p95_db, r.spec.level_mean_db, -r.floor_sample.snr_db,
              r.floor_spec.band_mean_db, r.floor_spec.band_p95_db, r.floor_spec.level_mean_db);
  CHECK_MESSAGE(r.sample.ref_rms > 0.01, name << ": the reference must be audible");
  // Where the original is a steady system (it holds 80 dB against its own 1-ulp change) the port must agree to 75 dB; where it is
  // chaotic the port must be within 10 dB of what the original itself holds, and the spectrum and the levels must agree to 0.1 dB plus
  // twice what the original shows against itself (see the top of the file: those are the bars that guard the chaotic cases).
  const double floor_snr = r.floor_sample.snr_db;
  const double need = floor_snr - 10.0 < 75.0 ? floor_snr - 10.0 : 75.0;
  CHECK_MESSAGE(r.sample.snr_db >= need, name << ": sample-exact agreement " << r.sample.snr_db << " dB, need " << need);
  CHECK_MESSAGE(r.spec.band_mean_db <= 0.1 + 2.0 * r.floor_spec.band_mean_db, name << ": band energies differ by " << r.spec.band_mean_db << " dB");
  CHECK_MESSAGE(r.spec.level_mean_db <= 0.1 + 2.0 * r.floor_spec.level_mean_db, name << ": levels differ by " << r.spec.level_mean_db << " dB");
  return r;
}

}  // namespace

TEST_CASE("tb303 oracle: note lines through both voices, each wave") {
  const double bpm = 125.0;
  for (int wave = 0; wave < 2; ++wave) {
    const char* w = wave == 0 ? "saw" : "sqr";
    const std::vector<Ev> wv = {param(0, Tb303::Wave, static_cast<float>(wave))};
    struct L {
      const char* name;
      std::vector<Step> steps;
    } lines[] = {{"plain notes", plain_line()}, {"accents", accent_line()}, {"slides", slide_line()}, {"rests and slides", rest_line()}};
    for (const L& l : lines) {
      char name[64];
      std::snprintf(name, sizeof(name), "%s, %s", w, l.name);
      (void)run_case(name, with_defaults(wv, steps_to_events(l.steps, bpm)), line_length(l.steps, bpm));
    }
  }
}

TEST_CASE("tb303 oracle: a cutoff sweep over held notes, each wave") {
  for (int wave = 0; wave < 2; ++wave) {
    char name[64];
    std::snprintf(name, sizeof(name), "%s, cutoff sweep 80 Hz..3 kHz", wave == 0 ? "saw" : "sqr");
    (void)run_case(name, with_defaults({param(0, Tb303::Wave, static_cast<float>(wave))}, sweep_events()), 3 * 44100 + 10000);
  }
}

TEST_CASE("tb303 oracle: other settings (resonance, decay, env mod, tune, cutoff)") {
  const double bpm = 125.0;
  struct S {
    const char* name;
    std::vector<Ev> extra;
  } sets[] = {
      {"reso 0.3", {param(0, Tb303::Reso, 0.3f)}},
      {"reso 0.95", {param(0, Tb303::Reso, 0.95f)}},
      {"decay 200 ms, env mod 1.0", {param(0, Tb303::Decay, 200.0f), param(0, Tb303::EnvMod, 1.0f)}},
      {"decay 2500 ms, cutoff 90 Hz", {param(0, Tb303::Decay, 2500.0f), param(0, Tb303::Cutoff, 90.0f)}},
      {"tune +7 st, accent 1.0", {param(0, Tb303::Tune, 7.0f), param(0, Tb303::Accent, 1.0f)}},
      {"cutoff 4500 Hz, env mod 0", {param(0, Tb303::Cutoff, 4500.0f), param(0, Tb303::EnvMod, 0.0f)}},
      // The corner: the ladder at full feedback with the cutoff and the accent at the top (g reaches its ceiling of 0.95).
      {"reso 1, cutoff 4500, accent 1", {param(0, Tb303::Reso, 1.0f), param(0, Tb303::Cutoff, 4500.0f), param(0, Tb303::Accent, 1.0f)}},
      {"reso 1, cutoff 4500, accent 1, SQR", {param(0, Tb303::Reso, 1.0f), param(0, Tb303::Cutoff, 4500.0f), param(0, Tb303::Accent, 1.0f), param(0, Tb303::Wave, 1.0f)}},
      {"reso 1, cutoff 4500, accent 1, env mod 1", {param(0, Tb303::Reso, 1.0f), param(0, Tb303::Cutoff, 4500.0f), param(0, Tb303::Accent, 1.0f), param(0, Tb303::EnvMod, 1.0f)}},
      {"reso 1, cutoff 1500, accent 1, decay 200", {param(0, Tb303::Reso, 1.0f), param(0, Tb303::Cutoff, 1500.0f), param(0, Tb303::Accent, 1.0f), param(0, Tb303::Decay, 200.0f)}},
      {"reso 0.95, cutoff 4500, accent 1, tune -12", {param(0, Tb303::Reso, 0.95f), param(0, Tb303::Cutoff, 4500.0f), param(0, Tb303::Accent, 1.0f), param(0, Tb303::Tune, -12.0f)}},
      {"reso 1, cutoff 90, accent 1, env mod 1, decay 2500", {param(0, Tb303::Reso, 1.0f), param(0, Tb303::Cutoff, 90.0f), param(0, Tb303::Accent, 1.0f), param(0, Tb303::EnvMod, 1.0f), param(0, Tb303::Decay, 2500.0f)}},
  };
  for (const S& s : sets) {
    const auto line = slide_line();
    (void)run_case(s.name, with_defaults(s.extra, steps_to_events(line, bpm)), line_length(line, bpm));
  }
}
