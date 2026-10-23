/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "FM6Instrument.h"

#include "Application/Utils/char.h"
#include "Externals/etl/include/etl/array.h"
#include "System/Console/n_assert.h"
#include <algorithm>
#include <cstring>

namespace {

// Offset of the global block (pitch EG, algorithm, feedback/sync, LFO,
// transpose) inside a packed DX7 voice record, right after the six
// 17-byte operator blocks.
constexpr size_t kGlobalSliceOffset =
    fm6::OperatorCount * FM6Instrument::OperatorSliceSize;

// Offset of the algorithm byte inside the global slice (record byte 110).
constexpr size_t kAlgorithmOffset = 8;

void encodeSlice(const uint8_t *bytes, size_t byteCount, char *out) {
  for (size_t index = 0; index < byteCount; ++index) {
    hex2char(bytes[index], out + (2 * index));
  }
}

// Decodes a single uppercase hex digit, matching hex2char's encoding.
int32_t hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

// Hex slices of built-in preset 0, used as the variables' construction
// defaults (so Reset() restores preset 0) and as the decode fallback for
// malformed values.
struct DefaultSlices {
  char operators[fm6::OperatorCount][2 * FM6Instrument::OperatorSliceSize + 1];
  char globals[2 * FM6Instrument::GlobalSliceSize + 1];

  DefaultSlices() {
    const uint8_t *record = fm6::GetBuiltInPatchRecord(0);
    for (size_t op = 0; op < fm6::OperatorCount; ++op) {
      encodeSlice(record + (op * FM6Instrument::OperatorSliceSize),
                  FM6Instrument::OperatorSliceSize, operators[op]);
    }
    encodeSlice(record + kGlobalSliceOffset, FM6Instrument::GlobalSliceSize,
                globals);
  }
};

DefaultSlices &defaultSlices() {
  static DefaultSlices slices;
  return slices;
}

// Decodes one hex-encoded slice variable into `bytes`. Guards against a
// corrupt or truncated save: a value of the wrong length or with a non-hex
// character falls back to the preset-0 slice, and every byte is masked to the
// 7-bit packed-DX7 range.
void decodeSlice(Variable &variable, const char *fallback, size_t byteCount,
                 uint8_t *bytes) {
  const etl::string<MAX_VARIABLE_STRING_LENGTH> value = variable.GetString();
  const char *text = value.c_str();
  bool valid = value.size() == 2 * byteCount;
  for (size_t index = 0; valid && index < 2 * byteCount; ++index) {
    valid = hexNibble(text[index]) >= 0;
  }
  if (!valid) {
    text = fallback;
  }
  for (size_t index = 0; index < byteCount; ++index) {
    bytes[index] = static_cast<uint8_t>((hexNibble(text[2 * index]) << 4) |
                                        hexNibble(text[(2 * index) + 1])) &
                   0x7FU;
  }
}

} // namespace

etl::array<FM6Instrument::FM6RenderParams, SONG_CHANNEL_COUNT>
    FM6Instrument::renderParams_{};

FM6Instrument::FM6Instrument()
    : I_Instrument(&variables_), program_(FourCC::FM6InstrumentProgram, 0),
      table_(FourCC::FM6InstrumentTable, VAR_OFF),
      tableAuto_(FourCC::FM6InstrumentTableAutomation, false),
      op1Params_(FourCC::FM6InstrumentOp1Params, defaultSlices().operators[0]),
      op2Params_(FourCC::FM6InstrumentOp2Params, defaultSlices().operators[1]),
      op3Params_(FourCC::FM6InstrumentOp3Params, defaultSlices().operators[2]),
      op4Params_(FourCC::FM6InstrumentOp4Params, defaultSlices().operators[3]),
      op5Params_(FourCC::FM6InstrumentOp5Params, defaultSlices().operators[4]),
      op6Params_(FourCC::FM6InstrumentOp6Params, defaultSlices().operators[5]),
      globalParams_(FourCC::FM6InstrumentGlobalParams, defaultSlices().globals),
      previewPatch_(), previewActive_(false), initialized_(false) {
  variables_.push_back(&program_);
  variables_.push_back(&table_);
  variables_.push_back(&tableAuto_);
  variables_.push_back(&op1Params_);
  variables_.push_back(&op2Params_);
  variables_.push_back(&op3Params_);
  variables_.push_back(&op4Params_);
  variables_.push_back(&op5Params_);
  variables_.push_back(&op6Params_);
  variables_.push_back(&globalParams_);
  tableState_.Reset();
}

FM6Instrument::~FM6Instrument() {}

bool FM6Instrument::Init() {
  fm6::InitializeEngine();
  for (auto &params : renderParams_) {
    fm6::StopVoice(params.voice);
  }
  tableState_.Reset();
  initialized_ = true;
  return initialized_;
}

Variable &FM6Instrument::operatorParams(size_t opIndex) {
  Variable *operators[] = {&op1Params_, &op2Params_, &op3Params_,
                           &op4Params_, &op5Params_, &op6Params_};
  return *operators[opIndex];
}

void FM6Instrument::applyRecord(const uint8_t *record) {
  char buffer[(2 * OperatorSliceSize) + 1];
  for (size_t op = 0; op < fm6::OperatorCount; ++op) {
    encodeSlice(record + (op * OperatorSliceSize), OperatorSliceSize, buffer);
    operatorParams(op).SetString(buffer, false);
  }
  encodeSlice(record + kGlobalSliceOffset, GlobalSliceSize, buffer);
  globalParams_.SetString(buffer, false);
  SetChanged();
  NotifyObservers();
}

void FM6Instrument::BuildPatch(fm6::Patch &patch) {
  uint8_t record[fm6::Dx7PatchDataSize];
  for (size_t op = 0; op < fm6::OperatorCount; ++op) {
    decodeSlice(operatorParams(op), defaultSlices().operators[op],
                OperatorSliceSize, record + (op * OperatorSliceSize));
  }
  decodeSlice(globalParams_, defaultSlices().globals, GlobalSliceSize,
              record + kGlobalSliceOffset);
  // ConvertDx7Patch never reads the name bytes, but keep the staging record
  // fully initialized.
  std::memset(record + fm6::Dx7VoiceNameOffset, ' ', fm6::Dx7VoiceNameLength);
  fm6::ConvertDx7Patch(record, patch);
}

void FM6Instrument::SetBuiltInPatch(int32_t program) {
  program_.SetInt(program, false);
  applyRecord(fm6::GetBuiltInPatchRecord(program));
}

void FM6Instrument::ImportDx7Patch(const uint8_t *record) {
  applyRecord(record);
}

uint8_t FM6Instrument::GetPatchByte(size_t sliceIndex, size_t offset) {
  // sliceIndex is an internal selector (0-5 operator, GlobalSliceIndex
  // global), never user data, so an out-of-range value is a coding error.
  NAssert(sliceIndex <= GlobalSliceIndex);
  const bool global = sliceIndex == GlobalSliceIndex;
  const size_t byteCount = global ? GlobalSliceSize : OperatorSliceSize;
  if (offset >= byteCount) {
    return 0;
  }
  // OperatorSliceSize is the larger of the two, so one buffer serves both.
  uint8_t bytes[OperatorSliceSize];
  decodeSlice(global ? globalParams_ : operatorParams(sliceIndex),
              global ? defaultSlices().globals
                     : defaultSlices().operators[sliceIndex],
              byteCount, bytes);
  return bytes[offset];
}

void FM6Instrument::SetPatchByte(size_t sliceIndex, size_t offset,
                                 uint8_t value) {
  // sliceIndex is an internal selector (0-5 operator, GlobalSliceIndex
  // global), never user data, so an out-of-range value is a coding error.
  NAssert(sliceIndex <= GlobalSliceIndex);
  const bool global = sliceIndex == GlobalSliceIndex;
  const size_t byteCount = global ? GlobalSliceSize : OperatorSliceSize;
  if (offset >= byteCount) {
    return;
  }
  Variable &slice = global ? globalParams_ : operatorParams(sliceIndex);
  uint8_t bytes[OperatorSliceSize];
  decodeSlice(slice,
              global ? defaultSlices().globals
                     : defaultSlices().operators[sliceIndex],
              byteCount, bytes);
  bytes[offset] = value & 0x7FU;
  char buffer[(2 * OperatorSliceSize) + 1];
  encodeSlice(bytes, byteCount, buffer);
  slice.SetString(buffer);
}

uint8_t FM6Instrument::GetAlgorithm() {
  return GetPatchByte(GlobalSliceIndex, kAlgorithmOffset) & 0x1FU;
}

void FM6Instrument::SetAlgorithm(uint8_t algorithm) {
  SetPatchByte(GlobalSliceIndex, kAlgorithmOffset, algorithm & 0x1FU);
}

void FM6Instrument::SetPreviewPatch(const uint8_t *record) {
  fm6::ConvertDx7Patch(record, previewPatch_);
  previewActive_ = true;
}

void FM6Instrument::ClearPreviewPatch() { previewActive_ = false; }

bool FM6Instrument::Start(int channel, unsigned char note, bool retrigger) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT) {
    return false;
  }

  // A legato start retunes the sounding voice (which keeps its captured patch
  // copy) without restarting the operator envelopes, matching the SID
  // instrument's non-retrigger behaviour. An inactive voice still gets a full
  // start so legato phrases can begin from silence.
  if (!retrigger && renderParams_[channel].voice.active) {
    fm6::SetVoiceNote(renderParams_[channel].voice, note);
    return true;
  }

  if (previewActive_) {
    fm6::StartVoice(renderParams_[channel].voice, previewPatch_, note);
    return true;
  }

  fm6::Patch patch;
  BuildPatch(patch);
  fm6::StartVoice(renderParams_[channel].voice, patch, note);
  return true;
}

void FM6Instrument::Stop(int channel) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT) {
    return;
  }

  fm6::StopVoice(renderParams_[channel].voice);
}

void FM6Instrument::Release(int channel) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT) {
    return;
  }

  fm6::ReleaseVoice(renderParams_[channel].voice);
}

PICOTRACKER_FAST_CODE("fm6_instrument_render")
bool FM6Instrument::Render(int channel, AudioSample *buffer, int size,
                           bool updateTick) {
  (void)updateTick;

  if (channel < 0 || channel >= SONG_CHANNEL_COUNT || !buffer || size <= 0) {
    return false;
  }

  etl::array<int16_t, fm6::RenderBlockSampleCount> monoBuffer{};
  size_t frameOffset = 0;
  bool rendered = false;

  while (frameOffset < static_cast<size_t>(size)) {
    const size_t chunkSize =
        std::min(monoBuffer.size(), static_cast<size_t>(size) - frameOffset);
    if (!fm6::RenderVoice(renderParams_[channel].voice, monoBuffer.data(),
                          chunkSize)) {
      if (!rendered) {
        return false;
      }
      // The voice finished mid-slice; the AudioVoice chain still processes
      // every sample of the slice, so silence the remainder rather than leave
      // stale scratch data in the buffer. The next slice returns false and
      // tears the voice down.
      for (size_t i = frameOffset; i < static_cast<size_t>(size); ++i) {
        buffer[i] = 0;
      }
      return true;
    }

    rendered = true;
    for (size_t i = 0; i < chunkSize; ++i) {
      buffer[frameOffset + i] =
          static_cast<AudioSample>(monoBuffer[i]) / kAudioPcmScale;
    }

    frameOffset += chunkSize;
  }

  return rendered;
}

void FM6Instrument::SetPitchMultiplier(int channel, float multiplier) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT) {
    return;
  }
  fm6::SetVoicePitchMultiplier(renderParams_[channel].voice, multiplier);
}

PICOTRACKER_FAST_CODE("fm6_instrument_buffer_layout")
AudioBufferLayout FM6Instrument::GetAudioBufferLayout(int channel) const {
  (void)channel;
  return AudioBufferLayout::MonoPacked;
}

bool FM6Instrument::IsInitialized() { return initialized_; }

bool FM6Instrument::ShouldAuditionParameterChange(FourCC parameter) const {
  switch (parameter) {
  case FourCC::FM6InstrumentOp1Params:
  case FourCC::FM6InstrumentOp2Params:
  case FourCC::FM6InstrumentOp3Params:
  case FourCC::FM6InstrumentOp4Params:
  case FourCC::FM6InstrumentOp5Params:
  case FourCC::FM6InstrumentOp6Params:
  case FourCC::FM6InstrumentGlobalParams:
    return true;
  default:
    return false;
  }
}

bool FM6Instrument::IsEmpty() { return false; }

void FM6Instrument::ProcessCommand(int channel, TrackerCommand cc,
                                   ushort value) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT) {
    return;
  }

  switch (cc) {
  case TrackerCommand::InstrumentCommandTable:
    table_.SetInt(value);
    break;
  default:
    break;
  }
}

int FM6Instrument::GetTable() { return table_.GetInt(); }

bool FM6Instrument::GetTableAutomation() { return tableAuto_.GetBool(); }

void FM6Instrument::GetTableState(TableSaveState &state) {
  state = tableState_;
}

void FM6Instrument::SetTableState(TableSaveState &state) {
  tableState_ = state;
}

void FM6Instrument::OnStart() {
  for (auto &params : renderParams_) {
    fm6::StopVoice(params.voice);
  }
  tableState_.Reset();
}
