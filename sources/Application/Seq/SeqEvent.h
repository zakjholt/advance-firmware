/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#ifndef PICSEQ_SEQ_EVENT_H_
#define PICSEQ_SEQ_EVENT_H_

#include <cstdint>

namespace picoSeq {

struct SeqEvent {
  uint32_t tick_ = 0;
  uint8_t status_ = 0;
  uint8_t data1_ = 0;
  uint8_t data2_ = 0;

  uint8_t Channel() const { return static_cast<uint8_t>(status_ & 0x0F); }
  uint8_t TypeNibble() const { return static_cast<uint8_t>(status_ & 0xF0); }
};

} // namespace picoSeq

#endif
