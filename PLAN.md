# Pico art piece

Two programs live here. `matrix/` is the xyloscope, the wall piece, C++ on a Pico 2W driving a 64 by 64 panel and the switchboard. `main.py` is the handheld Unicorn program. `bringup_unicorn_ups.py` is a one-shot check of that pack and the battery monitor. The rest of this file is the original Unicorn plan.

A handheld light that pours its pixels along gravity, reshapes that pattern from the four buttons, reacts to a tap, shows a coarse equalizer, and speaks in tones and grains. It can hold the last few seconds of that sound as a loop. The computer is a Raspberry Pi Pico 2W. Everything else hangs off the two header rows.

## Parts

| Part                            | Role                                                                              |
| ------------------------------- | --------------------------------------------------------------------------------- |
| Raspberry Pi Pico 2W            | Controller. Needs male stacking headers long enough to stand proud of both faces. |
| Pimoroni Pico Unicorn Pack      | 16×7 RGB matrix and buttons A, B, X, Y.                                           |
| Waveshare Pico-UPS-A            | Battery power and charge path. 14500 Li-ion cell is separate and required.        |
| Adafruit MPU-6050 STEMMA QT     | Accelerometer and gyro. Cable is separate.                                        |
| Adafruit MAX9814 microphone amp | Analog ear for the equalizer. Electret is already on the board.                   |
| MonkMakes Speaker for micro:bit | Small amplified speaker for tones.                                                |
| Adafruit 16 mm illuminated pushbuttons | Four momentary panel buttons. Each switch uses one spare pin. Each LED glows from 3V3 through a resistor. |

Leave the UPS I2C solder pads on the pair they ship with. Moving them onto GP20 and GP21 shorts the battery monitor into the Unicorn row drivers.

## Stack

Solder long male headers through the Pico so the pins pass through the board and remain reachable past one face. Seat the Unicorn on one face and the UPS on the other. Match the USB end to the silkscreen on each pack.

Both packs land on the same 40 pins. A signal used on the bottom is the same signal on the top. The IMU, microphone, and speaker attach to the pin tails that stick out past the upper header.

## Pins

| Function                     | Pins                                                                   |
| ---------------------------- | ---------------------------------------------------------------------- |
| Unicorn matrix and buttons   | GP8 through GP22, owned by the pack                                    |
| UPS battery monitor (INA219) | GP6 SDA, GP7 SCL. Leave it here.                                       |
| MPU-6050                     | GP4 SDA, GP5 SCL, 3V3, GND. Address `0x68`. Optional interrupt on GP0. |
| MAX9814                      | 3V3, GND, OUT to GP26                                                  |
| MonkMakes speaker            | 3V to Pico 3V3, GND, input to GP27                                     |
| Panel buttons                | GP1, GP2, GP3, GP28. One contact each, to ground.                      |

GP26 is the analog input. Do not put the speaker or another digital signal on it.

The MPU-6050, the UPS monitor, and any other I2C device must keep distinct addresses if they ever share a bus. As wired above, the IMU and the UPS are on separate pairs.

## Power

USB 5 V charges the UPS. The UPS feeds VSYS. The Unicorn draws the LED current from VSYS. The Pico's 3V3 pin feeds the IMU, the microphone, and the speaker.

A single 14500 cell cannot hold a full-white matrix and a loud speaker at once. Keep the matrix dim on battery. The speaker's amplifier peaks around 80 mA, which is acceptable beside a dim matrix and too much beside a bright one. A groove keeps the speaker running, so compose on USB and keep the matrix dim on the cell.

After inserting a new cell, press the UPS activate button so the protection circuit wakes.

## Firmware

Flash Pimoroni's Pico 2W MicroPython build from the `pimoroni-pico-rp2350` releases. The original Pico Unicorn uf2 is built for the RP2040 and will not run this board. That build supplies the Unicorn library, which refreshes the matrix in the background on a programmable I/O block.

The application loop then does four things:

1. Read the MPU-6050 at about 100 Hz.
2. Read the four buttons.
3. When the equalizer pattern is selected, sample GP26 at about 16 kHz in short blocks.
4. Write the pixel buffer, and emit a tone on GP27 when the current pattern asks for sound.

The fourth step is also where the granular synthesizer plays, once its place among the behaviours is chosen. Samples for the grains and the loops move by DMA. The application loop keeps reading sensors and buttons, and keeps choosing the picture.

## Behaviour

**Gravity flow.** The accelerometer is which way is down. The gyro keeps that direction steady while the piece is turning. A handful of sub-pixel particles on the 16×7 grid is enough to read as a smooth pour. Map particle position into the frame the Unicorn library already accepts.

**Patterns.** Buttons A, B, X, and Y select the pattern. First set: gravity flow, a still field, the equalizer, and a tone mode. Inside a pattern, the buttons modify the picture. Those modifications include warping, dithering, rotation, and colour shift, along with smaller settings such as speed or pitch range.

**Tap.** A tap is a short spike in acceleration magnitude above a threshold taken while the piece is still. Polling at 100 Hz is enough. Wiring the MPU-6050 interrupt to GP0 lets the spike arrive between polls.

**Equalizer.** The MAX9814 output is a signal of about 2 V peak-to-peak on a 1.25 V bias, which sits inside the Pico's analog range. Its automatic gain keeps a quiet room and a nearby speaker in a usable range, and it softens sudden claps. A loudness reading fills the 7 rows as a level meter. A 64-point transform of the same samples fills the 16 columns with coarse bands through a few kilohertz. That is the resolution this matrix can show.

**Sound.** The MonkMakes input is a switched amplifier, so the Pico produces the audio by toggling GP27. Pitch, duration, and a rough loudness follow the active pattern: tilt, tap, particle height, or the current equalizer band. The result is tones and buzzes. Sampled music needs a different amplifier and more current than this cell should supply.

**Grains.** A simple synthesizer adds random granular sound on that same GP27 output. A grain is a short burst. Its timing and pitch are partly random, and the character of the grains follows the buttons and the picture, either the change underway or the frame as it stands. A warp, a dither, a rotation, or a colour shift moves the grains with it, for example by smear, density, cycle, or brightness. The synthesizer may be its own behaviour, or it may run inside every behaviour. That choice stays open.

**Panel buttons.** Four 16 mm illuminated momentary buttons sit in the shell. Their contacts are record, play, effect, and tempo. The LEDs share 3V3 through a resistor of about 470 Ω to 1 kΩ, so they glow while the piece is on. Each body is about 18 mm across and 29 mm deep behind a 16 mm hole, and the shell grows at the end or the side where they mount.

**Loops.** While the grains are playing, every sample on its way to the speaker is also written into a five-second ring. A button copies that ring into a loop slot, and the loop can then be played and modulated. The microphone can feed the same ring when the loop should hold the room. At 8 kHz and 8 bits, five seconds is 40 KB. Four such loops are 160 KB, and about fifteen to twenty seconds of live audio fits in the Pico 2W's 520 KB with Wi-Fi left off. The modulations are bitcrush, sample-rate reduction, delay with feedback, stutter, a playback-rate ramp, a simple low-pass, tremolo, and a short multi-tap echo. The MonkMakes speaker plays these loops as crunchy PWM. A small amplifier with an audio input, fed by filtered PWM, is the optional part that lets a loop sound like the recording.

**Drums.** A later pattern is a 16-step grid on the Unicorn's 16 columns. A few rows are voices. A kick is a falling tone and a hat is a noise burst, so the drums use the speaker the way the tones already do. The Unicorn buttons move a cursor and toggle the step under it. Tempo is a number. Tap tempo is two gaps between presses.

## Bring-up order

1. Flash the Pico 2W on its own and confirm the Unicorn library draws one pixel.
2. Add the UPS, charge over USB, and read the battery monitor on GP6 and GP7 with the cell installed.
3. Add the MPU-6050 on GP4 and GP5. Confirm the gravity vector flips when the board is turned over, then add tap detection.
4. Draw the gravity flow. Only then attach meaning to the four buttons.
5. Add the MAX9814 on GP26. Start with the level meter, then the short transform.
6. Add the speaker on GP27. Tie pitch to one parameter that is already on screen, such as particle height or the strongest band.
7. With a pattern on screen and the speaker following it, let the buttons warp, dither, rotate, and shift colour. Add the granular synthesizer on GP27 so the grains follow those changes.
8. With the grains playing, keep a five-second ring of the samples going to the speaker. A panel button copies that ring into a loop, and the effect button modulates it.

Stop at the first step that misbehaves. A dark matrix with the UPS attached is a pin or power problem. A matrix with stuck rows after the IMU is attached usually means a signal landed on GP8 through GP22.

## Some inspo

https://github.com/collidingScopes/liquid-shape-distortions

