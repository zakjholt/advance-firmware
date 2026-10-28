/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 zakjholt
 *
 * This file is part of the picoTracker Advance firmware (zakjholt fork).
 */

#include "PerformanceEffectsView.h"
#include "Application/Effects/PerformanceFxDefinitions.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Player/Player.h"
#include "System/Console/n_assert.h"
#include "System/Console/nanoprintf.h"

namespace {

constexpr int32_t Bus0ColumnX = 0;
constexpr int32_t Bus1ColumnX = 14;
constexpr int32_t HeaderY = 2;
constexpr int32_t FieldBaseY = 3;
constexpr int32_t StatusY = 9;

int ColumnForBus(int bus) { return bus == 0 ? Bus0ColumnX : Bus1ColumnX; }

} // namespace

PerformanceEffectsView::PerformanceEffectsView(GUIWindow &w, ViewData *viewData)
    : FieldView(w, viewData) {
  initFields();
}

PerformanceEffectsView::~PerformanceEffectsView() {}

void PerformanceEffectsView::Reset() {
  ClearFocus();
  fieldList_.clear();
  fields_.clear();
  triggerBus_ = -1;
  initFields();
}

FourCC PerformanceEffectsView::TypeFourCC(int bus) const {
  return bus == 0 ? FourCC::VarPfx1EffectType : FourCC::VarPfx2EffectType;
}

FourCC PerformanceEffectsView::AmountFourCC(int bus) const {
  return bus == 0 ? FourCC::VarPfx1Amount : FourCC::VarPfx2Amount;
}

FourCC PerformanceEffectsView::ParamAFourCC(int bus) const {
  return bus == 0 ? FourCC::VarPfx1ParamA : FourCC::VarPfx2ParamA;
}

FourCC PerformanceEffectsView::ParamBFourCC(int bus) const {
  return bus == 0 ? FourCC::VarPfx1ParamB : FourCC::VarPfx2ParamB;
}

void PerformanceEffectsView::fillBusFields(int bus, int columnX) {
  Project *project = viewData_ ? viewData_->project_ : nullptr;
  if (!project) {
    return;
  }

  Variable *type = project->FindVariable(TypeFourCC(bus));
  Variable *amount = project->FindVariable(AmountFourCC(bus));
  Variable *paramA = project->FindVariable(ParamAFourCC(bus));
  Variable *paramB = project->FindVariable(ParamBFourCC(bus));
  NAssert(type && amount && paramA && paramB);
  if (!type || !amount || !paramA || !paramB) {
    return;
  }

  fields_.emplace_back(GUIPoint(columnX, FieldBaseY), *type, "Type: %s", 0,
                       kPerformanceFxEffectTypeCount - 1, 1, 1);
  fieldList_.push_back(&fields_.back());

  fields_.emplace_back(GUIPoint(columnX, FieldBaseY + 1), *amount,
                       "Amount: %2.2X", 0, PerformanceFxAmountMaximum, 1, 0x10);
  fieldList_.push_back(&fields_.back());

  fields_.emplace_back(GUIPoint(columnX, FieldBaseY + 2), *paramA, "P1: %2.2X",
                       0, PerformanceFxParamMaximum, 1, 0x10);
  fieldList_.push_back(&fields_.back());

  fields_.emplace_back(GUIPoint(columnX, FieldBaseY + 3), *paramB, "P2: %2.2X",
                       0, PerformanceFxParamMaximum, 1, 0x10);
  fieldList_.push_back(&fields_.back());
}

void PerformanceEffectsView::initFields() {
  fields_.clear();
  fieldList_.clear();
  fillBusFields(0, Bus0ColumnX);
  fillBusFields(1, Bus1ColumnX);
  if (!fields_.empty()) {
    SetFocus(&fields_.front());
  }
}

int PerformanceEffectsView::FocusedBusIndex() {
  UIField *focus = GetFocus();
  if (!focus || fields_.empty()) {
    return 0;
  }
  for (size_t i = 0; i < fields_.size(); ++i) {
    if (&fields_[i] == focus) {
      return static_cast<int>(i / FieldsPerBus);
    }
  }
  return 0;
}

void PerformanceEffectsView::ProcessButtonMask(unsigned short mask,
                                               bool pressed) {
  MixerService *mixer = MixerService::GetInstance();

  if (!pressed) {
    if (triggerBus_ >= 0 && (mask & EPBM_ENTER) == 0) {
      mixer->SetPerformanceFxTrigger(triggerBus_, false);
      triggerBus_ = -1;
      SetDirty(true);
    }
    return;
  }

  if ((mask & EPBM_EDIT) && (mask & EPBM_PLAY)) {
    if (!Player::GetInstance()->IsRunning()) {
      switchToRecordView();
    }
    return;
  }

  if (mask & EPBM_PLAY) {
    togglePlay();
    return;
  }

  if ((mask & EPBM_NAV) && (mask & EPBM_UP)) {
    if (triggerBus_ >= 0) {
      mixer->SetPerformanceFxTrigger(triggerBus_, false);
      triggerBus_ = -1;
    }
    ViewType vt = VT_MIXER;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
    return;
  }

  if (mask & EPBM_NAV) {
    return;
  }

  const int bus = FocusedBusIndex();

  if ((mask & EPBM_ALT) && (mask & EPBM_ENTER)) {
    mixer->TogglePerformanceFxLatch(bus);
    SetDirty(true);
    return;
  }

  if (mask == EPBM_ENTER) {
    mixer->SetPerformanceFxTrigger(bus, true);
    triggerBus_ = bus;
    SetDirty(true);
    return;
  }

  if (!fieldList_.empty()) {
    FieldView::ProcessButtonMask(mask, pressed);
  }
}

void PerformanceEffectsView::drawHeaders() {
  GUITextProperties props;
  SetColor(CD_HILITE2);
  DrawString(Bus0ColumnX, HeaderY, "PFX 1", props);
  DrawString(Bus1ColumnX, HeaderY, "PFX 2", props);
}

void PerformanceEffectsView::drawStatus() {
  MixerService *mixer = MixerService::GetInstance();
  GUITextProperties props;
  SetColor(CD_NORMAL);

  for (int bus = 0; bus < PERFORMANCE_FX_BUS_COUNT; ++bus) {
    char line[16];
    const char *state = "----";
    if (mixer->IsPerformanceFxLatched(bus)) {
      state = "LAT ";
    } else if (mixer->IsPerformanceFxEngaged(bus)) {
      state = "ON  ";
    }

    char tracks[12];
    int trackPos = 0;
    tracks[trackPos++] = '[';
    if (viewData_ && viewData_->project_) {
      for (int ch = 0; ch < SONG_CHANNEL_COUNT; ++ch) {
        if (viewData_->project_->GetChannelPfxBusIndex(ch) == bus &&
            trackPos < 10) {
          tracks[trackPos++] = static_cast<char>('1' + ch);
        }
      }
    }
    if (trackPos == 1) {
      tracks[trackPos++] = '-';
      tracks[trackPos++] = '-';
    }
    tracks[trackPos++] = ']';
    tracks[trackPos] = '\0';

    npf_snprintf(line, sizeof(line), "%s%s", state, tracks);
    DrawString(ColumnForBus(bus), StatusY, line, props);
  }
}

void PerformanceEffectsView::DrawView() {
  Clear();
  setTitle(etl::string<23>("Performance FX"));
  drawHeaders();
  FieldView::Redraw();
  drawStatus();
  drawMap();
}

void PerformanceEffectsView::OnPlayerUpdate(PlayerEventType eventType,
                                            unsigned int tick) {
  ScreenView::OnPlayerUpdate(eventType, tick);
}

void PerformanceEffectsView::OnFocus() {
  setTitle(etl::string<23>("Performance FX"));
  triggerBus_ = -1;
  if (!GetFocus() && !fieldList_.empty()) {
    SetFocus(*fieldList_.begin());
  }
}

void PerformanceEffectsView::AnimationUpdate() {
  ScreenView::AnimationUpdate();
  drawStatus();
  w_.Flush();
}

void PerformanceEffectsView::togglePlay() {
  if (!viewData_ || !viewData_->song_) {
    return;
  }
  Player::GetInstance()->OnSongStartButton(0, SONG_CHANNEL_COUNT - 1, false,
                                           false);
}
