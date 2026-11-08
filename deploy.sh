#!/bin/sh
# Copy main.py onto a Pico 2W that already has the Pimoroni MicroPython build.
# Plug the Pico's own micro-USB in. The Unicorn and UPS can stay attached.
# Hold BOOTSEL only when the RP2350 drive must be reinstalled.
set -e
cd "$(dirname "$0")"

if [ -d /Volumes/RP2350 ]; then
  echo "Pico is in BOOTSEL. Reinstall the firmware, then run this again once the serial port appears."
  exit 1
fi

port=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
if [ -z "$port" ]; then
  echo "No Pico serial port. Plug a data cable into the Pico's micro-USB."
  exit 1
fi

python3 -m mpremote connect "$port" fs cp main.py :main.py
python3 -m mpremote connect "$port" reset
echo "Copied main.py to $port"
