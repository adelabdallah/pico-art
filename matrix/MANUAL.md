# Xyloscope

The xyloscope is a Pico 2W, one or more 64 by 64 panels, five buttons, an encoder, a two-colour OLED, a tilt sensor, and a microphone. The coloured buttons read like a xylophone, and the panels are the picture.

This file follows the firmware in this directory. When a pin, a pattern, or a button action changes, update the matching section and the pin diagram.

## Parts

| Part | Role | Connection |
| --- | --- | --- |
| Raspberry Pi Pico 2W | Controller. The firmware is built for `pico2_w` and copied to the RP2350 drive. | USB for power and flashing. |
| Waveshare P2.5 64×64 HUB75 panel | The picture. Wired the way the Waveshare Pico example is wired. Four of these, powered as two pairs. | Colour on GP0 through GP5. Row address A through E on GP6 through GP10. Clock GP11, latch GP12, output enable GP13. Share ground with the Pico. Feed each pair 5 V from the supply below. |
| Bergen Industries PS915143 cord | Wall lead for the 5 V supply. 9 foot, 14 AWG, three wires. NEMA 5-15 plug and a pigtail. 15 A at 125 V AC, 1875 W. | Plug in the wall. The pigtail is the AC side of the supply, with the rocker in that feed. https://www.amazon.ca/dp/B07BQ8PY21 |
| Velainwom 5 V 30 A switching supply | The panels' 5 V supply. 150 W, AC 110/220 V in, DC 5 V out. | AC comes from the cord above, through the rocker. Four panels, powered as two pairs. Each pair's 5 V leads share one fork on +V, and that pair's grounds share one fork on −V. https://www.amazon.ca/dp/B07PRY73TN |
| DaierTek SPST rocker | On-off for the supply that feeds both panel pairs. Waterproof toggle with a red safety cover. 20 A at 125 V AC, 15 A at 250 V AC. Two pins, in the AC feed of the 5 V supply. | https://www.amazon.ca/dp/B07T6YNRZS |
| FainWan MT3608 step-up modules | 12 V for the button LEDs. A pack of five. Each takes 2 V to 24 V in and can raise it as high as 28 V, at up to 2 A. Set the output to 12 V from the panel 5 V supply. | https://www.amazon.ca/dp/B09LM83XBF |
| 128×64 SSD1306 OLED | Names the pattern and the open setting. The top 16 rows are the yellow band. The lower rows are blue. | I2C1, GP14 SDA, GP15 SCL, address `0x3C`. |
| MPU-6050 | Tilt. Galaxy tilts its disk with it. Solar looks around the orrery and around each world with it. | Same I2C bus as the OLED, address `0x68`. |
| Analogue microphone | Loudness for flow field, the equalizer, and the stars, and the beat for flow field. The module name is absent from the firmware. | GP27, ADC input 1. |
| Five momentary buttons | Red, orange, yellow, green, and blue. Each button also has an LED on the 12 V feed above. | Each switch common goes to its GPIO. Each normally open contact goes to ground. The pin pull-up makes a press read as low. On the blue button, ground is the yellow wire. If a button behaves as if it is being held, try yellow for ground. The LEDs take 12 V from an MT3608 set to 12 V. |
| BAOMAIN 5-pin button harness | One plug on each momentary button. The lead is 15 cm, and the ends are stripped. https://www.amazon.ca/dp/B09NN28Q1B | Red is the lamp positive, and black is the lamp negative. Green is the switch common and goes to that button's GPIO. Blue is the normally open contact and goes to ground. Yellow is the normally closed contact, closed to green while the button is up. On the blue button, ground is the yellow wire. If a button behaves as if it is being held, try yellow for ground. |
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
    B             GP7  |        |  GP26  green C, ADC0
    C             GP8  |        |  RUN
    D             GP9  |        |  GP22  blue C
    E             GP10 |        |  GND
                  GND  |        |  GP21  orange C
    CLK           GP11 |        |  GP20  yellow C
    LAT           GP12 |        |  GP19  red C
    OE            GP13 |        |  GND
                  GND  |        |  GP18  encoder click
    SDA OLED+MPU  GP14 |        |  GP17  encoder B
    SCL OLED+MPU  GP15 |        |  GP16  encoder A
```

The nRF24L01+ header, with the antenna up and the parts facing you. The square pad is ground. VCC takes 3.3 V from the Pico. IRQ stays open.

```text
                         antenna

                   crystal      chip

              VCC     CSN     MOSI     IRQ
              GND     CE      SCK      MISO
              square
```

A PiCowBell proto board's STEMMA port is tied to GP4 and GP5. Those pins are the panel's G2 and B2, so the MPU-6050 cannot use that port. Plug the sensor onto the OLED bus, GP14 and GP15, at address `0x68`. Leave the STEMMA port empty while the panels are wired, because the sensor pull-ups hold the lower half's green and blue.

Four panels are powered as two pairs. Each pair takes one +V and one −V from the Velainwom supply. That pair's 5 V leads are joined to one fork, and its grounds are joined to the other. Both pairs can be on together. The Pico shares any GND pin with the supply's −V. While the pairs are still on separate HUB75 chains, each pair has its own Pico 2W, and both share that ground. The OLED and the MPU-6050 share GP14 and GP15.

## Panels

The panel count is a stored setting. The choices are 1, 2, and 4. While the buttons are unwired, a new board, and any board whose saved settings are older than this setting, boots as 4. Changing it saves at once and takes effect on the next boot. The scan locks the chain length when it starts, so the picture stays wrong until that reboot. The OLED and the encoder still work while the picture is wrong, which is how the count gets changed.

One panel is 64 by 64. Two panels are 128 wide by 64 tall. From the front, the Pico's panel is the right one and the HUB75 cable runs left. Four panels are 128 by 128. From the front, the Pico's panel is the bottom right. The cable runs left, then up, then right. Every panel stays the same way up. Four panels use one brightness step per bit so the scan buffers fit. One and two panels keep the split steps.

```text
[ top left ] --> [ top right ]
      ^
[ bottom left ] <-- [ bottom right / Pico ]
```

A hold is about a third of a second.

Any button, the encoder click, and a turn of the encoder wake the panel and the OLED.

## What is remembered

The last flash sector stores the pattern, the panel brightness, the hue, the microphone sensitivity, the tilt sensitivity, the OLED contrast, both timers, the encoder sensitivity, the double-click window, and the panel count.

Pause, each pattern's turn, each pattern's sideways slide, each pattern's mirrors, and the pattern a double click leaves behind last until power-off. They clear on reboot. Hue is one of the stored settings, so it comes back.

The panel timer choices are 1, 2, 5, 10, 15, 30, and 60 minutes, then a forever entry. Forever is the default, shown as `~`. The OLED timer choices are the same minutes with no forever entry. The default is 2 minutes. The dimmest OLED contrast is 5 percent.

## Startup

Boot plays an intro, then fades in the saved pattern. With a blank memory that pattern is plasma.

A glass phrase fills over 3 seconds and sways a little. A coloured stroke sits on the pixels that touch the letters, and a white stroke sits one pixel further out. The liquid inside shifts through a rainbow, one turn about every 0.7 seconds. Each boot picks one phrase at random: BOOTING, LOADING, PAINTING, NOW IN 3D!, BEEP BOOP, or WOWZA. NOW IN 3D! is two lines, with 3D! larger and underneath. BEEP BOOP is two lines of the same size. A phrase shrinks until the whole word fits on the panel. The word then flickers, slowly and then quickly, and fades over 1 second. A starfield shows through that flicker. The stars speed up over 1 second, rush into the centre, and fade to black over 1 second.

XYLO slides in from the left. SCOPE grows out of the centre. The two words sit one above the other in the middle of the panel. Both have landed after about 1.2 seconds. The letters are 28 pixels tall on one panel and stay one pixel clear of the border. On four panels they keep that same share of the square. XYLO is blocky, with straight edges. SCOPE has rounded edges. Both words are the same height. XYLO is red, yellow, green, then blue. SCOPE is chrome. A rainbow of seven colours chases the border for 2 seconds. The border is one pixel wide, and both words fill while the rainbow runs. A small pop follows, and a gold shine sweeps the whole panel from the top left to the bottom right. The rainbow border then locks and holds. The letters fly toward the camera in about a third of a second, and the saved pattern fades in over 2 seconds.

Red skips the intro at any moment, including the fade. The saved pattern appears on the next frame, already running. After the intro, red pauses the picture again. The other buttons and the encoder wait until the intro ends. The OLED yellow band reads XYLOSCOPE for the whole intro.

## Shared controls

These apply on every pattern except where a later section names a different action.

| Control | Action |
| --- | --- |
| Encoder click | Waits for a second click. When the wait ends, the pattern list opens and the OLED flashes the current name. A turn moves through the names, and the next name shows at once. A click while the list is open selects the name under the flash. `INTRO` sits between settings and `RESET?`, with no number. A click there plays the intro, then the pattern that was showing returns. The list ends on `RESET?`. A click there asks `ARE YOU SURE?`. Another click clears every pattern's mirrors, slide, and turn, and clears the hue. A turn during the wait opens the list and takes the turn. |
| Encoder double click | Jumps to settings. Another double click returns to the pattern that was showing. That return lasts until power-off. On settings, with no pattern waiting, the double click stays on settings. |
| Encoder turn | Flings the picture sideways. The fling coasts and then stops. |
| Red | Eases the picture to a stop, or back to full speed, over about two seconds. A stopped picture pulses. |
| Orange | Adds a diagonal mirror to the current pattern. The sixth press clears that pattern's lines. A line runs near a corner-to-corner diagonal and misses the centre. The smaller side shows the other side reflected. Holding orange turns that new line about the centre until it is released. |
| Yellow | Adds a round mirror. The eighth press clears that pattern's circles. A new circle covers 1 to 10 percent of the panel. Its inside shows the pixels outside the rim, folded in. Holding yellow grows that new circle up to 40 percent of the panel, then back to empty, and repeats until it is released. |
| Green | Turns the picture 5 degrees clockwise. A hold spins it. |
| Blue | Shifts the hue by a fifth of a turn. A hold spins the hue. The shift lasts while that pattern is on screen. Changing pattern clears it, and a reboot starts with no shift. |

Mirrors, the turn, and the sideways slide belong to the pattern that was showing when they were made. Changing pattern restores that pattern's own set.

## Patterns

Plasma is 1. Settings, INTRO, and the reset prompts have no number. The yellow band shows the name, and the pattern number sits on the right of that band. The blue band draws slow waves that follow the mirrors, the turn, and a sideways slide. Flow field replaces those waves with the microphone state and an equalizer.

| No. | Name | Picture |
| --- | --- | --- |
| 1 | plasma | The handheld plasma, sampled between the original 16 by 7 pixels. Hue walks the colour wheel, and the view turns a quarter turn every few seconds. |
| 2 | warp | Soft blobs from value noise, sampled in a warped place. |
| 3 | feedback | Each frame samples the previous frame closer to the centre, turns it, and adds a soft spot. The picture smears into a tunnel. |
| 4 | metaballs | Moving balls added into one field. They merge where they meet. |
| 5 | life | Conway's Life on the whole panel. Colour is how long a cell has lived. A fresh board starts when the population stalls, or every 45 seconds. |
| 6 | flow field | Particles trace a slowly turning noise field. Colour follows the direction they travel. The encoder sets how much of the panel the streams cover. A beat bends the field, brightens the streams, shifts their hue, and hurries them, and adds extra streams in proportion to the encoder. Those extra streams collapse. The dim field keeps its light and turns with the bend. |
| 7 | voronoi cells | Fourteen drifting seeds and the regions they claim, shaded like stained glass. |
| 8 | loop | Langton's loop. A generation that changes nothing plants the loop at a new spot. Green, while held, runs the generations from full speed up to double and back down again. |
| 9 | ramadan | A crescent over domes, or lanterns over an arcade. The encoder swaps the two. Yellow walks the moon through its phases. |
| 10 | hearts | Red and pink hearts of every size rise through a rose haze. |
| 11 | autumn | Pumpkins, a hay bale, and a river at sunset, with a tree on the right. Water caustics move on the river. |
| 12 | solar | An orrery on the wide view, and a close portrait of each world. The stops run from the wide view through the sun, the planets, the belt, and Pluto, then back. |
| 13 | campfire | A campfire under a milky way, with a tent, a pine line, and sparks. |
| 14 | eq | Radial sound bars, a chrome boombox, or a graphic equalizer. All three follow the microphone. |
| 15 | lighthouse | A lighthouse on the rocks in a storm, its beam sweeping through the rain. |
| 16 | ferris wheel | A lit wheel turning at dusk, with gondolas that stay upright. |
| 17 | aurora | Green and violet curtains over a snowy pine shore and a frozen lake. |
| 18 | fireworks | Rockets climb over a night skyline and burst into coloured shells. Shells also launch on their own. |
| 19 | galaxy | A tilted spiral, warm at the core and blue along two arms. The solar system sits on the outer tip of one arm. |
| 20 | stars | A band of white, gold, and lilac stars, tiled until the panel is full. Music twinkles them faster. |
| 21 | civ | A terrain map solved by wave function collapse, then villages, sandy paths, and travellers. |
| 22 | boids | A meadow with a flock of pale birds. They steer together, split when a hawk dives through, and regroup. |
|  | settings | Three gears. Blue picks a stored value and the encoder raises or lowers it. |

Plasma, warp, feedback, metaballs, life, voronoi cells, hearts, and boids use the shared controls only.

Campfire, lighthouse, ferris wheel, aurora, and fireworks keep the 64 by 64 picture at the same size. A wider chain continues the scene on both sides. A taller chain adds sky above it.

## Controls by pattern

### Flow field

The encoder sets how much of the panel the streams cover. Clockwise fills more of the panel. The sparse end covers about 15 percent. The busy end covers about 75 percent. On four panels the busy end stops where the scratch memory runs out.

A beat adds extra streams in proportion to that setting. At the sparse end it adds none. At the busy end it adds about as many as are already on the panel. On four panels those extras appear only while room remains. The extra streams finish their path and collapse back to the level the encoder set.

Green starts and stops the microphone. Listening starts on. With the microphone stopped, the field holds its path and no extra streams appear. Yellow adds a round mirror. Orange still adds a diagonal mirror. Red and blue keep their shared actions.

A beat bends the field. The streams leave their usual paths, and the dim field behind them turns with that bend. The bend settles in about two seconds. The same beat brightens the streams a little, shifts their hue, and hurries them, and those three ease off in about a third of a second. The dim field keeps its light. The beat is the onset this pattern listens for. Microphone sensitivity is on the settings page. 50 percent is the tuned response, and flow field uses that ear.

### Loop

Green, while held, runs the generations from full speed up to double speed and back down to full speed. One trip takes about 4 seconds, and it repeats until green is released. Releasing green returns the loop to full speed. Red, orange, yellow, blue, and the encoder keep their shared actions.

### Ramadan

The crescent opens the pattern. One encoder step swaps to the lanterns, and the next step swaps back. The OLED name stays ramadan.

Yellow, while held, walks the moon through a full set of phases in about 3 seconds, and it keeps walking until yellow is released. The moon stays on the phase it reached.

Orange on the crescent, while held, runs the rooftop lights at double speed. Orange on the lanterns, while held, walks the glass hue. Releasing orange leaves that hue where it stopped. The lantern chains hang straight down, and the lanterns sway.

Green toggles a shower of the medium shells from fireworks, about 3 a second, with about 10 in the sky at once. A second press stops the shower. It keeps going when the scene swaps.

Two panels show the full width of either scene. One and four panels show the same square. On the crescent the moon sits in the upper right. On the lanterns the square is the middle of the scene. Red and blue keep their shared actions.

### Hearts

The hearts fill the panel. Red, orange, yellow, green, blue, and the encoder keep their shared actions.

### Autumn

Orange, while held, doubles how many leaves are falling. Yellow, while held, doubles the speed of the river. Green, while held, doubles how fast the tree canopy billows. Releasing a button returns that part to its own pace.

The river runs between the far trees and the soil, in the harvest gold. The caustic web brightens that gold as it drifts with the current.

Two panels show the full width. One and four panels show the square that holds the tree. Red, blue, and the encoder keep their shared actions.

### Solar

The encoder walks from the wide view through each world and back. A turn while the tour is running stops the tour.

Red pauses, the same as on the other patterns. While the pattern list is open, the buttons keep their shared actions.

Green flings the current world's spin, or the whole orrery on the wide view. On Earth and Pluto it also speeds the Moon and Charon. Holding green keeps that spin going.

Orange steps outward. On a world, it returns to the wide orrery. On the wide orrery, it pulls back over about two seconds until the system sits on the outer tip of one spiral arm, and the pattern becomes galaxy when the picture arrives. A second press during that flight turns it around and brings it back along the same path.

Yellow starts a tour. The wide view, the sun, each planet, the belt, and Pluto each hold for 5 seconds, and then the tour repeats. The view tumbles slowly, the way a gentle tilt of the panel looks around the orrery and around each world. The nod is the large swing, so the poles pass through the disc. The world stays in the centre. The Moon and Charon speed up and move with their planet. Another press stops the tour. While it runs, a play mark bobs in the bottom right of the OLED.

Blue keeps the shared hue shift.

Tipping the panel looks around the hoops, and around a close-up the same way. The world stays in the centre. Flat and face up is the resting view. During the tour the tumble replaces that look until the tour stops.

### Campfire

Orange throws sparks. Yellow flares the flame. Holding either keeps that going. Green sends one comet across the milky way. Holding green keeps a meteor shower along that band, and each meteor wanders a little on its own path. At most 20 are in the sky at once. Letting go thins new meteors out over 3 seconds, and each one already crossing finishes its path. The campfire picture stays upright. Red, blue, and the encoder keep their shared actions.

### Eq

Selecting the pattern opens the radial bars, including when eq is already the pattern on screen. The encoder swaps those bars with the boombox. Orange on the boombox opens the graphic equalizer, and orange there returns to the boombox. The encoder waits while the equalizer is open.

The boombox and the rack use shiny greys. Their bars run from cyan at the bottom to orange at the top, and the peak marks follow the bars. The radial picture keeps its rainbow. All three follow the microphone. On the radial bars, orange still adds a diagonal mirror. Red, yellow, green, and blue keep their shared actions.

### Lighthouse

The encoder changes how fast the beam moves. Clockwise is faster. The sea and the spray keep their own pace.

Yellow strikes lightning. Orange thickens the rain until 240 drops are falling. The calm rain is 80 drops. The rain rises for a second, holds for a second, and falls for a second. Holding orange keeps the rain thick. Red, green, and blue keep their shared actions.

### Ferris wheel

The encoder changes how fast the wheel moves. Clockwise is faster. The rim lights keep their own pace. The buttons keep their shared actions.

### Aurora

Green steps the curtain hue by a fifth of a turn. A hold spins that hue. The lake follows the curtains. Snow, pines, and stars keep their own colours. Red, orange, yellow, blue, and the encoder keep their shared actions.

Extra width continues the forest and the lake. Extra height adds sky above the picture.

### Stars

The star band tiles until the panel is full. Louder music twinkles the stars faster and lifts their brightness a little. Quiet music is the original twinkle. Red, orange, yellow, green, blue, and the encoder keep their shared actions.

### Civ

The map solves itself on the panel. Each choice narrows what the neighbouring cells can be. The tiles are deep water, shallow water, sand, grass, forest, mountain, and snow. Water grows from a few seeds into lakes and seas, and most of the map stays land. A shallow river sometimes runs between two of those bodies. Land of up to 3 pixels that sits in the water is cleared before the villages appear. Green pressed twice, inside the double-click window, solves a new map.

Villages appear on sand, grass, and forest. Each village has one roof colour, taken in order: red, orange, yellow, violet, cyan, then hot pink. The order repeats, so the colours stay even. It starts as one house and gains another about every 5 minutes, up to 15, scattered around its first house. On four panels a house is 3 by 3 and the roof is 4 pixels wide. On one or two panels a house is 2 by 2, and the eaves hang one pixel past the walls. The roof is a triangle in the village colour. Villages stay 32 pixels apart on four panels, and 16 pixels apart on one or two panels. They stop growing before they meet.

People leave home in that roof colour, visit up to four villages, favouring the larger ones, then return home and the trip ends. They stay at a settlement for up to 5 seconds before they leave, and a new traveller takes their place. On four panels a person is 2 pixels tall. On water they sail a brown boat, a row of three pixels and a row of two, with a four pixel triangular sail in the home colour. A cyan sail is brighter than its roof. They pause about once every two minutes for 2 seconds, and a campfire always appears beside them, on water, mountain, or snow the same as on land. On four panels that campfire is one pixel centered on three. Travellers walk through a campfire, across snow, and through each other. A route bows across the land and walks around water. About one leg in five is a sail, and that crossing runs straighter, water included. Repeated steps warm the ground toward sand. Villages let off a little grey and white smoke.

Red, orange, yellow, blue, and the encoder keep their shared actions.

### Boids

The meadow fills the panel. Pale birds steer together and leave a short cyan trail. An orange hawk dives through, the flock splits, and then it regroups. The flock starts again every minute. Red, orange, yellow, green, blue, and the encoder keep their shared actions.

### Fireworks

Orange launches a small shell. Yellow launches a large one. Shells still launch on their own. On four panels about a third of those shells burst in the sky above the buildings, on the top two panels.

Windows blink on their own. Each lit window keeps a random colour.

Green sends one shooting star across the sky. It leaves from the left or the right at random, arcs over the buildings, and spins about its centre as it goes. The head is about the size of a small shell. The head and the trail shift hue quickly and smoothly. Holding green sends more stars, some each way. Shells keep launching while the stars fly. Letting go stops new stars, and each star already in the sky finishes its crossing. Red, blue, and the encoder keep their shared actions.

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
| Panels | 1, 2, or 4 | 4 |

Panels is the chain length. 1 is one 64 by 64 panel. 2 is a 128 by 64 pair, Pico on the right, cable running left. 4 is a 128 by 128 square, Pico on the bottom right, cable left, then up, then right. The new count is stored when the encoder changes it, and the picture uses it on the next boot.

The sideways fling uses each detent on its own. Encoder sensitivity does not slow that fling.

## TODO

The sections above match the firmware on one Pico. This section is the plan. Remove it when the plan is in the firmware, and keep the control panel layout in the manual.

One Pico owns the panels and the controls, and the only free GPIO is GP28. The destination is two Pico 2Ws. The IO pico takes the console. The display pico takes the panels, runs the patterns, and draws the picture. These phases use one display pico for the whole panel chain. The pin section also describes a temporary pair of boards, one per HUB75 chain, while those chains are still apart.

### Control panel

The console is two rows, as you face it.

```text
[ left OLED ] [ right OLED ] [ left encoder ] [ right encoder ]

[ red ] [ orange ] [ yellow ] [ green ] [ blue ]
```

With one Pico doing every job, the left OLED and the left encoder are the wired parts. The left OLED is the 128×64 SSD1306 on I2C1 at `0x3C`. The silkscreen on that module reads `0x78`, the 8-bit form of `0x3C`. The left encoder is the quadrature encoder on GP16, GP17, and GP18. The right OLED and the right encoder are part of this plan. Both OLEDs share the I2C bus. The right OLED's address jumper now reads `0x7A`. That is the 8-bit form of `0x3D`, and the firmware addresses it as `0x3D`.

### Power

Each Pico has its own supply. The display pico runs from a wall supply of its own. The panels stay on the Velainwom 5 V supply described under Parts. A Waveshare Pico-UPS-A is under consideration for the IO pico. That board takes a separate 14500 cell, and it reports the battery over I2C. https://www.waveshare.com/wiki/Pico-UPS-A

### Phase 1

The two Pico 2Ws sit side by side. A plug that comes apart joins them, and the plug includes a shared ground.

The IO pico carries the left OLED, the right OLED, the left encoder, the right encoder, the five buttons, and the MPU-6050. Both OLEDs and the MPU-6050 share its I2C bus. The MPU-6050 stays at `0x68`. The right OLED and the right encoder are fitted here and take their jobs in phase 2. The display pico is wired to the panels, runs the patterns, and draws the picture. It carries the microphone.

The IO pico sends each control as it happens, including a hold and a turn in progress, and it sends the tilt. The display pico sends the pattern, the open setting, and what the OLEDs should draw. The IO pico draws that on the two displays.

The actions in Shared controls and Controls by pattern stay as they are through this phase. The pin diagram above stays the one-Pico map until the split is built.

### Phase 2

The right encoder takes the encoder actions from Shared controls and Controls by pattern. A turn flings the picture, or runs that pattern's encoder action. A click opens the pattern list. A double click opens settings. The list still ends on `RESET?`, and the confirm there stays as it is. On settings, the right encoder raises or lowers the open value, and the blue button moves to the next value.

The left encoder's turn is navigation. One step selects the neighbouring stop. Encoder sensitivity groups detents into one step, as it does on the list. The stops follow the pattern list and wrap. They are the patterns, settings, the intro, and `RESET?`. Landing on a pattern or on settings selects it. Landing on the intro or on `RESET?` rests there, and the picture keeps running.

The left encoder's click launches the intro when the intro is the resting stop. On `RESET?`, the first click asks `ARE YOU SURE?`, and the next click clears every pattern's mirrors, slide, and turn, and clears the hue. A turn leaves `ARE YOU SURE?` and selects the neighbouring stop. On a pattern or on settings, the click wakes. Further jobs for that click can be added later.

The left OLED names the pattern, the open setting, or the stop the left encoder is resting on.

The right OLED draws a miniature of the control panel for the pattern on screen. The miniature uses the two rows above. A small icon sits over each control and shows what that control does on this pattern. The yellow band names the action just used. Rotate, translate, mirror, circular mirror, launch small firework, and launch large firework are examples, and every pattern has a name for each of its actions. The name stays until another control is used. The OLED timer blanks the right OLED, and the name is there again when it wakes.

On settings, the miniature shows the right encoder changing the open value and the blue button moving to the next value.

The right OLED uses the same OLED timer as the left OLED. A button, a click on either encoder, or a turn of either encoder wakes the panels and both OLEDs.

### Phase 3

Each Pico gets an nRF24L01+. The header is the one drawn under Pins. The plug comes off, and the radios carry the same controls, the tilt, and the same OLED updates. Everything phase 2 adds, and everything the sections above this TODO describe, still works over the radios. The delay stays low enough that a turn, a press, and a hold still feel attached to the picture.

### Upgrades

A Pimoroni Pico LiPo 2 can take the place of a Pico 2W. The header matches. The extra memory adds a lot of room for the patterns. https://www.pishop.ca/product/pimoroni-pico-lipo-2/
