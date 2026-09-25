# Waveshare ESP32-S3-RGB-Matrix + four 64×64 panels (256×64)

The same 256×64 wall as the [MatrixPortal S3 build](matrixportal-s3-4x1.md), on
Waveshare's **ESP32-S3-RGB-Matrix** driver board (SKU 34422). The build env is
`waveshare_s3_matrix_4x1`, and it is the default: a bare `pio run` builds it.

Everything on the panel side is shared with the MatrixPortal guide, which this
page links to rather than repeats: chain order, the frame, flicker, your own
receiver, units. This page covers the board itself, and how to flash and debug it.

> **Not yet run on real hardware.** The firmware builds, a debug session gets as
> far as looking for the board on USB, and the web UI runs against a stand-in
> for this board (see [below](#try-the-settings-page-without-a-board)). The pin
> map, memory mode and USB setup match Waveshare's schematic and example
> firmware, and a community bring-up on the real board. First light on your
> panels is what's left.

## What's different from the MatrixPortal

| | Waveshare ESP32-S3-RGB-Matrix | MatrixPortal S3 |
|---|---|---|
| Chip | ESP32-S3, as the WROOM-2-N32R16V module | ESP32-S3 |
| Flash / PSRAM | 32 MB / 16 MB, both **octal** | 8 MB / 2 MB quad |
| USB | the chip's own USB serial + JTAG: flash, logs and **debugging over one cable** | TinyUSB serial, no debugging over USB |
| Second USB-C | power only | – |
| Level shifters | 2 × SN74HC245 at 5 V | 2 × 74AHCT245 at 5 V |
| Address line E | GPIO 9, fixed: no jumper | jumper, pin 8 or 16 |
| Light sensor | **none**, and no free analog pin | onboard, GPIO 5 |
| Buttons you can use | BOOT (GPIO 0) | UP and DOWN |
| Also on the board, unused here | clock chip, motion sensor, temperature/humidity sensor, audio codec, two mics, microSD slot | accelerometer |

The HUB75 pins are fixed by the board. It is the
[ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA)
library's default ESP32-S3 pin map with E added on GPIO 9:

| Signal | GPIO | Signal | GPIO | Signal | GPIO |
|---|---|---|---|---|---|
| R1 | 4 | R2 | 7 | A | 18 |
| G1 | 5 | G2 | 15 | B | 8 |
| B1 | 6 | B2 | 16 | C | 3 |
| CLK | 41 | LAT | 40 | D | 42 |
| OE | 2 | | | E | 9 |

## Parts

| | Notes |
|---|---|
| Waveshare ESP32-S3-RGB-Matrix | SKU 34422. |
| 4 × 64×64 HUB75 panels | As in the [MatrixPortal parts list](matrixportal-s3-4x1.md#parts): all the same model, 1/32 scan. |
| 5 V power supply | 10 A minimum, 20 A if you'll run it bright. See [Power](#power). |
| Power leads + capacitors | One lead per panel, and a 1000–2000 µF capacitor (10 V or more) across each panel's input. See [Flicker](matrixportal-s3-4x1.md#flicker). |
| HUB75 ribbons | Three to join the panels. A fourth, plus a **2×8 double-row male pin header** (2.54 mm), if you mount the board off the panel: the board's HUB75 connector is a socket, and so are the ribbon's ends. |
| USB-C data cable | For flashing and logs, into the port marked for data (not **POWER**). |

## Chain order

Exactly as for the MatrixPortal. Read
[Chain order](matrixportal-s3-4x1.md#chain-order): the panel the board feeds shows
the right-most 64 columns seen from the front, and **Rotate 180°** in the web UI
covers mounting the row the other way up. There is no address-E jumper to set on
this board: E is wired to GPIO 9.

## Power

- **Power the panels straight from the 5 V supply**, one lead each. Four 64×64
  panels can draw roughly 16 A at full white. At the default brightness the
  flight cards draw a small fraction of that.
- **Feed the board from the same supply, through its two screw posts** (5 V and
  GND). The posts are on the board's 5 V rail, the one the **POWER** USB-C also
  feeds.
- **Never feed the posts and the POWER USB-C at once.** The schematic joins them
  directly, with no diode, so the supply would push current into the charger.
- **The data USB-C can stay plugged into a computer while the supply is on.** Its
  5 V reaches the board's rail through a MOSFET arranged as an ideal diode, which
  should stop the supply pushing current back into the computer. That's read
  from the schematic, not a promise from Waveshare, so if you'd rather not rely
  on it, flash with the panel supply off.
- **Don't try to run the panels from USB-C.** With no USB Power Delivery chip the
  board can take at most 15 W (5 V, 3 A) from a USB-C charger.
- The community notes quote Waveshare's rating for the board as 10 A. That's
  another reason to wire panel power around the board, not through it.

## Mounting and WiFi

The board's module has a PCB antenna, like the MatrixPortal's. Upstream measured a
~20 dB drop in WiFi signal with a MatrixPortal plugged straight onto a panel (see
[Mounting and WiFi](matrixportal-s3-4x1.md#mounting-and-wifi)), and nothing about
this board's design suggests it would fare better. So mount it on a short ribbon
(with the 2×8 male header as a gender changer), with the antenna end clear of the
panels and the backer.

## Install the tools

On the computer the board plugs into. Nothing below can be done from a cloud
session: flashing and debugging need the board on your USB port.

1. **VS Code + the PlatformIO IDE extension** (recommended), or the command line
   alone: `pipx install platformio` (or `pip install platformio`). PlatformIO
   brings its own Python, so none needs installing for it.
2. **Git** ([Git for Windows](https://git-scm.com/downloads) on a PC; macOS and
   most Linux systems already have it). It clones this repository, and PlatformIO
   needs it too: the panel library is fetched from its git repository, and
   without Git the build stops with "Please install Git client". Restart VS Code
   after installing it.
3. **Open the `firmware/` folder.** The first build downloads everything else:
   the ESP32-S3 compiler, the Arduino core, the libraries, and, for debugging,
   OpenOCD and GDB. That's about 2 GB on disk.
4. **USB driver, depending on your system:**
   - **macOS:** nothing to install.
   - **Windows 10/11:** the serial port works with Windows' own driver. For
     **debugging**, install Espressif's USB driver, or OpenOCD reports
     `LIBUSB_ERROR_NOT_FOUND`. From PowerShell, either run `eim install-drivers`
     (Espressif Installation Manager), or:

     ```powershell
     Invoke-WebRequest 'https://dl.espressif.com/dl/idf-env/idf-env.exe' -OutFile .\idf-env.exe; .\idf-env.exe driver install --espressif
     ```
   - **Linux:** install PlatformIO's udev rules, which include this board's USB
     ID (303a:1001) for both serial and debugging. Then unplug and replug the board.

     ```bash
     curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
     sudo udevadm control --reload-rules && sudo udevadm trigger
     ```
5. **Plug the board's data USB-C into the computer** (not the one marked POWER).
   It shows up as Espressif's USB JTAG/serial device, ID `303A:1001`.

**Not needed for this board:** the CP210x or CH340 drivers many ESP32 guides
start with (it has no USB-to-serial chip), the Arduino IDE, ESP-IDF, or a
separate JTAG probe.

## Flash it

### First install, from a browser

No tools needed on the computer doing the flashing, only Chrome or Edge.

1. Build the one-file factory image (on any machine with PlatformIO). On
   Windows, run it from VS Code's PlatformIO terminal as
   `python tools\make_factory_image.py`.

   ```bash
   python3 tools/make_factory_image.py
   # -> firmware/.pio/build/waveshare_s3_matrix_4x1/flightwall-waveshare_s3_matrix_4x1-factory.bin
   ```

   It holds the bootloader, partition table, firmware and filesystem (web UI and
   airline logos), each at its own offset, so the whole file goes to address 0x0.
2. Open Espressif's [esptool-js](https://espressif.github.io/esptool-js/) and press
   **Connect**. Pick the board's port.
3. Set the flash address to `0x0`, choose the file, and press **Program**.
4. When it finishes, press **RESET** on the board.

The factory image **wipes WiFi and settings**, which is right for a first install
only. Update a working wall over the air, or with PlatformIO as below.

### Day to day, with PlatformIO

```bash
cd firmware
pio run -t upload       # firmware  (env waveshare_s3_matrix_4x1, the default)
pio run -t uploadfs     # web UI + airline logos -- ERASES saved settings; first time only
```

In VS Code these are **Upload**, and **Upload Filesystem Image** under Project
Tasks → waveshare_s3_matrix_4x1 → Platform.

### If the port vanishes or the board boot-loops

Hold **BOOT**, press and release **RESET**, then release BOOT. The chip's own
bootloader now waits for an upload, whatever the firmware was doing. Upload
again, then press RESET. Espressif's docs give the same fix for firmware that
switches off the USB port; this firmware doesn't.

**One memory-mode trap, already handled.** This module's flash is octal, so the env
sets `memory_type = opi_opi`. With the DevKit's `qio_opi` an image flashes cleanly
and then fails on every boot with
`assert failed: do_core_init ... (flash_ret == ESP_OK)`. If you ever see that, the
env is wrong, not the board.

## Serial monitor

```bash
pio device monitor
```

Crash backtraces come out with function names and line numbers: the env turns on
the exception decoder. Look for `[hub75] 256x64 (4 x 64x64), refresh ~116 Hz` in
the boot log. Any serial terminal works too: this board's USB serial is the
chip's own, so the MatrixPortal's need to raise DTR doesn't apply.

## Debug with breakpoints

The ESP32-S3 has a JTAG debugger built into its USB port, so the same data cable
does it. No probe, no extra wiring.

- **VS Code:** open Run and Debug, pick **PIO Debug**, and start it. It builds a
  debug image of the default env, flashes it, and stops at `setup()`. Then use
  breakpoints, stepping and variables as usual.
- **Command line:** `pio debug --interface=gdb -- -x .pioinit`.
- **Windows and Linux need the driver or udev rules** from
  [Install the tools](#install-the-tools).
- **While the chip is stopped, WiFi and the web page stop answering.** Afterwards,
  flash a normal build with `pio run -t upload`: the debug build is unoptimised.

The env uses Espressif's standalone GDB 11.2, not the GDB bundled with this
toolchain, which needs Python 2.7 and fails to start on current Linux (Ubuntu
24.04: `libpython2.7.so.1.0: cannot open shared object file`). See
`tools/esp_gdb.py`. Its launcher uses whichever supported Python 3 it finds, and
runs a build without Python if there's none. That includes the Windows one,
`xtensa-esp32s3-elf-gdb.exe`, so debugging needs no Python install either.
OpenOCD reaches the chip through Espressif's `esp_usb_jtag` driver
(`interface/esp_usb_jtag.cfg` with `target/esp32s3.cfg`).

## First-time setup

Identical to the MatrixPortal: follow
[First-time setup](matrixportal-s3-4x1.md#first-time-setup) from the
`FlightWall-Setup` hotspot onward. **Advanced → HUB75 panel** should already read
64 × 64 × 4.

## Auto-dim: use the schedule

This board has **no light sensor, and no free pin for an analog one.** The
ESP32-S3 can only read analog voltages on GPIO 1–10 while WiFi is running, and
the board uses all ten: 2–9 for the panel, 1 for the microSD clock, 10 for the
clock chip. The web UI knows this and doesn't offer the analog option.

Use **Display → Brightness schedule** instead: a day brightness, a night
brightness, and the hours to switch between them. Set the timezone in the same
section, so "night" is yours and not UTC's.

**An I2C light sensor is possible, with some soldering.** The firmware puts I2C on
the 4-pin GPIO header (silkscreen GND, 3V3, IO46, IO45: SDA 46, SCL 45), and the
web UI offers BH1750 and TCS3472. Two catches, both from the schematic:

- Each header pin has a **10 kΩ pull-down** fitted (R59, R60). That drags a
  typical sensor board's 4.7–10 kΩ pull-ups below a logic high. Either remove the
  two resistors, or add ~2 kΩ pull-ups from each line to 3V3.
- With pull-ups on IO46, **hold-BOOT recovery stops working**: that pin has to be
  low at reset for the chip to enter its bootloader. Normal boots are fine. Unplug
  the sensor before recovering a board.

## Panels that stay dark, or lose a column

All three settings are under **Advanced → HUB75 panel → Signal tuning**, and each
takes a Save and Restart.

- **Dark, or garbage, on a supply you've measured at 5 V:** set **Driver chip** to
  **FM6126A**. Seven of the ten Arduino examples in Waveshare's repository set it,
  four of them for 64×64 panels.
- **Right-most column missing, or everything shifted by one pixel:** turn
  **Clock phase** off. It is off by default.
- **Sparkles or ghosting:** drop the clock to **16 MHz**, then **8 MHz**. This board
  buffers the panel signals with SN74HC245 chips run from 5 V. Chips of that family
  are only guaranteed to see about 3.5 V as a logic high at a 5 V supply, and the
  ESP32-S3 drives 3.3 V. It works in practice, but the margin is thin. Adafruit
  uses the AHCT family, which is built for 3.3 V inputs, for exactly this reason.
  Keeping the supply nearer 5.0 V than 5.2 V helps a little too.

For flicker from the supply itself, see [Flicker](matrixportal-s3-4x1.md#flicker).

## Try the settings page without a board

The real web page, driven by a stand-in for this board: its pins, its missing light
sensor, its single button, and a fresh board's defaults.

```bash
node tools/webui_stub.mjs --board waveshare                      # http://localhost:8099
node tools/webui_stub.mjs --export emulator.html --board waveshare   # one file, no server
```

The exported file runs from disk in any browser. **Save** works, but only into
that browser's local storage, and passwords you type are dropped rather than
stored. **Reset** at the top clears it. Use `--board matrixportal` or
`--board devkit` for the other boards.

## Sources

- **Waveshare's repository**,
  [waveshareteam/ESP32-S3-RGB-Matrix](https://github.com/waveshareteam/ESP32-S3-RGB-Matrix)
  (commit `4047e4e`, August 2026):
  - the schematic, `hardware/schematics/ESP32-S3-RGB-Matrix-Schematics.pdf`: the
    module, the pin table, the USB and power paths, the level shifters, the header
    and its pull-downs;
  - the ESP-IDF board support `config.h` and `sdkconfig.defaults`;
  - the Arduino examples' `esp32s3-default-pins.hpp`.
- **A community bring-up on the real board**,
  [NickoScope/waveshare-rgb-matrix-p2-64x64](https://github.com/NickoScope/waveshare-rgb-matrix-p2-64x64):
  - `docs/12-bringup.md`: the `opi_opi` boot failure, 32 MB flash and 16 MB 1.8 V
    PSRAM read by esptool, and the header silkscreen;
  - `docs/11-control-and-pins.md`: the header pull-downs, measured;
  - `docs/02-controller.md`: the driver-chip notes, and the 10 A rating it quotes
    from Waveshare.
- **Espressif**:
  - the ESP-IDF documentation source, `api-guides/jtag-debugging/configure-builtin-jtag.rst`
    and `api-guides/usb-serial-jtag-console.rst`: the USB pins, drivers, udev rules
    and manual download mode;
  - the [esptool-js](https://github.com/espressif/esptool-js) README: browser
    support, and the live flasher's address.
