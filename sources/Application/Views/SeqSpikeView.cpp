/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#include "Application/Views/SeqSpikeView.h"
#include "Application/Seq/SeqMidiBridge.h"
#include "Application/Seq/SeqPartStore.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <nanoprintf.h>

namespace {

void SeqBootloaderCallback(View & /*v*/, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    System *sys = System::GetInstance();
    if (sys != nullptr) {
      // Same path as Device → Update firmware (advSystem::SystemBootloader →
      // platform_bootloader).
      sys->SystemBootloader();
    }
  }
}

} // namespace

SeqSpikeView::SeqSpikeView(GUIWindow &w, ViewData *viewData)
    : View(w, viewData) {
  viewType_ = VT_SEQ_SPIKE;
}

void SeqSpikeView::OnFocus() {
  isDirty_ = true;
#ifdef SEQ_MIDI_ONLY
  picoSeq::SeqMidiBridge::Get().Init();
#endif
}

void SeqSpikeView::OnPlayerUpdate(PlayerEventType, unsigned int) {}

void SeqSpikeView::requestBootloader() {
#ifdef SEQ_MIDI_ONLY
  auto &engine = picoSeq::SeqMidiBridge::Get().Engine();
  if (engine.Transport() != picoSeq::SeqTransport::Stopped) {
    MessageBox *mb =
        MessageBox::Create(*this, "Stop transport first", MBBF_OK);
    DoModal(mb);
    return;
  }

  MessageBox *mb = MessageBox::Create(*this, "Reboot to bootloader?",
                                      "Unsaved takes will be lost",
                                      MBBF_YES | MBBF_NO);
  DoModal(mb, ModalViewCallback::create<&SeqBootloaderCallback>());
#endif
}

void SeqSpikeView::ProcessButtonMask(unsigned short mask, bool pressed) {
  if (!pressed) {
    return;
  }

#ifdef SEQ_MIDI_ONLY
  auto &bridge = picoSeq::SeqMidiBridge::Get();
  auto &engine = bridge.Engine();

  // Safe triple-modifier: NAV+ALT+ENTER → confirm → SD bootloader UI.
  // Mirrors Device → Update firmware without colliding with save/load/rec.
  if ((mask & EPBM_NAV) && (mask & EPBM_ALT) && (mask & EPBM_ENTER)) {
    requestBootloader();
    return;
  }

  if ((mask & EPBM_PLAY) && (mask & EPBM_EDIT)) {
    engine.Record();
    isDirty_ = true;
    return;
  }

  if (mask & EPBM_PLAY) {
    if (engine.Transport() == picoSeq::SeqTransport::Stopped) {
      engine.Play();
    } else {
      engine.Stop();
    }
    isDirty_ = true;
    return;
  }

  if ((mask & EPBM_ENTER) && (mask & EPBM_ALT)) {
    if (picoSeq::SeqPartStore::LoadPart(engine.Part())) {
      Trace::Log("SEQ", "Part loaded");
    } else {
      Trace::Error("SEQ", "Part load failed");
    }
    isDirty_ = true;
    return;
  }

  if (mask & EPBM_ENTER) {
    if (picoSeq::SeqPartStore::SavePart(engine.Part())) {
      Trace::Log("SEQ", "Part saved");
    } else {
      Trace::Error("SEQ", "Part save failed");
    }
    isDirty_ = true;
    return;
  }

  if ((mask & EPBM_LEFT) || (mask & EPBM_RIGHT)) {
    int track = engine.ArmedTrack();
    if (track < 0) {
      track = 0;
    }
    if (mask & EPBM_LEFT) {
      track = (track + picoSeq::kTrackCount - 1) % picoSeq::kTrackCount;
    } else {
      track = (track + 1) % picoSeq::kTrackCount;
    }
    engine.ArmTrack(track);
    isDirty_ = true;
  }
#else
  (void)mask;
#endif
}

void SeqSpikeView::drawStatus() {
#ifdef SEQ_MIDI_ONLY
  auto &engine = picoSeq::SeqMidiBridge::Get().Engine();
  GUITextProperties props;
  SetColor(CD_NORMAL);

  DrawString(0, 0, "picoSeq Phase 0", props);

  const char *state = "STOP";
  switch (engine.Transport()) {
  case picoSeq::SeqTransport::Playing:
    state = "PLAY";
    break;
  case picoSeq::SeqTransport::Recording:
    state = "REC ";
    break;
  default:
    break;
  }

  char line[64];
  npf_snprintf(line, sizeof(line), "%s tick:%lu arm:%d thru:%s", state,
               static_cast<unsigned long>(engine.PlayTick()),
               engine.ArmedTrack(),
               engine.Router().ThruEnabled() ? "on" : "off");
  DrawString(0, 2, line, props);

  npf_snprintf(line, sizeof(line), "ev:%u cap:%u drop:%u tracks:%d",
               static_cast<unsigned>(engine.Part().TotalEventCount()),
               static_cast<unsigned>(engine.EventsCaptured()),
               static_cast<unsigned>(engine.EventsDropped()),
               picoSeq::kTrackCount);
  DrawString(0, 3, line, props);

  DrawString(0, 5, "PLAY play/stop  EDIT+PLAY rec", props);
  DrawString(0, 6, "ENTER save  ALT+ENTER load", props);
  DrawString(0, 7, "LEFT/RIGHT arm track 1-16", props);
  DrawString(0, 9, "NAV+ALT+ENTER bootloader (confirm)", props);
  DrawString(0, 10, "Hold POWER 3s to shut down", props);
#else
  GUITextProperties props;
  SetColor(CD_NORMAL);
  DrawString(0, 0, "Build with -DSEQ_MIDI_ONLY=ON", props);
#endif
}

void SeqSpikeView::DrawView() {
  Clear();
  drawStatus();
}

void SeqSpikeView::AnimationUpdate() {
#ifdef SEQ_MIDI_ONLY
  System *sys = System::GetInstance();
  if (sys != nullptr) {
    picoSeq::SeqMidiBridge::Get().OnServiceTick(sys->Micros());
  }
  if (picoSeq::SeqMidiBridge::Get().Engine().Transport() !=
      picoSeq::SeqTransport::Stopped) {
    isDirty_ = true;
  }
#endif
  if (isDirty_) {
    DrawView();
    isDirty_ = false;
  }

  // Same hold-to-shutdown path as ScreenView (stock Song/Device/etc.).
  // SeqSpikeView subclasses View directly, so it must call this each tick.
  GUITextProperties props;
  drawPowerButtonUI(props);
}
