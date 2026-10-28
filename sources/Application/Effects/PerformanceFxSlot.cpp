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
}

void PerformanceFxSlot::ClearAccumulationBuffer() {
  memset(inputBuffer_, 0, MAX_SAMPLE_COUNT * 2 * sizeof(AudioSample));
}

AudioSample *PerformanceFxSlot::GetAccumulationBuffer() { return inputBuffer_; }

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

  // Phase 2 passthrough: group sum goes straight to the master bus. Phase 3
  // replaces this with the selected performance effect processor.
  memcpy(buffer, inputBuffer_, static_cast<size_t>(count) * sizeof(AudioSample));
  return true;
}
