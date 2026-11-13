/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#include "Application/Seq/SeqPartSerializer.h"
#include <cstring>

namespace picoSeq {

namespace {

void WriteU16(uint8_t *&p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p += 2;
}

void WriteU32(uint8_t *&p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
  p += 4;
}

bool ReadU16(const uint8_t *&p, const uint8_t *end, uint16_t &v) {
  if (p + 2 > end) {
    return false;
  }
  v = static_cast<uint16_t>(p[0] | (p[1] << 8));
  p += 2;
  return true;
}

bool ReadU32(const uint8_t *&p, const uint8_t *end, uint32_t &v) {
  if (p + 4 > end) {
    return false;
  }
  v = static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
  p += 4;
  return true;
}

} // namespace

std::size_t SeqPartSerializer::MaxEncodedSize() {
  // header 4+2+4+1+1 = 12; per track 4 + 8*kMaxEventsPerTrack
  return 12 +
         static_cast<std::size_t>(kTrackCount) *
             (4 + 8 * kMaxEventsPerTrack);
}

std::size_t SeqPartSerializer::EncodedSize(const SeqPart &part) {
  std::size_t size = 12;
  for (int t = 0; t < kTrackCount; ++t) {
    size += 4 + part.Track(t).events_.Size() * 8;
  }
  return size;
}

std::size_t SeqPartSerializer::SaveToBuffer(const SeqPart &part, uint8_t *buffer,
                                            std::size_t capacity) {
  const std::size_t need = EncodedSize(part);
  if (buffer == nullptr || capacity < need) {
    return 0;
  }

  uint8_t *p = buffer;
  std::memcpy(p, kPartMagic, 4);
  p += 4;
  WriteU16(p, kPartFormatVersion);
  WriteU32(p, part.LengthTicks());
  *p++ = part.Loop() ? 1 : 0;
  *p++ = static_cast<uint8_t>(kTrackCount);

  for (int t = 0; t < kTrackCount; ++t) {
    const MidiEventList &list = part.Track(t).events_;
    WriteU32(p, static_cast<uint32_t>(list.Size()));
    for (std::size_t i = 0; i < list.Size(); ++i) {
      const SeqEvent &e = list.At(i);
      WriteU32(p, e.tick_);
      *p++ = e.status_;
      *p++ = e.data1_;
      *p++ = e.data2_;
      *p++ = 0;
    }
  }

  return static_cast<std::size_t>(p - buffer);
}

bool SeqPartSerializer::LoadFromBuffer(SeqPart &part, const uint8_t *buffer,
                                       std::size_t size) {
  if (buffer == nullptr || size < 12) {
    return false;
  }

  const uint8_t *p = buffer;
  const uint8_t *end = buffer + size;

  if (std::memcmp(p, kPartMagic, 4) != 0) {
    return false;
  }
  p += 4;

  uint16_t version = 0;
  if (!ReadU16(p, end, version) || version != kPartFormatVersion) {
    return false;
  }

  uint32_t lengthTicks = 0;
  if (!ReadU32(p, end, lengthTicks)) {
    return false;
  }
  if (p + 2 > end) {
    return false;
  }
  const bool loop = (*p++ != 0);
  const uint8_t trackCount = *p++;
  if (trackCount != kTrackCount) {
    return false;
  }

  part.Reset();
  part.SetLengthTicks(lengthTicks);
  part.SetLoop(loop);

  for (int t = 0; t < kTrackCount; ++t) {
    uint32_t count = 0;
    if (!ReadU32(p, end, count)) {
      return false;
    }
    if (count > kMaxEventsPerTrack) {
      return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t tick = 0;
      if (!ReadU32(p, end, tick) || p + 4 > end) {
        return false;
      }
      SeqEvent e;
      e.tick_ = tick;
      e.status_ = *p++;
      e.data1_ = *p++;
      e.data2_ = *p++;
      ++p; // pad
      if (!part.Track(t).events_.Add(e)) {
        return false;
      }
    }
  }

  return p <= end;
}

} // namespace picoSeq
