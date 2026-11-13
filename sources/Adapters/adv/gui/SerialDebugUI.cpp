/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "SerialDebugUI.h"
#include "../system/advSystem.h"
#include "Adapters/adv/audio/record.h"
#include "Application/Model/Config.h"
#ifdef SEQ_MIDI_ONLY
#include "Application/Seq/SeqMidiBridge.h"
#include "Application/Seq/SeqPartStore.h"
#endif
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/I_File.h"
#include "System/FileSystem/PathUtils.h"
#include "System/System/System.h"
#include "main.h"
#include <etl/string.h>
#include <etl/string_view.h>

extern UART_HandleTypeDef huart1; // Declare the UART handle
#include <cstdint>
#include <nanoprintf.h>
#include <new>
#include <stdio.h>
#include <string.h>

// ASCII control characters
#define BACKSPACE 0x08
#define DELETE 0x7F
#define CR 13
#define LF 10
#define ENDSTDIN 255
#define READ_BUFFER_SIZE 32

namespace {

using SerialFileEntries = etl::vector<FileSystemEntry, 32>;

alignas(SerialFileEntries) __attribute__((section(".SDRAM1_NC")))
__attribute__((aligned(32))) static unsigned char serialFileEntriesBuffer
    [sizeof(SerialFileEntries)];

SerialFileEntries &GetSerialFileEntries() {
  static SerialFileEntries *entries = nullptr;
  if (entries == nullptr) {
    entries = new (serialFileEntriesBuffer) SerialFileEntries();
  }
  return *entries;
}

} // namespace

SerialDebugUI::SerialDebugUI(){};

// ref: https://forums.raspberrypi.com/viewtopic.php?t=303964
// will return true if user pressed return and the buffer now contains the
// string they entered up to pressing return
bool SerialDebugUI::readSerialIn(char *buffer, short size) {
  uint8_t chr;
  // Check if a character is available in the UART receive buffer
  if (HAL_UART_Receive(&huart1, &chr, 1, 0) == HAL_OK) {
    // Handle backspace or delete key
    if (chr == BACKSPACE || chr == DELETE) {
      if (lp_ > 0) {
        // Move cursor back, print space, move cursor back again
        char backspace[] = "\b \b";
        HAL_UART_Transmit(&huart1, (uint8_t *)backspace, 3, HAL_MAX_DELAY);
        lp_--; // Decrement buffer pointer
      }
    } else {
      // Echo the character back
      HAL_UART_Transmit(&huart1, &chr, 1, HAL_MAX_DELAY);

      // Only add to buffer if we have space
      if (lp_ < size - 1) {
        buffer[lp_++] = chr;
      }

      // Check for carriage return (Enter key)
      if (chr == CR || chr == '\n' || lp_ == size - 1) {
        buffer[lp_] = 0; // terminate the string
        // strip off trailing newline/carriage return chars
        size_t len = strlen(buffer);
        while (len > 0 &&
               (buffer[len - 1] == '\r' || buffer[len - 1] == '\n')) {
          buffer[--len] = '\0';
        }
        dispatchCmd(buffer);
        lp_ = 0; // reset string buffer pointer
        return true;
      }
    }
  }
  return false;
}

void SerialDebugUI::dispatchCmd(char *input) {
  // Create an ETL string view of the input
  etl::string_view inputView(input);

  // Find the first space to separate command from arguments
  size_t spacePos = inputView.find(' ');

  // Used for extracting the command & arg
  etl::string_view cmdView;
  etl::string_view argView;

  if (spacePos == etl::string_view::npos) {
    // No space found, the entire input is the command
    cmdView = inputView;
  } else {
    // Split into command and argument
    cmdView = inputView.substr(0, spacePos);

    // Skip any leading spaces in the argument
    size_t argStart = spacePos + 1;
    while (argStart < inputView.size() && inputView[argStart] == ' ') {
      argStart++;
    }

    if (argStart < inputView.size()) {
      argView = inputView.substr(argStart);
    }
  }

  // Convert to C strings for compatibility with existing code
  char cmdBuffer[READ_BUFFER_SIZE] = {0};
  char argBuffer[READ_BUFFER_SIZE] = {0};

  if (!cmdView.empty()) {
    strncpy(cmdBuffer, cmdView.data(), cmdView.size());
    cmdBuffer[cmdView.size()] = '\0';
  }

  if (!argView.empty()) {
    strncpy(argBuffer, argView.data(), argView.size());
    argBuffer[argView.size()] = '\0';
  }

  // Use these buffers for command processing
  const char *cmd = cmdBuffer;
  const char *arg = argBuffer[0] != '\0' ? argBuffer : nullptr;

  if (strcmp(cmd, "cat") == 0) {
    catFile(arg);
  } else if (strcmp(cmd, "ls") == 0) {
    listFiles(arg);
  } else if (strcmp(cmd, "rm") == 0) {
    rmFile(arg);
  } else if (strcmp(cmd, "save") == 0) {
    saveConfig();
  } else if (strcmp(cmd, "mkdir") == 0) {
    mkdir(arg);
  } else if (strcmp(cmd, "rmdir") == 0) {
    rmdir(arg);
  } else if (strcmp(cmd, "shutdown") == 0) {
    shutdown();
  } else if (strcmp(cmd, "battery") == 0) {
    readBattery();
  } else if (strcmp(cmd, "startrec") == 0) {
    // char *filename = strtok(NULL, " ");
    // uint32_t milliseconds = atoi(strtok(NULL, " "));
    // uint32_t threshold = atoi(strtok(NULL, " "));
    StartRecording("/recordings/testrecord.wav", 1, 0);
  } else if (strcmp(cmd, "stoprec") == 0) {
    StopRecording();
  }
#ifdef SEQ_MIDI_ONLY
  else if (strcmp(cmd, "seq") == 0) {
    auto &engine = picoSeq::SeqMidiBridge::Get().Engine();
    if (arg == nullptr || strcmp(arg, "status") == 0) {
      Trace::Log("SEQ", "state=%d tick=%lu events=%u armed=%d",
                 static_cast<int>(engine.Transport()),
                 static_cast<unsigned long>(engine.PlayTick()),
                 static_cast<unsigned>(engine.Part().TotalEventCount()),
                 engine.ArmedTrack());
    } else if (strcmp(arg, "record") == 0) {
      engine.Record();
      Trace::Log("SEQ", "recording");
    } else if (strcmp(arg, "play") == 0) {
      engine.Play();
      Trace::Log("SEQ", "playing");
    } else if (strcmp(arg, "stop") == 0) {
      engine.Stop();
      Trace::Log("SEQ", "stopped");
    } else if (strcmp(arg, "save") == 0) {
      Trace::Log("SEQ", picoSeq::SeqPartStore::SavePart(engine.Part())
                            ? "saved"
                            : "save failed");
    } else if (strcmp(arg, "load") == 0) {
      Trace::Log("SEQ", picoSeq::SeqPartStore::LoadPart(engine.Part())
                            ? "loaded"
                            : "load failed");
    } else {
      Trace::Log("SEQ", "usage: seq [status|record|play|stop|save|load]");
    }
  }
#endif
  else if (strcmp(cmd, "help") == 0) {
    Trace::Log("SERIALDEBUG",
               "cat, ls, rm, mkdir, rmdir, save, battery, help"
#ifdef SEQ_MIDI_ONLY
               ", seq"
#endif
    );
  } else {
    Trace::Log("SERIALDEBUG", "unknown command");
  }
}

void SerialDebugUI::catFile(const char *path) {
  auto fs = FileSystem::GetInstance();
  if (!fs->exists(path)) {
    Trace::Log("SERIALDEBUG", "File not found: %s", path);
    return;
  }

  auto file = fs->Open(path, "r");
  if (!file) {
    Trace::Log("SERIALDEBUG", "Failed to open file: %s", path);
    return;
  }

  uint8_t buffer[1];
  int bytesRead;

  while ((bytesRead = file->Read(buffer, 1)) > 0) {
    // If we see a newline, make sure it's preceded by a carriage return
    if (buffer[0] == '\n') {
      uint8_t cr = '\r';
      HAL_UART_Transmit(&huart1, &cr, 1, HAL_MAX_DELAY);
    }
    // Send the character as-is
    HAL_UART_Transmit(&huart1, buffer, 1, HAL_MAX_DELAY);

    // If we just sent a carriage return, make sure it's followed by a newline
    if (buffer[0] == '\r') {
      uint8_t lf = '\n';
      HAL_UART_Transmit(&huart1, &lf, 1, HAL_MAX_DELAY);
    }
  }

  // Add a final newline if the file doesn't end with one
  uint8_t newline[] = {'\r', '\n'};
  HAL_UART_Transmit(&huart1, newline, 2, HAL_MAX_DELAY);
}

void SerialDebugUI::listFiles(const char *path) {
  auto fs = FileSystem::GetInstance();
  auto &serialFileEntries = GetSerialFileEntries();
  const char *requestedPath = (path != nullptr && path[0] != '\0') ? path : "/";
  FileSystemPath::PathString directoryPath;
  if (!FileSystemPath::NormalizePath(requestedPath, directoryPath) ||
      !FileSystemPath::IsAbsolutePath(directoryPath.c_str())) {
    Trace::Error("failed to resolve ls path:%s", requestedPath);
    return;
  }

  serialFileEntries.clear();
  if (!fs->ListDirectory(directoryPath.c_str(), &serialFileEntries, "", false,
                         true)) {
    Trace::Error("failed to ls files path:%s", directoryPath.c_str());
    return;
  }

  for (size_t i = 0; i < serialFileEntries.size(); i++) {
    const char *entryType =
        (serialFileEntries[i].type == PFT_DIR) ? "[dir]" : "[file]";
    Trace::Log("SERIALDEBUG", "%s %s", entryType,
               serialFileEntries[i].name.c_str());
  }
}

void SerialDebugUI::rmFile(const char *path) {
  auto fs = FileSystem::GetInstance();
  if (!fs->DeleteFile(path)) {
    Trace::Error("failed to delete file:%s", path);
  } else {
    Trace::Log("SERIALDEBUGUI", "deleted file:%s", path);
    char buf[128];
    int len = npf_snprintf(buf, sizeof(buf), "deleted:%s\r\n", path);
    if (len > 0) {
      HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, HAL_MAX_DELAY);
    }
  }
}

void SerialDebugUI::saveConfig() {
  Config *config = Config::GetInstance();
  config->Save();
}

void SerialDebugUI::mkdir(const char *path) {
  // TODO: Implement directory creation for STM32
  // This is a placeholder - replace with actual STM32 filesystem calls
  Trace::Debug("mkdir %s (not implemented yet)", path);
}

void SerialDebugUI::rmdir(const char *path) {
  // TODO: Implement directory removal for STM32
  // This is a placeholder - replace with actual STM32 filesystem calls
  Trace::Debug("rmdir %s (not implemented yet)", path);
}

void SerialDebugUI::shutdown() {
  Trace::Log("SERIALDEBUGUI", "shutting down");
  System::GetInstance()->PowerDown();
}

void SerialDebugUI::readBattery() {
  // Get battery level (SOC percentage) from the system
  BatteryState batteryState;
  System::GetInstance()->GetBatteryState(batteryState);

  char buf[48];
  int len = npf_snprintf(
      buf, sizeof(buf), "Battery SOC:%d%% V:%dmV T:%dC CHR:%s\r\n",
      batteryState.percentage, batteryState.voltage_mv,
      batteryState.temperature_c, batteryState.charging ? "Y" : "N");

  if (len > 0) {
    // reading the source of HAL_UART_Transmit() suggests that the timeout is in
    // ms but who knows given they didnt bother to actually document it
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, 1 * 1000);
  }
}
