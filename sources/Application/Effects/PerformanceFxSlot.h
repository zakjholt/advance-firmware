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
#include "Application/Effects/OutputEffectConstants.h"
#include "Application/Effects/PerformanceFxConstants.h"
#include "Application/Effects/PerformanceFxProcessors.h"
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

  void SetEngaged(bool engaged);
  void SetEffectParameters(PerformanceFxEffectType type, int amount, int paramA,
                           int paramB);
  void ClearPostSendRouting();
  bool AddPostSendRouting(AudioSample *buffer, AudioSample sendGain);

private:
  AudioSample *const inputBuffer_;
  PerformanceFxBusState state_;
  PerformanceFxEffectType type_ = kPerformanceFxEffectNone;
  int amount_ = 0;
  int paramA_ = 0;
  int paramB_ = 0;
  bool engaged_ = false;
  AudioSample *postSendBuffers_[OUTPUT_EFFECT_COUNT];
  AudioSample postSendGains_[OUTPUT_EFFECT_COUNT];
  uint8_t postSendCount_ = 0;
};

#endif
