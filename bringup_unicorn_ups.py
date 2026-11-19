"""One-shot check for the Unicorn Pack and the Pico-UPS-A.

The running handheld program is main.py. This script lights one pixel, prints which pack buttons are held, and reads the battery monitor three times.

Unicorn Pack owns GP8 through GP22. The UPS INA219 stays on the pads it
ships with: GP6 SDA and GP7 SCL, I2C address 0x43. The shunt is 0.01 ohm,
so the current register is 1 mA per bit after calibration 4096.
"""

import time
from machine import I2C, Pin

import picounicorn

# This firmware builds PicoUnicorn as a class. Constructing it starts the PIO.
display = None

REG_CONFIG = 0x00
REG_SHUNT = 0x01
REG_BUS = 0x02
REG_CURRENT = 0x04
REG_CAL = 0x05

INA_ADDR = 0x43
# 16 V bus range, /8 shunt gain, 12-bit single samples, continuous.
CONFIG = (0 << 13) | (3 << 11) | (3 << 7) | (3 << 3) | 0x07
CAL = 4096

PIXEL = (0, 0, 255, 255, 255)


def _write(i2c, reg, value):
    i2c.writeto_mem(INA_ADDR, reg, bytes(((value >> 8) & 0xFF, value & 0xFF)))


def _read(i2c, reg):
    data = i2c.readfrom_mem(INA_ADDR, reg, 2)
    return (data[0] << 8) | data[1]


def _signed(value):
    if value > 32767:
        return value - 65536
    return value


def read_pack(i2c):
    _write(i2c, REG_CAL, CAL)
    _write(i2c, REG_CONFIG, CONFIG)
    time.sleep_ms(2)
    bus = _read(i2c, REG_BUS)
    shunt = _signed(_read(i2c, REG_SHUNT)) * 0.01
    # The current register is empty until calibration has been written.
    _write(i2c, REG_CAL, CAL)
    current = _signed(_read(i2c, REG_CURRENT))
    volts = (bus >> 3) * 0.004
    overflow = bool(bus & 0x01)
    if volts <= 3.0:
        percent = 0.0
    elif volts >= 4.2:
        percent = 100.0
    else:
        percent = (volts - 3.0) / 1.2 * 100.0
    return volts, current, shunt, percent, overflow


def main():
    global display
    display = picounicorn.PicoUnicorn()
    width = display.get_width()
    height = display.get_height()
    display.set_pixel(*PIXEL)
    print("unicorn", width, "x", height)
    print("pixel", PIXEL[0], PIXEL[1], "on")

    buttons = (
        ("A", picounicorn.BUTTON_A),
        ("B", picounicorn.BUTTON_B),
        ("X", picounicorn.BUTTON_X),
        ("Y", picounicorn.BUTTON_Y),
    )
    held = [name for name, pin in buttons if display.is_pressed(pin)]
    print("buttons", ",".join(held) if held else "none")

    i2c = I2C(1, sda=Pin(6), scl=Pin(7), freq=100000)
    found = i2c.scan()
    print("i2c", " ".join(hex(addr) for addr in found) if found else "none")
    if INA_ADDR not in found:
        print("ups missing")
        return

    for _ in range(3):
        volts, current, shunt, percent, overflow = read_pack(i2c)
        print(
            "ups",
            "{:.3f} V".format(volts),
            "{} mA".format(current),
            "{:.2f} mV shunt".format(shunt),
            "{:.0f}%".format(percent),
            "overflow" if overflow else "ok",
        )
        time.sleep_ms(200)


main()
