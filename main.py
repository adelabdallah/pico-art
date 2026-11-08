"""XOR, plasma, and Life for the Pico Unicorn Pack, plus settings and a clock.

The encoder click opens select mode, and turning it steps through XOR,
plasma, Life, settings, and the clock. Another click leaves select mode.
On settings, outside select mode, turning the encoder sets brightness.
On those three pictures, green glides the hue, Y holds the picture
under a brightness ripple, B turns the view by a quarter turn, and X
steps the speed from 10% to 100%. Red eases XOR and plasma to a stop,
and the lit pixels then breathe. On Life, red reseeds the board.
Settings shows the battery, brightness, and volume. The following
screen is the time, white hours over minutes.
On settings, X selects brightness or volume, B raises the open value,
and Y opens that value and then stores it for the next boot.
"""

import math
import random
import time
from machine import I2C, Pin, disable_irq, enable_irq

import picounicorn

WIDTH = 16
HEIGHT = 7
FRAME_MS = 40
HOLD_MS = 320
DT_CAP_MS = 200
CX = (WIDTH - 1) / 2
CY = (HEIGHT - 1) / 2

# Panel buttons, each to ground. Red is GP3 and green is GP28.
PIN_RED = 3
PIN_GREEN = 28
# Encoder to ground. A is GP0, B is GP1, and the click is GP2.
PIN_ENC_A = 0
PIN_ENC_B = 1
PIN_ENC_CLICK = 2
# One mechanical detent is four quadrature edges. A falling first is clockwise.
ENC_DETENT = 4
CLICK_GAP_MS = 200
ENC_TABLE = (
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0,
)

# Ten steps, 10% through 100%. A hold steps every half second.
SPEED_COUNT = 10
SPEED_HOLD_STEP_MS = 500
# Full speed matches the old per-frame tick on a 40 ms loop.
XOR_SHIFT_MS = FRAME_MS * 2
PLASMA_PHASE_MS = FRAME_MS // 2

# A quarter turn takes half a second, and further presses keep turning.
TURN_MS = 500
TURN_STEP = math.pi / 2

# Red eases motion over two seconds. Stopped pixels breathe across three.
PAUSE_MS = 2000
BREATHE_MS = 3000
BREATHE_LOW = 0.10
BREATHE_HIGH = 0.80

# One brightness crest crosses the matrix in about 1.2 seconds.
RIPPLE_SPEED = 14.0
RIPPLE_WIDTH = 2.2
RIPPLE_LIFE_MS = 1200
RIPPLE_CAP = 3

# Four generations a second at full speed. Red reseeds this board.
LIFE_STEP_MS = 250
LIFE_DENSITY = 0.32
# Saturated so the shared hue glide has something to turn.
LIFE_RGB = (255, 40, 130)
LIFE_TRAIL = (80, 12, 40)

# Min to max for the cycling control. The return trip takes the same time.
SWEEP_MS = 2000

# One shared hue. A tap animates a fifth of the wheel, a hold spins it.
HUE_TAP = 0.2
HUE_TAP_MS = 300
HUE_SPIN = 1.0

REG_CONFIG = 0x00
REG_BUS = 0x02
REG_CURRENT = 0x04
REG_CAL = 0x05
INA_ADDR = 0x43
INA_CONFIG = (0 << 13) | (3 << 11) | (3 << 7) | (3 << 3) | 0x07
INA_CAL = 4096
# The shunt faces the cell, so charge current reads negative.
CHARGE_MA = -20
CHARGE_REST_MS = 4000
CHARGE_FILL_MS = 1000
CHARGE_FLASH_MS = 500
CHARGE_CYCLE = CHARGE_REST_MS + CHARGE_FILL_MS + CHARGE_FLASH_MS * 2

SETTINGS_PATH = "settings.txt"
# Six steps, spread for the Unicorn gamma curve. Index 0 is the dimmest step.
LEVELS = (8, 22, 48, 100, 175, 255)
GREEN = (0, 255, 0)
YELLOW = (255, 210, 0)
RED = (255, 0, 0)
CYAN = (0, 255, 255)
WHITE = (255, 255, 255)
ORANGE = (255, 130, 0)

WAVE_N = 64
WAVE = tuple(int(math.sin(i * 2 * math.pi / WAVE_N) * 255) for i in range(WAVE_N))

# 3 by 3, top row first. On the held pack the glyph is three pixels tall.
DIGITS = (
    ("111", "101", "111"),
    ("010", "010", "010"),
    ("111", "001", "111"),
    ("111", "011", "111"),
    ("101", "111", "001"),
    ("111", "100", "111"),
    ("100", "111", "111"),
    ("111", "001", "001"),
    ("111", "010", "111"),
    ("111", "111", "001"),
)
# Columns run top to bottom when X and Y are up. Rows run left to right.
# Hours sit above minutes, in the middle of the long axis.
CLOCK_SLOTS = (
    ((11, 10, 9), (7, 6, 5)),
    ((11, 10, 9), (3, 2, 1)),
    ((7, 6, 5), (7, 6, 5)),
    ((7, 6, 5), (3, 2, 1)),
)

display = None
frame = [[(0, 0, 0) for _ in range(WIDTH)] for _ in range(HEIGHT)]
brightness = 3
# Kept for a later sound path.
volume = 3
pattern = 0
i2c = None
ina_ready = False
battery = {"percent": None, "charging": False, "at": None}
charge_origin = None
buttons = {}
settings_sel = 0
editing = False
draft = 3
hue = 0.0
hue_anim = None
hue_spin = False
hue_mark = 0
ripples = []
anim_ms = 0
anim_frac = 0.0
life_ms = 0
life_frac = 0.0
motion = 1.0
motion_from = 1.0
motion_to = 1.0
motion_at = None
breathe_from = 0
angle = 0.0
angle_target = 0.0
view_cos = 1.0
view_sin = 0.0
speed_index = SPEED_COUNT - 1
speed_hold = False
speed_mark = 0
selecting = False
click_quiet = 0
enc_a = None
enc_b = None
enc_state = 0
enc_counts = 0
enc_rest = 0
cells = [[0 for _ in range(WIDTH)] for _ in range(HEIGHT)]
cells_buf = [[0 for _ in range(WIDTH)] for _ in range(HEIGHT)]
trail = [[0 for _ in range(WIDTH)] for _ in range(HEIGHT)]


def sweep_unit(now):
    phase = now % (SWEEP_MS * 2)
    if phase <= SWEEP_MS:
        return phase / SWEEP_MS
    return (SWEEP_MS * 2 - phase) / SWEEP_MS


def sweep_float(now, low, high):
    return low + (high - low) * sweep_unit(now)


def sweep_pick(now, values):
    if len(values) == 1:
        return values[0]
    index = int(sweep_unit(now) * (len(values) - 1) + 0.5)
    if index >= len(values):
        index = len(values) - 1
    return values[index]


def wave_at(pos):
    pos = pos % WAVE_N
    base = int(pos)
    frac = pos - base
    first = WAVE[base & (WAVE_N - 1)]
    second = WAVE[(base + 1) & (WAVE_N - 1)]
    return first + (second - first) * frac


def scale(rgb, level):
    return (rgb[0] * level // 255, rgb[1] * level // 255, rgb[2] * level // 255)


def _byte(value):
    value = int(value + 0.5)
    if value < 0:
        return 0
    if value > 255:
        return 255
    return value


def hue_shift(rgb, turns):
    red, green, blue = rgb
    if red == green == blue:
        return rgb
    peak = red
    if green > peak:
        peak = green
    if blue > peak:
        peak = blue
    floor = red
    if green < floor:
        floor = green
    if blue < floor:
        floor = blue
    if peak == 0:
        return rgb
    span = peak - floor
    if peak == red:
        hue_value = ((green - blue) / span) % 6
    elif peak == green:
        hue_value = (blue - red) / span + 2
    else:
        hue_value = (red - green) / span + 4
    hue_value = (hue_value / 6 + turns) % 1
    sat = span / peak
    sector = hue_value * 6
    index = int(sector)
    frac = sector - index
    low = peak * (1 - sat)
    mid_down = peak * (1 - frac * sat)
    mid_up = peak * (1 - (1 - frac) * sat)
    index = index % 6
    if index == 0:
        red, green, blue = peak, mid_up, low
    elif index == 1:
        red, green, blue = mid_down, peak, low
    elif index == 2:
        red, green, blue = low, peak, mid_up
    elif index == 3:
        red, green, blue = low, mid_down, peak
    elif index == 4:
        red, green, blue = mid_up, low, peak
    else:
        red, green, blue = peak, low, mid_down
    return (_byte(red), _byte(green), _byte(blue))


def present(rgb, level):
    if hue != 0.0:
        rgb = hue_shift(rgb, hue)
    return scale(rgb, level)


def paint(col, row, rgb):
    x = col - 1
    y = HEIGHT - row
    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        frame[y][x] = rgb


def view_xy(x, y):
    dx = x - CX
    dy = y - CY
    return (
        CX + dx * view_cos + dy * view_sin,
        CY - dx * view_sin + dy * view_cos,
    )


def update_view():
    global view_cos, view_sin
    view_cos = math.cos(angle)
    view_sin = math.sin(angle)


def nudge_turn():
    global angle_target
    angle_target += TURN_STEP


def update_turn(dt):
    global angle, angle_target
    gap = angle_target - angle
    if gap <= 0.000001:
        angle = angle_target
    else:
        step = TURN_STEP * dt / TURN_MS
        if step >= gap:
            angle = angle_target
        else:
            angle += step
    cycle = math.pi * 2
    if angle >= cycle:
        angle -= cycle
        angle_target -= cycle


def begin_motion(now, target):
    global motion_from, motion_to, motion_at
    motion_from = motion
    motion_to = target
    motion_at = now


def update_motion(now):
    global motion, motion_at, breathe_from
    if motion_at is None:
        return
    portion = time.ticks_diff(now, motion_at) / PAUSE_MS
    if portion >= 1:
        motion = motion_to
        motion_at = None
        if motion == 0.0:
            breathe_from = now
        return
    slant = portion * portion * (3 - 2 * portion)
    motion = motion_from + (motion_to - motion_from) * slant


def toggle_pause(now):
    if motion_to > 0:
        begin_motion(now, 0.0)
    else:
        begin_motion(now, 1.0)


def breathe_factor(now):
    # Phase starts at the bright end, so the first stopped frame sits near full level.
    elapsed = time.ticks_diff(now, breathe_from)
    phase = (0.25 + elapsed / BREATHE_MS) % 1
    wave = 0.5 + 0.5 * math.sin(phase * 2 * math.pi)
    return BREATHE_LOW + (BREATHE_HIGH - BREATHE_LOW) * wave


def breathe_gain(now):
    # Lit pixels swing from 10% to 80% once motion has stopped.
    # While speed is returning, that swing eases back to full level.
    if motion >= 1:
        return 1.0
    if motion_to > 0:
        factor = breathe_factor(now)
        return factor + (1.0 - factor) * motion
    if motion > 0:
        return 1.0
    return breathe_factor(now)


def add_ripple(now):
    if len(ripples) >= RIPPLE_CAP:
        ripples.pop(0)
    ripples.append((random.randint(1, 14), random.randint(1, 5), now))


def active_ripples(now):
    found = []
    for origin_x, origin_y, born in ripples:
        age_ms = time.ticks_diff(now, born)
        if 0 <= age_ms < RIPPLE_LIFE_MS:
            span = age_ms / RIPPLE_LIFE_MS
            if span < 0.2:
                fade = span / 0.2
            elif span > 0.75:
                fade = (1 - span) / 0.25
            else:
                fade = 1.0
            found.append((origin_x, origin_y, age_ms / 1000, fade))
    return found


def ripple_gain(x, y, live):
    gain = 1.0
    for origin_x, origin_y, age, fade in live:
        dx = x - origin_x
        dy = y - origin_y
        dist = math.sqrt(dx * dx + dy * dy)
        delta = dist - age * RIPPLE_SPEED
        if delta < 0:
            delta = -delta
        if delta < RIPPLE_WIDTH:
            crest = 1 - delta / RIPPLE_WIDTH
        else:
            crest = 0
        sample = (0.25 + 0.75 * crest) * fade + (1 - fade)
        if sample < gain:
            gain = sample
    return gain


def prune_ripples(now):
    ripples[:] = [item for item in ripples if time.ticks_diff(now, item[2]) < RIPPLE_LIFE_MS]


def step_speed():
    global speed_index
    speed_index = (speed_index + 1) % SPEED_COUNT


def handle_speed(now, pressed, released, down):
    global speed_hold, speed_mark
    if pressed:
        speed_hold = False
    if released:
        if not speed_hold:
            step_speed()
        speed_hold = False
        return
    held = time.ticks_diff(now, buttons[picounicorn.BUTTON_X]["at"])
    if down and not speed_hold and held >= HOLD_MS:
        speed_hold = True
        step_speed()
        speed_mark = now
    elif speed_hold and down:
        elapsed = time.ticks_diff(now, speed_mark)
        hops = elapsed // SPEED_HOLD_STEP_MS
        if hops > 0:
            for _ in range(hops):
                step_speed()
            speed_mark = time.ticks_add(speed_mark, hops * SPEED_HOLD_STEP_MS)


def advance_clocks(dt, frozen):
    global anim_ms, anim_frac, life_ms, life_frac
    if frozen or dt <= 0:
        return
    rate = (speed_index + 1) / SPEED_COUNT
    if pattern < LIFE:
        anim_frac += dt * rate * motion
        whole = int(anim_frac)
        anim_frac -= whole
        anim_ms += whole
    elif pattern == LIFE:
        life_frac += dt * rate
        whole = int(life_frac)
        life_frac -= whole
        life_ms += whole


def take_life_steps():
    global life_ms
    while life_ms >= LIFE_STEP_MS:
        life_ms -= LIFE_STEP_MS
        step_life()


def seed_life():
    global life_ms, life_frac
    life_ms = 0
    life_frac = 0.0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            cells[y][x] = 1 if random.random() < LIFE_DENSITY else 0
            trail[y][x] = 0


def step_life():
    global cells, cells_buf
    for y in range(HEIGHT):
        row = cells[y]
        nxt = cells_buf[y]
        for x in range(WIDTH):
            total = 0
            for oy in (-1, 0, 1):
                seen = cells[(y + oy) % HEIGHT]
                for ox in (-1, 0, 1):
                    if ox == 0 and oy == 0:
                        continue
                    total += seen[(x + ox) % WIDTH]
            alive = row[x]
            nxt[x] = 1 if (total == 3 or (alive and total == 2)) else 0
            trail[y][x] = 1 if alive and not nxt[x] else 0
    cells, cells_buf = cells_buf, cells


def cell_at(sx, sy):
    ix = int(round(sx)) % WIDTH
    iy = int(round(sy)) % HEIGHT
    if cells[iy][ix]:
        return LIFE_RGB
    if trail[iy][ix]:
        return LIFE_TRAIL
    return (0, 0, 0)


def draw_field(sample):
    for y in range(HEIGHT):
        row = frame[y]
        for x in range(WIDTH):
            sx, sy = view_xy(x, y)
            row[x] = sample(sx, sy)


def xor_rgb(x, y, horizontal, shift):
    band = ((int(round(x)) * horizontal) ^ ((int(round(y)) + shift) * 5)) & 7
    return (band * 36, 40, 255 - band * 28)


def plasma_rgb(x, y, horizontal, phase):
    sample = wave_at(x * horizontal + phase) + wave_at(y * 20 + phase)
    level = int((sample + 510) / 4)
    if level < 0:
        level = 0
    elif level > 255:
        level = 255
    return (level, 80, 255 - level)


def draw_xor():
    horizontal = sweep_pick(anim_ms, (1, 3, 5, 7))
    shift = anim_ms // XOR_SHIFT_MS
    draw_field(lambda x, y: xor_rgb(x, y, horizontal, shift))


def draw_plasma():
    horizontal = sweep_float(anim_ms, 4, 14)
    phase = (anim_ms % (PLASMA_PHASE_MS * WAVE_N)) / PLASMA_PHASE_MS
    draw_field(lambda x, y: plasma_rgb(x, y, horizontal, phase))


def draw_life():
    for y in range(HEIGHT):
        row = frame[y]
        for x in range(WIDTH):
            sx, sy = view_xy(x, y)
            row[x] = cell_at(sx, sy)


def draw_bar(col, level, rgb):
    if level > 6:
        level = 6
    for step in range(1, level + 1):
        paint(col, step, rgb)


def blink_on(now):
    return (now // 500) % 2 == 0


def draw_digit(number, slot, rgb):
    columns, rows = slot
    glyph = DIGITS[number]
    for gy, bits in enumerate(glyph):
        col = columns[gy]
        for gx, bit in enumerate(bits):
            if bit == "1":
                paint(col, rows[gx], rgb)


def draw_clock():
    now = time.localtime()
    text = "{:02d}{:02d}".format(now[3], now[4])
    for index, char in enumerate(text):
        draw_digit(int(char), CLOCK_SLOTS[index], WHITE)


def charge_phase(now):
    global charge_origin
    percent = battery["percent"]
    full = percent is not None and percent >= 100
    if not battery["charging"] or full:
        charge_origin = None
        return None
    if charge_origin is None:
        charge_origin = now
    elapsed = time.ticks_diff(now, charge_origin)
    if elapsed >= CHARGE_CYCLE:
        charge_origin = time.ticks_add(charge_origin, (elapsed // CHARGE_CYCLE) * CHARGE_CYCLE)
        elapsed = time.ticks_diff(now, charge_origin)
    if elapsed < CHARGE_REST_MS:
        return None
    elapsed -= CHARGE_REST_MS
    if elapsed < CHARGE_FILL_MS:
        lit = (elapsed * 6 + 500) // 1000
        if lit > 6:
            lit = 6
        return ("fill", lit)
    elapsed -= CHARGE_FILL_MS
    if elapsed < CHARGE_FLASH_MS:
        return ("on", 6)
    return ("off", 0)


def draw_battery_column(now):
    phase = charge_phase(now)
    if phase is not None:
        kind, lit = phase
        if kind == "off":
            return
        draw_bar(1, lit, GREEN)
        return
    percent = battery["percent"]
    if percent is None:
        return
    if percent >= 100:
        draw_bar(1, 6, CYAN)
        return
    sixths = (percent * 6 + 50) // 100
    if percent < 10:
        if sixths < 1:
            sixths = 1
        if blink_on(now):
            draw_bar(1, sixths, RED)
        return
    if sixths >= 4:
        rgb = GREEN
    elif sixths >= 2:
        rgb = YELLOW
    elif sixths == 1:
        rgb = RED
    else:
        return
    draw_bar(1, sixths, rgb)


def setting_index(which):
    if editing and settings_sel == which:
        return draft
    if which == 0:
        return brightness
    return volume


def draw_setting_bar(col, which, rgb, now):
    selected = settings_sel == which
    if selected and not editing and not blink_on(now):
        return
    draw_bar(col, setting_index(which) + 1, rgb)


def draw_settings(now):
    draw_battery_column(now)
    draw_setting_bar(3, 0, WHITE, now)
    draw_setting_bar(5, 1, ORANGE, now)


def flush_art(now, live):
    level = LEVELS[brightness]
    pulse = breathe_gain(now)
    for y in range(HEIGHT):
        row = frame[y]
        for x in range(WIDTH):
            red, green, blue = present(row[x], level)
            if live or pulse < 0.999:
                gain = ripple_gain(x, y, live) * pulse
                red = _byte(red * gain)
                green = _byte(green * gain)
                blue = _byte(blue * gain)
            display.set_pixel(x, y, red, green, blue)
            row[x] = (0, 0, 0)


def flush_plain():
    # This page stays at full level. Art pages scale by LEVELS once Y
    # has stored the brightness index. Those low steps also sit close
    # together after the Unicorn gamma table. Leave this for a later pass.
    for y in range(HEIGHT):
        row = frame[y]
        for x in range(WIDTH):
            red, green, blue = row[x]
            display.set_pixel(x, y, red, green, blue)
            row[x] = (0, 0, 0)


def _write(reg, value):
    i2c.writeto_mem(INA_ADDR, reg, bytes(((value >> 8) & 0xFF, value & 0xFF)))


def _read(reg):
    data = i2c.readfrom_mem(INA_ADDR, reg, 2)
    return (data[0] << 8) | data[1]


def _signed(value):
    if value > 32767:
        return value - 65536
    return value


def read_pack():
    global ina_ready
    if i2c is None:
        return None, None
    try:
        if not ina_ready:
            found = i2c.scan()
            ina_ready = INA_ADDR in found
            if not ina_ready:
                return None, None
        _write(REG_CAL, INA_CAL)
        _write(REG_CONFIG, INA_CONFIG)
        time.sleep_ms(2)
        _write(REG_CAL, INA_CAL)
        bus = _read(REG_BUS)
        current = _signed(_read(REG_CURRENT))
    except OSError:
        ina_ready = False
        return None, None
    volts = (bus >> 3) * 0.004
    if volts <= 3.0:
        percent = 0
    elif volts >= 4.2:
        percent = 100
    else:
        percent = int((volts - 3.0) / 1.2 * 100 + 0.5)
    return percent, current


def refresh_battery(now):
    if battery["at"] is not None and time.ticks_diff(now, battery["at"]) < 2000:
        return
    percent, current = read_pack()
    battery["percent"] = percent
    battery["charging"] = current is not None and current <= CHARGE_MA
    battery["at"] = now


def load_settings():
    global brightness, volume
    try:
        with open(SETTINGS_PATH) as handle:
            text = handle.read()
    except OSError:
        return
    for line in text.splitlines():
        key, sep, value = line.partition("=")
        if sep != "=":
            continue
        try:
            level = int(value.strip())
        except ValueError:
            continue
        index = level - 1
        if index < 0 or index > 5:
            continue
        if key.strip() == "brightness":
            brightness = index
        elif key.strip() == "volume":
            volume = index


def save_settings():
    try:
        with open(SETTINGS_PATH, "w") as handle:
            handle.write("brightness={}\nvolume={}\n".format(brightness + 1, volume + 1))
    except OSError:
        return


def track_edge(state, down, now):
    pressed = down and not state["down"]
    released = state["down"] and not down
    state["down"] = down
    if pressed:
        state["at"] = now
    return pressed, released, down


def track(pin, now):
    return track_edge(buttons[pin], display.is_pressed(pin), now)


def begin_hue_tap(now):
    global hue_anim, hue_spin
    hue_spin = False
    hue_anim = (now, hue, hue + HUE_TAP)


def begin_hue_spin(now):
    global hue, hue_spin, hue_anim, hue_mark
    if hue_anim is not None:
        hue = hue_anim[2] % 1
        hue_anim = None
    hue_spin = True
    hue_mark = now


def update_hue(now):
    global hue, hue_anim, hue_mark
    if hue_spin:
        elapsed = time.ticks_diff(now, hue_mark) / 1000
        hue = (hue + elapsed * HUE_SPIN) % 1
        hue_mark = now
        return
    hue_mark = now
    if hue_anim is None:
        return
    start, origin, target = hue_anim
    portion = time.ticks_diff(now, start) / HUE_TAP_MS
    if portion >= 1:
        hue = target % 1
        hue_anim = None
        return
    hue = origin + (target - origin) * portion


def change_pattern(delta):
    global pattern, hue_spin, editing, speed_hold
    pattern = (pattern + delta) % SCREEN_COUNT
    hue_spin = False
    editing = False
    speed_hold = False
    ripples[:] = []


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


def turn_brightness(steps):
    global brightness, draft
    level = brightness + steps
    if level < 0:
        level = 0
    elif level > 5:
        level = 5
    if level == brightness:
        return
    brightness = level
    if editing and settings_sel == 0:
        draft = level
    save_settings()


def handle_encoder(now, click_pressed, steps):
    global selecting, click_quiet
    if click_pressed and time.ticks_diff(now, click_quiet) >= 0:
        selecting = not selecting
        click_quiet = time.ticks_add(now, CLICK_GAP_MS)
    if steps == 0:
        return
    if selecting:
        change_pattern(steps)
    elif pattern == SETTINGS:
        turn_brightness(steps)


def mark_select():
    if selecting:
        display.set_pixel(WIDTH - 1, 0, 255, 255, 255)


def handle_settings(b_pressed, x_pressed, y_pressed):
    # Y opens the selected value, B raises that open value, and Y stores it.
    global editing, draft, settings_sel, brightness, volume
    if x_pressed and not editing:
        settings_sel = (settings_sel + 1) % 2
    if y_pressed:
        if editing:
            if settings_sel == 0:
                brightness = draft
            else:
                volume = draft
            editing = False
            save_settings()
        else:
            editing = True
            draft = brightness if settings_sel == 0 else volume
    elif b_pressed and editing:
        draft = (draft + 1) % 6


def handle_hue(now, pressed, released, down):
    global hue_spin
    if pressed:
        begin_hue_tap(now)
    if released:
        hue_spin = False
    elif down and not hue_spin and time.ticks_diff(now, buttons["green"]["at"]) >= HOLD_MS:
        begin_hue_spin(now)


ART = (
    draw_xor,
    draw_plasma,
    draw_life,
)
LIFE = ART.index(draw_life)
SETTINGS = len(ART)
CLOCK = SETTINGS + 1
SCREEN_COUNT = CLOCK + 1


def main():
    global display, i2c, enc_a, enc_b, enc_state
    load_settings()
    seed_life()
    display = picounicorn.PicoUnicorn()
    for pin in (
        picounicorn.BUTTON_B,
        picounicorn.BUTTON_X,
        picounicorn.BUTTON_Y,
    ):
        buttons[pin] = {"down": False, "at": 0}
    buttons["green"] = {"down": False, "at": 0}
    buttons["red"] = {"down": False, "at": 0}
    buttons["click"] = {"down": False, "at": 0}
    green_pin = Pin(PIN_GREEN, Pin.IN, Pin.PULL_UP)
    red_pin = Pin(PIN_RED, Pin.IN, Pin.PULL_UP)
    click_pin = Pin(PIN_ENC_CLICK, Pin.IN, Pin.PULL_UP)
    enc_a = Pin(PIN_ENC_A, Pin.IN, Pin.PULL_UP)
    enc_b = Pin(PIN_ENC_B, Pin.IN, Pin.PULL_UP)
    enc_state = (enc_a.value() << 1) | enc_b.value()
    enc_a.irq(on_encoder, Pin.IRQ_RISING | Pin.IRQ_FALLING)
    enc_b.irq(on_encoder, Pin.IRQ_RISING | Pin.IRQ_FALLING)
    try:
        i2c = I2C(1, sda=Pin(6), scl=Pin(7), freq=100000)
    except OSError:
        i2c = None

    last = time.ticks_ms()
    while True:
        started = time.ticks_ms()
        dt = time.ticks_diff(started, last)
        last = started
        if dt < 0:
            dt = 0
        elif dt > DT_CAP_MS:
            dt = DT_CAP_MS
        refresh_battery(started)
        b_pressed, _, _ = track(picounicorn.BUTTON_B, started)
        x_pressed, x_released, x_down = track(picounicorn.BUTTON_X, started)
        y_pressed, _, _ = track(picounicorn.BUTTON_Y, started)
        g_pressed, g_released, g_down = track_edge(
            buttons["green"], green_pin.value() == 0, started
        )
        r_pressed, _, _ = track_edge(buttons["red"], red_pin.value() == 0, started)
        c_pressed, _, _ = track_edge(
            buttons["click"], click_pin.value() == 0, started
        )
        handle_encoder(started, c_pressed, take_steps())
        if pattern == SETTINGS:
            handle_settings(b_pressed, x_pressed, y_pressed)
        elif pattern < SETTINGS:
            handle_hue(started, g_pressed, g_released, g_down)
            if y_pressed:
                add_ripple(started)
            if b_pressed:
                nudge_turn()
            handle_speed(started, x_pressed, x_released, x_down)
            if r_pressed and pattern == LIFE:
                seed_life()
            elif r_pressed:
                toggle_pause(started)
        update_motion(started)
        update_turn(dt)
        update_view()
        live = active_ripples(started)
        advance_clocks(dt, bool(live))
        if pattern == LIFE and not live:
            take_life_steps()
        update_hue(started)
        if pattern == SETTINGS:
            draw_settings(started)
            flush_plain()
        elif pattern == CLOCK:
            draw_clock()
            flush_plain()
        else:
            ART[pattern]()
            flush_art(started, live)
        mark_select()
        prune_ripples(started)
        spent = time.ticks_diff(time.ticks_ms(), started)
        if spent < FRAME_MS:
            time.sleep_ms(FRAME_MS - spent)


main()
