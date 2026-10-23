/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "SyncMaster.h"
#include "Services/Audio/Audio.h"
#include "config/MemorySections.h"
#include <algorithm>

namespace {
constexpr int kDefaultTempo = 120;
constexpr uint32_t kMicrosPerMinute = 60000000U;
constexpr uint32_t kMidiClocksPerQuarterNote = 24U;
constexpr uint32_t kMidiClockPeriodFractionBits = 12U;
constexpr uint32_t kMidiClockPeriodScale = 1U << kMidiClockPeriodFractionBits;
constexpr uint32_t midiClockPeriodQ12(uint32_t tempo) {
  const uint32_t clocksPerMinute = kMidiClocksPerQuarterNote * tempo;
  return static_cast<uint32_t>(
      (static_cast<uint64_t>(kMicrosPerMinute) * kMidiClockPeriodScale +
       (clocksPerMinute / 2U)) /
      clocksPerMinute);
}
constexpr uint32_t kMinimumMidiClockPeriodQ12 = midiClockPeriodQ12(300U);
constexpr uint32_t kMaximumMidiClockPeriodQ12 = midiClockPeriodQ12(60U);
constexpr uint32_t kMaximumMissingClocksToFold = 4U;
constexpr uint32_t kMidiClockLossTimeoutUs =
    ((kMicrosPerMinute / (kMidiClocksPerQuarterNote * 60U)) * 2U) + 2U;
constexpr float kClockPeriodSlewDivisor = 8.0f;
constexpr float kClockPeriodSnapThresholdSamples = 0.25f;
constexpr uint32_t kDisplayTempoWindowUs = 1000000U;
} // namespace

SyncMaster::SyncMaster()
    : tempo_(kDefaultTempo), displayTempo_(kDefaultTempo),
      internalTempo_(kDefaultTempo), currentSlice_(0), beatCount_(0),
      playSampleCount_(0.0f), tickSampleCount_(0.0f), sampleRate_(44100),
      clockSource_(SyncClockSource::Internal),
      activeClockSource_(SyncClockSource::Internal), midiClockPeriodQ12_(0),
      midiClockIntervalSumUs_(0), lastMidiClockTimestampUs_(0),
      lastMidiClockIntervalUs_(0), lastDisplayTempoTimestampUs_(0),
      displayTempoWindowElapsedUs_(0), displayTempoAccumulator_(0.0f),
      midiClockIntervalIndex_(0), midiClockIntervalCount_(0),
      hasMidiClockTimestamp_(false), midiClockLocked_(false),
      returningToInternalClock_(false), displayTempoWindowActive_(false),
      midiClockPeriodLimited_(false) {}

void SyncMaster::Start() {
  currentSlice_ = 0;
  beatCount_ = 0;
};

void SyncMaster::Stop(){};

PICOTRACKER_FAST_CODE("sync_master_set_tempo")
void SyncMaster::SetTempo(int tempo) {
  internalTempo_ = tempo;
  sampleRate_ = Audio::GetInstance()->GetSampleRate();
  if ((activeClockSource_ == SyncClockSource::Internal &&
       !returningToInternalClock_) ||
      playSampleCount_ <= 0.0f) {
    applyInternalTempo();
  }
};

void SyncMaster::applyInternalTempo() {
  const float internalPeriodSamples =
      60.0f * sampleRate_ * 2.0f / internalTempo_ / 8.0f / float(SlicesPerStep);
  applyClockPeriod(internalPeriodSamples);
};

int SyncMaster::GetTempo() { return tempo_; };

int SyncMaster::GetDisplayTempo() const { return displayTempo_; }

void SyncMaster::SetClockSource(SyncClockSource source) {
  if (clockSource_ == source) {
    return;
  }

  clockSource_ = source;
  resetMidiClock();

  if (clockSource_ == SyncClockSource::Internal) {
    returningToInternalClock_ = activeClockSource_ == SyncClockSource::Midi;
    activeClockSource_ = SyncClockSource::Internal;
    if (!returningToInternalClock_) {
      applyInternalTempo();
    }
  } else {
    returningToInternalClock_ = false;
  }
}

void SyncMaster::resetMidiClock() {
  resetMidiClockPeriodEstimate();
  lastMidiClockTimestampUs_ = 0;
  hasMidiClockTimestamp_ = false;
}

void SyncMaster::resetMidiClockPeriodEstimate() {
  midiClockPeriodQ12_ = 0;
  midiClockIntervalSumUs_ = 0;
  midiClockIntervalIndex_ = 0;
  midiClockIntervalCount_ = 0;
  lastMidiClockIntervalUs_ = 0;
  midiClockLocked_ = false;
  midiClockPeriodLimited_ = false;
}

SyncClockSource SyncMaster::GetClockSource() const { return clockSource_; }

SyncClockSource SyncMaster::GetActiveClockSource() const {
  return activeClockSource_;
}

void SyncMaster::applyClockPeriod(float periodSamples) {
  playSampleCount_ = periodSamples;
  tickSampleCount_ = periodSamples;
  const float effectiveTempo =
      60.0f * static_cast<float>(sampleRate_) /
      (periodSamples * static_cast<float>(kMidiClocksPerQuarterNote));
  tempo_ = static_cast<int>(effectiveTempo + 0.5f);
}

void SyncMaster::slewClockPeriod(float targetPeriodSamples) {
  const float errorSamples = targetPeriodSamples - playSampleCount_;
  const float absoluteErrorSamples =
      errorSamples < 0.0f ? -errorSamples : errorSamples;
  if (absoluteErrorSamples <= kClockPeriodSnapThresholdSamples) {
    applyClockPeriod(targetPeriodSamples);
    return;
  }

  applyClockPeriod(playSampleCount_ + (errorSamples / kClockPeriodSlewDivisor));
}

void SyncMaster::updateDisplayTempo(uint32_t timestampUs) {
  const bool smoothDisplayTempo =
      activeClockSource_ == SyncClockSource::Midi || returningToInternalClock_;
  if (!smoothDisplayTempo) {
    displayTempo_ = tempo_;
    displayTempoWindowElapsedUs_ = 0;
    displayTempoAccumulator_ = 0.0f;
    displayTempoWindowActive_ = false;
    return;
  }

  if (!displayTempoWindowActive_) {
    lastDisplayTempoTimestampUs_ = timestampUs;
    displayTempoWindowActive_ = true;
    return;
  }

  const uint32_t elapsedUs = timestampUs - lastDisplayTempoTimestampUs_;
  lastDisplayTempoTimestampUs_ = timestampUs;
  if (elapsedUs == 0U) {
    return;
  }

  // A large gap means there were no oscillator samples to average. Restart the
  // window at the current effective tempo instead of treating the gap as if the
  // last known rate had continued throughout it.
  if (elapsedUs > kDisplayTempoWindowUs) {
    displayTempo_ = tempo_;
    displayTempoWindowElapsedUs_ = 0;
    displayTempoAccumulator_ = 0.0f;
    return;
  }

  const float elapsedSeconds = static_cast<float>(elapsedUs) / 1000000.0f;
  displayTempoAccumulator_ += static_cast<float>(tempo_) * elapsedSeconds;
  displayTempoWindowElapsedUs_ += elapsedUs;
  if (displayTempoWindowElapsedUs_ < kDisplayTempoWindowUs) {
    return;
  }

  const float windowSeconds =
      static_cast<float>(displayTempoWindowElapsedUs_) / 1000000.0f;
  displayTempo_ =
      static_cast<int>((displayTempoAccumulator_ / windowSeconds) + 0.5f);
  displayTempoWindowElapsedUs_ = 0;
  displayTempoAccumulator_ = 0.0f;
}

void SyncMaster::OnMidiClock(uint32_t timestampUs) {
  if (clockSource_ != SyncClockSource::Midi) {
    return;
  }

  if (!hasMidiClockTimestamp_) {
    lastMidiClockTimestampUs_ = timestampUs;
    hasMidiClockTimestamp_ = true;
    return;
  }

  uint32_t intervalUs = timestampUs - lastMidiClockTimestampUs_;
  lastMidiClockTimestampUs_ = timestampUs;
  if (intervalUs == 0U) {
    return;
  }

  // A clock after the holdover timeout begins a new lock acquisition. This
  // prevents the time without MIDI from being interpreted as a very slow beat.
  if (intervalUs > kMidiClockLossTimeoutUs) {
    resetMidiClockPeriodEstimate();
    return;
  }

  const uint32_t rawIntervalUs = intervalUs;
  const uint32_t intervalQ12 = intervalUs << kMidiClockPeriodFractionBits;

  // Fold a short gap back to one clock interval. Audio continues rendering
  // through an occasional dropped MIDI byte, so one missed clock must not be
  // interpreted as an immediate halving of the external tempo.
  const bool longInterval =
      midiClockLocked_ &&
      intervalQ12 > midiClockPeriodQ12_ + (midiClockPeriodQ12_ / 2U);
  const uint32_t repeatedIntervalDifferenceUs =
      (rawIntervalUs > lastMidiClockIntervalUs_)
          ? rawIntervalUs - lastMidiClockIntervalUs_
          : lastMidiClockIntervalUs_ - rawIntervalUs;
  const bool repeatedLongInterval =
      longInterval && lastMidiClockIntervalUs_ != 0U &&
      (repeatedIntervalDifferenceUs << kMidiClockPeriodFractionBits) <=
          (midiClockPeriodQ12_ / 4U);
  if (longInterval && !repeatedLongInterval) {
    const uint32_t elapsedClocks =
        (intervalQ12 + (midiClockPeriodQ12_ / 2U)) / midiClockPeriodQ12_;
    if (elapsedClocks >= 2U && elapsedClocks <= kMaximumMissingClocksToFold) {
      intervalUs /= elapsedClocks;
    }
  }
  lastMidiClockIntervalUs_ = rawIntervalUs;

  if (midiClockIntervalCount_ == MidiClockPeriodWindowSize) {
    midiClockIntervalSumUs_ -= midiClockIntervalsUs_[midiClockIntervalIndex_];
  } else {
    ++midiClockIntervalCount_;
  }

  // Average observed intervals before limiting the supported tempo. USB MIDI
  // can quantize a steady 300 BPM clock to 8, 8, then 9 ms. Clamping the two
  // short intervals first would leave the 9 ms interval unbalanced and read as
  // about 292 BPM instead of 300 BPM. Retaining fractional microseconds also
  // avoids accumulating integer-rounding error at the supported tempo limits.
  midiClockIntervalsUs_[midiClockIntervalIndex_] = intervalUs;
  midiClockIntervalSumUs_ += intervalUs;
  midiClockIntervalIndex_ = static_cast<uint8_t>(
      (midiClockIntervalIndex_ + 1U) % MidiClockPeriodWindowSize);
  const uint32_t averageIntervalQ12 =
      ((midiClockIntervalSumUs_ << kMidiClockPeriodFractionBits) +
       (midiClockIntervalCount_ / 2U)) /
      midiClockIntervalCount_;
  midiClockPeriodLimited_ = averageIntervalQ12 < kMinimumMidiClockPeriodQ12 ||
                            averageIntervalQ12 > kMaximumMidiClockPeriodQ12;
  midiClockPeriodQ12_ =
      std::clamp(averageIntervalQ12, kMinimumMidiClockPeriodQ12,
                 kMaximumMidiClockPeriodQ12);
  midiClockLocked_ = true;
}

void SyncMaster::UpdateClock(uint32_t timestampUs) {
  // The effective tempo can make small phase corrections every render cycle.
  // Average the value used during the preceding interval for the status line.
  updateDisplayTempo(timestampUs);

  const uint32_t midiClockAgeUs = timestampUs - lastMidiClockTimestampUs_;
  const bool midiClockFresh = clockSource_ == SyncClockSource::Midi &&
                              midiClockLocked_ && hasMidiClockTimestamp_ &&
                              midiClockAgeUs <= kMidiClockLossTimeoutUs;

  if (midiClockFresh) {
    activeClockSource_ = SyncClockSource::Midi;
    returningToInternalClock_ = false;

    uint32_t targetPeriodQ12 = midiClockPeriodQ12_;
    if (!midiClockPeriodLimited_) {
      // Compare the audio render boundary with the nearest external pulse and
      // correct only a bounded part of that phase error. The period estimator
      // rejects MIDI arrival jitter; this small correction prevents phase
      // drift.
      const uint32_t elapsedQ12 = midiClockAgeUs
                                  << kMidiClockPeriodFractionBits;
      const uint32_t phaseOffsetQ12 = elapsedQ12 % midiClockPeriodQ12_;
      int32_t phaseErrorQ12 = static_cast<int32_t>(phaseOffsetQ12);
      if (phaseOffsetQ12 > (midiClockPeriodQ12_ / 2U)) {
        phaseErrorQ12 -= static_cast<int32_t>(midiClockPeriodQ12_);
      }

      const int32_t maximumCorrectionQ12 =
          static_cast<int32_t>(midiClockPeriodQ12_ / 16U);
      const int32_t phaseCorrectionQ12 = std::clamp(
          phaseErrorQ12 / 4, -maximumCorrectionQ12, maximumCorrectionQ12);
      targetPeriodQ12 = static_cast<uint32_t>(
          static_cast<int32_t>(midiClockPeriodQ12_) - phaseCorrectionQ12);
    }

    // The supported range limits the estimated source tempo, not the bounded
    // phase correction. Allowing a correction on either side of a valid 60 or
    // 300 BPM estimate avoids a one-sided controller and keeps boundary clocks
    // phase-correctable. An input outside the range remains at the hard limit.
    const float targetPeriodSamples =
        static_cast<float>(targetPeriodQ12) *
        (static_cast<float>(sampleRate_) /
         (1000000.0f * static_cast<float>(kMidiClockPeriodScale)));
    slewClockPeriod(targetPeriodSamples);
    return;
  }

  if (clockSource_ == SyncClockSource::Midi && midiClockLocked_ &&
      hasMidiClockTimestamp_ && midiClockAgeUs > kMidiClockLossTimeoutUs) {
    resetMidiClockPeriodEstimate();
  }

  if (activeClockSource_ == SyncClockSource::Midi) {
    activeClockSource_ = SyncClockSource::Internal;
    returningToInternalClock_ = true;
  }

  if (returningToInternalClock_) {
    const float internalPeriodSamples = 60.0f * sampleRate_ * 2.0f /
                                        internalTempo_ / 8.0f /
                                        float(SlicesPerStep);
    slewClockPeriod(internalPeriodSamples);

    const float remainingErrorSamples =
        internalPeriodSamples - playSampleCount_;
    if (remainingErrorSamples >= -kClockPeriodSnapThresholdSamples &&
        remainingErrorSamples <= kClockPeriodSnapThresholdSamples) {
      applyClockPeriod(internalPeriodSamples);
      returningToInternalClock_ = false;
    }
  }
}

bool SyncMaster::IsMidiClockLocked() const { return midiClockLocked_; }

uint32_t SyncMaster::GetMidiClockPeriodUs() const {
  return (midiClockPeriodQ12_ + (kMidiClockPeriodScale / 2U)) /
         kMidiClockPeriodScale;
}

PICOTRACKER_FAST_CODE("sync_master_next_slice")
void SyncMaster::NextSlice() {
  currentSlice_ = (currentSlice_ + 1) % SlicesPerStep;
  if (currentSlice_ == 0) {
    beatCount_++;
  };
};

bool SyncMaster::MajorSlice() { return currentSlice_ == 0; };

PICOTRACKER_FAST_CODE("sync_master_table_slice")
bool SyncMaster::TableSlice() { return MidiSlice(); };

PICOTRACKER_FAST_CODE("sync_master_midi_slice")
bool SyncMaster::MidiSlice() {
  int midiTick = currentSlice_ % (SlicesPerStep / 6);
  return midiTick == 0;
};

// Returns the number of samples per play slice

PICOTRACKER_FAST_CODE("sync_master_get_play_sample_count")
float SyncMaster::GetPlaySampleCount() { return playSampleCount_; };

// Returns the number of sample per tick
float SyncMaster::GetTickSampleCount() { return tickSampleCount_; };

// Returns the number of samples per sequencer step
float SyncMaster::GetStepSampleCount() {
  return tickSampleCount_ * SlicesPerStep;
}

// xx samples per tick
// xx/driverRate seconds
// xx/driverRate*1000 msecs

float SyncMaster::GetTickTime() {
  return tickSampleCount_ / static_cast<float>(sampleRate_) * 1000.0f;
};

unsigned int SyncMaster::GetBeatCount() { return beatCount_; };
