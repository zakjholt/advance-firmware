/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#ifndef PICSEQ_SEQ_SPIKE_VIEW_H_
#define PICSEQ_SEQ_SPIKE_VIEW_H_

#include "BaseClasses/View.h"
#include "ViewData.h"

// Phase 0 UI: replaces Song view under SEQ_MIDI_ONLY.
// PLAY = play/stop, EDIT+PLAY = record, ENTER = save part, ALT+ENTER = load.
// NAV+ALT+ENTER = confirm → reboot into SD firmware bootloader.
// Hold POWER ~3s = stock shutdown (drawPowerButtonUI → AppWindow::PowerDown).
class SeqSpikeView : public View {
public:
  SeqSpikeView(GUIWindow &w, ViewData *viewData);
  ~SeqSpikeView() override = default;

  void ProcessButtonMask(unsigned short mask, bool pressed) override;
  void DrawView() override;
  void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0) override;
  void OnFocus() override;
  void AnimationUpdate() override;

private:
  void drawStatus();
  void requestBootloader();
};
#endif
