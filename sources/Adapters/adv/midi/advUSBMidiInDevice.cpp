/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "advUSBMidiInDevice.h"
#include "Adapters/adv/platform/platform.h"
#include "Adapters/adv/usb/usb_utils.h"
#include "System/Console/Trace.h"
#include "tusb.h"

namespace {
advUSBMidiInDevice *g_hostRxTarget = nullptr;
} // namespace

extern "C" void advUSBMidiHostFeedRxBytes(const uint8_t *bytes,
                                          uint16_t count) {
  if ((g_hostRxTarget != nullptr) && (bytes != nullptr) && (count != 0u)) {
    for (uint16_t i = 0; i < count; ++i) {
      g_hostRxTarget->processMidiData(bytes[i], micros());
    }
  }
}

advUSBMidiInDevice::advUSBMidiInDevice(const char *name) : MidiInDevice(name) {
  g_hostRxTarget = this;
  Trace::Log("MIDI", "Created USB MIDI input device %s", name);
}

advUSBMidiInDevice::~advUSBMidiInDevice() {
  if (g_hostRxTarget == this) {
    g_hostRxTarget = nullptr;
  }
  Close();
}

bool advUSBMidiInDevice::Init() {
  Trace::Log("MIDI", "Initialized USB MIDI input driver");
  return true;
}

void advUSBMidiInDevice::Close() {
  Trace::Log("MIDI", "Closed USB MIDI input driver");
}

bool advUSBMidiInDevice::Start() {
  Trace::Log("MIDI", "Started USB MIDI input driver");
  return MidiInDevice::Start();
}

void advUSBMidiInDevice::Stop() {
  Trace::Log("MIDI", "Stopped USB MIDI input driver");
  MidiInDevice::Stop();
}

void advUSBMidiInDevice::poll() {
  if (tuh_inited()) {
    return;
  }

  if (!tud_midi_mounted()) {
    return;
  }

  uint8_t packet[4];
  while (tud_midi_available()) {
    const uint32_t bytesRead = tud_midi_packet_read(packet);
    if (bytesRead == 0u) {
      Trace::Error("USBMIDI: failed to read expected MIDI packet");
      break;
    }

    const uint8_t cin = packet[0] & 0x0Fu;
    const uint8_t payloadLength = usbMidiPayloadLength(cin);
    if (payloadLength == 0u) {
      continue;
    }

    for (uint8_t i = 0; i < payloadLength; ++i) {
      processMidiData(packet[1u + i], micros());
    }
  }
}
