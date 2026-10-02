#!/usr/bin/env bash
# Runs inside python:3.11-slim (see AGENTS.md): native unit tests + firmware build.
#   $1 = "test" | "build" | "all" (default)
set -euo pipefail

MODE="${1:-all}"

if ! command -v pio >/dev/null 2>&1; then
  # Pinned: newer PlatformIO Core fails on the pioarduino package-postinstall step.
  pip install -q platformio==6.1.16
fi
if [[ "$MODE" != "build" ]] && ! command -v g++ >/dev/null 2>&1; then
  DEBIAN_FRONTEND=noninteractive apt-get update -qq
  DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential >/dev/null
fi

mkdir -p /build/fw
rm -rf /build/fw/src /build/fw/web /build/fw/test
cp -a /firmware/. /build/fw/
cd /build/fw

if [[ "$MODE" != "build" ]]; then
  pio test -e native
fi
if [[ "$MODE" != "test" ]]; then
  pio run -e esp32c3
  mkdir -p /out
  cp .pio/build/esp32c3/firmware.bin /out/ 2>/dev/null || true
fi
