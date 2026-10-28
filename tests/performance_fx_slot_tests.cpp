/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 zakjholt
 *
 * This file is part of the picoTracker Advance firmware (zakjholt fork).
 */

#include "doctest/doctest.h"

#include "Application/Effects/PerformanceFxConstants.h"
#include "Application/Effects/PerformanceFxProcessors.h"
#include "Application/Effects/PerformanceFxSlot.h"
#include "Services/Audio/AudioDriver.h"
#include <cmath>

namespace {

constexpr int kFrames = 8;
constexpr int kCount = kFrames * 2;

} // namespace

TEST_CASE("PerformanceFxSlot bypasses DSP when not engaged") {
  AudioSample input[MAX_SAMPLE_COUNT * 2] = {};
  AudioSample output[MAX_SAMPLE_COUNT * 2] = {};
  PerformanceFxSlot slot(input);
  slot.SetEffectParameters(kPerformanceFxEffectGate, 0xFF, 0, 0);
  slot.SetEngaged(false);

  for (int i = 0; i < kCount; ++i) {
    input[i] = 0.5f;
  }

  CHECK(slot.Render(output, kFrames));
  for (int i = 0; i < kCount; ++i) {
    CHECK(output[i] == doctest::Approx(0.5f));
  }
}

TEST_CASE("PerformanceFxSlot gate ducks when engaged") {
  AudioSample input[MAX_SAMPLE_COUNT * 2] = {};
  AudioSample output[MAX_SAMPLE_COUNT * 2] = {};
  PerformanceFxSlot slot(input);
  slot.SetEffectParameters(kPerformanceFxEffectGate, 0xFF, 0, 0);
  slot.SetEngaged(true);

  for (int i = 0; i < kCount; ++i) {
    input[i] = 0.5f;
  }

  CHECK(slot.Render(output, kFrames));
  for (int i = 0; i < kCount; ++i) {
    CHECK(output[i] == doctest::Approx(0.0f));
  }
}

TEST_CASE("PerformanceFxSlot applies post-FX sends from processed output") {
  AudioSample input[MAX_SAMPLE_COUNT * 2] = {};
  AudioSample output[MAX_SAMPLE_COUNT * 2] = {};
  AudioSample send[MAX_SAMPLE_COUNT * 2] = {};
  PerformanceFxSlot slot(input);
  slot.SetEffectParameters(kPerformanceFxEffectNone, 0, 0, 0);
  slot.SetEngaged(false);
  CHECK(slot.AddPostSendRouting(send, 0.5f));

  for (int i = 0; i < kCount; ++i) {
    input[i] = 1.0f;
  }

  CHECK(slot.Render(output, kFrames));
  for (int i = 0; i < kCount; ++i) {
    CHECK(send[i] == doctest::Approx(0.5f));
  }
}

TEST_CASE("PerformanceFxSlot returns silence without input") {
  AudioSample input[MAX_SAMPLE_COUNT * 2] = {};
  AudioSample output[MAX_SAMPLE_COUNT * 2];
  for (int i = 0; i < kCount; ++i) {
    output[i] = 1.0f;
  }
  PerformanceFxSlot slot(input);

  CHECK_FALSE(slot.Render(output, kFrames));
  for (int i = 0; i < kCount; ++i) {
    CHECK(output[i] == doctest::Approx(0.0f));
  }
}

TEST_CASE("Filter processor attenuates highs when engaged with amount") {
  AudioSample buffer[kCount];
  for (int i = 0; i < kCount; ++i) {
    buffer[i] = (i & 1) ? 1.0f : -1.0f;
  }
  PerformanceFxBusState state;
  PerformanceFxProcess(kPerformanceFxEffectFilter, buffer, kFrames, 0xFF, 0x10,
                       0x00, state);
  CHECK(std::fabs(buffer[2]) < 1.0f);
}

TEST_CASE("PFX bus count is two") {
  CHECK(PERFORMANCE_FX_BUS_COUNT == 2);
}
