/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#ifndef PICSEQ_SEQ_PART_SERIALIZER_H_
#define PICSEQ_SEQ_PART_SERIALIZER_H_

#include "Application/Seq/SeqPart.h"
#include <cstddef>
#include <cstdint>

namespace picoSeq {

// Compact little-endian binary part format for Phase 0.
// Layout:
//   magic[4] "PSQ1"
//   version u16
//   lengthTicks u32
//   loop u8
//   trackCount u8 (=16)
//   for each track:
//     eventCount u32
//     events: tick u32, status u8, d1 u8, d2 u8, pad u8
class SeqPartSerializer {
public:
  static std::size_t MaxEncodedSize();
  static std::size_t EncodedSize(const SeqPart &part);

  // Returns bytes written, or 0 on failure.
  static std::size_t SaveToBuffer(const SeqPart &part, uint8_t *buffer,
                                  std::size_t capacity);

  // Returns true on success.
  static bool LoadFromBuffer(SeqPart &part, const uint8_t *buffer,
                             std::size_t size);
};

} // namespace picoSeq

#endif
