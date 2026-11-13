# SPDX-License-Identifier: GPL-3.0-only
#
# Copyright (c) 2026 Zak Holt / picoSeq

add_library(application_seq
  MidiEventList.cpp
  SeqPart.cpp
  SeqTickClock.cpp
  MidiRouter.cpp
  SeqEngine.cpp
  SeqPartSerializer.cpp
  SeqMidiBridge.cpp
  SeqPartStore.cpp
)

target_link_libraries(application_seq PUBLIC
  etl
  services_midi
  foundation
  system_filesystem
)

target_include_directories(application_seq PUBLIC ${PROJECT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})
