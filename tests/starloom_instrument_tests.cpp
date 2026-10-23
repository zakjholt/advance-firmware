/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "doctest/doctest.h"

#include "Application/Instruments/StarLoomInstrument.h"
#include "Application/Instruments/StarLoomInstrumentDSP.h"
#include "Application/Instruments/StarLoomInstrumentShapeOptions.h"
#include "Externals/braids/dsp.h"
#include "Externals/braids/signature_waveshaper.h"
#include "System/System/DeviceIdentity.h"
#include <algorithm>
#include <array>

TEST_CASE("StarLoom oscillator render blocks satisfy Braids pair generation") {
  CHECK(StarLoomInstrumentDSP::BraidsRenderSize(0) == 0);
  CHECK(StarLoomInstrumentDSP::BraidsRenderSize(1) == 2);
  CHECK(StarLoomInstrumentDSP::BraidsRenderSize(23) == 24);
  CHECK(StarLoomInstrumentDSP::BraidsRenderSize(24) == 24);
}

TEST_CASE(
    "StarLoom oscillator pitch offsets use cents and clamp Braids pitch") {
  CHECK(StarLoomInstrumentDSP::CalculatePitch(81, 0) == 69 * 128);
  CHECK(StarLoomInstrumentDSP::CalculatePitch(81, 50) == 69 * 128 + 64);
  CHECK(StarLoomInstrumentDSP::CalculatePitch(81, -50) == 69 * 128 - 64);
  CHECK(StarLoomInstrumentDSP::CalculatePitch(81, 100) == 70 * 128);
  CHECK(StarLoomInstrumentDSP::CalculatePitch(81, -100) == 68 * 128);
  CHECK(StarLoomInstrumentDSP::CalculatePitch(81, 1200) == 81 * 128);
  CHECK(StarLoomInstrumentDSP::CalculatePitch(81, -1200) == 57 * 128);
  CHECK(StarLoomInstrumentDSP::CalculatePitch(81, 50, 64) == 70 * 128);
  CHECK(StarLoomInstrumentDSP::CalculatePitch(0, -100) == 0);
  CHECK(StarLoomInstrumentDSP::CalculatePitch(255, 100) ==
        StarLoomInstrumentDSP::kBraidsPitchMax);
}

TEST_CASE("StarLoom oscillator converts AudioVoice pitch multipliers") {
  CHECK(StarLoomInstrumentDSP::PitchMultiplierToPitchUnits(1.0f) == 0);
  CHECK(StarLoomInstrumentDSP::PitchMultiplierToPitchUnits(2.0f) == 12 * 128);
  CHECK(StarLoomInstrumentDSP::PitchMultiplierToPitchUnits(0.5f) == -12 * 128);
}

TEST_CASE("StarLoom oscillator mix keeps both oscillators full at center") {
  CHECK(StarLoomInstrumentDSP::Oscillator1LevelFromMix(-0x7F) == 0x7F);
  CHECK(StarLoomInstrumentDSP::Oscillator2LevelFromMix(-0x7F) == 0);
  CHECK(StarLoomInstrumentDSP::Oscillator1LevelFromMix(0) == 0x7F);
  CHECK(StarLoomInstrumentDSP::Oscillator2LevelFromMix(0) == 0x7F);
  CHECK(StarLoomInstrumentDSP::Oscillator1LevelFromMix(0x7F) == 0);
  CHECK(StarLoomInstrumentDSP::Oscillator2LevelFromMix(0x7F) == 0x7F);

  CHECK(StarLoomInstrumentDSP::Oscillator1LevelFromMix(-0x40) == 0x7F);
  CHECK(StarLoomInstrumentDSP::Oscillator2LevelFromMix(-0x40) == 0x3F);
  CHECK(StarLoomInstrumentDSP::Oscillator1LevelFromMix(0x40) == 0x3F);
  CHECK(StarLoomInstrumentDSP::Oscillator2LevelFromMix(0x40) == 0x7F);

  CHECK(StarLoomInstrumentDSP::Oscillator2LevelFromMix(-0xFF) == 0);
  CHECK(StarLoomInstrumentDSP::Oscillator1LevelFromMix(0xFF) == 0);
}

TEST_CASE("StarLoom oscillator interaction modes combine levelled signals") {
  using StarLoomInstrumentDSP::InteractionMode;

  CHECK(StarLoomInstrumentDSP::ApplyInteraction(12000, -4000, 0x7F, 0x7F,
                                                InteractionMode::Mix) == 8000);
  CHECK(StarLoomInstrumentDSP::ApplyInteraction(
            12000, -4000, 0x7F, 0x7F, InteractionMode::Difference) == 16000);
  CHECK(StarLoomInstrumentDSP::ApplyInteraction(16384, 16384, 0x7F, 0x7F,
                                                InteractionMode::Ring) == 8192);
  CHECK(StarLoomInstrumentDSP::ApplyInteraction(
            16384, -16384, 0x7F, 0x7F, InteractionMode::Ring) == -8192);
  CHECK(StarLoomInstrumentDSP::ApplyInteraction(16384, 16384, 0x7F, 0,
                                                InteractionMode::Ring) == 0);
}

TEST_CASE("StarLoom shape selections map safely through the allow-list") {
  using namespace StarLoomInstrumentShapes;

  for (int selection = 0; selection < kAllowedShapeCount; ++selection) {
    CHECK(ShapeForSelection(selection) ==
          static_cast<braids::MacroOscillatorShape>(
              kAllowedShapeValues[selection]));
  }
  const auto fallback =
      static_cast<braids::MacroOscillatorShape>(kAllowedShapeValues[0]);
  CHECK(ShapeForSelection(-1) == fallback);
  CHECK(ShapeForSelection(kAllowedShapeCount) == fallback);
}

TEST_CASE(
    "StarLoom signature waveshaper transforms samples deterministically") {
  braids::SignatureWaveshaper waveshaper;
  waveshaper.Init(0x1234U);

  CHECK(waveshaper.Transform(-32768) == -32766);
  CHECK(waveshaper.Transform(-16384) == -20779);
  CHECK(waveshaper.Transform(0) == -13825);
  CHECK(waveshaper.Transform(8191) == 11903);
  CHECK(waveshaper.Transform(16384) == 15499);
  CHECK(waveshaper.Transform(32767) == 31989);
}

TEST_CASE("Disabled StarLoom signature preserves Braids zero-balance output") {
  for (int32_t sample = -32768; sample <= 32767; ++sample) {
    CHECK(StarLoomInstrumentDSP::ApplyDisabledSignature(static_cast<int16_t>(
              sample)) == braids::Mix(static_cast<int16_t>(sample),
                                      static_cast<int16_t>(0), 0));
  }
}

TEST_CASE("Device identity seed uses all STM32 UID words deterministically") {
  constexpr uint32_t seed =
      DeviceIdentity::SeedFromUidWords(0x12345678U, 0x9ABCDEF0U, 0x0BADF00DU);

  CHECK(seed == 0xCB7980DCU);
  CHECK(DeviceIdentity::SeedFromUidWords(0x12345679U, 0x9ABCDEF0U,
                                         0x0BADF00DU) != seed);
  CHECK(DeviceIdentity::SeedFromUidWords(0x12345678U, 0x9ABCDEF1U,
                                         0x0BADF00DU) != seed);
  CHECK(DeviceIdentity::SeedFromUidWords(0x12345678U, 0x9ABCDEF0U,
                                         0x0BADF00CU) != seed);
}

TEST_CASE("StarLoom instrument table settings and playback state round trip") {
  StarLoomInstrument instrument;
  REQUIRE(instrument.Init());

  CHECK(instrument.GetTable() == VAR_OFF);
  CHECK_FALSE(instrument.GetTableAutomation());
  instrument.ProcessCommand(0, TrackerCommand::InstrumentCommandTable, 0x12);
  CHECK(instrument.GetTable() == 0x12);

  Variable *automation =
      instrument.FindVariable(FourCC::StarLoomInstrumentTableAutomation);
  REQUIRE(automation != nullptr);
  automation->SetInt(1);
  CHECK(instrument.GetTableAutomation());

  TableSaveState input;
  input.Reset();
  input.position_[1] = 7;
  input.hopCount_[3][2] = 4;
  instrument.SetTableState(input);

  TableSaveState output;
  output.Reset();
  instrument.GetTableState(output);
  CHECK(output.position_[1] == 7);
  CHECK(output.hopCount_[3][2] == 4);

  instrument.OnStart();
  instrument.GetTableState(output);
  CHECK(output.position_[1] == 0);
  CHECK(output.hopCount_[3][2] == 0);
}

TEST_CASE("StarLoom instrument auditions only direct sound parameters") {
  StarLoomInstrument instrument;

  CHECK(
      instrument.ShouldAuditionParameterChange(FourCC::StarLoomInstrumentMix));
  CHECK(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentInteractionMode));
  CHECK(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentSignature));
  CHECK(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentShape));
  CHECK(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentTimbre));
  CHECK(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentColor));
  CHECK(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentOscillator1PitchOffset));
  CHECK(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentOscillator2Shape));
  CHECK(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentOscillator2Timbre));
  CHECK(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentOscillator2Color));
  CHECK(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentOscillator2PitchOffset));

  CHECK_FALSE(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentTable));
  CHECK_FALSE(instrument.ShouldAuditionParameterChange(
      FourCC::StarLoomInstrumentTableAutomation));
  CHECK_FALSE(instrument.ShouldAuditionParameterChange(FourCC::InstrumentName));
}

TEST_CASE(
    "StarLoom instrument renders odd mono blocks on independent channels") {
  constexpr size_t kFrames = 25;
  std::array<AudioSample, kFrames> channel0{};
  std::array<AudioSample, kFrames> channel1{};
  StarLoomInstrument instrument;

  REQUIRE(instrument.Init());
  instrument.Stop(0);
  instrument.Stop(1);
  CHECK_FALSE(instrument.Render(0, channel0.data(), channel0.size(), false));

  REQUIRE(instrument.Start(0, 60, true));
  REQUIRE(instrument.Render(0, channel0.data(), channel0.size(), false));
  CHECK(std::any_of(channel0.begin(), channel0.end(),
                    [](AudioSample sample) { return sample != 0; }));

  REQUIRE(instrument.Start(1, 72, true));
  REQUIRE(instrument.Render(1, channel1.data(), channel1.size(), false));
  CHECK(std::any_of(channel1.begin(), channel1.end(),
                    [](AudioSample sample) { return sample != 0; }));

  instrument.Stop(0);
  CHECK_FALSE(instrument.Render(0, channel0.data(), channel0.size(), false));
  CHECK(instrument.Render(1, channel1.data(), channel1.size(), false));
  instrument.Stop(1);
}
