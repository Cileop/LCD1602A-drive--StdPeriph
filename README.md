# STM32F103C8T6 + LCD1602A（PCF8574 / 软件 I2C）驱动

基于 **STM32 标准外设库（StdPeriph）** 的 LCD1602A 驱动，通过 PCF8574 转接板用
**4 位数据总线** 驱动 1602 字符屏；软件 I2C 运行在 PB6(SCL) / PB7(SDA)。

> **背景 / Background**：这个仓库最早是一个"求助帖"——屏幕显示乱码、有时能亮有时不亮。
> 问题已经定位并修复：**不是硬件问题，是驱动里的初始化序列和 I2C 时序写错了**。
> 完整的事故分析见 [修复说明.md](修复说明.md)，数据手册依据见
> [analysis/datasheet-findings.md](analysis/datasheet-findings.md)。

| | 原代码 | 修复后 |
|---|---|---|
| 初始化后总线模式 | 8-bit（错） | 4-bit |
| 收到 `0x28/0x0C/0x01/0x06` | 0/0/0/0 | 全部收到 |
| DDRAM 第 1 行 | 乱码 | `Password:` |
| I2C 协议违规 | 56 处 | 0 处 |

---

## 目录

- [1. 特性](#1-特性)
- [2. 硬件与接线](#2-硬件与接线)
- [3. 快速开始](#3-快速开始)
- [4. 目录结构](#4-目录结构)
- [5. 驱动 API](#5-驱动-api)
- [6. 原理：4 位模式是怎么工作的](#6-原理4-位模式是怎么工作的)
- [7. 常见问题](#7-常见问题)
  - [7.1 显示很淡 / 对比度不够（重点）](#71-显示很淡--对比度不够重点)
  - [7.2 乱码、时好时坏](#72-乱码时好时坏)
  - [7.3 屏幕完全不亮 / NO PCF8574 ACK](#73-屏幕完全不亮--no-pcf8574-ack)
  - [7.4 背光不亮或常亮](#74-背光不亮或常亮)
  - [7.5 按键和 PB6 冲突](#75-按键和-pb6-冲突)
- [8. 不用开发板的验证方法](#8-不用开发板的验证方法)
- [9. 修复记录](#9-修复记录)
- [10. 参考资料](#10-参考资料)
- [附录：最初的提问](#附录最初的提问)

---

## 1. 特性

| 特性 | 说明 |
|---|---|
| 4 位数据总线 | I2C 转接板只引出 D4~D7，驱动按手册做高/低半字节两次 E 脉冲 |
| 正确的复位流程 | `0x3,0x3,0x3 → 0x2 → 0x28 → 0x08 → 0x01 → 0x06 → 0x0C`，上电等 50ms |
| 自动识别地址 | 扫描 `0x27`（PCF8574）与 `0x3F`（PCF8574A） |
| ACK 检查 | 每次写都检查从机应答；失败返回错误码，不静默失败 |
| 总线恢复 | 上电打 9 个 SCL，防止从机卡死把 SDA 拉低 |
| 不读忙标志 | 按手册固定延时（39µs / 清屏 1.53ms），总线只有"写"，简单可靠 |
| 行列定位 | `row=1~2`、`col=1~16`，越界自动夹住 |
| 自定义字符 | 8 个 CGRAM 自定义点阵字符 |
| 对比度自检图 | 16 个实心黑块，用来判断 V0 是否偏弱 |
| 背光控制 | P3 控制（极性可配）；硬件常亮的模块此功能无效 |

---

## 2. 硬件与接线

### 2.1 接线

| LCD / 转接板 | STM32F103C8T6 | 说明 |
|---|---|---|
| VSS | GND | 必须共地 |
| VDD | **5V** | **必须 5V**，接 3.3V 会导致字很淡（见 7.1） |
| SDA | PB7 | 开漏输出，模块自带 4.7k 上拉 |
| SCL | PB6 | 开漏输出，模块自带 4.7k 上拉 |

- 转接板 P0~P7 与 LCD 的对应关系：`P0=RS, P1=RW, P2=E, P3=背光, P4~P7=D4~D7`，
  这是主流 4 线 I2C 模块的标准接法（`原理图.pdf` 第 1 页）。
- PB6/PB7 是 5V 容忍（FT）引脚，且配置为**开漏**，所以模块上拉到 5V 也不会倒灌。
- 杜邦线尽量短，GND 一定要接好——接触不良是"时好时坏"的头号硬件原因。

### 2.2 供电的坑

1602 的对比度由 **V0（第 3 脚）** 决定：`VDD − V0` 越大字越黑，
数据手册给的典型值是 **`VDD − V0 = 4.2~4.8V`（5V 供电、25℃）**。

- **VDD = 5V** → 正常。
- **VDD = 3.3V** → 最大也只有 3.3V，达不到 4.2V，字一定发灰、对比度调不上去。
  如果接在开发板的 3.3V 上，先改成 5V（USB 的 5V 或开发板的 5V 脚）。

---

## 3. 快速开始

1. 用 Keil 5 打开 `Project.uvprojx`；
2. 编译（`mokuai/MyPCF8574.c`、`mokuai/MyLCD1602A.c` 已在工程里）；
3. 下载到 STM32F103C8T6。

正常应该看到：

```
第 1 行: LCD1602A 4-bit OK
第 2 行: CNT: 00000        ← 每 0.5 秒 +1
```

- 计数器稳定跳动 = 4 位初始化、I2C 时序、DDRAM 全部正常。
- 如果**每个字符都正确但发灰** → 是对比度问题，看 [7.1](#71-显示很淡--对比度不够重点)。
- 如果**某个字符固定缺笔画/错位** → 是那一位数据线的硬件问题。

### 只想要对比度测试图？

把 `User/main.c` 顶部的开关改成 1 重新编译：

```c
#define LCD_SHOW_CONTRAST_TEST  1
```

第 1 行会显示 16 个实心黑块，第 2 行显示 `CONTRAST TEST`。
**黑块应该是纯黑的**；发灰、边缘模糊就说明 V0 偏弱。

---

## 4. 目录结构

```
├─ Project.uvprojx            Keil 工程
├─ User/
│  └─ main.c                  自检程序（含对比度自检开关）
├─ mokuai/
│  ├─ MyPCF8574.c/.h          软件 I2C + PCF8574 4 位并口
│  ├─ MyLCD1602A.c/.h         LCD1602A 指令/显示层
│  ├─ key.c/.h                按键（注意 PB6 冲突，见 7.5）
│  ├─ fengmingqi.c/.h         蜂鸣器
│  ├─ OLED.c/.h               OLED（PB8/PB9，与 LCD 不冲突）
│  └─ lcd1602.c/.h            历史遗留，未加入工程
├─ system/Delay.c/.h          SysTick 微秒/毫秒延时
├─ Start/  Library/           CMSIS + 标准外设库
├─ verify/                    主机端 I2C+HD44780 仿真器（不需要开发板）
├─ analysis/                  数据手册取证与原文
├─ 修复说明.md                问题分析 / 修复记录（事故复盘）
└─ README.md
```

---

## 5. 驱动 API

### 5.1 初始化与状态

| 函数 | 说明 |
|---|---|
| `LCD1602A_Init()` | 初始化（配 I2C、自动找地址、4 位复位、开显示） |
| `LCD1602A_IsPresent()` | 1 = I2C 全程有应答；0 = 接线/地址有问题 |
| `LCD1602A_LastError()` | 0 正常 / 1 I2C 无应答 / 2 没找到 PCF8574 |

### 5.2 显示

| 函数 | 说明 |
|---|---|
| `LCD1602A_Clear()` | 清屏（内部已等 1.53ms） |
| `LCD1602A_ReturnHome()` | 光标回原点 |
| `LCD1602A_Display(on, cursor, blink)` | 显示开关 / 光标 / 闪烁 |
| `LCD1602A_SetCursor(row, col)` | 定位，`row=1~2`、`col=1~16` |
| `LCD1602A_WriteChar(ch)` | 写一个字符 |
| `LCD1602A_WriteString(str)` | 写字符串（不清行尾） |
| `LCD1602A_ShowString(row, col, str)` | 写一整行，不足 16 字符补空格（不会留残影） |
| `LCD1602A_Command(cmd)` | 直接发一条原始指令 |

### 5.3 其它

| 函数 | 说明 |
|---|---|
| `LCD1602A_Backlight(0/1)` | 背光开关；硬件常亮的模块无效 |
| `LCD1602A_DefineChar(slot, rows[8])` | 定义 CGRAM 自定义字符（slot 0~7） |
| `LCD1602A_SelfTest()` | 对比度自检图 |
| `ShowLCDChars(row, str)` | 兼容旧接口（显示整行） |

### 5.4 用法示例

```c
#include "MyLCD1602A.h"

int main(void)
{
    LCD1602A_Init();
    if (!LCD1602A_IsPresent()) { /* 接线/地址问题 */ }

    LCD1602A_ShowString(1, 1, "Hello LCD1602A");
    LCD1602A_ShowString(2, 1, "I2C 0x27 OK");
    while (1) { }
}
```

---

## 6. 原理：4 位模式是怎么工作的

### 6.1 PCF8574 的引脚映射

```
 PCF8574 一个字节 = 同时刷新 P7..P0
 ┌────┬────┬────┬────┬─────┬────┬────┬────┐
 │ P7 │ P6 │ P5 │ P4 │ P3  │ P2 │ P1 │ P0 │
 │ D7 │ D6 │ D5 │ D4 │背光 │ E  │ RW │ RS │
 └────┴────┴────┴────┴─────┴────┴────┴────┘
```

所以"给 LCD 送半个字节"= 写一个 PCF8574 字节：高 4 位放数据、`P2(E)=1`；
再写一个字节把 `P2(E)=0` —— **LCD 在 E 的下降沿把 D7~D4 锁存进去**。

### 6.2 一个字节 = 两次 E 脉冲

```
高 4 位:  [D7 D6 D5 D4] E↑ E↓
低 4 位:  [D3 D2 D1 D0] E↑ E↓
```

顺序不能反（先高后低）。RS 决定是"指令"还是"数据"，RW 全程为 0（只写）。

### 6.3 初始化序列（为什么必须发 0x2）

LCD 上电默认是 **8 位模式**，而转接板只接了 4 根数据线。手册要求的复位流程是：

```
上电等 ≥40ms
  → 0x3, 0x3, 0x3      (单半字节, 仍 8 位)
  → 0x2               (DL=0, 从这条起进入 4 位模式)
  → 0x28              4 位 / 双行 / 5x8
  → 0x08              先关显示
  → 0x01              清屏 (等 1.53ms)
  → 0x06              输入模式: 光标右移、屏幕不滚动
  → 0x0C              开显示
```

**少发 0x2 就永远进不了 4 位模式**，后面所有指令都会变成残缺的 8 位指令——
这正是原代码乱码的根因。

### 6.4 为什么不读忙标志

读忙标志需要把 PCF8574 的 P4~P7 先置 1（准双向口作输入），再读回 D7，
并且要正确发 NACK + STOP。任何一步错了都会把总线搅乱。
本驱动改用数据手册给的固定延时（普通指令 39µs、清屏/归位 1.53ms），
总线只做"写"，简单可靠——这也是 LiquidCrystal_I2C 等成熟库的做法。

---

## 7. 常见问题

### 7.1 显示很淡 / 对比度不够（重点）

**先明确一点：对比度是纯模拟量，由 V0 电压决定，软件改不了。**
改代码、换显示内容、开关背光都不会让"黑"变得更黑。

**排查顺序：**

**① 量一下 VDD 是不是 5V（最常见）**

用万用表量转接板 VDD 对 GND：

| 实测 | 结论 |
|---|---|
| 5V | 供电没问题，看第 ② 条 |
| 3.3V | **这就是原因**。VDD−V0 最大只有 3.3V，达不到 4.2V，字一定发灰。改接 5V 即可 |

**② 如果已经是 5V：电位器（V0）被设在了偏弱的位置**

电位器通常在转接板朝里的那一面。如果你的模块是"转接板 + LCD 用黑胶粘死"的，
电位器就在夹层里，够不到。可用的办法：

1. **软化黑胶后再调**：黑胶多半是热熔胶，用吹风机吹热或蘸一点无水酒精让它变软，
   用塑料片轻轻分开一点，露出蓝色电位器；
   **不要用金属刀硬撬**，很容易把 16 脚排针撬断。
2. **从 16 脚排针引出 V0（第 3 脚）外接电位器**：在 V0 与 GND 之间接一个 10k 电位器，
   调到字最黑为止（V0 越低对比度越深）。
3. **换一块背面带可调电位器的模块**（最省事，也很便宜）。

**③ 用自检图判断"到底有多黑"**

`User/main.c` 里把 `LCD_SHOW_CONTRAST_TEST` 改成 1，第 1 行会显示 16 个实心黑块：

- 黑块是**纯黑**的 → 对比度其实没问题，只是 5x8 字体本身比较细；
- 黑块**发灰/发绿** → 确实是 V0 不足，按 ① ② 处理。

**为什么调背光没用？**

背光是整体亮度：`黑` 和 `白` 会按同样比例变亮/变暗，**对比度比例不变**，
所以"字淡"不会因为调背光而变好。而且有些模块的背光是硬件常亮的
（`LCD1602A_Backlight(0)` 也不灭，P3 没接三极管），更没法用软件调。

### 7.2 乱码、时好时坏

**已在本仓库修复**，根因（详见 [修复说明.md](修复说明.md)）：

1. 初始化只发了 `0x38`，经 4 位拆分后总线上是 `0x3`、`0x8`，**从来没有发过 `0x2`**，
   LCD 一直停在 8 位模式；而 D3~D0 没接线，读到的值随温湿度变化 →
   有时能显示（读到 0xF）、有时全黑（读到 0x0），而且内容是乱码。
2. 读忙标志函数用写地址去读、读完发一个字节代替 NACK+STOP，每次写字符前都把总线搅乱。
3. 清屏后没等 1.53ms，上电只等了 25ms。

### 7.3 屏幕完全不亮 / NO PCF8574 ACK

按顺序查：

1. VDD 是不是 5V，GND 是否共地；
2. SDA→PB7、SCL→PB6 有没有接反/松动；
3. 模块上有没有 4.7k 上拉（多数自带）；
4. 地址：PCF8574 是 `0x27`，PCF8574A 是 `0x3F`——本驱动会自动扫描，不用手改；
5. 用 `LCD1602A_LastError()` 看是"没应答"还是"没找到器件"。

### 7.4 背光不亮或常亮

`MyPCF8574.h` 里：

```c
#define PCF8574_BL_ACTIVE_HIGH  1   /* 亮灭相反时改成 0 */
```

如果改了没用，说明这块板的背光根本没经过 P3（硬件常亮或常灭），属正常现象。

### 7.5 按键和 PB6 冲突

`mokuai/key.c` 把 **PB6 当成按键输入**，而 PB6 是 I2C 的 SCL，两者会打架；
而且它初始化配的是 GPIOA，读取时却用 GPIOB（复制粘贴错误）。
要恢复密码锁功能，按键必须换脚（例如统一用 PA4/PA5/PA6）。

---

## 8. 不用开发板的验证方法

`verify/` 里是一个**纯软件**的 I2C + HD44780 仿真器：它直接编译仓库里
真实的 `MyPCF8574.c` / `MyLCD1602A.c` / `Delay.c`（一个字节都不改），
把 GPIO 的每次翻转记录下来，按 I2C 协议解码、按 HD44780 指令集执行，
最后打印 DDRAM 内容。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File verify\run.ps1
```

它会同时跑"原始代码（`verify/baseline/`，与 git HEAD 逐字节一致）"和"当前代码"，
结果并排输出。详细报告见 [verify/RESULTS.md](verify/RESULTS.md)。

---

## 9. 修复记录

**2026-10：修复乱码 / 时好时坏**

- `mokuai/MyPCF8574.c/.h`：重写 I2C 层（ACK 检查、总线恢复、地址扫描、显式 E 脉冲、
  只配置 PB6/PB7）；
- `mokuai/MyLCD1602A.c/.h`：重写初始化序列、用延时替代读忙、新增定位/字符串/自定义字符/
  对比度自检；
- `User/main.c`：改成自检程序，保留原密码锁代码；
- 新增 `verify/` 仿真验证器与 `analysis/` 数据手册取证。

**2026-10：新增对比度自检图**（`LCD1602A_SelfTest()`），用于区分
"对比度不足" 与 "字体本身较细"。

---

## 10. 参考资料

| 内容 | 出处 |
|---|---|
| P0=RS, P1=RW, P2=E, P3=背光, P4~P7=DB4~DB7；板载 4.7k 上拉 | `Wiring diagram and manual/…/原理图.pdf` p.1 |
| 初始化：三个 `0x3` 再功能设置 | `字符点阵液晶使用手册.pdf` p.14 |
| 4 位模式只用 DB7~DB4，一个字节分两次 | `1602A.pdf` p.9 |
| 清屏/归位 1.53ms，其它指令 39µs，E 脉冲 ≥450ns | `字符点阵液晶使用手册.pdf` p.10、`1602A.pdf` p.8 |
| PCF8574 上电输出默认 0xFF；准双向口 | `PCF8574中文手册.pdf` p.6 |
| `0x27` / `0x3F` 地址 | `PCF8574中文手册.pdf` p.4、`使用说明.pdf` p.1 |
| LCD 供电 5V、VDD−V0 = 4.2~4.8V | `1602datasheet.pdf` p.1 |

---

## English summary

An STM32F103C8T6 (StdPeriph) driver for an LCD1602A via a PCF8574 I2C backpack,
using a 4-bit bus over software I2C on PB6/PB7. The original driver showed garbage
and worked only intermittently because it never sent the `0x2` nibble that switches
the controller to 4-bit mode (so D3-D0 floated), and because its busy-flag routine
corrupted the I2C bus. Both are fixed; a host-side simulation harness under
`verify/` reproduces the old failure and verifies the fix without hardware.
See [修复说明.md](修复说明.md) for the full post-mortem.

---

## 附录：最初的提问

> stm32f103c8t6-LCD1602A-drive--java-standard-library-
> 我写了LCD1602A的驱动(LCD1602A是四针脚I2C,里面有PCF8574做I2C转接),但不知道为什么无法正常显示,字体有乱码,(用的是标准库),请求大佬们指点 (代码在master里) (接线图,现象,手册,都在master中的Wriring diagram and manual) (软件I2C的代码用的江协科技的)
>
> 主要驱动代码在 mokuai里的 MyPCF8574.c/.h 和 MyLCD1602A.c/.h 部分配置采用寄存器,已经把正常配置的注释了
>
> **ENGLISH**: I wrote the driver for LCD1602A (the LCD1602A is a four-pin I2C; inside there is a PCF8574 used for I2C conversion), but I don't know why it can't display normally. There are garbled characters. (I used the standard library.) I'm asking the experts for guidance. (The code is in the master branch.) (The wiring diagram, phenomenon, and manual are all in the master.) (The code for the software I2C uses the technology from Jiangxie Technology.)
