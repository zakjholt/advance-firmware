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

#ifndef _SID_INSTRUMENT_H_
#define _SID_INSTRUMENT_H_

#include "Application/Model/Song.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "I_Instrument.h"
#include "PitchOffset.h"
#include "SIDRenderingParams.h"
#include "VoiceParameters.h"
#include "config/MemorySections.h"

enum SIDInstrumentWaveform {
  DWF_NONE = 0,
  DWF_TRI,
  DWF_SAW,
  DWF_TRI_SAW,
  DWF_SQ,
  DWF_TRI_SQ,
  DWF_SAW_SQ,
  DWF_TRI_SAW_SQ,
  DWF_NOISE,
  DWF_LAST
};
// TODO: filter modes are additive, so they can be used together. Does it make
// sense to offer all combinations, or just some that make sense?
enum SIDInstrumentFilterMode {
  DFM_LP = 0,
  DFM_BP,
  DFM_HP,
  DFM_NOTCH,
  DFM_LAST
};

enum SIDInstrumentChipModel { SID_CHIP_6581 = 0, SID_CHIP_8580, SID_CHIP_LAST };

static const unsigned short sid_notes[96] = {
    0x0112, 0x0123, 0x0134, 0x0146, 0x015A, 0x016E, 0x0184, 0x019B, 0x01B3,
    0x01CD, 0x01E9, 0x0206, 0x0225, 0x0245, 0x0268, 0x028C, 0x02B3, 0x02DC,
    0x0308, 0x0336, 0x0367, 0x039B, 0x03D2, 0x040C, 0x0449, 0x048B, 0x04D0,
    0x0519, 0x0567, 0x05B9, 0x0610, 0x066C, 0x06CE, 0x0735, 0x07A3, 0x0817,
    0x0893, 0x0915, 0x099F, 0x0A32, 0x0ACD, 0x0B72, 0x0C20, 0x0CD8, 0x0D9C,
    0x0E6B, 0x0F46, 0x102F, 0x1125, 0x122A, 0x133F, 0x1464, 0x159A, 0x16E3,
    0x183F, 0x1981, 0x1B38, 0x1CD6, 0x1E80, 0x205E, 0x224B, 0x2455, 0x267E,
    0x28C8, 0x2B34, 0x2DC6, 0x307F, 0x3361, 0x366F, 0x39AC, 0x3D1A, 0x40BC,
    0x4495, 0x48A9, 0x4CFC, 0x518F, 0x5669, 0x5B8C, 0x60FE, 0x6602, 0x6CDF,
    0x7358, 0x7A34, 0x8178, 0x892B, 0x9153, 0x99F7, 0xA31F, 0xACD2, 0xB719,
    0xC1FC, 0xCD85, 0xD9BD, 0xE6B0, 0xF467, 0xFFFF}; // last one is 0x1F2F0

class SIDInstrument : public I_Instrument {

public:
  static constexpr int VoiceCount = 3;

  SIDInstrument();
  virtual ~SIDInstrument();

  virtual bool Init();

  // Start & stop the instument
  virtual bool Start(int channel, unsigned char note, bool retrigger = true);
  virtual void Release(int channel) override;
  virtual void Stop(int channel);

  // size refers to the number of samples
  // should always fill packed mono AudioSample values
  virtual bool Render(int channel, AudioSample *buffer, int size,
                      bool updateTick);
  void SetPitchMultiplier(int channel, float multiplier) override;
  virtual AudioBufferLayout GetAudioBufferLayout(int channel) const override;
  virtual void ProcessCommand(int channel, TrackerCommand cc, ushort value);

  virtual bool IsInitialized();

  virtual bool IsEmpty() { return false; };

  PICOTRACKER_FAST_CODE("sid_instrument_get_type")
  virtual InstrumentType GetType() { return IT_SID; };
  bool ShouldAuditionParameterChange(FourCC parameter) const override;

  virtual void RestoreContent(PersistencyDocument *doc) override;

  virtual etl::string<MAX_INSTRUMENT_NAME_LENGTH> GetName();

  virtual void OnStart();

  virtual int GetTable();
  virtual bool GetTableAutomation();
  virtual void GetTableState(TableSaveState &state);
  virtual void SetTableState(TableSaveState &state);
  etl::ivector<Variable *> *Variables() { return &variables_; };

private:
  struct VoiceVariables {
    VoiceVariables(FourCC pulseWidthID, FourCC waveformID, FourCC syncID,
                   FourCC ringID, FourCC adsrID, FourCC filterOnID,
                   FourCC pitchOffsetID);

    void AppendTo(etl::ivector<Variable *> &variables);

    Variable pulseWidth_;
    Variable waveform_;
    Variable sync_;
    Variable ring_;
    Variable adsr_;
    Variable filterOn_;
    Variable pitchOffset_;
  };

  VoiceVariables &GetVoiceVariables(int voice);
  static uint16_t GetNoteFrequency(int note);
  static uint16_t GetPitchFrequency(int32_t pitchCents);
  AdvanceSID::Model GetChipModel();
  void NormalizeWaveforms();
  void HardStopOscillators(SIDRenderParams &renderParams);

  static constexpr size_t BaseVariableCount = 28;
  static constexpr size_t VariableCount =
      BaseVariableCount + VoiceParameters::PersistedVariableCount;

  etl::vector<Variable *, VariableCount> variables_;

  //  bool retrig_;
  // int retrigLoop_;
  TableSaveState tableState_;
  // Playback state is shared by channel rather than instrument definition,
  // matching the sample instrument render-parameter ownership model.
#if !defined(HOST_TEST)
  __attribute__((section(".DTCMRAM")))
#endif
  static SIDRenderParams renderParams_[SONG_CHANNEL_COUNT];
  //  bool first_[SONG_CHANNEL_COUNT];
  //  reSID::SID sid_;
  //  reSID::cycle_count delta_t_;
  //  static bool rendered1_;

  VoiceVariables voice1_;
  VoiceVariables voice2_;
  VoiceVariables voice3_;
  Variable table_;
  Variable tableAuto_;

  // These chip settings are shared by all three voices.
  Variable chip_;
  Variable fltcut_;
  Variable fltres_;
  Variable fltmode_;
  Variable vol_;
};

#endif
