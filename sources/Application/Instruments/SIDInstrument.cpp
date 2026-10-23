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

#include "SIDInstrument.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "CommandList.h"
#include "Externals/etl/include/etl/to_string.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include "I_Instrument.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <cmath>
#include <limits>
#include <string.h>

namespace {

constexpr int SID_WAVEFORM_TEXT_COUNT = DWF_LAST * 2;
constexpr int SID_NOTE_TABLE_FIRST_NOTE = 24;

// The first nine entries are the compact editor values. The final nine retain
// the old display strings so projects saved by earlier builds still load.
const char *sidWaveformText[SID_WAVEFORM_TEXT_COUNT] = {
    "------",
    char_waveform_tri_s "----",
    "--" char_waveform_saw_s "--",
    char_waveform_tri_s char_waveform_saw_s "--",
    "----" char_waveform_pulse_s,
    char_waveform_tri_s "--" char_waveform_pulse_s,
    "--" char_waveform_saw_s char_waveform_pulse_s,
    char_waveform_tri_s char_waveform_saw_s char_waveform_pulse_s,
    char_waveform_noise_s "----",
    "--------",
    char_waveform_tri_s "------",
    "--" char_waveform_saw_s "----",
    char_waveform_tri_s char_waveform_saw_s "----",
    "----" char_waveform_pulse_s "--",
    char_waveform_tri_s "--" char_waveform_pulse_s "--",
    "--" char_waveform_saw_s char_waveform_pulse_s "--",
    char_waveform_tri_s char_waveform_saw_s char_waveform_pulse_s "--",
    "------" char_waveform_noise_s};
const char *sidFilterModeText[DFM_LAST] = {"LP", "BP", "HP", "Notch"};
const char *sidChipModelText[SID_CHIP_LAST] = {"6581", "8580"};

inline uint16_t scaleSIDFrequency(uint16_t baseFrequency, float multiplier) {
  if (baseFrequency == 0) {
    return 0;
  }

  const float frequency = static_cast<float>(baseFrequency) * multiplier;
  if (frequency <= 1.0f) {
    return 1;
  }
  const uint16_t maximumFrequency = std::numeric_limits<uint16_t>::max();
  if (frequency >= static_cast<float>(maximumFrequency)) {
    return maximumFrequency;
  }
  return static_cast<uint16_t>(std::lround(frequency));
}

inline void updatePitchRegisters(SIDRenderParams &renderParams,
                                 float multiplier) {
  for (int voice = 0; voice < SIDInstrument::VoiceCount; ++voice) {
    const int registerOffset = voice * 7;
    const uint16_t frequency =
        scaleSIDFrequency(renderParams.baseFrequency_[voice], multiplier);
    renderParams.sid_.Register[registerOffset] = frequency & 0xFF;
    renderParams.sid_.Register[registerOffset + 1] = frequency >> 8;
  }
}

} // namespace

SIDRenderParams SIDInstrument::renderParams_[SONG_CHANNEL_COUNT];

SIDInstrument::VoiceVariables::VoiceVariables(FourCC pulseWidthID,
                                              FourCC waveformID, FourCC syncID,
                                              FourCC ringID, FourCC adsrID,
                                              FourCC filterOnID,
                                              FourCC pitchOffsetID)
    : pulseWidth_(pulseWidthID, 0x800),
      waveform_(waveformID, sidWaveformText, SID_WAVEFORM_TEXT_COUNT, DWF_TRI),
      sync_(syncID, false), ring_(ringID, false), adsr_(adsrID, 0x2282),
      filterOn_(filterOnID, false), pitchOffset_(pitchOffsetID, 0) {}

void SIDInstrument::VoiceVariables::AppendTo(
    etl::ivector<Variable *> &variables) {
  variables.push_back(&pulseWidth_);
  variables.push_back(&waveform_);
  variables.push_back(&sync_);
  variables.push_back(&ring_);
  variables.push_back(&adsr_);
  variables.push_back(&filterOn_);
  variables.push_back(&pitchOffset_);
}

SIDInstrument::SIDInstrument()
    : I_Instrument(&variables_),
      voice1_(FourCC::SIDInstrumentPulseWidth, FourCC::SIDInstrumentWaveform,
              FourCC::SIDInstrumentVSync, FourCC::SIDInstrumentRingModulator,
              FourCC::SIDInstrumentADSR, FourCC::SIDInstrumentFilterOn,
              FourCC::SIDInstrumentPitchOffset),
      voice2_(FourCC::SIDInstrument2PulseWidth, FourCC::SIDInstrument2Waveform,
              FourCC::SIDInstrument2VSync, FourCC::SIDInstrument2RingModulator,
              FourCC::SIDInstrument2ADSR, FourCC::SIDInstrument2FilterOn,
              FourCC::SIDInstrument2PitchOffset),
      voice3_(FourCC::SIDInstrument3PulseWidth, FourCC::SIDInstrument3Waveform,
              FourCC::SIDInstrument3VSync, FourCC::SIDInstrument3RingModulator,
              FourCC::SIDInstrument3ADSR, FourCC::SIDInstrument3FilterOn,
              FourCC::SIDInstrument3PitchOffset),
      table_(FourCC::SIDInstrumentTable, -1),
      tableAuto_(FourCC::SIDInstrumentTableAutomation, false),
      chip_(FourCC::SIDInstrumentChipModel, sidChipModelText, SID_CHIP_LAST,
            SID_CHIP_6581),
      fltcut_(FourCC::SIDInstrument1FilterCut, 0x1FF),
      fltres_(FourCC::SIDInstrument1FilterResonance, 0x0),
      fltmode_(FourCC::SIDInstrument1FilterMode, sidFilterModeText, DFM_LAST,
               0x0),
      vol_(FourCC::SIDInstrument1Volume, 0xF) {
  // name_ is now an etl::string in the base class, not a Variable
  voice1_.AppendTo(variables_);
  voice2_.AppendTo(variables_);
  voice3_.AppendTo(variables_);
  variables_.insert(variables_.end(), &table_);
  variables_.insert(variables_.end(), &tableAuto_);
  variables_.insert(variables_.end(), &chip_);
  variables_.insert(variables_.end(), &fltcut_);
  variables_.insert(variables_.end(), &fltres_);
  variables_.insert(variables_.end(), &fltmode_);
  variables_.insert(variables_.end(), &vol_);
}

SIDInstrument::~SIDInstrument(){};

bool SIDInstrument::Init() {
  tableState_.Reset();
  return true;
};

void SIDInstrument::RestoreContent(PersistencyDocument *doc) {
  I_Instrument::RestoreContent(doc);
  NormalizeWaveforms();
}

SIDInstrument::VoiceVariables &SIDInstrument::GetVoiceVariables(int voice) {
  NAssert(voice >= 0 && voice < VoiceCount);
  VoiceVariables *voices[VoiceCount] = {&voice1_, &voice2_, &voice3_};
  return *voices[voice];
}

uint16_t SIDInstrument::GetNoteFrequency(int note) {
  NAssert(note >= LOWEST_NOTE && note <= HIGHEST_NOTE);
  if (note >= SID_NOTE_TABLE_FIRST_NOTE) {
    return sid_notes[note - SID_NOTE_TABLE_FIRST_NOTE];
  }

  // The original SID table starts at the sequencer's C0. Derive its two lower
  // octaves from the same values so the complete sequencer range is playable.
  const int octaveShift = (SID_NOTE_TABLE_FIRST_NOTE - note + 11) / 12;
  const int sourceNote = note + octaveShift * 12;
  return sid_notes[sourceNote - SID_NOTE_TABLE_FIRST_NOTE] >> octaveShift;
}

uint16_t SIDInstrument::GetPitchFrequency(int32_t pitchCents) {
  NAssert(pitchCents >= LOWEST_NOTE * PitchOffset::CentsPerSemitone &&
          pitchCents <= HIGHEST_NOTE * PitchOffset::CentsPerSemitone);

  const int32_t note = pitchCents / PitchOffset::CentsPerSemitone;
  const int32_t cents = pitchCents % PitchOffset::CentsPerSemitone;
  const uint16_t baseFrequency = GetNoteFrequency(note);
  const float frequencyRatio =
      std::pow(2.0f, static_cast<float>(cents) / PitchOffset::CentsPerOctave);
  const uint32_t tunedFrequency =
      static_cast<uint32_t>(baseFrequency * frequencyRatio + 0.5f);
  const uint32_t maximumFrequency = std::numeric_limits<uint16_t>::max();
  return static_cast<uint16_t>(
      tunedFrequency > maximumFrequency ? maximumFrequency : tunedFrequency);
}

AdvanceSID::Model SIDInstrument::GetChipModel() {
  // Default invalid or legacy values to 8580, matching the instrument's
  // behavior before model selection was exposed.
  return chip_.GetInt() == SID_CHIP_6581 ? AdvanceSID::Model::MOS6581
                                         : AdvanceSID::Model::MOS8580;
}

void SIDInstrument::NormalizeWaveforms() {
  for (int voice = 0; voice < VoiceCount; voice++) {
    Variable &waveform = GetVoiceVariables(voice).waveform_;
    const int value = waveform.GetInt();
    if (value >= DWF_LAST && value < SID_WAVEFORM_TEXT_COUNT) {
      waveform.SetInt(value - DWF_LAST, false);
    } else if (value < DWF_NONE || value >= DWF_LAST) {
      waveform.Reset();
    }
  }
}

void SIDInstrument::OnStart() {
  tableState_.Reset();
  for (SIDRenderParams &renderParams : renderParams_) {
    renderParams.Reset();
  }
};

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)                                                   \
  ((byte) & 0x80 ? '1' : '0'), ((byte) & 0x40 ? '1' : '0'),                    \
      ((byte) & 0x20 ? '1' : '0'), ((byte) & 0x10 ? '1' : '0'),                \
      ((byte) & 0x08 ? '1' : '0'), ((byte) & 0x04 ? '1' : '0'),                \
      ((byte) & 0x02 ? '1' : '0'), ((byte) & 0x01 ? '1' : '0')

bool SIDInstrument::Start(int c, unsigned char note, bool retrigger) {
  Trace::Debug("Retrigger: %i", retrigger);
  SIDRenderParams &renderParams = renderParams_[c];
  if (retrigger) {
    renderParams.Reset();
  }
  AdvanceSID &sid = renderParams.sid_;
  const AdvanceSID::Model chipModel = GetChipModel();
  const bool modelChanged = sid.GetModel() != chipModel;
  sid.SetModel(chipModel);
  // Each song channel owns its SID playback state, so the same instrument can
  // render independently on multiple channels.
  const int32_t noteValue = static_cast<int32_t>(note);
  uint8_t filterRouting = 0;
  for (int voice = 0; voice < VoiceCount; voice++) {
    const int channelOffset = voice * 7;
    VoiceVariables &settings = GetVoiceVariables(voice);
    sid.Register[channelOffset + 2] = settings.pulseWidth_.GetInt() & 0xFF;
    sid.Register[channelOffset + 3] = settings.pulseWidth_.GetInt() >> 8;
    sid.Register[channelOffset + 5] = settings.adsr_.GetInt() >> 8;
    sid.Register[channelOffset + 6] = settings.adsr_.GetInt() & 0xFF;

    const int32_t targetPitchCents = noteValue * PitchOffset::CentsPerSemitone +
                                     settings.pitchOffset_.GetInt();
    const bool noteInRange =
        targetPitchCents >= LOWEST_NOTE * PitchOffset::CentsPerSemitone &&
        targetPitchCents <= HIGHEST_NOTE * PitchOffset::CentsPerSemitone;
    if (noteInRange) {
      const uint16_t frequency = GetPitchFrequency(targetPitchCents);
      renderParams.baseFrequency_[voice] = frequency;
      // Switching models resets the chip state, so its first note must open
      // the gate even when the caller requested a legato start.
      const int gate =
          retrigger || modelChanged ? 1 : (sid.Register[channelOffset + 4] & 1);
      sid.Register[channelOffset] = frequency & 0xFF;
      sid.Register[channelOffset + 1] = frequency >> 8;
      sid.Register[channelOffset + 4] = settings.waveform_.GetInt() << 4 |
                                        settings.ring_.GetInt() << 2 |
                                        settings.sync_.GetInt() << 1 | gate;
      filterRouting |= settings.filterOn_.GetInt() << voice;
    } else {
      // An out-of-range chord voice must not retain a previously playing note.
      renderParams.baseFrequency_[voice] = 0;
      sid.Register[channelOffset] = 0;
      sid.Register[channelOffset + 1] = 0;
      sid.Register[channelOffset + 4] = 0;
      sid.ResetChannel(channelOffset);
    }
  }

  // filter settings
  sid.Register[21] = fltcut_.GetInt() & 0x7; // Filter Cut lo
  sid.Register[22] = fltcut_.GetInt() >> 3;  // Filter Cut Hi

  sid.Register[23] = fltres_.GetInt() << 4 | filterRouting;

  int8_t mode = 0;
  switch (fltmode_.GetInt()) {
  case DFM_LP:
    mode = 1;
    break;
  case DFM_BP:
    mode = 2;
    break;
  case DFM_HP:
    mode = 4;
    break;
  case DFM_NOTCH:
    mode = 5;
    break;
  }
  // TODO: implement v3off
  //  sid_->Register[24] =
  //      v3off_.GetInt() << 7 | mode << 4 | vol_->GetInt(); // Filter Mode/Vol

  sid.Register[24] = 0 << 7 | mode << 4 | vol_.GetInt(); // Filter Mode / Vol

  //  for (int n = 0; n < 29; n++) {
  //    printf("Register %i value: 0x%x (0b" BYTE_TO_BINARY_PATTERN ")",
  //    n,
  //           sid_->Register[n], BYTE_TO_BINARY(sid_->Register[n]));
  //  }

  renderParams.playing_ = true;

  return true;
};

void SIDInstrument::Stop(int c) {
  SIDRenderParams &renderParams = renderParams_[c];
  renderParams.playing_ = false;
  HardStopOscillators(renderParams);
};

void SIDInstrument::Release(int channel) {
  SIDRenderParams &renderParams = renderParams_[channel];
  for (int voice = 0; voice < VoiceCount; voice++) {
    renderParams.sid_.Register[4 + voice * 7] &= ~1; // Set gate bit off
  }
};

void SIDInstrument::HardStopOscillators(SIDRenderParams &renderParams) {
  for (int voice = 0; voice < VoiceCount; voice++) {
    renderParams.sid_.Register[4 + voice * 7] = 0;
    renderParams.sid_.ResetChannel(voice * 7);
  }
  renderParams.sid_.Register[23] &= ~0x7;
}

PICOTRACKER_FAST_CODE("sid_instrument_render")
bool SIDInstrument::Render(int channel, AudioSample *buffer, int size,
                           bool updateTick) {
  SIDRenderParams &renderParams = renderParams_[channel];
  if (renderParams.playing_) {
    // A SID voice is finished only after every gate is closed and its envelope
    // has reached zero. Check before rendering so a false result means this
    // call did not produce a valid audio buffer.
    if (renderParams.sid_.AreVoicesIdle()) {
      renderParams.playing_ = false;
      return false;
    }

    // No need to clear the fixed point buffer: AdvanceSID overwrites every
    // sample.
    renderParams.sid_.Render(buffer, size);

    return true;
  }
  return false;
};

void SIDInstrument::SetPitchMultiplier(int channel, float multiplier) {
  updatePitchRegisters(renderParams_[channel], multiplier);
}

PICOTRACKER_FAST_CODE("sid_instrument_buffer_layout")
AudioBufferLayout SIDInstrument::GetAudioBufferLayout(int channel) const {
  (void)channel;
  return AudioBufferLayout::MonoPacked;
}

bool SIDInstrument::IsInitialized() {
  return true; // Always initialised
};

bool SIDInstrument::ShouldAuditionParameterChange(FourCC parameter) const {
  switch (parameter) {
  case FourCC::SIDInstrumentChipModel:
  case FourCC::SIDInstrument1Volume:
  case FourCC::SIDInstrumentWaveform:
  case FourCC::SIDInstrument2Waveform:
  case FourCC::SIDInstrument3Waveform:
  case FourCC::SIDInstrumentPitchOffset:
  case FourCC::SIDInstrument2PitchOffset:
  case FourCC::SIDInstrument3PitchOffset:
  case FourCC::SIDInstrumentPulseWidth:
  case FourCC::SIDInstrument2PulseWidth:
  case FourCC::SIDInstrument3PulseWidth:
  case FourCC::SIDInstrumentVSync:
  case FourCC::SIDInstrument2VSync:
  case FourCC::SIDInstrument3VSync:
  case FourCC::SIDInstrumentRingModulator:
  case FourCC::SIDInstrument2RingModulator:
  case FourCC::SIDInstrument3RingModulator:
  case FourCC::SIDInstrumentADSR:
  case FourCC::SIDInstrument2ADSR:
  case FourCC::SIDInstrument3ADSR:
  case FourCC::SIDInstrumentFilterOn:
  case FourCC::SIDInstrument2FilterOn:
  case FourCC::SIDInstrument3FilterOn:
  case FourCC::SIDInstrument1FilterCut:
  case FourCC::SIDInstrument1FilterResonance:
  case FourCC::SIDInstrument1FilterMode:
    return true;
  default:
    return false;
  }
}

void SIDInstrument::ProcessCommand(int channel, TrackerCommand cc,
                                   ushort value) {
  (void)channel;
  (void)cc;
  (void)value;
};

etl::string<MAX_INSTRUMENT_NAME_LENGTH> SIDInstrument::GetName() {
  // first check if the name_ string has been explicitly set
  if (!name_.empty()) {
    return name_;
  }
  // otherwise return the default name for this instrument type
  return etl::string<MAX_INSTRUMENT_NAME_LENGTH>(InstrumentTypeNames[IT_SID]);
}

int SIDInstrument::GetTable() {
  Variable *v = FindVariable(FourCC::SIDInstrumentTable);
  return v->GetInt();
};

bool SIDInstrument::GetTableAutomation() {
  Variable *v = FindVariable(FourCC::SIDInstrumentTableAutomation);
  return v->GetBool();
};

void SIDInstrument::GetTableState(TableSaveState &state) {
  memcpy(state.hopCount_, tableState_.hopCount_,
         sizeof(uchar) * TABLE_STEPS * 3);
  memcpy(state.position_, tableState_.position_, sizeof(int) * 3);
  state.groove_ = tableState_.groove_;
};

void SIDInstrument::SetTableState(TableSaveState &state) {
  memcpy(tableState_.hopCount_, state.hopCount_,
         sizeof(uchar) * TABLE_STEPS * 3);
  memcpy(tableState_.position_, state.position_, sizeof(int) * 3);
  tableState_.groove_ = state.groove_;
};
