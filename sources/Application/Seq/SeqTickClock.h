/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#ifndef PICSEQ_SEQ_TICK_CLOCK_H_
#define PICSEQ_SEQ_TICK_CLOCK_H_

#include "Application/Seq/SeqConstants.h"
#include <cstdint>

namespace picoSeq {

class SeqTickClock {
public:
  SeqTickClock();

  void Reset();
  void SetTempo(int bpm);
  int Tempo() const { return tempoBpm_; }

  uint32_t Tick() const { return tick_; }
  void SetTick(uint32_t tick) { tick_ = tick; }

  // Advance by N seq ticks (96 PPQN).
  void Advance(uint32_t ticks = 1);

  // Host/device helper: advance based on elapsed microseconds at current tempo.
  // Returns number of seq ticks advanced.
  uint32_t AdvanceMicros(uint32_t elapsedUs);

  // Micros per seq tick at current tempo.
  uint32_t MicrosPerTick() const;

private:
  uint32_t tick_;
  int tempoBpm_;
  uint32_t microsRemainder_;
};

} // namespace picoSeq

#endif
