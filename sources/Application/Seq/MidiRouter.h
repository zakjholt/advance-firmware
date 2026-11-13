/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#ifndef PICSEQ_MIDI_ROUTER_H_
#define PICSEQ_MIDI_ROUTER_H_

#include "Application/Seq/IMidiSink.h"
#include "Application/Seq/SeqEvent.h"
#include <cstdint>

namespace picoSeq {

class SeqEngine;

// Soft-thru + record dispatch for incoming channel messages.
class MidiRouter {
public:
  MidiRouter();

  void SetSink(IMidiSink *sink) { sink_ = sink; }
  void SetEngine(SeqEngine *engine) { engine_ = engine; }

  bool ThruEnabled() const { return thruEnabled_; }
  void SetThruEnabled(bool enabled) { thruEnabled_ = enabled; }

  // Handle an incoming MIDI channel/realtime message at seq tick.
  // Returns true if the message was consumed (should not reach tracker Player).
  bool OnIncoming(uint8_t status, uint8_t data1, uint8_t data2, uint32_t tick);

private:
  IMidiSink *sink_;
  SeqEngine *engine_;
  bool thruEnabled_;
};

} // namespace picoSeq

#endif
