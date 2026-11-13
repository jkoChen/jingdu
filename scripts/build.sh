#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 scripts/embed_web.py
if [ -z "${PLATFORMIO_CORE_DIR:-}" ] && [ -d "$PWD/.local/pio/platforms" ]; then export PLATFORMIO_CORE_DIR="$PWD/.local/pio"; fi
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=no PYTHONDONTWRITEBYTECODE=1
if [ -z "${PIO:-}" ] && [ -x .local/pio/penv/bin/pio ]; then PIO="$PWD/.local/pio/penv/bin/pio"; fi
exec "${PIO:-pio}" run -e jingdu -j "${JOBS:-4}"
