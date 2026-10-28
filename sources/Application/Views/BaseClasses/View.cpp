/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "View.h"
#include "Adapters/adv/usb/usb_utils.h"
#include "Application/AppWindow.h"
#include "Application/Player/PlayButtonBehavior.h"
#include "Application/Player/Player.h"
#include "Application/Player/SyncMaster.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include "Application/Utils/mathutils.h"
#include "Application/Views/BaseClasses/PlaybackStatusIndicator.h"
#include "Application/Views/RecordView.h"
#include "Application/Views/SampleEditorView.h"
#include "Application/Views/VoiceScreenUtils.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include "ModalView.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <UIFramework/SimpleBaseClasses/EventManager.h>
#include <nanoprintf.h>

bool View::initPrivate_ = false;

int View::margin_ = 0;
int View::songRowCount_ = 16;

namespace {
BatteryState batteryDisplayState = {
    .percentage = 0,
    .voltage_mv = 0,
    .temperature_c = 0,
    .charging = false,
    .error = false,
};

uint32_t lastBatteryPollFrame = 0;
bool batteryDisplayInitialized = false;

bool currentInstrumentSupportsPreview(ViewData *viewData) {
  if (!viewData || !viewData->project_) {
    return false;
  }

  InstrumentBank *bank = viewData->project_->GetInstrumentBank();
  if (!bank) {
    return false;
  }

  I_Instrument *instrument =
      bank->GetInstrument(viewData->currentInstrumentID_);
  return SupportsInstrumentPreview(instrument);
}

} // namespace

View::View(GUIWindow &w, ViewData *viewData)
    : w_(w), viewData_(viewData), needsRedraw_(false), isVisible_(true),
      vuMeterCount_(0), viewMode_(VM_NORMAL), isDirty_(true),
      viewType_(VT_SONG), hasFocus_(false), playButtonPressed_(false),
      powerButtonPressed_(false), powerButtonHoldCount_(0),
      deferredPlayButtonActive_(false), deferredPlayButtonKillTriggered_(false),
      deferredPlayButtonMask_(0), deferredPlayButtonPressedAt_(0) {
  if (!initPrivate_) {
    View::margin_ = 0;
    songRowCount_ = 16;
    initPrivate_ = true;
  }
  mask_ = 0;
  locked_ = false;
  modalView_ = 0;
  modalViewCallback_ = ModalViewCallback();
  // Initialize VU meter tracking variables
  for (int i = 0; i < SONG_CHANNEL_COUNT + 1; i++) {
    prevLeftVU_[i] = 0;
    prevRightVU_[i] = 0;
  }
}
GUIPoint View::GetAnchor() {
  // Original code had a dynamic anchor point dending on song count, but
  // changing the song count didn't work anyway given that there are many places
  // where it was statically defined as 8. Other screens also don't fit with a
  // dynamic anchor point
  return GUIPoint(5, 3);
}

GUIPoint View::GetTitlePosition() { return GUIPoint(0, 0); };

void View::setTitle(const etl::istring &title) { title_ = title; }

void View::drawTitle() {
  GUIPoint pos = GetTitlePosition();
  GUITextProperties props;

  SetColor(CD_NORMAL);
  DrawString(pos._x, pos._y, title_.c_str(), props);
}

bool View::Lock() {
  if (locked_)
    return false;
  locked_ = true;
  return true;
};

void View::WaitForObject() {
  while (locked_) {
  };
}

void View::Unlock() { locked_ = false; }

void View::drawMap() {
  GUIPoint anchor = GetAnchor();

  int mapYOffset = View::songRowCount_;
  GUIPoint pos(View::margin_, anchor._y + mapYOffset);
  GUITextProperties props;
  I_Instrument *currentInstrument = nullptr;
  if (viewData_ && viewData_->project_) {
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    if (bank) {
      currentInstrument = bank->GetInstrument(viewData_->currentInstrumentID_);
    }
  }
  const bool showVoiceOnMap =
      ShouldShowAudioVoiceScreenOnMap(viewType_, currentInstrument);

  // draw entire map
  SetColor(CD_NORMAL);
  char buffer[6];
  props.invert_ = false;
  // row1
  strcpy(buffer, "D    ");
  DrawString(pos._x, pos._y, buffer, props);
  pos._y++;
  // row2
  strcpy(buffer, "P G  ");
  DrawString(pos._x, pos._y, buffer, props);
  pos._y++;
  // row3
  strcpy(buffer, showVoiceOnMap ? "SCPIV" : "SCPI ");
  DrawString(pos._x, pos._y, buffer, props);
  pos._y++;
  // row4
  strcpy(buffer, "M TT ");
  DrawString(pos._x, pos._y, buffer, props);
  pos._y++;
  // row5
  strcpy(buffer, "X    ");
  DrawString(pos._x, pos._y, buffer, props);

  // draw current screen on map
  SetColor(CD_HILITE2);
  pos._y = anchor._y + mapYOffset;
  switch (viewType_) {
  case VT_CHAIN:
    pos._x += 1;
    pos._y += 2;
    DrawString(pos._x, pos._y, "C", props);
    break;
  case VT_PHRASE:
    pos._x += 2;
    pos._y += 2;
    DrawString(pos._x, pos._y, "P", props);
    break;
  case VT_DEVICE:
    DrawString(pos._x, pos._y, "D", props);
    break;
  case VT_PROJECT:
    pos._y += 1;
    DrawString(pos._x, pos._y, "P", props);
    break;
  case VT_VOICE:
    if (showVoiceOnMap) {
      pos._x += 4;
      pos._y += 2;
      DrawString(pos._x, pos._y, "V", props);
    }
    break;
  case VT_INSTRUMENT:
    pos._x += 3;
    pos._y += 2;
    DrawString(pos._x, pos._y, "I", props);
    break;
  case VT_TABLE: // under phrase
    pos._x += 2;
    pos._y += 3;
    DrawString(pos._x, pos._y, "T", props);
    break;
  case VT_TABLE2: // under instrument
    pos._x += 3;
    pos._y += 3;
    DrawString(pos._x, pos._y, "T", props);
    break;
  case VT_GROOVE:
    pos._x += 2;
    pos._y += 1;
    DrawString(pos._x, pos._y, "G", props);
    break;
  case VT_MIXER:
    pos._y += 3;
    DrawString(pos._x, pos._y, "M", props);
    break;
  case VT_MASTER_EFFECTS:
    pos._y += 4;
    DrawString(pos._x, pos._y, "X", props);
    break;
  case VT_PERFORMANCE_EFFECTS:
    pos._y += 4;
    pos._x += 1;
    DrawString(pos._x, pos._y, "F", props);
    break;
  default: // VT_SONG
    pos._y += 2;
    DrawString(pos._x, pos._y, "S", props);
  }
}

void View::drawNotes() {
  GUIPoint anchor = GetAnchor();
  int initialX = View::margin_ + 5;
  int initialY = anchor._y + View::songRowCount_ + 2;
  GUIPoint pos(initialX, initialY);
  GUITextProperties props;

  Player *player = Player::GetInstance();
  const bool isSequencerPlayback =
      player->IsRunning() && viewData_->playMode_ != PM_AUDITION;

  props.invert_ = true;
  const int channelCount = SONG_CHANNEL_COUNT;
  for (int i = 0; i < channelCount; i++) {
    if (i == viewData_->songX_) {
      SetColor(CD_HILITE2);
    } else {
      SetColor(CD_HILITE1);
    }
    const bool showPlaybackVisualizer = ShowsChannelPlaybackVisualizer(
        isSequencerPlayback, viewData_->song_->IsMidiInputChannelAssigned(i),
        player->IsMidiInputChannelPlaying(static_cast<uint8_t>(i)));
    if (showPlaybackVisualizer) {
      uint8_t sliceIndex = 0;
      if (player->GetPlayedSliceIndex(i, sliceIndex)) {
        DrawString(pos._x, pos._y, "SL", props);
        pos._y++;
        char sliceBuffer[3];
        sliceBuffer[0] = static_cast<char>('0' + (sliceIndex / 10));
        sliceBuffer[1] = static_cast<char>('0' + (sliceIndex % 10));
        sliceBuffer[2] = '\0';
        DrawString(pos._x, pos._y, sliceBuffer, props);
        pos._y++;
        DrawString(pos._x, pos._y, player->GetPlayedInstrument(i),
                   props); // draw instrument number
      } else {
        DrawString(pos._x, pos._y, player->GetPlayedNote(i),
                   props); // row for the note values
        pos._y++;
        DrawString(pos._x, pos._y, player->GetPlayedOctive(i),
                   props); // row for the octive values
        pos._y++;
        DrawString(pos._x, pos._y, player->GetPlayedInstrument(i),
                   props); // draw instrument number
      }
    } else {
      DrawString(pos._x, pos._y, "  ", props); // row for the note
                                               // values
      pos._y++;
      DrawString(pos._x, pos._y, "  ",
                 props); // row for the octive values
      pos._y++;
      DrawString(pos._x, pos._y, "  ", props); // draw instrument number
    }
    pos._y = initialY;
    pos._x += 3;
  }
}

void View::drawMasterVuMeter(Player *player, GUITextProperties props,
                             bool forceRedraw, uint8_t xoffset,
                             uint8_t meterHeight) {
  stereosample playerLevel = player->GetMasterLevel();

  // Convert amplitude to bar levels
  int32_t leftBars, rightBars;
  amplitudeToBars(playerLevel, &leftBars, &rightBars);

  // we start at the bottom of the VU meter and draw it growing upwards
  GUIPoint pos = GetAnchor();
  pos._x += xoffset;
  pos._y += meterHeight - 1; // -1 to align with song grid

  // Use index 0 for the master VU meter
  drawVUMeter(leftBars, rightBars, pos, props, 0, forceRedraw, meterHeight);
}

void View::drawVUMeter(int32_t leftBars, int32_t rightBars, GUIPoint pos,
                       GUITextProperties props, int vuIndex, bool forceRedraw,
                       uint8_t meterHeight) {
  const int32_t localMeterMax = meterHeight * 10 - 1;
  const int32_t coloredRows = std::max<int32_t>(
      2, (7 * meterHeight + (VU_METER_HEIGHT / 2)) / VU_METER_HEIGHT);
  const int32_t warnLevel = meterHeight - coloredRows - 1;
  const int32_t clipLevel = meterHeight - 1;

  leftBars = (leftBars * localMeterMax) / VU_METER_MAX;
  rightBars = (rightBars * localMeterMax) / VU_METER_MAX;

  // Clamp the values to the maximum height
  leftBars = std::min<int32_t>(leftBars, localMeterMax);
  rightBars = std::min<int32_t>(rightBars, localMeterMax);

  // Add inertia effect by limiting the rate of change
  // Maximum step change allowed per update
  const int maxStepChange = 20;
  const int fallStepChange = 10;

  // For rising levels (current > previous), allow faster response
  if (leftBars > prevLeftVU_[vuIndex]) {
    // If the difference is greater than maxStepChange, limit it
    if (leftBars - prevLeftVU_[vuIndex] > maxStepChange) {
      leftBars = prevLeftVU_[vuIndex] + maxStepChange;
    }
  }
  // For falling levels (current < previous), add more inertia for a slower fall
  else if (leftBars < prevLeftVU_[vuIndex]) {
    // Use a smaller step for falling levels to create more inertia
    if (prevLeftVU_[vuIndex] - leftBars > fallStepChange) {
      leftBars = prevLeftVU_[vuIndex] - fallStepChange;
    }
  }

  // Same for right channel
  if (rightBars > prevRightVU_[vuIndex]) {
    if (rightBars - prevRightVU_[vuIndex] > maxStepChange) {
      rightBars = prevRightVU_[vuIndex] + maxStepChange;
    }
  } else if (rightBars < prevRightVU_[vuIndex]) {
    if (prevRightVU_[vuIndex] - rightBars > fallStepChange) {
      rightBars = prevRightVU_[vuIndex] - fallStepChange;
    }
  }

  // Left channel: Handle level changes
  bool leftChanged = (leftBars != prevLeftVU_[vuIndex]);
  bool rightChanged = (rightBars != prevRightVU_[vuIndex]);

  if (forceRedraw || leftChanged || rightChanged) {
    // If forcing redraw or level changed, redraw the entire meter

    // Then draw the active cells with inversion enabled
    props.invert_ = false;

    for (int i = 0; i < meterHeight; i++) {
      // Set appropriate color based on level
      if (i == clipLevel) {
        SetColor(CD_ERROR);
      } else if (i > warnLevel) {
        SetColor(CD_WARN);
      } else {
        SetColor(CD_INFO);
      }

      // draw left channel if changed
      if (leftChanged) {
        DrawString(pos._x, pos._y - i, char_bargraph_s(leftBars - 10 * i),
                   props);
      }

      // draw right channel if changed
      if (rightChanged) {
        DrawString(pos._x + 1, pos._y - i, char_bargraph_s(rightBars - 10 * i),
                   props);
      }
    }
  }

  // Store the current values for next time
  prevLeftVU_[vuIndex] = leftBars;
  prevRightVU_[vuIndex] = rightBars;
}

void View::DoModal(ModalView *view, ModalViewCallback cb) {
  modalView_ = view;
  modalView_->OnFocus();
  modalViewCallback_ = cb;
  isDirty_ = true;
};

void View::DismissModal() {
  if (modalView_ && modalView_->IsFinished()) {
    ModalView *finishedModal = modalView_;
    const uint32_t finishedModalId = finishedModal->GetInstanceId();
    ModalViewCallback callback = modalViewCallback_;

    // Clear current modal first so callback can safely open another modal.
    modalView_ = nullptr;
    modalViewCallback_ = ModalViewCallback();

    if (callback) {
      callback(*this, *finishedModal);
    }

    // Callback may have already replaced this instance in shared storage.
    if (finishedModal->GetInstanceId() == finishedModalId) {
      finishedModal->Destroy();
    }

    isDirty_ = true;
  }
};

void View::Redraw() {
  if (modalView_) {
    if (isDirty_) {
      DrawView();
    }
    modalView_->Redraw();
  } else {
    DrawView();
  }
  isDirty_ = false;
};

void View::SetDirty(bool isDirty) { isDirty_ = isDirty; };

void View::ProcessButton(unsigned short mask, unsigned short heldMask,
                         bool pressed) {
  const uint16_t eventMask = static_cast<uint16_t>(mask);
  const uint16_t physicalHeldMask = static_cast<uint16_t>(heldMask);
  const bool wasPlayPressed = playButtonPressed_;
  const bool isPlayPressed = (physicalHeldMask & EPBM_PLAY) != 0;
  const bool playReleasedEvent = !pressed && wasPlayPressed && !isPlayPressed;
  playButtonPressed_ = isPlayPressed;

  if (!pressed) {
    powerButtonPressed_ = false;
  } else if (physicalHeldMask & EPBM_POWER) {
    powerButtonPressed_ = pressed;
  }

  if (handleDeferredPlayButton(eventMask, physicalHeldMask, pressed,
                               playReleasedEvent)) {
    if (isDirty_) {
      ((AppWindow &)w_).SetDirty();
    }
    return;
  }

  // Normal button processing
  if (modalView_) {
    modalView_->ProcessButton(mask, heldMask, pressed);
    // checks if modal is done and if it is disposes of it:
    DismissModal();
  } else {
    ProcessButtonMask(mask, pressed);
  }

  if (isDirty_)
    ((AppWindow &)w_).SetDirty();
};

bool View::shouldDeferPlayButton(uint16_t mask) const {
  if (!ShouldDeferPlayButtonInput() || (mask & EPBM_PLAY) == 0 ||
      modalView_ != nullptr || (mask & EPBM_ENTER)) {
    return false;
  }

  Player *player = Player::GetInstance();
  if ((viewType_ == VT_INSTRUMENT || viewType_ == VT_VOICE) &&
      ShouldBypassPlayButtonDeferralForPreview(
          currentInstrumentSupportsPreview(viewData_), mask, EPBM_PLAY,
          EPBM_ALT)) {
    return false;
  }

  if (mask == static_cast<uint16_t>(EPBM_ALT | EPBM_PLAY)) {
    return true;
  }

  switch (viewType_) {
  case VT_SONG:
    if (mask == EPBM_PLAY || mask == (EPBM_NAV | EPBM_PLAY) ||
        mask == (EPBM_ALT | EPBM_PLAY)) {
      return true;
    }
    return mask == (EPBM_EDIT | EPBM_PLAY) && player != nullptr &&
           player->GetSequencerMode() == SM_LIVE;

  case VT_CHAIN:
  case VT_PHRASE:
  case VT_TABLE:
  case VT_TABLE2:
  case VT_GROOVE:
  case VT_INSTRUMENT:
  case VT_VOICE:
    return mask == EPBM_PLAY || mask == (EPBM_NAV | EPBM_PLAY);

  case VT_PROJECT:
  case VT_DEVICE:
  case VT_THEME:
  case VT_MIXER:
  case VT_MASTER_EFFECTS:
  case VT_PERFORMANCE_EFFECTS:
    return mask == EPBM_PLAY;

  default:
    return false;
  }
}

bool View::handleDeferredPlayButton(uint16_t eventMask, uint16_t heldMask,
                                    bool pressed, bool playReleasedEvent) {
  if (deferredPlayButtonActive_) {
    if (!playReleasedEvent) {
      if ((heldMask & EPBM_PLAY) == 0) {
        deferredPlayButtonActive_ = false;
        deferredPlayButtonKillTriggered_ = false;
        deferredPlayButtonMask_ = 0;
        deferredPlayButtonPressedAt_ = 0;
        return false;
      }

      if ((eventMask & EPBM_PLAY) != 0) {
        if (shouldDeferPlayButton(eventMask)) {
          if (deferredPlayButtonMask_ !=
                  static_cast<uint16_t>(EPBM_ALT | EPBM_PLAY) ||
              eventMask == static_cast<uint16_t>(EPBM_ALT | EPBM_PLAY)) {
            deferredPlayButtonMask_ = eventMask;
          }
        }
        return true;
      }

      if (pressed &&
          (eventMask &
           (EPBM_ALT | EPBM_EDIT | EPBM_ENTER | EPBM_NAV | EPBM_SELECT)) != 0 &&
          shouldDeferPlayButton(heldMask)) {
        deferredPlayButtonMask_ = heldMask;
        return true;
      }

      deferredPlayButtonActive_ = false;
      deferredPlayButtonKillTriggered_ = false;
      deferredPlayButtonMask_ = 0;
      deferredPlayButtonPressedAt_ = 0;
      return false;
    }

    const uint16_t deferredMask = deferredPlayButtonMask_;
    const bool killTriggered = deferredPlayButtonKillTriggered_;
    deferredPlayButtonActive_ = false;
    deferredPlayButtonKillTriggered_ = false;
    deferredPlayButtonMask_ = 0;
    deferredPlayButtonPressedAt_ = 0;

    if (!killTriggered) {
      Player *player = Player::GetInstance();
      const bool isAltPlay =
          (deferredMask == static_cast<uint16_t>(EPBM_ALT | EPBM_PLAY));
      // ALT+PLAY starts/stops the song from any screen while the sequencer
      // is in Song mode. On the song screen in Live mode it queues the
      // current row instead, so it needs to be handled by SongView itself.
      const bool startStopSong =
          isAltPlay &&
          (viewType_ != VT_SONG ||
           (player != nullptr && player->GetSequencerMode() == SM_SONG));
      if (startStopSong && player != nullptr && viewData_ != nullptr) {
        player->OnStartButton(PM_SONG, viewData_->songX_, false,
                              viewData_->songX_);
      } else {
        ProcessButtonMask(deferredMask, true);
      }
    }
    return true;
  }

  if (!pressed || !shouldDeferPlayButton(eventMask)) {
    return false;
  }

  System *system = System::GetInstance();
  deferredPlayButtonActive_ = true;
  deferredPlayButtonKillTriggered_ = false;
  deferredPlayButtonMask_ = eventMask;
  deferredPlayButtonPressedAt_ =
      (system != nullptr) ? static_cast<uint32_t>(system->GetClock()) : 0U;
  return true;
}

void View::Clear() { ((AppWindow &)w_).Clear(); }

void View::ForceClear() { ((AppWindow &)w_).Clear(true); }

void View::SetColor(ColorDefinition cd) { ((AppWindow &)w_).SetColor(cd); };

void View::ClearTextRect(int x, int y, int w, int h) {
  GUIRect rect(x, y, (x + w), (y + h));
  w_.ClearTextRect(rect);
};

void View::DrawString(int x, int y, const char *txt,
                      const GUITextProperties &props) {
  GUIPoint pos(x, y);
  w_.DrawString(txt, pos, props);
};

void View::DrawRect(GUIRect &r, ColorDefinition color) {
  w_.SetCurrentRectColor(AppWindow::GetColor(color));
  w_.DrawRect(r);
}

void View::drawBattery(GUITextProperties &props) {
  const uint32_t frameCounter = AppWindow::GetAnimationFrameCounter();
  constexpr uint32_t kBatteryPollIntervalFrames = PICO_CLOCK_HZ;

  if (!batteryDisplayInitialized ||
      (frameCounter - lastBatteryPollFrame) >= kBatteryPollIntervalFrames) {
    System::GetInstance()->GetBatteryState(batteryDisplayState);
    lastBatteryPollFrame = frameCounter;
    batteryDisplayInitialized = true;
  }

  GUIPoint battpos = GetAnchor();
  battpos._y = 0;

  uint8_t batteryPercent = batteryDisplayState.percentage;
  if (batteryPercent > 100) {
    batteryPercent = 100;
  }

  ColorDefinition batteryColor = CD_NORMAL;
  if (batteryPercent <= 5) {
    batteryColor = CD_ERROR;
  } else if (batteryPercent < 20) {
    batteryColor = CD_WARN;
  } else if (batteryDisplayState.charging) {
    batteryColor = CD_INFO;
  }

  SetColor(batteryColor);

  // Keep percentage branch compact: 3 digits + right battery cap.
  char batteryText[4];
  npf_snprintf(batteryText, sizeof(batteryText), "%3u", batteryPercent);

  GUITextProperties normalProps = props;
  normalProps.invert_ = false;

  GUITextProperties invertedProps = normalProps;
  invertedProps.invert_ = true;

  constexpr int kBatteryWidgetWidth = 4; // 3 text chars + right-side symbol
  int startX = SCREEN_WIDTH - kBatteryWidgetWidth;
  ClearTextRect(startX, battpos._y, kBatteryWidgetWidth, 1);

  DrawString(startX, battpos._y, batteryText, invertedProps);
  const char *rightSymbol = batteryDisplayState.charging
                                ? char_symbol_charging_s
                                : char_battery_right_s;
  DrawString(startX + 3, battpos._y, rightSymbol, normalProps);
}

void View::drawTitlebar(GUITextProperties &props) {
  drawTitle();
  drawStatusIndicators(props);
}

void View::drawStatusIndicators(GUITextProperties &props) {
  // Layout: columns 22-27 (6 chars for status indicators)
  //   22: playing indicator (>)
  //   23: audition/MIDI/sample/phrase/chain/tail indicator (A/M/S/P/C/T)
  //   24-26: effective BPM
  //   27: active clock source (I/M)
  constexpr int kStatusWidgetStartX = SCREEN_WIDTH - 10; // column 22
  GUIPoint statusPos(0, 0);

  GUITextProperties normalProps = props;
  normalProps.invert_ = false;

  // Status indicators at columns 22-23 (each draws space when inactive)
  Player *player = Player::GetInstance();
  const bool isRunning = player != nullptr && player->IsRunning();
  const bool isAuditioning = player != nullptr && player->IsAuditioning();
  const bool isMidiInputPlaying =
      player != nullptr && player->IsMidiInputPlaying();
  const bool isSamplePreviewing =
      player != nullptr && player->IsStreamingActive();
  const bool isTailPlaying =
      player != nullptr && !isSamplePreviewing && player->IsTailPlaying();
  const PlayMode playMode = viewData_ ? viewData_->playMode_ : PM_SONG;
  const PlaybackStatusIndicator playbackStatus = ResolvePlaybackStatusIndicator(
      isRunning, isAuditioning, isMidiInputPlaying, isSamplePreviewing,
      isTailPlaying, playMode);
  int indicatorX = kStatusWidgetStartX; // column 22

  // Playing indicator: ">" while any playback status is active.
  if (ShowsPlaybackIcon(playbackStatus)) {
    SetColor(CD_INFO);
    DrawString(indicatorX, statusPos._y, char_playback_play_s, normalProps);
  } else {
    DrawString(indicatorX, statusPos._y, " ", normalProps);
  }
  indicatorX++;

  // Audition takes precedence over MIDI input and sample preview, which take
  // precedence over sequencer mode. Tail is only active once those states have
  // stopped.
  const char *statusSuffix = GetPlaybackStatusSuffix(playbackStatus);
  if (statusSuffix[0] != ' ') {
    SetColor(CD_INFO);
  }
  DrawString(indicatorX, statusPos._y, statusSuffix, normalProps);
  indicatorX++;

  // Show the oscillator's effective tempo. MIDI clock and fallback timing is
  // averaged for the display so its small phase corrections do not flicker.
  char bpmText[4];
  SyncMaster *sync = SyncMaster::GetInstance();
  const int effectiveTempo = sync != nullptr ? sync->GetDisplayTempo() : 0;
  npf_snprintf(bpmText, sizeof(bpmText), "%3d", effectiveTempo);
  SetColor(CD_WARN);
  DrawString(indicatorX, statusPos._y, bpmText, normalProps);
  indicatorX += 3;

  const char *clockSourceText =
      sync != nullptr && sync->GetActiveClockSource() == SyncClockSource::Midi
          ? "M"
          : "I";
  DrawString(indicatorX, statusPos._y, clockSourceText, normalProps);

  // Battery at far right
  drawBattery(props);
}

void View::updateDeferredPlayButtonHold() {
  if (!deferredPlayButtonActive_ || deferredPlayButtonKillTriggered_) {
    return;
  }

  System *system = System::GetInstance();
  if (system == nullptr) {
    return;
  }

  const uint32_t now = static_cast<uint32_t>(system->GetClock());
  if (!HasPlayButtonKillHoldElapsed(deferredPlayButtonActive_,
                                    deferredPlayButtonPressedAt_, now)) {
    return;
  }

  Player *player = Player::GetInstance();
  if (player != nullptr) {
    player->Kill();
  }
  deferredPlayButtonKillTriggered_ = true;
}

// Draw power button UI overlay
void View::drawPowerButtonUI(GUITextProperties &props) {
  // Only process and draw UI when power button is pressed
  if (powerButtonPressed_) {
    char countdownMessage[SCREEN_WIDTH];
    powerButtonHoldCount_++;

    int remainingSeconds = 3 - (powerButtonHoldCount_ / PICO_CLOCK_HZ);
    if (remainingSeconds < 0) {
      remainingSeconds = 0;
    }

    snprintf(countdownMessage, sizeof(countdownMessage),
             "Hold for shutdown (%d sec)", remainingSeconds);

    if (remainingSeconds == 0) {
      Trace::Debug("Power button held for threshold time, Powerdown!");

      // goes via AppWindow so that the project gets auto saved before we
      // actually power off
      static_cast<AppWindow &>(w_).PowerDown();
    }

    // Calculate center position for the message
    GUIPoint pos = GetAnchor();
    uint16_t mesgLen = strlen(countdownMessage);
    pos._x = (SCREEN_WIDTH - mesgLen) / 2;
    pos._y = SCREEN_HEIGHT / 2 - 1;

    // Draw a background box
    SetColor(CD_BACKGROUND);
    for (int y = pos._y - 1; y <= pos._y + 1; y++) {
      for (int x = pos._x - 1; x <= (uint16_t)(pos._x + mesgLen + 1); x++) {
        GUITextProperties invProps = props;
        invProps.invert_ = true;
        DrawString(x, y, " ", invProps);
      }
    }

    // Draw the message
    SetColor(CD_EMPHASIS);
    DrawString(pos._x, pos._y, countdownMessage, props);
  } else if (powerButtonHoldCount_ > 0) {
    // Reset hold counter when button is released
    powerButtonHoldCount_ = 0;

    // Force immediate redraw by calling DrawView directly
    // This will redraw the entire screen with the correct content
    DrawView();

    Trace::Debug("Power button released! View redrawn.");
  }
}

void View::printHelpLegend(TrackerCommand command, GUITextProperties props) {
  if (command == TrackerCommand::InstrumentCommandNone) {
    return;
  }

  HelpLegendPair helpLegend = getHelpLegend(command);
  // clear top two lines (including title bar)
  DrawString(0, 0, "                           ", props);
  DrawString(0, 1, "                           ", props);
  DrawString(0, 0, helpLegend[0].c_str(), props);
  if (!helpLegend[1].empty()) {
    DrawString(0, 1, helpLegend[1].c_str(), props);
  }
}

void View::switchToRecordView() {
  // recording view only for the Advance
  if (!Player::GetInstance()->IsRunning()) {
    RecordView::SetSourceViewType(viewType_);
    SampleEditorView::SetSourceViewType(viewType_);
    ViewType vt = VT_RECORD;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
  }
}
