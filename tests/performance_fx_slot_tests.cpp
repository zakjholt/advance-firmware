/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 zakjholt
 *
 * This file is part of the picoTracker Advance firmware (zakjholt fork).
 */

#include "doctest/doctest.h"

#include "Application/Effects/PerformanceFxConstants.h"
#include "Application/Effects/PerformanceFxSlot.h"
#include "Services/Audio/AudioDriver.h"

namespace {

constexpr int kFrames = 8;
constexpr int kCount = kFrames * 2;

} // namespace

TEST_CASE("PerformanceFxSlot copies accumulated input") {
  AudioSample input[MAX_SAMPLE_COUNT * 2] = {};
  AudioSample output[MAX_SAMPLE_COUNT * 2] = {};
  PerformanceFxSlot slot(input);

  for (int i = 0; i < kCount; ++i) {
    input[i] = 0.25f * static_cast<AudioSample>(i + 1);
  }

  CHECK(slot.Render(output, kFrames));
  for (int i = 0; i < kCount; ++i) {
    CHECK(output[i] == doctest::Approx(0.25f * static_cast<AudioSample>(i + 1)));
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

TEST_CASE("PerformanceFxSlot clear resets accumulation") {
  AudioSample input[MAX_SAMPLE_COUNT * 2] = {};
  PerformanceFxSlot slot(input);
  input[0] = 0.5f;
  CHECK(slot.HasInput(kCount));
  slot.ClearAccumulationBuffer();
  CHECK_FALSE(slot.HasInput(kCount));
}

TEST_CASE("PFX bus count is two") {
  CHECK(PERFORMANCE_FX_BUS_COUNT == 2);
}
