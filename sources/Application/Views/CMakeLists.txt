# SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
#
# Copyright (c) 2026 xiphonics, inc.
#
# Inherited BSD-licensed material remains BSD-3-Clause.
# New original contributions by xiphonics, inc. are GPL-3.0-only.
# Previously granted BSD permissions are preserved. See LICENSING.md.

add_library(application_views
  FieldView.cpp
  GraphField.cpp
  VoiceEnvelopeGraph.cpp
  VoiceFilterGraph.cpp
  ChainView.cpp
  ConsoleView.cpp
  GrooveView.cpp
  ImportView.cpp
  InstrumentImportView.cpp
  FM6PresetBrowserView.cpp
  InstrumentView.cpp
  VoiceView.cpp
  ThemeImportView.cpp
  DeviceView.cpp
  NullView.cpp
  PhraseView.cpp
  ProjectView.cpp
  RecordView.cpp
  SelectionResampleSupport.cpp
  SelectProjectView.cpp
  SongView.cpp
  TableView.cpp
  ThemeView.cpp
  UIController.cpp
  ViewData.cpp
  ScreenView.cpp
  MixerView.cpp
  MasterEffectsView.cpp
  PerformanceEffectsView.cpp
  SampleIndexField.cpp
  SampleEditorView.cpp
  SampleEditProgressDisplay.cpp
  SampleSlicer.cpp
  SampleSlicesView.cpp
  CreditsView.cpp
)

target_link_libraries(application_views PUBLIC
  application_views_baseclasses
  application_effects
  application_instruments
  etl
  application_utils
  platform_display
)

target_include_directories(application_views PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR})

add_subdirectory(BaseClasses)
add_subdirectory(ModalDialogs)
