/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "doctest/doctest.h"

#include "Application/Player/SyncMaster.h"
#include <cstdint>

namespace {
constexpr uint32_t kMidiClock120BpmUs = 20833U;
constexpr uint32_t kMidiClock300BpmUs = 8333U;
constexpr uint32_t kMidiClock400BpmUs = 6250U;
constexpr uint32_t kAudioSampleRate = 44100U;
constexpr uint32_t kMidiClocksPerQuarterNote = 24U;
constexpr uint32_t kMicrosPerMinute = 60000000U;
constexpr uint32_t kUsbFrameUs = 1000U;

void returnToInternalClock(SyncMaster *sync, int tempo) {
  sync->SetClockSource(SyncClockSource::Internal);
  sync->SetTempo(tempo);
  for (int i = 0; i < 96; ++i) {
    sync->UpdateClock(static_cast<uint32_t>(i));
  }
  sync->SetTempo(tempo);
}

uint32_t quantizedMidiClockTimestampUs(uint32_t firstTimestampUs,
                                       uint32_t clock, uint32_t tempo) {
  const uint32_t clocksPerMinute = kMidiClocksPerQuarterNote * tempo;
  const uint64_t elapsedNumerator =
      static_cast<uint64_t>(clock) * kMicrosPerMinute;
  const uint32_t idealElapsedUs = static_cast<uint32_t>(
      (elapsedNumerator + (clocksPerMinute / 2U)) / clocksPerMinute);
  const uint32_t quantizedElapsedUs =
      ((idealElapsedUs + (kUsbFrameUs / 2U)) / kUsbFrameUs) * kUsbFrameUs;
  return firstTimestampUs + quantizedElapsedUs;
}

void checkOneMinuteOfQuantizedMidiClock(SyncMaster *sync, uint32_t tempo,
                                        uint32_t firstTimestampUs) {
  constexpr uint32_t warmupClocks = 120U;
  sync->OnMidiClock(firstTimestampUs);
  for (uint32_t clock = 1U; clock <= warmupClocks; ++clock) {
    const uint32_t timestampUs =
        quantizedMidiClockTimestampUs(firstTimestampUs, clock, tempo);
    sync->OnMidiClock(timestampUs);
    sync->UpdateClock(timestampUs);
  }

  const uint32_t measurementStartUs =
      quantizedMidiClockTimestampUs(firstTimestampUs, warmupClocks, tempo);
  const uint32_t measurementClocks = kMidiClocksPerQuarterNote * tempo;
  double renderedSamples = 0.0;
  uint32_t measurementEndUs = measurementStartUs;
  for (uint32_t clock = warmupClocks + 1U;
       clock <= warmupClocks + measurementClocks; ++clock) {
    measurementEndUs =
        quantizedMidiClockTimestampUs(firstTimestampUs, clock, tempo);
    sync->OnMidiClock(measurementEndUs);
    sync->UpdateClock(measurementEndUs);
    renderedSamples += static_cast<double>(sync->GetPlaySampleCount());
  }

  const double expectedSamples =
      static_cast<double>(measurementEndUs - measurementStartUs) *
      static_cast<double>(kAudioSampleRate) / 1000000.0;
  CHECK(renderedSamples > expectedSamples - 1.0);
  CHECK(renderedSamples < expectedSamples + 1.0);
}

void checkBoundaryPhaseCorrection(SyncMaster *sync, uint32_t tempo,
                                  uint32_t firstTimestampUs,
                                  float expectedPeriodSamples) {
  constexpr uint32_t clockCount = 120U;
  sync->OnMidiClock(firstTimestampUs);
  uint32_t timestampUs = firstTimestampUs;
  for (uint32_t clock = 1U; clock <= clockCount; ++clock) {
    timestampUs = quantizedMidiClockTimestampUs(firstTimestampUs, clock, tempo);
    sync->OnMidiClock(timestampUs);
    sync->UpdateClock(timestampUs);
  }
  REQUIRE(sync->GetPlaySampleCount() ==
          doctest::Approx(expectedPeriodSamples).epsilon(0.002));

  sync->UpdateClock(timestampUs + 1000U);
  CHECK(sync->GetPlaySampleCount() < expectedPeriodSamples);
}
} // namespace

TEST_CASE("SyncMaster acquires MIDI clock without stepping directly from it") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);

  REQUIRE(sync->GetActiveClockSource() == SyncClockSource::Internal);
  REQUIRE(sync->GetPlaySampleCount() == doctest::Approx(918.75f));

  sync->SetClockSource(SyncClockSource::Midi);
  sync->OnMidiClock(100000U);
  sync->UpdateClock(100000U);
  CHECK_FALSE(sync->IsMidiClockLocked());
  CHECK(sync->GetActiveClockSource() == SyncClockSource::Internal);

  const uint32_t secondClockUs = 100000U + kMidiClock120BpmUs;
  sync->OnMidiClock(secondClockUs);
  sync->UpdateClock(secondClockUs);

  CHECK(sync->IsMidiClockLocked());
  CHECK(sync->GetActiveClockSource() == SyncClockSource::Midi);
  CHECK(sync->GetMidiClockPeriodUs() == kMidiClock120BpmUs);
  CHECK(sync->GetTempo() == 120);
  CHECK(sync->GetPlaySampleCount() == doctest::Approx(918.735f).epsilon(0.001));

  returnToInternalClock(sync, 120);
}

TEST_CASE("SyncMaster slews to MIDI clock and holds the project tempo") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);
  sync->SetClockSource(SyncClockSource::Midi);

  uint32_t timestampUs = 200000U;
  sync->OnMidiClock(timestampUs);
  timestampUs += kMidiClock300BpmUs;
  sync->OnMidiClock(timestampUs);
  sync->UpdateClock(timestampUs);

  CHECK(sync->GetActiveClockSource() == SyncClockSource::Midi);
  CHECK(sync->GetPlaySampleCount() < 918.75f);
  CHECK(sync->GetPlaySampleCount() > 367.0f);

  sync->SetTempo(200);
  CHECK(sync->GetTempo() < 200);

  for (int i = 0; i < 64; ++i) {
    timestampUs += kMidiClock300BpmUs;
    sync->OnMidiClock(timestampUs);
    sync->UpdateClock(timestampUs);
  }

  CHECK(sync->GetTempo() == 300);
  CHECK(sync->GetPlaySampleCount() == doctest::Approx(367.5f).epsilon(0.002));

  returnToInternalClock(sync, 120);
}

TEST_CASE("SyncMaster reaches its limit with faster MIDI clock input") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);
  sync->SetClockSource(SyncClockSource::Midi);

  uint32_t timestampUs = 225000U;
  sync->OnMidiClock(timestampUs);
  for (int i = 0; i < 96; ++i) {
    timestampUs += kMidiClock400BpmUs;
    sync->OnMidiClock(timestampUs);

    // Keep render boundaries late relative to each unsupported external pulse.
    // Phase correction at the clamped period would otherwise be one-sided and
    // pull the resulting tempo below the supported maximum.
    sync->UpdateClock(timestampUs + 4500U);
  }

  CHECK(sync->GetMidiClockPeriodUs() == kMidiClock300BpmUs);
  CHECK(sync->GetTempo() == 300);
  CHECK(sync->GetPlaySampleCount() == doctest::Approx(367.5f).epsilon(0.002));

  returnToInternalClock(sync, 120);
}

TEST_CASE("SyncMaster reaches its limit after following a slower MIDI clock") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);
  sync->SetClockSource(SyncClockSource::Midi);

  uint32_t timestampUs = 225000U;
  sync->OnMidiClock(timestampUs);
  for (int i = 0; i < 96; ++i) {
    timestampUs += 8503U; // Approximately 294 BPM.
    sync->OnMidiClock(timestampUs);
    sync->UpdateClock(timestampUs);
  }
  REQUIRE(sync->GetTempo() == 294);

  for (int i = 0; i < 96; ++i) {
    timestampUs += kMidiClock400BpmUs;
    sync->OnMidiClock(timestampUs);
    sync->UpdateClock(timestampUs + 4500U);
  }

  CHECK(sync->GetMidiClockPeriodUs() == kMidiClock300BpmUs);
  CHECK(sync->GetTempo() == 300);
  CHECK(sync->GetPlaySampleCount() == doctest::Approx(367.5f).epsilon(0.002));

  returnToInternalClock(sync, 120);
}

TEST_CASE("SyncMaster averages USB-quantized clock before limiting tempo") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);
  sync->SetClockSource(SyncClockSource::Midi);

  constexpr uint32_t quantizedIntervalsUs[] = {8000U, 8000U, 9000U};
  uint32_t timestampUs = 225000U;
  sync->OnMidiClock(timestampUs);
  for (int i = 0; i < 120; ++i) {
    timestampUs += quantizedIntervalsUs[i % 3];
    sync->OnMidiClock(timestampUs);
    sync->UpdateClock(timestampUs);
  }

  CHECK(sync->GetMidiClockPeriodUs() == kMidiClock300BpmUs);
  CHECK(sync->GetTempo() == 300);
  CHECK(sync->GetPlaySampleCount() == doctest::Approx(367.5f).epsilon(0.002));

  returnToInternalClock(sync, 120);
}

TEST_CASE("SyncMaster preserves USB-quantized tempo below its limit") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);
  sync->SetClockSource(SyncClockSource::Midi);

  constexpr uint32_t sourceTempo = 299U;
  constexpr uint32_t clocksPerMinute = 24U * sourceTempo;
  constexpr uint32_t usbFrameUs = 1000U;
  constexpr uint32_t firstTimestampUs = 225000U;
  sync->OnMidiClock(firstTimestampUs);
  for (uint32_t clock = 1; clock <= 360U; ++clock) {
    const uint64_t elapsedNumerator =
        static_cast<uint64_t>(clock) * 60000000ULL;
    const uint32_t idealElapsedUs = static_cast<uint32_t>(
        (elapsedNumerator + (clocksPerMinute / 2U)) / clocksPerMinute);
    const uint32_t quantizedElapsedUs =
        ((idealElapsedUs + (usbFrameUs / 2U)) / usbFrameUs) * usbFrameUs;
    const uint32_t timestampUs = firstTimestampUs + quantizedElapsedUs;
    sync->OnMidiClock(timestampUs);
    sync->UpdateClock(timestampUs);
  }

  CHECK(sync->GetDisplayTempo() == 299);

  returnToInternalClock(sync, 120);
}

TEST_CASE("SyncMaster preserves cumulative timing at quantized tempo limits") {
  SyncMaster *sync = SyncMaster::GetInstance();

  SUBCASE("60 BPM") {
    returnToInternalClock(sync, 120);
    sync->SetClockSource(SyncClockSource::Midi);
    checkOneMinuteOfQuantizedMidiClock(sync, 60U, 1000000U);
    CHECK(sync->GetTempo() == 60);
    returnToInternalClock(sync, 120);
  }

  SUBCASE("300 BPM") {
    returnToInternalClock(sync, 120);
    sync->SetClockSource(SyncClockSource::Midi);
    checkOneMinuteOfQuantizedMidiClock(sync, 300U, 2000000U);
    CHECK(sync->GetTempo() == 300);
    returnToInternalClock(sync, 120);
  }
}

TEST_CASE("SyncMaster phase-corrects valid clocks at the tempo limits") {
  SyncMaster *sync = SyncMaster::GetInstance();

  SUBCASE("60 BPM") {
    returnToInternalClock(sync, 120);
    sync->SetClockSource(SyncClockSource::Midi);
    checkBoundaryPhaseCorrection(sync, 60U, 3000000U, 1837.5f);
    returnToInternalClock(sync, 120);
  }

  SUBCASE("300 BPM") {
    returnToInternalClock(sync, 120);
    sync->SetClockSource(SyncClockSource::Midi);
    checkBoundaryPhaseCorrection(sync, 300U, 4000000U, 367.5f);
    returnToInternalClock(sync, 120);
  }
}

TEST_CASE("SyncMaster averages the displayed MIDI tempo over one second") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);
  REQUIRE(sync->GetDisplayTempo() == 120);
  sync->SetClockSource(SyncClockSource::Midi);

  uint32_t timestampUs = 250000U;
  sync->OnMidiClock(timestampUs);
  sync->UpdateClock(timestampUs);
  timestampUs += kMidiClock300BpmUs;
  sync->OnMidiClock(timestampUs);
  sync->UpdateClock(timestampUs);

  // The real oscillator follows the new clock immediately, but the status
  // value remains unchanged until a complete display window has elapsed.
  for (int i = 0; i < 60; ++i) {
    timestampUs += kMidiClock300BpmUs;
    sync->OnMidiClock(timestampUs);
    sync->UpdateClock(timestampUs);
  }
  REQUIRE(sync->GetTempo() > 290);
  CHECK(sync->GetDisplayTempo() == 120);

  for (int i = 0; i < 64; ++i) {
    timestampUs += kMidiClock300BpmUs;
    sync->OnMidiClock(timestampUs);
    sync->UpdateClock(timestampUs);
  }
  CHECK(sync->GetDisplayTempo() > 250);
  CHECK(sync->GetDisplayTempo() <= 300);

  // A second complete window at the settled rate reports that rate while the
  // underlying oscillator continues to receive every clock and phase update.
  for (int i = 0; i < 122; ++i) {
    timestampUs += kMidiClock300BpmUs;
    sync->OnMidiClock(timestampUs);
    sync->UpdateClock(timestampUs);
  }
  CHECK(sync->GetDisplayTempo() == 300);

  returnToInternalClock(sync, 120);
}

TEST_CASE("SyncMaster displays internal tempo changes without averaging") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);

  sync->SetTempo(230);
  sync->UpdateClock(100000U);

  CHECK(sync->GetTempo() == 230);
  CHECK(sync->GetDisplayTempo() == 230);

  returnToInternalClock(sync, 120);
}

TEST_CASE(
    "SyncMaster smoothly returns to internal tempo when MIDI clock stops") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);
  sync->SetClockSource(SyncClockSource::Midi);

  uint32_t timestampUs = 300000U;
  sync->OnMidiClock(timestampUs);
  for (int i = 0; i < 64; ++i) {
    timestampUs += kMidiClock300BpmUs;
    sync->OnMidiClock(timestampUs);
    sync->UpdateClock(timestampUs);
  }
  REQUIRE(sync->GetTempo() == 300);

  const float externalPeriodSamples = sync->GetPlaySampleCount();
  const uint32_t afterTimeoutUs = timestampUs + 90000U;
  sync->UpdateClock(afterTimeoutUs);

  CHECK_FALSE(sync->IsMidiClockLocked());
  CHECK(sync->GetActiveClockSource() == SyncClockSource::Internal);
  CHECK(sync->GetPlaySampleCount() > externalPeriodSamples);
  CHECK(sync->GetPlaySampleCount() < 918.75f);

  for (int i = 0; i < 96; ++i) {
    sync->UpdateClock(afterTimeoutUs + static_cast<uint32_t>(i + 1));
  }

  CHECK(sync->GetTempo() == 120);
  CHECK(sync->GetPlaySampleCount() == doctest::Approx(918.75f));

  returnToInternalClock(sync, 120);
}

TEST_CASE("SyncMaster reacquires MIDI clock after holdover") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);
  sync->SetClockSource(SyncClockSource::Midi);

  uint32_t timestampUs = 400000U;
  sync->OnMidiClock(timestampUs);
  timestampUs += kMidiClock120BpmUs;
  sync->OnMidiClock(timestampUs);
  sync->UpdateClock(timestampUs);
  REQUIRE(sync->IsMidiClockLocked());

  timestampUs += 100000U;
  sync->UpdateClock(timestampUs);
  REQUIRE_FALSE(sync->IsMidiClockLocked());

  sync->OnMidiClock(timestampUs);
  CHECK_FALSE(sync->IsMidiClockLocked());
  timestampUs += kMidiClock300BpmUs;
  sync->OnMidiClock(timestampUs);
  sync->UpdateClock(timestampUs);

  CHECK(sync->IsMidiClockLocked());
  CHECK(sync->GetActiveClockSource() == SyncClockSource::Midi);
  CHECK(sync->GetMidiClockPeriodUs() == kMidiClock300BpmUs);

  returnToInternalClock(sync, 120);
}

TEST_CASE("SyncMaster folds a missed MIDI clock instead of halving tempo") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);
  sync->SetClockSource(SyncClockSource::Midi);

  sync->OnMidiClock(500000U);
  sync->OnMidiClock(500000U + kMidiClock120BpmUs);
  sync->OnMidiClock(500000U + (kMidiClock120BpmUs * 3U));

  CHECK(sync->GetMidiClockPeriodUs() == kMidiClock120BpmUs);

  returnToInternalClock(sync, 120);
}

TEST_CASE("SyncMaster follows a sustained slower MIDI clock") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);
  sync->SetClockSource(SyncClockSource::Midi);

  uint32_t timestampUs = 600000U;
  sync->OnMidiClock(timestampUs);
  timestampUs += kMidiClock120BpmUs;
  sync->OnMidiClock(timestampUs);

  constexpr uint32_t midiClock60BpmUs = 41666U;
  for (int i = 0; i < 12; ++i) {
    timestampUs += midiClock60BpmUs;
    sync->OnMidiClock(timestampUs);
  }

  CHECK(sync->GetMidiClockPeriodUs() ==
        doctest::Approx(midiClock60BpmUs).epsilon(0.05));

  returnToInternalClock(sync, 120);
}

TEST_CASE("SyncMaster handles the microsecond timestamp wrapping") {
  SyncMaster *sync = SyncMaster::GetInstance();
  returnToInternalClock(sync, 120);
  sync->SetClockSource(SyncClockSource::Midi);

  constexpr uint32_t firstTimestampUs = UINT32_MAX - 10000U;
  sync->OnMidiClock(firstTimestampUs);
  sync->OnMidiClock(firstTimestampUs + kMidiClock120BpmUs);

  CHECK(sync->IsMidiClockLocked());
  CHECK(sync->GetMidiClockPeriodUs() == kMidiClock120BpmUs);

  returnToInternalClock(sync, 120);
}
