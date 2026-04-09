# Asteroids - nRF54LM20-DK

Asteroids clone running at a nearly locked 30FPS on the nRF54LM20-DK with a 3.5" 480x320 TFT display (UEED035HV-RX40-L001, ST7365 controller).

- **CPU app** (`displaytest/`) runs game logic + rendering on the ARM Cortex-M33
- **FLPR app** (`displaydriver/`) runs a dedicated display driver on the RISC-V FLPR core
- 16-bit parallel (8080 MCU) interface, 30fps target, landscape orientation
- Set `DISPLAY_16BIT` in `displaydriver/src/st7365.h` to switch between 8/16-bit modes

## Display Pinout — 16-Bit Mode (default)

### nRF54LM20-DK to Display FPC (40-pin)

All display GPIO is on **Port 1**, accessible from the FLPR core.
Data lines on the lower P2 header, control lines on the upper P3 header.

#### 16-Bit Data Bus

| Display FPC Pin | Signal | nRF54LM20 GPIO | Notes |
|:---------------:|:------:|:--------------:|:------|
| 17 | DB0  | P1.00 | |
| 18 | DB1  | P1.01 | NFC1 — may need solder bridge R33→R3 |
| 19 | DB2  | P1.02 | NFC2 — may need solder bridge R34→R4 |
| 20 | DB3  | P1.03 | |
| 21 | DB4  | P1.04 | |
| 22 | DB5  | P1.05 | |
| 23 | DB6  | P1.06 | |
| 24 | DB7  | P1.07 | |
| 25 | DB8  | P1.10 | Skips P1.08-P1.09 (devkit buttons) |
| 26 | DB9  | P1.11 | |
| 27 | DB10 | P1.12 | |
| 28 | DB11 | P1.13 | |
| 29 | DB12 | P1.14 | |
| 30 | DB13 | P1.15 | |
| 31 | DB14 | P1.16 | |
| 32 | DB15 | P1.17 | |

#### Control Signals

| Display FPC Pin | Signal | nRF54LM20 GPIO |
|:---------------:|:------:|:--------------:|
| 9  | CS    | P1.23 |
| 10 | WR    | P1.24 |
| 11 | DC/RS | P1.29 |
| 12 | RD    | P1.30 |
| 15 | RESET | P1.31 |

#### Interface Mode Select

| Display FPC Pin | Signal | Wire To |
|:---------------:|:------:|:-------:|
| 38 | IM0 | GND |
| 39 | IM1 | VCC |
| 40 | IM2 | GND |

IM[2:0] = 010 = 8080 MCU 16-bit bus.

#### Power

| Display FPC Pin | Signal | Connection | Notes |
|:---------------:|:------:|:----------:|:------|
| 5, 16, 37 | GND | Ground | |
| 6 | IOVCC | VDD:nRF (1.8V) | I/O level, range 1.65-3.6V |
| 7 | VCI | 3.3V | Analog supply, needs 2.5-3.6V |
| 33 | LED-A | 3.3V via 10-22 ohm resistor | Backlight anode |
| 34, 35, 36 | LED-K1/K2/K3 | GND | Backlight cathode (3 strings) |

#### Unused Pins (leave unconnected)

| Display FPC Pin | Signal | Notes |
|:---------------:|:------:|:------|
| 1-4 | CTP_SDA/SCL/INT/RST | Touch panel (not used) |
| 8 | TE | Tearing effect output (not used) |
| 13-14 | NC | Not connected |

## 8-Bit Mode (legacy)

Set `DISPLAY_16BIT 0` in `st7365.h` and rewire:
- DB0-DB4 on P1.03-P1.07, DB5-DB7 on P1.10-P1.12
- CS=P1.13, WR=P1.14, DC=P1.15, RD=P1.23, RESET=P1.24
- IM[2:0] = 011 (IM0=VCC, IM1=VCC, IM2=GND)
- DB8-DB15 unconnected

## Game Controls

| Devkit Button | nRF54LM20 GPIO | Game Function |
|:-------------:|:--------------:|:-------------:|
| Button 0 | P1.26 | Thrust |
| Button 1 | P1.09 | Rotate Left |
| Button 2 | P1.08 | Rotate Right |
| Button 3 | P0.05 | Shoot / Start / Restart |

## Building and Flashing

```bash
cd displaytest
west build -p always -b nrf54lm20dk/nrf54lm20a/cpuapp --sysbuild
west flash
```

## Architecture

```
+------------------+       Shared SRAM (150KB)       +------------------+
|   CPU (ARM M33)  |    framebuffer + dirty bitmap   | FLPR (RISC-V)    |
|                  | <=============================> |                  |
|  Game Logic      |   Poll-based sync:              | ST7365 Init      |
|  Physics (FPU)   |   CPU: "frame ready"            | 16-bit Parallel  |
|  Collision (int) |   FLPR: "frame done"            | Dirty Row Stream |
|  Rendering       |                                 | Palette→RGB565   |
+------------------+                                 +------------------+
       |                                                     |
  [4 Buttons]                                     [21 GPIO on Port 1]
```

## Benchmark Mode

Set `BENCHMARK_MODE 1` in `game.h` to disable collisions, shooting, and death.
Fly through asteroids freely — isolates display throughput from game logic.

## Serial Console

UART20 (CPU) at 115200 baud. UART30 (FLPR) for FPS/frame counter.
