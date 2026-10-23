/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _PICO_SWARM_INSTRUMENT_H_
#define _PICO_SWARM_INSTRUMENT_H_

#include "Application/Model/Song.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Externals/etl/include/etl/array.h"
#include "Foundation/Types/Types.h"
#include "I_Instrument.h"
#include "VoiceParameters.h"
#include "config/MemorySections.h"
#include <cstddef>
#include <cstdint>

namespace picoSwarm {

static constexpr size_t OscillatorCount = 7;
static constexpr size_t CenterOscillator = 3;
static constexpr float SampleRate = 44100.0f;
static constexpr size_t HighPassStageCount = 2;

// Pitch-tracked four-pole high-pass used after the oscillator mixer. The
// stages form a fourth-order Butterworth response: this is the closest
// specified reconstruction currently available for the otherwise
// undocumented filter at the end of the original JP-8000 signal path.
class PitchTrackedHighPass {
public:
  PitchTrackedHighPass();

  void Reset();
  void Configure(float cutoffFrequency);
  void ProcessBlock(AudioSample *buffer, size_t sampleCount);

private:
  struct Stage {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    AudioSample state1;
    AudioSample state2;
  };

  etl::array<Stage, HighPassStageCount> stages_;
};

// One monophonic seven-oscillator source. The saws are intentionally naive
// rather than band limited: the thesis identifies their aliasing as part of
// the JP-8000 oscillator character.
class Voice {
public:
  Voice();

  void Reset(uint32_t randomSeed);
  void Start(uint8_t note, bool retrigger);
  void SetPitchMultiplier(float multiplier);
  void Stop();
  bool Render(AudioSample *buffer, size_t sampleCount, uint8_t detune,
              uint8_t mix);

  bool IsActive() const { return active_; }
  float GetBaseFrequency() const { return baseFrequency_; }
  double GetPhase(size_t oscillator) const;

  // Curve and ratio helpers are public so the fitted behavior can be verified
  // independently of oscillator phase and filtering.
  static float DetuneAmount(float control);
  static float OscillatorRatio(size_t oscillator, float control);
  static float CenterGain(float control);
  static float SideGain(float control);
  static float NoteFrequency(uint8_t note);

private:
  uint32_t nextRandom();
  void randomizePhases();
  void updateHighPass();

  etl::array<double, OscillatorCount> phases_;
  float baseNoteFrequency_;
  float baseFrequency_;
  float pitchMultiplier_;
  PitchTrackedHighPass highPass_;
  uint32_t randomState_;
  bool active_;
};

} // namespace picoSwarm

class picoSwarmInstrument : public I_Instrument {
public:
  picoSwarmInstrument();
  ~picoSwarmInstrument() override;

  bool Init() override;
  bool Start(int channel, unsigned char note, bool retrigger = true) override;
  void Release(int channel) override;
  void Stop(int channel) override;
  bool Render(int channel, AudioSample *buffer, int size,
              bool updateTick) override;
  void SetPitchMultiplier(int channel, float multiplier) override;
  AudioBufferLayout GetAudioBufferLayout(int channel) const override;
  bool IsInitialized() override;
  bool IsEmpty() override;
  PICOTRACKER_FAST_CODE("pico_swarm_instrument_get_type")
  InstrumentType GetType() override { return IT_PICO_SWARM; }
  bool ShouldAuditionParameterChange(FourCC parameter) const override;
  void ProcessCommand(int channel, TrackerCommand cc, ushort value) override;
  int GetTable() override;
  bool GetTableAutomation() override;
  void GetTableState(TableSaveState &state) override;
  void SetTableState(TableSaveState &state) override;
  etl::ivector<Variable *> *Variables() override { return &variables_; }
  void OnStart() override;

private:
  static constexpr size_t BaseVariableCount = 4;
  static constexpr size_t VariableCount =
      BaseVariableCount + VoiceParameters::PersistedVariableCount;

  etl::vector<Variable *, VariableCount> variables_;
  static etl::array<picoSwarm::Voice, SONG_CHANNEL_COUNT> voices_;
  TableSaveState tableState_;
  Variable detune_;
  Variable mix_;
  Variable table_;
  Variable tableAuto_;
  bool initialized_;
};

#endif
