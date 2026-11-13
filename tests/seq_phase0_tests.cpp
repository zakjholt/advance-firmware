/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 Zak Holt / picoSeq
 */

#include "doctest/doctest.h"

#include "Application/Seq/MidiEventList.h"
#include "Application/Seq/MidiRouter.h"
#include "Application/Seq/SeqEngine.h"
#include "Application/Seq/SeqPartSerializer.h"
#include "Application/Seq/SeqTickClock.h"
#include <vector>

using namespace picoSeq;

namespace {

class CapturingSink : public IMidiSink {
public:
  void SendImmediate(const MidiBytes &msg) override { sent_.push_back(msg); }

  std::vector<MidiBytes> sent_;
};

} // namespace

TEST_CASE("MidiEventList keeps tick order and lower_bound") {
  MidiEventList list;
  SeqEvent a{10, 0x90, 60, 100};
  SeqEvent b{20, 0x80, 60, 0};
  SeqEvent c{15, 0x90, 64, 80};

  CHECK(list.Add(a));
  CHECK(list.Add(b));
  CHECK(list.Add(c));
  REQUIRE(list.Size() == 3);
  CHECK(list.At(0).tick_ == 10);
  CHECK(list.At(1).tick_ == 15);
  CHECK(list.At(2).tick_ == 20);
  CHECK(list.LowerBound(15) == 1);
  CHECK(list.LowerBound(21) == 3);
}

TEST_CASE("SeqPart exposes 16 tracks") {
  SeqPart part;
  CHECK(kTrackCount == 16);
  part.ArmOnly(15);
  CHECK(part.Track(15).armed_);
  CHECK_FALSE(part.Track(0).armed_);
  part.Track(3).events_.Add(SeqEvent{0, 0x90, 48, 100});
  CHECK(part.TotalEventCount() == 1);
}

TEST_CASE("SeqTickClock advances from micros at tempo") {
  SeqTickClock clock;
  clock.SetTempo(120);
  // At 120 BPM and 96 PPQN: micros/tick = 60e6/(120*96) = 5208
  const uint32_t advanced = clock.AdvanceMicros(5208 * 4);
  CHECK(advanced == 4);
  CHECK(clock.Tick() == 4);
}

TEST_CASE("Soft thru forwards incoming notes") {
  CapturingSink sink;
  SeqEngine engine;
  engine.SetSink(&sink);
  engine.Router().SetThruEnabled(true);
  engine.Stop();

  CHECK(engine.HandleIncoming(0x91, 60, 100));
  REQUIRE(sink.sent_.size() == 1);
  CHECK(sink.sent_[0].status_ == 0x91);
  CHECK(sink.sent_[0].data1_ == 60);
  CHECK(sink.sent_[0].data2_ == 100);
  CHECK(engine.EventsCaptured() == 0);
}

TEST_CASE("Record one track while clock runs then playback") {
  CapturingSink sink;
  SeqEngine engine;
  engine.SetSink(&sink);
  engine.Part().SetLengthTicks(96); // 1 beat
  engine.Part().SetLoop(false);
  engine.ArmTrack(0);
  engine.Record();

  // Capture a note at tick 0, advance past it, then stop and replay.
  CHECK(engine.HandleIncoming(0x90, 60, 100));
  engine.AdvanceTicks(1);
  CHECK(engine.HandleIncoming(0x80, 60, 0));
  engine.AdvanceTicks(95);
  CHECK(engine.Transport() == SeqTransport::Stopped);
  CHECK(engine.Part().Track(0).events_.Size() == 2);
  CHECK(engine.EventsCaptured() == 2);

  sink.sent_.clear();
  engine.Play();
  engine.AdvanceTicks(96);
  REQUIRE(sink.sent_.size() >= 2);
  CHECK(sink.sent_[0].status_ == 0x90);
  CHECK(sink.sent_[0].data1_ == 60);
  CHECK(sink.sent_[1].status_ == 0x80);
}

TEST_CASE("Recording does not double-send thru notes via playback") {
  CapturingSink sink;
  SeqEngine engine;
  engine.SetSink(&sink);
  engine.Part().SetLengthTicks(32);
  engine.Part().SetLoop(false);
  engine.ArmTrack(2);
  engine.Record();

  engine.HandleIncoming(0x92, 61, 90);
  const auto thruCount = sink.sent_.size();
  CHECK(thruCount == 1);
  engine.AdvanceTicks(8);
  // Armed track playback is skipped while recording; only thru should have sent.
  CHECK(sink.sent_.size() == thruCount);
}

TEST_CASE("Part serializer round-trips 16-track part") {
  SeqPart part;
  part.SetLengthTicks(192);
  part.SetLoop(true);
  part.Track(0).events_.Add(SeqEvent{0, 0x90, 60, 100});
  part.Track(0).events_.Add(SeqEvent{48, 0x80, 60, 0});
  part.Track(15).events_.Add(SeqEvent{12, 0x9F, 72, 64});

  std::vector<uint8_t> buffer(SeqPartSerializer::EncodedSize(part));
  const std::size_t written =
      SeqPartSerializer::SaveToBuffer(part, buffer.data(), buffer.size());
  REQUIRE(written == buffer.size());

  SeqPart loaded;
  REQUIRE(SeqPartSerializer::LoadFromBuffer(loaded, buffer.data(), written));
  CHECK(loaded.LengthTicks() == 192);
  CHECK(loaded.Loop());
  CHECK(loaded.Track(0).events_.Size() == 2);
  CHECK(loaded.Track(15).events_.Size() == 1);
  CHECK(loaded.Track(15).events_.At(0).status_ == 0x9F);
  CHECK(loaded.Track(15).events_.At(0).data1_ == 72);
}
