/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#ifndef _SAMPLE_IMPORT_UTILS_H_
#define _SAMPLE_IMPORT_UTILS_H_

#include <cstdint>

// LGPT and AKWF single-cycle waveform file sizes.
constexpr bool IsSingleCycleWaveformFile(uint64_t fileSize) {
  return fileSize == 1376 || fileSize == 1344 || fileSize == 300;
}

#endif
