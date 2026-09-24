#!/usr/bin/env bash
#
# Render the wall's screens on your computer, no board or panels needed.
#
#   tools/panel_preview/run.sh            # 256x64 (the 4x1 MatrixPortal wall)
#   tools/panel_preview/run.sh 2          # chain of 2 -> 128x64, for comparison
#   PREVIEW_ROTATE180=1 tools/panel_preview/run.sh
#
# Compiles the UNMODIFIED firmware/adapters/Hub75Display.cpp with g++ against
# Adafruit GFX and the stand-ins in shim/, runs every screen through it, and
# writes PNGs (plus sheet.png) to tools/panel_preview/out/. Because it is the
# real display code, it doubles as a compile check of that file.
#
# Adafruit GFX is taken from a PlatformIO build if there has been one
# (firmware/.pio/libdeps/*), else from GFX_DIR, else cloned once into .cache/.
# Needs g++ and Python 3 with Pillow (pip install -r tools/requirements.txt).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

command -v "${CXX:-g++}" >/dev/null || { echo "needs a C++ compiler (g++ or set CXX)" >&2; exit 2; }
python3 -c "import PIL" 2>/dev/null ||
  { echo "needs Pillow: pip install -r tools/requirements.txt" >&2; exit 2; }
fw="$(cd "$here/../../firmware" && pwd)"
out="$here/out"
chain="${1:-4}"

gfx="${GFX_DIR:-}"
if [ -z "$gfx" ]; then
  for d in "$fw"/.pio/libdeps/*/"Adafruit GFX Library"; do
    if [ -f "$d/Adafruit_GFX.cpp" ]; then gfx="$d"; break; fi
  done
fi
if [ -z "$gfx" ]; then
  gfx="$here/.cache/Adafruit-GFX-Library"
  [ -f "$gfx/Adafruit_GFX.cpp" ] ||
    git clone --quiet --depth 1 https://github.com/adafruit/Adafruit-GFX-Library.git "$gfx"
fi

mkdir -p "$out"
rm -f "$out"/*.rgb565 "$out"/*.png

# The same defines the matrixportal_s3_4x1 env passes (platformio.ini).
"${CXX:-g++}" -std=c++17 -O1 -w \
  -DARDUINO=10819 -DFLIGHTWALL_BOARD_MATRIXPORTAL_S3 -DPIXEL_COLOR_DEPTH_BITS=6 \
  -DFW_PANEL_RES_X=64 -DFW_PANEL_RES_Y=64 -DFW_PANEL_CHAIN=4 -DFW_HUB75_MIN_REFRESH_HZ=90 \
  -DPREVIEW_DATA_DIR="\"$fw/data\"" \
  -I "$here/shim" -I "$gfx" -I "$fw" \
  "$here/preview.cpp" "$fw/adapters/Hub75Display.cpp" "$gfx/Adafruit_GFX.cpp" \
  -o "$out/preview"

# First line of output is the geometry and the refresh rate the library would pick.
"$out/preview" "$out" "$chain" 2>/dev/null | head -1
python3 "$here/render.py" "$out" >/dev/null
echo "wrote $out/*.png and $out/sheet.png"
