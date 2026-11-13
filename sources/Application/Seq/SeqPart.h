/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#ifndef PICSEQ_SEQ_PART_H_
#define PICSEQ_SEQ_PART_H_

#include "Application/Seq/MidiEventList.h"
#include "Application/Seq/SeqConstants.h"
#include <cstdint>

namespace picoSeq {

struct SeqTrack {
  MidiEventList events_;
  bool muted_ = false;
  bool armed_ = false;

  void Clear() {
    events_.Clear();
    muted_ = false;
    armed_ = false;
  }
};

class SeqPart {
public:
  SeqPart();

  void Reset();
  SeqTrack &Track(int index);
  const SeqTrack &Track(int index) const;

  uint32_t LengthTicks() const { return lengthTicks_; }
  void SetLengthTicks(uint32_t ticks);

  bool Loop() const { return loop_; }
  void SetLoop(bool loop) { loop_ = loop; }

  void ArmTrack(int index, bool armed);
  void ArmOnly(int index);
  void ClearAllArms();

  std::size_t TotalEventCount() const;

private:
  SeqTrack tracks_[kTrackCount];
  uint32_t lengthTicks_;
  bool loop_;
};

} // namespace picoSeq

#endif
