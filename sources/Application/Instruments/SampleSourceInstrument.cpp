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

#include "SampleSourceInstrument.h"
#include "Application/Model/Table.h"
#include "Application/Player/SyncMaster.h"
#include "CommandList.h"
#include "SamplePool.h"
#include "SampleVariable.h"
#include "Services/Audio/Audio.h"
#include "System/Console/Trace.h"
#include "System/Console/nanoprintf.h"
#include <algorithm>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr bool kShouldKillClicks = false;
constexpr int kDeclickTailSamples = 32;
constexpr int kLoopCrossfadeSamples = kDeclickTailSamples;
constexpr int kNoSample = -1;
constexpr float kOscBaseFrequency = 261.6255653006f;
constexpr AudioSample kDeclickSilenceThreshold =
    1.0f / static_cast<AudioSample>(kAudioPcmScale);

const char *sampleSourcePlaybackModes[SSLM_LAST] = {
    "fwd", "rev", "loop", "rev-loop", "pingpong", "osc", "loopsync"};

PICOTRACKER_FAST_CODE("sample_source_start_declick")
inline bool startDeclickTail(sampleSourceRenderParams &rp);

enum class SamplePlaybackTopology {
  Clamp = 0,
  Wrap,
  Reflect,
};

PICOTRACKER_FAST_CODE("sample_source_clamp_frame")
inline int clampFrameIndex(int frame, int firstFrame, int lastFrame) {
  if (frame < firstFrame) {
    return firstFrame;
  }
  if (frame > lastFrame) {
    return lastFrame;
  }
  return frame;
}

PICOTRACKER_FAST_CODE("sample_source_wrap_frame")
inline int wrapFrameIndex(int frame, int firstFrame, int lastFrame) {
  const int frameCount = lastFrame - firstFrame + 1;
  if (frameCount <= 0) {
    return firstFrame;
  }

  int offset = (frame - firstFrame) % frameCount;
  if (offset < 0) {
    offset += frameCount;
  }
  return firstFrame + offset;
}

PICOTRACKER_FAST_CODE("sample_source_reflect_frame")
inline int reflectFrameIndex(int frame, int firstFrame, int lastFrame) {
  const int span = lastFrame - firstFrame;
  if (span <= 0) {
    return firstFrame;
  }

  const int period = span * 2;
  int offset = (frame - firstFrame) % period;
  if (offset < 0) {
    offset += period;
  }
  if (offset > span) {
    offset = period - offset;
  }
  return firstFrame + offset;
}

PICOTRACKER_FAST_CODE("sample_source_resolve_frame")
inline int resolveFrameIndex(int frame, int firstFrame, int lastFrame,
                             SamplePlaybackTopology topology) {
  switch (topology) {
  case SamplePlaybackTopology::Clamp:
    return clampFrameIndex(frame, firstFrame, lastFrame);
  case SamplePlaybackTopology::Wrap:
    return wrapFrameIndex(frame, firstFrame, lastFrame);
  case SamplePlaybackTopology::Reflect:
    return reflectFrameIndex(frame, firstFrame, lastFrame);
  }

  return clampFrameIndex(frame, firstFrame, lastFrame);
}

PICOTRACKER_FAST_CODE("sample_source_pcm_frame")
inline AudioSample pcmFrameSample(const int16_t *sampleFrames, int channelCount,
                                  int frameIndex, int channelIndex) {
  return static_cast<AudioSample>(
             sampleFrames[frameIndex * channelCount + channelIndex]) /
         kAudioPcmScale;
}

PICOTRACKER_FAST_CODE("sample_source_hermite")
inline AudioSample hermiteInterpolate(AudioSample ym1, AudioSample y0,
                                      AudioSample y1, AudioSample y2,
                                      float fraction) {
  const AudioSample c0 = y0;
  const AudioSample c1 = 0.5f * (y1 - ym1);
  const AudioSample c2 = ym1 - (2.5f * y0) + (2.0f * y1) - (0.5f * y2);
  const AudioSample c3 = 0.5f * (y2 - ym1) + (1.5f * (y0 - y1));

  return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
}

PICOTRACKER_FAST_CODE("sample_source_playback_topology")
inline SamplePlaybackTopology
getPlaybackTopology(SampleSourceInstrumentLoopMode loopMode) {
  switch (loopMode) {
  case SSLM_LOOP:
  case SSLM_REVERSE_LOOP:
  case SSLM_OSC:
  case SSLM_LOOPSYNC:
    return SamplePlaybackTopology::Wrap;
  case SSLM_LOOP_PINGPONG:
    return SamplePlaybackTopology::Reflect;
  case SSLM_ONESHOT:
  case SSLM_REVERSE:
  case SSLM_LAST:
    return SamplePlaybackTopology::Clamp;
  }

  return SamplePlaybackTopology::Clamp;
}

PICOTRACKER_FAST_CODE("sample_source_uses_crossfade")
inline bool usesLoopCrossfade(SampleSourceInstrumentLoopMode loopMode) {
  return loopMode == SSLM_LOOP || loopMode == SSLM_REVERSE_LOOP;
}

PICOTRACKER_FAST_CODE("sample_source_uses_loop_point")
inline bool playbackModeUsesLoopPoint(SampleSourceInstrumentLoopMode mode) {
  switch (mode) {
  case SSLM_LOOP:
  case SSLM_REVERSE_LOOP:
  case SSLM_LOOP_PINGPONG:
  case SSLM_OSC:
  case SSLM_LOOPSYNC:
    return true;
  case SSLM_ONESHOT:
  case SSLM_REVERSE:
  case SSLM_LAST:
    return false;
  }

  return false;
}

PICOTRACKER_FAST_CODE("sample_source_reverse_range")
inline bool isReverseOneShotInRange(const sampleSourceRenderParams &rp,
                                    float position) {
  if (rp.rendLoopEnd_ > rp.rendFirst_) {
    return position >= static_cast<float>(rp.rendFirst_) &&
           position < static_cast<float>(rp.rendLoopEnd_);
  }

  return position <= static_cast<float>(rp.rendFirst_) &&
         position >= static_cast<float>(rp.rendLoopEnd_);
}

PICOTRACKER_FAST_CODE("sample_source_normalize_wrapped")
inline void normalizeWrappedPosition(int loopStart, int loopEnd,
                                     float &position, bool reverse) {
  const bool forwardRange = loopStart <= loopEnd;
  const float firstFrame =
      static_cast<float>(forwardRange ? loopStart : loopEnd);
  const float endFrame = static_cast<float>(forwardRange ? loopEnd : loopStart);
  const float length = endFrame - firstFrame;
  if (length <= 0.0f) {
    position = firstFrame;
    return;
  }

  if (forwardRange && !reverse) {
    if (position >= endFrame) {
      const int wraps = static_cast<int>((position - firstFrame) / length);
      position -= static_cast<float>(wraps) * length;
      if (position >= endFrame) {
        position -= length;
      }
    }
    return;
  }

  if (forwardRange && reverse) {
    if (position < firstFrame) {
      const int wraps = static_cast<int>((endFrame - position) / length);
      position += static_cast<float>(wraps) * length;
      if (position < firstFrame) {
        position += length;
      }
    }
    if (position >= endFrame) {
      const int wraps = static_cast<int>((position - firstFrame) / length);
      position -= static_cast<float>(wraps) * length;
      if (position >= endFrame) {
        position -= length;
      }
    }
    return;
  }

  if (!reverse) {
    if (position > endFrame) {
      const int wraps = static_cast<int>((position - firstFrame) / length);
      position -= static_cast<float>(wraps) * length;
      if (position > endFrame) {
        position -= length;
      }
    }
    if (position < firstFrame) {
      const int wraps = static_cast<int>((endFrame - position) / length);
      position += static_cast<float>(wraps) * length;
      if (position < firstFrame) {
        position += length;
      }
    }
    return;
  }

  if (position < firstFrame) {
    const int wraps = static_cast<int>((endFrame - position) / length);
    position += static_cast<float>(wraps) * length;
    if (position < firstFrame) {
      position += length;
    }
  }
}

PICOTRACKER_FAST_CODE("sample_source_normalize_ping_pong")
inline void normalizePingPongPosition(int loopStart, int loopEnd,
                                      float &position, bool &reverse) {
  float firstFrame = static_cast<float>(loopStart);
  float lastFrame = static_cast<float>(loopEnd - 1);
  if (loopStart > loopEnd) {
    firstFrame = static_cast<float>(loopEnd);
    lastFrame = static_cast<float>(loopStart);
  }

  if (lastFrame <= firstFrame) {
    position = firstFrame;
    reverse = false;
    return;
  }

  while (true) {
    if (!reverse) {
      if (position <= lastFrame) {
        return;
      }

      const float overshoot = position - lastFrame;
      position = lastFrame - overshoot;
      reverse = true;
    } else {
      if (position >= firstFrame) {
        return;
      }

      const float overshoot = firstFrame - position;
      position = firstFrame + overshoot;
      reverse = false;
    }
  }
}

PICOTRACKER_FAST_CODE("sample_source_normalize_playback")
inline bool normalizePlaybackPosition(sampleSourceRenderParams &rp,
                                      SampleSourceInstrumentLoopMode loopMode,
                                      float &position, bool &reverse) {
  switch (loopMode) {
  case SSLM_ONESHOT:
    if (!reverse) {
      if (position < static_cast<float>(rp.rendLoopEnd_)) {
        return true;
      }
    } else {
      if (position >= static_cast<float>(rp.rendLoopEnd_)) {
        return true;
      }
    }
    startDeclickTail(rp);
    return false;

  case SSLM_REVERSE:
    if (isReverseOneShotInRange(rp, position)) {
      reverse = true;
      return true;
    }
    startDeclickTail(rp);
    return false;

  case SSLM_LOOP:
  case SSLM_REVERSE_LOOP:
  case SSLM_OSC:
  case SSLM_LOOPSYNC:
    normalizeWrappedPosition(rp.rendLoopStart_, rp.rendLoopEnd_, position,
                             reverse);
    return true;

  case SSLM_LOOP_PINGPONG:
    normalizePingPongPosition(rp.rendLoopStart_, rp.rendLoopEnd_, position,
                              reverse);
    return true;

  case SSLM_LAST:
    NAssert(0);
    return false;
  }

  return false;
}

PICOTRACKER_FAST_CODE("sample_source_interpolation_bounds")
inline bool getInterpolationBounds(const sampleSourceRenderParams &rp,
                                   SampleSourceInstrumentLoopMode loopMode,
                                   int sampleFrameCount, float position,
                                   int &firstFrame, int &lastFrame,
                                   SamplePlaybackTopology &topology) {
  if (sampleFrameCount <= 0) {
    return false;
  }

  topology = SamplePlaybackTopology::Clamp;
  int rangeStart = rp.rendFirst_;
  int rangeEnd = rp.rendLoopEnd_;

  if (playbackModeUsesLoopPoint(loopMode)) {
    const int loopLow = std::min(rp.rendLoopStart_, rp.rendLoopEnd_);
    const int loopHigh = std::max(rp.rendLoopStart_, rp.rendLoopEnd_);
    if (position >= static_cast<float>(loopLow) &&
        position <= static_cast<float>(loopHigh)) {
      rangeStart = rp.rendLoopStart_;
      rangeEnd = rp.rendLoopEnd_;
      topology = getPlaybackTopology(loopMode);
    }
  }

  firstFrame = std::min(rangeStart, rangeEnd);
  lastFrame = std::max(rangeStart, rangeEnd);
  if (rangeStart <= rangeEnd) {
    lastFrame -= 1;
  }

  if (lastFrame < firstFrame) {
    lastFrame = firstFrame;
  }

  firstFrame = clampFrameIndex(firstFrame, 0, sampleFrameCount - 1);
  lastFrame = clampFrameIndex(lastFrame, firstFrame, sampleFrameCount - 1);
  return true;
}

PICOTRACKER_FAST_CODE("sample_source_crossfade_target")
inline bool getLoopCrossfadeTarget(const sampleSourceRenderParams &rp,
                                   SampleSourceInstrumentLoopMode loopMode,
                                   float position, bool reverse, float speed,
                                   float &targetPosition,
                                   AudioSample &targetMix) {
  targetPosition = 0.0f;
  targetMix = 0.0f;

  if (!usesLoopCrossfade(loopMode) || speed <= 0.0f ||
      rp.rendLoopEnd_ <= rp.rendLoopStart_) {
    return false;
  }

  const int loopLength = rp.rendLoopEnd_ - rp.rendLoopStart_;
  if (loopLength <= kLoopCrossfadeSamples || speed >= loopLength) {
    return false;
  }

  const float loopStart = static_cast<float>(rp.rendLoopStart_);
  const float loopEnd = static_cast<float>(rp.rendLoopEnd_);
  const float crossfadeSamples = static_cast<float>(kLoopCrossfadeSamples);
  const float crossfadeDenominator =
      static_cast<float>(kLoopCrossfadeSamples - 1);

  // This is a pre-wrap crossfade into the position that will be heard after
  // the loop jumps, not the one-shot fade-to-silence tail.
  if (!reverse && loopMode == SSLM_LOOP) {
    const float distanceToWrap = loopEnd - position;
    const float samplesUntilWrap = distanceToWrap / speed;
    if (distanceToWrap <= 0.0f || samplesUntilWrap > crossfadeSamples) {
      return false;
    }

    targetMix = (crossfadeSamples - samplesUntilWrap) / crossfadeDenominator;
    const float overshoot = speed - distanceToWrap;
    targetPosition = loopStart + std::max(0.0f, overshoot);
  } else if (reverse && loopMode == SSLM_REVERSE_LOOP) {
    const float distanceToWrap = position - loopStart;
    const float samplesUntilWrap = (distanceToWrap / speed) + 1.0f;
    if (distanceToWrap < 0.0f || samplesUntilWrap > crossfadeSamples) {
      return false;
    }

    targetMix = (crossfadeSamples - samplesUntilWrap) / crossfadeDenominator;
    const float overshoot = speed - distanceToWrap;
    targetPosition = loopEnd - (overshoot > 0.0f ? overshoot : speed);
  } else {
    return false;
  }

  targetMix = std::clamp(targetMix, 0.0f, 1.0f);
  if (targetMix <= 0.0f) {
    return false;
  }

  if (targetPosition < loopStart) {
    targetPosition = loopStart;
  } else if (targetPosition >= loopEnd) {
    targetPosition = loopEnd - 1.0f;
  }
  return true;
}

PICOTRACKER_FAST_CODE("sample_source_interpolate_channel")
inline AudioSample interpolateChannelHermite(const int16_t *sampleFrames,
                                             int channelCount, int baseFrame,
                                             float fraction, int firstFrame,
                                             int lastFrame,
                                             SamplePlaybackTopology topology,
                                             int channelIndex) {
  if (baseFrame > firstFrame && (baseFrame + 2) <= lastFrame) {
    const AudioSample ym1 =
        pcmFrameSample(sampleFrames, channelCount, baseFrame - 1, channelIndex);
    const AudioSample y0 =
        pcmFrameSample(sampleFrames, channelCount, baseFrame, channelIndex);
    const AudioSample y1 =
        pcmFrameSample(sampleFrames, channelCount, baseFrame + 1, channelIndex);
    const AudioSample y2 =
        pcmFrameSample(sampleFrames, channelCount, baseFrame + 2, channelIndex);
    return hermiteInterpolate(ym1, y0, y1, y2, fraction);
  }

  const int frameM1 =
      resolveFrameIndex(baseFrame - 1, firstFrame, lastFrame, topology);
  const int frame0 =
      resolveFrameIndex(baseFrame, firstFrame, lastFrame, topology);
  const int frame1 =
      resolveFrameIndex(baseFrame + 1, firstFrame, lastFrame, topology);
  const int frame2 =
      resolveFrameIndex(baseFrame + 2, firstFrame, lastFrame, topology);

  const AudioSample ym1 =
      pcmFrameSample(sampleFrames, channelCount, frameM1, channelIndex);
  const AudioSample y0 =
      pcmFrameSample(sampleFrames, channelCount, frame0, channelIndex);
  const AudioSample y1 =
      pcmFrameSample(sampleFrames, channelCount, frame1, channelIndex);
  const AudioSample y2 =
      pcmFrameSample(sampleFrames, channelCount, frame2, channelIndex);
  return hermiteInterpolate(ym1, y0, y1, y2, fraction);
}

PICOTRACKER_FAST_CODE("sample_source_wrap_oscillator_frame")
inline int wrapOscillatorFrameIndex(int frame, int frameCount) {
  if (frame >= frameCount) {
    frame -= frameCount;
    if (frame >= frameCount) {
      frame %= frameCount;
    }
  } else if (frame < 0) {
    frame += frameCount;
    if (frame < 0) {
      frame %= frameCount;
      if (frame < 0) {
        frame += frameCount;
      }
    }
  }
  return frame;
}

PICOTRACKER_FAST_CODE("sample_source_oscillator_frame")
inline AudioSample oscillatorFrameSample(const int16_t *sampleFrames,
                                         int channelCount, int frameIndex,
                                         int channelIndex) {
  return static_cast<AudioSample>(
             sampleFrames[frameIndex * channelCount + channelIndex]) /
         kAudioPcmScale;
}

PICOTRACKER_FAST_CODE("sample_source_interpolate_oscillator")
inline AudioSample interpolateOscillatorChannelHermite(
    const int16_t *sampleFrames, int channelCount, int baseFrame,
    float fraction, int frameCount, int channelIndex) {
  if (frameCount <= 0) {
    return 0.0f;
  }
  if (frameCount == 1) {
    return oscillatorFrameSample(sampleFrames, channelCount, 0, channelIndex);
  }

  if (baseFrame > 0 && (baseFrame + 2) < frameCount) {
    const AudioSample ym1 = oscillatorFrameSample(sampleFrames, channelCount,
                                                  baseFrame - 1, channelIndex);
    const AudioSample y0 = oscillatorFrameSample(sampleFrames, channelCount,
                                                 baseFrame, channelIndex);
    const AudioSample y1 = oscillatorFrameSample(sampleFrames, channelCount,
                                                 baseFrame + 1, channelIndex);
    const AudioSample y2 = oscillatorFrameSample(sampleFrames, channelCount,
                                                 baseFrame + 2, channelIndex);
    return hermiteInterpolate(ym1, y0, y1, y2, fraction);
  }

  const int frameM1 = wrapOscillatorFrameIndex(baseFrame - 1, frameCount);
  const int frame1 = wrapOscillatorFrameIndex(baseFrame + 1, frameCount);
  const int frame2 = wrapOscillatorFrameIndex(baseFrame + 2, frameCount);
  const AudioSample ym1 =
      oscillatorFrameSample(sampleFrames, channelCount, frameM1, channelIndex);
  const AudioSample y0 = oscillatorFrameSample(sampleFrames, channelCount,
                                               baseFrame, channelIndex);
  const AudioSample y1 =
      oscillatorFrameSample(sampleFrames, channelCount, frame1, channelIndex);
  const AudioSample y2 =
      oscillatorFrameSample(sampleFrames, channelCount, frame2, channelIndex);
  return hermiteInterpolate(ym1, y0, y1, y2, fraction);
}

PICOTRACKER_FAST_CODE("sample_source_normalize_oscillator")
inline void normalizeOscillatorPhase(float &phase, float frameCount) {
  if (frameCount <= 0.0f) {
    phase = 0.0f;
    return;
  }

  if (phase >= frameCount || phase < 0.0f) {
    const int wraps = static_cast<int>(phase / frameCount);
    phase -= static_cast<float>(wraps) * frameCount;
    if (phase >= frameCount) {
      phase -= frameCount;
    }
    while (phase < 0.0f) {
      phase += frameCount;
    }
  }
}

inline void resetDeclickState(sampleSourceRenderParams &rp) {
  rp.finished_ = false;
  rp.sourceEnded_ = false;
  rp.tailSamplesRemaining_ = 0;
  rp.tailOriginLeft_ = 0.0f;
  rp.tailOriginRight_ = 0.0f;
  rp.lastOutputLeft_ = 0.0f;
  rp.lastOutputRight_ = 0.0f;
}

PICOTRACKER_FAST_CODE("sample_source_should_declick")
inline bool shouldStartDeclickTail(const sampleSourceRenderParams &rp) {
  return fabsf(rp.lastOutputLeft_) > kDeclickSilenceThreshold ||
         fabsf(rp.lastOutputRight_) > kDeclickSilenceThreshold;
}

PICOTRACKER_FAST_CODE("sample_source_start_declick")
inline bool startDeclickTail(sampleSourceRenderParams &rp) {
  rp.sourceEnded_ = true;
  if (!shouldStartDeclickTail(rp)) {
    rp.finished_ = true;
    rp.tailSamplesRemaining_ = 0;
    rp.tailOriginLeft_ = 0.0f;
    rp.tailOriginRight_ = 0.0f;
    rp.lastOutputLeft_ = 0.0f;
    rp.lastOutputRight_ = 0.0f;
    return false;
  }

  rp.tailSamplesRemaining_ = kDeclickTailSamples;
  rp.tailOriginLeft_ = rp.lastOutputLeft_;
  rp.tailOriginRight_ = rp.lastOutputRight_;
  return true;
}

PICOTRACKER_FAST_CODE("sample_source_render_declick")
inline void renderDeclickTail(sampleSourceRenderParams &rp,
                              AudioSample *&result, int &count) {
  while (count > 0 && rp.tailSamplesRemaining_ > 0) {
    const AudioSample factor =
        static_cast<AudioSample>(rp.tailSamplesRemaining_ - 1) /
        static_cast<AudioSample>(kDeclickTailSamples);
    const AudioSample left = rp.tailOriginLeft_ * factor;
    const AudioSample right = rp.tailOriginRight_ * factor;

    if (rp.channelCount_ > 0) {
      *result++ = left;
    }
    if (rp.channelCount_ > 1) {
      *result++ = right;
    }

    rp.lastOutputLeft_ = left;
    rp.lastOutputRight_ = right;
    rp.tailSamplesRemaining_--;
    count--;
  }

  if (rp.tailSamplesRemaining_ == 0) {
    rp.finished_ = true;
    rp.tailOriginLeft_ = 0.0f;
    rp.tailOriginRight_ = 0.0f;
    rp.lastOutputLeft_ = 0.0f;
    rp.lastOutputRight_ = 0.0f;
  }
}
} // namespace

sampleSourceRenderParams
    SampleSourceInstrument::renderParams_[SONG_CHANNEL_COUNT];

SampleSourceInstrument::SampleSourceInstrument()
    : I_Instrument(&variables_), source_(nullptr), running_(false),
      dirty_(false), sample_(FourCC::SampleSourceInstrumentSample),
      level_(FourCC::SampleSourceInstrumentLevel, 0xc0),
      rootNote_(FourCC::SampleSourceInstrumentRootNote, 60),
      start_(FourCC::SampleSourceInstrumentStart, 0),
      loopMode_(FourCC::SampleSourceInstrumentLoopMode,
                sampleSourcePlaybackModes, SSLM_LAST, 0),
      loopStart_(FourCC::SampleSourceInstrumentLoopStart, 0),
      loopEnd_(FourCC::SampleSourceInstrumentEnd, 0),
      table_(FourCC::SampleSourceInstrumentTable, -1),
      tableAuto_(FourCC::SampleSourceInstrumentTableAutomation, false) {
  variables_.insert(variables_.end(), &sample_);
  sample_.AddObserver(*this);
  variables_.insert(variables_.end(), &level_);
  variables_.insert(variables_.end(), &rootNote_);
  variables_.insert(variables_.end(), &start_);
  start_.AddObserver(*this);
  variables_.insert(variables_.end(), &loopMode_);
  loopMode_.SetInt(0);
  variables_.insert(variables_.end(), &loopStart_);
  loopStart_.AddObserver(*this);
  variables_.insert(variables_.end(), &loopEnd_);
  loopEnd_.AddObserver(*this);
  variables_.insert(variables_.end(), &table_);
  variables_.insert(variables_.end(), &tableAuto_);

  tableState_.Reset();
  slicePoints_.fill(0);
}

SampleSourceInstrument::~SampleSourceInstrument() {}

uint32_t SampleSourceInstrument::GetSlicePoint(size_t index) const {
  if (index >= MaxSlices) {
    return 0;
  }
  return slicePoints_[index];
}

void SampleSourceInstrument::SetSlicePoint(size_t index, uint32_t start) {
  if (index >= MaxSlices) {
    return;
  }

  uint32_t clamped = start;
  if (source_ && !source_->IsMulti()) {
    int size = source_->GetSize(0);
    if (size > 0) {
      uint32_t limit = static_cast<uint32_t>(size);
      if (clamped > limit) {
        clamped = limit;
      }
    }
  }
  if (index > 0) {
    uint32_t prev = slicePoints_[index - 1];
    if (clamped < prev) {
      clamped = prev;
    }
  }

  bool changed = false;
  if (slicePoints_[index] != clamped) {
    slicePoints_[index] = clamped;
    changed = true;
  }

  for (size_t i = index + 1; i < MaxSlices; ++i) {
    if (slicePoints_[i] == 0) {
      break;
    }
    if (slicePoints_[i] < clamped) {
      slicePoints_[i] = clamped;
      changed = true;
    }
  }

  if (changed) {
    SetChanged();
    NotifyObservers();
  }
}

void SampleSourceInstrument::ClearSlices() {
  bool hadSlices = hasAnySliceValue();
  slicePoints_.fill(0);
  if (hadSlices) {
    SetChanged();
    NotifyObservers();
  }
}

bool SampleSourceInstrument::HasSlicesForPlayback() const {
  return hasAnySliceValue();
}

bool SampleSourceInstrument::HasSlicesForWarning() const {
  return hasAnySliceValue();
}

bool SampleSourceInstrument::IsSliceDefined(size_t index) const {
  return isSliceIndexActive(index);
}

bool SampleSourceInstrument::PlaybackModeUsesLoopPoint(int playbackMode) {
  return playbackModeUsesLoopPoint(
      static_cast<SampleSourceInstrumentLoopMode>(playbackMode));
}

bool SampleSourceInstrument::ShouldDisplaySliceForNote(uint8_t midinote) const {
  if (!HasSlicesForPlayback()) {
    return false;
  }
  if (source_ == nullptr || source_->IsMulti()) {
    return false;
  }
  if (midinote < SliceNoteBase) {
    return false;
  }
  size_t index = midinote - SliceNoteBase;
  if (index >= MaxSlices) {
    return false;
  }
  if (!isSliceIndexActive(index)) {
    return false;
  }
  int size = source_->GetSize(0);
  if (size <= 0) {
    return false;
  }
  uint32_t sampleSize = static_cast<uint32_t>(size);
  uint32_t start = computeSliceStart(index, sampleSize);
  uint32_t end = computeSliceEnd(index, sampleSize);
  return start < end;
}

bool SampleSourceInstrument::GetSliceNoteRange(uint8_t &first,
                                               uint8_t &last) const {
  if (!HasSlicesForPlayback()) {
    return false;
  }
  bool found = false;
  size_t firstIndex = 0;
  size_t lastIndex = 0;
  for (size_t i = 0; i < MaxSlices; ++i) {
    if (isSliceIndexActive(i)) {
      if (!found) {
        firstIndex = i;
        found = true;
      }
      lastIndex = i;
    }
  }
  if (!found) {
    return false;
  }
  first = static_cast<uint8_t>(SliceNoteBase + firstIndex);
  last = static_cast<uint8_t>(SliceNoteBase + lastIndex);
  return true;
}

bool SampleSourceInstrument::hasAnySliceValue() const {
  for (auto value : slicePoints_) {
    if (value > 0) {
      return true;
    }
  }
  return false;
}

bool SampleSourceInstrument::isSliceIndexActive(size_t index) const {
  if (index >= MaxSlices) {
    return false;
  }
  if (index == 0) {
    if (slicePoints_[0] > 0) {
      return true;
    }
    for (size_t i = 1; i < MaxSlices; ++i) {
      if (slicePoints_[i] > 0) {
        return true;
      }
    }
    return false;
  }
  return slicePoints_[index] > 0;
}

bool SampleSourceInstrument::shouldUseSlice(unsigned char midinote,
                                            size_t &sliceIndex,
                                            uint32_t sampleSize) const {
  if (!HasSlicesForPlayback()) {
    return false;
  }
  if (source_ == nullptr || source_->IsMulti()) {
    return false;
  }
  if (midinote < SliceNoteBase) {
    return false;
  }
  size_t index = midinote - SliceNoteBase;
  if (index >= MaxSlices) {
    return false;
  }
  if (!isSliceIndexActive(index)) {
    return false;
  }
  uint32_t start = computeSliceStart(index, sampleSize);
  uint32_t end = computeSliceEnd(index, sampleSize);
  if (start >= end) {
    return false;
  }
  sliceIndex = index;
  return true;
}

uint32_t SampleSourceInstrument::computeSliceStart(size_t index,
                                                   uint32_t sampleSize) const {
  if (index >= MaxSlices || sampleSize == 0) {
    return 0;
  }
  uint32_t stored = slicePoints_[index];
  if (index == 0 && stored == 0) {
    return 0;
  }
  if (stored > sampleSize) {
    return sampleSize;
  }
  return stored;
}

uint32_t SampleSourceInstrument::computeSliceEnd(size_t index,
                                                 uint32_t sampleSize) const {
  if (sampleSize == 0) {
    return 0;
  }
  uint32_t start = computeSliceStart(index, sampleSize);
  uint32_t end = sampleSize;
  for (size_t i = index + 1; i < MaxSlices; ++i) {
    uint32_t candidate = slicePoints_[i];
    if (candidate > start && candidate < end) {
      end = candidate;
    }
  }
  if (end <= start) {
    end = std::min(start + 1, sampleSize);
  }
  return end;
}

void SampleSourceInstrument::clampSlicePoints(uint32_t sampleSize) {
  bool changed = false;
  if (sampleSize == 0) {
    for (auto &value : slicePoints_) {
      if (value != 0) {
        value = 0;
        changed = true;
      }
    }
  } else {
    for (auto &value : slicePoints_) {
      if (value > sampleSize) {
        value = sampleSize;
        changed = true;
      }
    }
  }
  if (changed) {
    SetChanged();
    NotifyObservers();
  }
}

bool SampleSourceInstrument::Init() {
  SamplePool *pool = SamplePool::GetInstance();
  Variable *vSample = FindVariable(FourCC::SampleSourceInstrumentSample);
  NAssert(vSample);
  int index = vSample->GetInt();
  source_ = (index >= 0) ? pool->GetSource(index) : nullptr;
  tableState_.Reset();
  return false;
}

void SampleSourceInstrument::OnStart() { tableState_.Reset(); }

unsigned char SampleSourceInstrument::GetPreviewNote() {
  return static_cast<unsigned char>(std::clamp(rootNote_.GetInt(), 0, 127));
}

bool SampleSourceInstrument::Start(int channel, unsigned char midinote,
                                   bool cleanstart) {
  if (dirty_) {
    updateInstrumentData(false);
  }

  running_ = true;
  if (source_ == nullptr) {
    return false;
  }

  sampleSourceRenderParams *rp = renderParams_ + channel;
  const bool resumePosition = (!cleanstart) && (!rp->finished_);
  rp->midiNote_ = midinote;

  rp->sampleBuffer_ = source_->GetSampleBuffer(rp->midiNote_);
  if (rp->sampleBuffer_ == nullptr) {
    return false;
  }

  rp->channelCount_ = source_->GetChannelCount(rp->midiNote_);
  rp->levelGain_ = level_.GetInt() / 255.0;
  int sampleSize = source_->GetSize(rp->midiNote_);
  uint32_t sampleSizeU = sampleSize > 0 ? static_cast<uint32_t>(sampleSize) : 0;

  int rootNote =
      (rootNote_.GetInt() - 60) + source_->GetRootNote(rp->midiNote_);

  if (!source_->IsMulti()) {
    rp->rendLoopStart_ = loopStart_.GetInt();
    rp->rendLoopEnd_ = loopEnd_.GetInt();
  } else {
    long start = source_->GetLoopStart(rp->midiNote_);
    if (start > 0) {
      rp->rendLoopStart_ = source_->GetLoopStart(rp->midiNote_);
      rp->rendLoopEnd_ = source_->GetLoopEnd(rp->midiNote_);
      loopMode_.SetInt(SSLM_LOOP);
    } else {
      rp->rendLoopStart_ = 0;
      rp->rendLoopEnd_ = source_->GetSize(rp->midiNote_);
      loopMode_.SetInt(SSLM_ONESHOT);
    }
  }
  SampleSourceInstrumentLoopMode loopmode =
      static_cast<SampleSourceInstrumentLoopMode>(loopMode_.GetInt());

  size_t sliceIndex = 0;
  bool sliceActive = shouldUseSlice(midinote, sliceIndex, sampleSizeU);
  if (!sliceActive && HasSlicesForPlayback() && midinote >= SliceNoteBase &&
      midinote < static_cast<unsigned char>(SliceNoteBase + MaxSlices)) {
    return false;
  }

  uint32_t sliceStart = 0;
  uint32_t sliceEnd = 0;
  if (sliceActive) {
    sliceStart = computeSliceStart(sliceIndex, sampleSizeU);
    sliceEnd = computeSliceEnd(sliceIndex, sampleSizeU);
    if (sliceStart >= sliceEnd) {
      sliceActive = false;
    }
  }

  rp->sliceActive_ = sliceActive;
  rp->activeSliceIndex_ = sliceActive ? static_cast<uint8_t>(sliceIndex) : 0;

  if (sliceActive) {
    loopmode = SSLM_ONESHOT;
  }
  rp->loopModeValue_ = static_cast<int>(loopmode);
  rp->reverse_ = false;

  float driverRate = static_cast<float>(Audio::GetInstance()->GetSampleRate());
  int oscOriginalLoopStart = 0;
  int oscOriginalLoopEnd = 0;
  int oscOriginalLength = 0;
  int oscRenderLength = 0;
  bool oscLoopClamped = false;

  switch (loopmode) {
  case SSLM_ONESHOT:
  case SSLM_LOOP:
  case SSLM_LOOP_PINGPONG:
    rp->rendFirst_ = start_.GetInt();
    if (!resumePosition) {
      rp->position_ = static_cast<float>(rp->rendFirst_);
    }
    rp->baseSpeed_ = source_->GetSampleRate(rp->midiNote_) / driverRate;
    rp->reverse_ = (rp->rendLoopEnd_ < rp->position_);
    break;

  case SSLM_REVERSE_LOOP:
    rp->rendFirst_ = start_.GetInt();
    if (!resumePosition) {
      int reverseLoopStart = rp->rendFirst_;
      if (rp->rendLoopEnd_ > rp->rendLoopStart_) {
        reverseLoopStart = rp->rendLoopEnd_ - 1;
      } else if (rp->rendLoopEnd_ < rp->rendLoopStart_) {
        reverseLoopStart = rp->rendLoopEnd_;
      }
      rp->position_ = static_cast<float>(reverseLoopStart);
    }
    rp->baseSpeed_ = source_->GetSampleRate(rp->midiNote_) / driverRate;
    rp->reverse_ = true;
    break;

  case SSLM_REVERSE:
    rp->rendFirst_ = start_.GetInt();
    if (!resumePosition) {
      int reverseStart = rp->rendFirst_;
      if (rp->rendLoopEnd_ > rp->rendFirst_) {
        reverseStart = rp->rendLoopEnd_ - 1;
      }
      rp->position_ = static_cast<float>(reverseStart);
    }
    rp->baseSpeed_ = source_->GetSampleRate(rp->midiNote_) / driverRate;
    rp->reverse_ = true;
    break;

  case SSLM_OSC: {
    float freq = kOscBaseFrequency;
    oscOriginalLoopStart = rp->rendLoopStart_;
    oscOriginalLoopEnd = rp->rendLoopEnd_;
    int length = rp->rendLoopEnd_ - rp->rendLoopStart_;
    if (length == 0) {
      length = 1;
    }
    if (length < 0) {
      rp->reverse_ = true;
      length = -length;
    }
    oscOriginalLength = length;
    if (length > SampleSourceInstrument::MaxOscillatorSampleFrames) {
      length = SampleSourceInstrument::MaxOscillatorSampleFrames;
      rp->rendLoopEnd_ = rp->reverse_ ? rp->rendLoopStart_ - length
                                      : rp->rendLoopStart_ + length;
      oscLoopClamped = true;
    }
    oscRenderLength = length;
    rp->baseSpeed_ = (freq * length) / driverRate;
    rp->rendFirst_ = rp->rendLoopStart_;
    if (!resumePosition) {
      rp->position_ = static_cast<float>(rp->rendFirst_);
    }
    break;
  }

  case SSLM_LOOPSYNC: {
    int length = rp->rendLoopEnd_ - rp->rendLoopStart_;
    if (length < 0) {
      rp->reverse_ = true;
      length = -length;
    }
    SyncMaster *sm = SyncMaster::GetInstance();
    int sampleCount = static_cast<int>(sm->GetTickSampleCount());
    sampleCount *= (6 * 16);
    rp->baseSpeed_ = length / static_cast<float>(sampleCount);
    rp->rendFirst_ = rp->rendLoopStart_;
    if (!resumePosition) {
      rp->position_ = static_cast<float>(rp->rendFirst_);
    }
    break;
  }

  case SSLM_LAST:
    NAssert(0);
    break;
  }

  if (sliceActive) {
    rp->rendLoopStart_ = static_cast<int>(sliceStart);
    rp->rendLoopEnd_ = static_cast<int>(sliceEnd);
    rp->rendFirst_ = static_cast<int>(sliceStart);
    rp->position_ = static_cast<float>(sliceStart);
    rp->reverse_ = false;
  }

  int offset = midinote - rootNote;
  if (sliceActive) {
    offset = 0;
  }
  while (offset > 127) {
    offset -= 12;
  }

  float freqFactor = static_cast<float>(pow(2.0, offset / 12.0));
  rp->baseSpeed_ = rp->baseSpeed_ * freqFactor;
  rp->speed_ = rp->baseSpeed_;
#if !defined(HOST_TEST)
  if (loopmode == SSLM_OSC) {
    Trace::Log("SAMPLE_SOURCE",
               "OSC start note=%d source=%d origLoop=%d..%d origLen=%d "
               "renderLoop=%d..%d renderLen=%d channelCount=%d offset=%d "
               "baseSpeed=%f speed=%f clamped=%d",
               midinote, sampleSize, oscOriginalLoopStart, oscOriginalLoopEnd,
               oscOriginalLength, rp->rendLoopStart_, rp->rendLoopEnd_,
               oscRenderLength, rp->channelCount_, offset, rp->baseSpeed_,
               rp->speed_, oscLoopClamped ? 1 : 0);
  }
#else
  (void)oscOriginalLoopStart;
  (void)oscOriginalLoopEnd;
  (void)oscOriginalLength;
  (void)oscRenderLength;
  (void)oscLoopClamped;
#endif
  resetDeclickState(*rp);

  if (cleanstart) {
    rp->retrigger_.Reset();
    rp->couldClick_ = kShouldKillClicks;
  }
  return true;
}

void SampleSourceInstrument::Stop(int channel) {
  sampleSourceRenderParams *rp = renderParams_ + channel;
  rp->sourceEnded_ = false;
  rp->tailSamplesRemaining_ = 0;
  rp->tailOriginLeft_ = 0.0f;
  rp->tailOriginRight_ = 0.0f;
  rp->lastOutputLeft_ = 0.0f;
  rp->lastOutputRight_ = 0.0f;
  rp->finished_ = true;
}

void SampleSourceInstrument::SetPitchMultiplier(int channel, float multiplier) {
  sampleSourceRenderParams &renderParams = renderParams_[channel];
  renderParams.speed_ = renderParams.baseSpeed_ * multiplier;
}

PICOTRACKER_FAST_CODE("sample_source_render")
bool SampleSourceInstrument::Render(int channel, AudioSample *buffer, int size,
                                    bool updateTick) {
  bool somethingToMix = false;

  sampleSourceRenderParams *rp = renderParams_ + channel;
  bool *rpFinished = &(rp->finished_);

  if (source_) {
    if (*rpFinished) {
      return false;
    }

    if (updateTick && rp->retrigger_.AdvanceTick()) {
      ApplySampleRetrigger(rp->position_, rp->speed_,
                           SyncMaster::GetInstance()->GetTickSampleCount(),
                           rp->retrigger_);
    }

    SampleSourceInstrumentLoopMode loopMode =
        static_cast<SampleSourceInstrumentLoopMode>(rp->loopModeValue_);

    const int16_t *sampleFrames =
        static_cast<const int16_t *>(rp->sampleBuffer_);
    int channelCount = rp->channelCount_;
    memset(buffer, 0, size * channelCount * sizeof(AudioSample));
    int count = size;
    AudioSample *result = buffer;
    const AudioSample levelGain = rp->levelGain_;
    int sampleFrameCount = source_->GetSize(rp->midiNote_);
    if (sampleFrameCount <= 0) {
      return false;
    }

    float position = rp->position_;
    bool rpReverse = rp->reverse_;

    // OSC playback can advance tens of source frames per output sample, and
    // LOOPSYNC does not need the generic renderer's loop-crossfade checks.
    // Keep their common forward case on a local wrapped phase and direct
    // Hermite lookup; unusual/reverse ranges fall through below.
    if ((loopMode == SSLM_OSC || loopMode == SSLM_LOOPSYNC) && !rpReverse &&
        channelCount > 0 && rp->rendLoopEnd_ > rp->rendLoopStart_ &&
        rp->rendLoopStart_ >= 0 && rp->rendLoopEnd_ <= sampleFrameCount) {
      const int oscFrameCount = rp->rendLoopEnd_ - rp->rendLoopStart_;
      const int16_t *oscFrames =
          sampleFrames + rp->rendLoopStart_ * channelCount;
      const float oscFrameCountFloat = static_cast<float>(oscFrameCount);
      float oscillatorPhase = position - static_cast<float>(rp->rendLoopStart_);
      normalizeOscillatorPhase(oscillatorPhase, oscFrameCountFloat);

      while (count > 0) {
        if (rp->sourceEnded_) {
          renderDeclickTail(*rp, result, count);
          if (*rpFinished) {
            break;
          }
          continue;
        }

        int baseFrame = static_cast<int>(oscillatorPhase);
        if (baseFrame >= oscFrameCount) {
          baseFrame = 0;
          oscillatorPhase = 0.0f;
        }
        const float fraction = oscillatorPhase - static_cast<float>(baseFrame);

        AudioSample left = interpolateOscillatorChannelHermite(
            oscFrames, channelCount, baseFrame, fraction, oscFrameCount, 0);
        AudioSample right = left;
        if (channelCount > 1) {
          right = interpolateOscillatorChannelHermite(
              oscFrames, channelCount, baseFrame, fraction, oscFrameCount, 1);
        }

        *result++ = left * levelGain;
        rp->lastOutputLeft_ = left * levelGain;
        if (channelCount > 1) {
          *result++ = right * levelGain;
          rp->lastOutputRight_ = right * levelGain;
        } else {
          rp->lastOutputRight_ = rp->lastOutputLeft_;
        }

        oscillatorPhase += rp->speed_;
        normalizeOscillatorPhase(oscillatorPhase, oscFrameCountFloat);
        count--;
      }

      rp->reverse_ = rpReverse;
      rp->position_ = static_cast<float>(rp->rendLoopStart_) + oscillatorPhase;

      return true;
    }

    while (count > 0) {
      if (rp->sourceEnded_) {
        renderDeclickTail(*rp, result, count);
        if (*rpFinished) {
          break;
        }
        continue;
      }

      if (!normalizePlaybackPosition(*rp, loopMode, position, rpReverse)) {
        if (*rpFinished) {
          break;
        }
        if (rp->sourceEnded_) {
          continue;
        }
      }

      int firstFrame = 0;
      int lastFrame = 0;
      SamplePlaybackTopology topology = SamplePlaybackTopology::Clamp;
      if (!getInterpolationBounds(*rp, loopMode, sampleFrameCount, position,
                                  firstFrame, lastFrame, topology)) {
        break;
      }

      float samplePosition = position;
      if (topology == SamplePlaybackTopology::Clamp) {
        samplePosition =
            std::clamp(samplePosition, static_cast<float>(firstFrame),
                       static_cast<float>(lastFrame));
      }

      int baseFrame = static_cast<int>(floorf(samplePosition));
      float fraction = samplePosition - static_cast<float>(baseFrame);
      if (topology == SamplePlaybackTopology::Clamp && baseFrame >= lastFrame) {
        baseFrame = lastFrame;
        fraction = 0.0f;
      }

      AudioSample left = 0.0f;
      AudioSample right = 0.0f;
      if (channelCount > 0) {
        left = interpolateChannelHermite(sampleFrames, channelCount, baseFrame,
                                         fraction, firstFrame, lastFrame,
                                         topology, 0);
      }
      if (channelCount > 1) {
        right = interpolateChannelHermite(sampleFrames, channelCount, baseFrame,
                                          fraction, firstFrame, lastFrame,
                                          topology, 1);
      } else {
        right = left;
      }

      float crossfadePosition = 0.0f;
      AudioSample crossfadeMix = 0.0f;
      if (channelCount > 0 && topology == SamplePlaybackTopology::Wrap &&
          getLoopCrossfadeTarget(*rp, loopMode, samplePosition, rpReverse,
                                 fabsf(rp->speed_), crossfadePosition,
                                 crossfadeMix)) {
        const int crossfadeBaseFrame =
            static_cast<int>(floorf(crossfadePosition));
        const float crossfadeFraction =
            crossfadePosition - static_cast<float>(crossfadeBaseFrame);
        AudioSample crossfadeLeft = interpolateChannelHermite(
            sampleFrames, channelCount, crossfadeBaseFrame, crossfadeFraction,
            firstFrame, lastFrame, topology, 0);
        AudioSample crossfadeRight = crossfadeLeft;
        if (channelCount > 1) {
          crossfadeRight = interpolateChannelHermite(
              sampleFrames, channelCount, crossfadeBaseFrame, crossfadeFraction,
              firstFrame, lastFrame, topology, 1);
        }

        const AudioSample currentMix = 1.0f - crossfadeMix;
        left = left * currentMix + crossfadeLeft * crossfadeMix;
        right = right * currentMix + crossfadeRight * crossfadeMix;
      }

      *result++ = left * levelGain;
      rp->lastOutputLeft_ = left * levelGain;
      if (channelCount > 1) {
        *result++ = right * levelGain;
        rp->lastOutputRight_ = right * levelGain;
      } else {
        rp->lastOutputRight_ = rp->lastOutputLeft_;
      }

      position += rpReverse ? -rp->speed_ : rp->speed_;
      count--;
    }

    rp->reverse_ = rpReverse;
    rp->position_ = position;

    somethingToMix = true;
  }

  return somethingToMix;
}

PICOTRACKER_FAST_CODE("sample_source_buffer_layout")
AudioBufferLayout
SampleSourceInstrument::GetAudioBufferLayout(int channel) const {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT) {
    return AudioBufferLayout::StereoInterleaved;
  }

  return renderParams_[channel].channelCount_ > 1
             ? AudioBufferLayout::StereoInterleaved
             : AudioBufferLayout::MonoPacked;
}

bool SampleSourceInstrument::GetPlaybackPosition(int channel,
                                                 uint32_t &position) const {
  position = 0;
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT || !source_) {
    return false;
  }

  const sampleSourceRenderParams &rp = renderParams_[channel];
  if (rp.finished_ || rp.sourceEnded_ || rp.sampleBuffer_ == nullptr) {
    return false;
  }

  int sampleSize = source_->GetSize(rp.midiNote_);
  if (sampleSize <= 0) {
    return false;
  }

  float samplePosition = rp.position_;
  if (samplePosition < 0.0f) {
    samplePosition = 0.0f;
  }

  uint32_t maxPosition = static_cast<uint32_t>(sampleSize - 1);
  if (samplePosition >= static_cast<float>(sampleSize)) {
    position = maxPosition;
    return true;
  }

  position = static_cast<uint32_t>(samplePosition);
  if (position > maxPosition) {
    position = maxPosition;
  }
  return true;
}

void SampleSourceInstrument::AssignSample(int i) {
  Variable *v = FindVariable(FourCC::SampleSourceInstrumentSample);
  v->SetInt(i);
  ResetOscillatorLoopModeIfSampleTooLarge();
}

int SampleSourceInstrument::GetSampleIndex() {
  Variable *v = FindVariable(FourCC::SampleSourceInstrumentSample);
  return v->GetInt();
}

int SampleSourceInstrument::GetSampleSize(int channel) {
  if (channel >= 0 && channel < SONG_CHANNEL_COUNT) {
    if (!source_) {
      return 0;
    }
    sampleSourceRenderParams *rp = renderParams_ + channel;
    return source_->GetSize(rp->midiNote_);
  }

  return getSelectedSampleSize();
}

int SampleSourceInstrument::getSelectedSampleSize() {
  Variable *vSample = FindVariable(FourCC::SampleSourceInstrumentSample);
  if (vSample) {
    int index = vSample->GetInt();
    SamplePool *pool = SamplePool::GetInstance();
    if (pool && index >= 0 && index < pool->GetNameListSize()) {
      SoundSource *source = pool->GetSource(static_cast<uint32_t>(index));
      if (source) {
        return source->GetSize(-1);
      }
    }
  }

  if (source_) {
    return source_->GetSize(-1);
  }

  return 0;
}

bool SampleSourceInstrument::ResetOscillatorLoopModeIfSampleTooLarge() {
  if (loopMode_.GetInt() != SSLM_OSC) {
    return false;
  }

  if (getSelectedSampleSize() <= MaxOscillatorSampleFrames) {
    return false;
  }

  loopMode_.SetInt(SSLM_ONESHOT);
  return true;
}

bool SampleSourceInstrument::SkipOscillatorLoopModeIfSampleTooLarge(
    int direction) {
  if (loopMode_.GetInt() != SSLM_OSC) {
    return false;
  }

  if (getSelectedSampleSize() <= MaxOscillatorSampleFrames) {
    return false;
  }

  loopMode_.SetInt(direction < 0 ? SSLM_LOOP_PINGPONG : SSLM_LOOPSYNC);
  return true;
}

float SampleSourceInstrument::GetLengthInSec() {
  if (source_) {
    return source_->GetLengthInSec();
  }
  return 0.0f;
}

bool SampleSourceInstrument::IsInitialized() { return (source_ != nullptr); }

bool SampleSourceInstrument::ShouldAuditionParameterChange(
    FourCC parameter) const {
  switch (parameter) {
  case FourCC::SampleSourceInstrumentLevel:
  case FourCC::SampleSourceInstrumentLoopMode:
  case FourCC::SampleSourceInstrumentStart:
  case FourCC::SampleSourceInstrumentLoopStart:
  case FourCC::SampleSourceInstrumentEnd:
    return true;
  default:
    return false;
  }
}

void SampleSourceInstrument::updateInstrumentData(bool search) {
  (void)search;

  SamplePool *pool = SamplePool::GetInstance();
  Variable *vSample = FindVariable(FourCC::SampleSourceInstrumentSample);
  int index = vSample->GetInt();
  int instrSize = 0;

  source_ = nullptr;
  if (index >= pool->GetNameListSize()) {
    index = kNoSample;
    vSample->SetInt(kNoSample);
  }

  if (index != kNoSample) {
    source_ = pool->GetSource(index);
    if (source_ && !source_->IsMulti()) {
      instrSize = source_->GetSize(-1);
    }
  }

  // Skip resetting the user's edits when we're only tracking a pool re-index.
  if (!preserveEditsOnReload_) {
    Variable *v = FindVariable(FourCC::SampleSourceInstrumentEnd);
    v->SetInt(instrSize);
    v = FindVariable(FourCC::SampleSourceInstrumentLoopStart);
    v->SetInt(0);
    v = FindVariable(FourCC::SampleSourceInstrumentStart);
    v->SetInt(0);
    clampSlicePoints(static_cast<uint32_t>(instrSize));
  }
  preserveEditsOnReload_ = false;
  dirty_ = false;
}

void SampleSourceInstrument::Update(Observable &o, I_ObservableData *d) {
  (void)d;

  WatchedVariable &v = (WatchedVariable &)o;
  FourCC id = v.GetID();

  switch (id) {
  case FourCC::SampleSourceInstrumentSample:
    // A pool re-index (an entry below us was purged) shifts our unchanged
    // sample to a new pool position: reload the source pointer but keep the
    // user's start/loop/end/slice edits.
    preserveEditsOnReload_ = sample_.IsReindexing();
    if (running_) {
      dirty_ = true;
    } else {
      updateInstrumentData(true);
      SetChanged();
      NotifyObservers();
    }
    break;

  default:
    break;
  }
}

void SampleSourceInstrument::ProcessCommand(int channel, TrackerCommand cc,
                                            ushort value) {
  sampleSourceRenderParams *rp = renderParams_ + channel;
  if (!source_) {
    return;
  }

  switch (cc) {
  case TrackerCommand::InstrumentCommandLoopOfset:
    if (value > 0x8000) {
      value = 0x10000 - value;
      if (rp->rendLoopStart_ > value) {
        rp->rendLoopEnd_ -= value;
        rp->rendLoopStart_ -= value;
      }
    } else {
      if (rp->rendLoopEnd_ + value < source_->GetSize(rp->midiNote_)) {
        rp->rendLoopEnd_ += value;
        rp->rendLoopStart_ += value;
      }
    }
    break;

  case TrackerCommand::InstrumentCommandPlayOfset: {
    int wavSize = source_->GetSize(rp->midiNote_);
    float chkSize = wavSize / 256.0f;
    int absShft = value >> 8;
    if (absShft != 0) {
      rp->position_ = chkSize * absShft;
    }
    int relShift = value & 0xFF;
    if (relShift > 127) {
      relShift = relShift - 256;
    }
    rp->position_ += relShift * chkSize;
    while (rp->position_ < 0) {
      rp->position_ = wavSize + rp->position_;
    }
    while (rp->position_ >= wavSize) {
      rp->position_ -= wavSize;
    }
    rp->couldClick_ = kShouldKillClicks;
  } break;

  case TrackerCommand::InstrumentCommandRetrigger: {
    rp->retrigger_.Configure(value);
    if (rp->retrigger_.IsActive()) {
      rp->couldClick_ = kShouldKillClicks;
    }
  } break;

  default:
    break;
  }
}

etl::string<MAX_INSTRUMENT_NAME_LENGTH>
SampleSourceInstrument::GetUserSetName() {
  return I_Instrument::GetUserSetName();
}

etl::string<MAX_INSTRUMENT_FILENAME_LENGTH>
SampleSourceInstrument::GetSampleFileName() {
  Variable *v = FindVariable(FourCC::SampleSourceInstrumentSample);
  if (v) {
    return v->GetString();
  }
  return etl::string<MAX_INSTRUMENT_FILENAME_LENGTH>();
}

etl::string<MAX_INSTRUMENT_NAME_LENGTH>
SampleSourceInstrument::GetDisplayName() {
  auto userSetName = I_Instrument::GetUserSetName();
  if (!userSetName.empty()) {
    return userSetName;
  }

  auto sampleFileName = GetSampleFileName();
  if (sampleFileName.empty()) {
    return etl::string<MAX_INSTRUMENT_NAME_LENGTH>("NONE");
  }

  size_t dotPos = sampleFileName.find_last_of('.');
  if (dotPos != etl::string<MAX_INSTRUMENT_FILENAME_LENGTH>::npos) {
    return sampleFileName.substr(0, dotPos);
  }

  return sampleFileName;
}

void SampleSourceInstrument::SaveContent(tinyxml2::XMLPrinter *printer) {
  I_Instrument::SaveContent(printer);

  for (size_t i = 0; i < slicePoints_.size(); ++i) {
    if (slicePoints_[i] == 0) {
      continue;
    }
    printer->OpenElement("PARAM");
    char sliceName[6];
    npf_snprintf(sliceName, sizeof(sliceName), "SL%02u",
                 static_cast<unsigned>(i));
    printer->PushAttribute("NAME", sliceName);
    printer->PushAttribute("VALUE", static_cast<unsigned int>(slicePoints_[i]));
    printer->CloseElement();
  }
}

void SampleSourceInstrument::RestoreContent(PersistencyDocument *doc) {
  auto setSliceFromString = [this](const char *indexStr, const char *valueStr) {
    int idx = atoi(indexStr);
    if (idx < 0 || idx >= static_cast<int>(MaxSlices)) {
      return;
    }
    uint32_t value = static_cast<uint32_t>(strtoul(valueStr, nullptr, 10));
    slicePoints_[static_cast<size_t>(idx)] = value;
  };

  bool hasAttr = doc->NextAttribute();
  while (hasAttr) {
    if (!strcasecmp(doc->attrname_, "TYPE")) {
      Trace::Log("I_INSTRUMENT", "Instrument type from XML: %s", doc->attrval_);
    } else if (!strncasecmp(doc->attrname_, "SL", 2)) {
      setSliceFromString(doc->attrname_ + 2, doc->attrval_);
    }
    hasAttr = doc->NextAttribute();
  }

  bool subelem = doc->FirstChild();
  while (subelem) {
    bool attr = doc->NextAttribute();
    etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> name;
    etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> value;

    while (attr) {
      if (!strcasecmp(doc->attrname_, "NAME")) {
        name = doc->attrval_;
      }
      if (!strcasecmp(doc->attrname_, "VALUE")) {
        value = doc->attrval_;
      }
      attr = doc->NextAttribute();
    }

    if (!name.empty() && !value.empty()) {
      if (!strcasecmp(name.c_str(), "InstrumentName")) {
        SetName(value.c_str());
      } else if (!strncasecmp(name.c_str(), "SL", 2)) {
        setSliceFromString(name.c_str() + 2, value.c_str());
      } else {
        bool found = false;
        for (auto it = Variables()->begin(); it != Variables()->end(); ++it) {
          if (!strcasecmp((*it)->GetName(), name.c_str())) {
            (*it)->SetString(value.c_str());
            found = true;
            break;
          }
        }
        if (!found) {
          Trace::Error("Parameter '%s' not found in instrument", name.c_str());
        }
      }
    }

    subelem = doc->NextSibling();
  }

  Variable *nameVar = FindVariable(FourCC::InstrumentName);
  if (nameVar && !name_.empty()) {
    nameVar->SetString(name_.c_str());
  }
}

void SampleSourceInstrument::Purge() {
  auto it = variables_.begin();
  for (size_t i = 0; i < variables_.size(); ++i) {
    (*it)->Reset();
    ++it;
  }
  source_ = nullptr;
  slicePoints_.fill(0);
}

void SampleSourceInstrument::CloneFrom(I_Instrument &src) {
  I_Instrument::CloneFrom(src);
  auto &sampleSrc = static_cast<SampleSourceInstrument &>(src);
  slicePoints_ = sampleSrc.slicePoints_;
  SetChanged();
  NotifyObservers();
}

bool SampleSourceInstrument::IsEmpty() {
  Variable *v = FindVariable(FourCC::SampleSourceInstrumentSample);
  return (v->GetInt() == -1);
}

int SampleSourceInstrument::GetTable() {
  int result = table_.GetInt();
  if (result > TABLE_COUNT) {
    return VAR_OFF;
  }
  return result;
}

bool SampleSourceInstrument::GetTableAutomation() {
  return tableAuto_.GetBool();
}

void SampleSourceInstrument::GetTableState(TableSaveState &state) {
  memcpy(state.hopCount_, tableState_.hopCount_,
         sizeof(uchar) * TABLE_STEPS * 3);
  memcpy(state.position_, tableState_.position_, sizeof(int) * 3);
  state.groove_ = tableState_.groove_;
}

void SampleSourceInstrument::SetTableState(TableSaveState &state) {
  memcpy(tableState_.hopCount_, state.hopCount_,
         sizeof(uchar) * TABLE_STEPS * 3);
  memcpy(tableState_.position_, state.position_, sizeof(int) * 3);
  tableState_.groove_ = state.groove_;
}

bool SampleSourceInstrument::IsMulti() { return source_->IsMulti(); }
