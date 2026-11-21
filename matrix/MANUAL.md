# Matrix manual

A Pico 2W drives a 64 by 64 panel, five buttons, an encoder, a two-colour OLED, a tilt sensor, and a microphone.

This file follows the firmware in this directory. When a pin, a pattern, or a button action changes, update the matching section and the pin diagram.

## Parts

| Part | Role | Connection |
| --- | --- | --- |
| Raspberry Pi Pico 2W | Controller. The firmware is built for `pico2_w` and copied to the RP2350 drive. | USB for power and flashing. |
| Waveshare P2.5 64×64 HUB75 panel | The picture. Wired the way the Waveshare Pico example is wired. | Colour on GP0 through GP5. Row address A through E on GP6 through GP10. Clock GP11, latch GP12, output enable GP13. Share ground with the Pico. Feed the panel 5 V from the supply below. |
| Bergen Industries PS915143 cord | Wall lead for the 5 V supply. 9 foot, 14 AWG, three wires. NEMA 5-15 plug and a pigtail. 15 A at 125 V AC, 1875 W. | Plug in the wall. The pigtail is the AC side of the supply, with the rocker in that feed. https://www.amazon.ca/dp/B07BQ8PY21 |
| Velainwom 5 V 30 A switching supply | The panel's 5 V supply. 150 W, AC 110/220 V in, DC 5 V out. | AC comes from the cord above, through the rocker. +V to the panel 5 V. −V to the shared ground. https://www.amazon.ca/dp/B07PRY73TN |
| DaierTek SPST rocker | On-off for the whole setup, including a later build of up to four panels. Waterproof toggle with a red safety cover. 20 A at 125 V AC, 15 A at 250 V AC. Two pins, in the AC feed of the 5 V supply. | https://www.amazon.ca/dp/B07T6YNRZS |
| FainWan MT3608 step-up modules | 12 V for the button LEDs. A pack of five. Each takes 2 V to 24 V in and can raise it as high as 28 V, at up to 2 A. Set the output to 12 V from the panel 5 V supply. | https://www.amazon.ca/dp/B09LM83XBF |
| 128×64 SSD1306 OLED | Names the pattern and the open setting. The top 16 rows are the yellow band. The lower rows are blue. | I2C1, GP14 SDA, GP15 SCL, address `0x3C`. |
| MPU-6050 | Tilt. Liquid pours with it. Galaxy tilts its disk with it. Solar looks around the orrery with it. | Same I2C bus as the OLED, address `0x68`. |
| Analogue microphone | Loudness and pitch for liquid. The module name is absent from the firmware. | GP27, ADC input 1. |
| Five momentary buttons | Red, orange, yellow, green, and blue. Each button also has an LED on the 12 V feed above. | Each switch common goes to its GPIO. Each normally open contact goes to ground. The pin pull-up makes a press read as low. The LEDs take 12 V from an MT3608 set to 12 V. |
| Quadrature encoder with a click | Pattern list, sideways fling, and the controls named per pattern below. | A is GP16, B is GP17, click is GP18. Each goes to ground. Clockwise is A changing before B. |

## Pins

Still free on the 40-pin header: GP28, which is also ADC2. 3V3, GND, VSYS, VBUS, ADC_VREF, 3V3_EN, and RUN are still free for power and reset.

GP23 turns the radio on, GP24 is its data and wake line, GP25 is its chip select, and GP29 is its clock and the VSYS sense pin. Those four stay on the Pico 2W module. The debug pads, SWCLK and SWDIO, are free for a debugger.

USB is at the top. The left column runs down the left header, and the right column runs down the right header.

```text
                         USB
    R1            GP0  |        |  VBUS
    G1            GP1  |        |  VSYS
                  GND  |        |  GND
    B1            GP2  |        |  3V3_EN
    R2            GP3  |        |  3V3
    G2            GP4  |  Pico  |  ADC_VREF
    B2            GP5  |   2W   |  GP28  free, ADC2
                  GND  |        |  GND
    A             GP6  |        |  GP27  microphone, ADC1
    B             GP7  |        |  GP26  green, ADC0
    C             GP8  |        |  RUN
    D             GP9  |        |  GP22  blue
    E             GP10 |        |  GND
                  GND  |        |  GP21  orange
    CLK           GP11 |        |  GP20  yellow
    LAT           GP12 |        |  GP19  red
    OE            GP13 |        |  GND
                  GND  |        |  GP18  encoder click
    SDA OLED+MPU  GP14 |        |  GP17  encoder B
    SCL OLED+MPU  GP15 |        |  GP16  encoder A
```

The panel shares any GND pin. Its 5 V comes from the Velainwom supply. The OLED and the MPU-6050 share GP14 and GP15.

A hold is about a third of a second.

Any button, the encoder click, and a turn of the encoder wake the panel and the OLED.

## What is remembered

The last flash sector stores the pattern, the panel brightness, the hue, the microphone sensitivity, the tilt sensitivity, the OLED contrast, both timers, the encoder sensitivity, and the double-click window.

Pause, each pattern's turn, each pattern's sideways slide, each pattern's mirrors, and the pattern a double click leaves behind last until power-off. They clear on reboot. Hue is one of the stored settings, so it comes back.

The panel timer choices are 1, 2, 5, 10, 15, 30, and 60 minutes, then a forever entry. Forever is the default, shown as `~`. The OLED timer choices are the same minutes with no forever entry. The default is 2 minutes. The dimmest OLED contrast is 5 percent.

## Shared controls

These apply on every pattern except where a later section names a different action.

| Control | Action |
| --- | --- |
| Encoder click | Waits for a second click. When the wait ends, the pattern list opens and the OLED flashes the current name. A turn moves through the names, and the next name shows at once. A click while the list is open selects the name under the flash. The list ends on `RESET?`. A click there asks `ARE YOU SURE?`. Another click clears every pattern's mirrors, slide, and turn, and clears the hue. A turn during the wait opens the list and takes the turn. |
| Encoder double click | Jumps to settings. Another double click returns to the pattern that was showing. That return lasts until power-off. On settings, with no pattern waiting, the double click stays on settings. |
| Encoder turn | Flings the picture sideways. The fling coasts and then stops. |
| Red | Eases the picture to a stop, or back to full speed, over about two seconds. A stopped picture pulses. |
| Orange | Adds a diagonal mirror to the current pattern. The sixth press clears that pattern's lines. A line runs near a corner-to-corner diagonal and misses the centre. The smaller side shows the other side reflected. Holding orange turns that new line about the centre until it is released. |
| Yellow | Adds a round mirror. The eighth press clears that pattern's circles. A new circle covers 1 to 10 percent of the panel. Its inside shows the pixels outside the rim, folded in. Holding yellow grows that new circle up to 40 percent of the panel, then back to empty, and repeats until it is released. |
| Green | Turns the picture 5 degrees clockwise. A hold spins it. |
| Blue | Shifts the hue by a fifth of a turn. A hold spins the hue. Releasing the hold stores the hue. |

Mirrors, the turn, and the sideways slide belong to the pattern that was showing when they were made. Changing pattern restores that pattern's own set.

## Patterns

Plasma is 1. Settings and the reset prompts have no number. The yellow band shows the name, and the pattern number sits on the right of that band. The blue band draws slow waves that follow the mirrors, the turn, and a sideways slide. Liquid replaces those waves with the microphone state and an equalizer.

| No. | Name | Picture |
| --- | --- | --- |
| 1 | plasma | The handheld plasma, sampled between the original 16 by 7 pixels. Hue walks the colour wheel, and the view turns a quarter turn every few seconds. |
| 2 | warp | Soft blobs from value noise, sampled in a warped place. |
| 3 | feedback | Each frame samples the previous frame closer to the centre, turns it, and adds a soft spot. The picture smears into a tunnel. |
| 4 | metaballs | Moving balls added into one field. They merge where they meet. |
| 5 | reaction | Gray-Scott reaction-diffusion. Two chemicals grow into coral, spots, and worms. A dead field is seeded again. |
| 6 | life | Conway's Life on the whole panel. Colour is how long a cell has lived. A fresh board starts when the population stalls, or every 45 seconds. |
| 7 | flow field | Particles trace a slowly turning noise field. Colour follows the direction they travel. |
| 8 | voronoi cells | Fourteen drifting seeds and the regions they claim, shaded like stained glass. |
| 9 | loop | Langton's loop. A generation that changes nothing plants the loop at a new spot. |
| 10 | liquid | A square cup seen from above. Tilt pours a pool against the low walls. Sound spreads the pool, tints it, and places a glint. |
| 11 | pipes | Perspective pipes in a box. Each pipe is a shaded cylinder. The box clears when it fills. |
| 12 | reef | Coral, rock, and open water, with fish and larger animals. Sunbeams stop when they meet rock. |
| 13 | solar | An orrery on the wide view, and a close portrait of each world. The stops run from the wide view through the sun, the planets, the belt, and Pluto, then back. |
| 14 | campfire | A campfire under a milky way, with a tent, a pine line, and sparks. |
| 15 | skyline | A neon harbour wider than the panel. The view is a 64 by 64 window on a 192 by 108 picture, and it stops at the edges. |
| 16 | lighthouse | A lighthouse on the rocks in a storm, its beam sweeping through the rain. |
| 17 | ferris wheel | A lit wheel turning at dusk, with gondolas that stay upright. |
| 18 | aurora | Three curtains, green, teal, and rose, swaying over a dark hill. |
| 19 | fireworks | Rockets climb over a night skyline and burst into coloured shells. Shells also launch on their own. |
| 20 | galaxy | A tilted spiral, warm at the core and blue along two arms. The solar system sits on the outer tip of one arm. |
|  | settings | Three gears. Blue picks a stored value and the encoder raises or lowers it. |

Plasma, warp, feedback, metaballs, reaction, life, flow field, voronoi cells, loop, pipes, reef, and aurora use the shared controls only.

## Controls by pattern

### Liquid

Yellow starts and stops the microphone. Listening starts on. Orange still adds a diagonal mirror. The other shared controls stay.

Tilt pours the pool. Flat and face up holds the pool in the middle. Microphone sensitivity and tilt sensitivity are on the settings page. 50 percent is the response tuned for this pattern.

### Solar

The encoder walks from the wide view through each world and back. A turn while the tour is running stops the tour.

Red pauses, the same as on the other patterns. While the pattern list is open, the buttons keep their shared actions.

Green flings the current world's spin, or the whole orrery on the wide view. Holding green keeps that spin going.

Orange steps outward. On a world, it returns to the wide orrery. On the wide orrery, it pulls back over about two seconds until the system sits on the outer tip of one spiral arm, and the pattern becomes galaxy when the picture arrives. A second press during that flight turns it around and brings it back along the same path.

Yellow starts a tour. The wide view, the sun, each planet, the belt, and Pluto each hold for 5 seconds, and then the tour repeats. The orrery and the worlds spin as if green is held, and pitch, yaw, and roll tumble together. Another press stops the tour. While it runs, a play mark bobs in the bottom right of the OLED.

Blue keeps the shared hue shift.

On the wide view, tipping the panel looks around the hoops. Flat and face up is the resting view. During the tour the tumble replaces that look until the tour stops.

### Campfire

Orange throws sparks. Yellow flares the flame. Holding either keeps that going. Green sends one comet across the milky way. Holding green keeps a meteor shower along that band, and each meteor wanders a little on its own path. At most 20 are in the sky at once. Letting go thins new meteors out over 3 seconds, and each one already crossing finishes its path. The campfire picture stays upright. Red, blue, and the encoder keep their shared actions.

### Skyline

Orange moves the view up. Yellow moves it down. A press steps a short way, and a hold keeps stepping. The encoder moves the view left and right. The view stops at the edge of the harbour. Red, green, and blue keep their shared actions.

### Lighthouse

The encoder changes how fast the beam moves. Clockwise is faster. The sea and the spray keep their own pace.

Yellow strikes lightning. Orange thickens the rain. The rain rises for a second, holds for a second, and falls for a second. Holding orange keeps the rain thick. Red, green, and blue keep their shared actions.

### Ferris wheel

The encoder changes how fast the wheel moves. Clockwise is faster. The rim lights keep their own pace. The buttons keep their shared actions.

### Fireworks

Orange launches a small shell. Yellow launches a large one. Red, green, blue, and the encoder keep their shared actions.

### Galaxy

The encoder changes how fast the disk moves. Clockwise is faster. The stars keep their own pace.

Orange flies in along the arm to the solar system. The pattern becomes solar, on the wide orrery, when the picture arrives. A second press during that flight turns it around and brings it back along the same path.

The solar system sits on the outer tip of one arm and turns with the disk.

Red, yellow, green, and blue keep their shared actions.

Tipping the panel tilts the disk. Flat and face up is the resting oval. A steeper tip draws it thinner.

### Settings

Blue moves to the next value. The encoder raises or lowers the open value. A double click returns to the pattern that was showing when a double click opened settings. With no pattern waiting, it stays on settings. Orange and yellow stay quiet. Red still pauses. Green still turns the picture.

The values, in order:

| Setting | Range | Default |
| --- | --- | --- |
| Panel brightness | 0 to 100 percent, in steps of 5 | 55 percent |
| OLED contrast | 5 to 100 percent, in steps of 5 | 100 percent |
| Microphone sensitivity | 0 to 100 percent, in steps of 5 | 50 percent |
| Tilt sensitivity | 0 to 100 percent, in steps of 5 | 50 percent |
| Encoder sensitivity | 1, 2, 4, or 8 detents per step on the list, the settings, and the pattern-specific encoder actions | 1 |
| Double click | 200 to 800 milliseconds, in steps of 50 | 400 milliseconds |
| Panel timer | 1, 2, 5, 10, 15, 30, 60 minutes, or forever | Forever |
| OLED timer | 1, 2, 5, 10, 15, 30, or 60 minutes | 2 minutes |

The sideways fling uses each detent on its own. Encoder sensitivity does not slow that fling.
