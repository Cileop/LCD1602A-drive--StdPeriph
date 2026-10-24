# LCD1602A I²C Driver for STM32F103C8T6

<p align="center">
  <a href="README.md"><img src="https://img.shields.io/badge/Language-English-2f81f7?style=flat-square" alt="English"></a>
  <a href="README.zh-CN.md"><img src="https://img.shields.io/badge/Language-%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-d73a4a?style=flat-square" alt="简体中文"></a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-STM32F103C8T6-007ec6?style=flat-square" alt="MCU">
  <img src="https://img.shields.io/badge/Library-StdPeriph-8e44ad?style=flat-square" alt="StdPeriph">
  <img src="https://img.shields.io/badge/Bus-I2C%20%28PCF8574%29-2ea44f?style=flat-square" alt="I2C">
  <img src="https://img.shields.io/badge/Build-Keil%20MDK%205-2ea44f?style=flat-square" alt="Keil MDK 5">
</p>

> 🌐 **Language:** **English** · [简体中文](README.zh-CN.md)

A compact, well-behaved driver for the **LCD1602A** character LCD driven through a
**PCF8574 I²C backpack** — 4-bit data bus over a software I²C bus on `PB6`/`PB7`,
written against the **STM32 Standard Peripheral Library (StdPeriph)**.

> **Background.** This repository started as a help request: the display showed
> garbled characters and sometimes lit up, sometimes did not. The problem turned out
> to be **software, not hardware** — the original driver never sent the `0x2` nibble
> that switches the HD44780 to 4-bit mode, and its busy-flag routine corrupted the
> I²C bus. Both are fixed and verified with a host-side simulation (no board needed).
>
> Detailed post-mortem (Chinese): [修复说明.md](修复说明.md) ·
> Datasheet evidence: [analysis/datasheet-findings.md](analysis/datasheet-findings.md)

| | Original driver | This driver |
|---|---|---|
| Bus mode after init | 8-bit (wrong) | **4-bit** |
| `0x28` / `0x0C` / `0x01` / `0x06` received | 0 / 0 / 0 / 0 | **all, in order** |
| DDRAM row 1 after `ShowLCDChars(1, "Password:")` | garbage bytes | **`Password:`** |
| I²C protocol violations | 56 | **0** |
| Simulation result | FAIL | **PASS** |

---

## Table of Contents

- [1. Features](#1-features)
- [2. Hardware](#2-hardware)
  - [2.1 Bill of materials](#21-bill-of-materials)
  - [2.2 Backpack and LCD pin mapping](#22-backpack-and-lcd-pin-mapping)
  - [2.3 Wiring to the MCU](#23-wiring-to-the-mcu)
  - [2.4 Power, pull-ups and level shifting](#24-power-pull-ups-and-level-shifting)
  - [2.5 Block diagram](#25-block-diagram)
- [3. Getting Started](#3-getting-started)
- [4. Repository Layout](#4-repository-layout)
- [5. API Reference](#5-api-reference)
- [6. How It Works](#6-how-it-works)
- [7. Troubleshooting](#7-troubleshooting)
  - [7.1 Garbled characters / intermittent display](#71-garbled-characters--intermittent-display)
  - [7.2 Faint characters / low contrast](#72-faint-characters--low-contrast)
  - [7.3 Nothing on screen / no ACK](#73-nothing-on-screen--no-ack)
  - [7.4 Backlight](#74-backlight)
  - [7.5 Keypad conflict on PB6](#75-keypad-conflict-on-pb6)
  - [7.6 Which I²C address?](#76-which-i2c-address)
- [8. Hardware-free Verification](#8-hardware-free-verification)
- [9. Changelog](#9-changelog)
- [10. References](#10-references)
- [Appendix A — Root causes of the original driver](#appendix-a--root-causes-of-the-original-driver)
- [Acknowledgements](#acknowledgements)
- [License](#license)

---

## 1. Features

| Feature | Description |
|---|---|
| **4-bit bus** | The backpack only exposes `D4`–`D7`; each byte is sent as two nibbles, each with its own `E` pulse |
| **Correct reset sequence** | `0x3, 0x3, 0x3 → 0x2 → 0x28 → 0x08 → 0x01 → 0x06 → 0x0C`, with a 50 ms power-on wait |
| **Automatic address detection** | Probes `0x27` (PCF8574) and `0x3F` (PCF8574A) |
| **ACK checking** | Every write checks the slave ACK; failures are reported instead of silently ignored |
| **Bus recovery** | Emits 9 SCL pulses on start-up in case a slave is stuck holding SDA low |
| **No BUSY polling** | Uses datasheet delays (39 µs / 1.53 ms) instead — the bus is write-only, which is simpler and safer |
| **Cursor helpers** | `row = 1..2`, `col = 1..16`, out-of-range values are clamped |
| **Custom characters** | 8 CGRAM user characters |
| **Contrast self-test** | 16 solid blocks to tell "low contrast" apart from "thin font" |
| **Backlight control** | Via `P3`, polarity configurable (modules with hard-wired backlight ignore it) |

---

## 2. Hardware

### 2.1 Bill of materials

| Part | Notes |
|---|---|
| STM32F103C8T6 board | e.g. "Blue Pill", 72 MHz, 3.3 V logic |
| LCD1602A | 16×2, HD44780-compatible controller |
| PCF8574 I²C backpack | 4-wire module, address `0x27` (PCF8574) or `0x3F` (PCF8574A) |
| 4 jumper wires | `5V`, `GND`, `SDA`, `SCL` |
| Pull-ups | Usually on the backpack (4.7 kΩ to VDD) |

### 2.2 Backpack and LCD pin mapping

The PCF8574 has 8 quasi-bidirectional ports. On the common 4-wire backpack they are
wired as follows:

```
 PCF8574 byte = all eight pins at once
 ┌────┬────┬────┬────┬──────┬────┬────┬────┐
 │ P7 │ P6 │ P5 │ P4 │ P3   │ P2 │ P1 │ P0 │
 │ D7 │ D6 │ D5 │ D4 │ BL   │ E  │ RW │ RS │
 └────┴────┴────┴────┴──────┴────┴────┴────┘
```

| PCF8574 pin | LCD1602A pin | Direction |
|---|---|---|
| `P0` | `RS` | register select |
| `P1` | `RW` | read/write (always 0 here) |
| `P2` | `E` | enable, data latched on falling edge |
| `P3` | backlight transistor | active level is board-dependent |
| `P4`–`P7` | `D4`–`D7` | 4-bit data |

### 2.3 Wiring to the MCU

| LCD / backpack | STM32F103C8T6 | Remark |
|---|---|---|
| `VSS` | `GND` | common ground is mandatory |
| `VDD` | **`5V`** | **must be 5 V** — 3.3 V makes the text look washed out (see [7.2](#72-faint-characters--low-contrast)) |
| `SDA` | `PB7` | open-drain output, pulled up on the backpack |
| `SCL` | `PB6` | open-drain output, pulled up on the backpack |

### 2.4 Power, pull-ups and level shifting

- **Supply the LCD from 5 V.** The HD44780 contrast reference needs
  `VDD − V0 ≈ 4.2 … 4.8 V` at 25 °C. With a 3.3 V rail this is physically impossible,
  so characters will always be faint.
- The backpack usually carries **4.7 kΩ pull-ups on SDA/SCL** to its own VDD.
- `PB6`/`PB7` are **5 V-tolerant (FT)** pins and the driver configures them as
  **open-drain**, so a 5 V pull-up cannot back-feed the MCU.
- Keep the wires short and make sure `GND` is solid — loose breadboard contacts are
  the number-one cause of "sometimes it works".

### 2.5 Block diagram

```
 STM32F103C8T6                 PCF8574 backpack              LCD1602A
 ┌──────────────┐              ┌───────────────┐             ┌──────────────┐
 │ PB6  (SCL) ──┼──────────────┤ SCL           │             │              │
 │ PB7  (SDA) ──┼──────────────┤ SDA           │             │              │
 │ 5V  ─────────┼──────────────┤ VDD           │ P0..P2 ─────┤ RS  RW  E    │
 │ GND ─────────┼──────────────┤ GND           │ P4..P7 ─────┤ D4  D5  D6 D7│
 └──────────────┘              │               │ P3 ─────────┤ backlight    │
                               └───────────────┘             └──────────────┘
```

---

## 3. Getting Started

### 3.1 Prerequisites

- **Keil MDK 5** with the ARM Compiler (the project also builds with GCC if you
  provide your own startup/linker files).
- STM32F10x Standard Peripheral Library (already vendored under `Library/`).
- A **5 V** supply for the LCD module.

### 3.2 Build and flash

1. Open `Project.uvprojx` in Keil MDK 5.
2. Build the project (`F7`).
3. Download to the board (`F8`).

The driver files (`mokuai/MyPCF8574.c`, `mokuai/MyLCD1602A.c`) are already part of
the Keil project.

### 3.3 Expected result

```
Line 1: LCD1602A 4-bit OK
Line 2: CNT: 00000            <- increments every 0.5 s
```

- A steadily incrementing counter means the 4-bit init, the I²C timing and the DDRAM
  are all correct.
- **Every character correct but faint** → contrast issue, see [7.2](#72-faint-characters--low-contrast).
- **One character always missing a stroke** → hardware problem on that data line.

### 3.4 Contrast self-test

Set the switch at the top of `User/main.c` to `1` and rebuild:

```c
#define LCD_SHOW_CONTRAST_TEST  1
```

Line 1 then shows **16 solid black blocks** and line 2 shows `CONTRAST TEST`.
The blocks should be **pure black**; if they look grey/green, the contrast voltage
(V0) is too weak.

---

## 4. Repository Layout

```
.
├── Project.uvprojx              Keil MDK 5 project
├── User/
│   └── main.c                   self-test program (with contrast test switch)
├── mokuai/
│   ├── MyPCF8574.c/.h           software I²C + PCF8574 4-bit parallel port
│   ├── MyLCD1602A.c/.h          LCD1602A command/display layer
│   ├── key.c/.h                 keypad (PB6 conflict — see 7.5)
│   ├── fengmingqi.c/.h          buzzer
│   ├── OLED.c/.h                OLED on PB8/PB9 (no conflict with the LCD)
│   └── lcd1602.c/.h             legacy file, not compiled
├── system/Delay.c/.h            SysTick µs/ms delays
├── Start/  Library/             CMSIS + StdPeriph
├── verify/                      host-side I²C + HD44780 simulator (no board needed)
├── analysis/                    datasheet evidence and extracts
├── 修复说明.md                   detailed post-mortem (Chinese)
└── README.md / README.zh-CN.md  this document (EN / 中文)
```

---

## 5. API Reference

### 5.1 Initialization and status

| Function | Description |
|---|---|
| `void LCD1602A_Init(void)` | Configure I²C, detect the address, run the 4-bit reset sequence, turn the display on |
| `uint8_t LCD1602A_IsPresent(void)` | `1` = every I²C transaction was ACKed; `0` = wiring/address problem |
| `uint8_t LCD1602A_LastError(void)` | `0` OK, `1` NACK, `2` no PCF8574 found |

### 5.2 Display

| Function | Description |
|---|---|
| `void LCD1602A_Clear(void)` | Clear display (waits 1.53 ms internally) |
| `void LCD1602A_ReturnHome(void)` | Cursor to home position |
| `void LCD1602A_Display(uint8_t on, uint8_t cursor, uint8_t blink)` | Display / cursor / blink control |
| `void LCD1602A_SetCursor(uint8_t row, uint8_t col)` | Position cursor, `row = 1..2`, `col = 1..16` |
| `void LCD1602A_WriteChar(char ch)` | Write one character |
| `void LCD1602A_WriteString(const char *str)` | Write a string (no line clearing) |
| `void LCD1602A_ShowString(uint8_t row, uint8_t col, const char *str)` | Write a full line, padded with spaces so old text does not linger |
| `void LCD1602A_Command(uint8_t cmd)` | Send a raw HD44780 instruction |

### 5.3 Utilities

| Function | Description |
|---|---|
| `void LCD1602A_Backlight(uint8_t on)` | Backlight on/off (no effect on modules with a hard-wired backlight) |
| `void LCD1602A_DefineChar(uint8_t slot, const uint8_t rows[8])` | Define a CGRAM character (slot 0..7) |
| `void LCD1602A_SelfTest(void)` | Contrast self-test pattern |
| `void ShowLCDChars(uint8_t row, const char *str)` | Legacy compatibility wrapper |

### 5.4 Example

```c
#include "MyLCD1602A.h"

int main(void)
{
    LCD1602A_Init();
    if (!LCD1602A_IsPresent())
    {
        /* No ACK: check wiring, 5 V supply and the I²C address. */
    }

    LCD1602A_ShowString(1, 1, "Hello LCD1602A");
    LCD1602A_ShowString(2, 1, "I2C 0x27 OK");

    while (1)
    {
    }
}
```

---

## 6. How It Works

### 6.1 Software I²C

`PB6` (SCL) and `PB7` (SDA) are configured as **50 MHz open-drain outputs**.
A "1" is written by releasing the pin (`BSRR`), a "0" by pulling it low (`BRR`).
The half-period is 5 µs (≈ 66 kHz), well inside the PCF8574's 100 kHz limit.
Each transaction is: START → address → data → STOP, with the ACK bit checked.

### 6.2 From a byte to four LCD pins

Because only `D4`–`D7` are wired, one LCD byte is transmitted as **two nibbles**:

```
High nibble:  [ D7 D6 D5 D4 ]  E high, then E low   (latch)
Low  nibble:  [ D3 D2 D1 D0 ]  E high, then E low   (latch)
```

Each nibble is two PCF8574 writes: one with `P2 (E) = 1`, one with `P2 (E) = 0`.
The LCD latches `D7..D4` on the **falling edge** of `E`.

### 6.3 Reset sequence

At power-on the HD44780 is in **8-bit mode**. The datasheet-mandated sequence is:

```
wait ≥ 40 ms
  → 0x3, 0x3, 0x3      single nibbles, still 8-bit mode
  → 0x2                DL = 0: from this command on, 4-bit mode
  → 0x28               4-bit / 2 lines / 5×8 font
  → 0x08               display off (avoid garbage during init)
  → 0x01               clear display (wait 1.53 ms)
  → 0x06               entry mode: increment, no shift
  → 0x0C               display on, cursor off
```

**Skipping `0x2` means the controller never leaves 8-bit mode**, so every following
command is interpreted as a truncated 8-bit instruction — this was the root cause of
the garbled output.

### 6.4 Why the driver does not poll BUSY

Reading the busy flag through a PCF8574 requires releasing `P4`–`P7` (quasi-bidirectional
ports must be written high before being read), then reading `D7`, then issuing a correct
NACK + STOP. Any mistake corrupts the bus. This driver uses the datasheet delays
instead (39 µs for normal instructions, 1.53 ms for clear/home), so the bus is
**write-only** — the same approach used by LiquidCrystal_I2C and other mature libraries.

---

## 7. Troubleshooting

### 7.1 Garbled characters / intermittent display

**Fixed in this repository.** Root causes (see [修复说明.md](修复说明.md)):

1. The original init sent `0x38` through the 4-bit routine, so the bus carried
   `0x3` then `0x8` — **the `0x2` nibble was never sent**, and the controller stayed in
   8-bit mode. Since `D3`–`D0` are not connected, their floating value decided the
   result: reading `0x0` kept the display off, reading `0xF` accidentally enabled it —
   hence "sometimes works, sometimes not".
2. The busy-flag routine performed reads with a **write** address, released nothing,
   and replaced the NACK with a whole extra byte. It corrupted the bus on every
   character written (56 protocol violations in simulation).
3. Clear-display waited 0 ms instead of 1.53 ms, and power-on waited only 25 ms.

### 7.2 Faint characters / low contrast

**Contrast is an analog quantity controlled by the `V0` voltage — there is no software
knob for it.** Changing code, content or backlight will not make the "black" darker.

Check in this order:

**① Measure VDD with a multimeter (most common cause)**

| Measured VDD | Conclusion |
|---|---|
| 5 V | Supply is fine, see ② |
| 3.3 V | **This is the cause.** `VDD − V0` can never exceed 3.3 V, but the LCD needs 4.2–4.8 V. Move the module to 5 V |

**② Already 5 V: the contrast trimmer (V0) is set too weak**

The trimmer sits on the component side of the backpack. On modules where the backpack
is glued to the LCD, it is sandwiched between the two boards and cannot be reached.
Options:

1. **Soften the glue first** — black glue is usually hot-melt; warm it with a hair dryer
   or a drop of isopropyl alcohol, then gently separate the boards with a plastic
   spudger. **Never pry with a metal tool** — the 16-pin header breaks easily.
2. **Wire an external trimmer to V0** — solder a thin wire to pin 3 (`V0`) of the
   16-pin header and connect a 10 kΩ potentiometer between `V0` and `GND`
   (lower V0 = higher contrast).
3. **Use a module with an accessible trimmer** — cheapest and safest option.

**③ Use the self-test pattern to judge how black "black" can be**

Set `#define LCD_SHOW_CONTRAST_TEST 1` in `User/main.c`. Line 1 shows 16 solid blocks:

- blocks are **pure black** → contrast is fine, the 5×8 font is simply thin;
- blocks are **grey/green** → V0 is indeed too weak; go back to ① ②.

**Why dimming the backlight does not help**

The backlight changes overall brightness: "black" and "white" scale together, so the
contrast **ratio** does not change. Many of these modules also have a hard-wired
backlight (`LCD1602A_Backlight(0)` does nothing), so there is no software lever there
either.

### 7.3 Nothing on screen / no ACK

1. Is `VDD` 5 V and is `GND` common?
2. Are `SDA→PB7` and `SCL→PB6` correct and tight?
3. Does the backpack have 4.7 kΩ pull-ups (most do)?
4. Address: PCF8574 is `0x27`, PCF8574A is `0x3F` — the driver scans both automatically.
5. Check `LCD1602A_LastError()`: `1` = no ACK, `2` = no device found.

### 7.4 Backlight

```c
/* mokuai/MyPCF8574.h — flip if the backlight is inverted */
#define PCF8574_BL_ACTIVE_HIGH  1
```

If nothing changes, the module's backlight is not driven by `P3` at all
(hard-wired on or off). That is normal for some cheap modules.

### 7.5 Keypad conflict on PB6

`mokuai/key.c` reads **PB6** as a key input, but PB6 is the I²C **SCL** line.
The two will fight each other. It also initializes GPIOA pins while reading GPIOB
pins (copy-paste bug). Move the keypad to other pins (e.g. PA4/PA5/PA6) before
re-enabling the password-lock demo.

### 7.6 Which I2C address?

| Chip | Address (7-bit) |
|---|---|
| PCF8574 | `0x27` |
| PCF8574A | `0x3F` |

`LCD1602A_Init()` scans `0x20–0x27` and `0x38–0x3F`, so no manual change is needed.

---

## 8. Hardware-free Verification

`verify/` contains a **pure-software** I²C + HD44780 simulator. It compiles the real
driver sources (`MyPCF8574.c`, `MyLCD1602A.c`, `Delay.c`) **unmodified** as C++, turns
every GPIO write into a timestamped I²C waveform, decodes it, and runs the resulting
bytes through a cycle-accurate HD44780 model (4/8-bit mode, `E` falling-edge latch,
DDRAM/CGRAM, instruction execution times).

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File verify\run.ps1
```

It runs both the original sources (frozen in `verify/baseline/`, byte-identical to
git HEAD) and the current sources, and prints the results side by side.
See [verify/RESULTS.md](verify/RESULTS.md) for the full report.

---

## 9. Changelog

### 2026-10 — Fixed garbled display / intermittent operation
- Rewrote `mokuai/MyPCF8574.c/.h`: ACK checking, bus recovery, address scanning,
  explicit `E` pulses, only `PB6`/`PB7` reconfigured.
- Rewrote `mokuai/MyLCD1602A.c/.h`: correct reset sequence, delay-based timing
  instead of BUSY polling, cursor/string/custom-character helpers.
- Rewrote `User/main.c` as a self-test program; the original password-lock demo is kept
  in comments.
- Added `verify/` (host-side simulator) and `analysis/` (datasheet evidence).

### 2026-10 — Contrast self-test
- Added `LCD1602A_DefineChar()` and `LCD1602A_SelfTest()`.
- Added the `LCD_SHOW_CONTRAST_TEST` switch to `User/main.c` (default `0`).

---

## 10. References

| What | Where |
|---|---|
| Pin mapping `P0=RS, P1=RW, P2=E, P3=BL, P4–P7=D4–D7`; onboard 4.7 kΩ pull-ups | `Wiring diagram and manual/…/原理图.pdf` p.1 |
| Init: three `0x3` nibbles, then function set | `字符点阵液晶使用手册.pdf` p.14 |
| 4-bit mode uses `DB7–DB4` only, one byte in two transfers | `1602A.pdf` p.9 |
| Clear/home 1.53 ms, other instructions 39 µs, `E` pulse ≥ 450 ns | `字符点阵液晶使用手册.pdf` p.10, `1602A.pdf` p.8 |
| PCF8574 power-on state `0xFF`; quasi-bidirectional I/O | `PCF8574中文手册.pdf` p.6 |
| `0x27` / `0x3F` addresses | `PCF8574中文手册.pdf` p.4, `使用说明.pdf` p.1 |
| LCD supply 5 V, `VDD − V0 = 4.2–4.8 V` | `1602datasheet.pdf` p.1 |

---

## Appendix A — Root causes of the original driver

1. **Init sequence.** `MyPCF8574SendByte(0x38, 0, 0)` splits the byte into nibbles, so
   the bus carried `0x3` then `0x8` (observed: `0x34/0x30` followed by `0x84/0x80`).
   The mandatory `0x2` never appeared, `DL` stayed `1`, and the controller stayed in
   8-bit mode. With `D3–D0` floating, the result depended on the floating value:
   `0x0` → display never turned on; `0xF` → `0x0F` was accidentally accepted and the
   display turned on with garbage in DDRAM.
2. **Busy-flag routine.** It addressed the device with the *write* address
   (`READ_TXNS_0x4F = 0`), never released `P4–P7`, sent a full byte where a single
   NACK belonged, and issued repeated STARTs without STOP. Simulation counted
   56 violations. Its final check was also dead code:
   `Hdata &= 0x0F; Hdata &= 0x80;` is always 0.
3. **Timing.** Clear-display waited 0 ms instead of 1.53 ms; power-on waited 25 ms
   instead of ≥ 40 ms (module manual says > 4.1 ms after the supply stabilises;
   the original HD44780U datasheet says 40 ms — the driver now waits 50 ms).
4. **GPIO.** `GPIOB->CRL = 0x77000000` overwrote the configuration of `PB0`–`PB5`.
   The driver now uses `GPIO_Init()` on `PB6`/`PB7` only.
5. **Backlight.** `P3` was never driven high. The new driver sets it explicitly and
   makes the polarity configurable.

---

## Acknowledgements

- The software-I²C structure follows the well-known **江协科技 (Jiangxie Technology)**
  tutorial style (`MyI2C_*` naming, bit-banged open-drain bus).
- The contrast/timing numbers come from the vendor documents shipped in
  `Wiring diagram and manual/`.

---

## License

No license file is included in this repository yet. If you plan to redistribute or
reuse the code, add one — **MIT** is a good default for projects like this.
