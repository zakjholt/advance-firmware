/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 zakjholt
 *
 * This file is part of the picoTracker Advance firmware (zakjholt fork).
 */

#ifndef _PERFORMANCE_EFFECTS_VIEW_H_
#define _PERFORMANCE_EFFECTS_VIEW_H_

#include "Application/Effects/PerformanceFxConstants.h"
#include "BaseClasses/UIIntVarField.h"
#include "FieldView.h"
#include "ViewData.h"

class PerformanceEffectsView : public FieldView {
public:
  PerformanceEffectsView(GUIWindow &w, ViewData *viewData);
  ~PerformanceEffectsView();

  void Reset();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);
  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
  virtual void OnFocus();
  virtual void AnimationUpdate();

private:
  static constexpr size_t FieldsPerBus = 4;

  void initFields();
  void fillBusFields(int bus, int columnX);
  void drawHeaders();
  void drawStatus();
  void togglePlay();
  int FocusedBusIndex();
  FourCC TypeFourCC(int bus) const;
  FourCC AmountFourCC(int bus) const;
  FourCC ParamAFourCC(int bus) const;
  FourCC ParamBFourCC(int bus) const;
  bool shouldDrawPlayTime() const override { return true; }

  etl::vector<UIIntVarField, PERFORMANCE_FX_BUS_COUNT * FieldsPerBus> fields_;
  int triggerBus_ = -1;
};

#endif
