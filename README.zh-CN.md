# STM32F103C8T6 + LCD1602A（PCF8574 / 软件 I²C）驱动

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

> 🌐 **语言：** [English](README.md) · **简体中文**

一个给 **16×2 LCD1602A** 用的小而稳的驱动：**PCF8574 I²C 转接板** + **4 位数据总线** +
`PB6`/`PB7` 软件 I²C，基于 STM32 标准外设库。不读忙标志、不乱码。

**背景**：本仓库最早是一个"求助帖"——屏幕乱码、时好时坏。根因是软件问题
（4 位初始化序列少了 `0x2`，加上读忙函数会破坏 I²C 总线），已修复，并用主机端仿真验证过。
详细复盘：[修复说明.md](修复说明.md)。

| | 原代码 | 修复后 |
|---|---|---|
| 初始化后总线模式 | 8-bit（错） | **4-bit** |
| 收到 `0x28` / `0x0C` / `0x01` / `0x06` | 0/0/0/0 | **全部** |
| I²C 协议违规 | 56 处 | **0 处** |

---

## 快速上手

### 方案 A —— 直接跑 Demo（30 秒）

1. 用 **Keil MDK 5** 打开 `Project.uvprojx`；
2. 编译（`F7`）→ 下载（`F8`）；
3. 屏幕上应出现：

```
第 1 行: LCD1602A 4-bit OK
第 2 行: CNT: 00000        ← 每 0.5 秒 +1
```

### 方案 B —— 用到自己的工程里（5 分钟）

**第 1 步：把这 6 个文件加进你的 Keil 工程**
（右键分组 → *Add Existing Files…*）：

```
mokuai/MyPCF8574.c     mokuai/MyPCF8574.h
mokuai/MyLCD1602A.c    mokuai/MyLCD1602A.h
system/Delay.c         system/Delay.h
```

在 *Options → C/C++ → Include Paths* 里加上 `.\mokuai` 和 `.\system`。
驱动还依赖标准外设库的 **GPIO** 和 **RCC**（`stm32f10x_gpio.c`、`stm32f10x_rcc.c`），
一般工程里已经带了。

**第 2 步：接 4 根线**

| 转接板 | STM32F103 | 说明 |
|---|---|---|
| `VSS` | `GND` | 必须共地 |
| `VDD` | **`5V`** | 必须 5 V，不要 3.3 V |
| `SDA` | `PB7` | 开漏输出，模块自带 4.7 kΩ 上拉 |
| `SCL` | `PB6` | 开漏输出，模块自带 4.7 kΩ 上拉 |

**第 3 步：写代码**

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

**第 4 步：检查结果**

- 屏幕全黑/没反应 → `LCD1602A_IsPresent()` 返回 `0`，查接线、5 V 和 `LCD1602A_LastError()`；
- 有字但很淡 → 这是对比度（`V0` 模拟电压）问题，不是代码，见 [常见问题](#常见问题)。

整个集成就是：**6 个文件 + 4 根线 + 3 个函数调用**。

---

## 接线说明

转接板把 PCF8574 的 8 个引脚这样接（主流 4 线模块）：

```
 PCF8574:  P7  P6  P5  P4  P3    P2  P1  P0
           D7  D6  D5  D4  背光  E   RW  RS
```

- `P4`–`P7` → `D4`–`D7`（4 位数据）
- `P2` → `E`（数据在下降沿被锁存）
- `P1` → `RW`（恒为 0，本驱动只写）
- `P0` → `RS`，`P3` → 背光
- 模块通常在 SDA/SCL 上各带 **4.7 kΩ 上拉**
- `PB6`/`PB7` 是 **5 V 容忍** 引脚，且配置为 **开漏输出**，5 V 上拉不会倒灌 MCU

杜邦线尽量短，地线一定要接牢——接触不良是"时好时坏"的头号原因。

---

## API

| 函数 | 作用 |
|---|---|
| `LCD1602A_Init()` | 配 I²C、扫描地址、执行 4 位复位序列、开显示 |
| `LCD1602A_IsPresent()` | `1` = 所有 I²C 写都有应答 |
| `LCD1602A_LastError()` | `0` 正常 / `1` 无应答 / `2` 没找到 PCF8574 |
| `LCD1602A_Clear()` | 清屏（内部等 1.53 ms） |
| `LCD1602A_ReturnHome()` | 光标回原点 |
| `LCD1602A_Display(on, cursor, blink)` | 显示 / 光标 / 闪烁 |
| `LCD1602A_SetCursor(row, col)` | 定位，`row` 1–2，`col` 1–16 |
| `LCD1602A_WriteChar(ch)` | 写一个字符 |
| `LCD1602A_WriteString(str)` | 写字符串 |
| `LCD1602A_ShowString(row, col, str)` | 写一整行，不足补空格 |
| `LCD1602A_Command(cmd)` | 发原始 HD44780 指令 |
| `LCD1602A_Backlight(0/1)` | 背光开关（硬件常亮的模块无效） |
| `LCD1602A_DefineChar(slot, rows[8])` | 定义 CGRAM 自定义字符 |
| `LCD1602A_SelfTest()` | 对比度自检图（16 个实心黑块） |
| `ShowLCDChars(row, str)` | 兼容旧接口 |

---

## 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| 乱码、时好时坏 | 原驱动的 8 位模式 + 总线被搅乱 | 已修复，见 [修复说明.md](修复说明.md) |
| 有字但**很淡** | 对比度 `V0` / 供电 | 先量 `VDD` 必须 **5 V**，再调电位器（见下） |
| 完全不亮 | 没有 ACK | 查接线、5 V、共地、地址（`0x27` / `0x3F`） |
| 背光没反应 | 模块背光是硬件常亮 | 正常现象 |
| 按键异常 | `key.c` 把 `PB6` 当按键，与 SCL 冲突 | 按键换脚 |

**字很淡——快速判断**

1. 量转接板 `VDD`：**3.3 V 就是原因**（LCD 需要 `VDD − V0 ≈ 4.2–4.8 V`），改接 5 V；
2. 已经是 5 V → `V0` 电位器设得太弱。它在转接板朝里的那一面，黑胶粘死的模块被夹在中间，
   可先用吹风机/无水酒精软化黑胶再调；或从 16 脚排针第 3 脚（`V0`）外接 10 kΩ 电位器到 GND；
   或换一块带可调电位器的模块；
3. 想知道"黑"能有多黑：把 `User/main.c` 里 `#define LCD_SHOW_CONTRAST_TEST` 改成 `1`，
   第 1 行显示 16 个实心黑块。纯黑 = 对比度没问题（只是 5×8 字体细）；发灰 = `V0` 不足。

调背光**没用**：它把"黑"和"白"同比例变亮变暗，对比度比例不变。

---

## 工作原理（精简版）

- 一个 LCD 字节拆成**两个半字节**发送，各产生一次 `E` 脉冲（转接板只引出 `D4`–`D7`）；
- 初始化必须发 `0x3, 0x3, 0x3 → 0x2 → 0x28 → 0x01 → 0x06 → 0x0C`，
  **少了 `0x2` 就永远进不了 4 位模式**——这就是原来的乱码根因；
- 不读忙标志，改用固定延时（普通指令 39 µs、清屏/归位 1.53 ms），总线只写不读；
- 每次写都检查 ACK；上电先打 9 个 SCL 救回被拉死的总线。

完整解释见 [修复说明.md](修复说明.md)。

---

## 目录结构

```
Project.uvprojx            Keil MDK 5 工程
User/main.c                Demo：固定文字 + 计数器（含对比度测试开关）
mokuai/MyPCF8574.c/.h      软件 I²C + PCF8574 4 位并口
mokuai/MyLCD1602A.c/.h     LCD1602A 指令/显示层
system/Delay.c/.h          SysTick 微秒/毫秒延时
verify/                    主机端 I²C + HD44780 仿真器（不需要开发板）
analysis/                  数据手册取证
修复说明.md                 详细事故复盘
```

跑仿真（编译真实驱动源码，不需要硬件）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File verify\run.ps1
```

---

## 更新记录

- **2026-10** —— 修复乱码 / 时好时坏：正确的 4 位初始化、用延时替代读忙、ACK 检查、总线恢复；
- **2026-10** —— 新增 `LCD1602A_SelfTest()` / `LCD1602A_DefineChar()` 与
  `LCD_SHOW_CONTRAST_TEST` 开关。

---

## 许可

仓库目前没有 LICENSE 文件。如果要分发或复用，建议补一个——这类工程用 **MIT** 比较合适。
