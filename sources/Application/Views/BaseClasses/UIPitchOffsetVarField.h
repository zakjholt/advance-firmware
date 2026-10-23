/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _UI_PITCH_OFFSET_VAR_FIELD_H_
#define _UI_PITCH_OFFSET_VAR_FIELD_H_

#include "Application/Instruments/PitchOffset.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/to_string.h"
#include "UIIntVarField.h"
#include <cstddef>
#include <cstdint>

namespace UIPitchOffsetVarFieldDetail {

constexpr size_t DisplayWidth = 7;
using DisplayText = etl::string<DisplayWidth>;

inline DisplayText FormatPitchOffsetCents(int32_t cents) {
  const int32_t clamped = PitchOffset::Clamp(cents);
  const int32_t magnitude = clamped < 0 ? -clamped : clamped;
  const int32_t semitones = magnitude / PitchOffset::CentsPerSemitone;
  const int32_t remainder = magnitude % PitchOffset::CentsPerSemitone;

  DisplayText result;
  if (clamped < 0) {
    result += '-';
  }
  if (semitones < 10) {
    result += '0';
  }
  etl::to_string(semitones, result, true);
  result += '.';
  if (remainder < 10) {
    result += '0';
  }
  etl::to_string(remainder, result, true);
  return result;
}

} // namespace UIPitchOffsetVarFieldDetail

class UIPitchOffsetVarField : public UIIntVarField {
public:
  UIPitchOffsetVarField(const GUIPoint &position, Variable &variable);
  ~UIPitchOffsetVarField() override = default;

  void Draw(GUIWindow &window, int offset = 0) override;
};

#endif
