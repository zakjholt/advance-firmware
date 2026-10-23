/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#ifndef _SYNC_MASTER_H_
#define _SYNC_MASTER_H_

#include "Foundation/T_Singleton.h"
#include <cstdint>

// Provide basic functionalities to compute various
// setting regarding tempo, buffer sizes, ticks

enum class SyncClockSource : uint8_t { Internal, Midi };

class SyncMaster : public T_Singleton<SyncMaster> {
public:
  // In theory, this could be updated in the future in order to make rendering
  // intervals smaller, if done, it must remain a multiple of 6 so table and
  // MIDI slices stay aligned.
  // In practice, we should probably never chnage this
  static constexpr int SlicesPerStep = 6;

  SyncMaster();
  void Start();
  void Stop();
  void SetTempo(int tempo);
  int GetTempo();
  int GetDisplayTempo() const;
  void SetClockSource(SyncClockSource source);
  SyncClockSource GetClockSource() const;
  SyncClockSource GetActiveClockSource() const;
  void OnMidiClock(uint32_t timestampUs);
  void UpdateClock(uint32_t timestampUs);
  bool IsMidiClockLocked() const;
  uint32_t GetMidiClockPeriodUs() const;
  void NextSlice();
  bool MajorSlice();
  bool TableSlice();
  bool MidiSlice();
  float GetPlaySampleCount();
  float GetTickSampleCount();
  float GetStepSampleCount();
  unsigned int GetBeatCount();
  float GetTickTime();

private:
  static constexpr uint8_t MidiClockPeriodWindowSize = 6;

  void applyInternalTempo();
  void applyClockPeriod(float periodSamples);
  void resetMidiClock();
  void resetMidiClockPeriodEstimate();
  void slewClockPeriod(float targetPeriodSamples);
  void updateDisplayTempo(uint32_t timestampUs);

  int tempo_;
  int displayTempo_;
  int internalTempo_;
  int currentSlice_;
  unsigned int beatCount_;
  float playSampleCount_;
  float tickSampleCount_;
  int sampleRate_;
  SyncClockSource clockSource_;
  SyncClockSource activeClockSource_;
  uint32_t midiClockPeriodQ12_;
  uint32_t midiClockIntervalsUs_[MidiClockPeriodWindowSize];
  uint32_t midiClockIntervalSumUs_;
  uint32_t lastMidiClockTimestampUs_;
  uint32_t lastMidiClockIntervalUs_;
  uint32_t lastDisplayTempoTimestampUs_;
  uint32_t displayTempoWindowElapsedUs_;
  float displayTempoAccumulator_;
  uint8_t midiClockIntervalIndex_;
  uint8_t midiClockIntervalCount_;
  bool hasMidiClockTimestamp_;
  bool midiClockLocked_;
  bool returningToInternalClock_;
  bool displayTempoWindowActive_;
  bool midiClockPeriodLimited_;
};
#endif
