/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#include "Application/Seq/SeqMidiBridge.h"
#include "Services/Midi/MidiInDevice.h"
#include "Services/Midi/MidiService.h"
#include <new>

namespace picoSeq {

MidiServiceSink::MidiServiceSink(MidiService *service) : service_(service) {}

void MidiServiceSink::SendImmediate(const MidiBytes &msg) {
  if (service_ == nullptr) {
    return;
  }
  MidiMessage out(msg.status_,
                  msg.hasData1_ ? msg.data1_ : MidiMessage::UNUSED_BYTE,
                  msg.hasData2_ ? msg.data2_ : MidiMessage::UNUSED_BYTE);
  service_->SendImmediate(out);
}

SeqMidiBridge &SeqMidiBridge::Get() {
  // Event lists for 16 tracks are too large for AXI SRAM; keep the bridge in
  // external RAM (see STM32H743XX_FLASH.ld .RAM_EXT).
  alignas(SeqMidiBridge) __attribute__((section(".RAM_EXT")))
  static unsigned char bridgeMem[sizeof(SeqMidiBridge)];
  static SeqMidiBridge *bridge = nullptr;
  if (bridge == nullptr) {
    bridge = new (bridgeMem) SeqMidiBridge();
  }
  return *bridge;
}

SeqMidiBridge::SeqMidiBridge()
    : service_(nullptr), lastTickUs_(0), hasLastTickUs_(false), active_(false),
      attached_(false) {
  engine_.SetSink(&sink_);
}

void SeqMidiBridge::Init() {
  active_ = true;
  hasLastTickUs_ = false;
  engine_.Stop();
  engine_.Part().Reset();
  engine_.Part().ArmOnly(0);
  engine_.Router().SetThruEnabled(true);
}

void SeqMidiBridge::AttachToService(MidiService &service) {
  service_ = &service;
  sink_.SetService(service_);
  if (attached_) {
    return;
  }
  attached_ = true;
}

void SeqMidiBridge::OnServiceTick(uint32_t nowUs) {
  if (!active_) {
    return;
  }
  if (!hasLastTickUs_) {
    lastTickUs_ = nowUs;
    hasLastTickUs_ = true;
    return;
  }
  const uint32_t elapsed = nowUs >= lastTickUs_ ? nowUs - lastTickUs_ : 0;
  lastTickUs_ = nowUs;
  if (elapsed > 0 && elapsed < 1000000U) {
    engine_.AdvanceMicros(elapsed);
  }
}

void SeqMidiBridge::Update(Observable &o, I_ObservableData *d) {
  (void)o;
  if (!active_ || d == nullptr) {
    return;
  }

  MidiMessage *message = static_cast<MidiMessage *>(d);
  if (message->status_ == MidiMessage::UNUSED_BYTE) {
    return;
  }

  if (message->status_ >= 0xF8) {
    return;
  }

  const uint8_t data1 =
      message->data1_ == MidiMessage::UNUSED_BYTE ? 0 : message->data1_;
  const uint8_t data2 =
      message->data2_ == MidiMessage::UNUSED_BYTE ? 0 : message->data2_;

  engine_.HandleIncoming(message->status_, data1, data2);
  message->Consume();
}

} // namespace picoSeq
