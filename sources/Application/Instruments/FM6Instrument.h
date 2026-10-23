/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _FM6_INSTRUMENT_H_
#define _FM6_INSTRUMENT_H_

#include "AdvanceFM6/FM6Voice.h"
#include "Application/Audio/AudioSample.h"
#include "Application/Model/Song.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Externals/etl/include/etl/array.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Variables/StringVariable.h"
#include "I_Instrument.h"
#include "VoiceParameters.h"
#include "config/MemorySections.h"
#include <cstddef>
#include <cstdint>

class FM6Instrument : public I_Instrument {
public:
  FM6Instrument();
  virtual ~FM6Instrument();

  virtual bool Init();
  virtual bool Start(int channel, unsigned char note, bool retrigger = true);
  virtual void Release(int channel) override;
  virtual void Stop(int channel);
  virtual bool Render(int channel, AudioSample *buffer, int size,
                      bool updateTick) override;
  void SetPitchMultiplier(int channel, float multiplier) override;
  virtual AudioBufferLayout GetAudioBufferLayout(int channel) const override;
  virtual bool IsInitialized();
  virtual bool IsEmpty();
  PICOTRACKER_FAST_CODE("fm6_instrument_get_type")
  virtual InstrumentType GetType() { return IT_FM6; };
  bool ShouldAuditionParameterChange(FourCC parameter) const override;
  virtual void ProcessCommand(int channel, TrackerCommand cc,
                              ushort value) override;
  virtual int GetTable();
  virtual bool GetTableAutomation();
  virtual void GetTableState(TableSaveState &state);
  virtual void SetTableState(TableSaveState &state);
  etl::ivector<Variable *> *Variables() { return &variables_; };
  virtual void OnStart();

  // Selects a built-in preset, loading its record into the patch variables.
  void SetBuiltInPatch(int32_t program);
  // Loads a raw 128-byte packed DX7 voice record into the patch variables.
  void ImportDx7Patch(const uint8_t *record);

  // Rebuilds a 128-byte staging record from the patch variables and converts
  // it into an engine patch. The variables are the persisted state; the
  // engine patch is assembled from them at note start.
  void BuildPatch(fm6::Patch &patch);

  // Temporarily overrides the sounding patch so the import browser can
  // audition a candidate record without touching the instrument's state.
  void SetPreviewPatch(const uint8_t *record);
  void ClearPreviewPatch();

  // The DX7 algorithm number (0-31), stored inside the global parameter
  // slice (packed record byte 110).
  uint8_t GetAlgorithm();
  void SetAlgorithm(uint8_t algorithm);

  // Raw byte access into the seven patch slices, for editors that work on
  // individual packed-record fields. `sliceIndex` 0-5 selects an operator
  // slice in the record's physical order, so slice 0 is DX7 operator 6 and
  // slice 5 is DX7 operator 1; GlobalSliceIndex selects the global block.
  // Out-of-range offsets read 0 and ignore writes.
  static constexpr size_t GlobalSliceIndex = fm6::OperatorCount;
  uint8_t GetPatchByte(size_t sliceIndex, size_t offset);
  void SetPatchByte(size_t sliceIndex, size_t offset, uint8_t value);

  // The packed DX7 voice record is stored as seven hex-encoded string
  // variables: one 17-byte slice per operator plus the 16-byte global block
  // (pitch EG, algorithm, feedback/sync, LFO, transpose). The slices follow
  // the record's physical order — DX7 packed records store operator 6 first,
  // so op1Params_ holds DX7 operator 6 and op6Params_ holds DX7 operator 1.
  static constexpr size_t OperatorSliceSize = 17;
  static constexpr size_t GlobalSliceSize = 16;

private:
  // Slices `record` into the seven patch variables and notifies once.
  void applyRecord(const uint8_t *record);
  Variable &operatorParams(size_t opIndex);

  struct FM6RenderParams {
    fm6::VoiceState voice;
  };

  static constexpr size_t BaseVariableCount = 3;
  static constexpr size_t PatchVariableCount = fm6::OperatorCount + 1;
  static constexpr size_t VariableCount =
      BaseVariableCount + PatchVariableCount +
      VoiceParameters::PersistedVariableCount;

  etl::vector<Variable *, VariableCount> variables_;
  // Playback state is shared by channel rather than instrument definition,
  // matching the SID instrument's render-parameter ownership model, so the
  // same instrument can render independently on multiple channels.
  static etl::array<FM6RenderParams, SONG_CHANNEL_COUNT> renderParams_;
  TableSaveState tableState_;
  Variable program_;
  Variable table_;
  Variable tableAuto_;
  // Shared-default variables keep FM6 instruments from growing the
  // instrument pool's slot size: the preset-0 default slices live in one
  // static copy instead of one per instrument.
  SharedDefaultStringVariable<2 * OperatorSliceSize> op1Params_;
  SharedDefaultStringVariable<2 * OperatorSliceSize> op2Params_;
  SharedDefaultStringVariable<2 * OperatorSliceSize> op3Params_;
  SharedDefaultStringVariable<2 * OperatorSliceSize> op4Params_;
  SharedDefaultStringVariable<2 * OperatorSliceSize> op5Params_;
  SharedDefaultStringVariable<2 * OperatorSliceSize> op6Params_;
  SharedDefaultStringVariable<2 * GlobalSliceSize> globalParams_;
  // Preview override used by the import browser's audition path.
  fm6::Patch previewPatch_;
  bool previewActive_;
  bool initialized_;
};

#endif
