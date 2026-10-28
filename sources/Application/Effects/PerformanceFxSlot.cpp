/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 zakjholt
 *
 * This file is part of the picoTracker Advance firmware (zakjholt fork).
 */

#include "PerformanceFxSlot.h"
#include "System/Console/n_assert.h"
#include "config/MemorySections.h"
#include <string.h>

PerformanceFxSlot::PerformanceFxSlot(AudioSample *inputBuffer)
    : inputBuffer_(inputBuffer) {
  NAssert(inputBuffer_);
  ClearAccumulationBuffer();
  ClearPostSendRouting();
}

void PerformanceFxSlot::ClearAccumulationBuffer() {
  memset(inputBuffer_, 0, MAX_SAMPLE_COUNT * 2 * sizeof(AudioSample));
}

AudioSample *PerformanceFxSlot::GetAccumulationBuffer() { return inputBuffer_; }

void PerformanceFxSlot::SetEngaged(bool engaged) { engaged_ = engaged; }

void PerformanceFxSlot::SetEffectParameters(PerformanceFxEffectType type,
                                            int amount, int paramA,
                                            int paramB) {
  const PerformanceFxEffectType clamped =
      static_cast<PerformanceFxEffectType>(ClampPerformanceFxEffectType(type));
  if (clamped != type_) {
    state_.Reset();
    type_ = clamped;
  }
  amount_ = amount;
  paramA_ = paramA;
  paramB_ = paramB;
}

void PerformanceFxSlot::ClearPostSendRouting() { postSendCount_ = 0; }

bool PerformanceFxSlot::AddPostSendRouting(AudioSample *buffer,
                                           AudioSample sendGain) {
  if (buffer == nullptr || postSendCount_ >= OUTPUT_EFFECT_COUNT) {
    return false;
  }
  postSendBuffers_[postSendCount_] = buffer;
  postSendGains_[postSendCount_] = sendGain;
  ++postSendCount_;
  return true;
}

PICOTRACKER_FAST_CODE("performance_fx_slot_has_input")
bool PerformanceFxSlot::HasInput(int count) const {
  for (int i = 0; i < count; ++i) {
    if (inputBuffer_[i] != kAudioSampleZero) {
      return true;
    }
  }
  return false;
}

PICOTRACKER_FAST_CODE("performance_fx_slot_render")
bool PerformanceFxSlot::Render(AudioSample *buffer, int samplecount) {
  const int count = samplecount * 2;
  if (!HasInput(count)) {
    memset(buffer, 0, static_cast<size_t>(count) * sizeof(AudioSample));
    return false;
  }

  memcpy(buffer, inputBuffer_, static_cast<size_t>(count) * sizeof(AudioSample));

  if (engaged_ && type_ != kPerformanceFxEffectNone) {
    PerformanceFxProcess(type_, buffer, samplecount, amount_, paramA_, paramB_,
                         state_);
  }

  for (uint8_t send = 0; send < postSendCount_; ++send) {
    AudioSample *sendBuffer = postSendBuffers_[send];
    const AudioSample sendGain = postSendGains_[send];
    if (sendGain == kAudioSampleZero) {
      continue;
    }
    for (int i = 0; i < count; ++i) {
      sendBuffer[i] += buffer[i] * sendGain;
    }
  }

  return true;
}
