/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "doctest/doctest.h"

#define private public
#include "Application/Instruments/SIDInstrument.h"
#undef private
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Player/AudioVoice.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include <cstring>

namespace {

void ConfigureFullSIDChip(AdvanceSID &sid) {
  static const uint8_t waveforms[3] = {0x40, 0x36, 0x80};
  for (int voice = 0; voice < 3; voice++) {
    int channel = voice * 7;
    uint16_t frequency = 0x1800 + voice * 0x0711;
    sid.Register[channel] = frequency & 0xFF;
    sid.Register[channel + 1] = frequency >> 8;
    sid.Register[channel + 2] = 0x35 + voice * 0x21;
    sid.Register[channel + 3] = 0x08;
    sid.Register[channel + 4] = waveforms[voice];
    sid.EnvelopeCounter[channel] = 0xFF - voice * 0x21;
  }
  sid.Register[21] = 0x07;
  sid.Register[22] = 0xB0;
  sid.Register[23] = 0xF7;
  sid.Register[24] = 0x7F;
}

uint64_t HashSIDOutputBits(const AudioSample *buffer, int sampleCount) {
  uint64_t hash = 1469598103934665603ULL;
  for (int sample = 0; sample < sampleCount; sample++) {
    uint32_t bits;
    memcpy(&bits, &buffer[sample], sizeof(bits));
    hash ^= bits;
    hash *= 1099511628211ULL;
  }
  return hash;
}

uint16_t ReadSIDFrequency(const AdvanceSID &sid, int voice) {
  const int registerOffset = voice * 7;
  return static_cast<uint16_t>(sid.Register[registerOffset] |
                               (sid.Register[registerOffset + 1] << 8));
}

} // namespace

TEST_CASE("SID instruments own their definition parameters") {
  SIDInstrument first;
  SIDInstrument second;

  REQUIRE(first.Init());
  REQUIRE(second.Init());

  Variable *firstCutoff = first.FindVariable(FourCC::SIDInstrument1FilterCut);
  Variable *secondCutoff = second.FindVariable(FourCC::SIDInstrument1FilterCut);
  REQUIRE(firstCutoff != nullptr);
  REQUIRE(secondCutoff != nullptr);
  CHECK(firstCutoff != secondCutoff);

  firstCutoff->SetInt(0x123);
  CHECK(firstCutoff->GetInt() == 0x123);
  CHECK(secondCutoff->GetInt() == 0x1FF);
}

TEST_CASE("SID instrument applies its selected chip model when starting") {
  SIDInstrument instrument;

  REQUIRE(instrument.Init());
  instrument.OnStart();
  Variable *chip = instrument.FindVariable(FourCC::SIDInstrumentChipModel);
  REQUIRE(chip != nullptr);
  CHECK(chip->GetInt() == SID_CHIP_6581);

  static constexpr int channel = 0;
  REQUIRE(instrument.Start(channel, 60));
  CHECK(SIDInstrument::renderParams_[channel].sid_.GetModel() ==
        AdvanceSID::Model::MOS6581);

  chip->SetInt(SID_CHIP_8580);
  REQUIRE(instrument.Start(channel, 60, false));
  CHECK(SIDInstrument::renderParams_[channel].sid_.GetModel() ==
        AdvanceSID::Model::MOS8580);
  CHECK((SIDInstrument::renderParams_[channel].sid_.Register[4] & 1) == 1);

  instrument.Stop(channel);
}

TEST_CASE("SID instruments render independently") {
  static AudioSample firstBuffer[64];
  static AudioSample secondBuffer[64];
  SIDInstrument first;
  SIDInstrument second;

  REQUIRE(first.Init());
  REQUIRE(second.Init());
  first.OnStart();
  CHECK_FALSE(first.Render(0, firstBuffer, 64, false));
  CHECK_FALSE(second.Render(1, secondBuffer, 64, false));

  REQUIRE(first.Start(0, 60));
  CHECK(first.Render(0, firstBuffer, 64, false));
  CHECK_FALSE(second.Render(1, secondBuffer, 64, false));

  REQUIRE(second.Start(1, 67));
  CHECK(first.Render(0, firstBuffer, 64, false));
  CHECK(second.Render(1, secondBuffer, 64, false));

  first.Stop(0);
  CHECK_FALSE(first.Render(0, firstBuffer, 64, false));
  CHECK(second.Render(1, secondBuffer, 64, false));
  second.Stop(1);
}

TEST_CASE("SID instrument renders packed mono audio") {
  static constexpr int renderSamples = 64;
  static constexpr AudioSample sentinel = 12345.0f;
  static AudioSample buffer[renderSamples * 2];
  SIDInstrument instrument;

  REQUIRE(instrument.Init());
  instrument.OnStart();
  CHECK(instrument.GetAudioBufferLayout(0) == AudioBufferLayout::MonoPacked);
  REQUIRE(instrument.Start(0, 60));

  for (AudioSample &sample : buffer) {
    sample = sentinel;
  }
  REQUIRE(instrument.Render(0, buffer, renderSamples, false));

  for (int sample = 0; sample < renderSamples; ++sample) {
    CHECK(buffer[sample] != sentinel);
    CHECK(buffer[renderSamples + sample] == sentinel);
  }
  instrument.Stop(0);
}

TEST_CASE("one SID instrument renders independent song channel states") {
  static AudioSample firstBuffer[64];
  static AudioSample secondBuffer[64];
  SIDInstrument instrument;

  REQUIRE(instrument.Init());
  instrument.OnStart();

  REQUIRE(instrument.Start(0, 60));
  REQUIRE(instrument.Start(1, 67));

  AdvanceSID &firstSID = SIDInstrument::renderParams_[0].sid_;
  AdvanceSID &secondSID = SIDInstrument::renderParams_[1].sid_;
  CHECK(firstSID.Register[0] == (sid_notes[60 - 24] & 0xFF));
  CHECK(firstSID.Register[1] == (sid_notes[60 - 24] >> 8));
  CHECK(secondSID.Register[0] == (sid_notes[67 - 24] & 0xFF));
  CHECK(secondSID.Register[1] == (sid_notes[67 - 24] >> 8));

  CHECK(instrument.Render(0, firstBuffer, 64, false));
  CHECK(firstSID.PhaseAccu[0] != 0);
  CHECK(secondSID.PhaseAccu[0] == 0);

  CHECK(instrument.Render(1, secondBuffer, 64, false));
  CHECK(secondSID.PhaseAccu[0] != 0);

  instrument.Stop(0);
  CHECK_FALSE(instrument.Render(0, firstBuffer, 64, false));
  CHECK(instrument.Render(1, secondBuffer, 64, false));
  instrument.Stop(1);
}

TEST_CASE("SID instrument programs all voices from independent settings") {
  SIDInstrument instrument;

  REQUIRE(instrument.Init());
  instrument.OnStart();

  static constexpr FourCC::enum_type waveformIDs[] = {
      FourCC::SIDInstrumentWaveform, FourCC::SIDInstrument2Waveform,
      FourCC::SIDInstrument3Waveform};
  static constexpr FourCC::enum_type pulseWidthIDs[] = {
      FourCC::SIDInstrumentPulseWidth, FourCC::SIDInstrument2PulseWidth,
      FourCC::SIDInstrument3PulseWidth};
  static constexpr FourCC::enum_type syncIDs[] = {FourCC::SIDInstrumentVSync,
                                                  FourCC::SIDInstrument2VSync,
                                                  FourCC::SIDInstrument3VSync};
  static constexpr FourCC::enum_type ringIDs[] = {
      FourCC::SIDInstrumentRingModulator, FourCC::SIDInstrument2RingModulator,
      FourCC::SIDInstrument3RingModulator};
  static constexpr FourCC::enum_type adsrIDs[] = {FourCC::SIDInstrumentADSR,
                                                  FourCC::SIDInstrument2ADSR,
                                                  FourCC::SIDInstrument3ADSR};
  static constexpr FourCC::enum_type filterOnIDs[] = {
      FourCC::SIDInstrumentFilterOn, FourCC::SIDInstrument2FilterOn,
      FourCC::SIDInstrument3FilterOn};
  static constexpr int waveforms[] = {DWF_TRI, DWF_SAW_SQ, DWF_NOISE};
  static constexpr int pulseWidths[] = {0x123, 0x456, 0x789};
  static constexpr int sync[] = {0, 1, 0};
  static constexpr int ring[] = {1, 0, 1};
  static constexpr int adsr[] = {0x1234, 0x5678, 0x9ABC};
  static constexpr int filterOn[] = {1, 0, 1};

  for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
    instrument.FindVariable(waveformIDs[voice])->SetInt(waveforms[voice]);
    instrument.FindVariable(pulseWidthIDs[voice])->SetInt(pulseWidths[voice]);
    instrument.FindVariable(syncIDs[voice])->SetInt(sync[voice]);
    instrument.FindVariable(ringIDs[voice])->SetInt(ring[voice]);
    instrument.FindVariable(adsrIDs[voice])->SetInt(adsr[voice]);
    instrument.FindVariable(filterOnIDs[voice])->SetInt(filterOn[voice]);
  }
  instrument.FindVariable(FourCC::SIDInstrument1FilterResonance)->SetInt(0xA);

  static constexpr int channel = 2;
  static constexpr int note = 60;
  REQUIRE(instrument.Start(channel, note));
  AdvanceSID &sid = SIDInstrument::renderParams_[channel].sid_;
  const uint16_t frequency = sid_notes[note - 24];

  for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
    const int offset = voice * 7;
    CHECK(sid.Register[offset] == (frequency & 0xFF));
    CHECK(sid.Register[offset + 1] == (frequency >> 8));
    CHECK(sid.Register[offset + 2] == (pulseWidths[voice] & 0xFF));
    CHECK(sid.Register[offset + 3] == (pulseWidths[voice] >> 8));
    CHECK(sid.Register[offset + 4] ==
          ((waveforms[voice] << 4) | (ring[voice] << 2) | (sync[voice] << 1) |
           1));
    CHECK(sid.Register[offset + 5] == (adsr[voice] >> 8));
    CHECK(sid.Register[offset + 6] == (adsr[voice] & 0xFF));
  }
  CHECK(sid.Register[23] == 0xA5);

  instrument.Release(channel);
  for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
    CHECK((sid.Register[voice * 7 + 4] & 1) == 0);
  }

  instrument.Stop(channel);
  for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
    CHECK(sid.Register[voice * 7 + 4] == 0);
  }
  CHECK((sid.Register[23] & 0x7) == 0);
}

TEST_CASE("SID pitch offsets create chords within sequencer note bounds") {
  SIDInstrument instrument;
  static constexpr FourCC::enum_type offsetIDs[] = {
      FourCC::SIDInstrumentPitchOffset, FourCC::SIDInstrument2PitchOffset,
      FourCC::SIDInstrument3PitchOffset};
  static constexpr FourCC::enum_type filterOnIDs[] = {
      FourCC::SIDInstrumentFilterOn, FourCC::SIDInstrument2FilterOn,
      FourCC::SIDInstrument3FilterOn};

  REQUIRE(instrument.Init());
  instrument.OnStart();
  for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
    instrument.FindVariable(filterOnIDs[voice])->SetBool(true);
  }

  static constexpr int32_t chordOffsets[] = {400, 700, 1200};
  for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
    instrument.FindVariable(offsetIDs[voice])->SetInt(chordOffsets[voice]);
  }

  static constexpr int channel = 3;
  static constexpr int rootNote = 60;
  REQUIRE(instrument.Start(channel, rootNote));
  AdvanceSID &sid = SIDInstrument::renderParams_[channel].sid_;
  for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
    const int channelOffset = voice * 7;
    const uint16_t frequency = SIDInstrument::GetPitchFrequency(
        rootNote * PitchOffset::CentsPerSemitone + chordOffsets[voice]);
    CHECK(sid.Register[channelOffset] == (frequency & 0xFF));
    CHECK(sid.Register[channelOffset + 1] == (frequency >> 8));
    CHECK((sid.Register[channelOffset + 4] & 1) == 1);
  }
  CHECK((sid.Register[23] & 0x7) == 0x7);

  static constexpr int32_t lowOffsets[] = {-100, 0, 700};
  for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
    instrument.FindVariable(offsetIDs[voice])->SetInt(lowOffsets[voice]);
  }
  REQUIRE(instrument.Start(channel, LOWEST_NOTE));
  CHECK(sid.Register[0] == 0);
  CHECK(sid.Register[1] == 0);
  CHECK(sid.Register[4] == 0);
  CHECK((sid.Register[11] & 1) == 1);
  CHECK((sid.Register[18] & 1) == 1);
  CHECK((sid.Register[23] & 0x7) == 0x6);

  static constexpr int32_t highOffsets[] = {-700, 0, 100};
  for (int voice = 0; voice < SIDInstrument::VoiceCount; voice++) {
    instrument.FindVariable(offsetIDs[voice])->SetInt(highOffsets[voice]);
  }
  REQUIRE(instrument.Start(channel, HIGHEST_NOTE));
  CHECK((sid.Register[4] & 1) == 1);
  CHECK((sid.Register[11] & 1) == 1);
  CHECK(sid.Register[14] == 0);
  CHECK(sid.Register[15] == 0);
  CHECK(sid.Register[18] == 0);
  CHECK((sid.Register[23] & 0x7) == 0x3);

  CHECK(SIDInstrument::GetNoteFrequency(LOWEST_NOTE) == (sid_notes[0] >> 2));
  CHECK(SIDInstrument::GetNoteFrequency(12) == (sid_notes[0] >> 1));
  CHECK(SIDInstrument::GetNoteFrequency(24) == sid_notes[0]);

  instrument.Stop(channel);
}

TEST_CASE("SID pitch offsets support fractional semitones") {
  const uint16_t note59 = SIDInstrument::GetNoteFrequency(59);
  const uint16_t note60 = SIDInstrument::GetNoteFrequency(60);
  const uint16_t note61 = SIDInstrument::GetNoteFrequency(61);

  CHECK(SIDInstrument::GetPitchFrequency(6000) == note60);
  CHECK(SIDInstrument::GetPitchFrequency(6100) == note61);

  const uint16_t fiftyCentsUp = SIDInstrument::GetPitchFrequency(6050);
  CHECK(fiftyCentsUp > note60);
  CHECK(fiftyCentsUp < note61);

  const uint16_t oneCentDown = SIDInstrument::GetPitchFrequency(5999);
  CHECK(oneCentDown > note59);
  CHECK(oneCentDown <= note60);
}

TEST_CASE(
    "equal SID whole-semitone offsets match a transposed sequencer note") {
  static AudioSample offsetBuffer[128];
  static AudioSample noteBuffer[128];
  SIDInstrument offsetInstrument;
  SIDInstrument noteInstrument;
  static constexpr FourCC::enum_type offsetIDs[] = {
      FourCC::SIDInstrumentPitchOffset, FourCC::SIDInstrument2PitchOffset,
      FourCC::SIDInstrument3PitchOffset};

  REQUIRE(offsetInstrument.Init());
  REQUIRE(noteInstrument.Init());
  offsetInstrument.OnStart();
  for (FourCC::enum_type id : offsetIDs) {
    offsetInstrument.FindVariable(id)->SetInt(700);
  }

  REQUIRE(offsetInstrument.Start(0, 60));
  REQUIRE(noteInstrument.Start(1, 67));
  AdvanceSID &offsetSID = SIDInstrument::renderParams_[0].sid_;
  AdvanceSID &noteSID = SIDInstrument::renderParams_[1].sid_;
  for (int reg = 0; reg < 25; reg++) {
    CHECK(offsetSID.Register[reg] == noteSID.Register[reg]);
  }

  REQUIRE(offsetInstrument.Render(0, offsetBuffer, 128, false));
  REQUIRE(noteInstrument.Render(1, noteBuffer, 128, false));
  for (int sample = 0; sample < 128; sample++) {
    CHECK(offsetBuffer[sample] == noteBuffer[sample]);
  }

  offsetInstrument.Stop(0);
  noteInstrument.Stop(1);
}

TEST_CASE("SID applies the AudioVoice pitch multiplier to every chord voice") {
  SIDInstrument instrument;
  static constexpr FourCC::enum_type offsetIDs[] = {
      FourCC::SIDInstrumentPitchOffset, FourCC::SIDInstrument2PitchOffset,
      FourCC::SIDInstrument3PitchOffset};
  static constexpr int32_t chordOffsets[] = {0, 400, 700};

  REQUIRE(instrument.Init());
  instrument.OnStart();
  for (int voice = 0; voice < SIDInstrument::VoiceCount; ++voice) {
    instrument.FindVariable(offsetIDs[voice])->SetInt(chordOffsets[voice]);
  }

  static constexpr int channel = 2;
  REQUIRE(instrument.Start(channel, 60));
  SIDRenderParams &renderParams = SIDInstrument::renderParams_[channel];

  instrument.SetPitchMultiplier(channel, 2.0f);
  for (int voice = 0; voice < SIDInstrument::VoiceCount; ++voice) {
    CHECK(ReadSIDFrequency(renderParams.sid_, voice) ==
          renderParams.baseFrequency_[voice] * 2);
  }

  instrument.SetPitchMultiplier(channel, 0.5f);
  for (int voice = 0; voice < SIDInstrument::VoiceCount; ++voice) {
    CHECK(ReadSIDFrequency(renderParams.sid_, voice) ==
          static_cast<uint16_t>(renderParams.baseFrequency_[voice] * 0.5f +
                                0.5f));
  }
  instrument.Stop(channel);
}

TEST_CASE("SID instruments expose and clone audio voice parameters") {
  static InstrumentBank bank;
  bank.Reset();

  static constexpr uint8_t instrumentID = 0;
  REQUIRE(bank.GetNextAndAssignID(IT_SID, instrumentID) == instrumentID);
  I_Instrument *instrument = bank.GetInstrument(instrumentID);
  REQUIRE(instrument != nullptr);
  CHECK(InstrumentTypeUsesModernAudioVoice(instrument->GetType()));

  Variable *amp = instrument->FindVariable(FourCC::VoiceAmp);
  REQUIRE(amp != nullptr);
  amp->SetInt(0x62);
  Variable *ampRelease = instrument->FindVariable(FourCC::VoiceAmpRelease);
  REQUIRE(ampRelease != nullptr);
  CHECK(ampRelease->GetInt() == 0xFF);
  ampRelease->SetInt(0x43);

  const unsigned short cloneID = bank.Clone(instrumentID);
  REQUIRE(cloneID != NO_MORE_INSTRUMENT);
  I_Instrument *clone = bank.GetInstrument(cloneID);
  REQUIRE(clone != nullptr);
  Variable *clonedAmp = clone->FindVariable(FourCC::VoiceAmp);
  REQUIRE(clonedAmp != nullptr);
  CHECK(clonedAmp->GetInt() == 0x62);
  Variable *clonedAmpRelease = clone->FindVariable(FourCC::VoiceAmpRelease);
  REQUIRE(clonedAmpRelease != nullptr);
  CHECK(clonedAmpRelease->GetInt() == 0x43);

  bank.Reset();
  Variable *resetAmpRelease = bank.GetVoiceParameters(instrumentID)
                                  ->FindVariable(FourCC::VoiceAmpRelease);
  REQUIRE(resetAmpRelease != nullptr);
  CHECK(resetAmpRelease->GetInt() == 0x00);
}

TEST_CASE("Freshly assigned SID instrument reports no modified variables") {
  // Regression: switching an instrument slot to SID must not leave any variable
  // looking modified, otherwise the InstrumentView shows a bogus "lose
  // settings?" confirmation immediately after switching to the type. The SID's
  // infinite amp release (0xFF) is its default state, not a user edit.
  static InstrumentBank bank;
  bank.Reset();

  static constexpr uint8_t instrumentID = 0;
  REQUIRE(bank.GetNextAndAssignID(IT_SID, instrumentID) == instrumentID);
  I_Instrument *instrument = bank.GetInstrument(instrumentID);
  REQUIRE(instrument != nullptr);

  for (Variable *var : *instrument->Variables()) {
    CAPTURE(FourCC(var->GetID()).c_str());
    CHECK_FALSE(var->IsModified());
  }
}

TEST_CASE("A slot reused after SID does not inherit the SID amp release "
          "default") {
  // The bank reuses one VoiceParameters instance per slot, so the SID amp
  // release override must be restored on release and not leak to the next
  // instrument assigned to the same slot.
  static InstrumentBank bank;
  bank.Reset();

  static constexpr uint8_t instrumentID = 0;
  REQUIRE(bank.GetNextAndAssignID(IT_SID, instrumentID) == instrumentID);
  bank.releaseInstrument(instrumentID);
  REQUIRE(bank.GetNextAndAssignID(IT_STARLOOM, instrumentID) == instrumentID);

  Variable *ampRelease = bank.GetVoiceParameters(instrumentID)
                             ->FindVariable(FourCC::VoiceAmpRelease);
  REQUIRE(ampRelease != nullptr);
  CHECK(ampRelease->GetInt() == 0x00);
  CHECK_FALSE(ampRelease->IsModified());
}

TEST_CASE(
    "AudioVoice finishes an infinite release after the SID becomes idle") {
  static constexpr int channel = 0;
  static constexpr int renderSamples = 64;
  static AudioSample buffer[renderSamples * 2];
  static InstrumentBank bank;
  bank.Reset();

  static constexpr uint8_t instrumentID = 0;
  REQUIRE(bank.GetNextAndAssignID(IT_SID, instrumentID) == instrumentID);
  I_Instrument *instrument = bank.GetInstrument(instrumentID);
  REQUIRE(instrument != nullptr);
  Variable *ampRelease = instrument->FindVariable(FourCC::VoiceAmpRelease);
  REQUIRE(ampRelease != nullptr);
  REQUIRE(ampRelease->GetInt() == 0xFF);

  AudioVoice voice(channel);
  REQUIRE(voice.StartInstrument(instrument, 60, true));
  REQUIRE(voice.Render(buffer, renderSamples, false));
  voice.ReleaseInstrument();

  bool rendering = true;
  int renderCount = 0;
  while (rendering && renderCount < 1000) {
    rendering = voice.Render(buffer, renderSamples, false);
    ++renderCount;
  }

  CHECK(renderCount < 1000);
  CHECK_FALSE(rendering);
  CHECK(voice.GetInstrument() == nullptr);
}

TEST_CASE("SID legacy waveform text normalizes to compact values") {
  SIDInstrument instrument;
  Variable *voice1 = instrument.FindVariable(FourCC::SIDInstrumentWaveform);
  Variable *voice2 = instrument.FindVariable(FourCC::SIDInstrument2Waveform);
  REQUIRE(voice1 != nullptr);
  REQUIRE(voice2 != nullptr);

  voice1->SetString("--" char_waveform_saw_s char_waveform_pulse_s "--", false);
  voice2->SetString("not-a-waveform", false);
  REQUIRE(voice1->GetInt() >= DWF_LAST);
  REQUIRE(voice2->GetInt() == -1);

  instrument.NormalizeWaveforms();

  CHECK(voice1->GetInt() == DWF_SAW_SQ);
  CHECK(voice2->GetInt() == DWF_TRI);
  CHECK(voice1->GetString() == "--" char_waveform_saw_s char_waveform_pulse_s);
}

TEST_CASE("SID chip rendering is independent of buffer size") {
  static AudioSample singleBuffer[256];
  static AudioSample segmentedBuffer[256];
  AdvanceSID single(44100);
  AdvanceSID segmented(44100);

  ConfigureFullSIDChip(single);
  ConfigureFullSIDChip(segmented);
  single.Render(singleBuffer, 256);
  for (int block = 0; block < 4; block++) {
    segmented.Render(&segmentedBuffer[block * 64], 64);
  }

  for (int sample = 0; sample < 256; sample++) {
    CHECK(singleBuffer[sample] == segmentedBuffer[sample]);
  }
}

TEST_CASE("SID ADSR sample clock accumulates the exact PAL clock rate") {
  AdvanceSID sid(44100);
  uint32_t elapsedCycles = 0;

  for (uint32_t sample = 0; sample < 44100; ++sample)
    elapsedCycles += sid.NextADSRCycleCount();

  CHECK(elapsedCycles == C64_PAL_CPUCLK);
  CHECK(sid.adsrClockRemainder_ == 0);
}

TEST_CASE("SID ADSR processes multiple rate events in one output sample") {
  AdvanceSID sid(44100);
  sid.Register[4] = 0x01;

  sid.EmulateADSRs();

  // The first 44.1 kHz sample contains 22 PAL SID clocks. Attack rate zero
  // advances at clocks 9 and 18, leaving four clocks in the rate counter.
  CHECK(sid.EnvelopeCounter[0] == 2);
  CHECK(sid.RateCounter[0] == 4);
}

TEST_CASE("SID voice idle state requires closed gates and empty envelopes") {
  AdvanceSID sid(44100);
  CHECK(sid.AreVoicesIdle());

  // A slow attack can leave the envelope at zero for an initial render even
  // though the note is active, so the open gate must keep the voice alive.
  sid.Register[4] = 0x01;
  CHECK_FALSE(sid.AreVoicesIdle());

  sid.Register[4] = 0;
  sid.EnvelopeCounter[7] = 1;
  CHECK_FALSE(sid.AreVoicesIdle());

  sid.EnvelopeCounter[7] = 0;
  CHECK(sid.AreVoicesIdle());
}

TEST_CASE("SID instrument finishes after its released envelopes drain") {
  static constexpr int channel = 0;
  static constexpr int renderSamples = 64;
  static AudioSample buffer[renderSamples];
  SIDInstrument instrument;

  REQUIRE(instrument.Init());
  instrument.OnStart();
  REQUIRE(instrument.Start(channel, 60));

  // The gates prevent zero-valued attack envelopes from being mistaken for a
  // completed release.
  REQUIRE(instrument.Render(channel, buffer, renderSamples, false));
  CHECK(SIDInstrument::renderParams_[channel].playing_);

  instrument.Release(channel);
  bool rendering = true;
  int renderCount = 0;
  while (rendering && renderCount < 1000) {
    rendering = instrument.Render(channel, buffer, renderSamples, false);
    ++renderCount;
  }

  CHECK(renderCount > 0);
  CHECK(renderCount < 1000);
  CHECK_FALSE(rendering);
  CHECK_FALSE(SIDInstrument::renderParams_[channel].playing_);
  CHECK_FALSE(instrument.Render(channel, buffer, renderSamples, false));
}

TEST_CASE("SID ADSR attack timing follows the PAL SID clock") {
  struct AttackTiming {
    uint8_t rate;
    uint16_t sampleCount;
  };
  static constexpr AttackTiming timings[] = {
      {.rate = 0, .sampleCount = 103},
      {.rate = 2, .sampleCount = 720},
  };

  for (const AttackTiming &timing : timings) {
    AdvanceSID sid(44100);
    sid.Register[4] = 0x01;
    sid.Register[5] = timing.rate << 4;
    sid.Register[6] = 0xF0;
    uint16_t sampleCount = 0;

    while (sid.EnvelopeCounter[0] != 0xFF && sampleCount < 1000) {
      sid.EmulateADSRs();
      ++sampleCount;
    }

    CAPTURE(timing.rate);
    CAPTURE(sid.ADSRstate[0]);
    CAPTURE(sid.RateCounter[0]);
    CAPTURE(sid.ExponentCounter[0]);
    CHECK(sampleCount == timing.sampleCount);
    CHECK(sid.EnvelopeCounter[0] == 0xFF);
  }
}

TEST_CASE("SID ADSR rate changes preserve the delay bug counter wrap") {
  AdvanceSID sid(44100);
  sid.Register[4] = 0x01;
  sid.RateCounter[0] = 10;

  // Attack rate zero matches at nine. Starting just beyond that match must
  // wait for the 15-bit counter to wrap and reach nine on the next pass.
  sid.EmulateADSRs(32766);
  CHECK(sid.EnvelopeCounter[0] == 0);
  CHECK(sid.RateCounter[0] == 8);

  sid.EmulateADSRs(1);
  CHECK(sid.EnvelopeCounter[0] == 1);
  CHECK(sid.RateCounter[0] == 0);
}

TEST_CASE("SID full chip rendering stays bit exact") {
  static AudioSample buffer[256];
  AdvanceSID sid(44100);

  ConfigureFullSIDChip(sid);
  sid.Render(buffer, 256);

  CHECK(HashSIDOutputBits(buffer, 256) == 0x182842E364E87183ULL);
}

TEST_CASE("SID defaults to the existing 8580 model") {
  static AudioSample defaultBuffer[128];
  static AudioSample explicitBuffer[128];
  AdvanceSID defaultModel(44100);
  AdvanceSID explicitModel(AdvanceSID::Model::MOS8580, 44100);

  CHECK(defaultModel.GetModel() == AdvanceSID::Model::MOS8580);
  CHECK(explicitModel.GetModel() == AdvanceSID::Model::MOS8580);
  ConfigureFullSIDChip(defaultModel);
  ConfigureFullSIDChip(explicitModel);
  defaultModel.Render(defaultBuffer, 128);
  explicitModel.Render(explicitBuffer, 128);

  for (int sample = 0; sample < 128; sample++) {
    CHECK(defaultBuffer[sample] == explicitBuffer[sample]);
  }
}

TEST_CASE("SID 6581 model restores its distinct chip response") {
  static AudioSample mos6581Buffer[256];
  static AudioSample mos8580Buffer[256];
  AdvanceSID mos6581(AdvanceSID::Model::MOS6581, 44100);
  AdvanceSID mos8580(AdvanceSID::Model::MOS8580, 44100);

  CHECK(mos6581.GetModel() == AdvanceSID::Model::MOS6581);
  ConfigureFullSIDChip(mos6581);
  ConfigureFullSIDChip(mos8580);
  mos6581.Render(mos6581Buffer, 256);
  mos8580.Render(mos8580Buffer, 256);

  const uint64_t mos6581Hash = HashSIDOutputBits(mos6581Buffer, 256);
  CHECK(mos6581Hash == 0x646435EE6CD46783ULL);
  CHECK(mos6581Hash != HashSIDOutputBits(mos8580Buffer, 256));
}

TEST_CASE("buffered 6581 rendering matches sample-at-a-time rendering") {
  static AudioSample bufferedOutput[256];
  static AudioSample sampleOutput[256];
  AdvanceSID buffered(AdvanceSID::Model::MOS6581, 44100);
  AdvanceSID sampleAtATime(AdvanceSID::Model::MOS6581, 44100);

  ConfigureFullSIDChip(buffered);
  ConfigureFullSIDChip(sampleAtATime);
  buffered.Render(bufferedOutput, 256);
  for (int sample = 0; sample < 256; sample++) {
    sampleAtATime.EmulateADSRs();
    const AudioSample output =
        static_cast<AudioSample>(sampleAtATime.EmulateWaves() / 32768.0f);
    sampleOutput[sample] = output;
  }

  for (int sample = 0; sample < 256; sample++) {
    CHECK(bufferedOutput[sample] == sampleOutput[sample]);
  }
}

TEST_CASE("buffered SID ADSR rendering matches sample-at-a-time rendering") {
  static AudioSample bufferedOutput[256];
  static AudioSample sampleOutput[256];
  AdvanceSID buffered(44100);
  AdvanceSID sampleAtATime(44100);

  ConfigureFullSIDChip(buffered);
  ConfigureFullSIDChip(sampleAtATime);
  buffered.Render(bufferedOutput, 256);
  for (int sample = 0; sample < 256; sample++) {
    sampleAtATime.EmulateADSRs();
    const AudioSample output =
        static_cast<AudioSample>(sampleAtATime.EmulateWaves() / 32768.0f);
    sampleOutput[sample] = output;
  }

  for (int sample = 0; sample < 256; sample++) {
    CHECK(bufferedOutput[sample] == sampleOutput[sample]);
  }
}

TEST_CASE("buffered SID rendering matches all stateful waveform families") {
  static constexpr uint8_t waveformSets[][3] = {{0x11, 0x15, 0x21},
                                                {0x31, 0x41, 0x51},
                                                {0x61, 0x71, 0x81},
                                                {0x91, 0x01, 0x49},
                                                {0x13, 0x23, 0x43}};
  static constexpr int sampleCount = 37;
  static AudioSample bufferedOutput[sampleCount];
  static AudioSample sampleOutput[sampleCount];

  for (const auto &waveforms : waveformSets) {
    AdvanceSID buffered(44100);
    AdvanceSID sampleAtATime(44100);
    ConfigureFullSIDChip(buffered);
    ConfigureFullSIDChip(sampleAtATime);

    for (int voice = 0; voice < 3; voice++) {
      const int channel = voice * 7;
      buffered.Register[channel + 4] = waveforms[voice];
      sampleAtATime.Register[channel + 4] = waveforms[voice];
    }

    buffered.Render(bufferedOutput, sampleCount);
    for (int sample = 0; sample < sampleCount; sample++) {
      sampleAtATime.EmulateADSRs();
      const AudioSample output =
          static_cast<AudioSample>(sampleAtATime.EmulateWaves() / 32768.0f);
      sampleOutput[sample] = output;
    }

    for (int sample = 0; sample < sampleCount; sample++)
      CHECK(bufferedOutput[sample] == sampleOutput[sample]);

    for (int voice = 0; voice < 3; voice++) {
      const int channel = voice * 7;
      CHECK(buffered.ADSRstate[channel] == sampleAtATime.ADSRstate[channel]);
      CHECK(buffered.RateCounter[channel] ==
            sampleAtATime.RateCounter[channel]);
      CHECK(buffered.EnvelopeCounter[channel] ==
            sampleAtATime.EnvelopeCounter[channel]);
      CHECK(buffered.ExponentCounter[channel] ==
            sampleAtATime.ExponentCounter[channel]);
      CHECK(buffered.PhaseAccu[channel] == sampleAtATime.PhaseAccu[channel]);
      CHECK(buffered.PrevPhaseAccu[channel] ==
            sampleAtATime.PrevPhaseAccu[channel]);
      CHECK(buffered.NoiseLFSR[channel] == sampleAtATime.NoiseLFSR[channel]);
      CHECK(buffered.PrevWavGenOut[channel] ==
            sampleAtATime.PrevWavGenOut[channel]);
      CHECK(buffered.PrevWavData[channel] ==
            sampleAtATime.PrevWavData[channel]);
    }
    CHECK(buffered.SyncSourceMSBrise == sampleAtATime.SyncSourceMSBrise);
    CHECK(buffered.RingSourceMSB == sampleAtATime.RingSourceMSB);
    CHECK(buffered.PrevLowPass == sampleAtATime.PrevLowPass);
    CHECK(buffered.PrevBandPass == sampleAtATime.PrevBandPass);
  }
}
