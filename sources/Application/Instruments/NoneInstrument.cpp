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

#include "NoneInstrument.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Externals/etl/include/etl/string.h"

NoneInstrument::NoneInstrument() : I_Instrument(&variables_) {}

NoneInstrument::~NoneInstrument(){};

bool NoneInstrument::Init() { return true; };

void NoneInstrument::OnStart(){};

bool NoneInstrument::Start(int c, unsigned char note, bool retrigger) {
  return true;
};

void NoneInstrument::Stop(int c){};

bool NoneInstrument::Render(int channel, AudioSample *buffer, int size,
                            bool updateTick) {
  (void)channel;
  (void)buffer;
  (void)size;
  (void)updateTick;
  return false;
};

bool NoneInstrument::IsInitialized() {
  return true; // Always initialised
};

void NoneInstrument::ProcessCommand(int channel, TrackerCommand cc,
                                    ushort value){};

int NoneInstrument::GetTable() { return 0; };

bool NoneInstrument::GetTableAutomation() { return false; };

void NoneInstrument::GetTableState(TableSaveState &state){};

void NoneInstrument::SetTableState(TableSaveState &state){};
