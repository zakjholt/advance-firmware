/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#include "Application/Seq/MidiRouter.h"
#include "Application/Seq/SeqEngine.h"

namespace picoSeq {

MidiRouter::MidiRouter()
    : sink_(nullptr), engine_(nullptr), thruEnabled_(true) {}

bool MidiRouter::OnIncoming(uint8_t status, uint8_t data1, uint8_t data2,
                            uint32_t tick) {
  const uint8_t type = static_cast<uint8_t>(status & 0xF0);
  const bool isRealtime = status >= 0xF8;
  const bool isChannelVoice =
      (type >= 0x80 && type <= 0xE0) || (status >= 0x80 && status < 0xF0);

  if (thruEnabled_ && sink_ != nullptr && !isRealtime) {
    MidiBytes out;
    out.status_ = status;
    out.data1_ = data1;
    out.data2_ = data2;
    out.hasData1_ = isChannelVoice;
    out.hasData2_ =
        (type == 0x80 || type == 0x90 || type == 0xA0 || type == 0xB0 ||
         type == 0xE0);
    if (type == 0xC0 || type == 0xD0) {
      out.hasData1_ = true;
      out.hasData2_ = false;
    }
    sink_->SendImmediate(out);
  }

  if (engine_ != nullptr && !isRealtime) {
    engine_->CaptureIncoming(status, data1, data2, tick);
  }

  // Phase 0: when Seq is driving MIDI, always consume so tracker Player is
  // bypassed.
  return engine_ != nullptr;
}

} // namespace picoSeq
