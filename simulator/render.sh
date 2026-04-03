#!/bin/sh
# Render simulator screenshots to simulator/screenshots/
# Usage: ./render.sh [page]       — page 1-4, omit for all pages
#        ./render.sh autopilot    — render autopilot app
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
OUT_DIR="$SCRIPT_DIR/screenshots"

cmake --build "$BUILD_DIR" --parallel 2>&1 | grep -v "^\[" || true

mkdir -p "$OUT_DIR"
cd "$OUT_DIR"

"$BUILD_DIR/lvgl_sim" ${1+"$1"}
