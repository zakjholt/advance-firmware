/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#ifndef PICSEQ_SEQ_MIDI_BRIDGE_H_
#define PICSEQ_SEQ_MIDI_BRIDGE_H_

#include "Application/Seq/IMidiSink.h"
#include "Application/Seq/SeqEngine.h"
#include "Foundation/Observable.h"
#include "Services/Midi/MidiMessage.h"
#include <cstdint>

class MidiInDevice;
class MidiService;

namespace picoSeq {

// Adapts MidiService::SendImmediate to IMidiSink.
class MidiServiceSink : public IMidiSink {
public:
  explicit MidiServiceSink(MidiService *service = nullptr);
  void SetService(MidiService *service) { service_ = service; }
  void SendImmediate(const MidiBytes &msg) override;

private:
  MidiService *service_;
};

// Observes MIDI input devices, drives SeqEngine, consumes messages so the
// tracker Player path is bypassed under SEQ_MIDI_ONLY.
class SeqMidiBridge : public I_Observer {
public:
  static SeqMidiBridge &Get();

  SeqEngine &Engine() { return engine_; }

  void Init();
  void AttachToService(MidiService &service);
  void OnServiceTick(uint32_t nowUs);

  // I_Observer: MidiInDevice notifies with MidiMessage*.
  void Update(Observable &o, I_ObservableData *d) override;

  bool IsActive() const { return active_; }
  void SetActive(bool active) { active_ = active; }

private:
  SeqMidiBridge();

  SeqEngine engine_;
  MidiServiceSink sink_;
  MidiService *service_;
  uint32_t lastTickUs_;
  bool hasLastTickUs_;
  bool active_;
  bool attached_;
};

} // namespace picoSeq

#endif
