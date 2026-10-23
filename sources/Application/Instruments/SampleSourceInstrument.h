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

#ifndef _SAMPLE_SOURCE_INSTRUMENT_H_
#define _SAMPLE_SOURCE_INSTRUMENT_H_

#include "Application/Audio/AudioSample.h"
#include "Application/Model/Song.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Externals/etl/include/etl/array.h"
#include "Foundation/Observable.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "I_Instrument.h"
#include "SampleRetrigger.h"
#include "SampleVariable.h"
#include "SoundSource.h"
#include "VoiceParameters.h"
#include "config/MemorySections.h"

enum SampleSourceInstrumentLoopMode {
  SSLM_ONESHOT = 0,
  SSLM_REVERSE,
  SSLM_LOOP,
  SSLM_REVERSE_LOOP,
  SSLM_LOOP_PINGPONG,
  SSLM_OSC,
  SSLM_LOOPSYNC,
  SSLM_LAST
};

struct sampleSourceRenderParams {
  void *sampleBuffer_;
  int channelCount_;

  float position_;
  int rendFirst_;
  int rendLoopStart_;
  int rendLoopEnd_;
  float levelGain_;

  float baseSpeed_;
  float speed_;
  bool reverse_;

  SampleRetriggerState retrigger_;

  bool finished_;
  bool sourceEnded_;

  int tailSamplesRemaining_;
  AudioSample tailOriginLeft_;
  AudioSample tailOriginRight_;
  AudioSample lastOutputLeft_;
  AudioSample lastOutputRight_;

  bool couldClick_;

  char midiNote_;
  bool sliceActive_;
  uint8_t activeSliceIndex_;
  int loopModeValue_;
};

class SampleSourceInstrument : public I_Instrument, I_Observer {
public:
  SampleSourceInstrument();
  virtual ~SampleSourceInstrument();

  virtual bool Init();
  virtual bool Start(int channel, unsigned char note, bool trigger = true);
  virtual void Stop(int channel);
  virtual bool Render(int channel, AudioSample *buffer, int size,
                      bool updateTick) override;
  void SetPitchMultiplier(int channel, float multiplier) override;
  virtual AudioBufferLayout GetAudioBufferLayout(int channel) const override;
  bool GetPlaybackPosition(int channel, uint32_t &position) const override;
  virtual bool IsInitialized();
  virtual bool IsEmpty();

  PICOTRACKER_FAST_CODE("sample_source_get_type")
  virtual InstrumentType GetType() { return IT_SAMPLE_SOURCE; };
  bool ShouldAuditionParameterChange(FourCC parameter) const override;
  unsigned char GetPreviewNote();
  virtual void ProcessCommand(int channel, TrackerCommand cc, ushort value);
  virtual int GetTable();
  virtual bool GetTableAutomation();
  virtual void GetTableState(TableSaveState &state);
  virtual void SetTableState(TableSaveState &state);
  etl::ivector<Variable *> *Variables() { return &variables_; };

  bool IsMulti();

  virtual void OnStart();
  static constexpr size_t MaxSlices = 64;
  static constexpr unsigned char SliceNoteBase = 48;
  static constexpr int MaxOscillatorSampleFrames = 4096;

  uint32_t GetSlicePoint(size_t index) const;
  void SetSlicePoint(size_t index, uint32_t start);
  void ClearSlices();
  bool HasSlicesForPlayback() const;
  bool HasSlicesForWarning() const;
  bool IsSliceDefined(size_t index) const;
  bool ShouldDisplaySliceForNote(uint8_t midinote) const;
  bool GetSliceNoteRange(uint8_t &first, uint8_t &last) const;
  static bool PlaybackModeUsesLoopPoint(int playbackMode);

  virtual void Update(Observable &o, I_ObservableData *d);

  void AssignSample(int i);
  int GetSampleIndex();
  int GetSampleSize(int channel = -1);
  bool ResetOscillatorLoopModeIfSampleTooLarge();
  bool SkipOscillatorLoopModeIfSampleTooLarge(int direction);
  float GetLengthInSec();

  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetUserSetName();
  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetDisplayName() override;
  virtual etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> GetSampleFileName();

  virtual void SaveContent(tinyxml2::XMLPrinter *printer) override;
  virtual void RestoreContent(PersistencyDocument *doc) override;
  void Purge();
  virtual void CloneFrom(I_Instrument &src) override;

protected:
  void updateInstrumentData(bool search);

private:
  static constexpr size_t BaseVariableCount = 9;
  static constexpr size_t VariableCount =
      BaseVariableCount + VoiceParameters::PersistedVariableCount;

  etl::vector<Variable *, VariableCount> variables_;

  SoundSource *source_;
#if !defined(HOST_TEST)
  __attribute__((section(".DTCMRAM")))
#endif
  static sampleSourceRenderParams renderParams_[SONG_CHANNEL_COUNT];
  bool running_;
  bool dirty_;
  // When true, the next updateInstrumentData() reload keeps the user's
  // start/loop/end/slice edits instead of resetting them. Set when the sample
  // change is only a pool re-index (same sample shifted to a new index).
  bool preserveEditsOnReload_ = false;
  TableSaveState tableState_;

  SampleVariable sample_;
  Variable level_;
  Variable rootNote_;
  WatchedVariable start_;
  Variable loopMode_;
  WatchedVariable loopStart_;
  WatchedVariable loopEnd_;
  Variable table_;
  Variable tableAuto_;
  etl::array<uint32_t, MaxSlices> slicePoints_;

  bool isSliceIndexActive(size_t index) const;
  bool shouldUseSlice(unsigned char midinote, size_t &sliceIndex,
                      uint32_t sampleSize) const;
  uint32_t computeSliceStart(size_t index, uint32_t sampleSize) const;
  uint32_t computeSliceEnd(size_t index, uint32_t sampleSize) const;
  bool hasAnySliceValue() const;
  void clampSlicePoints(uint32_t sampleSize);
  int getSelectedSampleSize();
};

#endif
