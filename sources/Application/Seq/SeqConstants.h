/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 *
 * Phase 0 MMT-style sequencer constants for picoTracker Advance alt firmware.
 */

#ifndef PICSEQ_SEQ_CONSTANTS_H_
#define PICSEQ_SEQ_CONSTANTS_H_

#include <cstddef>
#include <cstdint>

namespace picoSeq {

// Locked product target: up to 16 MIDI channels / tracks.
constexpr int kTrackCount = 16;
constexpr int kMidiChannelCount = 16;

// Internal tick resolution (4x MIDI clock PPQN of 24).
constexpr uint32_t kPpqn = 96;
constexpr uint32_t kMidiClockPpqn = 24;
constexpr uint32_t kTicksPerMidiClock = kPpqn / kMidiClockPpqn;

// Phase 0 per-track capacity (16 tracks × 8 bytes ≈ 64KB in internal RAM).
// Phase 1 should move storage to external SDRAM and raise this.
constexpr std::size_t kMaxEventsPerTrack = 512;

// Default part length: 4 bars of 4/4 at 96 PPQN.
constexpr uint32_t kDefaultPartLengthTicks = kPpqn * 4 * 4;

// Binary part file magic / version.
constexpr char kPartMagic[4] = {'P', 'S', 'Q', '1'};
constexpr uint16_t kPartFormatVersion = 1;

constexpr const char *kPartSdPath = "/picoseq/part00.psq";

} // namespace picoSeq

#endif
