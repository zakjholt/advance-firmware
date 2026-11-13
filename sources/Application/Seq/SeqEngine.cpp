/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#include "Application/Seq/SeqEngine.h"

namespace picoSeq {

SeqEngine::SeqEngine()
    : sink_(nullptr), transport_(SeqTransport::Stopped), playTick_(0),
      eventsPlayed_(0), eventsCaptured_(0), eventsDropped_(0) {
  router_.SetEngine(this);
  part_.ArmOnly(0);
}

void SeqEngine::SetSink(IMidiSink *sink) {
  sink_ = sink;
  router_.SetSink(sink);
}

void SeqEngine::Stop() {
  transport_ = SeqTransport::Stopped;
  playTick_ = 0;
  clock_.SetTick(0);
}

void SeqEngine::Play() {
  transport_ = SeqTransport::Playing;
  playTick_ = 0;
  clock_.SetTick(0);
}

void SeqEngine::Record() {
  if (ArmedTrack() < 0) {
    part_.ArmOnly(0);
  }
  // Replace-record: clear armed tracks before take.
  for (int i = 0; i < kTrackCount; ++i) {
    if (part_.Track(i).armed_) {
      part_.Track(i).events_.Clear();
    }
  }
  transport_ = SeqTransport::Recording;
  playTick_ = 0;
  clock_.SetTick(0);
  eventsCaptured_ = 0;
  eventsDropped_ = 0;
}

void SeqEngine::ArmTrack(int track) { part_.ArmOnly(track); }

int SeqEngine::ArmedTrack() const {
  for (int i = 0; i < kTrackCount; ++i) {
    if (part_.Track(i).armed_) {
      return i;
    }
  }
  return -1;
}

void SeqEngine::CaptureIncoming(uint8_t status, uint8_t data1, uint8_t data2,
                                uint32_t tick) {
  if (transport_ != SeqTransport::Recording) {
    return;
  }

  SeqEvent event;
  event.tick_ = tick;
  event.status_ = status;
  event.data1_ = data1;
  event.data2_ = data2;

  bool stored = false;
  for (int i = 0; i < kTrackCount; ++i) {
    SeqTrack &track = part_.Track(i);
    if (!track.armed_) {
      continue;
    }
    if (track.events_.Add(event)) {
      stored = true;
      ++eventsCaptured_;
    } else {
      ++eventsDropped_;
    }
  }

  (void)stored;
}

void SeqEngine::emitRange(uint32_t fromTick, uint32_t toTick) {
  if (sink_ == nullptr || fromTick >= toTick) {
    return;
  }

  for (int t = 0; t < kTrackCount; ++t) {
    const SeqTrack &track = part_.Track(t);
    if (track.muted_) {
      continue;
    }
    // Avoid double-send: thru already monitors the live take on armed tracks.
    if (transport_ == SeqTransport::Recording && track.armed_) {
      continue;
    }
    track.events_.ForEachInRange(fromTick, toTick, [&](const SeqEvent &e) {
      MidiBytes out;
      out.status_ = e.status_;
      out.data1_ = e.data1_;
      out.data2_ = e.data2_;
      const uint8_t type = e.TypeNibble();
      out.hasData1_ = true;
      out.hasData2_ =
          (type == 0x80 || type == 0x90 || type == 0xA0 || type == 0xB0 ||
           type == 0xE0);
      if (type == 0xC0 || type == 0xD0) {
        out.hasData2_ = false;
      }
      sink_->SendImmediate(out);
      ++eventsPlayed_;
    });
  }
}

void SeqEngine::wrapPlayhead() {
  const uint32_t length = part_.LengthTicks();
  if (length == 0) {
    return;
  }
  if (playTick_ >= length) {
    if (part_.Loop() && (transport_ == SeqTransport::Playing ||
                         transport_ == SeqTransport::Recording)) {
      playTick_ %= length;
      // Recording drops out of record on loop (MMT-like); keep playing.
      if (transport_ == SeqTransport::Recording) {
        transport_ = SeqTransport::Playing;
        part_.ClearAllArms();
      }
    } else {
      transport_ = SeqTransport::Stopped;
      playTick_ = 0;
    }
  }
}

void SeqEngine::AdvanceTicks(uint32_t ticks) {
  if (transport_ == SeqTransport::Stopped || ticks == 0) {
    return;
  }

  const uint32_t from = playTick_;
  const uint32_t length = part_.LengthTicks();
  uint32_t remaining = ticks;

  while (remaining > 0 && transport_ != SeqTransport::Stopped) {
    const uint32_t room = length > playTick_ ? length - playTick_ : 0;
    const uint32_t step = remaining < room ? remaining : room;
    if (step == 0) {
      wrapPlayhead();
      if (transport_ == SeqTransport::Stopped) {
        break;
      }
      continue;
    }

    const uint32_t to = playTick_ + step;
    // During record, still play back non-armed tracks (overdub preview later).
    emitRange(playTick_, to);
    playTick_ = to;
    clock_.SetTick(playTick_);
    remaining -= step;
    wrapPlayhead();
  }

  (void)from;
}

uint32_t SeqEngine::AdvanceMicros(uint32_t elapsedUs) {
  const uint32_t advanced = clock_.AdvanceMicros(elapsedUs);
  AdvanceTicks(advanced);
  return advanced;
}

bool SeqEngine::HandleIncoming(uint8_t status, uint8_t data1, uint8_t data2) {
  return router_.OnIncoming(status, data1, data2, playTick_);
}

} // namespace picoSeq
