"""A pool across two OLEDs, with a sun in the yellow band.

The PiCowbell STEMMA connector is GP4 for SDA and GP5 for SCL.
The left OLED answers at 0x3C and the right OLED answers at 0x3D.
The LSM6DSOX is the chip at 0x6A or 0x6B whose id reads 0x6C.
Encoder A is GP16, B is GP17, and the click is GP18.

The top 16 rows are the yellow band. The encoder slides a sun across
both screens, and the click turns that sun into a moon. The blue rows
are one pool. Tilt pours the pool toward the low side.
A clockwise turn, A changing before B, moves the sun toward the right.
"""

import array
import time
from machine import I2C, Pin, disable_irq, enable_irq

SDA = 4
SCL = 5
PIN_A = 16
PIN_B = 17
PIN_CLICK = 18

LEFT = 0x3C
RIGHT = 0x3D
IMU_ADDRS = (0x6A, 0x6B)
IMU_ID = 0x6C
WHO_AM_I = 0x0F
CTRL1_XL = 0x10
CTRL3_C = 0x12
OUTX_L_A = 0x28
# ±2 g. One g is about 16393 counts.
LSB_G = 0.000061

# 0 is accelerometer X, 1 is Y, 2 is Z.
# Across points at the right-hand screen. Down points at the bottom of the blue band.
# On this board Y reads about -1 g when the yellow band is up, so down is flipped.
AXIS_ACROSS = 0
AXIS_DOWN = 1
SIGN_ACROSS = 1
SIGN_DOWN = -1

WIDTH = 256
HEIGHT = 64
YELLOW = 16
PAGES = HEIGHT // 8
SKY_Y = 7
SUN_R = 5
SUN_REACH = 8
SUN_MIN = SUN_REACH
SUN_MAX = WIDTH - 1 - SUN_REACH
SUN_STEP = 10

# A wide puddle. Half of it sits on each screen when the board is level.
COLS = 15
ROWS = 5
N = COLS * ROWS

ENC_DETENT = 4
CLICK_GAP_MS = 200
ENC_TABLE = (
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0,
)

INIT = bytes((
    0x00,
    0xAE,
    0xD5, 0x80,
    0xA8, 0x3F,
    0xD3, 0x00,
    0x40,
    0x8D, 0x14,
    0x20, 0x00,
    0xA1,
    0xC8,
    0xDA, 0x12,
    0x81, 0xCF,
    0xD9, 0xF1,
    0xDB, 0x40,
    0xA4,
    0xA6,
    0xAF,
))
COL_WIN = bytes((0x00, 0x21, 0x00, 0x7F))
PAGE_WIN = bytes((0x00, 0x22, 0x00, 0x07))

frame = bytearray(WIDTH * PAGES)
blank = bytearray(WIDTH * PAGES)
pagebuf = bytearray(129)
pagebuf[0] = 0x40
view = memoryview(frame)
pageview = memoryview(pagebuf)
raw = bytearray(6)

xs = array.array("f", (0.0 for _ in range(N)))
ys = array.array("f", (0.0 for _ in range(N)))
vxs = array.array("f", (0.0 for _ in range(N)))
vys = array.array("f", (0.0 for _ in range(N)))
ox = array.array("f", (0.0 for _ in range(N)))
oy = array.array("f", (0.0 for _ in range(N)))

i2c = I2C(0, scl=Pin(SCL), sda=Pin(SDA), freq=400000, timeout=100000)
imu = None
left_ok = False
right_ok = False
gx = 0.0
gy = 0.0
sun_x = 64
moon = False
click_down = False
click_at = 0
enc_a = Pin(PIN_A, Pin.IN, Pin.PULL_UP)
enc_b = Pin(PIN_B, Pin.IN, Pin.PULL_UP)
enc_sw = Pin(PIN_CLICK, Pin.IN, Pin.PULL_UP)
enc_state = (enc_a.value() << 1) | enc_b.value()
enc_counts = 0
enc_rest = 0


def s16(buf, index):
    value = buf[index] | (buf[index + 1] << 8)
    if value & 0x8000:
        value -= 0x10000
    return value


def sky(x, y):
    if 0 <= x < WIDTH and 0 <= y < YELLOW - 1:
        frame[(y >> 3) * WIDTH + x] |= 1 << (y & 7)


def blot(x, y):
    xi = int(x)
    yi = int(y)
    for dy in (-1, 0, 1):
        py = yi + dy
        if py < YELLOW or py >= HEIGHT:
            continue
        bit = 1 << (py & 7)
        row = (py >> 3) * WIDTH
        for dx in (-1, 0, 1):
            if dx * dx + dy * dy > 2:
                continue
            px = xi + dx
            if 0 <= px < WIDTH:
                frame[row + px] |= bit


def draw_disc(cx, cy, radius, cut_x, cut_y, cut_r):
    limit = radius * radius
    cut = cut_r * cut_r
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if dx * dx + dy * dy > limit:
                continue
            if cut_r and (dx - cut_x) * (dx - cut_x) + (dy - cut_y) * (dy - cut_y) <= cut:
                continue
            sky(cx + dx, cy + dy)


def draw_sun():
    cx = sun_x
    draw_disc(cx, SKY_Y, SUN_R, 0, 0, 0)
    for dx, dy in ((1, 0), (-1, 0), (0, -1), (1, -1), (-1, -1), (1, 1), (-1, 1)):
        sky(cx + dx * 7, SKY_Y + dy * 7)
        sky(cx + dx * 8, SKY_Y + dy * 8)


def draw_horizon():
    row = (15 >> 3) * WIDTH
    bit = 1 << (15 & 7)
    for x in range(WIDTH):
        frame[row + x] |= bit


def init_oled(addr):
    i2c.writeto(addr, INIT)
    return True


def find_imu():
    for addr in IMU_ADDRS:
        try:
            if i2c.readfrom_mem(addr, WHO_AM_I, 1)[0] == IMU_ID:
                i2c.writeto_mem(addr, CTRL3_C, b"\x44")
                i2c.writeto_mem(addr, CTRL1_XL, b"\x50")
                return addr
        except OSError:
            pass
    return None


def bringup():
    global imu, left_ok, right_ok
    found = [hex(addr) for addr in i2c.scan()]
    print("i2c", " ".join(found))
    try:
        left_ok = init_oled(LEFT)
    except OSError:
        left_ok = False
    try:
        right_ok = init_oled(RIGHT)
    except OSError:
        right_ok = False
    imu = find_imu()
    print("left", hex(LEFT) if left_ok else "missing")
    print("right", hex(RIGHT) if right_ok else "missing")
    print("imu", hex(imu) if imu is not None else "missing")


def on_encoder(_pin):
    global enc_state, enc_counts
    state = (enc_a.value() << 1) | enc_b.value()
    enc_counts += ENC_TABLE[(enc_state << 2) | state]
    enc_state = state


def take_steps():
    global enc_counts, enc_rest
    irq = disable_irq()
    counts = enc_counts
    enc_counts = 0
    enable_irq(irq)
    enc_rest += counts
    if enc_rest >= 0:
        steps = enc_rest // ENC_DETENT
    else:
        steps = -((-enc_rest) // ENC_DETENT)
    enc_rest -= steps * ENC_DETENT
    return steps


def move_sun(steps):
    global sun_x
    if steps == 0:
        return
    sun_x += steps * SUN_STEP
    if sun_x < SUN_MIN:
        sun_x = SUN_MIN
    elif sun_x > SUN_MAX:
        sun_x = SUN_MAX


def poll_click(now):
    global click_down, click_at, moon
    down = enc_sw.value() == 0
    if down and not click_down and time.ticks_diff(now, click_at) >= CLICK_GAP_MS:
        moon = not moon
        click_at = now
    click_down = down


def read_gravity():
    global gx, gy
    across = 0.0
    down = 0.0
    if imu is not None:
        try:
            i2c.readfrom_mem_into(imu, OUTX_L_A, raw)
            sample = (s16(raw, 0) * LSB_G, s16(raw, 2) * LSB_G, s16(raw, 4) * LSB_G)
            across = SIGN_ACROSS * sample[AXIS_ACROSS]
            down = SIGN_DOWN * sample[AXIS_DOWN]
        except OSError:
            pass
    gx += (across - gx) * 0.45
    gy += (down - gy) * 0.45


def puddle_target():
    # One g of sideways tilt carries the pool most of the way onto one screen.
    tx = WIDTH / 2 + gx * 100.0
    if tx < 48.0:
        tx = 48.0
    elif tx > WIDTH - 48.0:
        tx = WIDTH - 48.0
    ty = 46.0 + gy * 10.0
    if ty < 32.0:
        ty = 32.0
    elif ty > 54.0:
        ty = 54.0
    return tx, ty


def step_water(_dt):
    tx, ty = puddle_target()
    for i in range(N):
        hx = tx + ox[i]
        hy = ty + oy[i]
        vx = vxs[i] * 0.55 + (hx - xs[i]) * 0.55
        vy = vys[i] * 0.55 + (hy - ys[i]) * 0.55
        vxs[i] = vx
        vys[i] = vy
        xs[i] += vx
        ys[i] += vy


def draw_water():
    for i in range(N):
        blot(xs[i], ys[i])


def flush(addr, x0):
    i2c.writeto(addr, COL_WIN)
    i2c.writeto(addr, PAGE_WIN)
    for page in range(PAGES):
        start = page * WIDTH + x0
        pageview[1:129] = view[start:start + 128]
        i2c.writeto(addr, pagebuf)


def present():
    global left_ok, right_ok
    if left_ok:
        try:
            flush(LEFT, 0)
        except OSError:
            left_ok = False
            print("left missing")
    if right_ok:
        try:
            flush(RIGHT, 128)
        except OSError:
            right_ok = False
            print("right missing")


def seed_pool():
    for i in range(N):
        col = i % COLS
        row = i // COLS
        ox[i] = (col - (COLS - 1) / 2) * 5.0
        oy[i] = (row - (ROWS - 1) / 2) * 3.5
        xs[i] = WIDTH / 2 + ox[i]
        ys[i] = 46.0 + oy[i]
        vxs[i] = 0.0
        vys[i] = 0.0


def main():
    global click_at
    bringup()
    seed_pool()
    enc_a.irq(on_encoder, Pin.IRQ_RISING | Pin.IRQ_FALLING)
    enc_b.irq(on_encoder, Pin.IRQ_RISING | Pin.IRQ_FALLING)
    click_at = time.ticks_ms()
    last = click_at
    while True:
        now = time.ticks_ms()
        dt = time.ticks_diff(now, last) / 1000
        last = now
        if dt < 0:
            dt = 0.0
        elif dt > 0.05:
            dt = 0.05
        move_sun(take_steps())
        poll_click(now)
        read_gravity()
        step_water(dt)
        frame[:] = blank
        draw_water()
        draw_horizon()
        if moon:
            draw_disc(sun_x, SKY_Y, SUN_R, 3, -1, 4)
        else:
            draw_sun()
        present()


main()
