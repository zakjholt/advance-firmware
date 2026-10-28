/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 zakjholt
 *
 * This file is part of the picoTracker Advance firmware (zakjholt fork).
 */

#ifndef _PERFORMANCE_FX_CONSTANTS_H_
#define _PERFORMANCE_FX_CONSTANTS_H_

#include <cstdint>

// Two buses for v1 hardware load testing before scaling.
static constexpr int PERFORMANCE_FX_BUS_COUNT = 2;

// Persisted track assignment: 0 = OFF, 1 = bus 0, 2 = bus 1, ...
static constexpr int kPerformanceFxAssignmentOff = 0;
static constexpr int kPerformanceFxAssignmentMaximum = PERFORMANCE_FX_BUS_COUNT;

enum PerformanceFxEffectType : int {
  kPerformanceFxEffectNone = 0,
  kPerformanceFxEffectFilter = 1,
  kPerformanceFxEffectDriveCrush = 2,
  kPerformanceFxEffectGate = 3,
};

static constexpr int kPerformanceFxEffectTypeCount =
    kPerformanceFxEffectGate + 1;
static constexpr int kDefaultPerformanceFxEffectType = kPerformanceFxEffectNone;
static constexpr int kDefaultPerformanceFxAmount = 0;
static constexpr int kDefaultPerformanceFxParam = 0;
static constexpr uint8_t PerformanceFxAmountMaximum = 0xFF;
static constexpr uint8_t PerformanceFxParamMaximum = 0xFF;

constexpr int ClampPerformanceFxBusIndex(int bus) {
  if (bus < 0) {
    return 0;
  }
  if (bus >= PERFORMANCE_FX_BUS_COUNT) {
    return PERFORMANCE_FX_BUS_COUNT - 1;
  }
  return bus;
}

constexpr int ClampPerformanceFxAssignment(int assignment) {
  if (assignment < kPerformanceFxAssignmentOff) {
    return kPerformanceFxAssignmentOff;
  }
  if (assignment > kPerformanceFxAssignmentMaximum) {
    return kPerformanceFxAssignmentMaximum;
  }
  return assignment;
}

constexpr int ClampPerformanceFxEffectType(int type) {
  if (type < kPerformanceFxEffectNone || type >= kPerformanceFxEffectTypeCount) {
    return kDefaultPerformanceFxEffectType;
  }
  return type;
}

// Convert persisted assignment (0=OFF, 1..=N) to zero-based bus index.
// Returns -1 when unassigned.
constexpr int PerformanceFxAssignmentToBusIndex(int assignment) {
  const int clamped = ClampPerformanceFxAssignment(assignment);
  if (clamped == kPerformanceFxAssignmentOff) {
    return -1;
  }
  return clamped - 1;
}

#endif
