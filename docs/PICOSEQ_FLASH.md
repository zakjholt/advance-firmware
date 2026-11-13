# picoSeq alt firmware — build & flash

Alternate MMT-style sequencer firmware for picoTracker Advance (`SEQ_MIDI_ONLY`).
This is third-party firmware; xiphonics does not support it. Keep a stock
Advance `.bin` so you can restore the official firmware anytime.

## Build (host tests)

```bash
cd ~/dev/picoSeq/tests
cmake -S . -B build-host
cmake --build build-host -j8
./build-host/picoTracker_tests --test-case=*Seq*  # or run all
```

## Build (device, picoSeq)

Requires `arm-none-eabi-gcc`, cmake, ninja (see `docs/DEV.md`).

```bash
cd ~/dev/picoSeq
cmake --preset picoseq-debug
cmake --build --preset picoseq-debug
# artifacts:
#   build-picoseq/Adapters/adv/picoTrackerAdvance.bin
#   build-picoseq/Adapters/adv/picoTrackerAdvance.elf
```

Optional: rename the `.bin` to `picoSeq.bin` on the SD card so it is obvious
which image is the alt firmware.

## Flash alt firmware (SD)

1. Copy `picoTrackerAdvance.bin` (or `picoSeq.bin`) to the **top level** of the SD card.
2. Insert SD into the Advance.
3. Device screen → **Update firmware** → select the alt image → reboot.

## Flash alt firmware (ST-LINK)

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x_dual_bank.cfg \
  -c "set DUAL_BANK 1" -c "adapter speed 5000"
# then gdb load of picoTrackerAdvance.elf (see docs/DEV.md)
```

## Restore stock Advance firmware

1. Obtain the official Advance firmware `.bin` from xiphonics.
2. Copy it to the SD card top level (max 10 `.bin` files).
3. Device → Update firmware → select the **stock** image → reboot.
4. Or ST-LINK/OpenOCD-flash the stock ELF/BIN the same way as above.

## Phase 0 controls (SEQ_MIDI_ONLY)

On-device spike view (boots instead of Song):

| Input | Action |
|---|---|
| PLAY | Play / Stop |
| EDIT + PLAY | Record (replace armed track) |
| LEFT / RIGHT | Arm track 0–15 |
| ENTER | Save part to `/picoseq/part00.psq` |
| ALT + ENTER | Load part |

Serial REPL (UART1 115200, debug builds): `seq status|record|play|stop|save|load`

## Queue / polyphony notes (Phase 0)

- Model: **16 tracks**, `kMaxEventsPerTrack = 512` (~64KB event RAM).
- `MIDI_MAX_MESG_QUEUE` under `SEQ_MIDI_ONLY`: `16*8+2+16 = 146` msgs/slice (was `8*4+2+3 = 37`).
- Soft thru uses `SendImmediate` (not the timed queue).
- Raise event capacity / move to SDRAM in Phase 1 after denser takes.
