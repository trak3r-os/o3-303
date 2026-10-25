// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support: how two renderings of the same note script are compared.
//
// Sample-exact error (10 log10 sum ref^2 / sum err^2) is the right figure while the voice is a deterministic system. With accent and
// resonance it is not: the ladder's Euler sub-steps with a feedback gain of 11 or more and a state clamp behave chaotically, so the
// ORIGINAL differs from itself by -21 dB for a 1-ulp change of the cutoff. For that material the figure is spectral: the energy in
// octave bands of 1024-point frames, compared in dB, and the level per 256-sample window; and the same figures are taken for the original
// against itself with a 1-ulp change, which is the floor no port can beat.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace tb303_test {

struct Spectral {
  double band_mean_db;  // mean |dB difference| of the octave-band energies over the frames and bands that count
  double band_p95_db;   // the 95th percentile of the same
  double level_mean_db; // mean |dB difference| of the RMS of 256-sample windows that count
  std::size_t cells;
};

inline void fft(std::vector<std::complex<double>>& a) {
  const std::size_t n = a.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; (j & bit) != 0; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      std::swap(a[i], a[j]);
    }
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      for (std::size_t k = 0; k < len / 2; ++k) {
        const std::complex<double> u = a[i + k];
        const std::complex<double> v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

constexpr std::size_t kFrame = 1024;
constexpr std::size_t kHop = 512;
constexpr std::size_t kBands = 8;  // octaves from 60 Hz: 60-120, ..., 7.7-15.4 kHz

// Octave-band energies (linear power) of every frame.
inline std::vector<double> band_energies(const std::vector<float>& x, double rate) {
  std::vector<double> out;
  std::vector<std::complex<double>> buf(kFrame);
  for (std::size_t at = 0; at + kFrame <= x.size(); at += kHop) {
    for (std::size_t i = 0; i < kFrame; ++i) {
      const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(kFrame));
      buf[i] = static_cast<double>(x[at + i]) * w;
    }
    fft(buf);
    double e[kBands] = {};
    for (std::size_t k = 1; k < kFrame / 2; ++k) {
      const double f = static_cast<double>(k) * rate / static_cast<double>(kFrame);
      if (f < 60.0) {
        continue;
      }
      const auto b = static_cast<std::size_t>(std::log2(f / 60.0));
      if (b < kBands) {
        e[b] += std::norm(buf[k]);
      }
    }
    for (double v : e) {
      out.push_back(v);
    }
  }
  return out;
}

inline Spectral spectral(const std::vector<float>& ref, const std::vector<float>& got, double rate) {
  Spectral s{0.0, 0.0, 0.0, 0};
  const auto er = band_energies(ref, rate);
  const auto eg = band_energies(got, rate);
  double top = 0.0;
  for (double v : er) {
    top = v > top ? v : top;
  }
  std::vector<double> diffs;
  for (std::size_t i = 0; i < er.size(); ++i) {
    if (er[i] > top * 1e-6) {  // within 60 dB of the loudest cell
      diffs.push_back(std::fabs(10.0 * std::log10((eg[i] + 1e-30) / er[i])));
    }
  }
  s.cells = diffs.size();
  if (!diffs.empty()) {
    double sum = 0.0;
    for (double d : diffs) {
      sum += d;
    }
    s.band_mean_db = sum / static_cast<double>(diffs.size());
    std::vector<double> sorted = diffs;
    std::sort(sorted.begin(), sorted.end());
    s.band_p95_db = sorted[static_cast<std::size_t>(0.95 * static_cast<double>(sorted.size() - 1))];
  }
  // Level per 256-sample window.
  double sum = 0.0;
  std::size_t n = 0;
  double peak = 0.0;
  for (float v : ref) {
    peak = std::fabs(static_cast<double>(v)) > peak ? std::fabs(static_cast<double>(v)) : peak;
  }
  for (std::size_t at = 0; at + 256 <= ref.size(); at += 256) {
    double a = 0.0, b = 0.0;
    for (std::size_t i = 0; i < 256; ++i) {
      a += static_cast<double>(ref[at + i]) * static_cast<double>(ref[at + i]);
      b += static_cast<double>(got[at + i]) * static_cast<double>(got[at + i]);
    }
    if (std::sqrt(a / 256.0) > peak * 0.01) {  // within 40 dB of the peak
      sum += std::fabs(10.0 * std::log10((b + 1e-30) / a));
      ++n;
    }
  }
  s.level_mean_db = n != 0 ? sum / static_cast<double>(n) : 0.0;
  return s;
}

}  // namespace tb303_test
