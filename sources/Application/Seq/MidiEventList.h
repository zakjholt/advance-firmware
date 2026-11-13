/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#ifndef PICSEQ_MIDI_EVENT_LIST_H_
#define PICSEQ_MIDI_EVENT_LIST_H_

#include "Application/Seq/SeqConstants.h"
#include "Application/Seq/SeqEvent.h"
#include "Externals/etl/include/etl/vector.h"
#include <cstddef>
#include <cstdint>

namespace picoSeq {

class MidiEventList {
public:
  using Storage = etl::vector<SeqEvent, kMaxEventsPerTrack>;

  void Clear();
  bool Empty() const { return events_.empty(); }
  std::size_t Size() const { return events_.size(); }
  bool Full() const { return events_.full(); }

  // Append in tick order. If tick < last tick, inserts sorted (O(n)).
  bool Add(const SeqEvent &event);

  const SeqEvent &At(std::size_t index) const { return events_[index]; }
  SeqEvent &At(std::size_t index) { return events_[index]; }

  // First index with tick >= fromTick, or Size() if none.
  std::size_t LowerBound(uint32_t fromTick) const;

  // Emit all events with tick in [fromTick, toTick).
  template <typename Fn>
  void ForEachInRange(uint32_t fromTick, uint32_t toTick, Fn &&fn) const {
    for (std::size_t i = LowerBound(fromTick); i < events_.size(); ++i) {
      const SeqEvent &e = events_[i];
      if (e.tick_ >= toTick) {
        break;
      }
      fn(e);
    }
  }

  const Storage &Events() const { return events_; }
  Storage &Events() { return events_; }

private:
  Storage events_;
};

} // namespace picoSeq

#endif
