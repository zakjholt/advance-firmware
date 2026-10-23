# Development
If you'd like to make changes and develop for the picoTracker Advance, here are guidelines about how to do so.

The picoTracker Advance is based on the **STMicroelectronics STM32H7** (STM32H743/H750, Arm Cortex-M7) microcontroller. The firmware is built around the [STM32 HAL](https://www.st.com/en/embedded-software/stm32cubeh.html) drivers (generated with [STM32CubeMX](https://www.st.com/en/development-tools/stm32cubeMX.html); see `sources/Adapters/adv/adv.ioc`) plus [FreeRTOS](https://www.freertos.org/), TinyUSB and FatFs.

The main requirements are:

* Basic build tools (build-essentials in Debian based distros)
* `cmake` (>= 3.13) and `ninja`
* The Arm GNU toolchain: `gcc-arm-none-eabi` (`arm-none-eabi-gcc` must be on your `PATH`)

### Printf, sprintf and friends

The printf family of functions are not to be used as they have significant issues on embedded systems with limited ram, especially in the form of a large buffer that is allocated on the stack. Instead please use the [nanoprintf](https://github.com/martinets/nanoprintf) library functions found in `nanoprintf.h`.

For debug logging, please do *not* use even nanoprintf functions directly and instead use the `Trace` class.

## Getting started

### Building with CMake presets

The recommended way to build is using the presets defined in `sources/CMakePresets.json` (they use the Ninja generator):

```bash
~ % git clone https://github.com/democloid/picoTracker
~ % cd picoTracker
picoTracker % cmake --preset adv-debug        # configures into ./build
picoTracker % cmake --build --preset adv-debug
```

There is also an `adv-release` preset which configures into `./build-opt`.

### Building manually

Assuming a unix style operating system (Linux, OSX). Below example assumes 8 thread processor on your build machine, adjust ```-j``` flag on the ```make``` command to match your platform.

```bash
~ % git clone https://github.com/democloid/picoTracker
~ % cd picoTracker
picoTracker % cmake -S sources -B build -DCMAKE_BUILD_TYPE=Debug
picoTracker % cmake --build build -j8
```

Subsequent builds just need the build command. If anything changed on the CMakeLists files, cmake will update itself.

### Build outputs

The build produces:

* `build/Adapters/adv/picoTrackerAdvance.elf` — the debuggable ELF image
* `build/Adapters/adv/picoTrackerAdvance.bin` — raw binary used for SD card firmware updates

Build configuration notes:

* `Debug` is the default build type (`-Os -g3`); `Release` builds use `-O3` and define `NDEBUG`.
* Non-Release builds automatically enable `SERIAL_REPL`, a basic control REPL over the serial port.
* Logging (`Trace`) is compiled in only when configured with `-DENABLE_LOGGING=ON`.

## Loading code onto the Advance

There are two ways to get a build onto the device:

### Firmware update over SD card

Copy `picoTrackerAdvance.bin` to the top level directory of the SD card, insert it into the Advance, then use `Update firmware` on the Device Screen. The device reboots into its bootloader UI and flashes the new firmware. This is the same flow end users follow (see the user manual); a maximum of 10 firmware `.bin` files can be kept at the SD card top level.

### Flashing with ST-LINK and OpenOCD

For development iteration, a [ST-LINK](https://www.st.com/en/development-tools/st-link-v2.html) debug probe and [OpenOCD](https://openocd.org/) let you flash and debug directly. The Advance uses the dual-bank H7 flash layout, so the OpenOCD invocation looks like:

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x_dual_bank.cfg \
  -c "set DUAL_BANK 1" -c "adapter speed 5000" -c "bindto 0.0.0.0"
```

The `bindto` command can be omitted if you're developing locally; it's useful when running OpenOCD on one machine and connecting GDB from another.

## Configuring serial output of logs

On the Advance, log output goes over **UART1** (`DEBUG_UART`, see `sources/Adapters/adv/Core/Inc/main.h`) at **115200 8N1**. This port is dedicated to debugging — it is *not* shared with MIDI (TRS MIDI uses UART7) — so there is no need to disable any peripheral to get a console.

To compile the `Trace` logging into the firmware, configure with:

```bash
cmake -S sources -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_LOGGING=ON
```

Debug builds also expose the `SERIAL_REPL` interactive control console on the same UART.

## Connecting to your serial output

Connect a 3.3V USB-serial adapter to the UART1 debug pins (TX/RX/GND) and use your serial communications program of choice:

```bash
~ % minicom -D /dev/ttyUSB0 -b 115200
```

## Profiling

The project currently has two profiling helpers under
`sources/System/Profiler`:

- `Profiler.h` for simple wall-clock scope timings using `System::Micros()`
- `DWTProfiler.h` for low-overhead aggregated profiling using
  the Cortex-M7 DWT counters

The current audio/render profiling uses `DWTProfiler.h`.

### Build configuration

Profiling is enabled in `sources/CMakeLists.txt` with:

```cmake
add_definitions(-DENABLE_PROFILING=1)
```

`Profiler.h` logs through the normal `Trace` path. In Release builds, `Trace`
logging is disabled by default, so if you want to see those `[PROFILER]` lines
on the serial console you must enable it explicitly:

```bash
cmake -S sources -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_LOGGING=ON
cmake --build build -j8
```

### Simple scope timing with `Profiler.h`

Include the profiler header:

```cpp
#include "System/Profiler/Profiler.h"
```

For a simple one-shot scope timing, use:

```cpp
PROFILE_SCOPE("MyFunction::HotPath");
```

or:

```cpp
PROFILE_FUNCTION();
```

These emit a single `[PROFILER]` line when the scope exits.

### Aggregated DWT profiling

For realtime code, use `DWTProfiler.h`. This profiler aggregates
stats across many calls and prints a periodic summary instead of logging every
buffer.

```cpp
#include "System/Profiler/DWTProfiler.h"

namespace {

DWTProfileStats gRenderProfile = {"render"};

void RegisterProfiles() {
  static bool registered = false;
  if (registered) {
    return;
  }

  RegisterDWTProfileStats(&gRenderProfile);
  registered = true;
}

} // namespace

void Render() {
  ScopedDWTProfile profile(&gRenderProfile);

  // hot code here
}
```

If you need to measure only selected sections inside a larger function, use
`AccumulatingDWTProfile` and wrap the measured segments with `Begin()` / `End()`.

The DWT profiler batches cycle counts and prints a report automatically roughly
every 5 seconds at the end of an audio render pass. You do not manually flush
it.

Every displayed percentage is absolute CPU consumption over the full reporting
interval. Voice measurements are arranged as columns so individual channels
and their calculated sums can be compared directly:

```text
[DWT] absolute CPU              Total        V1        V2        V3        V4        V5        V6        V7        V8
[DWT] Voice source              2.30%     1.20%     1.10%     0.00%     0.00%     0.00%     0.00%     0.00%     0.00%
[DWT] ...
[DWT] ifx1(0.625%)              0.38%     0.20%     0.18%     0.00%     0.00%     0.00%     0.00%     0.00%     0.00%
[DWT] Other                     0.40%     0.20%     0.20%     0.00%     0.00%     0.00%     0.00%     0.00%     0.00%
[DWT] Channel total(6.25%)      8.70%     4.50%     4.20%     0.00%     0.00%     0.00%     0.00%     0.00%     0.00%
[DWT] Voices other              0.10%
[DWT] Voices total(50%)         8.80%
```

The totals and residuals have distinct meanings:

- A component's `Total` is calculated from its eight channel columns.
- Each `Channel total` is measured around the complete channel path and also
  includes attributed player-control work.
- `Other` is the measured channel total minus its displayed components.
- `Voices total` is independently measured from all channel paths plus the
  enclosing player-control scope.
- `Voices other` is the measured voices total minus the channel totals.
- `mixer total` includes master-bus accumulation, master gain and level work,
  output-effect sends to later slots, and each output-effect return gain. The
  effect processors themselves remain attributed to their `ofx1`–`ofx4`
  profiles.
- A percentage in parentheses is the absolute slot ceiling. Values over their
  ceiling are colored red; values within it are green.
- The voice budget is 50% total and 6.25% per channel. Each IFX slot may use 5%
  across all channels, or 0.625% in one channel. The four OFX slots share a
  four-unit, 40% budget. Each slot's displayed ceiling follows the selected
  effect's reservation, with one unit representing 10% of total CPU.

If you want a summary line to print after the component lines, set
`printLast_ = true` in the `DWTProfileStats` initializer.

## Loading code and running it using gdb

Start OpenOCD as described above, then replace ```localhost``` with whatever your host running OpenOCD is:

```bash
arm-none-eabi-gdb <PATH_TO_BUILD>/Adapters/adv/picoTrackerAdvance.elf \
  --eval-command="target extended-remote localhost:3333" \
  --eval-command="load" \
  --eval-command="monitor reset init" \
  --eval-command="continue"
```

This command should load your code into the Advance, reset it and run the program. You can go ahead now and use any GDB functionality you may like, or not use it at all other than to load and run your code.

## MIDI Development

### Debugging MIDI TRS (serial) output

On the Advance, TRS MIDI uses its own dedicated UART (UART7, `MIDI_UART`) while debug logging uses UART1, so — unlike earlier hardware — you do **not** need to disable MIDI to get a debug console; both can be connected at the same time.

A helpful tool for debugging MIDI output is [ShowMIDI](https://github.com/gbevin/ShowMIDI) or for more low level debugging you may want to use a [BusPirate](http://dangerousprototypes.com/docs/Bus_Pirate).

