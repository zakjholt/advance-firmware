/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#ifndef PICSEQ_SEQ_PART_STORE_H_
#define PICSEQ_SEQ_PART_STORE_H_

#include "Application/Seq/SeqPart.h"

namespace picoSeq {

// SD card save/load for a single Phase 0 part. Returns false if FS unavailable
// or I/O fails (host tests use SeqPartSerializer buffers instead).
class SeqPartStore {
public:
  static bool EnsureDir();
  static bool SavePart(const SeqPart &part, const char *path = kPartSdPath);
  static bool LoadPart(SeqPart &part, const char *path = kPartSdPath);
};

} // namespace picoSeq

#endif
