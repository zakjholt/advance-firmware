/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "doctest/doctest.h"

#include "Application/Audio/AudioBufferLayout.h"
#include "Application/Audio/AudioSample.h"
#include "Application/Instruments/AdvanceFM6/FM6Voice.h"
#include "Application/Instruments/FM6Instrument.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Externals/etl/include/etl/array.h"
#include <cmath>
#include <cstring>

namespace {

// An all-carrier patch whose 99-rate envelopes release to silence within a
// few render blocks, so voice-end behaviour is reachable deterministically.
fm6::Patch makeFastReleasePatch() {
  fm6::Patch patch{};
  patch.algorithm = 31; // DX7 algorithm 32: every operator is a carrier
  for (auto &op : patch.operators) {
    op.rates = {99, 99, 99, 99};
    op.levels = {99, 99, 99, 0};
    op.outputLevel = 99;
    op.ratioNumerator = 1;
    op.ratioDenominator = 1;
  }
  return patch;
}

// The same patch as a raw 128-byte packed DX7 voice record, for tests that
// exercise the instrument's imported-patch path.
etl::array<uint8_t, fm6::Dx7PatchDataSize>
makeFastReleaseRecord(const char *name = "FASTREL   ") {
  etl::array<uint8_t, fm6::Dx7PatchDataSize> voice{};
  for (size_t op = 0; op < fm6::OperatorCount; ++op) {
    uint8_t *opData = voice.data() + op * 17;
    opData[0] = opData[1] = opData[2] = opData[3] = 99; // EG rates
    opData[4] = opData[5] = opData[6] = 99;             // EG levels
    opData[7] = 0;                                      // release to silence
    opData[12] = 0x38;                                  // detune 0
    opData[14] = 99;                                    // output level
    opData[15] = 0x02; // ratio mode, coarse = 1
  }
  voice[110] = 31; // DX7 algorithm 32: every operator is a carrier
  voice[117] = 24; // transpose 0
  std::memcpy(voice.data() + fm6::Dx7VoiceNameOffset, name,
              fm6::Dx7VoiceNameLength);
  return voice;
}

void checkPatchesEqual(const fm6::Patch &actual, const fm6::Patch &expected) {
  CHECK(actual.algorithm == expected.algorithm);
  CHECK(actual.feedback == expected.feedback);
  CHECK(actual.transpose == expected.transpose);
  for (size_t op = 0; op < fm6::OperatorCount; ++op) {
    const fm6::OperatorPatch &a = actual.operators[op];
    const fm6::OperatorPatch &e = expected.operators[op];
    CHECK(a.rates == e.rates);
    CHECK(a.levels == e.levels);
    CHECK(a.breakPoint == e.breakPoint);
    CHECK(a.leftDepth == e.leftDepth);
    CHECK(a.rightDepth == e.rightDepth);
    CHECK(a.leftCurve == e.leftCurve);
    CHECK(a.rightCurve == e.rightCurve);
    CHECK(a.rateScaling == e.rateScaling);
    CHECK(a.keyVelocitySensitivity == e.keyVelocitySensitivity);
    CHECK(a.outputLevel == e.outputLevel);
    CHECK(a.ratioNumerator == e.ratioNumerator);
    CHECK(a.ratioDenominator == e.ratioDenominator);
    CHECK(a.frequencyCoarse == e.frequencyCoarse);
    CHECK(a.frequencyFine == e.frequencyFine);
    CHECK(a.detune == e.detune);
    CHECK(a.fixedFrequency == e.fixedFrequency);
  }
}

// The seven hex-slice patch variables, in record order.
constexpr FourCC::enum_type kPatchVariableIds[] = {
    FourCC::FM6InstrumentOp1Params,    FourCC::FM6InstrumentOp2Params,
    FourCC::FM6InstrumentOp3Params,    FourCC::FM6InstrumentOp4Params,
    FourCC::FM6InstrumentOp5Params,    FourCC::FM6InstrumentOp6Params,
    FourCC::FM6InstrumentGlobalParams,
};

} // namespace

TEST_CASE("FM6 instrument exposes the phase 1 skeleton variables") {
  FM6Instrument instrument;

  CHECK(instrument.Init());
  CHECK(instrument.GetType() == IT_FM6);
  CHECK_FALSE(instrument.IsEmpty());
  CHECK(instrument.FindVariable(FourCC::FM6InstrumentProgram) != nullptr);
  CHECK(instrument.FindVariable("bank") == nullptr);
  CHECK(instrument.FindVariable(FourCC::FM6InstrumentTable) != nullptr);
  CHECK(instrument.FindVariable(FourCC::FM6InstrumentTableAutomation) !=
        nullptr);
  // The patch is stored as seven hex-encoded record slices; operator IDs
  // are contiguous so they can be computed from the first one.
  for (const auto id : kPatchVariableIds) {
    CHECK(instrument.FindVariable(id) != nullptr);
  }
  CHECK(FourCC::FM6InstrumentOp6Params ==
        FourCC::FM6InstrumentOp1Params + (fm6::OperatorCount - 1));
  // The old per-parameter variables are gone.
  CHECK(instrument.FindVariable("algorithm") == nullptr);
  CHECK(instrument.FindVariable("feedback") == nullptr);
  CHECK(instrument.FindVariable("transpose") == nullptr);
  CHECK(instrument.FindVariable("op1rate1") == nullptr);
  CHECK(instrument.GetTable() == VAR_OFF);
  CHECK_FALSE(instrument.GetTableAutomation());
}

TEST_CASE("Freshly assigned FM6 instrument reports no modified variables") {
  // Regression: switching an instrument slot to FM6 must not leave any variable
  // looking modified, otherwise the InstrumentView shows a bogus "lose
  // settings?" confirmation immediately after switching to the type. The FM6's
  // infinite amp release (0xFF) is its default state, not a user edit.
  static InstrumentBank bank;
  bank.Reset();

  static constexpr uint8_t instrumentID = 0;
  REQUIRE(bank.GetNextAndAssignID(IT_FM6, instrumentID) == instrumentID);
  I_Instrument *instrument = bank.GetInstrument(instrumentID);
  REQUIRE(instrument != nullptr);

  for (Variable *var : *instrument->Variables()) {
    CAPTURE(FourCC(var->GetID()).c_str());
    CHECK_FALSE(var->IsModified());
  }
}

TEST_CASE("FM6 instruments keep program selections independently") {
  FM6Instrument first;
  FM6Instrument second;

  first.FindVariable(FourCC::FM6InstrumentProgram)->SetInt(31, false);
  second.FindVariable(FourCC::FM6InstrumentProgram)->SetInt(7, false);

  CHECK(first.FindVariable(FourCC::FM6InstrumentProgram)->GetInt() == 31);
  CHECK(second.FindVariable(FourCC::FM6InstrumentProgram)->GetInt() == 7);
}

TEST_CASE("FM6 patch variables follow presets and imported patches") {
  FM6Instrument instrument;

  // A fresh instrument's variables hold the first built-in preset.
  fm6::Patch built;
  instrument.BuildPatch(built);
  checkPatchesEqual(built, fm6::GetBuiltInPatch(0));

  instrument.SetBuiltInPatch(5);
  CHECK(instrument.FindVariable(FourCC::FM6InstrumentProgram)->GetInt() == 5);
  instrument.BuildPatch(built);
  checkPatchesEqual(built, fm6::GetBuiltInPatch(5));

  // Importing a record loads its slices into the variables; building runs
  // the same parser as a direct conversion, so the patches are identical.
  const auto record = makeFastReleaseRecord();
  fm6::Patch expected;
  fm6::ConvertDx7Patch(record.data(), expected);
  instrument.ImportDx7Patch(record.data());
  instrument.BuildPatch(built);
  checkPatchesEqual(built, expected);

  // Selecting a preset again replaces the imported patch.
  instrument.SetBuiltInPatch(9);
  CHECK(instrument.FindVariable(FourCC::FM6InstrumentProgram)->GetInt() == 9);
  instrument.BuildPatch(built);
  checkPatchesEqual(built, fm6::GetBuiltInPatch(9));
}

TEST_CASE("FM6 patch variables round-trip through their string values") {
  const auto record = makeFastReleaseRecord();
  FM6Instrument source;
  source.ImportDx7Patch(record.data());

  // The persistence path in miniature: read each variable's string and set
  // it onto a fresh instrument.
  FM6Instrument restored;
  for (const auto id : kPatchVariableIds) {
    Variable *from = source.FindVariable(id);
    Variable *to = restored.FindVariable(id);
    REQUIRE(from != nullptr);
    REQUIRE(to != nullptr);
    to->SetString(from->GetString().c_str());
  }

  fm6::Patch expected;
  fm6::ConvertDx7Patch(record.data(), expected);
  fm6::Patch built;
  restored.BuildPatch(built);
  checkPatchesEqual(built, expected);
}

TEST_CASE("FM6 build falls back to preset 0 for malformed slice values") {
  const auto record = makeFastReleaseRecord();
  FM6Instrument instrument;
  instrument.ImportDx7Patch(record.data());

  // Corrupt one operator slice with non-hex characters and the global slice
  // with a wrong-length value; only those slices fall back to preset 0.
  char nonHex[(2 * FM6Instrument::OperatorSliceSize) + 1];
  std::memset(nonHex, 'Z', sizeof(nonHex) - 1);
  nonHex[sizeof(nonHex) - 1] = '\0';
  instrument.FindVariable(FourCC::FM6InstrumentOp1Params)->SetString(nonHex);
  instrument.FindVariable(FourCC::FM6InstrumentGlobalParams)->SetString("00");

  etl::array<uint8_t, fm6::Dx7PatchDataSize> expectedRecord{};
  std::memcpy(expectedRecord.data(), record.data(), record.size());
  const uint8_t *preset0 = fm6::GetBuiltInPatchRecord(0);
  std::memcpy(expectedRecord.data(), preset0, FM6Instrument::OperatorSliceSize);
  std::memcpy(expectedRecord.data() +
                  (fm6::OperatorCount * FM6Instrument::OperatorSliceSize),
              preset0 + (fm6::OperatorCount * FM6Instrument::OperatorSliceSize),
              FM6Instrument::GlobalSliceSize);

  fm6::Patch expected;
  fm6::ConvertDx7Patch(expectedRecord.data(), expected);
  fm6::Patch built;
  instrument.BuildPatch(built);
  checkPatchesEqual(built, expected);
}

TEST_CASE("FM6 decode masks high-bit bytes to the 7-bit packed-DX7 range") {
  FM6Instrument instrument;

  // 0xFF bytes are masked to the 7-bit packed-DX7 range on decode.
  char allF[(2 * FM6Instrument::OperatorSliceSize) + 1];
  std::memset(allF, 'F', sizeof(allF) - 1);
  allF[sizeof(allF) - 1] = '\0';
  instrument.FindVariable(FourCC::FM6InstrumentOp1Params)->SetString(allF);

  etl::array<uint8_t, fm6::Dx7PatchDataSize> expectedRecord{};
  std::memcpy(expectedRecord.data(), fm6::GetBuiltInPatchRecord(0),
              fm6::Dx7PatchDataSize);
  std::memset(expectedRecord.data(), 0x7F, FM6Instrument::OperatorSliceSize);

  fm6::Patch expected;
  fm6::ConvertDx7Patch(expectedRecord.data(), expected);
  fm6::Patch built;
  instrument.BuildPatch(built);
  checkPatchesEqual(built, expected);
}

TEST_CASE("FM6 decode treats lower-case hex as invalid and falls back") {
  FM6Instrument instrument;

  // The encoder only ever emits upper-case hex, so a lower-case value is
  // treated as corrupt and the slice falls back to preset 0.
  char lower[(2 * FM6Instrument::OperatorSliceSize) + 1];
  std::memset(lower, 'f', sizeof(lower) - 1);
  lower[sizeof(lower) - 1] = '\0';
  instrument.FindVariable(FourCC::FM6InstrumentOp1Params)->SetString(lower);

  etl::array<uint8_t, fm6::Dx7PatchDataSize> expectedRecord{};
  std::memcpy(expectedRecord.data(), fm6::GetBuiltInPatchRecord(0),
              fm6::Dx7PatchDataSize);

  fm6::Patch expected;
  fm6::ConvertDx7Patch(expectedRecord.data(), expected);
  fm6::Patch built;
  instrument.BuildPatch(built);
  checkPatchesEqual(built, expected);
}

TEST_CASE("FM6 patch variables track modification and reset to preset 0") {
  FM6Instrument instrument;
  for (const auto id : kPatchVariableIds) {
    CHECK_FALSE(instrument.FindVariable(id)->IsModified());
  }

  const auto record = makeFastReleaseRecord();
  instrument.ImportDx7Patch(record.data());
  bool anyModified = false;
  for (const auto id : kPatchVariableIds) {
    anyModified = anyModified || instrument.FindVariable(id)->IsModified();
  }
  CHECK(anyModified);

  // Purge resets every variable to its default, restoring preset 0.
  instrument.Purge();
  fm6::Patch built;
  instrument.BuildPatch(built);
  checkPatchesEqual(built, fm6::GetBuiltInPatch(0));
  for (const auto id : kPatchVariableIds) {
    CHECK_FALSE(instrument.FindVariable(id)->IsModified());
  }
}

TEST_CASE("FM6 instrument clone preserves an imported patch") {
  static InstrumentBank bank;
  bank.Reset();

  static constexpr uint8_t instrumentID = 0;
  REQUIRE(bank.GetNextAndAssignID(IT_FM6, instrumentID) == instrumentID);
  I_Instrument *instrument = bank.GetInstrument(instrumentID);
  REQUIRE(instrument != nullptr);
  REQUIRE(instrument->GetType() == IT_FM6);

  const auto record = makeFastReleaseRecord();
  static_cast<FM6Instrument *>(instrument)->ImportDx7Patch(record.data());

  const unsigned short cloneID = bank.Clone(instrumentID);
  REQUIRE(cloneID != NO_MORE_INSTRUMENT);
  I_Instrument *clone = bank.GetInstrument(cloneID);
  REQUIRE(clone != nullptr);
  REQUIRE(clone->GetType() == IT_FM6);

  // Cloning copies the string variables' content (not just the value
  // unions), so the clone rebuilds the imported patch byte-for-byte.
  fm6::Patch expected;
  fm6::ConvertDx7Patch(record.data(), expected);
  fm6::Patch built;
  static_cast<FM6Instrument *>(clone)->BuildPatch(built);
  checkPatchesEqual(built, expected);

  bank.Reset();
}

TEST_CASE("FM6 instrument renders packed mono audio") {
  FM6Instrument instrument;
  etl::array<AudioSample, 128> buffer{};
  buffer.fill(kAudioSampleZero);

  REQUIRE(instrument.Init());
  CHECK(instrument.GetAudioBufferLayout(0) == AudioBufferLayout::MonoPacked);
  REQUIRE(instrument.Start(0, 60, true));
  REQUIRE(instrument.Render(0, buffer.data(), 64, false));

  AudioSample total = 0.0f;
  for (size_t sample = 0; sample < 64; ++sample) {
    total += std::abs(buffer[sample]);
  }
  CHECK(total > 0.01f);
  // Packed mono writes exactly `size` samples.
  for (size_t sample = 64; sample < buffer.size(); ++sample) {
    CHECK(buffer[sample] == kAudioSampleZero);
  }

  instrument.Stop(0);
  CHECK_FALSE(instrument.Render(0, buffer.data(), 4, false));
}

TEST_CASE("FM6 program selection changes rendered timbre") {
  FM6Instrument programZero;
  FM6Instrument programThree;
  etl::array<AudioSample, 128> bufferZero{};
  etl::array<AudioSample, 128> bufferThree{};

  REQUIRE(programZero.Init());
  REQUIRE(programThree.Init());

  programZero.SetBuiltInPatch(0);
  programThree.SetBuiltInPatch(3);

  REQUIRE(programZero.Start(0, 60, true));
  REQUIRE(programThree.Start(1, 60, true));
  REQUIRE(programZero.Render(0, bufferZero.data(), 64, false));
  REQUIRE(programThree.Render(1, bufferThree.data(), 64, false));

  bool differs = false;
  for (size_t index = 0; index < bufferZero.size(); ++index) {
    if (std::abs(bufferZero[index] - bufferThree[index]) > 1.0e-5f) {
      differs = true;
      break;
    }
  }

  CHECK(differs);
}

TEST_CASE("New FM6 instruments default to an infinite voice amp release") {
  static InstrumentBank bank;
  bank.Reset();

  static constexpr uint8_t instrumentID = 0;
  REQUIRE(bank.GetNextAndAssignID(IT_FM6, instrumentID) == instrumentID);
  I_Instrument *instrument = bank.GetInstrument(instrumentID);
  REQUIRE(instrument != nullptr);
  CHECK(InstrumentTypeUsesModernAudioVoice(instrument->GetType()));

  // Like SID, the DX7 patch envelopes control the complete audible release,
  // so the audio voice amp envelope must not cut the tail on note-off.
  Variable *ampRelease = instrument->FindVariable(FourCC::VoiceAmpRelease);
  REQUIRE(ampRelease != nullptr);
  CHECK(ampRelease->GetInt() == 0xFF);

  bank.Reset();
}

TEST_CASE("FM6 legato retunes operators without restarting envelopes") {
  fm6::VoiceState voice{};
  fm6::StartVoice(voice, makeFastReleasePatch(), 60);

  etl::array<int16_t, 128> renderBuffer{};
  REQUIRE(fm6::RenderVoice(voice, renderBuffer.data(), renderBuffer.size()));

  const fm6::VoiceState before = voice;
  fm6::SetVoiceNote(voice, 72);

  CHECK(voice.active);
  CHECK(voice.note == 72);
  for (size_t op = 0; op < fm6::OperatorCount; ++op) {
    // An octave up raises every ratio-mode operator's phase step.
    CHECK(voice.operators[op].phaseStep > before.operators[op].phaseStep);
    // The running envelope and oscillator state must not restart.
    CHECK(voice.operators[op].phase == before.operators[op].phase);
    CHECK(voice.operators[op].gainQ24 == before.operators[op].gainQ24);
    CHECK(voice.operators[op].envelope.levelQ24 ==
          before.operators[op].envelope.levelQ24);
    CHECK(voice.operators[op].envelope.stageIndex ==
          before.operators[op].envelope.stageIndex);
    CHECK(voice.operators[op].envelope.keyDown ==
          before.operators[op].envelope.keyDown);
  }

  // A released, inactive voice ignores legato retunes.
  fm6::StopVoice(voice);
  fm6::SetVoiceNote(voice, 60);
  CHECK_FALSE(voice.active);
  CHECK(voice.note == 0);
}

TEST_CASE("FM6 applies the AudioVoice pitch multiplier without restarting") {
  fm6::VoiceState voice{};
  fm6::StartVoice(voice, makeFastReleasePatch(), 60);

  const fm6::VoiceState before = voice;
  fm6::SetVoicePitchMultiplier(voice, 2.0f);

  CHECK(voice.pitchCommandQ24 == (1 << 24));
  for (size_t op = 0; op < fm6::OperatorCount; ++op) {
    CHECK(static_cast<double>(voice.operators[op].phaseStep) /
              before.operators[op].phaseStep ==
          doctest::Approx(2.0).epsilon(0.0001));
    CHECK(voice.operators[op].phase == before.operators[op].phase);
    CHECK(voice.operators[op].envelope.levelQ24 ==
          before.operators[op].envelope.levelQ24);
    CHECK(voice.operators[op].envelope.stageIndex ==
          before.operators[op].envelope.stageIndex);
  }
}

TEST_CASE("FM6 preserves the minimum combined AudioVoice pitch multiplier") {
  fm6::VoiceState voice{};
  fm6::StartVoice(voice, makeFastReleasePatch(), 60);

  // PSL and LEG each reach -128 semitones; PFT reaches -127/128 of a
  // semitone. Their positive combined ratio is below 1e-6 and must not be
  // replaced by the non-positive-input fallback.
  constexpr float minimumSemitones = -128.0f - 128.0f - (127.0f / 128.0f);
  const float multiplier = std::pow(2.0f, minimumSemitones / 12.0f);
  fm6::SetVoicePitchMultiplier(voice, multiplier);

  constexpr float q24PerOctave = static_cast<float>(1 << 24);
  const int32_t expectedPitch = static_cast<int32_t>(
      std::lround(minimumSemitones / 12.0f * q24PerOctave));
  CHECK(std::abs(voice.pitchCommandQ24 - expectedPitch) < 64);
}

TEST_CASE("FM6 legato start keeps the sounding voice running") {
  FM6Instrument legato;
  FM6Instrument fresh;
  etl::array<AudioSample, 128> legatoBuffer{};
  etl::array<AudioSample, 128> freshBuffer{};

  REQUIRE(legato.Init());
  REQUIRE(fresh.Init());

  REQUIRE(legato.Start(0, 60, true));
  REQUIRE(legato.Render(0, legatoBuffer.data(), 128, false));

  // Non-retrigger start on the active voice changes pitch mid-envelope, so
  // its output differs from a freshly triggered voice at the same note.
  REQUIRE(legato.Start(0, 72, false));
  REQUIRE(fresh.Start(1, 72, true));
  REQUIRE(legato.Render(0, legatoBuffer.data(), 128, false));
  REQUIRE(fresh.Render(1, freshBuffer.data(), 128, false));

  bool differs = false;
  for (size_t index = 0; index < legatoBuffer.size(); ++index) {
    if (std::abs(legatoBuffer[index] - freshBuffer[index]) > 1.0e-5f) {
      differs = true;
      break;
    }
  }
  CHECK(differs);

  legato.Stop(0);
  fresh.Stop(1);

  // A legato start on an inactive voice still starts the note.
  REQUIRE(legato.Start(0, 60, false));
  legatoBuffer.fill(kAudioSampleZero);
  REQUIRE(legato.Render(0, legatoBuffer.data(), 128, false));
  AudioSample total = 0.0f;
  for (const AudioSample sample : legatoBuffer) {
    total += std::abs(sample);
  }
  CHECK(total > 0.01f);
  legato.Stop(0);
}

TEST_CASE("FM6 render writes every sample of a slice until the voice ends") {
  const auto record = makeFastReleaseRecord();

  FM6Instrument instrument;
  REQUIRE(instrument.Init());
  instrument.ImportDx7Patch(record.data());
  REQUIRE(instrument.Start(0, 60, true));

  // 96 is not a multiple of the 64-sample FM render block, so the voice's end
  // can land inside a slice and exercise the zero-filled remainder.
  constexpr size_t sliceSize = 96;
  constexpr AudioSample sentinel = 12345.0f;
  etl::array<AudioSample, sliceSize> buffer{};

  buffer.fill(sentinel);
  REQUIRE(instrument.Render(0, buffer.data(), sliceSize, false));
  instrument.Release(0);

  bool finished = false;
  for (int slice = 0; slice < 200 && !finished; ++slice) {
    buffer.fill(sentinel);
    if (!instrument.Render(0, buffer.data(), sliceSize, false)) {
      finished = true;
      break;
    }
    // Every slice reported as rendered must overwrite all of its samples,
    // either with audio or with the silent tail after the voice ends.
    bool fullyWritten = true;
    for (const AudioSample sample : buffer) {
      fullyWritten = fullyWritten && sample != sentinel;
    }
    CHECK(fullyWritten);
  }
  CHECK(finished);
}
