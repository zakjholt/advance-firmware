# SPDX-License-Identifier: GPL-3.0-only
#
# Copyright (c) 2026 xiphonics, inc.

add_library(application_effects
  DattorroReverb.cpp
  DattorroReverbOutputEffect.cpp
  DelayOutputEffect.cpp
  OutputEffect.cpp
  OutputEffectSlot.cpp
  PerformanceFxSlot.cpp
  PerformanceFxProcessors.cpp
)

set_source_files_properties(
  DattorroReverb.cpp
  DattorroReverbOutputEffect.cpp
  DelayOutputEffect.cpp
  PROPERTIES
  COMPILE_OPTIONS "$<$<CONFIG:Release>:-O3>"
)

target_link_libraries(application_effects PUBLIC
  foundation_services
  foundation_variables
  profiler
  system_console
  tinyxml2
  etl
)

target_include_directories(application_effects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
