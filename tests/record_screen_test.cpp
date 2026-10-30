// SPDX-License-Identifier: GPL-3.0-or-later
// RECORD and HOOKS (docs/UX-303.md 5.10 and 5.11, docs/UX-PLAN.md R1): the pure view logic (the bar arithmetic, the list of bars and of sources, the clock, the envelope) and the
// screens end to end in the simulator, on a scratch card: a one-bar take of the master starts on the bar's sample and is exactly a bar long, the WAV lands in Samples/ as the take
// is, BARS that do not fit the buffer are refused, FREE and F1, DISCARD's second press, Shift + Rec from another screen (it arms, stops and saves without changing screen, and
// the plain Rec stays the step record), the update blocker, a transport that stops under a take, the input sources on the fake codec, a full buffer, and the HOOKS placeholder.
// The pictures are record.txt's and hooks.txt's.
#include <doctest/doctest.h>

#include "../app/src/hooks_screen.h"
#include "../app/src/main_screen.h"
#include "../app/src/project_service.h"
#include "../app/src/record_screen.h"

#include <o3sdk/app/app.h>
#include <o3_303/app_access.h>
#include <o3sdk/hal/audio.h>
#include <o3sdk/platform/host/dir_card.h>
#include <o3sdk/platform/host/fake_capture.h>
#include <o3sdk/platform/host/fake_firmware.h>
#include <o3sdk/sim/harness.h>

#include "support/mem_card.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace o3sdk;
using namespace o3_303;
using app::Button;
namespace fs = std::filesystem;
namespace host = o3sdk::platform::host;
using hal::record::State;

namespace {

std::string clock_str(std::uint32_t frames, bool tenth) {
  char s[16];
  (void)clock_text(frames, 44'100, tenth, s, sizeof s);
  return s;
}
std::string bars_str(unsigned bars) {
  char s[8];
  (void)bars_text(bars, s, sizeof s);
  return s;
}

// The app on a scratch card (or none), with the fake codec input when asked.
struct Session {
  host::ScratchDir dir{"o3303-r1"};
  host::DirCard card{dir.path()};
  host::FakeCapture capture;
  host::FakeFirmwareSink fw;
  std::unique_ptr<sim::Harness> h;

  explicit Session(bool with_card = true, bool with_input = false) {
    REQUIRE(dir.ok());
    h = std::make_unique<sim::Harness>(o3sdk_app(), &fw, nullptr, with_card ? static_cast<host::HostCard*>(&card) : nullptr, "o3_303", with_input ? &capture : nullptr);
    REQUIRE(h->start());
    h->run_ms(100);
  }
  app::Context& ctx() { return h->runtime().context(); }
  app::Recorder& rec() { return ctx().recorder(); }
  const char* title() { return ctx().screen()->title(); }
  RecordScreen& record() {
    REQUIRE(std::strcmp(title(), "RECORD") == 0);
    return *static_cast<RecordScreen*>(ctx().screen());
  }
  void wait(unsigned ms) { h->run_ms(ms); }
  void press(Button b) {
    h->press(b);
    h->run_ms(2);
  }
  void release(Button b) {
    h->release(b);
    h->run_ms(2);
  }
  void tap(Button b, unsigned hold = 40) {
    press(b);
    h->run_ms(hold);
    release(b);
  }
  void shift_tap(Button b) {
    press(Button::Shift);
    tap(b);
    release(Button::Shift);
  }
  void turn(int d) {
    h->turn(d);
    h->run_ms(2);
  }
  // Runs until `pred` holds, at most `limit_ms`; true when it did.
  template <class P>
  bool until(P pred, unsigned limit_ms) {
    for (unsigned i = 0; i < limit_ms; ++i) {
      if (pred()) {
        return true;
      }
      h->run_ms(1);
    }
    return pred();
  }
  void open_record() {
    tap(Button::SampleRec);
    REQUIRE(std::strcmp(title(), "RECORD") == 0);
  }
  // F3 until the list shows `bars` (0: FREE).
  void choose_bars(unsigned bars) {
    for (unsigned i = 0; i < kBarChoices && record().bars() != bars; ++i) {
      tap(Button::F3);
    }
    REQUIRE(record().bars() == bars);
  }
  // A FREE take of the master of about `ms` milliseconds, stopped, finished and not saved; back on RECORD.
  void finished_take(unsigned ms = 300) {
    open_record();
    choose_bars(0);
    tap(Button::F1);
    REQUIRE(rec().state() == State::Recording);
    wait(ms);
    tap(Button::F1);
    REQUIRE(until([&] { return rec().state() == State::Done; }, 200));
  }
  // Runs until the project service and the files service are both idle.
  bool project_settled(unsigned limit_ms = 4000) { return until([&] { return app_project().idle() && !ctx().files().busy(); }, limit_ms); }
  fs::path samples() const { return fs::path(dir.path()) / "Samples"; }
  std::vector<fs::path> takes() const {
    std::vector<fs::path> v;
    if (fs::exists(samples())) {
      for (const auto& e : fs::directory_iterator(samples())) {
        v.push_back(e.path());
      }
    }
    return v;
  }
  // Runs until the take is saved and the buffer is free (Idle, no card job): the whole save takes a few dozen passes.
  bool saved(unsigned limit_ms = 4000) {
    return until([&] { return rec().state() == State::Idle && !ctx().files().busy(); }, limit_ms);
  }
};

// A 16-bit PCM WAV of the card: the data chunk's frames and its samples (stereo).
struct Wav {
  bool ok = false;
  unsigned channels = 0;
  std::uint32_t rate = 0;
  std::vector<std::int16_t> pcm;
};
Wav read_wav(const fs::path& p) {
  Wav w;
  std::ifstream in(p, std::ios::binary);
  std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) {
    return w;
  }
  w.channels = b[22] | (b[23] << 8);
  w.rate = b[24] | (b[25] << 8) | (b[26] << 16) | (static_cast<std::uint32_t>(b[27]) << 24);
  const std::size_t n = (b.size() - 44) / 2;
  w.pcm.resize(n);
  std::memcpy(w.pcm.data(), b.data() + 44, n * 2);
  w.ok = true;
  return w;
}

}  // namespace

// ---- The view logic ----------------------------------------------------------------------------------------------------------

TEST_CASE("bar_frames: 16 sixteenths at the engine's rate, and the bars that fit 30 s") {
  // (the expectations come from the engine's rate, which is 44,100 Hz or 44,118 Hz by the build: O3SDK_RATE_44100)
  const std::uint64_t rate_mhz = hal::audio::sample_rate_mhz();
  CHECK(bar_frames(125'000) == rate_mhz * 240u / 125'000u);  // 1.92 s
  CHECK(bar_frames(120'000) == rate_mhz * 2u / 1000u);  // 2 s
  CHECK(bar_frames(125'000) >= 84'672u);
  CHECK(bar_frames(125'000) <= 84'706u);
  CHECK(bar_frames(0) == 0u);
  CHECK(bars_fit(8, 125'000));  // 15.4 s
  CHECK(bars_fit(8, 70'000));   // 27.4 s
  for (std::uint32_t t = 50'000; t <= 80'000; t += 500) {  // the edge is wherever the rate puts it: the rule is the same
    CHECK(bars_fit(8, t) == (8ull * bar_frames(t) + kTakeSlack <= kTakeFrames));
  }
  CHECK_FALSE(bars_fit(8, 60'000));
  CHECK(bars_fit(4, 60'000));
  CHECK(bars_fit(0, 20'000));  // FREE always fits
  CHECK_FALSE(bars_fit(1, 0));
  CHECK_FALSE(bars_fit(1, 125'000, bar_frames(125'000)));  // not to the last sample: a block of room
  CHECK(bars_fit(1, 125'000, bar_frames(125'000) + kTakeSlack));
  CHECK(max_bars(125'000) == 8u);
  CHECK(max_bars(55'000) == 4u);
  CHECK(max_bars(20'000) == 2u);  // a bar is 12 s
  CHECK(max_bars(10'000) == 1u);
  CHECK(max_bars(7'000) == 0u);   // 34 s
  static_assert(kTakeFrames == 1'323'540u, "30 s");
}

TEST_CASE("bars: the list is 1 2 4 8 FREE, kept inside it; the clock and the bar counter") {
  CHECK(bars_str(1) == "1");
  CHECK(bars_str(8) == "8");
  CHECK(bars_str(0) == "FREE");
  CHECK(bars_index(4) == 2u);
  CHECK(bars_index(0) == 4u);
  CHECK(bars_index(3) == 0u);  // not in the list: the first place
  CHECK(step_bars_index(0, -1) == 0u);
  CHECK(step_bars_index(0, 1) == 1u);
  CHECK(step_bars_index(4, 1) == 4u);
  CHECK(step_bars_index(3, 5) == 4u);
  CHECK(step_bars_index(9, 1) == 1u);  // a place past the list counts as the first
  CHECK(clock_str(0, false) == "0:00");
  CHECK(clock_str(44'100 * 5, false) == "0:05");
  CHECK(clock_str(44'100 * 65 + 22'050, true) == "1:05.5");
  CHECK(clock_str(44'099, true) == "0:00.9");
  CHECK(bar_of(0, 84'672, 4) == 1u);
  CHECK(bar_of(84'671, 84'672, 4) == 1u);
  CHECK(bar_of(84'672, 84'672, 4) == 2u);
  CHECK(bar_of(84'672 * 9, 84'672, 4) == 4u);   // kept at the take's length
  CHECK(bar_of(84'672 * 9, 84'672, 0) == 10u);  // FREE: no limit
  CHECK(bar_of(5, 0, 4) == 1u);
}

TEST_CASE("sources: MASTER, LINE, MIC, RADIO, USB, the ones that are not offered skipped") {
  using S = app::AudioIn::Source;
  CHECK(source_of(S::LineMix) == RecSource::Line);
  CHECK(source_of(S::LineRight) == RecSource::Line);
  CHECK(source_of(S::MicHigh) == RecSource::Mic);
  CHECK(source_of(S::Radio) == RecSource::Radio);
  CHECK(source_of(S::Usb) == RecSource::Usb);
  CHECK(input_source(RecSource::Line) == S::LineMix);
  CHECK(input_source(RecSource::Mic) == S::MicLow);
  CHECK(std::string(source_name(true, S::LineMix)) == "MASTER");
  CHECK(std::string(source_name(false, S::LineLeft)) == "LINE L");
  CHECK(std::string(source_name(false, S::MicHigh)) == "MIC HI");
  CHECK(step_source(RecSource::Master, 1, true, true, true) == RecSource::Line);
  CHECK(step_source(RecSource::Line, 1, true, true, true) == RecSource::Mic);
  CHECK(step_source(RecSource::Mic, 1, true, true, true) == RecSource::Radio);
  CHECK(step_source(RecSource::Radio, 1, true, true, true) == RecSource::Usb);
  CHECK(step_source(RecSource::Usb, 1, true, true, true) == RecSource::Master);
  CHECK(step_source(RecSource::Master, -1, true, true, true) == RecSource::Usb);
  CHECK(step_source(RecSource::Mic, 1, true, false, true) == RecSource::Usb);  // no radio
  CHECK(step_source(RecSource::Mic, 1, true, false, false) == RecSource::Master);
  CHECK(step_source(RecSource::Master, 1, false, true, true) == RecSource::Master);  // no input at all: MASTER is all there is
  CHECK(step_source(RecSource::Master, -1, false, true, true) == RecSource::Master);
}

TEST_CASE("Envelope: a peak a column over the span, built in slices, frames past the span ignored") {
  Envelope e;
  e.reset(Envelope::kCols * 10);  // 10 frames a column
  std::vector<std::int16_t> pcm(Envelope::kCols * 10 * 2, 0);
  pcm[(5 * 10 + 3) * 2] = 1000;       // column 5, left
  pcm[(5 * 10 + 4) * 2 + 1] = -2000;  // column 5, right, negative
  pcm[(95 * 10 + 9) * 2] = 32767;     // the last frame of the last column
  pcm[(7 * 10) * 2] = -32768;         // the first frame of column 7
  e.add(pcm.data(), 2, 0, 400);  // in slices that cut the columns
  e.add(pcm.data(), 2, 400, 403);
  e.add(pcm.data(), 2, 403, static_cast<std::uint32_t>(pcm.size() / 2));
  CHECK(e.peak(5) == 2000u);
  CHECK(e.peak(4) == 0u);
  CHECK(e.peak(7) == 32767u);  // -32768 is kept inside the scale
  CHECK(e.peak(95) == 32767u);
  CHECK(e.peak(96) == 0u);
  e.add(pcm.data(), 2, 0, 100'000);  // past the span: nothing out of bounds (the sanitizer build holds it)
  e.reset(0);
  CHECK(e.span() == 1u);
  CHECK(e.peak(5) == 0u);
  e.add(nullptr, 2, 0, 1);  // no buffer: nothing
}

TEST_CASE("Envelope: the columns of a span that does not divide by their count hold every frame once") {
  Envelope e;
  e.reset(1000);  // 10.4 frames a column
  std::vector<std::int16_t> pcm(1000, 0);
  for (unsigned f = 0; f < 1000; ++f) {
    pcm[f] = static_cast<std::int16_t>(f + 1);  // a ramp: a column's peak is its last frame's value
  }
  e.add(pcm.data(), 1, 0, 1000);
  std::uint16_t last = 0;
  for (unsigned c = 0; c < Envelope::kCols; ++c) {
    CHECK(e.peak(c) > last);  // strictly rising: no frame went to the wrong column, none skipped a column
    last = e.peak(c);
  }
  CHECK(e.peak(Envelope::kCols - 1) == 1000u);
}

// ---- The screens ---------------------------------------------------------------------------------------------------------------

TEST_CASE("the Sample buttons: Sample Recorder opens RECORD, the three others HOOKS; Rec alone stays the step record") {
  Session s;
  s.tap(Button::SampleRec);
  CHECK(std::strcmp(s.title(), "RECORD") == 0);
  s.tap(Button::SampleRec);  // again: nothing
  CHECK(std::strcmp(s.title(), "RECORD") == 0);
  for (Button b : {Button::SampleLoad, Button::SamplePlay, Button::SampleEdit}) {
    s.tap(Button::SampleRec);
    s.tap(b);
    CHECK(std::strcmp(s.title(), "HOOKS") == 0);
  }
  s.tap(Button::Pattern);
  REQUIRE(std::strcmp(s.title(), "RECORD") != 0);
  auto& home = static_cast<MainScreen*>(s.ctx().screen())->home_map();
  CHECK_FALSE(home.rec());
  s.tap(Button::Rec);  // the plain Rec: write mode (a step record), never an audio take
  CHECK(home.rec());
  CHECK(s.rec().state() == State::Idle);
  s.open_record();
  CHECK(home.rec());  // RECORD's own Rec is the home map's too
  s.tap(Button::Rec);
  CHECK_FALSE(home.rec());
  CHECK(s.rec().state() == State::Idle);
}

TEST_CASE("HOOKS: the placeholder takes no pad, key or turn, and the pads are dark") {
  Session s;
  s.tap(Button::SampleLoad);
  REQUIRE(std::strcmp(s.title(), "HOOKS") == 0);
  s.h->pad_down(0);
  s.wait(20);
  s.h->pad_up(0);
  s.turn(5);
  s.tap(Button::F1);
  s.tap(Button::Rec);
  CHECK(std::strcmp(s.title(), "HOOKS") == 0);
  for (unsigned p = 0; p < 48; ++p) {
    CHECK(s.h->leds().level(p) == 0u);
  }
  CHECK(s.rec().state() == State::Idle);
}

TEST_CASE("a one-bar take of the master starts on the bar's sample, is exactly a bar long and lands in Samples/ as it is") {
  Session s;
  s.open_record();
  s.choose_bars(1);
  CHECK(s.record().master_source());
  s.tap(Button::F1);  // ARM: the transport is stopped, the take waits for it
  CHECK(s.rec().state() == State::Waiting);
  CHECK(s.rec().tap() == hal::record::Tap::Master);
  s.h->record(true);
  s.tap(Button::Play);
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 4000));
  const std::uint32_t frames = s.rec().frames();
  // 125 BPM: 84,672 frames. The transport's grid is a Q32.32 position, so the sixteenths fall on whole samples and a bar is that, or one more: the take is the frames between two of them.
  const std::uint32_t bar = bar_frames(s.ctx().engine().tempo());
  CHECK(bar == static_cast<std::uint64_t>(hal::audio::sample_rate_mhz()) * 240u / 125'000u);
  CHECK(frames >= bar);
  CHECK(frames <= bar + 1u);
  CHECK(s.rec().overrun() == 0u);
  CHECK(s.record().envelope().span() == bar);

  // The take is what the main output played from the first sample of the bar: a stretch of the rendered audio, bit for bit, and it begins at the very frame the line first sounded (the
  // first step's note, at the transport's position 0): a take a block (64 frames) early would open with silence, a late one would open on a note already running.
  const std::int16_t* take = s.record().take_pcm();
  const std::vector<std::int16_t>& out = s.h->audio();
  std::size_t first_sound = 0;
  while (first_sound < frames && take[first_sound * 2] == 0 && take[first_sound * 2 + 1] == 0) {
    ++first_sound;
  }
  CHECK(first_sound == 0u);
  std::size_t out_first = 0;
  while (out_first < out.size() / 2 && out[out_first * 2] == 0 && out[out_first * 2 + 1] == 0) {
    ++out_first;
  }
  REQUIRE(out_first < out.size() / 2);
  bool found = false;
  std::size_t offset = 0;
  for (offset = 0; offset + frames <= out.size() / 2 && offset < 4096; ++offset) {
    if (std::memcmp(out.data() + offset * 2, take, 64u * 4u) == 0 && std::memcmp(out.data() + (offset + frames - 64u) * 2, take + (frames - 64u) * 2, 64u * 4u) == 0) {
      found = true;
      break;
    }
  }
  CHECK(found);
  if (found) {
    CHECK(offset == out_first);  // the take began on the bar's sample
    CHECK(std::memcmp(out.data() + offset * 2, take, static_cast<std::size_t>(frames) * 4u) == 0);
  }

  // F6 SAVE: Samples/take_001.wav, 44 bytes of header and the take's frames.
  s.tap(Button::F6);
  REQUIRE(s.until([&] { return !s.record().saving() && s.rec().state() == State::Idle; }, 4000));
  const std::vector<fs::path> files = s.takes();
  REQUIRE(files.size() == 1);
  CHECK(files[0].filename().string() == "take_001.wav");
  const Wav w = read_wav(files[0]);
  REQUIRE(w.ok);
  CHECK(w.channels == 2u);
  CHECK(w.rate >= 44'100u);
  REQUIRE(w.pcm.size() == static_cast<std::size_t>(frames) * 2u);  // the whole take, nothing more
  CHECK(std::memcmp(w.pcm.data(), take, w.pcm.size() * 2u) == 0);
  // The buffer is free again: a second take goes to take_002.
  s.tap(Button::F1);
  CHECK(s.rec().state() == State::Waiting);  // (the transport plays: the next bar)
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 4000));
  s.tap(Button::F6);
  REQUIRE(s.until([&] { return !s.record().saving() && s.rec().state() == State::Idle; }, 4000));
  CHECK(s.takes().size() == 2u);
  CHECK(fs::exists(s.samples() / "take_002.wav"));
}

TEST_CASE("a take armed while the line plays starts at the next bar, not before") {
  Session s;
  s.tap(Button::Play);
  s.wait(500);  // a quarter of the way into the first bar at 125 BPM
  s.open_record();
  s.choose_bars(1);
  s.tap(Button::F1);
  CHECK(s.rec().state() == State::Waiting);
  s.wait(300);
  CHECK(s.rec().state() == State::Waiting);  // the bar is 1.92 s long: this is not its end
  REQUIRE(s.until([&] { return s.rec().state() == State::Recording; }, 3000));
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 4000));
  CHECK(s.rec().frames() >= bar_frames(125'000));
  CHECK(s.rec().frames() <= bar_frames(125'000) + 1u);
}

TEST_CASE("BARS that do not fit the buffer at the tempo are refused, FREE never is") {
  Session s;
  REQUIRE(s.h->engine().set_tempo(55'000));
  s.wait(20);
  s.open_record();
  s.choose_bars(8);
  s.tap(Button::F1);
  CHECK(s.rec().state() == State::Idle);  // 8 bars at 55 BPM are 35 s
  CHECK(s.record().last_view().fits == false);
  s.choose_bars(4);
  s.tap(Button::F1);  // 4 bars: 17.5 s
  CHECK(s.rec().state() == State::Waiting);
  s.tap(Button::F1);  // STOP
  s.wait(20);
  CHECK(s.rec().state() == State::Idle);  // a take stopped before it began is thrown away by itself
  s.choose_bars(0);
  s.tap(Button::F1);
  CHECK(s.rec().state() == State::Recording);  // FREE with the transport stopped starts at once
  s.tap(Button::F1);
  s.wait(20);
  CHECK(s.rec().state() == State::Done);
}

TEST_CASE("FREE: F1 stops it, DISCARD asks twice, and the second press must come within 5 s") {
  Session s;
  s.open_record();
  s.choose_bars(0);
  s.tap(Button::F1);
  CHECK(s.rec().state() == State::Recording);
  s.wait(500);
  CHECK(s.rec().frames() > 20'000u);
  s.tap(Button::F1);
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 200));
  CHECK(s.rec().frames() > 20'000u);
  s.tap(Button::F7);  // asks
  CHECK(s.rec().state() == State::Done);
  s.tap(Button::F7);  // sure
  CHECK(s.rec().state() == State::Idle);
  // Arming over a take that was not dealt with is refused: SAVE or DISCARD first.
  s.tap(Button::F1);
  s.wait(300);
  s.tap(Button::F1);
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 200));
  s.tap(Button::F1);
  CHECK(s.rec().state() == State::Done);
  s.tap(Button::F7);
  s.wait(5200);  // the question went stale
  s.tap(Button::F7);
  CHECK(s.rec().state() == State::Done);  // asks again
  s.tap(Button::F7);
  CHECK(s.rec().state() == State::Idle);
  CHECK(s.takes().empty());
}

TEST_CASE("Settings are refused while a take waits or records; the gain and the monitor are not") {
  Session s(true, true);
  s.open_record();
  s.choose_bars(0);
  s.tap(Button::F1);
  REQUIRE(s.rec().state() == State::Recording);
  const unsigned bars = s.record().bars();
  s.tap(Button::F3);  // BARS: refused
  CHECK(s.record().bars() == bars);
  s.tap(Button::F2);  // SOURCE: refused
  CHECK(s.record().master_source());
  s.tap(Button::F5);  // MONITOR is live
  CHECK(s.ctx().audio_in().monitor() == app::AudioIn::kMonitorOn);
  s.tap(Button::F5);
  CHECK(s.ctx().audio_in().monitor() == 0u);
  s.tap(Button::F1);
  s.wait(20);
  CHECK(s.rec().state() == State::Done);
}

TEST_CASE("Shift + Rec from another screen arms, stops and saves a take without changing screen") {
  Session s;
  s.tap(Button::Pattern);
  s.tap(Button::Play);
  s.wait(300);
  const char* home_title = s.title();
  REQUIRE(std::strcmp(home_title, "RECORD") != 0);
  auto& home = static_cast<MainScreen*>(s.ctx().screen())->home_map();
  s.shift_tap(Button::Rec);
  CHECK(s.rec().state() == State::Waiting);
  CHECK(s.title() == home_title);
  CHECK_FALSE(home.rec());  // not the step record
  REQUIRE(s.until([&] { return s.rec().state() == State::Recording; }, 3000));
  s.wait(400);
  CHECK(s.title() == home_title);
  s.shift_tap(Button::Rec);  // again: stops it and saves it (it has frames)
  CHECK(s.title() == home_title);
  REQUIRE(s.saved());
  REQUIRE(s.takes().size() == 1);
  const Wav w = read_wav(s.takes()[0]);
  REQUIRE(w.ok);
  CHECK(w.pcm.size() > 2u * 10'000u);  // a few hundred milliseconds of it
  CHECK(w.pcm.size() < 2u * 4u * bar_frames(125'000));  // stopped before the 4 bars
}

TEST_CASE("a take armed with Shift + Rec saves itself when it ends by itself") {
  Session s;
  s.open_record();
  s.choose_bars(1);
  s.tap(Button::Pattern);
  s.tap(Button::Play);
  s.wait(100);
  s.shift_tap(Button::Rec);
  REQUIRE(s.until([&] { return s.rec().state() == State::Recording; }, 3000));
  REQUIRE(s.saved(6000));
  REQUIRE(s.takes().size() == 1);
  CHECK(read_wav(s.takes()[0]).pcm.size() / 2u >= bar_frames(125'000));
  CHECK(read_wav(s.takes()[0]).pcm.size() / 2u <= bar_frames(125'000) + 1u);
  // Shift + Rec with the take waiting for its bar disarms it; nothing is saved.
  s.shift_tap(Button::Rec);
  CHECK(s.rec().state() == State::Waiting);
  s.shift_tap(Button::Rec);
  s.wait(20);
  CHECK(s.rec().state() == State::Idle);
  s.wait(300);
  CHECK(s.takes().size() == 1u);
}

TEST_CASE("a take armed on RECORD waits for F6; F1 over it says so instead of overwriting, and Shift + Rec over it saves it") {
  Session s;
  s.open_record();
  s.choose_bars(0);
  s.tap(Button::F1);
  s.wait(300);
  s.tap(Button::F1);
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 200));
  s.wait(500);
  CHECK(s.rec().state() == State::Done);  // not saved by itself
  CHECK(s.takes().empty());
  s.tap(Button::F1);
  CHECK(s.rec().state() == State::Done);  // the take is kept, and the status line says why
  CHECK(std::string(s.record().message()) == "SAVE OR DISCARD THE TAKE (F6 F7)");
  s.tap(Button::Pattern);
  s.shift_tap(Button::Rec);  // from another screen: SAVE
  REQUIRE(s.saved());
  CHECK(s.takes().size() == 1u);
}

TEST_CASE("the update is blocked while a take waits or records, and while one is finished and not on the card") {
  Session s;
  app::App& a = o3sdk_app();
  CHECK(a.update_blocker(s.ctx()) == nullptr);
  s.open_record();
  s.choose_bars(0);
  s.tap(Button::Play);
  s.tap(Button::F1);
  REQUIRE(s.rec().state() == State::Waiting);
  REQUIRE(a.update_blocker(s.ctx()) != nullptr);
  CHECK(std::strcmp(a.update_blocker(s.ctx()), "RECORDING") == 0);
  REQUIRE(s.until([&] { return s.rec().state() == State::Recording; }, 3000));
  CHECK(std::strcmp(a.update_blocker(s.ctx()), "RECORDING") == 0);
  s.wait(3000);  // a few seconds of it, so the write takes more than a pass
  s.tap(Button::F1);
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 200));
  REQUIRE(a.update_blocker(s.ctx()) != nullptr);  // an update restarts the unit: the take would be lost
  CHECK(std::strcmp(a.update_blocker(s.ctx()), "SAVE OR DISCARD TAKE") == 0);
  s.press(Button::F6);
  s.release(Button::F6);
  CHECK(s.ctx().files().busy());  // the write: the kit's own check says SAVING, which is true
  REQUIRE(s.saved());
  CHECK(a.update_blocker(s.ctx()) == nullptr);
  CHECK(s.takes().size() == 1u);
}

TEST_CASE("the kit's FIRMWARE list refuses with the take's words, at the list and at INSTALL") {
  Session s;
  s.fw.add("O3SDK_303.PTF", 668920);
  const auto state = [&] {
    char buf[400];
    const std::size_t n = s.h->runtime().state_line(buf, sizeof buf);
    return std::string(buf, n);
  };
  const auto has = [&](const char* t) { return state().find(t) != std::string::npos; };
  s.open_record();
  s.choose_bars(0);
  RecordScreen& rs = s.record();

  // A take that records: the list does not open.
  s.tap(Button::F1);
  REQUIRE(s.rec().state() == State::Recording);
  s.tap(Button::Config);
  REQUIRE(s.h->runtime().system_open());
  CHECK(has("section=firmware fw=closed"));
  CHECK(s.fw.count("list_begin") == 0u);
  s.tap(Button::Config);
  CHECK_FALSE(s.h->runtime().system_open());
  s.tap(Button::F1);  // stop: a finished take, not saved
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 200));
  s.tap(Button::Config);
  CHECK(has("section=firmware fw=closed"));  // SAVE OR DISCARD TAKE
  CHECK(s.fw.count("list_begin") == 0u);
  s.tap(Button::Config);

  // Discarded: the list opens, and INSTALL is refused when a take starts while the question waits.
  s.tap(Button::F7);
  s.tap(Button::F7);
  REQUIRE(s.rec().state() == State::Idle);
  s.tap(Button::Config);
  CHECK(has("section=firmware fw=list"));
  s.tap(Button::F1);  // question 1
  s.tap(Button::F1);  // the check
  s.wait(20);
  REQUIRE(has("fw=ask_install"));
  rs.shift_rec(s.ctx());  // (a Shift + Rec from outside: the dialog is modal)
  REQUIRE(s.rec().state() != State::Idle);
  s.tap(Button::F1);
  CHECK(has("fw=list"));
  CHECK(s.h->runtime().system().firmware().msg == "RECORDING");
  CHECK(s.fw.installs == 0u);
}

TEST_CASE("a transport that stops under a BARS take ends it where it stopped") {
  Session s;
  s.open_record();
  s.choose_bars(4);
  s.tap(Button::Play);
  s.tap(Button::F1);
  REQUIRE(s.until([&] { return s.rec().state() == State::Recording; }, 3000));
  s.wait(800);
  s.tap(Button::Play);  // stop
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 500));
  CHECK(s.rec().frames() > 20'000u);
  CHECK(s.rec().frames() < 4u * bar_frames(125'000));
}

TEST_CASE("the input sources on the fake codec: F2 goes MASTER, LINE, MIC, RADIO and back; the take is what the codec heard") {
  Session s(true, true);
  s.capture.input_tone(440.0f, -12.0f);
  s.open_record();
  CHECK(s.record().master_source());
  s.tap(Button::F2);
  CHECK_FALSE(s.record().master_source());
  CHECK(s.ctx().audio_in().source() == app::AudioIn::Source::LineMix);
  CHECK(s.ctx().audio_in().selected());
  CHECK(s.ctx().audio_in().gain() == 50u);
  s.tap(Button::F4);  // the encoder is GAIN
  s.turn(10);
  CHECK(s.ctx().audio_in().gain() == 60u);
  s.press(Button::Shift);
  s.turn(-1);  // coarse: 5
  s.release(Button::Shift);
  CHECK(s.ctx().audio_in().gain() == 55u);
  s.choose_bars(0);
  s.wait(400);  // the codec's input switch settles
  s.tap(Button::F1);  // transport stopped: a FREE take starts at once
  CHECK(s.rec().state() == State::Recording);
  CHECK(s.rec().tap() == hal::record::Tap::Input);
  s.wait(600);
  s.tap(Button::F1);
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 200));
  const std::uint32_t frames = s.rec().frames();
  REQUIRE(frames > 20'000u);
  int peak = 0;
  for (std::uint32_t f = frames / 2; f < frames; ++f) {
    const int v = s.record().take_pcm()[f * 2];
    peak = peak > (v < 0 ? -v : v) ? peak : (v < 0 ? -v : v);
  }
  CHECK(peak > 3000);  // the tone, -12 dBFS and the gain
  s.tap(Button::F7);
  s.tap(Button::F7);
  CHECK(s.rec().state() == State::Idle);
  // MONITOR: a tap switches it on at 70 %, the encoder moves it, a tap switches it off.
  s.tap(Button::F5);
  CHECK(s.ctx().audio_in().monitor() == app::AudioIn::kMonitorOn);
  s.turn(-10);
  CHECK(s.ctx().audio_in().monitor() == 60u);
  s.tap(Button::F5);
  CHECK(s.ctx().audio_in().monitor() == 0u);
  s.tap(Button::F2);
  CHECK(s.ctx().audio_in().source() == app::AudioIn::Source::MicLow);
  s.tap(Button::F2);
  CHECK(s.ctx().audio_in().source() == app::AudioIn::Source::Radio);
  s.tap(Button::F2);  // no USB port on this rig
  CHECK(s.record().master_source());
}

TEST_CASE("without a codec input F2 stays on MASTER and an input take is never armed") {
  Session s;
  s.open_record();
  s.tap(Button::F2);
  CHECK(s.record().master_source());
  CHECK_FALSE(s.ctx().audio_in().available());
}

TEST_CASE("a take that fills the buffer ends by itself with everything before it kept") {
  Session s;
  s.open_record();
  s.choose_bars(0);
  s.tap(Button::F1);
  REQUIRE(s.rec().state() == State::Recording);
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 31'500));
  CHECK(s.rec().frames() >= kTakeFrames - 64u);
  CHECK(s.rec().frames() <= kTakeFrames);
  CHECK(s.rec().overrun() > 0u);
  CHECK(s.record().envelope().peak(Envelope::kCols - 1) == 0u);  // the master is silent: the envelope is flat but the scan got to the end without leaving the buffer
  s.tap(Button::F7);
  s.tap(Button::F7);
  CHECK(s.rec().state() == State::Idle);
}

TEST_CASE("no card: the take stays and SAVE says so") {
  Session s(false);
  s.open_record();
  s.choose_bars(0);
  s.tap(Button::F1);
  s.wait(300);
  s.tap(Button::F1);
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 200));
  s.tap(Button::F6);
  s.wait(200);
  CHECK_FALSE(s.record().saving());
  CHECK(s.rec().state() == State::Done);
  CHECK(std::string(s.record().last_view().dest) == "NO CARD: CANNOT SAVE");
  CHECK(s.record().last_view().dest_off);
}

// ---- The take speaks on every screen -------------------------------------------------------------------------------------------

namespace {

// The status line's band of the screen (the header is 32 px, the line under it 20): a hash of its pixels.
std::uint64_t status_band(Session& s) {
  int w = 0;
  int hgt = 0;
  const std::vector<std::uint8_t> img = s.h->panel_image(w, hgt);
  std::uint64_t hash = 1469598103934665603ull;
  for (int y = 32; y < 52; ++y) {
    for (int x = 0; x < w * 4; ++x) {
      hash = (hash ^ img[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4u + static_cast<std::size_t>(x)]) * 1099511628211ull;
    }
  }
  return hash;
}

}  // namespace

TEST_CASE("Shift + Rec that is refused says so on the status line of every screen, not on RECORD's alone") {
  Session s;
  REQUIRE(s.h->engine().set_tempo(20'000));  // 4 bars are 48 s: no fit
  s.wait(50);
  s.open_record();
  RecordScreen& rs = s.record();
  struct Where {
    const char* name;
    Button button;
    unsigned taps;
  };
  const Where where[] = {{"HOME", Button::Pattern, 1}, {"INSTRUMENT", Button::Params, 1}, {"PERFORM", Button::Performance, 1}, {"FILE", Button::File, 1},
                         {"LIBRARY", Button::File, 2}, {"HOOKS", Button::SampleLoad, 1}, {"RECORD", Button::SampleRec, 1}};
  for (const Where& w : where) {
    CAPTURE(w.name);
    s.tap(Button::Pattern);  // (a known start, so that the taps below open the screen on its first page)
    for (unsigned i = 0; i < w.taps; ++i) {
      s.tap(w.button);
    }
    s.wait(3000);  // every toast has gone
    const std::uint64_t before = status_band(s);
    s.shift_tap(Button::Rec);
    s.wait(60);
    CHECK(s.rec().state() == State::Idle);
    CHECK(std::string(rs.message()) == "4 BARS TOO LONG AT 20.0");
    CHECK(status_band(s) != before);  // drawn: the line is not what it was
    s.wait(3000);
    CHECK(std::string(rs.message()).empty());
  }
  // The other words: DISARMED and STOPPED, from a screen that is not RECORD.
  REQUIRE(s.h->engine().set_tempo(125'000));
  s.tap(Button::Performance);
  s.wait(3000);
  s.shift_tap(Button::Rec);
  CHECK(s.rec().state() == State::Waiting);  // (stopped: it waits for Play)
  CHECK(std::string(rs.chip_text()) == "TAKE ARMED");
  s.wait(3000);
  s.shift_tap(Button::Rec);
  s.wait(20);
  CHECK(std::string(rs.message()) == "DISARMED");
  CHECK(s.rec().state() == State::Idle);
  CHECK(std::string(rs.chip_text()).empty());
}

TEST_CASE("a take that cannot be saved keeps its words on the status line and the take; Shift + Rec tries again") {
  Session s;
  s.finished_take();
  RecordScreen& rs = s.record();
  s.h->card_protect(true);
  s.wait(300);
  s.tap(Button::F6);
  s.wait(300);
  CHECK(s.rec().state() == State::Done);
  CHECK_FALSE(rs.saving());
  CHECK_FALSE(std::string(rs.message()).empty());  // the kit's words: the card is write protected
  CHECK(s.takes().empty());
  s.h->card_protect(false);
  s.wait(500);
  s.tap(Button::Performance);
  s.shift_tap(Button::Rec);  // from another screen
  REQUIRE(s.saved());
  CHECK(s.takes().size() == 1u);
  CHECK(std::string(rs.chip_text()).empty());
}

TEST_CASE("a card pulled while the WAV is written fails the save with the kit's words and keeps the take") {
  Session s;
  s.finished_take(2000);  // a big file: the job spans passes
  RecordScreen& rs = s.record();
  s.press(Button::F6);
  s.release(Button::F6);
  REQUIRE(rs.saving());
  s.h->card_out();
  s.wait(300);
  CHECK_FALSE(rs.saving());
  CHECK(s.rec().state() == State::Done);
  CHECK_FALSE(std::string(rs.message()).empty());
  s.h->card_in();
  s.wait(500);
  s.shift_tap(Button::Rec);
  REQUIRE(s.saved());
  CHECK(s.takes().size() == 1u);
  CHECK(read_wav(s.takes()[0]).pcm.size() / 2u > 80'000u);
}

namespace {

// HELLO's idea of a card over the in-memory one, to fill it and to make it fail.
class MemHostCard final : public host::HostCard {
 public:
  explicit MemHostCard(o3sdk::test::MemCard& m) : m_(m) {}
  void set_present(bool present) override { m_.gate_status = present ? hal::storage::WriteStatus::Ok : hal::storage::WriteStatus::NoCard; }
  unsigned violations() const override { return m_.violations; }
  hal::storage::Status mount() noexcept override { return m_.mount(); }
  bool mounted() const noexcept override { return m_.gate_status == hal::storage::WriteStatus::Ok; }
  bool card_present() const noexcept override { return mounted(); }
  hal::storage::VolumeInfo volume() const noexcept override { return m_.volume(); }
  hal::storage::Status exists(const char* p, bool* d) noexcept override { return m_.exists(p, d); }
  hal::storage::Status stat(const char* p, hal::storage::FileStamp& o) noexcept override { return m_.stat(p, o); }
  hal::storage::Status open(const char* p, hal::storage::FileHandle& o) noexcept override { return m_.open(p, o); }
  hal::storage::Status read(hal::storage::FileHandle f, void* d, std::size_t n, std::size_t& g) noexcept override { return m_.read(f, d, n, g); }
  hal::storage::Status seek(hal::storage::FileHandle f, std::uint32_t o) noexcept override { return m_.seek(f, o); }
  hal::storage::Status size(hal::storage::FileHandle f, std::uint32_t& o) const noexcept override { return m_.size(f, o); }
  void close(hal::storage::FileHandle& f) noexcept override { m_.close(f); }
  hal::storage::Status opendir(const char* p, hal::storage::DirHandle& o) noexcept override { return m_.opendir(p, o); }
  hal::storage::Status readdir(hal::storage::DirHandle d, hal::storage::DirEntry& o) noexcept override { return m_.readdir(d, o); }
  void closedir(hal::storage::DirHandle& d) noexcept override { m_.closedir(d); }
  hal::storage::WriteStatus gate() noexcept override { return m_.gate(); }
  hal::storage::WriteStatus make_dir(const char* p) noexcept override { return m_.make_dir(p); }
  hal::storage::WriteStatus create(const char* p) noexcept override { return m_.create(p); }
  hal::storage::WriteStatus write(const void* s, std::size_t n) noexcept override { return m_.write(s, n); }
  hal::storage::WriteStatus finish() noexcept override { return m_.finish(); }
  void abandon() noexcept override { m_.abandon(); }
  bool writing() const noexcept override { return m_.writing(); }
  hal::storage::WriteStatus rename(const char* a, const char* b) noexcept override { return m_.rename(a, b); }
  hal::storage::WriteStatus create_marker(const char* p) noexcept override { return m_.create_marker(p); }
  hal::storage::WriteStatus remove(const char* p) noexcept override { return m_.remove(p); }
  void begin_save() noexcept override { m_.begin_save(); }
  void end_save() noexcept override { m_.end_save(); }
  std::uint32_t card_id() const noexcept override { return m_.card_id(); }

 private:
  o3sdk::test::MemCard& m_;
};

}  // namespace

namespace {

// The app over the in-memory card (Apps/o3_303 made), with RECORD up and a FREE take of the master finished: `ms` milliseconds of it.
struct MemRig {
  o3sdk::test::MemCard mem;
  MemHostCard card{mem};
  sim::Harness h{o3sdk_app(), nullptr, nullptr, &card, "o3_303"};
  app::Context& ctx = h.runtime().context();
  RecordScreen* rs = nullptr;

  explicit MemRig(unsigned take_ms) {
    mem.make_dir_now("Apps");
    mem.make_dir_now("Apps/o3_303");
    REQUIRE(h.start());
    h.run_ms(100);
    tap(Button::SampleRec);
    rs = static_cast<RecordScreen*>(ctx.screen());
    for (unsigned i = 0; i < kBarChoices && rs->bars() != 0u; ++i) {
      tap(Button::F3);
    }
    tap(Button::F1);
    h.run_ms(take_ms);
    tap(Button::F1);
    h.run_ms(20);
    REQUIRE(ctx.recorder().state() == State::Done);
  }
  void tap(Button b) {
    h.press(b);
    h.run_ms(40);
    h.release(b);
    h.run_ms(2);
  }
  bool idle_again(unsigned limit) {
    for (unsigned i = 0; i < limit; ++i) {
      if (ctx.recorder().state() == State::Idle && !ctx.files().busy()) {
        return true;
      }
      h.run_ms(1);
    }
    return false;
  }
  bool project_idle(unsigned limit) {
    for (unsigned i = 0; i < limit; ++i) {
      if (app_project().idle() && !ctx.files().busy()) {
        return true;
      }
      h.run_ms(1);
    }
    return false;
  }
};

}  // namespace

TEST_CASE("a full card, and a write that fails: the take stays, the status line says why, and the next try works") {
  MemRig r(400);
  const RecordScreen& rs = *r.rs;
  r.mem.space_left = 4096;  // a card with no room for the take
  r.tap(Button::F6);
  r.h.run_ms(400);
  CHECK(r.ctx.recorder().state() == State::Done);
  CHECK_FALSE(rs.saving());
  CHECK(std::string(rs.message()) == "CARD IS FULL");
  CHECK_FALSE(r.mem.has("Samples/take_001.wav"));
  CHECK(r.mem.violations == 0u);

  r.mem.space_left = ~std::size_t{0};
  r.mem.ops = 0;
  r.mem.fail_at = 4;  // a write that fails once, a few calls into the job
  r.mem.fail_with = hal::storage::WriteStatus::Io;
  r.tap(Button::F6);
  r.h.run_ms(400);
  CHECK(r.ctx.recorder().state() == State::Done);
  CHECK_FALSE(std::string(rs.message()).empty());
  CHECK_FALSE(r.mem.has("Samples/take_001.wav"));  // never half a file under the name

  r.mem.fail_at = ~std::size_t{0};
  r.tap(Button::F6);
  REQUIRE(r.idle_again(4000));
  CHECK(r.mem.has("Samples/take_001.wav"));
  CHECK(r.mem.violations == 0u);
}

TEST_CASE("a take save that fails while the project is being saved is the take's failure alone: the project's three files are written and it says SAVED") {
  for (unsigned gap : {0u, 1u, 2u, 3u, 4u, 6u, 9u, 14u}) {
    CAPTURE(gap);
    MemRig r(300);
    // A card the take cannot go to and the project can: a file where Samples/ should be (the WAV job fails at its first look, taking no room the project needs).
    r.mem.files["samples"] = o3sdk::test::MemCard::Bytes{1, 2, 3};
    r.h.press(Button::Shift);
    r.h.run_ms(2);
    r.tap(Button::File);  // SAVE: no project yet, so it goes under the first free UNTITLED
    r.h.release(Button::Shift);
    r.h.run_ms(gap);
    r.tap(Button::F6);
    REQUIRE(r.project_idle(6000));
    r.h.run_ms(300);
    CHECK(r.ctx.recorder().state() == State::Done);  // the take: not saved, and still there
    CHECK_FALSE(std::string(r.rs->message()).empty());
    CHECK_FALSE(r.mem.has("Samples/take_001.wav"));
    CHECK(app_project().info().failed == false);  // the project: not a failure of the project's
    CHECK(r.mem.has("Apps/o3_303/UNTITLED/live.o3a"));
    CHECK(r.mem.has("Apps/o3_303/UNTITLED/project.o3a"));
    CHECK(r.mem.has("Apps/o3_303/last.txt"));
    CHECK(app_project().saves() >= 1u);
    CHECK(r.mem.violations == 0u);
  }
}

TEST_CASE("a SAVE asked while the WAV is written, on a card that fails the WAV and takes the project: the take says it failed and stays, the project says SAVED") {
  unsigned failed_wavs = 0;
  for (std::size_t fail_at : {20u, 40u, 80u, 120u}) {
    CAPTURE(fail_at);
    MemRig r(4000);  // 4 s of the master: a WAV of 700 KB, written over many passes
    r.mem.ops = 0;
    r.mem.fail_at = fail_at;  // the take's job fails at its fail_at-th call, once; later calls (the project's) work
    r.mem.fail_with = hal::storage::WriteStatus::Io;
    r.h.press(Button::F6);
    r.h.release(Button::F6);
    r.h.press(Button::Shift);  // SAVE the project at once: asked while the WAV is written, its steps wait on the card
    r.h.press(Button::File);
    r.h.release(Button::File);
    r.h.release(Button::Shift);
    r.h.run_ms(1);
    REQUIRE(r.rs->saving());
    REQUIRE(app_project().saving_explicit());
    REQUIRE(r.project_idle(8000));
    r.h.run_ms(300);
    const bool wav_on_card = r.mem.has("Samples/take_001.wav");
    if (wav_on_card) {  // the failure fell after the job's last call: the take is on the card and released
      CHECK(r.ctx.recorder().state() == State::Idle);
    } else {
      ++failed_wavs;
      CHECK(r.ctx.recorder().state() == State::Done);  // still there, READY
      CHECK(std::string(r.rs->chip_text()) == "TAKE READY");
      CHECK_FALSE(r.rs->saving());
      CHECK(std::string(r.rs->message()) != "SAVED");
    }
    // The project: all three files, and its own word.
    CHECK(app_project().info().failed == false);
    CHECK(r.mem.has("Apps/o3_303/UNTITLED/live.o3a"));
    CHECK(r.mem.has("Apps/o3_303/UNTITLED/project.o3a"));
    CHECK(r.mem.has("Apps/o3_303/last.txt"));
    CHECK(app_project().saves() >= 1u);
    CHECK(std::string(app_project().info().notice) == "SAVED");  // the project's own word
    CHECK(r.mem.violations == 0u);
    if (!wav_on_card) {  // and the take can still be saved
      r.mem.fail_at = ~std::size_t{0};
      r.h.press(Button::Shift);
      r.h.run_ms(2);
      r.h.press(Button::Rec);
      r.h.release(Button::Rec);
      r.h.release(Button::Shift);
      REQUIRE(r.idle_again(6000));
      CHECK(r.mem.has("Samples/take_001.wav"));
    }
  }
  CHECK(failed_wavs >= 3u);  // the sweep did put the failure inside the job
}

// ---- The take and the project share the card -------------------------------------------------------------------------------------

TEST_CASE("a take saved while the project is being saved: both finish with their own result, whichever asks first") {
  for (unsigned gap : {0u, 1u, 2u, 3u, 5u, 8u, 13u, 21u, 34u}) {
    CAPTURE(gap);
    Session s;
    s.finished_take(300);
    const std::size_t frames = s.rec().frames();
    // SAVE (live.o3a, project.o3a, last.txt: three jobs one after the other) and the take's SAVE `gap` ms apart.
    s.shift_tap(Button::File);
    s.wait(gap);
    s.tap(Button::F6);
    REQUIRE(s.project_settled(6000));
    REQUIRE(s.saved(6000));
    CHECK(app_project().info().failed == false);
    CHECK(app_project().saves() >= 1u);
    CHECK(fs::exists(fs::path(s.dir.path()) / "Apps" / "o3_303" / "UNTITLED" / "live.o3a"));
    CHECK(fs::exists(fs::path(s.dir.path()) / "Apps" / "o3_303" / "UNTITLED" / "project.o3a"));
    CHECK(fs::exists(fs::path(s.dir.path()) / "Apps" / "o3_303" / "last.txt"));
    REQUIRE(s.takes().size() == 1u);
    const Wav w = read_wav(s.takes()[0]);
    REQUIRE(w.ok);
    CHECK(w.pcm.size() == frames * 2u);  // the take, whole
    CHECK(s.rec().state() == State::Idle);
  }
}

// ---- The project under a take ------------------------------------------------------------------------------------------------

namespace {

// Two projects on the card: LOW at 55 BPM and HIGH at 125 BPM (the session ends as HIGH), and the list read.
void two_projects(Session& s) {
  REQUIRE(s.h->engine().set_tempo(55'000));
  s.wait(50);
  REQUIRE(app_project().save_as("LOW"));
  REQUIRE(s.project_settled());
  REQUIRE(s.h->engine().set_tempo(125'000));
  s.wait(50);
  REQUIRE(app_project().save_as("HIGH"));
  REQUIRE(s.project_settled());
  app_project().want_list(true);
  app_project().refresh_list();
  s.wait(300);
  REQUIRE(s.project_settled());
  REQUIRE(app_project().list_valid());
}

}  // namespace

TEST_CASE("a LOAD that slows the tempo under a take that waits for its bar disarms it, with the words on the status line") {
  Session s;
  two_projects(s);
  s.open_record();
  s.choose_bars(8);  // 15 s at 125 BPM
  s.tap(Button::F1);
  REQUIRE(s.rec().state() == State::Waiting);
  const std::uint32_t span_before = s.record().envelope().span();
  REQUIRE(app_project().load("LOW"));  // 55 BPM: 8 bars are 35 s
  REQUIRE(s.until([&] { return s.ctx().engine().tempo() == 55'000u; }, 3000));
  REQUIRE(s.until([&] { return s.rec().state() == State::Idle; }, 500));
  CHECK(s.record().message() == std::string("DISARMED: 8 BARS TOO LONG AT 55.0"));
  CHECK(s.takes().empty());
  (void)span_before;
}

TEST_CASE("a tempo change under a take that waits: the bar, the envelope's span and the bar counter follow it") {
  Session s;
  s.open_record();
  s.choose_bars(4);
  s.tap(Button::F1);
  REQUIRE(s.rec().state() == State::Waiting);
  CHECK(s.record().envelope().span() == 4u * bar_frames(125'000));
  REQUIRE(s.h->engine().set_tempo(100'000));
  s.wait(30);
  CHECK(s.rec().state() == State::Waiting);  // 4 bars at 100 BPM are 9.6 s: still fits
  CHECK(s.record().envelope().span() == 4u * bar_frames(100'000));
  REQUIRE(s.h->engine().set_tempo(20'000));  // 48 s: no
  s.wait(30);
  s.wait(30);
  CHECK(s.rec().state() == State::Idle);
  CHECK(s.record().message() == std::string("DISARMED: 4 BARS TOO LONG AT 20.0"));
}

TEST_CASE("NEW, REVERT and a LOAD under a FREE take that records: it goes on, and what it holds is saved") {
  Session s;
  two_projects(s);  // ends as HIGH, 125 BPM
  s.open_record();
  s.choose_bars(0);
  s.tap(Button::F1);  // the transport is stopped: a FREE take starts at once
  REQUIRE(s.rec().state() == State::Recording);
  s.wait(300);
  REQUIRE(app_project().new_project("FRESH"));
  REQUIRE(s.project_settled(6000));
  CHECK(s.rec().state() == State::Recording);
  REQUIRE(app_project().revert());
  REQUIRE(s.project_settled(6000));
  CHECK(s.rec().state() == State::Recording);
  REQUIRE(app_project().load("HIGH"));
  REQUIRE(s.project_settled(6000));
  CHECK(s.rec().state() == State::Recording);
  const std::uint32_t at_load = s.rec().frames();
  s.wait(300);
  CHECK(s.rec().frames() > at_load);
  s.tap(Button::F1);
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 200));
  const std::uint32_t frames = s.rec().frames();
  CHECK(frames > 30'000u);
  s.tap(Button::F6);
  REQUIRE(s.saved(6000));
  REQUIRE(s.takes().size() == 1u);
  CHECK(read_wav(s.takes()[0]).pcm.size() / 2u == frames);
}

TEST_CASE("a NEW under a BARS take that records stops the transport, and the take ends there with what it holds, whole and savable") {
  Session s;
  two_projects(s);
  s.open_record();
  s.choose_bars(4);
  s.tap(Button::Play);
  s.tap(Button::F1);
  REQUIRE(s.until([&] { return s.rec().state() == State::Recording; }, 3000));
  s.wait(600);
  REQUIRE(app_project().new_project("FRESH"));
  REQUIRE(s.project_settled(6000));
  REQUIRE(s.until([&] { return s.rec().state() == State::Done; }, 1000));
  const std::uint32_t frames = s.rec().frames();
  CHECK(frames > 20'000u);
  CHECK(frames < 4u * bar_frames(125'000));
  s.tap(Button::F6);
  REQUIRE(s.saved(6000));
  REQUIRE(s.takes().size() == 1u);
  CHECK(read_wav(s.takes()[0]).pcm.size() / 2u == frames);
}

TEST_CASE("the kit's saved input gain is kept by the first F2 tap") {
  Session s(true, true);
  REQUIRE(s.ctx().audio_in().select(app::AudioIn::Source::MicLow, 73));
  s.wait(300);
  s.open_record();
  CHECK(s.record().master_source());
  s.tap(Button::F2);  // MASTER to the first input
  CHECK_FALSE(s.record().master_source());
  CHECK(s.ctx().audio_in().gain() == 73u);
}
