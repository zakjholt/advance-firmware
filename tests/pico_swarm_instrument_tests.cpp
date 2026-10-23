/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "doctest/doctest.h"

#include "Application/Instruments/picoSwarmInstrument.h"
#include <algorithm>
#include <cmath>

namespace {

double measureHighPassGain(float inputFrequency, float cutoffFrequency) {
  static constexpr size_t settlingSamples = 16384;
  static constexpr size_t measurementSamples = 16384;
  static constexpr size_t blockSize = 64;
  static constexpr double twoPi = 6.28318530717958647692;
  picoSwarm::PitchTrackedHighPass filter;
  filter.Configure(cutoffFrequency);
  filter.Reset();

  AudioSample buffer[blockSize];
  double phase = 0.0;
  double inputPower = 0.0;
  double outputPower = 0.0;
  const double phaseIncrement =
      twoPi * static_cast<double>(inputFrequency) / picoSwarm::SampleRate;
  const size_t totalSamples = settlingSamples + measurementSamples;
  for (size_t blockStart = 0; blockStart < totalSamples;
       blockStart += blockSize) {
    const size_t samplesInBlock =
        std::min(blockSize, totalSamples - blockStart);
    for (size_t sample = 0; sample < samplesInBlock; ++sample) {
      const AudioSample input = static_cast<AudioSample>(std::sin(phase));
      buffer[sample] = input;
      phase += phaseIncrement;
      if (phase >= twoPi) {
        phase -= twoPi;
      }
      if (blockStart + sample >= settlingSamples) {
        inputPower += static_cast<double>(input) * input;
      }
    }

    filter.ProcessBlock(buffer, samplesInBlock);
    for (size_t sample = 0; sample < samplesInBlock; ++sample) {
      if (blockStart + sample >= settlingSamples) {
        outputPower += static_cast<double>(buffer[sample]) * buffer[sample];
      }
    }
  }
  return std::sqrt(outputPower / inputPower);
}

} // namespace

TEST_CASE("picoSwarm detune curve follows the measured JP-8000 points") {
  struct DetunePoint {
    float control;
    float amount;
  };
  static constexpr DetunePoint points[] = {
      {0.0f, 0.0f},
      {31.0f / 127.0f, 0.0467636f},
      {63.0f / 127.0f, 0.0967273f},
      {95.0f / 127.0f, 0.243418f},
      {119.0f / 127.0f, 0.3928f},
      {1.0f, 1.0f},
  };

  for (const DetunePoint &point : points) {
    CHECK(picoSwarm::Voice::DetuneAmount(point.control) ==
          doctest::Approx(point.amount).epsilon(0.015));
  }
}

TEST_CASE("picoSwarm detune uses the recovered upper control steps") {
  struct DetunePoint {
    uint8_t control;
    float amount;
  };
  static constexpr DetunePoint points[] = {
      {0x77, 0.39375f}, {0x78, 0.4f},   {0x79, 0.425f},
      {0x7A, 0.45f},    {0x7B, 0.475f}, {0x7C, 0.525f},
      {0x7D, 0.6f},     {0x7E, 0.7f},   {0x7F, 1.0f},
  };

  for (const DetunePoint &point : points) {
    const float normalizedControl = point.control / 127.0f;
    CHECK(picoSwarm::Voice::DetuneAmount(normalizedControl) ==
          doctest::Approx(point.amount).epsilon(0.000001));
  }
}

TEST_CASE("picoSwarm maximum detune uses all seven measured ratios") {
  static constexpr float expectedRatios[picoSwarm::OscillatorCount] = {
      0.88997687f, 0.93711561f, 0.98047644f, 1.0f,
      1.01991221f, 1.06216538f, 1.10745242f};

  for (size_t oscillator = 0; oscillator < picoSwarm::OscillatorCount;
       ++oscillator) {
    CHECK(picoSwarm::Voice::OscillatorRatio(oscillator, 0.0f) ==
          doctest::Approx(1.0f));
    CHECK(picoSwarm::Voice::OscillatorRatio(oscillator, 1.0f) ==
          doctest::Approx(expectedRatios[oscillator]).epsilon(0.000001));
  }
}

TEST_CASE("picoSwarm mix follows the measured center and side gain curves") {
  CHECK(picoSwarm::Voice::CenterGain(0.0f) == doctest::Approx(0.99785f));
  CHECK(picoSwarm::Voice::CenterGain(1.0f) == doctest::Approx(0.44419f));
  CHECK(picoSwarm::Voice::SideGain(0.0f) == doctest::Approx(0.044372f));
  CHECK(picoSwarm::Voice::SideGain(1.0f) == doctest::Approx(0.590832f));

  const float measuredControl = 95.0f / 127.0f;
  CHECK(picoSwarm::Voice::CenterGain(measuredControl) ==
        doctest::Approx(0.585f).epsilon(0.01));
  CHECK(picoSwarm::Voice::SideGain(measuredControl) ==
        doctest::Approx(0.59f).epsilon(0.01));
}

TEST_CASE("picoSwarm high pass has a pitch-tracked four-pole response") {
  static constexpr float cutoff = 440.0f;

  // A fourth-order Butterworth high-pass is about -24 dB one octave below
  // cutoff, -3 dB at cutoff, and effectively unity one octave above it.
  CHECK(measureHighPassGain(cutoff * 0.5f, cutoff) ==
        doctest::Approx(0.06232).epsilon(0.015));
  CHECK(measureHighPassGain(cutoff, cutoff) ==
        doctest::Approx(0.70711).epsilon(0.005));
  CHECK(measureHighPassGain(cutoff * 2.0f, cutoff) ==
        doctest::Approx(0.99807).epsilon(0.005));
}

TEST_CASE("picoSwarm retriggers randomize phases while legato preserves them") {
  picoSwarm::Voice voice;
  voice.Reset(0x12345678U);
  voice.Start(60, true);

  double firstPhases[picoSwarm::OscillatorCount];
  for (size_t oscillator = 0; oscillator < picoSwarm::OscillatorCount;
       ++oscillator) {
    firstPhases[oscillator] = voice.GetPhase(oscillator);
  }

  voice.Start(67, false);
  CHECK(voice.GetBaseFrequency() ==
        doctest::Approx(picoSwarm::Voice::NoteFrequency(67)));
  for (size_t oscillator = 0; oscillator < picoSwarm::OscillatorCount;
       ++oscillator) {
    CHECK(voice.GetPhase(oscillator) == firstPhases[oscillator]);
  }

  voice.Start(67, true);
  bool anyPhaseChanged = false;
  for (size_t oscillator = 0; oscillator < picoSwarm::OscillatorCount;
       ++oscillator) {
    anyPhaseChanged = anyPhaseChanged ||
                      voice.GetPhase(oscillator) != firstPhases[oscillator];
  }
  CHECK(anyPhaseChanged);
}

TEST_CASE("picoSwarm applies the AudioVoice pitch multiplier") {
  picoSwarm::Voice voice;
  voice.Reset(0x12345678U);
  voice.Start(69, true);
  CHECK(voice.GetBaseFrequency() == doctest::Approx(440.0f));

  voice.SetPitchMultiplier(2.0f);
  CHECK(voice.GetBaseFrequency() == doctest::Approx(880.0f));

  voice.SetPitchMultiplier(0.5f);
  CHECK(voice.GetBaseFrequency() == doctest::Approx(220.0f));
}

TEST_CASE("picoSwarm keeps phase normalized at maximum command pitch") {
  picoSwarm::Voice voice;
  voice.Reset(0x12345678U);
  voice.Start(119, true);

  // PSL + LEG + PFT + ARP can contribute at most 270 semitones together.
  voice.SetPitchMultiplier(std::pow(2.0f, 270.0f / 12.0f));

  AudioSample buffer[16]{};
  REQUIRE(voice.Render(buffer, 16, 127, 127));
  for (AudioSample sample : buffer) {
    CHECK(std::isfinite(sample));
    CHECK(std::abs(sample) < 10.0f);
  }
  for (size_t oscillator = 0; oscillator < picoSwarm::OscillatorCount;
       ++oscillator) {
    CHECK(voice.GetPhase(oscillator) >= 0.0);
    CHECK(voice.GetPhase(oscillator) < 1.0);
  }
}

TEST_CASE("picoSwarm detune changes side rates but not the center rate") {
  picoSwarm::Voice unison;
  picoSwarm::Voice detuned;
  static AudioSample unisonBuffer[128];
  static AudioSample detunedBuffer[128];

  unison.Reset(0xA5A5A5A5U);
  detuned.Reset(0xA5A5A5A5U);
  unison.Start(69, true);
  detuned.Start(69, true);
  REQUIRE(unison.Render(unisonBuffer, 128, 0, 127));
  REQUIRE(detuned.Render(detunedBuffer, 128, 127, 127));

  CHECK(detuned.GetPhase(picoSwarm::CenterOscillator) ==
        doctest::Approx(unison.GetPhase(picoSwarm::CenterOscillator)));
  CHECK(std::abs(detuned.GetPhase(0) - unison.GetPhase(0)) > 0.001);
  CHECK(std::abs(detuned.GetPhase(6) - unison.GetPhase(6)) > 0.001);
}

TEST_CASE("picoSwarm instrument renders packed mono through its source API") {
  static constexpr size_t renderSamples = 64;
  static constexpr AudioSample sentinel = 12345.0f;
  static AudioSample buffer[renderSamples * 2];
  picoSwarmInstrument instrument;

  REQUIRE(instrument.Init());
  CHECK(instrument.GetAudioBufferLayout(0) == AudioBufferLayout::MonoPacked);
  REQUIRE(instrument.Start(0, 60, true));
  for (AudioSample &sample : buffer) {
    sample = sentinel;
  }
  REQUIRE(instrument.Render(0, buffer, renderSamples, false));

  AudioSample magnitude = 0.0f;
  for (size_t sample = 0; sample < renderSamples; ++sample) {
    CHECK(buffer[sample] != sentinel);
    CHECK(buffer[renderSamples + sample] == sentinel);
    magnitude += std::abs(buffer[sample]);
  }
  CHECK(magnitude > 0.01f);

  instrument.Stop(0);
  CHECK_FALSE(instrument.Render(0, buffer, renderSamples, false));
}
