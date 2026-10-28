/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 zakjholt
 *
 * This file is part of the picoTracker Advance firmware (zakjholt fork).
 */

#ifndef _PERFORMANCE_FX_PROCESSORS_H_
#define _PERFORMANCE_FX_PROCESSORS_H_

#include "Application/Audio/AudioSample.h"
#include "PerformanceFxConstants.h"

struct PerformanceFxFilterState {
  AudioSample lowL_ = 0;
  AudioSample lowR_ = 0;
  AudioSample bandL_ = 0;
  AudioSample bandR_ = 0;

  void Reset() {
    lowL_ = lowR_ = 0;
    bandL_ = bandR_ = 0;
  }
};

struct PerformanceFxDriveState {
  float control_ = 0.0f;
  float driveControl_ = 0.0f;
  bool initialized_ = false;

  void Reset() {
    control_ = 0.0f;
    driveControl_ = 0.0f;
    initialized_ = false;
  }
};

struct PerformanceFxBusState {
  PerformanceFxFilterState filter_;
  PerformanceFxDriveState drive_;

  void Reset() {
    filter_.Reset();
    drive_.Reset();
  }
};

// Process stereo-interleaved buffers in place. Amount and params are 0..255.
void PerformanceFxProcessFilter(AudioSample *buffer, int samplecount,
                                int amount, int cutoff, int resonance,
                                PerformanceFxFilterState &state);

void PerformanceFxProcessDriveCrush(AudioSample *buffer, int samplecount,
                                    int amount, int crush, int,
                                    PerformanceFxDriveState &state);

void PerformanceFxProcessGate(AudioSample *buffer, int samplecount, int amount,
                              int, int);

void PerformanceFxProcess(PerformanceFxEffectType type, AudioSample *buffer,
                          int samplecount, int amount, int paramA, int paramB,
                          PerformanceFxBusState &state);

#endif
