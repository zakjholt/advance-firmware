/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "doctest/doctest.h"

#include "Services/Midi/MidiInDevice.h"
#include "Services/Midi/MidiNoteTracker.h"

void ResetPlayerMidiInputTestState();
int GetPlayerSongStartButtonCallCount();
int GetPlayerStopCallCount();
int GetPlayerPlayNoteCallCount();
int GetPlayerStopNoteCallCount();
int GetPlayerReleaseMidiInputSourceCallCount();
int GetPlayerLastMidiChannel();
int GetPlayerLastMidiInstrument();
int GetPlayerLastMidiNote();
int GetPlayerLastMidiInputSource();
uint32_t GetMidiClockCallCount();
uint32_t GetLastMidiClockTimestampUs();

namespace {

class CountingObserver : public I_Observer {
public:
  void Update(Observable &o, I_ObservableData *d) override {
    observed_ = &o;
    payload_ = d;
    notificationCount_++;
  }

  int notificationCount_ = 0;
  Observable *observed_ = nullptr;
  I_ObservableData *payload_ = nullptr;
};

class TestMidiInDevice : public MidiInDevice {
public:
  explicit TestMidiInDevice(MidiInputSource inputSource = MidiInputSource::Trs)
      : MidiInDevice("test"), inputSource_(inputSource) {}

  bool Start() override { return MidiInDevice::Start(); }
  void Stop() override { MidiInDevice::Stop(); }
  void poll() override {}

  void EmitMessage(MidiMessage &message, uint32_t timestampUs = 0) {
    onDriverMessage(message, timestampUs);
  }

  int startDriverCallCount_ = 0;
  int stopDriverCallCount_ = 0;

protected:
  bool initDriver() override { return true; }
  void closeDriver() override {}
  bool startDriver() override {
    startDriverCallCount_++;
    return true;
  }
  void stopDriver() override { stopDriverCallCount_++; }
  MidiInputSource getInputSource() const override { return inputSource_; }

private:
  MidiInputSource inputSource_;
};

} // namespace

TEST_CASE("MidiInDevice base lifecycle controls running state and driver") {
  TestMidiInDevice device;

  CHECK_FALSE(device.IsRunning());
  CHECK(device.startDriverCallCount_ == 0);
  CHECK(device.stopDriverCallCount_ == 0);

  REQUIRE(device.Start());
  CHECK(device.IsRunning());
  CHECK(device.startDriverCallCount_ == 1);

  device.Stop();
  CHECK_FALSE(device.IsRunning());
  CHECK(device.stopDriverCallCount_ == 1);
}

TEST_CASE("MidiInDevice does not start playback while stopped") {
  ResetPlayerMidiInputTestState();

  TestMidiInDevice device;
  CountingObserver observer;
  MidiMessage message(MidiMessage::MIDI_START);

  device.AddObserver(observer);
  device.EmitMessage(message);

  CHECK(observer.notificationCount_ == 1);
  CHECK(observer.payload_ == &message);
  CHECK(GetPlayerSongStartButtonCallCount() == 0);

  REQUIRE(device.Start());
  device.EmitMessage(message);
  CHECK(GetPlayerSongStartButtonCallCount() == 1);

  device.Stop();
  device.EmitMessage(message);
  CHECK(observer.notificationCount_ == 4);
  CHECK(GetPlayerSongStartButtonCallCount() == 1);
}

TEST_CASE("MidiInDevice handles transport without an observer") {
  ResetPlayerMidiInputTestState();

  TestMidiInDevice device;
  REQUIRE(device.Start());

  MidiMessage start(MidiMessage::MIDI_START);
  device.EmitMessage(start);
  CHECK(GetPlayerSongStartButtonCallCount() == 1);

  MidiMessage stop(MidiMessage::MIDI_STOP);
  device.EmitMessage(stop);
  CHECK(GetPlayerStopCallCount() == 1);

  MidiMessage continueMessage(MidiMessage::MIDI_CONTINUE);
  device.EmitMessage(continueMessage);
  CHECK(GetPlayerSongStartButtonCallCount() == 2);
}

TEST_CASE("MidiInDevice does not play notes while stopped") {
  ResetPlayerMidiInputTestState();

  TestMidiInDevice device;
  MidiMessage noteOn(MidiMessage::MIDI_NOTE_ON, 60, 100);

  device.EmitMessage(noteOn);
  CHECK(GetPlayerPlayNoteCallCount() == 0);

  REQUIRE(device.Start());
  device.EmitMessage(noteOn);
  CHECK(GetPlayerPlayNoteCallCount() == 1);

  device.Stop();
  device.EmitMessage(noteOn);
  CHECK(GetPlayerPlayNoteCallCount() == 1);
}

TEST_CASE("MidiInDevice stop releases notes owned by its input source") {
  ResetPlayerMidiInputTestState();

  TestMidiInDevice device(MidiInputSource::Usb);
  REQUIRE(device.Start());

  MidiMessage noteOn(MidiMessage::MIDI_NOTE_ON, 60, 100);
  device.EmitMessage(noteOn);

  device.Stop();

  CHECK(GetPlayerReleaseMidiInputSourceCallCount() == 1);
  CHECK(GetPlayerLastMidiInputSource() ==
        static_cast<int>(MidiInputSource::Usb));
}

TEST_CASE(
    "MidiInDevice uses instrument zero until a program change is received") {
  ResetPlayerMidiInputTestState();

  TestMidiInDevice device;
  REQUIRE(device.Start());

  MidiMessage noteOn(MidiMessage::MIDI_NOTE_ON + 5, 64, 100);
  device.EmitMessage(noteOn);

  CHECK(GetPlayerPlayNoteCallCount() == 1);
  CHECK(GetPlayerLastMidiChannel() == 5);
  CHECK(GetPlayerLastMidiInstrument() == 0);
  CHECK(GetPlayerLastMidiNote() == 64);
  CHECK(GetPlayerLastMidiInputSource() ==
        static_cast<int>(MidiInputSource::Trs));
}

TEST_CASE("MidiInDevice preserves its source on note messages") {
  ResetPlayerMidiInputTestState();

  TestMidiInDevice device(MidiInputSource::Usb);
  REQUIRE(device.Start());

  MidiMessage noteOn(MidiMessage::MIDI_NOTE_ON, 60, 100);
  device.EmitMessage(noteOn);
  CHECK(GetPlayerLastMidiInputSource() ==
        static_cast<int>(MidiInputSource::Usb));

  MidiMessage noteOff(MidiMessage::MIDI_NOTE_OFF, 60, 0);
  device.EmitMessage(noteOff);
  CHECK(GetPlayerLastMidiInputSource() ==
        static_cast<int>(MidiInputSource::Usb));
}

TEST_CASE("MidiInDevice program change selects the matching instrument") {
  ResetPlayerMidiInputTestState();

  TestMidiInDevice device;
  REQUIRE(device.Start());

  MidiMessage programChange(MidiMessage::MIDI_PROGRAM_CHANGE + 3, 0x2A);
  device.EmitMessage(programChange);
  MidiMessage noteOn(MidiMessage::MIDI_NOTE_ON + 3, 60, 100);
  device.EmitMessage(noteOn);

  CHECK(GetPlayerPlayNoteCallCount() == 1);
  CHECK(GetPlayerLastMidiChannel() == 3);
  CHECK(GetPlayerLastMidiInstrument() == 0x2A);

  MidiMessage noteOff(MidiMessage::MIDI_NOTE_OFF + 3, 60, 0);
  device.EmitMessage(noteOff);
  CHECK(GetPlayerStopNoteCallCount() == 1);
  CHECK(GetPlayerLastMidiChannel() == 3);
  CHECK(GetPlayerLastMidiNote() == 60);
}

TEST_CASE("MidiInDevice program can be selected directly for one channel") {
  ResetPlayerMidiInputTestState();

  TestMidiInDevice device;
  REQUIRE(device.Start());

  device.SetProgramForChannel(6, 0x19);

  MidiMessage selectedChannelNote(MidiMessage::MIDI_NOTE_ON + 6, 60, 100);
  device.EmitMessage(selectedChannelNote);
  CHECK(GetPlayerPlayNoteCallCount() == 1);
  CHECK(GetPlayerLastMidiChannel() == 6);
  CHECK(GetPlayerLastMidiInstrument() == 0x19);

  MidiMessage unchangedChannelNote(MidiMessage::MIDI_NOTE_ON + 7, 62, 100);
  device.EmitMessage(unchangedChannelNote);
  CHECK(GetPlayerPlayNoteCallCount() == 2);
  CHECK(GetPlayerLastMidiChannel() == 7);
  CHECK(GetPlayerLastMidiInstrument() == 0);
}

TEST_CASE("MidiInDevice ignores programs without instrument slots") {
  ResetPlayerMidiInputTestState();

  TestMidiInDevice device;
  REQUIRE(device.Start());

  MidiMessage programChange(MidiMessage::MIDI_PROGRAM_CHANGE, 0x40);
  device.EmitMessage(programChange);
  MidiMessage noteOn(MidiMessage::MIDI_NOTE_ON, 60, 100);
  device.EmitMessage(noteOn);

  CHECK(GetPlayerPlayNoteCallCount() == 0);
}

TEST_CASE("MidiNoteTracker occupies the requested reserved song channel") {
  MidiNoteTracker tracker;

  REQUIRE(tracker.registerNote(MidiInputSource::Trs, 60, 2, 5, 100));
  CHECK(tracker.getAudioChannelForNote(MidiInputSource::Trs, 60, 2) == 5);
  CHECK(tracker.getActiveChannelMask() == (1U << 5));
  CHECK(tracker.unregisterNote(MidiInputSource::Trs, 60, 2) == 5);
  CHECK(tracker.getActiveChannelMask() == 0);
}

TEST_CASE("MidiNoteTracker separates identical notes by input source") {
  MidiNoteTracker tracker;

  REQUIRE(tracker.registerNote(MidiInputSource::Trs, 60, 2, 1, 100));
  REQUIRE(tracker.registerNote(MidiInputSource::Usb, 60, 2, 5, 90));

  CHECK(tracker.unregisterNote(MidiInputSource::Usb, 60, 2) == 5);
  CHECK(tracker.getAudioChannelForNote(MidiInputSource::Trs, 60, 2) == 1);
  CHECK(tracker.getActiveChannelMask() == (1U << 1));
  CHECK(tracker.unregisterNote(MidiInputSource::Trs, 60, 2) == 1);
  CHECK(tracker.getActiveChannelMask() == 0);
}

TEST_CASE("MidiNoteTracker clears only notes from the selected input source") {
  MidiNoteTracker tracker;

  REQUIRE(tracker.registerNote(MidiInputSource::Trs, 60, 2, 1, 100));
  REQUIRE(tracker.registerNote(MidiInputSource::Usb, 62, 3, 5, 90));
  REQUIRE(tracker.registerNote(MidiInputSource::Trs, 64, 4, 7, 80));

  const uint16_t clearedMask = tracker.clearInputSource(MidiInputSource::Trs);

  CHECK(clearedMask == ((1U << 1) | (1U << 7)));
  CHECK(tracker.getAudioChannelForNote(MidiInputSource::Trs, 60, 2) == -1);
  CHECK(tracker.getAudioChannelForNote(MidiInputSource::Trs, 64, 4) == -1);
  CHECK(tracker.getAudioChannelForNote(MidiInputSource::Usb, 62, 3) == 5);
  CHECK(tracker.getActiveChannelMask() == (1U << 5));
}

TEST_CASE("MidiInDevice preserves the receive timestamp for MIDI clock") {
  ResetPlayerMidiInputTestState();

  TestMidiInDevice device;
  MidiMessage clock(MidiMessage::MIDI_CLOCK);

  REQUIRE(device.Start());
  device.EmitMessage(clock, 123456U);

  CHECK(GetMidiClockCallCount() == 1U);
  CHECK(GetLastMidiClockTimestampUs() == 123456U);
}
