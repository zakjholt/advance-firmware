/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "doctest/doctest.h"

#define private public
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleSourceInstrument.h"
#undef private
#include "Application/Audio/AudioSample.h"
#include "Application/Player/SyncMaster.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace {

class TestSoundSource : public SoundSource {
public:
  TestSoundSource(const int16_t *samples, int frameCount, int sampleRate,
                  int channelCount)
      : samples_(samples), frameCount_(frameCount), sampleRate_(sampleRate),
        channelCount_(channelCount) {}

  int GetLoopStart(int note) override {
    (void)note;
    return -1;
  }

  int GetLoopEnd(int note) override {
    (void)note;
    return -1;
  }

  int GetSize(int note) override {
    (void)note;
    return frameCount_;
  }

  int GetSampleRate(int note) override {
    (void)note;
    return sampleRate_;
  }

  int GetChannelCount(int note) override {
    (void)note;
    return channelCount_;
  }

  void *GetSampleBuffer(int note) override {
    (void)note;
    return const_cast<int16_t *>(samples_);
  }

  bool IsMulti() override { return false; }

  int GetRootNote(int note) override {
    (void)note;
    return 60;
  }

  float GetLengthInSec() override { return 0.0f; }

private:
  const int16_t *samples_;
  int frameCount_;
  int sampleRate_;
  int channelCount_;
};

class NoteSensitiveSizeSource : public SoundSource {
public:
  NoteSensitiveSizeSource(int selectedFrameCount, int noteZeroFrameCount)
      : selectedFrameCount_(selectedFrameCount),
        noteZeroFrameCount_(noteZeroFrameCount) {}

  int GetLoopStart(int note) override {
    (void)note;
    return -1;
  }

  int GetLoopEnd(int note) override {
    (void)note;
    return -1;
  }

  int GetSize(int note) override {
    return note < 0 ? selectedFrameCount_ : noteZeroFrameCount_;
  }

  int GetSampleRate(int note) override {
    (void)note;
    return 44100;
  }

  int GetChannelCount(int note) override {
    (void)note;
    return 1;
  }

  void *GetSampleBuffer(int note) override {
    (void)note;
    return sample_.data();
  }

  bool IsMulti() override { return false; }

  int GetRootNote(int note) override {
    (void)note;
    return 60;
  }

  float GetLengthInSec() override { return 0.0f; }

private:
  std::array<int16_t, 1> sample_{};
  int selectedFrameCount_;
  int noteZeroFrameCount_;
};

class TestSamplePool : public SamplePool {
public:
  void Reset() override { count_ = 0; }

  bool CheckSampleFits(uint32_t sampleSize) override {
    (void)sampleSize;
    return true;
  }

  uint32_t GetAvailableSampleStorageSpace() override { return 0; }

  bool unloadSample(uint32_t index) override {
    (void)index;
    return false;
  }

protected:
  bool loadSample(const char *name) override {
    (void)name;
    return false;
  }
};

class SamplePoolInstallGuard {
public:
  explicit SamplePoolInstallGuard(SamplePool *pool) {
    SamplePool::Install(pool);
  }
  ~SamplePoolInstallGuard() { SamplePool::Install(nullptr); }
};

Variable *requireVariable(SampleSourceInstrument &instrument, FourCC id) {
  Variable *variable = instrument.FindVariable(id);
  REQUIRE(variable != nullptr);
  return variable;
}

Variable *requireLegacyVariable(SampleInstrument &instrument, FourCC id) {
  Variable *variable = instrument.FindVariable(id);
  REQUIRE(variable != nullptr);
  return variable;
}

void configureLegacyPingPong(SampleInstrument &instrument,
                             TestSoundSource &source, int32_t endFrame) {
  instrument.source_ = &source;
  requireLegacyVariable(instrument, FourCC::SampleInstrumentVolume)
      ->SetInt(108, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentInterpolation)
      ->SetInt(1, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentCrush)
      ->SetInt(16, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentCrushVolume)
      ->SetInt(255, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentDownsample)
      ->SetInt(0, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentRootNote)
      ->SetInt(48, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentFineTune)
      ->SetInt(133, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentPan)
      ->SetInt(159, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentFilterCutOff)
      ->SetInt(143, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentFilterResonance)
      ->SetInt(64, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentFilterType)
      ->SetInt(176, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentFilterMode)
      ->SetInt(0, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentStart)
      ->SetInt(0, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentLoopMode)
      ->SetInt(SILM_LOOP_PINGPONG, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentLoopStart)
      ->SetInt(0, false);
  requireLegacyVariable(instrument, FourCC::SampleInstrumentEnd)
      ->SetInt(endFrame, false);
}

void configureMonoOneShot(SampleSourceInstrument &instrument,
                          TestSoundSource &source, int endFrame) {
  instrument.source_ = &source;
  requireVariable(instrument, FourCC::SampleSourceInstrumentLevel)
      ->SetInt(0xFF, false);
  requireVariable(instrument, FourCC::SampleSourceInstrumentRootNote)
      ->SetInt(60, false);
  requireVariable(instrument, FourCC::SampleSourceInstrumentStart)
      ->SetInt(0, false);
  requireVariable(instrument, FourCC::SampleSourceInstrumentLoopMode)
      ->SetInt(SSLM_ONESHOT, false);
  requireVariable(instrument, FourCC::SampleSourceInstrumentLoopStart)
      ->SetInt(0, false);
  requireVariable(instrument, FourCC::SampleSourceInstrumentEnd)
      ->SetInt(endFrame, false);
}

void configureMonoPlayback(SampleSourceInstrument &instrument,
                           TestSoundSource &source,
                           SampleSourceInstrumentLoopMode loopMode,
                           int startFrame, int loopStartFrame, int endFrame) {
  configureMonoOneShot(instrument, source, endFrame);
  requireVariable(instrument, FourCC::SampleSourceInstrumentStart)
      ->SetInt(startFrame, false);
  requireVariable(instrument, FourCC::SampleSourceInstrumentLoopMode)
      ->SetInt(loopMode, false);
  requireVariable(instrument, FourCC::SampleSourceInstrumentLoopStart)
      ->SetInt(loopStartFrame, false);
}

void forcePlaybackSpeed(float speed) {
  sampleSourceRenderParams &rp = SampleSourceInstrument::renderParams_[0];
  rp.baseSpeed_ = speed;
  rp.speed_ = speed;
}

constexpr AudioSample pcmToAudioSample(int pcm) {
  return static_cast<AudioSample>(pcm) / kAudioPcmScale;
}

enum class TestPlaybackTopology {
  Clamp = 0,
  Wrap,
  Reflect,
};

int clampFrameIndex(int frame, int firstFrame, int lastFrame) {
  if (frame < firstFrame) {
    return firstFrame;
  }
  if (frame > lastFrame) {
    return lastFrame;
  }
  return frame;
}

int wrapFrameIndex(int frame, int firstFrame, int lastFrame) {
  const int frameCount = lastFrame - firstFrame + 1;
  if (frameCount <= 0) {
    return firstFrame;
  }

  int offset = (frame - firstFrame) % frameCount;
  if (offset < 0) {
    offset += frameCount;
  }
  return firstFrame + offset;
}

int reflectFrameIndex(int frame, int firstFrame, int lastFrame) {
  const int span = lastFrame - firstFrame;
  if (span <= 0) {
    return firstFrame;
  }

  const int period = span * 2;
  int offset = (frame - firstFrame) % period;
  if (offset < 0) {
    offset += period;
  }
  if (offset > span) {
    offset = period - offset;
  }
  return firstFrame + offset;
}

int resolveFrameIndex(int frame, int firstFrame, int lastFrame,
                      TestPlaybackTopology topology) {
  switch (topology) {
  case TestPlaybackTopology::Clamp:
    return clampFrameIndex(frame, firstFrame, lastFrame);
  case TestPlaybackTopology::Wrap:
    return wrapFrameIndex(frame, firstFrame, lastFrame);
  case TestPlaybackTopology::Reflect:
    return reflectFrameIndex(frame, firstFrame, lastFrame);
  }

  return clampFrameIndex(frame, firstFrame, lastFrame);
}

AudioSample hermiteInterpolate(AudioSample ym1, AudioSample y0, AudioSample y1,
                               AudioSample y2, float fraction) {
  const AudioSample c0 = y0;
  const AudioSample c1 = 0.5f * (y1 - ym1);
  const AudioSample c2 = ym1 - (2.5f * y0) + (2.0f * y1) - (0.5f * y2);
  const AudioSample c3 = 0.5f * (y2 - ym1) + (1.5f * (y0 - y1));
  return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
}

AudioSample expectedHermiteSample(const int16_t *samples, int baseFrame,
                                  float fraction, int firstFrame, int lastFrame,
                                  TestPlaybackTopology topology) {
  const int frameM1 =
      resolveFrameIndex(baseFrame - 1, firstFrame, lastFrame, topology);
  const int frame0 =
      resolveFrameIndex(baseFrame, firstFrame, lastFrame, topology);
  const int frame1 =
      resolveFrameIndex(baseFrame + 1, firstFrame, lastFrame, topology);
  const int frame2 =
      resolveFrameIndex(baseFrame + 2, firstFrame, lastFrame, topology);

  return hermiteInterpolate(pcmToAudioSample(samples[frameM1]),
                            pcmToAudioSample(samples[frame0]),
                            pcmToAudioSample(samples[frame1]),
                            pcmToAudioSample(samples[frame2]), fraction);
}

AudioSample expectedHermiteChannelSample(const int16_t *samples,
                                         int channelCount, int channelIndex,
                                         int baseFrame, float fraction,
                                         int firstFrame, int lastFrame,
                                         TestPlaybackTopology topology) {
  const int frameM1 =
      resolveFrameIndex(baseFrame - 1, firstFrame, lastFrame, topology);
  const int frame0 =
      resolveFrameIndex(baseFrame, firstFrame, lastFrame, topology);
  const int frame1 =
      resolveFrameIndex(baseFrame + 1, firstFrame, lastFrame, topology);
  const int frame2 =
      resolveFrameIndex(baseFrame + 2, firstFrame, lastFrame, topology);

  return hermiteInterpolate(
      pcmToAudioSample(samples[frameM1 * channelCount + channelIndex]),
      pcmToAudioSample(samples[frame0 * channelCount + channelIndex]),
      pcmToAudioSample(samples[frame1 * channelCount + channelIndex]),
      pcmToAudioSample(samples[frame2 * channelCount + channelIndex]),
      fraction);
}

void fillOscillatorComparisonSamples(std::array<int16_t, 4096> &exactSamples,
                                     std::array<int16_t, 5000> &longSamples) {
  for (size_t i = 0; i < exactSamples.size(); ++i) {
    const int value = (static_cast<int>(i) * 37) % 24000;
    exactSamples[i] = static_cast<int16_t>(value - 12000);
    longSamples[i] = exactSamples[i];
  }
  for (size_t i = exactSamples.size(); i < longSamples.size(); ++i) {
    longSamples[i] = static_cast<int16_t>(12000 - (static_cast<int>(i) % 512));
  }
}

} // namespace

TEST_CASE("Legacy Sample pingpong stays within the sample allocation [#502]") {
  static constexpr int32_t kFrameCount = 600;
  static constexpr int32_t kRenderFrameCount = 320;
  std::array<int16_t, kFrameCount + 8> samples{};
  std::fill(samples.begin() + kFrameCount, samples.end(), 30000);

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleInstrument instrument;
  TestSoundSource source(samples.data(), kFrameCount, 44100, 1);
  configureLegacyPingPong(instrument, source, kFrameCount);

  REQUIRE(instrument.Start(0, NOTE_C3, true));

  std::array<AudioSample, kRenderFrameCount * 2> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), kRenderFrameCount, false));
  for (AudioSample sample : buffer) {
    CHECK(sample == doctest::Approx(0.0f));
  }
}

TEST_CASE("SampleSourceInstrument slice notes start the selected slice range") {
  static constexpr std::array<int16_t, 16> kSamples = {
      0,    1000, 2000, 3000, 4000, 5000, 6000, 7000,
      8000, 9000, 9500, 9600, 9700, 9800, 9900, 10000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoOneShot(instrument, source, static_cast<int>(kSamples.size()));
  instrument.SetSlicePoint(1, 4);
  instrument.SetSlicePoint(2, 10);

  REQUIRE(instrument.Start(
      0, static_cast<unsigned char>(SampleSourceInstrument::SliceNoteBase + 1),
      true));

  sampleSourceRenderParams &rp = SampleSourceInstrument::renderParams_[0];
  CHECK(rp.sliceActive_);
  CHECK(rp.activeSliceIndex_ == 1);
  CHECK(rp.rendLoopStart_ == 4);
  CHECK(rp.rendLoopEnd_ == 10);
  CHECK(rp.rendFirst_ == 4);
  CHECK(rp.position_ == doctest::Approx(4.0f));
  CHECK(rp.loopModeValue_ == SSLM_ONESHOT);
}

TEST_CASE(
    "SampleSourceInstrument one-shot starts and ends at configured markers") {
  static constexpr std::array<int16_t, 8> kSamples = {1000, 2000, 3000, 4000,
                                                      5000, 6000, 7000, 8000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_ONESHOT, 2, 0, 5);

  REQUIRE(instrument.Start(0, 60, true));

  sampleSourceRenderParams &rp = SampleSourceInstrument::renderParams_[0];
  CHECK_FALSE(rp.sliceActive_);
  CHECK(rp.rendFirst_ == 2);
  CHECK(rp.rendLoopEnd_ == 5);
  CHECK(rp.position_ == doctest::Approx(2.0f));

  std::array<AudioSample, 4> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));
  CHECK(buffer[0] == doctest::Approx(pcmToAudioSample(3000)));
  CHECK(buffer[1] == doctest::Approx(pcmToAudioSample(4000)));
  CHECK(buffer[2] == doctest::Approx(pcmToAudioSample(5000)));
}

TEST_CASE("SampleSourceInstrument marker fields use selected sample size") {
  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  NoteSensitiveSizeSource source(1234, 0);

  instrument.source_ = &source;

  CHECK(instrument.GetSampleSize() == 1234);
  CHECK(instrument.GetSampleSize(0) == 0);
}

TEST_CASE("SampleSourceInstrument preview note uses configured root note") {
  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;

  requireVariable(instrument, FourCC::SampleSourceInstrumentRootNote)
      ->SetInt(67, false);

  CHECK(instrument.GetPreviewNote() == 67);
}

TEST_CASE(
    "SampleSourceInstrument reverse mode plays configured range backward") {
  static constexpr std::array<int16_t, 6> kSamples = {0,    1000, 2000,
                                                      3000, 4000, 5000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_REVERSE, 1, 0, 5);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 5> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  static constexpr std::array<int16_t, 4> kExpected = {4000, 3000, 2000, 1000};
  for (size_t i = 0; i < kExpected.size(); ++i) {
    CHECK(buffer[i] == doctest::Approx(pcmToAudioSample(kExpected[i])));
  }
  CHECK(buffer[4] < buffer[3]);
}

TEST_CASE("SampleSourceInstrument play offset command repositions reverse "
          "playback") {
  static constexpr std::array<int16_t, 8> kSamples = {0,    1000, 2000, 3000,
                                                      4000, 5000, 6000, 7000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_REVERSE, 0, 0,
                        static_cast<int>(kSamples.size()));

  REQUIRE(instrument.Start(0, 60, true));
  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandPlayOfset,
                            0x8000);
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 2> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  CHECK(buffer[0] == doctest::Approx(pcmToAudioSample(4000)));
  CHECK(buffer[1] == doctest::Approx(pcmToAudioSample(3000)));
}

TEST_CASE("SampleSourceInstrument adds a short tail when a oneshot ends away "
          "from zero") {
  static constexpr std::array<int16_t, 4> kSamples = {0, 0, 12000, 12000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoOneShot(instrument, source, static_cast<int>(kSamples.size()));

  REQUIRE(instrument.Start(0, 60, true));

  std::array<AudioSample, 64> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  CHECK(buffer[0] == doctest::Approx(pcmToAudioSample(0)));
  CHECK(buffer[1] == doctest::Approx(pcmToAudioSample(0)));
  CHECK(buffer[2] == doctest::Approx(pcmToAudioSample(12000)));
  CHECK(buffer[3] == doctest::Approx(pcmToAudioSample(12000)));
  CHECK(buffer[4] < buffer[3]);
  CHECK(buffer[4] > buffer[5]);
  CHECK(buffer[35] == doctest::Approx(0.0f));
  CHECK(buffer[36] == doctest::Approx(0.0f));

  std::array<AudioSample, 8> stopped{};
  CHECK_FALSE(instrument.Render(0, stopped.data(),
                                static_cast<int>(stopped.size()), false));
}

TEST_CASE("SampleSourceInstrument adds a short tail when the configured end "
          "cuts playback before a zero crossing") {
  static constexpr std::array<int16_t, 6> kSamples = {0,    2000, 4000,
                                                      6000, 8000, 0};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoOneShot(instrument, source, 4);

  REQUIRE(instrument.Start(0, 60, true));

  std::array<AudioSample, 64> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  CHECK(buffer[0] == doctest::Approx(pcmToAudioSample(0)));
  CHECK(buffer[1] == doctest::Approx(pcmToAudioSample(2000)));
  CHECK(buffer[2] == doctest::Approx(pcmToAudioSample(4000)));
  CHECK(buffer[3] == doctest::Approx(pcmToAudioSample(6000)));
  CHECK(buffer[4] < buffer[3]);
  CHECK(buffer[4] > buffer[5]);
  CHECK(buffer[35] == doctest::Approx(0.0f));
  CHECK(buffer[36] == doctest::Approx(0.0f));

  std::array<AudioSample, 8> stopped{};
  CHECK_FALSE(instrument.Render(0, stopped.data(),
                                static_cast<int>(stopped.size()), false));
}

TEST_CASE(
    "SampleSourceInstrument non-retrigger start keeps playback position") {
  static constexpr std::array<int16_t, 5> kSamples = {1000, 2000, 3000, 4000,
                                                      5000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoOneShot(instrument, source, static_cast<int>(kSamples.size()));

  REQUIRE(instrument.Start(0, 60, true));

  std::array<AudioSample, 1> first{};
  REQUIRE(instrument.Render(0, first.data(), static_cast<int>(first.size()),
                            false));
  CHECK(first[0] == doctest::Approx(pcmToAudioSample(1000)));

  REQUIRE(instrument.Start(0, 60, false));

  std::array<AudioSample, 1> second{};
  REQUIRE(instrument.Render(0, second.data(), static_cast<int>(second.size()),
                            false));
  CHECK(second[0] == doctest::Approx(pcmToAudioSample(2000)));
}

TEST_CASE("SampleSourceInstrument one-shot plays through configured end") {
  static constexpr std::array<int16_t, 5> kSamples = {0, 1000, 2000, 3000,
                                                      4000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoOneShot(instrument, source, 4);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 8> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  CHECK(buffer[0] == doctest::Approx(pcmToAudioSample(0)));
  CHECK(buffer[1] == doctest::Approx(pcmToAudioSample(1000)));
  CHECK(buffer[2] == doctest::Approx(pcmToAudioSample(2000)));
  CHECK(buffer[3] == doctest::Approx(pcmToAudioSample(3000)));
  CHECK(buffer[4] < buffer[3]);
}

TEST_CASE("SampleSourceInstrument loop mode repeats from loop start") {
  static constexpr std::array<int16_t, 5> kSamples = {0, 1000, 2000, 3000,
                                                      4000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_LOOP, 0, 1, 4);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 7> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  static constexpr std::array<int16_t, 7> kExpected = {0,    1000, 2000, 3000,
                                                       1000, 2000, 3000};
  for (size_t i = 0; i < kExpected.size(); ++i) {
    CHECK(buffer[i] == doctest::Approx(pcmToAudioSample(kExpected[i])));
  }
}

TEST_CASE("SampleSourceInstrument reverse loop mode repeats backward") {
  static constexpr std::array<int16_t, 5> kSamples = {0, 1000, 2000, 3000,
                                                      4000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_REVERSE_LOOP, 0, 1, 4);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 7> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  static constexpr std::array<int16_t, 7> kExpected = {3000, 2000, 1000, 3000,
                                                       2000, 1000, 3000};
  for (size_t i = 0; i < kExpected.size(); ++i) {
    CHECK(buffer[i] == doctest::Approx(pcmToAudioSample(kExpected[i])));
  }
}

TEST_CASE("SampleSourceInstrument loop mode crossfades into loop point") {
  std::array<int16_t, 64> samples{};
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] = static_cast<int16_t>(i * 100);
  }

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(samples.data(), static_cast<int>(samples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_LOOP, 0, 10, 50);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 51> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  CHECK(buffer[17] == doctest::Approx(pcmToAudioSample(1700)));
  CHECK(buffer[18] == doctest::Approx(pcmToAudioSample(1800)));
  CHECK(buffer[49] == doctest::Approx(pcmToAudioSample(1000)));
  CHECK(buffer[50] == doctest::Approx(pcmToAudioSample(1000)));
}

TEST_CASE(
    "SampleSourceInstrument reverse loop mode crossfades into loop point") {
  std::array<int16_t, 64> samples{};
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] = static_cast<int16_t>(i * 100);
  }

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(samples.data(), static_cast<int>(samples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_REVERSE_LOOP, 0, 10, 50);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 41> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  CHECK(buffer[7] == doctest::Approx(pcmToAudioSample(4200)));
  CHECK(buffer[8] == doctest::Approx(pcmToAudioSample(4100)));
  CHECK(buffer[39] == doctest::Approx(pcmToAudioSample(4900)));
  CHECK(buffer[40] == doctest::Approx(pcmToAudioSample(4900)));
}

TEST_CASE("SampleSourceInstrument loop offset command shifts loop range") {
  static constexpr std::array<int16_t, 6> kSamples = {0,    1000, 2000,
                                                      3000, 4000, 5000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_LOOP, 0, 1, 4);

  REQUIRE(instrument.Start(0, 60, true));
  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandLoopOfset, 1);

  sampleSourceRenderParams &rp = SampleSourceInstrument::renderParams_[0];
  CHECK(rp.rendLoopStart_ == 2);
  CHECK(rp.rendLoopEnd_ == 5);
}

TEST_CASE("SampleSourceInstrument reports wrapped loop playback position") {
  static constexpr std::array<int16_t, 5> kSamples = {0, 1000, 2000, 3000,
                                                      4000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_LOOP, 0, 1, 4);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);

  uint32_t position = 0;
  REQUIRE(instrument.GetPlaybackPosition(0, position));
  CHECK(position == 0);

  std::array<AudioSample, 8> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  REQUIRE(instrument.GetPlaybackPosition(0, position));
  CHECK(position == 2);
}

TEST_CASE("SampleSourceInstrument pingpong mode reflects at both loop ends") {
  static constexpr std::array<int16_t, 4> kSamples = {0, 1000, 2000, 3000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_LOOP_PINGPONG, 0, 0, 4);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 8> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  static constexpr std::array<int16_t, 8> kExpected = {0,    1000, 2000, 3000,
                                                       2000, 1000, 0,    1000};
  for (size_t i = 0; i < kExpected.size(); ++i) {
    CHECK(buffer[i] == doctest::Approx(pcmToAudioSample(kExpected[i])));
  }
}

TEST_CASE(
    "SampleSourceInstrument reports reflected pingpong playback position") {
  static constexpr std::array<int16_t, 4> kSamples = {0, 1000, 2000, 3000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_LOOP_PINGPONG, 0, 0, 4);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 5> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  uint32_t position = 0;
  REQUIRE(instrument.GetPlaybackPosition(0, position));
  CHECK(position == 1);
}

TEST_CASE("SampleSourceInstrument oscillator mode wraps inside loop range") {
  static constexpr std::array<int16_t, 5> kSamples = {0, 1000, 2000, 3000,
                                                      4000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_OSC, 0, 1, 4);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 6> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  static constexpr std::array<int16_t, 6> kExpected = {1000, 2000, 3000,
                                                       1000, 2000, 3000};
  for (size_t i = 0; i < kExpected.size(); ++i) {
    CHECK(buffer[i] == doctest::Approx(pcmToAudioSample(kExpected[i])));
  }
}

TEST_CASE(
    "SampleSourceInstrument oscillator mode wraps large phase steps directly") {
  static constexpr std::array<int16_t, 4> kSamples = {0, 1000, 2000, 3000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_OSC, 0, 0,
                        static_cast<int>(kSamples.size()));

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(13.0f);

  std::array<AudioSample, 5> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  static constexpr std::array<int16_t, 5> kExpected = {0, 1000, 2000, 3000, 0};
  for (size_t i = 0; i < kExpected.size(); ++i) {
    CHECK(buffer[i] == doctest::Approx(pcmToAudioSample(kExpected[i])));
  }
}

TEST_CASE("SampleSourceInstrument oscillator mode interpolates across wrap") {
  static constexpr std::array<int16_t, 4> kSamples = {0, 12000, -6000, 9000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_OSC, 0, 0,
                        static_cast<int>(kSamples.size()));

  REQUIRE(instrument.Start(0, 60, true));
  SampleSourceInstrument::renderParams_[0].position_ = 3.5f;
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 1> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  const AudioSample expected = expectedHermiteSample(
      kSamples.data(), 3, 0.5f, 0, 3, TestPlaybackTopology::Wrap);
  CHECK(buffer[0] == doctest::Approx(expected));
}

TEST_CASE("SampleSourceInstrument oscillator mode limits long loops") {
  static constexpr std::array<int16_t, 4100> kSamples = {};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_OSC, 0, 0,
                        static_cast<int>(kSamples.size()));

  REQUIRE(instrument.Start(0, 60, true));

  CHECK(SampleSourceInstrument::renderParams_[0].rendLoopStart_ == 0);
  CHECK(SampleSourceInstrument::renderParams_[0].rendLoopEnd_ == 4096);
}

TEST_CASE("SampleSourceInstrument resets oscillator mode for oversized "
          "selected samples") {
  static constexpr std::array<int16_t, 4097> kSamples = {};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_OSC, 0, 0,
                        static_cast<int>(kSamples.size()));

  CHECK(instrument.ResetOscillatorLoopModeIfSampleTooLarge());
  CHECK(requireVariable(instrument, FourCC::SampleSourceInstrumentLoopMode)
            ->GetInt() == SSLM_ONESHOT);
}

TEST_CASE("SampleSourceInstrument keeps oscillator mode for 4096-frame "
          "selected samples") {
  static constexpr std::array<int16_t, 4096> kSamples = {};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_OSC, 0, 0,
                        static_cast<int>(kSamples.size()));

  CHECK_FALSE(instrument.ResetOscillatorLoopModeIfSampleTooLarge());
  CHECK(requireVariable(instrument, FourCC::SampleSourceInstrumentLoopMode)
            ->GetInt() == SSLM_OSC);
}

TEST_CASE("SampleSourceInstrument playback mode display names are compact") {
  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  Variable *loopMode =
      requireVariable(instrument, FourCC::SampleSourceInstrumentLoopMode);

  loopMode->SetInt(SSLM_ONESHOT, false);
  CHECK(loopMode->GetString() == "fwd");

  loopMode->SetInt(SSLM_REVERSE, false);
  CHECK(loopMode->GetString() == "rev");

  loopMode->SetInt(SSLM_LOOP, false);
  CHECK(loopMode->GetString() == "loop");

  CHECK(SSLM_REVERSE_LOOP == SSLM_LOOP + 1);
  loopMode->SetInt(SSLM_REVERSE_LOOP, false);
  CHECK(loopMode->GetString() == "rev-loop");

  loopMode->SetInt(SSLM_OSC, false);
  CHECK(loopMode->GetString() == "osc");

  loopMode->SetInt(SSLM_LOOPSYNC, false);
  CHECK(loopMode->GetString() == "loopsync");

  CHECK_FALSE(SampleSourceInstrument::PlaybackModeUsesLoopPoint(SSLM_ONESHOT));
  CHECK_FALSE(SampleSourceInstrument::PlaybackModeUsesLoopPoint(SSLM_REVERSE));
  CHECK(SampleSourceInstrument::PlaybackModeUsesLoopPoint(SSLM_LOOP));
  CHECK(SampleSourceInstrument::PlaybackModeUsesLoopPoint(SSLM_REVERSE_LOOP));
  CHECK(SampleSourceInstrument::PlaybackModeUsesLoopPoint(SSLM_LOOP_PINGPONG));
  CHECK(SampleSourceInstrument::PlaybackModeUsesLoopPoint(SSLM_OSC));
  CHECK(SampleSourceInstrument::PlaybackModeUsesLoopPoint(SSLM_LOOPSYNC));
}

TEST_CASE("SampleSourceInstrument skips oscillator mode forward for oversized "
          "selected samples") {
  static constexpr std::array<int16_t, 4097> kSamples = {};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_OSC, 0, 0,
                        static_cast<int>(kSamples.size()));

  CHECK(instrument.SkipOscillatorLoopModeIfSampleTooLarge(1));
  CHECK(requireVariable(instrument, FourCC::SampleSourceInstrumentLoopMode)
            ->GetInt() == SSLM_LOOPSYNC);
}

TEST_CASE("SampleSourceInstrument skips oscillator mode backward for oversized "
          "selected samples") {
  static constexpr std::array<int16_t, 4097> kSamples = {};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_OSC, 0, 0,
                        static_cast<int>(kSamples.size()));

  CHECK(instrument.SkipOscillatorLoopModeIfSampleTooLarge(-1));
  CHECK(requireVariable(instrument, FourCC::SampleSourceInstrumentLoopMode)
            ->GetInt() == SSLM_LOOP_PINGPONG);
}

TEST_CASE("SampleSourceInstrument clamped oscillator matches 4096-frame "
          "source") {
  static std::array<int16_t, 4096> kExactSamples = {};
  static std::array<int16_t, 5000> kLongSamples = {};
  fillOscillatorComparisonSamples(kExactSamples, kLongSamples);

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);

  SampleSourceInstrument exactInstrument;
  TestSoundSource exactSource(kExactSamples.data(),
                              static_cast<int>(kExactSamples.size()), 44100, 1);
  configureMonoPlayback(exactInstrument, exactSource, SSLM_OSC, 0, 0,
                        static_cast<int>(kExactSamples.size()));

  REQUIRE(exactInstrument.Start(0, 60, true));
  const float exactBaseSpeed =
      SampleSourceInstrument::renderParams_[0].baseSpeed_;
  CHECK(SampleSourceInstrument::renderParams_[0].rendLoopStart_ == 0);
  CHECK(SampleSourceInstrument::renderParams_[0].rendLoopEnd_ == 4096);

  std::array<AudioSample, 180> exactBuffer{};
  REQUIRE(exactInstrument.Render(0, exactBuffer.data(),
                                 static_cast<int>(exactBuffer.size()), false));
  const float exactEndPosition =
      SampleSourceInstrument::renderParams_[0].position_;

  SampleSourceInstrument longInstrument;
  TestSoundSource longSource(kLongSamples.data(),
                             static_cast<int>(kLongSamples.size()), 44100, 1);
  configureMonoPlayback(longInstrument, longSource, SSLM_OSC, 0, 0,
                        static_cast<int>(kLongSamples.size()));

  REQUIRE(longInstrument.Start(0, 60, true));
  CHECK(SampleSourceInstrument::renderParams_[0].rendLoopStart_ == 0);
  CHECK(SampleSourceInstrument::renderParams_[0].rendLoopEnd_ == 4096);
  CHECK(SampleSourceInstrument::renderParams_[0].baseSpeed_ ==
        doctest::Approx(exactBaseSpeed));
  CHECK(SampleSourceInstrument::renderParams_[0].speed_ ==
        doctest::Approx(exactBaseSpeed));

  std::array<AudioSample, 180> longBuffer{};
  REQUIRE(longInstrument.Render(0, longBuffer.data(),
                                static_cast<int>(longBuffer.size()), false));

  CHECK(SampleSourceInstrument::renderParams_[0].position_ ==
        doctest::Approx(exactEndPosition));
  for (size_t i = 0; i < exactBuffer.size(); ++i) {
    CHECK(longBuffer[i] == doctest::Approx(exactBuffer[i]));
  }
}

TEST_CASE("SampleSourceInstrument loop sync mode wraps inside loop range") {
  static constexpr std::array<int16_t, 5> kSamples = {0, 1000, 2000, 3000,
                                                      4000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_LOOPSYNC, 0, 1, 4);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);

  std::array<AudioSample, 6> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  static constexpr std::array<int16_t, 6> kExpected = {1000, 2000, 3000,
                                                       1000, 2000, 3000};
  for (size_t i = 0; i < kExpected.size(); ++i) {
    CHECK(buffer[i] == doctest::Approx(pcmToAudioSample(kExpected[i])));
  }
}

TEST_CASE("SampleSourceInstrument loop sync fast path preserves stereo Hermite "
          "wrap") {
  static constexpr std::array<int16_t, 12> kSamples = {
      30000, -30000, 1000, -2000,  9000,  4000,
      -7000, 12000,  2000, -11000, 28000, 29000};
  static constexpr std::array<int, 6> kBaseFrames = {1, 2, 4, 1, 3, 4};
  static constexpr std::array<float, 6> kFractions = {0.0f, 0.5f, 0.0f,
                                                      0.5f, 0.0f, 0.5f};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), 6, 44100, 2);
  configureMonoPlayback(instrument, source, SSLM_LOOPSYNC, 0, 1, 5);

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.5f);

  std::array<AudioSample, 12> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), 3, false));
  REQUIRE(instrument.Render(0, buffer.data() + 6, 3, false));

  for (size_t frame = 0; frame < kBaseFrames.size(); ++frame) {
    const size_t index = frame * 2;
    CHECK(buffer[index] ==
          doctest::Approx(expectedHermiteChannelSample(
              kSamples.data(), 2, 0, kBaseFrames[frame], kFractions[frame], 1,
              4, TestPlaybackTopology::Wrap)));
    CHECK(buffer[index + 1] ==
          doctest::Approx(expectedHermiteChannelSample(
              kSamples.data(), 2, 1, kBaseFrames[frame], kFractions[frame], 1,
              4, TestPlaybackTopology::Wrap)));
  }
}

TEST_CASE("SampleSourceInstrument uses Hermite interpolation by default") {
  static constexpr std::array<int16_t, 5> kSamples = {0, 12000, 0, -12000, 0};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         22050, 1);
  configureMonoOneShot(instrument, source, static_cast<int>(kSamples.size()));

  REQUIRE(instrument.Start(0, 60, true));

  std::array<AudioSample, 4> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  const AudioSample expected = expectedHermiteSample(
      kSamples.data(), 0, 0.5f, 0, static_cast<int>(kSamples.size()) - 1,
      TestPlaybackTopology::Clamp);
  const AudioSample linearMidpoint =
      0.5f * (pcmToAudioSample(kSamples[0]) + pcmToAudioSample(kSamples[1]));

  CHECK(buffer[1] == doctest::Approx(expected));
  CHECK(buffer[1] != doctest::Approx(linearMidpoint));
}

TEST_CASE(
    "SampleSourceInstrument wraps Hermite interpolation across loop end") {
  static constexpr std::array<int16_t, 4> kSamples = {0, 12000, -4000, -12000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         22050, 1);
  configureMonoOneShot(instrument, source, static_cast<int>(kSamples.size()));
  requireVariable(instrument, FourCC::SampleSourceInstrumentLoopMode)
      ->SetInt(SSLM_LOOP, false);
  requireVariable(instrument, FourCC::SampleSourceInstrumentLoopStart)
      ->SetInt(0, false);

  REQUIRE(instrument.Start(0, 60, true));

  std::array<AudioSample, 8> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  const AudioSample expected = expectedHermiteSample(
      kSamples.data(), 3, 0.5f, 0, static_cast<int>(kSamples.size()) - 1,
      TestPlaybackTopology::Wrap);

  CHECK(buffer[7] == doctest::Approx(expected));
  CHECK(buffer[7] != doctest::Approx(0.5f * (pcmToAudioSample(kSamples[3]) +
                                             pcmToAudioSample(kSamples[0]))));
}

TEST_CASE(
    "SampleSourceInstrument reflects Hermite interpolation in pingpong mode") {
  static constexpr std::array<int16_t, 4> kSamples = {0, 12000, -4000, -12000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(kSamples.data(), static_cast<int>(kSamples.size()),
                         22050, 1);
  configureMonoPlayback(instrument, source, SSLM_LOOP_PINGPONG, 0, 0, 4);

  REQUIRE(instrument.Start(0, 60, true));

  std::array<AudioSample, 8> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));

  const AudioSample expected = expectedHermiteSample(
      kSamples.data(), 2, 0.5f, 0, static_cast<int>(kSamples.size()) - 1,
      TestPlaybackTopology::Reflect);

  CHECK(buffer[7] == doctest::Approx(expected));
}

TEST_CASE(
    "SampleSourceInstrument RTG preserves the high-byte playback offset") {
  static constexpr std::array<int16_t, 8192> samples{};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(samples.data(), static_cast<int>(samples.size()),
                         44100, 1);
  configureMonoPlayback(instrument, source, SSLM_LOOP, 0, 0,
                        static_cast<int>(samples.size()));

  REQUIRE(instrument.Start(0, 60, true));
  forcePlaybackSpeed(1.0f);
  SyncMaster::GetInstance()->SetTempo(120);

  sampleSourceRenderParams &renderParams =
      SampleSourceInstrument::renderParams_[0];
  renderParams.position_ = 5000.0f;
  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandRetrigger,
                            0x0102);

  std::array<AudioSample, 1> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), 0, true));
  REQUIRE(instrument.Render(0, buffer.data(), 0, true));
  CHECK(renderParams.position_ == doctest::Approx(5000.0f));

  REQUIRE(instrument.Render(0, buffer.data(), 0, true));
  const float expectedPosition =
      5000.0f -
      static_cast<int32_t>(SyncMaster::GetInstance()->GetTickSampleCount());
  CHECK(renderParams.position_ == doctest::Approx(expectedPosition));

  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandRetrigger,
                            0x0000);
  REQUIRE(instrument.Render(0, buffer.data(), 0, true));
  CHECK(renderParams.position_ == doctest::Approx(expectedPosition));
}

TEST_CASE("SampleSourceInstrument applies the AudioVoice pitch multiplier to "
          "playback") {
  static constexpr std::array<int16_t, 6> samples = {0,    1000, 2000,
                                                     3000, 4000, 5000};

  TestSamplePool pool;
  SamplePoolInstallGuard poolGuard(&pool);
  SampleSourceInstrument instrument;
  TestSoundSource source(samples.data(), static_cast<int>(samples.size()),
                         44100, 1);
  configureMonoOneShot(instrument, source, static_cast<int>(samples.size()));

  REQUIRE(instrument.Start(0, 60, true));
  instrument.SetPitchMultiplier(0, 2.0f);

  sampleSourceRenderParams &renderParams =
      SampleSourceInstrument::renderParams_[0];
  CHECK(renderParams.speed_ == doctest::Approx(renderParams.baseSpeed_ * 2.0f));

  std::array<AudioSample, 3> buffer{};
  REQUIRE(instrument.Render(0, buffer.data(), static_cast<int>(buffer.size()),
                            false));
  CHECK(buffer[0] == doctest::Approx(pcmToAudioSample(0)));
  CHECK(buffer[1] == doctest::Approx(pcmToAudioSample(2000)));
  CHECK(buffer[2] == doctest::Approx(pcmToAudioSample(4000)));
}
