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

#include "AudioDriver.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"
#include "System/System/System.h"
#include "config/MemorySections.h"

AudioBufferData AudioDriver::pool_[SOUND_BUFFER_COUNT];

AudioDriver::AudioDriver(AudioSettings &settings) { settings_ = settings; }

AudioDriver::~AudioDriver() {}

bool AudioDriver::Init() {

  // Clear all buffers

  for (int i = 0; i < SOUND_BUFFER_COUNT; i++) {
    pool_[i].size_ = 0;
    pool_[i].empty_ = true;
  };
  isPlaying_ = false;

  return InitDriver();
}

void AudioDriver::Close() { CloseDriver(); };

bool AudioDriver::Start() {

  isPlaying_ = true;

  poolQueuePosition_ = 0;
  poolPlayPosition_ = 0;
  hasData_ = false;

  return StartDriver();
};

void AudioDriver::Stop() {
  isPlaying_ = false;
  hasData_ = false;
  StopDriver();
}

PICOTRACKER_FAST_CODE("audio_driver_add_buffer")
void AudioDriver::AddBuffer(short *buffer, int samplecount) {
  int len = samplecount * 2 * sizeof(short);

  if (!isPlaying_)
    return;

  if (len > SOUND_BUFFER_MAX) {
    Trace::Error("Alert: buffer size exceeded");
  }

  if (!pool_[poolQueuePosition_].empty_) {
    NInvalid;
    Trace::Error("Audio overrun, please report");
    pool_[poolQueuePosition_].empty_ = true;
    return;
  }

  memcpy(pool_[poolQueuePosition_].buffer_, (char *)buffer, len);
  pool_[poolQueuePosition_].size_ = len;
  pool_[poolQueuePosition_].empty_ = false;
  poolQueuePosition_ = (poolQueuePosition_ + 1) % SOUND_BUFFER_COUNT;
  hasData_ = true;
}

PICOTRACKER_FAST_CODE("audio_driver_new_buffer_needed")
void AudioDriver::OnNewBufferNeeded() {
  SetChanged();
  Event event(Event::ADET_BUFFERNEEDED);
  NotifyObservers(&event);
};

void AudioDriver::onAudioBufferTick() {
  SetChanged();
  Event event(Event::ADET_DRIVERTICK);
  NotifyObservers(&event);
}

bool AudioDriver::hasData() { return hasData_; };

AudioSettings AudioDriver::GetAudioSettings() { return settings_; };
