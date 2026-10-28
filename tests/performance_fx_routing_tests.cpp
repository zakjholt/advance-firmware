/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 zakjholt
 *
 * This file is part of the picoTracker Advance firmware (zakjholt fork).
 */

#include "doctest/doctest.h"

#include "Application/Effects/OutputEffectConstants.h"
#include "Application/Effects/PerformanceFxConstants.h"
#include "Application/Model/Song.h"

TEST_CASE("PFX bus indices sit between stream and master output FX") {
  // Mirrors MixerService.h layout without pulling the full mixer service.
  constexpr int trackMixBusCount = SONG_CHANNEL_COUNT;
  constexpr int streamMixBus = trackMixBusCount;
  constexpr int performanceFxBusBase = streamMixBus + 1;
  constexpr int outputEffectBusBase =
      performanceFxBusBase + PERFORMANCE_FX_BUS_COUNT;
  constexpr int maxBusCount = outputEffectBusBase + OUTPUT_EFFECT_COUNT;

  CHECK(PERFORMANCE_FX_BUS_COUNT == 2);
  CHECK(performanceFxBusBase == streamMixBus + 1);
  CHECK(outputEffectBusBase == performanceFxBusBase + 2);
  CHECK(maxBusCount == SONG_CHANNEL_COUNT + 1 + 2 + OUTPUT_EFFECT_COUNT);
  // Master AudioMixer allows 16 modules; keep PFX inside that budget.
  CHECK(maxBusCount <= 16);
}
