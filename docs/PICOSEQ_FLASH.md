# picoSeq alt firmware — build & flash

Alternate MMT-style sequencer firmware for picoTracker Advance (`SEQ_MIDI_ONLY`).
This is third-party firmware; xiphonics does not support it. Keep a stock
Advance `.bin` so you can restore the official firmware anytime.

## Build (host tests)

```bash
cd ~/dev/picoSeq/tests
cmake -S . -B build-host
cmake --build build-host -j8
./build-host/picoTracker_tests --source-file='*seq_phase0*'
```

## Build (device, picoSeq)

Requires `arm-none-eabi-gcc`, cmake, ninja (see `docs/DEV.md`).
Presets live under `sources/`:

```bash
cd ~/dev/picoSeq/sources
cmake --preset picoseq-debug
cmake --build --preset picoseq-debug
# artifacts:
#   ../build-picoseq/Adapters/adv/picoTrackerAdvance.bin
#   ../build-picoseq/Adapters/adv/picoTrackerAdvance.elf
```

The `.bin` is the same Advance bootloader image format as stock (CRC/length
patched by `tools/crc.py` into `.firmware_info`). You may rename it to
`picoSeq.bin` on the SD card; the bootloader lists top-level `*.bin` files
(max 10).

## Flash via SD bootloader (recommended)

Flashing always happens in the **Advance bootloader UI**, which reads `.bin`
files from the SD card top level. Ways to **enter** that UI:

### A. From picoSeq (in-app)

Same code path as stock Device → Update firmware:
`System::SystemBootloader()` → `platform_bootloader()` (jump to bootloader
at `0x81e0000`).

1. Copy `picoTrackerAdvance.bin` / `picoSeq.bin` (or a stock `.bin`) to the SD
   card **top level**.
2. Insert the SD; boot picoSeq.
3. Stop transport if running.
4. Press **NAV + ALT + ENTER** → confirm **Yes** on “Reboot to bootloader?”.
5. In the bootloader UI: select the `.bin` → **ENTER** → **Reboot**.

### B. From stock Advance (Device → Update firmware)

1. Copy the `.bin` to the SD top level.
2. Device screen → **Update firmware** → confirm (stop playback first).
3. Bootloader UI → select `.bin` → ENTER → Reboot.

### C. Boot-button (no working app required)

Use if the app will not start (including after a bad flash):

1. Copy the `.bin` to the SD card top level.
2. Hold the **boot button** (small hole **below the SD card slot**) while
   holding **power** for ~**8 seconds**.
3. Bootloader UI → select `.bin` → ENTER → Reboot.

Source: Advance user manual Device page (`usermanual/.../device.md` /
https://manual.xiphonics.com/advance/device.html) and `docs/DEV.md`.

**Yes — picoSeq `.bin` is the same bootloader image format** (CRC-patched
`picoTrackerAdvance.bin`); renaming to `picoSeq.bin` is fine.

## Flash alt firmware (ST-LINK)

For development iteration (bypasses the SD bootloader):

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x_dual_bank.cfg \
  -c "set DUAL_BANK 1" -c "adapter speed 5000"
# then gdb load of picoTrackerAdvance.elf (see docs/DEV.md)
```

## Restore stock Advance firmware

1. Obtain the official Advance firmware `.bin` from xiphonics.
2. Copy it to the SD card top level (max 10 `.bin` files).
3. Enter the bootloader via **NAV+ALT+ENTER** (picoSeq), **Device → Update
   firmware** (stock), *or* the **boot-button** method → select the stock
   `.bin` → ENTER → Reboot.
4. Or ST-LINK/OpenOCD-flash the stock ELF/BIN.

## Phase 0 controls (SEQ_MIDI_ONLY)

On-device spike view (boots instead of Song):

| Input | Action |
|---|---|
| PLAY | Play / Stop |
| EDIT + PLAY | Record (replace armed track) |
| LEFT / RIGHT | Arm track 0–15 |
| ENTER | Save part to `/picoseq/part00.psq` |
| ALT + ENTER | Load part |
| NAV + ALT + ENTER | Confirm → reboot into SD firmware bootloader |

Serial REPL (UART1 115200, debug builds): `seq status|record|play|stop|save|load`

## Queue / polyphony notes (Phase 0)

- Model: **16 tracks**, `kMaxEventsPerTrack = 512` (~64KB event RAM in `.RAM_EXT`).
- `MIDI_MAX_MESG_QUEUE` under `SEQ_MIDI_ONLY`: `16*8+2+16 = 146` msgs/slice (was `8*4+2+3 = 37`).
- Soft thru uses `SendImmediate` (not the timed queue).
- Raise event capacity in Phase 1 after denser takes.
