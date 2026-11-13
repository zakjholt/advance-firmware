/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#include "Application/Seq/MidiEventList.h"

namespace picoSeq {

void MidiEventList::Clear() { events_.clear(); }

bool MidiEventList::Add(const SeqEvent &event) {
  if (events_.full()) {
    return false;
  }

  if (events_.empty() || event.tick_ >= events_.back().tick_) {
    events_.push_back(event);
    return true;
  }

  auto it = events_.begin();
  while (it != events_.end() && it->tick_ <= event.tick_) {
    ++it;
  }
  events_.insert(it, event);
  return true;
}

std::size_t MidiEventList::LowerBound(uint32_t fromTick) const {
  std::size_t lo = 0;
  std::size_t hi = events_.size();
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (events_[mid].tick_ < fromTick) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

} // namespace picoSeq
