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

#include "AudioOutDriver.h"
#include "System/Profiler/DWTProfiler.h"
#include "System/System/System.h"
#include "config/MemorySections.h"
#include <algorithm>

namespace {

constexpr uint16_t kProfileOrderGrandTotal = 65;

DWTProfileStats gGrandTotalProfile = {
    .name_ = "grand total",
    .printLast_ = true,
    .flushBoundary_ = true,
    .displayOrder_ = kProfileOrderGrandTotal,
};

void registerAudioOutProfiles() {
  static bool registered = false;
  if (registered) {
    return;
  }

  RegisterDWTProfileStats(&gGrandTotalProfile);
  registered = true;
}

} // namespace

AudioSample AudioOutDriver::primarySoundBuffer_[MIX_BUFFER_SIZE];
short AudioOutDriver::mixBuffer_[MIX_BUFFER_SIZE];

AudioOutDriver::AudioOutDriver(AudioDriver &driver) {
  registerAudioOutProfiles();
  driver_ = &driver;
  driver.AddObserver(*this);
}

AudioOutDriver::~AudioOutDriver() {
  driver_->RemoveObserver(*this);
  delete driver_;
};

bool AudioOutDriver::Init() { return driver_->Init(); };

void AudioOutDriver::Close() { driver_->Close(); }

bool AudioOutDriver::Start() {
  sampleCount_ = 0;
  return driver_->Start();
}

void AudioOutDriver::Stop() {
  SetAudioActive(false);
  driver_->Stop();
}

void AudioOutDriver::SetAudioActive(bool active) {
  driver_->OnAudioActive(active);
}

stereosample AudioOutDriver::GetLastPeakLevels() { return lastPeakVolume_; };

PICOTRACKER_FAST_CODE("audio_out_driver_trigger")
void AudioOutDriver::Trigger() {
  prepareMixBuffers();
  ScopedDWTProfile grandTotalProfile(&gGrandTotalProfile);
  hasSound_ = AudioMixer::Render(primarySoundBuffer_, sampleCount_) > 0;
  clipToMix();
  driver_->AddBuffer(mixBuffer_, sampleCount_);
}

void AudioOutDriver::Update(Observable &o, I_ObservableData *d) {
  SetChanged();
  NotifyObservers(d);
}

PICOTRACKER_FAST_CODE("audio_out_driver_prepare_mix_buffers")
void AudioOutDriver::prepareMixBuffers() {
  sampleCount_ = getPlaySampleCount();
};

PICOTRACKER_FAST_CODE("audio_out_driver_clip_to_mix")
void AudioOutDriver::clipToMix() {

  bool interlaced = driver_->Interlaced();

  if (!hasSound_) {
    memset(mixBuffer_, 0, sampleCount_ * 2 * sizeof(short));
  } else {
    short *s1 = mixBuffer_;
    short *s2 = (interlaced) ? s1 + 1 : s1 + sampleCount_;
    int offset = (interlaced) ? 2 : 1;

    AudioSample *p = primarySoundBuffer_;

    short peakL = 0;
    short peakR = 0;

    for (int i = 0; i < sampleCount_; i++) {
      // Left
      const AudioSample left =
          std::clamp(*p++, kAudioSampleMin, kAudioSampleMax);
      int iVal = static_cast<int32_t>(left * kAudioPcmScale);
      *s1 = short(iVal);
      s1 += offset;
      if (iVal >= peakL) {
        peakL = iVal;
      }

      // Right
      const AudioSample right =
          std::clamp(*p++, kAudioSampleMin, kAudioSampleMax);
      iVal = static_cast<int32_t>(right * kAudioPcmScale);
      *s2 = short(iVal);
      s2 += offset;
      if (iVal >= peakR) {
        peakR = iVal;
      }
    };
    lastPeakVolume_ = peakL << 16;
    lastPeakVolume_ += peakR;
    peakL = peakR = 0;
  }
};

int AudioOutDriver::GetPlayedBufferPercentage() {
  return driver_->GetPlayedBufferPercentage();
};

AudioDriver *AudioOutDriver::GetDriver() { return driver_; };

etl::string<STRING_AUDIO_API_MAX> AudioOutDriver::GetAudioAPI() {
  AudioSettings as = driver_->GetAudioSettings();
  return as.audioAPI_;
};

etl::string<STRING_AUDIO_DEVICE_MAX> AudioOutDriver::GetAudioDevice() {
  AudioSettings as = driver_->GetAudioSettings();
  return as.audioDevice_;
};
int AudioOutDriver::GetAudioBufferSize() {
  AudioSettings as = driver_->GetAudioSettings();
  return as.bufferSize_;
};

int AudioOutDriver::GetAudioRequestedBufferSize() {
  AudioSettings as = driver_->GetAudioSettings();
  return as.bufferSize_;
}

int AudioOutDriver::GetAudioPreBufferCount() {
  AudioSettings as = driver_->GetAudioSettings();
  return as.preBufferCount_;
};
double AudioOutDriver::GetStreamTime() { return driver_->GetStreamTime(); };
