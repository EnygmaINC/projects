<p align="center">
  <img width="500" alt="FlipperZero-LD2450App" src="https://github.com/user-attachments/assets/31d26510-aad2-40ec-878d-de48664a0ff9" />
</p>

# LD2450 Radar — Flipper Zero FAP

Real-time 2D mmWave radar visualizer for the **Flipper Zero** using the **HLK-LD2450** 24 GHz presence sensor. Tracks up to three simultaneous targets and plots them as numbered dots on a semicircular radar display rendered on the Flipper's 128×64 monochrome screen.

## Hardware

| Component | Notes |
|-----------|-------|
| Flipper Zero | Any firmware ≥ 0.80, API version 87.1 |
| HLK-LD2450 | 24 GHz mmWave radar, up to 3 targets, ~10 Hz output, 256000 baud UART |

### Wiring (Flipper GPIO header)

| Flipper pin | LD2450 pin |
|-------------|------------|
| Pin 1 (5V)  | 5V         |
| Pin 11 (GND)| GND        |
| Pin 13 (TX) | RX         |
| Pin 14 (RX) | TX         |

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

## Related

- [M5Stack Tab5 LD2450 Radar](../M5Stack%20Tab5%20LD2450%20Radar/) — same sensor, richer display on ESP32 hardware

## License

MIT
