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

#ifndef _NONE_INSTRUMENT_H_
#define _NONE_INSTRUMENT_H_

#include "Application/Model/Song.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "I_Instrument.h"

class NoneInstrument : public I_Instrument {

public:
  NoneInstrument();
  virtual ~NoneInstrument();

  virtual bool Init();

  // Start & stop the instument
  virtual bool Start(int channel, unsigned char note, bool retrigger = true);
  virtual void Stop(int channel);

  // size refers to the number of samples
  // should always fill interleaved stereo / 16bit
  virtual bool Render(int channel, AudioSample *buffer, int size,
                      bool updateTick) override;
  virtual void ProcessCommand(int channel, TrackerCommand cc, ushort value);

  virtual bool IsInitialized();

  virtual bool IsEmpty() { return true; };

  virtual InstrumentType GetType() { return IT_NONE; };

  virtual void OnStart();

  virtual void Purge(){};

  virtual int GetTable();
  virtual bool GetTableAutomation();
  virtual void GetTableState(TableSaveState &state);
  virtual void SetTableState(TableSaveState &state);
  etl::ivector<Variable *> *Variables() { return &variables_; };

private:
  etl::vector<Variable *, 2> variables_;
};
#endif
