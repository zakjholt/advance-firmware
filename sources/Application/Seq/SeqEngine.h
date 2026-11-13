/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#ifndef PICSEQ_SEQ_ENGINE_H_
#define PICSEQ_SEQ_ENGINE_H_

#include "Application/Seq/IMidiSink.h"
#include "Application/Seq/MidiRouter.h"
#include "Application/Seq/SeqPart.h"
#include "Application/Seq/SeqTickClock.h"
#include <cstdint>

namespace picoSeq {

enum class SeqTransport : uint8_t { Stopped, Playing, Recording };

class SeqEngine {
public:
  SeqEngine();

  void SetSink(IMidiSink *sink);
  MidiRouter &Router() { return router_; }
  SeqPart &Part() { return part_; }
  const SeqPart &Part() const { return part_; }
  SeqTickClock &Clock() { return clock_; }

  SeqTransport Transport() const { return transport_; }
  uint32_t PlayTick() const { return playTick_; }

  void Stop();
  void Play();
  // Replace-record on currently armed tracks (arms track 0 if none armed).
  void Record();

  void ArmTrack(int track);
  int ArmedTrack() const;

  // Called when MIDI arrives (from router).
  void CaptureIncoming(uint8_t status, uint8_t data1, uint8_t data2,
                       uint32_t tick);

  // Advance the sequencer by `ticks` and emit playback events in that window.
  void AdvanceTicks(uint32_t ticks);

  // Convenience: advance from elapsed wall time.
  uint32_t AdvanceMicros(uint32_t elapsedUs);

  // Host-test helper: process one incoming message at the current play tick
  // through the router (thru + optional capture).
  bool HandleIncoming(uint8_t status, uint8_t data1, uint8_t data2);

  uint32_t EventsPlayed() const { return eventsPlayed_; }
  uint32_t EventsCaptured() const { return eventsCaptured_; }
  uint32_t EventsDropped() const { return eventsDropped_; }

private:
  void emitRange(uint32_t fromTick, uint32_t toTick);
  void wrapPlayhead();

  SeqPart part_;
  SeqTickClock clock_;
  MidiRouter router_;
  IMidiSink *sink_;
  SeqTransport transport_;
  uint32_t playTick_;
  uint32_t eventsPlayed_;
  uint32_t eventsCaptured_;
  uint32_t eventsDropped_;
};

} // namespace picoSeq

#endif
