/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 zakjholt
 *
 * This file is part of the picoTracker Advance firmware (zakjholt fork).
 */

#include "doctest/doctest.h"

#include "Application/Effects/PerformanceFxConstants.h"
#include "Application/Model/Project.h"
#include "Foundation/Types/Types.h"

TEST_CASE("Project defaults all tracks to PFX OFF") {
  Project project("pfx-defaults");

  for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    CHECK(project.GetChannelPfxAssignment(channel) ==
          kPerformanceFxAssignmentOff);
    CHECK(project.GetChannelPfxBusIndex(channel) == -1);
  }
}

TEST_CASE("Project defaults both PFX buses to none with zero macros") {
  Project project("pfx-bus-defaults");

  CHECK(PERFORMANCE_FX_BUS_COUNT == 2);
  for (int bus = 0; bus < PERFORMANCE_FX_BUS_COUNT; ++bus) {
    CHECK(project.GetPfxEffectType(bus) == kPerformanceFxEffectNone);
    CHECK(project.GetPfxAmount(bus) == kDefaultPerformanceFxAmount);
    CHECK(project.GetPfxParamA(bus) == kDefaultPerformanceFxParam);
    CHECK(project.GetPfxParamB(bus) == kDefaultPerformanceFxParam);
  }
}

TEST_CASE("PFX assignment variables are registered for project save/load") {
  Project project("pfx-registered");

  CHECK(project.FindVariable(FourCC::VarChannel1PfxBus) != nullptr);
  CHECK(project.FindVariable(FourCC::VarChannel8PfxBus) != nullptr);
  CHECK(project.FindVariable(FourCC::VarPfx1EffectType) != nullptr);
  CHECK(project.FindVariable(FourCC::VarPfx1Amount) != nullptr);
  CHECK(project.FindVariable(FourCC::VarPfx1ParamA) != nullptr);
  CHECK(project.FindVariable(FourCC::VarPfx1ParamB) != nullptr);
  CHECK(project.FindVariable(FourCC::VarPfx2EffectType) != nullptr);
  CHECK(project.FindVariable(FourCC::VarPfx2Amount) != nullptr);
  CHECK(project.FindVariable(FourCC::VarPfx2ParamA) != nullptr);
  CHECK(project.FindVariable(FourCC::VarPfx2ParamB) != nullptr);
}

TEST_CASE("PFX assignment changes mark mix routing dirty") {
  Project project("pfx-dirty");
  Variable *assignment = project.FindVariable(FourCC::VarChannel3PfxBus);
  REQUIRE(assignment != nullptr);
  project.ClearMixRoutingDirty();

  assignment->SetInt(1, true);

  CHECK(project.GetChannelPfxAssignment(2) == 1);
  CHECK(project.GetChannelPfxBusIndex(2) == 0);
  CHECK(project.IsMixRoutingDirty());
}

TEST_CASE("PFX bus parameter changes mark mix routing dirty") {
  Project project("pfx-params-dirty");
  Variable *type = project.FindVariable(FourCC::VarPfx1EffectType);
  Variable *amount = project.FindVariable(FourCC::VarPfx2Amount);
  REQUIRE(type != nullptr);
  REQUIRE(amount != nullptr);
  project.ClearMixRoutingDirty();

  type->SetInt(kPerformanceFxEffectFilter, true);
  CHECK(project.GetPfxEffectType(0) == kPerformanceFxEffectFilter);
  CHECK(project.IsMixRoutingDirty());

  project.ClearMixRoutingDirty();
  amount->SetInt(PerformanceFxAmountMaximum, true);
  CHECK(project.GetPfxAmount(1) == PerformanceFxAmountMaximum);
  CHECK(project.IsMixRoutingDirty());
}

TEST_CASE("Loading a project resets PFX assignments and bus params") {
  Project project("pfx-reset");
  Variable *assignment = project.FindVariable(FourCC::VarChannel1PfxBus);
  Variable *type = project.FindVariable(FourCC::VarPfx2EffectType);
  Variable *amount = project.FindVariable(FourCC::VarPfx2Amount);
  REQUIRE(assignment != nullptr);
  REQUIRE(type != nullptr);
  REQUIRE(amount != nullptr);

  assignment->SetInt(2, true);
  type->SetInt(kPerformanceFxEffectGate, true);
  amount->SetInt(0x40, true);

  project.Load("pfx-reset");

  CHECK(project.GetChannelPfxAssignment(0) == kPerformanceFxAssignmentOff);
  CHECK(project.GetChannelPfxBusIndex(0) == -1);
  CHECK(project.GetPfxEffectType(1) == kPerformanceFxEffectNone);
  CHECK(project.GetPfxAmount(1) == kDefaultPerformanceFxAmount);
  CHECK(project.IsMixRoutingDirty());
}

TEST_CASE("PFX assignment helpers map OFF and bus indices") {
  CHECK(PerformanceFxAssignmentToBusIndex(0) == -1);
  CHECK(PerformanceFxAssignmentToBusIndex(1) == 0);
  CHECK(PerformanceFxAssignmentToBusIndex(2) == 1);
  CHECK(ClampPerformanceFxAssignment(99) == kPerformanceFxAssignmentMaximum);
  CHECK(ClampPerformanceFxEffectType(-1) == kPerformanceFxEffectNone);
  CHECK(ClampPerformanceFxEffectType(kPerformanceFxEffectDriveCrush) ==
        kPerformanceFxEffectDriveCrush);
}
