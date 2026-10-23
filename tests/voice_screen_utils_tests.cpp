/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "doctest/doctest.h"

#include "Application/Audio/AudioSample.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Views/VoiceScreenUtils.h"

namespace {

class TestInstrumentBase : public I_Instrument {
public:
  explicit TestInstrumentBase(InstrumentType type)
      : I_Instrument(&variables_), type_(type) {}

  bool Init() override { return true; }

  bool Start(int channel, unsigned char note, bool retrigger = true) override {
    (void)channel;
    (void)note;
    (void)retrigger;
    return true;
  }

  void Stop(int channel) override { (void)channel; }

  void OnStart() override {}

  bool IsInitialized() override { return true; }

  bool IsEmpty() override { return false; }

  InstrumentType GetType() override { return type_; }

  void ProcessCommand(int channel, TrackerCommand cc, ushort value) override {
    (void)channel;
    (void)cc;
    (void)value;
  }

  bool Render(int channel, AudioSample *buffer, int size,
              bool updateTick) override {
    (void)channel;
    (void)buffer;
    (void)size;
    (void)updateTick;
    return false;
  }

  int GetTable() override { return 0; }

  bool GetTableAutomation() override { return false; }

  void GetTableState(TableSaveState &state) override { (void)state; }

  void SetTableState(TableSaveState &state) override { (void)state; }

  etl::ivector<Variable *> *Variables() override { return &variables_; }

private:
  etl::vector<Variable *, 1> variables_;
  InstrumentType type_;
};

} // namespace

TEST_CASE("Audio voice parameters exclude only non-audio and legacy paths") {
  CHECK_FALSE(InstrumentTypeUsesModernAudioVoice(IT_NONE));
  CHECK_FALSE(InstrumentTypeUsesModernAudioVoice(IT_SAMPLE));
  CHECK_FALSE(InstrumentTypeUsesModernAudioVoice(IT_MIDI));
  CHECK(InstrumentTypeUsesModernAudioVoice(IT_SAMPLE_SOURCE));
  CHECK(InstrumentTypeUsesModernAudioVoice(IT_SID));
  CHECK(InstrumentTypeUsesModernAudioVoice(IT_STARLOOM));
}

TEST_CASE("Instrument preview supports audio instruments but not MIDI") {
  TestInstrumentBase noneInstrument(IT_NONE);
  TestInstrumentBase sampleInstrument(IT_SAMPLE);
  TestInstrumentBase midiInstrument(IT_MIDI);
  TestInstrumentBase synthInstrument(IT_FM6);

  CHECK_FALSE(SupportsInstrumentPreview(nullptr));
  CHECK_FALSE(SupportsInstrumentPreview(&noneInstrument));
  CHECK(SupportsInstrumentPreview(&sampleInstrument));
  CHECK_FALSE(SupportsInstrumentPreview(&midiInstrument));
  CHECK(SupportsInstrumentPreview(&synthInstrument));
}

TEST_CASE("Audio voice map marker is only shown on eligible screens") {
  TestInstrumentBase legacyInstrument(IT_SAMPLE);
  TestInstrumentBase audioVoiceInstrument(IT_SID);

  CHECK_FALSE(ShouldShowAudioVoiceScreenOnMap(VT_INSTRUMENT, nullptr));
  CHECK_FALSE(
      ShouldShowAudioVoiceScreenOnMap(VT_INSTRUMENT, &legacyInstrument));
  CHECK_FALSE(ShouldShowAudioVoiceScreenOnMap(VT_VOICE, &legacyInstrument));
  CHECK_FALSE(
      ShouldShowAudioVoiceScreenOnMap(VT_PHRASE, &audioVoiceInstrument));
  CHECK(ShouldShowAudioVoiceScreenOnMap(VT_INSTRUMENT, &audioVoiceInstrument));
  CHECK(ShouldShowAudioVoiceScreenOnMap(VT_VOICE, &audioVoiceInstrument));
}
