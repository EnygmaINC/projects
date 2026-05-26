<p align="center">
  <img width="500" alt="FlipperZero-LD2450App" src="https://github.com/user-attachments/assets/31d26510-aad2-40ec-878d-de48664a0ff9" />
</p>

# LD2450 Radar — Flipper Zero FAP

Real-time 2D mmWave radar visualizer for the **Flipper Zero** using the **HLK-LD2450** 24 GHz presence sensor. Tracks up to three simultaneous targets and plots them as numbered dots on a semicircular radar display rendered on the Flipper's 128×64 monochrome screen.

## Hardware

| Component | Notes |
|-----------|-------|
| Flipper Zero | Any firmware ≥ 0.80, API version 87.1 |
| [HLK-LD2450](https://www.hlktech.net/index.php?id=1157) | 24 GHz mmWave radar, up to 3 targets, ~10 Hz output, 256000 baud UART — [Datasheet / Manual](https://drive.google.com/drive/folders/1kTt0Z3hjKKrIF3OCIDGdwQ4KotDJ8SGA) |
| JST 1.25mm 4-pin to Dupont female pigtail | Usually already included w/ LD2450 - [AliExpress](https://www.aliexpress.us/item/3256808370751686.html?spm=a2g0o.productlist.main.8.9e408UeB8UeBq8&aem_p4p_detail=202605261617029945822959262240004108994&algo_pvid=314bf892-6390-46a5-9f62-5c9eac054fd5&algo_exp_id=314bf892-6390-46a5-9f62-5c9eac054fd5-7&pdp_ext_f=%7B%22order%22%3A%2245%22%2C%22eval%22%3A%221%22%2C%22fromPage%22%3A%22search%22%7D&pdp_npi=6%40dis%21USD%214.21%214.21%21%21%2128.44%2128.44%21%40210337c117798374224678164e903d%2112000045699242900%21sea%21US%214059689880%21X%211%210%21n_tag%3A-29919%3Bd%3A8b05804a%3Bm03_new_user%3A-29895&curPageLogUid=Fg2tk0bVXeUx&utparam-url=scene%3Asearch%7Cquery_from%3A%7Cx_object_id%3A1005008557066438%7C_p_origin_prod%3A&search_p4p_id=202605261617029945822959262240004108994_2) |
| Dupont Cables (Male-Male) | [Amazon Link](https://www.amazon.com/Elegoo-EL-CP-004-Multicolored-Breadboard-arduino/dp/B01EV70C78/ref=sr_1_1?crid=YGXL09WVEILA&dib=eyJ2IjoiMSJ9.SszVHKRXXxbEIG2ErBYriTYR5PGk9WL9Ph5J0Uu87uGxs-7UlU_CKqzBs0eoTT91zR1i3msv7tPUTy2ZcZlf-v7Eksej5wOFjv9k1ayokFXPCPd2u7r9_YI3lO_yAxKV42zUgCvO1fO7xuk5IOcEWmpz2j7-3wPFDbQ19on8THuJ4f0oC4xoM7yxOOD60V5RV1sm_aGnC9gMg-rx0_Kjm2VvGdtttLyU7-iUV-uzskI.9dXNAZ-fsQpDlI7OExaFcK_ZxglvdyARHdffWXstR1c&dib_tag=se&keywords=dupont+cables+kit&qid=1779836995&sprefix=dupont+cables%2Caps%2C359&sr=8-1) |


### Wiring (Flipper GPIO header)

| Flipper pin | LD2450 pin |
|-------------|------------|
| Pin 1 (5V)  | 5V         |
| Pin 11 (GND)| GND        |
| Pin 13 (TX) | RX         |
| Pin 14 (RX) | TX         |

> [!IMPORTANT]
> The Flipper's 5V GPIO pin is **disabled by default**. Before connecting the sensor, enable it:
> **Main Menu → GPIO → 5V on GPIO → Enabled**
> Without this the sensor receives no power and the app will show "No signal".

> Mount the sensor **horizontally** (landscape orientation). The X axis is lateral (left/right) and Y axis is forward depth. Rotating the sensor 90° will swap the axes.

## Features

- Semicircular radar display with 6 sectors and dotted divider lines
- 3 range rings — inner rings dotted, outer ring solid
- Up to 3 targets plotted as filled dots with numbered labels (1 / 2 / 3)
- Status bar: range, target count, and per-target X/Y position in decimeters
- Zoom control: 3 range levels (≈6 ft / 13 ft / 20 ft)
- No signal indicator if sensor goes quiet for > 2 seconds
- Pure integer math — no floating point, no `math.h`

## Installing

1. Copy `releases/ld2450_radar_v1.0.fap` to `/ext/apps/Tools/` on the Flipper SD card
2. Launch from **Apps → Tools → LD2450 Radar**

## Controls

| Button | Action |
|--------|--------|
| UP     | Zoom in (decrease max range) |
| DOWN   | Zoom out (increase max range) |
| BACK   | Exit app |

## Status Bar

```
20ft T:1   1:-3,14
```

- `20ft` — current max range
- `T:1` — number of active targets
- `1:-3,14` — target 1 at X = −3 dm (30 cm left), Y = 14 dm (1.4 m deep)
- X negative = sensor's left, X positive = sensor's right

## Building from Source

Requires the [Flipper Zero firmware tree](https://github.com/flipperdevices/flipperzero-firmware).

```powershell
# Copy source into firmware tree
Copy-Item src\ld2450_radar.c  flipperzero-firmware\applications_user\ld2450_radar\
Copy-Item src\application.fam flipperzero-firmware\applications_user\ld2450_radar\

# Build
cd flipperzero-firmware
.\fbt.cmd fap_ld2450_radar
```

Output: `build\f7-firmware-D\.extapps\ld2450_radar.fap`

## Protocol

The LD2450 sends 30-byte frames at ~10 Hz over 256000 baud 8N1 UART:

```
[AA FF 03 00] [Target1: 8 bytes] [Target2: 8 bytes] [Target3: 8 bytes] [55 CC]
```

Per-target coordinate encoding (per HLK spec, pages 15–16):

| MSB of raw uint16 | Meaning |
|---|---|
| 1 | Positive value: `raw & 0x7FFF` (mm) |
| 0 | Negative value: `-(raw)` (mm) |

Y always has MSB = 1 (always forward/positive). X can be positive (right) or negative (left).

## Releases

| Version | Notes |
|---------|-------|
| v1.0 | Initial release — semicircular radar UI, 6 sectors, 3-target tracking, coordinate readout |

## 3D Printed Enclosure

| Part | Link |
|------|------|
| Sensor box for HLK-LD2450 | [MakerWorld](https://makerworld.com/en/models/2106350-sensor-box-for-hlk-ld2450#profileId-2278280) |
| Small ball joint mount | [MakerWorld](https://makerworld.com/de/models/2104478-small-ball-joint#profileId-2276067) |
| Fixed .STL for sensor box back (when mounted facing "right", the most common way) | [Link to .STL](https://github.com/EnygmaINC/projects/blob/main/Flipper%20Zero%20LD2450%20Radar/Sensor%20Box%20-%20Back%20(To%20Right)%20LD2450.stl) |

The ball joint allows the sensor to be aimed and locked at any angle without tools.

## Related

- [M5Stack Tab5 LD2450 Radar](../M5Stack%20Tab5%20LD2450%20Radar/) — same sensor, richer display on ESP32 hardware

## License

MIT
