/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 zakjholt
 *
 * This file is part of the picoTracker Advance firmware (zakjholt fork).
 */

#ifndef _PERFORMANCE_FX_SLOT_H_
#define _PERFORMANCE_FX_SLOT_H_

#include "Application/Audio/AudioSample.h"
#include "Services/Audio/AudioDriver.h"
#include "Services/Audio/AudioModule.h"

class PerformanceFxSlot : public AudioModule {
public:
  explicit PerformanceFxSlot(AudioSample *inputBuffer);
  virtual ~PerformanceFxSlot() {}

  virtual bool Render(AudioSample *buffer, int samplecount) override;

  void ClearAccumulationBuffer();
  AudioSample *GetAccumulationBuffer();
  bool HasInput(int count) const;

private:
  AudioSample *const inputBuffer_;
};

#endif
