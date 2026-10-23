/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "picoSwarmInstrument.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kMaximumHighPassFrequency = picoSwarm::SampleRate * 0.45f;
// Q values for the two conjugate pole pairs of a fourth-order Butterworth
// response. Cascading the lower-Q section first gives a little more internal
// headroom without changing the transfer function.
constexpr double kHighPassStageQ[picoSwarm::HighPassStageCount] = {
    0.541196100146197, 1.306562964876377};
// Recovered JP-8000 MCU detune values with the hardware's baseline value of
// one removed. The DSP receives these as fixed-point control values; 320 is
// the maximum effective detune. Keeping all 128 steps avoids interpolating the
// sparsely measured upper range with the old 11th-order polynomial.
constexpr uint16_t kDetuneControlTable[128] = {
    0,   0,   1,   1,   2,   2,   3,   3,   4,   4,   5,   5,   6,   6,   7,
    7,   8,   8,   9,   9,   10,  10,  11,  11,  12,  12,  13,  13,  14,  14,
    15,  15,  16,  16,  17,  17,  18,  18,  19,  19,  20,  20,  21,  21,  22,
    22,  23,  23,  24,  24,  25,  25,  26,  26,  27,  27,  28,  28,  29,  29,
    30,  30,  31,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,
    43,  44,  45,  46,  47,  48,  50,  52,  54,  56,  58,  60,  62,  64,  66,
    68,  70,  72,  74,  76,  78,  80,  82,  84,  86,  88,  90,  92,  94,  96,
    98,  100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126,
    128, 136, 144, 152, 168, 192, 224, 320};
constexpr float kInverseMaximumDetune = 1.0f / 320.0f;
// The seven measured frequency ratios at maximum detune, expressed as offsets
// from the center oscillator. The upward and downward offsets are deliberately
// asymmetric, as measured in table 1 of the thesis.
constexpr double kOscillatorOffsets[picoSwarm::OscillatorCount] = {
    -0.11002313, -0.06288439, -0.01952356, 0.0,
    0.01991221,  0.06216538,  0.10745242};
// A fixed mixer headroom factor preserves the paper's relative gain curves
// while keeping the worst-case in-phase sum close to the AudioSample range.
constexpr AudioSample kMixerHeadroom = 0.25f;

float clampControl(float control) { return std::clamp(control, 0.0f, 1.0f); }

} // namespace

namespace picoSwarm {

PitchTrackedHighPass::PitchTrackedHighPass() : stages_() {
  Configure(0.0f);
  Reset();
}

void PitchTrackedHighPass::Reset() {
  for (Stage &stage : stages_) {
    stage.state1 = 0.0f;
    stage.state2 = 0.0f;
  }
}

void PitchTrackedHighPass::Configure(float cutoffFrequency) {
  const float cutoff =
      std::clamp(cutoffFrequency, 0.0f, kMaximumHighPassFrequency);
  if (cutoff <= 0.0f) {
    for (Stage &stage : stages_) {
      stage.b0 = 1.0f;
      stage.b1 = 0.0f;
      stage.b2 = 0.0f;
      stage.a1 = 0.0f;
      stage.a2 = 0.0f;
    }
    return;
  }

  // Two bilinear-transform biquads implement a four-pole high-pass whose
  // cutoff follows the played fundamental. Szabo identifies the pitch
  // tracking but not the order; Shore's measured reconstruction found that a
  // 24 dB/octave filter reproduces the original waveform's steep low cut.
  const double radians = 2.0 * kPi * static_cast<double>(cutoff) / SampleRate;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  for (size_t index = 0; index < stages_.size(); ++index) {
    const double alpha = sine / (2.0 * kHighPassStageQ[index]);
    const double inverseA0 = 1.0 / (1.0 + alpha);
    Stage &stage = stages_[index];
    stage.b0 = static_cast<float>(0.5 * (1.0 + cosine) * inverseA0);
    stage.b1 = static_cast<float>(-(1.0 + cosine) * inverseA0);
    stage.b2 = stage.b0;
    stage.a1 = static_cast<float>(-2.0 * cosine * inverseA0);
    stage.a2 = static_cast<float>((1.0 - alpha) * inverseA0);
  }
}

PICOTRACKER_FAST_CODE("pico_swarm_high_pass")
void PitchTrackedHighPass::ProcessBlock(AudioSample *buffer,
                                        size_t sampleCount) {
  if (!buffer || sampleCount == 0) {
    return;
  }

  // Keep both stages' coefficients and state in registers across the entire
  // block. Interleaving the filter with the seven-oscillator render loop puts
  // enough pressure on the floating-point registers that these values are
  // otherwise loaded and stored again for every sample.
  const float firstB0 = stages_[0].b0;
  const float firstB1 = stages_[0].b1;
  const float firstB2 = stages_[0].b2;
  const float firstA1 = stages_[0].a1;
  const float firstA2 = stages_[0].a2;
  AudioSample firstState1 = stages_[0].state1;
  AudioSample firstState2 = stages_[0].state2;

  const float secondB0 = stages_[1].b0;
  const float secondB1 = stages_[1].b1;
  const float secondB2 = stages_[1].b2;
  const float secondA1 = stages_[1].a1;
  const float secondA2 = stages_[1].a2;
  AudioSample secondState1 = stages_[1].state1;
  AudioSample secondState2 = stages_[1].state2;

  for (size_t sample = 0; sample < sampleCount; ++sample) {
    const AudioSample input = buffer[sample];
    const AudioSample firstOutput = firstB0 * input + firstState1;
    firstState1 = firstB1 * input - firstA1 * firstOutput + firstState2;
    firstState2 = firstB2 * input - firstA2 * firstOutput;

    const AudioSample output = secondB0 * firstOutput + secondState1;
    secondState1 = secondB1 * firstOutput - secondA1 * output + secondState2;
    secondState2 = secondB2 * firstOutput - secondA2 * output;
    buffer[sample] = output;
  }

  stages_[0].state1 = firstState1;
  stages_[0].state2 = firstState2;
  stages_[1].state1 = secondState1;
  stages_[1].state2 = secondState2;
}

Voice::Voice()
    : phases_(), baseNoteFrequency_(0.0f), baseFrequency_(0.0f),
      pitchMultiplier_(1.0f), highPass_(), randomState_(1), active_(false) {}

void Voice::Reset(uint32_t randomSeed) {
  phases_.fill(0.0);
  baseNoteFrequency_ = 0.0f;
  baseFrequency_ = 0.0f;
  pitchMultiplier_ = 1.0f;
  highPass_.Reset();
  randomState_ = randomSeed == 0 ? 1 : randomSeed;
  active_ = false;
}

uint32_t Voice::nextRandom() {
  // xorshift32 is small, deterministic, and requires no shared or dynamically
  // allocated state. A different seed is assigned to every song channel.
  randomState_ ^= randomState_ << 13;
  randomState_ ^= randomState_ >> 17;
  randomState_ ^= randomState_ << 5;
  return randomState_;
}

void Voice::randomizePhases() {
  constexpr double scale = 1.0 / 4294967296.0;
  for (double &phase : phases_) {
    phase = static_cast<double>(nextRandom()) * scale;
  }
}

PICOTRACKER_FAST_CODE("pico_swarm_detune_amount")
float Voice::DetuneAmount(float control) {
  const float scaledControl = clampControl(control) * 127.0f;
  const size_t controlIndex = static_cast<size_t>(scaledControl + 0.5f);
  return static_cast<float>(kDetuneControlTable[controlIndex]) *
         kInverseMaximumDetune;
}

PICOTRACKER_FAST_CODE("pico_swarm_oscillator_ratio")
float Voice::OscillatorRatio(size_t oscillator, float control) {
  if (oscillator >= OscillatorCount) {
    return 1.0f;
  }
  return static_cast<float>(1.0 + kOscillatorOffsets[oscillator] *
                                      DetuneAmount(control));
}

PICOTRACKER_FAST_CODE("pico_swarm_center_gain")
float Voice::CenterGain(float control) {
  const float x = clampControl(control);
  return (-0.55366f * x) + 0.99785f;
}

PICOTRACKER_FAST_CODE("pico_swarm_side_gain")
float Voice::SideGain(float control) {
  const float x = clampControl(control);
  return (-0.73764f * x * x) + (1.2841f * x) + 0.044372f;
}

float Voice::NoteFrequency(uint8_t note) {
  const uint8_t midiNote = std::min(note, uint8_t{127});
  return 440.0f *
         std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

void Voice::updateHighPass() { highPass_.Configure(baseFrequency_); }

void Voice::Start(uint8_t note, bool retrigger) {
  const bool startFromSilence = !active_;
  baseNoteFrequency_ = NoteFrequency(note);
  baseFrequency_ = baseNoteFrequency_ * pitchMultiplier_;
  updateHighPass();

  if (retrigger || startFromSilence) {
    randomizePhases();
    highPass_.Reset();
  }
  active_ = true;
}

void Voice::SetPitchMultiplier(float multiplier) {
  pitchMultiplier_ = std::max(multiplier, 0.0f);
  baseFrequency_ = baseNoteFrequency_ * pitchMultiplier_;
  updateHighPass();
}

void Voice::Stop() { active_ = false; }

PICOTRACKER_FAST_CODE("pico_swarm_voice_render")
bool Voice::Render(AudioSample *buffer, size_t sampleCount, uint8_t detune,
                   uint8_t mix) {
  if (!active_ || !buffer || sampleCount == 0) {
    return false;
  }

  const float detuneControl = std::min(detune, uint8_t{127}) / 127.0f;
  const float mixControl = std::min(mix, uint8_t{127}) / 127.0f;
  const float detuneAmount = DetuneAmount(detuneControl);
  etl::array<double, OscillatorCount> increments;
  for (size_t oscillator = 0; oscillator < OscillatorCount; ++oscillator) {
    const double oscillatorRatio =
        1.0 + kOscillatorOffsets[oscillator] * detuneAmount;
    increments[oscillator] =
        static_cast<double>(baseFrequency_) * oscillatorRatio / SampleRate;
  }

  const AudioSample centerGain = CenterGain(mixControl);
  const AudioSample sideGain = SideGain(mixControl);
  for (size_t sample = 0; sample < sampleCount; ++sample) {
    AudioSample sum = 0.0f;
    for (size_t oscillator = 0; oscillator < OscillatorCount; ++oscillator) {
      const AudioSample saw =
          static_cast<AudioSample>((2.0 * phases_[oscillator]) - 1.0);
      sum += saw * (oscillator == CenterOscillator ? centerGain : sideGain);
      phases_[oscillator] += increments[oscillator];
      if (phases_[oscillator] >= 1.0) {
        // Pitch commands can move an oscillator by more than one complete
        // cycle per output sample. Preserve the aliased phase while keeping it
        // normalized for every positive increment.
        phases_[oscillator] -= std::floor(phases_[oscillator]);
      }
    }
    buffer[sample] = sum * kMixerHeadroom;
  }
  highPass_.ProcessBlock(buffer, sampleCount);
  return true;
}

double Voice::GetPhase(size_t oscillator) const {
  return oscillator < OscillatorCount ? phases_[oscillator] : 0.0;
}

} // namespace picoSwarm

etl::array<picoSwarm::Voice, SONG_CHANNEL_COUNT> picoSwarmInstrument::voices_{};

picoSwarmInstrument::picoSwarmInstrument()
    : I_Instrument(&variables_), detune_(FourCC::picoSwarmInstrumentDetune, 64),
      mix_(FourCC::picoSwarmInstrumentMix, 127),
      table_(FourCC::picoSwarmInstrumentTable, VAR_OFF),
      tableAuto_(FourCC::picoSwarmInstrumentTableAutomation, false),
      initialized_(false) {
  variables_.push_back(&detune_);
  variables_.push_back(&mix_);
  variables_.push_back(&table_);
  variables_.push_back(&tableAuto_);
  tableState_.Reset();
}

picoSwarmInstrument::~picoSwarmInstrument() {}

bool picoSwarmInstrument::Init() {
  OnStart();
  initialized_ = true;
  return true;
}

bool picoSwarmInstrument::Start(int channel, unsigned char note,
                                bool retrigger) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT) {
    return false;
  }
  voices_[channel].Start(note, retrigger);
  return true;
}

void picoSwarmInstrument::Release(int channel) { (void)channel; }

void picoSwarmInstrument::Stop(int channel) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT) {
    return;
  }
  voices_[channel].Stop();
}

PICOTRACKER_FAST_CODE("pico_swarm_instrument_render")
bool picoSwarmInstrument::Render(int channel, AudioSample *buffer, int size,
                                 bool updateTick) {
  (void)updateTick;
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT || size <= 0) {
    return false;
  }
  const uint8_t detune =
      static_cast<uint8_t>(std::clamp(detune_.GetInt(), 0, 127));
  const uint8_t mix = static_cast<uint8_t>(std::clamp(mix_.GetInt(), 0, 127));
  return voices_[channel].Render(buffer, static_cast<size_t>(size), detune,
                                 mix);
}

void picoSwarmInstrument::SetPitchMultiplier(int channel, float multiplier) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT) {
    return;
  }
  voices_[channel].SetPitchMultiplier(multiplier);
}

PICOTRACKER_FAST_CODE("pico_swarm_instrument_buffer_layout")
AudioBufferLayout picoSwarmInstrument::GetAudioBufferLayout(int channel) const {
  (void)channel;
  return AudioBufferLayout::MonoPacked;
}

bool picoSwarmInstrument::IsInitialized() { return initialized_; }

bool picoSwarmInstrument::ShouldAuditionParameterChange(
    FourCC parameter) const {
  return parameter == FourCC::picoSwarmInstrumentDetune ||
         parameter == FourCC::picoSwarmInstrumentMix;
}

bool picoSwarmInstrument::IsEmpty() { return false; }

void picoSwarmInstrument::ProcessCommand(int channel, TrackerCommand cc,
                                         ushort value) {
  (void)channel;
  if (cc == TrackerCommand::InstrumentCommandTable) {
    table_.SetInt(value);
  }
}

int picoSwarmInstrument::GetTable() { return table_.GetInt(); }

bool picoSwarmInstrument::GetTableAutomation() { return tableAuto_.GetBool(); }

void picoSwarmInstrument::GetTableState(TableSaveState &state) {
  state = tableState_;
}

void picoSwarmInstrument::SetTableState(TableSaveState &state) {
  tableState_ = state;
}

void picoSwarmInstrument::OnStart() {
  for (size_t channel = 0; channel < voices_.size(); ++channel) {
    // Knuth's 32-bit golden-ratio constant gives every channel a distinct,
    // non-zero deterministic seed without storing another seed table.
    voices_[channel].Reset(0x9E3779B9U * static_cast<uint32_t>(channel + 1));
  }
  tableState_.Reset();
}
