/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#include "Application/Seq/SeqTickClock.h"

namespace picoSeq {

namespace {
constexpr uint32_t kMicrosPerMinute = 60000000U;
}

SeqTickClock::SeqTickClock() { Reset(); }

void SeqTickClock::Reset() {
  tick_ = 0;
  tempoBpm_ = 120;
  microsRemainder_ = 0;
}

void SeqTickClock::SetTempo(int bpm) {
  if (bpm < 40) {
    bpm = 40;
  }
  if (bpm > 300) {
    bpm = 300;
  }
  tempoBpm_ = bpm;
}

void SeqTickClock::Advance(uint32_t ticks) { tick_ += ticks; }

uint32_t SeqTickClock::MicrosPerTick() const {
  // 60e6 / (bpm * ppqn)
  return kMicrosPerMinute / (static_cast<uint32_t>(tempoBpm_) * kPpqn);
}

uint32_t SeqTickClock::AdvanceMicros(uint32_t elapsedUs) {
  const uint32_t perTick = MicrosPerTick();
  if (perTick == 0) {
    return 0;
  }

  uint64_t total =
      static_cast<uint64_t>(microsRemainder_) + static_cast<uint64_t>(elapsedUs);
  const uint32_t advanced = static_cast<uint32_t>(total / perTick);
  microsRemainder_ = static_cast<uint32_t>(total % perTick);
  tick_ += advanced;
  return advanced;
}

} // namespace picoSeq
