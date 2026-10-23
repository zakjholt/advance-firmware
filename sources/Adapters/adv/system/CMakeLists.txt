# SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
#
# Copyright (c) 2026 xiphonics, inc.
#
# Inherited BSD-licensed material remains BSD-3-Clause.
# New original contributions by xiphonics, inc. are GPL-3.0-only.
# Previously granted BSD permissions are preserved. See LICENSING.md.

add_library(
  platform_system
  advSystem.cpp
  advUsbRoleManager.cpp
  input.cpp
  critical_error_message.c
  assets/error_bitmaps.c
  advSamplePool.cpp
  BatteryGauge.cpp
  syscalls.c
  charger.cpp)

target_link_libraries(
  platform_system
  PUBLIC
    uiframework_simplebaseclasses
    platform_timer
    platform_audio
    services_audio
    platform_midi
    platform_gui
    platform_display
    platform_filesystem
    etl
  INTERFACE
    system_system)

target_include_directories(platform_system PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
