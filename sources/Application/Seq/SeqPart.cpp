/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#include "Application/Seq/SeqPart.h"

namespace picoSeq {

SeqPart::SeqPart() { Reset(); }

void SeqPart::Reset() {
  for (int i = 0; i < kTrackCount; ++i) {
    tracks_[i].Clear();
  }
  lengthTicks_ = kDefaultPartLengthTicks;
  loop_ = true;
}

SeqTrack &SeqPart::Track(int index) {
  if (index < 0 || index >= kTrackCount) {
    return tracks_[0];
  }
  return tracks_[index];
}

const SeqTrack &SeqPart::Track(int index) const {
  if (index < 0 || index >= kTrackCount) {
    return tracks_[0];
  }
  return tracks_[index];
}

void SeqPart::SetLengthTicks(uint32_t ticks) {
  lengthTicks_ = ticks == 0 ? kDefaultPartLengthTicks : ticks;
}

void SeqPart::ArmTrack(int index, bool armed) {
  if (index < 0 || index >= kTrackCount) {
    return;
  }
  tracks_[index].armed_ = armed;
}

void SeqPart::ArmOnly(int index) {
  ClearAllArms();
  ArmTrack(index, true);
}

void SeqPart::ClearAllArms() {
  for (int i = 0; i < kTrackCount; ++i) {
    tracks_[i].armed_ = false;
  }
}

std::size_t SeqPart::TotalEventCount() const {
  std::size_t total = 0;
  for (int i = 0; i < kTrackCount; ++i) {
    total += tracks_[i].events_.Size();
  }
  return total;
}

} // namespace picoSeq
