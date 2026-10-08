#!/usr/bin/env bash
# Flash Open Firenet firmware to ESP32-S3
# Preserves NVS partition (WiFi credentials are kept across flashes)

set -e

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS] [PORT]

Flash the Open Firenet firmware onto an ESP32-S3.

Arguments:
  PORT              Serial port of the ESP32-S3 (default: auto-detected)

Options:
  -h, --help        Show this help message and exit
  --build           Compile only, do not flash
  --ota <ip>        Flash via WiFi ArduinoOTA instead of serial
                    If the bridge has an update password, give it in the
                    OTA_PASSWORD environment variable

Examples:
  $(basename "$0")                    Flash via serial (auto-detect port)
  $(basename "$0") /dev/ttyACM1       Flash via a specific serial port
  $(basename "$0") --build            Compile check only
  $(basename "$0") --ota 192.168.1.93 Flash via WiFi OTA
  OTA_PASSWORD=secret $(basename "$0") --ota 192.168.1.93

Notes:
  - Requires: arduino-cli, esptool (serial) or python3 (OTA)
  - Only bootloader + partition table + app are flashed in serial mode.
    The NVS partition (0x9000) is left untouched — WiFi credentials survive.
  - The port is released automatically if held by another process.
EOF
}

OTA_IP=""
BUILD_ONLY=0
PORT=""

detect_port() {
  for p in /dev/ttyACM0 /dev/ttyACM1 /dev/ttyUSB0 /dev/ttyCH343USB0; do
    if [ -e "$p" ]; then
      echo "$p"
      return 0
    fi
  done
  return 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --build)
      BUILD_ONLY=1
      ;;
    --ota)
      shift
      OTA_IP="${1:?--ota requires an IP address}"
      ;;
    --ota=*)
      OTA_IP="${1#--ota=}"
      ;;
    -*)
      echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    *)
      PORT="$1"
      ;;
  esac
  shift
done

[ -z "$PORT" ] && PORT="$(detect_port)"

BUILD_DIR="$(dirname "$0")/.build"
FQBN="esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,FlashSize=4M,PartitionScheme=min_spiffs"

echo "=== Open Firenet ==="
if [[ "$BUILD_ONLY" -eq 1 ]]; then
  echo "Mode   : Build only"
elif [[ -n "$OTA_IP" ]]; then
  echo "Mode   : OTA (WiFi ArduinoOTA)"
  echo "Target : $OTA_IP:3232"
else
  echo "Mode   : Serial"
  echo "Port   : ${PORT:-<none detected>}"
fi
echo "Board  : ESP32-S3 (min_spiffs OTA partition)"
echo ""

# Compile
echo "[1/2] Compiling..."
arduino-cli compile \
  --fqbn "$FQBN" \
  --output-dir "$BUILD_DIR" \
  "$(dirname "$0")/open-firenet/open-firenet.ino"

if [[ "$BUILD_ONLY" -eq 1 ]]; then
  echo "Build successful. Output in $BUILD_DIR"
  exit 0
fi

if [[ -n "$OTA_IP" ]]; then
  echo "[2/2] Flashing via ArduinoOTA → $OTA_IP ..."
  ESPOTA="$(find "$HOME/.arduino15/packages/esp32" -name espota.py 2>/dev/null | sort -V | tail -n 1)"
  if [[ -z "$ESPOTA" ]]; then
    echo "Error: espota.py not found in arduino15 packages." >&2
    exit 1
  fi
  # The password goes through the environment, not an option, so it stays out of the shell history.
  OTA_AUTH=()
  if [[ -n "${OTA_PASSWORD:-}" ]]; then OTA_AUTH=(-a "$OTA_PASSWORD"); fi
  python3 "$ESPOTA" -i "$OTA_IP" -f "$BUILD_DIR/open-firenet.ino.bin" -r "${OTA_AUTH[@]}"
  echo ""
  echo "Done. Device is rebooting."
else
  if [[ -z "$PORT" ]]; then
    echo "Error: No serial port specified or detected." >&2
    exit 1
  fi

  # Release port if held
  fuser -k "$PORT" 2>/dev/null || true
  sleep 1

  # Flash (bootloader + partition table + app only — NVS at 0x9000 is untouched)
  echo "[2/2] Flashing via serial → $PORT ..."
  esptool --chip esp32s3 --port "$PORT" --baud 460800 \
    --before default-reset --after hard-reset \
    write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB \
    0x0000  "$BUILD_DIR/open-firenet.ino.bootloader.bin" \
    0x8000  "$BUILD_DIR/open-firenet.ino.partitions.bin" \
    0x10000 "$BUILD_DIR/open-firenet.ino.bin"

  echo ""
  echo "Done. Connect a serial monitor at 115200 baud to follow startup."
fi
