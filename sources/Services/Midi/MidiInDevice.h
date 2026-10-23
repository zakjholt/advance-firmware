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

#ifndef _MIDIIN_DEVICE_H_
#define _MIDIIN_DEVICE_H_

#include "Foundation/Observable.h"
#include "MidiEvent.h"
#include "MidiInputSource.h"
#include "MidiMessage.h"

enum MidiSyncMessage { MSM_START, MSM_STOP, MSM_TEMPOTICK, MSM_CONTINUE };

struct MidiSyncData : public I_ObservableData {
  MidiSyncMessage message_;
  MidiSyncData(MidiSyncMessage msg) : message_(msg){};
};

class MidiInDevice : public Observable {
public:
  MidiInDevice(const char *name);
  virtual ~MidiInDevice();
  bool Init();
  void Close();
  virtual bool Start() = 0;
  virtual void Stop() = 0;

  virtual bool IsRunning();
  virtual void Trigger();

  // Each MIDI channel keeps its currently selected program.
  void SetProgramForChannel(int midiChannel, uint8_t program);
  int GetProgramForChannel(int midiChannel) const;

  virtual void poll() = 0;

protected:
  // Driver specific initialisation
  virtual bool initDriver() = 0;
  virtual void closeDriver() = 0;
  virtual bool startDriver() = 0;
  virtual void stopDriver() = 0;
  // The concrete device type defines which physical input owns its notes.
  virtual MidiInputSource getInputSource() const = 0;

  void treatChannelEvent(MidiMessage &event, uint32_t timestampUs);
  bool isRunning_;

  // Callbacks from driver

  void onDriverMessage(MidiMessage &event, uint32_t timestampUs = 0);
  void onMidiTempoTick(uint32_t timestampUs);
  void onMidiStart();
  void onMidiStop();
  void onMidiContinue();
  void queueEvent(MidiEvent &event);

  // MIDI message parsing
  void processMidiData(uint8_t data, uint32_t timestampUs = 0);

private:
  void resetParserState();
  void releaseActiveNotes();

  etl::vector<I_Observer *, 4> observers_;
  static bool dumpEvents_;

  // Programs map directly to instrument numbers when notes are played.
  uint8_t channelPrograms_[16];

  // MIDI message parsing state
  uint8_t midiStatus = 0;
  uint8_t midiData1 = 0;
  uint8_t midiDataCount = 0;
  uint8_t midiDataBytes = 0;
};

#endif
