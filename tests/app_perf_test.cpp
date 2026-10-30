// SPDX-License-Identifier: GPL-3.0-or-later
// Performance gestures end to end: the four performance buttons and the keyboard that transposes the pattern or plays the voice.
// (Split from the former o3_303_app_test.cpp; the hands are in app_hands.h.)
#include "app_hands.h"

#include <o3_303/app_access.h>
#include <o3_303/units/distortion/distortion.h>

#include <complex>
#include <cstring>
#include <utility>

TEST_CASE("performance: MUTE, ACC ALL, SLIDE ALL and DELAY THROW on the four buttons (assigned), momentary, and Shift + the button latches") {
  Hands t;
  // The defaults are Note = MUTE, Instr = ACC ALL, Vol = FILL, Fx = FREEZE (tests/perf_buttons_test.cpp); SLIDE ALL and THROW, which
  // this test checks as it did before C4, are one assignment each away.
  t.assign_perf(Button::Vol, PerfAction::SlideAll);
  t.assign_perf(Button::Fx, PerfAction::Throw, true);  // on PERFORM: there the colour buttons are the live actions
  seq::Sequencer& sequencer = app_sequencer();
  const std::uint8_t kMute = seq::kPerfMute;
  const std::uint8_t kAcc = seq::kPerfAccAll;
  const std::uint8_t kSlide = seq::kPerfSlideAll;
  CHECK(sequencer.perf() == 0);

  // Momentary: the flag is on while the button is down.
  t.press(Button::Note);
  CHECK(sequencer.perf() == kMute);
  t.release(Button::Note);
  CHECK(sequencer.perf() == 0);
  t.press(Button::Instr);
  CHECK(sequencer.perf() == kAcc);
  t.release(Button::Instr);
  t.press(Button::Vol);
  CHECK(sequencer.perf() == kSlide);
  t.press(Button::Instr);
  CHECK(sequencer.perf() == (kSlide | kAcc));  // together
  t.release(Button::Vol);
  t.release(Button::Instr);
  CHECK(sequencer.perf() == 0);

  // DELAY THROW is the engine's send A, not a sequencer flag.
  const float base = t.h->engine().mix(0, dsp::MixField::SendA);
  CHECK(base == doctest::Approx(0.3f));
  t.press(Button::Fx);
  t.wait(10);
  CHECK(sequencer.perf() == 0);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(1.0f));
  t.release(Button::Fx);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(base));

  // Latched with Shift, off with Shift again; the button's own release does not undo the latch.
  t.shift_tap(Button::Note);
  CHECK(sequencer.perf() == kMute);
  t.shift_tap(Button::Instr);
  CHECK(sequencer.perf() == (kMute | kAcc));
  t.shift_tap(Button::Note);
  CHECK(sequencer.perf() == kAcc);
  t.shift_tap(Button::Fx);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(1.0f));
  t.shift_tap(Button::Fx);
  t.wait(10);
  CHECK(t.h->engine().mix(0, dsp::MixField::SendA) == doctest::Approx(base));

  // A latch stays when the screen is left; a button held when it is left is let go (its release would reach another screen).
  t.press(Button::Vol);
  CHECK(sequencer.perf() == (kAcc | kSlide));
  t.tap(Button::Params);  // INSTRUMENT (the File button is the 303 screen's and INSTRUMENT's)
  CHECK(std::strcmp(t.title(), "INSTRUMENT VOICE") == 0);
  CHECK(sequencer.perf() == kAcc);
  t.release(Button::Vol);
  t.tap(Button::Pattern);
  CHECK(std::strcmp(t.title(), "STEPS") == 0);
  CHECK(sequencer.perf() == kAcc);
  t.tap(Button::Performance);
  t.shift_tap(Button::Instr);
  CHECK(sequencer.perf() == 0);
}

// An in-place radix-2 FFT (host tests may use double).
void fft(std::vector<std::complex<double>>& a) {
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
    const std::complex<double> wlen(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      for (std::size_t j = 0; j < len / 2; ++j) {
        const std::complex<double> u = a[i + j];
        const std::complex<double> v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
}

// The fundamental, in Hz, of the left channel of `lr` over `frames` frames from sample index `from`: the harmonic product spectrum of a
// Hann-windowed block (five harmonics), which does not mistake a filter's resonance on the fourth harmonic for the note.
double pitch_hz(const std::vector<std::int16_t>& lr, std::size_t from, std::size_t frames) {
  const std::size_t n = 32768;
  std::vector<std::complex<double>> a(n, std::complex<double>(0.0, 0.0));
  const std::size_t used = frames < n ? frames : n;
  for (std::size_t i = 0; i < used; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(used - 1));
    a[i] = std::complex<double>(static_cast<double>(lr[from + 2 * i]) * w, 0.0);
  }
  fft(a);
  std::vector<double> mag(n / 2);
  for (std::size_t k = 0; k < n / 2; ++k) {
    mag[k] = std::abs(a[k]);
  }
  const double bin_hz = 44100.0 / static_cast<double>(n);
  double best = 0.0;
  std::size_t best_k = 0;
  for (std::size_t k = static_cast<std::size_t>(40.0 / bin_hz); k < static_cast<std::size_t>(500.0 / bin_hz); ++k) {
    const double product = mag[k] * mag[2 * k] * mag[3 * k] * mag[4 * k] * mag[5 * k];
    if (product > best) {
      best = product;
      best_k = k;
    }
  }
  return static_cast<double>(best_k) * bin_hz;
}

TEST_CASE("playing with TRNSP on, the keyboard transposes the pattern (the bottom left C is none); off, it plays the voice instead") {
  Hands t;
  seq::Sequencer& sequencer = app_sequencer();
  // A steady tone: B01 with sixteen C2, every step sliding (SLIDE ALL latched), so the pitch is the transposition's.
  t.select_pattern(12);
  t.tap(Button::Rec);
  for (unsigned i = 0; i < 16; ++i) {
    t.pad(36);
  }
  t.tap(Button::Rec);
  t.assign_perf(Button::Vol, PerfAction::SlideAll, true);
  t.shift_tap(Button::Vol);  // (on PERFORM: the latch stays on when the screen is left)
  CHECK(sequencer.perf() == seq::kPerfSlideAll);
  t.to_home();
  // A plain saw so that the pitch can be measured: the filter wide open with no resonance and no envelope, the distortion off, no delay
  // send (no echo of the old pitch in the measure).
  t.tap(Button::F1);
  t.turn(250);
  t.tap(Button::F2);
  t.turn(-100);
  t.tap(Button::F3);
  t.turn(-100);
  t.to_instrument(kInstFx);
  t.tap(Button::F7);  // DLY SEND
  t.turn(-40);
  t.tap(Button::F4);  // MODEL
  t.turn(-10);
  CHECK(t.param(kDist, Distortion::Active) == doctest::Approx(0.0f));
  t.to_home();
  CHECK(t.led(kTrnspPad) == app::kLedLow);  // the keyboard plays the voice by default
  t.pad(kTrnspPad);                         // TRNSP: the keys transpose the running pattern
  CHECK(t.led(kTrnspPad) == app::kLedOn);
  t.h->record(true);
  t.tap(Button::Play);
  t.wait(1500);
  const std::size_t window = 22050;  // half a second of frames
  const auto measure = [&](unsigned settle_ms) {
    t.wait(settle_ms);
    const std::size_t at = t.h->audio().size() - 2 * window;
    return pitch_hz(t.h->audio(), at, window);
  };
  const double base = measure(500);
  CHECK(sequencer.live_transpose() == 0);
  CHECK(base > 55.0);
  CHECK(base < 80.0);  // C2 is 65.4 Hz

  t.pad(key_pad(48));  // C3: twelve semitones above the bottom left C
  t.wait(30);
  CHECK(sequencer.live_transpose() == 12);
  CHECK(t.led(key_pad(48)) == app::kLedMid);
  CHECK(t.led(key_pad(36)) == app::kLedOn);  // the pattern's own note (all C2), at full level
  const double octave_up = measure(1200);
  CHECK(octave_up / base > 1.9);
  CHECK(octave_up / base < 2.1);

  t.pad(key_pad(43));  // G2: seven semitones
  CHECK(sequencer.live_transpose() == 7);
  const double fifth = measure(1200);
  CHECK(fifth / base > 1.45);  // 2^(7/12) = 1.498
  CHECK(fifth / base < 1.55);

  t.pad(key_pad(36));  // back to none
  t.wait(30);
  CHECK(sequencer.live_transpose() == 0);
  CHECK(t.led(key_pad(48)) == app::kLedDim);
  const double back = measure(1200);
  CHECK(back / base > 0.95);
  CHECK(back / base < 1.05);

  t.pad(kTrnspPad);  // TRNSP off: the keyboard plays the voice, the transposition is left alone
  t.pad(key_pad(48));
  t.wait(30);
  CHECK(sequencer.live_transpose() == 0);
  CHECK(t.led(kTrnspPad) == app::kLedLow);
  t.pad(kTrnspPad);
  t.wait(40);
  CHECK(t.led(kTrnspPad) == app::kLedOn);

  // The transposition ends with the run, also when another screen is up.
  t.pad(key_pad(47));
  CHECK(sequencer.live_transpose() == 11);
  t.tap(Button::File);
  CHECK(std::strcmp(t.title(), "FILE PROJECTS") == 0);
  t.tap(Button::Play);  // Play is global
  t.wait(50);
  CHECK_FALSE(t.h->engine().playing());
  CHECK(sequencer.live_transpose() == 0);
  t.tap(Button::Performance);
  t.shift_tap(Button::Vol);
  CHECK(sequencer.perf() == 0);
}
