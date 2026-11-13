/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#ifndef PICSEQ_I_MIDI_SINK_H_
#define PICSEQ_I_MIDI_SINK_H_

#include <cstdint>

namespace picoSeq {

struct MidiBytes {
  uint8_t status_ = 0;
  uint8_t data1_ = 0;
  uint8_t data2_ = 0;
  bool hasData1_ = false;
  bool hasData2_ = false;
};

// Abstract MIDI output for host tests and device bridge.
class IMidiSink {
public:
  virtual ~IMidiSink() = default;
  virtual void SendImmediate(const MidiBytes &msg) = 0;
};

} // namespace picoSeq

#endif
