/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 zakjholt
 *
 * This file is part of the picoTracker Advance firmware (zakjholt fork).
 */

#include "PerformanceFxProcessors.h"
#include "config/MemorySections.h"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace {

constexpr int kAmountMax = 0xFF;

float AmountNorm(int amount) {
  return static_cast<float>(std::clamp(amount, 0, kAmountMax)) /
         static_cast<float>(kAmountMax);
}

AudioSample MixWetDry(AudioSample dry, AudioSample wet, float wetAmt) {
  return dry + (wet - dry) * wetAmt;
}

} // namespace

PICOTRACKER_FAST_CODE("performance_fx_filter")
void PerformanceFxProcessFilter(AudioSample *buffer, int samplecount,
                                int amount, int cutoff, int resonance,
                                PerformanceFxFilterState &state) {
  const float wetAmt = AmountNorm(amount);
  if (wetAmt <= 0.0f || samplecount <= 0) {
    return;
  }

  // Soft state-variable low-pass; cutoff/resonance are 00..FF.
  const float f = 0.002f + AmountNorm(cutoff) * AmountNorm(cutoff) * 0.55f;
  const float q = 1.0f - AmountNorm(resonance) * 0.92f;

  AudioSample lowL = state.lowL_;
  AudioSample lowR = state.lowR_;
  AudioSample bandL = state.bandL_;
  AudioSample bandR = state.bandR_;

  for (int frame = 0; frame < samplecount; ++frame) {
    const int i = frame * 2;
    const AudioSample inL = buffer[i];
    const AudioSample inR = buffer[i + 1];

    lowL += f * bandL;
    bandL += f * (inL - lowL - q * bandL);
    lowR += f * bandR;
    bandR += f * (inR - lowR - q * bandR);

    buffer[i] = MixWetDry(inL, lowL, wetAmt);
    buffer[i + 1] = MixWetDry(inR, lowR, wetAmt);
  }

  state.lowL_ = lowL;
  state.lowR_ = lowR;
  state.bandL_ = bandL;
  state.bandR_ = bandR;
}

namespace {

constexpr int kControlStepMax = 127;
constexpr int kControlStepCount = 128;
constexpr int kControlBlockSamples = 32;
constexpr float kControlSmoothing = 0.03f;
constexpr float kMinDrive = 0.15f;
constexpr float kMaxDrive = 1.0f;
constexpr int kStageCount = 3;
constexpr int kMaxBitReduction = 15;

float DriveControlForAmount(int amount) {
  const int control =
      ((amount * kControlStepMax) + (kAmountMax / 2)) / kAmountMax;
  return static_cast<float>(control) / static_cast<float>(kControlStepCount);
}

float DriveForControl(float control) {
  return kMinDrive + ((kMaxDrive - kMinDrive) * control * control);
}

AudioSample ShapeSample(AudioSample sample, float drive) {
  const AudioSample driven = sample * drive;
  const bool negative = driven < 0.0f;
  AudioSample shaped = negative ? -driven : driven;
  shaped = shaped > kAudioSampleMax ? kAudioSampleMax : shaped;
  for (int i = 0; i < kStageCount; ++i) {
    shaped *= 2.0f - shaped;
  }
  shaped = shaped > kAudioSampleMax ? kAudioSampleMax : shaped;
  return negative ? -shaped : shaped;
}

int CurveCrushAmount(int amount) {
  const int inverse = kAmountMax - amount;
  return kAmountMax - ((inverse * inverse + (kAmountMax / 2)) / kAmountMax);
}

AudioSample QuantizeSample(AudioSample sample, int32_t step) {
  constexpr int32_t kMinPcm = std::numeric_limits<int16_t>::min();
  constexpr int32_t kMaxPcm = std::numeric_limits<int16_t>::max();
  const AudioSample scaled = sample * kAudioPcmScale;
  int32_t pcm = static_cast<int32_t>(scaled >= 0.0f ? scaled + 0.5f
                                                    : scaled - 0.5f);
  pcm = std::clamp(pcm, kMinPcm, kMaxPcm);
  if (pcm == 0 || step <= 1) {
    return static_cast<AudioSample>(pcm) / kAudioPcmScale;
  }
  const int32_t half = step >> 1;
  const bool negative = pcm < 0;
  const int32_t magnitude = std::min<int32_t>(negative ? -pcm : pcm, kMaxPcm);
  const int32_t quantized =
      ((magnitude / step) * step) + half;
  const int32_t signedQ = negative ? -quantized : quantized;
  return static_cast<AudioSample>(std::clamp(signedQ, kMinPcm, kMaxPcm)) /
         kAudioPcmScale;
}

} // namespace

PICOTRACKER_FAST_CODE("performance_fx_drive_crush")
void PerformanceFxProcessDriveCrush(AudioSample *buffer, int samplecount,
                                    int amount, int crush, int /*unused*/,
                                    PerformanceFxDriveState &state) {
  const int driveAmount = std::clamp(amount, 0, kAmountMax);
  const int crushAmount = std::clamp(crush, 0, kAmountMax);
  if ((driveAmount == 0 && crushAmount == 0) || samplecount <= 0) {
    if (driveAmount == 0) {
      state.Reset();
    }
    return;
  }

  if (driveAmount > 0) {
    const float targetControl = DriveControlForAmount(driveAmount);
    if (!state.initialized_) {
      state.control_ = targetControl;
      state.driveControl_ = DriveForControl(state.control_);
      state.initialized_ = true;
    }

    int frameOffset = 0;
    while (frameOffset < samplecount) {
      const int block = std::min(samplecount - frameOffset, kControlBlockSamples);
      state.control_ +=
          (targetControl - state.control_) * kControlSmoothing;
      const float target = DriveForControl(state.control_);
      const float step =
          (target - state.driveControl_) /
          static_cast<float>(kControlBlockSamples);
      float drive = state.driveControl_;
      for (int frame = 0; frame < block; ++frame) {
        const int i = (frameOffset + frame) * 2;
        buffer[i] = ShapeSample(buffer[i], drive);
        buffer[i + 1] = ShapeSample(buffer[i + 1], drive);
        drive += step;
      }
      state.driveControl_ =
          block == kControlBlockSamples ? target : drive;
      frameOffset += block;
    }
  }

  if (crushAmount > 0) {
    const int shift =
        (CurveCrushAmount(crushAmount) * kMaxBitReduction + (kAmountMax / 2)) /
        kAmountMax;
    const int32_t step = 1 << shift;
    const int count = samplecount * 2;
    for (int i = 0; i < count; ++i) {
      buffer[i] = QuantizeSample(buffer[i], step);
    }
  }
}

PICOTRACKER_FAST_CODE("performance_fx_gate")
void PerformanceFxProcessGate(AudioSample *buffer, int samplecount, int amount,
                              int /*unusedA*/, int /*unusedB*/) {
  // Engaged duck depth: 00 = unchanged, FF = mute.
  const float open = 1.0f - AmountNorm(amount);
  if (open >= 1.0f || samplecount <= 0) {
    return;
  }
  const int count = samplecount * 2;
  for (int i = 0; i < count; ++i) {
    buffer[i] *= open;
  }
}

PICOTRACKER_FAST_CODE("performance_fx_process")
void PerformanceFxProcess(PerformanceFxEffectType type, AudioSample *buffer,
                          int samplecount, int amount, int paramA, int paramB,
                          PerformanceFxBusState &state) {
  switch (type) {
  case kPerformanceFxEffectFilter:
    PerformanceFxProcessFilter(buffer, samplecount, amount, paramA, paramB,
                               state.filter_);
    break;
  case kPerformanceFxEffectDriveCrush:
    PerformanceFxProcessDriveCrush(buffer, samplecount, amount, paramA, paramB,
                                   state.drive_);
    break;
  case kPerformanceFxEffectGate:
    PerformanceFxProcessGate(buffer, samplecount, amount, paramA, paramB);
    break;
  case kPerformanceFxEffectNone:
  default:
    break;
  }
}
