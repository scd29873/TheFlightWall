#!/usr/bin/env python3
"""
Build ONE file that takes a board from blank to running: bootloader, partition
table, boot_app0, the firmware and the LittleFS image (web UI + logo tiles),
merged at the offsets this env's build uses. Flash it at 0x0 -- from a browser
with no install (Chrome or Edge: https://espressif.github.io/esptool-js/), or
with esptool:

    esptool.py --chip esp32s3 write_flash 0x0 <image>

Usage (from the repo root or anywhere):

    python3 tools/make_factory_image.py                        # waveshare_s3_matrix_4x1
    python3 tools/make_factory_image.py -e matrixportal_s3_4x1

The image lands at firmware/.pio/build/<env>/flightwall-<env>-factory.bin.

IT IS A FIRST-INSTALL IMAGE. merge_bin pads the gaps between parts with erased
flash, so the NVS partition (WiFi credentials) and the whole filesystem --
/settings.json included -- are wiped. That is what a first install wants and
what an update does not: update a working wall over OTA, or with
`pio run -t upload`, which writes the app alone.

Nothing here is hardcoded per board. The offsets come from PlatformIO itself
(its idedata: bootloader, partitions, boot_app0, app) and from the built
partition table (the filesystem); the flash mode, frequency and size are kept
from the bootloader header PlatformIO wrote, which is the only place they are
guaranteed to match what `pio run -t upload` would send.
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMWARE = os.path.join(REPO, "firmware")

# The image header's chip ID (esptool's ESPLoader.IMAGE_CHIP_ID values).
CHIP_IDS = {0: "esp32", 2: "esp32s2", 5: "esp32c3", 9: "esp32s3", 12: "esp32c2",
            13: "esp32c6", 16: "esp32h2", 18: "esp32p4"}


def find_pio(explicit):
    # PATH first (VS Code's PlatformIO terminal puts pio there), then where
    # PlatformIO installs its own copy: penv/bin on macOS and Linux,
    # penv\Scripts\pio.exe on Windows.
    for candidate in (explicit, os.environ.get("PIO"), shutil.which("pio"), shutil.which("platformio"),
                      os.path.expanduser("~/.platformio/penv/bin/pio"),
                      os.path.expanduser("~/.platformio/penv/Scripts/pio.exe")):
        if candidate and os.path.exists(candidate):
            return candidate
    sys.exit("cannot find PlatformIO's `pio`; pass --pio /path/to/pio")


def run(cmd):
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=FIRMWARE, check=True)


def filesystem_partition(partitions_bin):
    """(offset, size) of the data/spiffs partition LittleFS lives in."""
    with open(partitions_bin, "rb") as f:
        table = f.read()
    for i in range(0, len(table) - 31, 32):
        magic, ptype, subtype, offset, size = struct.unpack_from("<HBBII", table, i)
        if magic != 0x50AA:  # 0xEBEB is the MD5 row, 0xFFFF the end
            break
        if ptype == 0x01 and subtype == 0x82:
            return offset, size
    sys.exit(f"no data/spiffs partition in {partitions_bin}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-e", "--env", default="waveshare_s3_matrix_4x1")
    ap.add_argument("--pio", help="path to PlatformIO's pio, if it is not on PATH")
    args = ap.parse_args()
    pio = find_pio(args.pio)
    build = os.path.join(FIRMWARE, ".pio", "build", args.env)

    # idedata FIRST: when the project config has changed, PlatformIO clears the
    # build directory on the next run, and it counts this as one.
    run([pio, "run", "-e", args.env, "-t", "idedata"])
    with open(os.path.join(build, "idedata.json")) as f:
        extra = json.load(f)["extra"]
    run([pio, "run", "-e", args.env])
    run([pio, "run", "-e", args.env, "-t", "buildfs"])

    parts = [(img["offset"], img["path"]) for img in extra["flash_images"]]
    parts.append((extra["application_offset"], os.path.join(build, "firmware.bin")))
    fs_offset, fs_size = filesystem_partition(os.path.join(build, "partitions.bin"))
    fs_image = os.path.join(build, "littlefs.bin")
    if os.path.getsize(fs_image) > fs_size:
        sys.exit(f"{fs_image} is larger than its partition ({fs_size} bytes)")
    parts.append((hex(fs_offset), fs_image))

    bootloader = next(path for off, path in parts if path.endswith("bootloader.bin"))
    with open(bootloader, "rb") as f:
        chip_id = struct.unpack_from("<H", f.read(14), 12)[0]
    chip = CHIP_IDS.get(chip_id)
    if not chip:
        sys.exit(f"unknown chip ID {chip_id} in {bootloader}")

    out = os.path.join(build, f"flightwall-{args.env}-factory.bin")
    cmd = [pio, "pkg", "exec", "--package", "tool-esptoolpy", "--", "esptool.py", "--chip", chip,
           "merge_bin", "-o", out, "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"]
    for off, path in sorted(parts, key=lambda p: int(p[0], 16)):
        cmd += [off, path]
    run(cmd)

    with open(out, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    print(f"\n{out}\n  {os.path.getsize(out)} bytes, sha256 {digest}")
    print("  parts: " + ", ".join(f"{off} {os.path.basename(path)}" for off, path in
                                  sorted(parts, key=lambda p: int(p[0], 16))))
    print(f"  flash at 0x0: esptool.py --chip {chip} write_flash 0x0 {os.path.basename(out)}")
    print("  first install only: this wipes WiFi and settings (see the top of this script)")


if __name__ == "__main__":
    main()
