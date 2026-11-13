/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#include "Application/Seq/SeqPartStore.h"
#include "Application/Seq/SeqConstants.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/I_File.h"
#include <cstring>

namespace picoSeq {

namespace {

bool WriteBytes(I_File *file, const void *data, int len) {
  return file->Write(data, 1, len) == len;
}

bool ReadBytes(I_File *file, void *data, int len) {
  return file->Read(data, len) == len;
}

void EncodeU16(uint8_t out[2], uint16_t v) {
  out[0] = static_cast<uint8_t>(v & 0xFF);
  out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void EncodeU32(uint8_t out[4], uint32_t v) {
  out[0] = static_cast<uint8_t>(v & 0xFF);
  out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

uint16_t DecodeU16(const uint8_t in[2]) {
  return static_cast<uint16_t>(in[0] | (in[1] << 8));
}

uint32_t DecodeU32(const uint8_t in[4]) {
  return static_cast<uint32_t>(in[0] | (in[1] << 8) | (in[2] << 16) |
                               (in[3] << 24));
}

} // namespace

bool SeqPartStore::EnsureDir() {
  FileSystem *fs = FileSystem::GetInstance();
  if (fs == nullptr) {
    return false;
  }
  if (!fs->exists("/picoseq")) {
    if (!fs->makeDir("/picoseq", true)) {
      Trace::Error("SEQ", "Failed to create /picoseq");
      return false;
    }
  }
  return true;
}

bool SeqPartStore::SavePart(const SeqPart &part, const char *path) {
  if (!EnsureDir()) {
    return false;
  }
  FileSystem *fs = FileSystem::GetInstance();
  if (fs == nullptr || path == nullptr) {
    return false;
  }

  auto file = fs->Open(path, "w");
  if (!file) {
    Trace::Error("SEQ", "Failed to open %s for write", path);
    return false;
  }

  uint8_t tmp[8];
  if (!WriteBytes(file.get(), kPartMagic, 4)) {
    return false;
  }
  EncodeU16(tmp, kPartFormatVersion);
  if (!WriteBytes(file.get(), tmp, 2)) {
    return false;
  }
  EncodeU32(tmp, part.LengthTicks());
  if (!WriteBytes(file.get(), tmp, 4)) {
    return false;
  }
  tmp[0] = part.Loop() ? 1 : 0;
  tmp[1] = static_cast<uint8_t>(kTrackCount);
  if (!WriteBytes(file.get(), tmp, 2)) {
    return false;
  }

  for (int t = 0; t < kTrackCount; ++t) {
    const MidiEventList &list = part.Track(t).events_;
    EncodeU32(tmp, static_cast<uint32_t>(list.Size()));
    if (!WriteBytes(file.get(), tmp, 4)) {
      return false;
    }
    for (std::size_t i = 0; i < list.Size(); ++i) {
      const SeqEvent &e = list.At(i);
      EncodeU32(tmp, e.tick_);
      tmp[4] = e.status_;
      tmp[5] = e.data1_;
      tmp[6] = e.data2_;
      tmp[7] = 0;
      if (!WriteBytes(file.get(), tmp, 8)) {
        return false;
      }
    }
  }

  Trace::Log("SEQ", "Saved part to %s", path);
  return true;
}

bool SeqPartStore::LoadPart(SeqPart &part, const char *path) {
  FileSystem *fs = FileSystem::GetInstance();
  if (fs == nullptr || path == nullptr || !fs->exists(path)) {
    return false;
  }
  auto file = fs->Open(path, "r");
  if (!file) {
    return false;
  }

  uint8_t tmp[8];
  if (!ReadBytes(file.get(), tmp, 4) || std::memcmp(tmp, kPartMagic, 4) != 0) {
    return false;
  }
  if (!ReadBytes(file.get(), tmp, 2) || DecodeU16(tmp) != kPartFormatVersion) {
    return false;
  }
  if (!ReadBytes(file.get(), tmp, 4)) {
    return false;
  }
  const uint32_t lengthTicks = DecodeU32(tmp);
  if (!ReadBytes(file.get(), tmp, 2)) {
    return false;
  }
  const bool loop = tmp[0] != 0;
  if (tmp[1] != kTrackCount) {
    return false;
  }

  part.Reset();
  part.SetLengthTicks(lengthTicks);
  part.SetLoop(loop);

  for (int t = 0; t < kTrackCount; ++t) {
    if (!ReadBytes(file.get(), tmp, 4)) {
      return false;
    }
    const uint32_t count = DecodeU32(tmp);
    if (count > kMaxEventsPerTrack) {
      return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
      if (!ReadBytes(file.get(), tmp, 8)) {
        return false;
      }
      SeqEvent e;
      e.tick_ = DecodeU32(tmp);
      e.status_ = tmp[4];
      e.data1_ = tmp[5];
      e.data2_ = tmp[6];
      if (!part.Track(t).events_.Add(e)) {
        return false;
      }
    }
  }

  Trace::Log("SEQ", "Loaded part from %s", path);
  return true;
}

} // namespace picoSeq
