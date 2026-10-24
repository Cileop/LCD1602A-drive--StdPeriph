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

A small, reliable driver for a **16×2 LCD1602A** on a **PCF8574 I²C backpack**:
4-bit data bus over software I²C (`PB6`/`PB7`), STM32 StdPeriph, no BUSY polling,
no garbled characters.

**Background:** this started as a help request — the display showed garbage and only
worked sometimes. Root cause was software (missing `0x2` in the 4-bit init sequence,
plus a broken busy-flag routine). Fixed and verified with a host-side simulator.
Post-mortem: [修复说明.md](修复说明.md).

| | Original driver | This driver |
|---|---|---|
| Bus mode after init | 8-bit (wrong) | **4-bit** |
| `0x28` / `0x0C` / `0x01` / `0x06` received | 0/0/0/0 | **all** |
| I²C protocol violations | 56 | **0** |

---

## Quick Start

### Option A — run the demo (30 seconds)

1. Open `Project.uvprojx` in **Keil MDK 5**.
2. Build (`F7`) → Download (`F8`).
3. You should see:

```
Line 1: LCD1602A 4-bit OK
Line 2: CNT: 00000        <- +1 every 0.5 s
```

### Option B — use the driver in your own project (5 minutes)

**Step 1. Add these 6 files to your Keil project**
(right-click a group → *Add Existing Files…*):

```
mokuai/MyPCF8574.c     mokuai/MyPCF8574.h
mokuai/MyLCD1602A.c    mokuai/MyLCD1602A.h
system/Delay.c         system/Delay.h
```

Add `.\mokuai` and `.\system` to *Options → C/C++ → Include Paths*.
The driver also needs StdPeriph **GPIO** and **RCC**
(`stm32f10x_gpio.c`, `stm32f10x_rcc.c`) — normally already in the project.

**Step 2. Connect 4 wires**

| Backpack | STM32F103 | Note |
|---|---|---|
| `VSS` | `GND` | common ground |
| `VDD` | **`5V`** | must be 5 V, not 3.3 V |
| `SDA` | `PB7` | open-drain, pulled up on the module |
| `SCL` | `PB6` | open-drain, pulled up on the module |

**Step 3. Write the code**

```c
#include "MyLCD1602A.h"

int main(void)
{
    LCD1602A_Init();
    LCD1602A_ShowString(1, 1, "Hello, world!");
    LCD1602A_ShowString(2, 1, "I2C 0x27 OK");
    while (1)
    {
    }
}
```

**Step 4. Done — check the result**

- Screen blank / nothing? `LCD1602A_IsPresent()` returns `0` → check wiring,
  5 V and `LCD1602A_LastError()`.
- Characters show but look faint? That is contrast (analog `V0`), not code —
  see [Troubleshooting](#troubleshooting).

That is the whole integration: **6 files + 4 wires + 3 function calls**.

---

## Wiring Details

The backpack maps the PCF8574's 8 pins as follows (standard 4-wire module):

```
 PCF8574:  P7  P6  P5  P4  P3    P2  P1  P0
           D7  D6  D5  D4  BL    E   RW  RS
```

- `P4`–`P7` → `D4`–`D7` (4-bit data)
- `P2` → `E` (data is latched on the falling edge)
- `P1` → `RW` (always 0; this driver never reads)
- `P0` → `RS`, `P3` → backlight
- The module usually carries **4.7 kΩ pull-ups** on SDA/SCL.
- `PB6`/`PB7` are **5 V-tolerant** and driven as **open-drain**, so a 5 V pull-up
  cannot back-feed the MCU.

Keep the wires short and the ground solid — loose contacts are the number one cause
of "it works sometimes".

---

## API

| Function | Purpose |
|---|---|
| `LCD1602A_Init()` | I²C setup, address scan, 4-bit reset sequence, display on |
| `LCD1602A_IsPresent()` | `1` = all I²C writes were ACKed |
| `LCD1602A_LastError()` | `0` OK / `1` NACK / `2` no PCF8574 found |
| `LCD1602A_Clear()` | Clear display (waits 1.53 ms) |
| `LCD1602A_ReturnHome()` | Cursor to home |
| `LCD1602A_Display(on, cursor, blink)` | Display / cursor / blink control |
| `LCD1602A_SetCursor(row, col)` | Position cursor, `row` 1–2, `col` 1–16 |
| `LCD1602A_WriteChar(ch)` | Write one character |
| `LCD1602A_WriteString(str)` | Write a string |
| `LCD1602A_ShowString(row, col, str)` | Write a full line, padded with spaces |
| `LCD1602A_Command(cmd)` | Raw HD44780 instruction |
| `LCD1602A_Backlight(0/1)` | Backlight (no effect if the module's is hard-wired) |
| `LCD1602A_DefineChar(slot, rows[8])` | Define a CGRAM custom character |
| `LCD1602A_SelfTest()` | Contrast test pattern (16 solid blocks) |
| `ShowLCDChars(row, str)` | Legacy compatibility wrapper |

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Garbled characters, works sometimes | old driver (8-bit mode + bus corruption) | already fixed here — see [修复说明.md](修复说明.md) |
| Text visible but **faint** | contrast `V0` / supply | measure `VDD`: must be **5 V**; then adjust the trimmer — see below |
| Nothing at all | no ACK | wiring, 5 V, ground, address (`0x27` / `0x3F`) |
| Backlight never changes | backlight hard-wired on the module | normal for some modules |
| Keypad behaves oddly | `key.c` reads `PB6`, which is SCL | move the keypad to other pins |

**Faint text — quick decision tree**

1. Measure `VDD` on the backpack: **3.3 V is the problem** (the LCD needs
   `VDD − V0 ≈ 4.2–4.8 V`). Move it to 5 V.
2. Already 5 V → the `V0` trimmer is set too weak. It sits on the component side of
   the backpack; on glued modules it is sandwiched and hard to reach.
   Options: soften the glue (hot-melt, hair dryer / isopropyl alcohol) and turn it;
   or wire a 10 kΩ trimmer between `V0` (pin 3 of the 16-pin header) and `GND`;
   or use a module with an accessible trimmer.
3. Want to know how black "black" can be? Set `#define LCD_SHOW_CONTRAST_TEST 1` in
   `User/main.c`: line 1 shows 16 solid blocks. Pure black = contrast is fine
   (the 5×8 font is just thin); grey = `V0` too weak.

Dimming the backlight does **not** help: it scales "black" and "white" together, so
the contrast ratio stays the same.

---

## How It Works (short version)

- Every LCD byte is sent as **two nibbles**, each with its own `E` pulse
  (the backpack only exposes `D4`–`D7`).
- Init must send `0x3, 0x3, 0x3 → 0x2 → 0x28 → 0x01 → 0x06 → 0x0C`.
  **Without `0x2` the controller never leaves 8-bit mode** — that was the bug.
- No BUSY polling: fixed delays (39 µs, 1.53 ms for clear/home) keep the bus
  write-only and reliable.
- ACK is checked on every write; 9 SCL pulses recover a stuck bus at start-up.

Full explanation (Chinese): [修复说明.md](修复说明.md).

---

## Project Layout

```
Project.uvprojx            Keil MDK 5 project
User/main.c                demo: fixed text + counter (+ contrast test switch)
mokuai/MyPCF8574.c/.h      software I²C + PCF8574 4-bit port
mokuai/MyLCD1602A.c/.h     LCD1602A command/display layer
system/Delay.c/.h          SysTick µs/ms delays
verify/                    host-side I²C + HD44780 simulator (no board needed)
analysis/                  datasheet evidence
修复说明.md                  detailed post-mortem (Chinese)
```

Run the simulator (compiles the real driver sources, no hardware):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File verify\run.ps1
```

---

## Changelog

- **2026-10** — Fixed the garbled/intermittent display: correct 4-bit init,
  delay-based timing instead of BUSY polling, ACK checking, bus recovery.
- **2026-10** — Added `LCD1602A_SelfTest()` / `LCD1602A_DefineChar()` and the
  `LCD_SHOW_CONTRAST_TEST` switch.

---

## License

No license file yet. If you redistribute or reuse the code, add one — **MIT** is a
good default for this kind of project.
