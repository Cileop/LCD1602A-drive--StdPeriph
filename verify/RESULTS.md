# LCD1602A + PCF8574 驱动：无硬件主机仿真验证报告

**仓库**：`D:\桌面\lcd1602a-repo`
**基线提交**：`0d8444f5b5a674e6746ada91f89df937b487e04c`（旧驱动，冻结副本在 `verify/baseline/`）
**被测源码**：`mokuai/MyPCF8574.c/.h`、`mokuai/MyLCD1602A.c/.h`、`system/Delay.c`（**未做任何修改**，原样编译）
**工具链**：MinGW g++ 6.3.0（`C:\MinGWcui\MinGW\bin\g++.exe`）、PowerShell 5.1、Windows 10/11 x64
**仿真时间基准**：虚拟微秒时钟（`Delay.c` 的 SysTick 由代理对象驱动），结果完全确定（同一输入两次运行日志逐字节相同）

---

## 0. 一句话结论

| | 旧驱动（git HEAD） | 新驱动（工作区） |
|---|---|---|
| `LCD1602A_Init()` 结束后的模式 | **8 位总线，从未进入 4 位模式** | **4 位总线，2 行显示** |
| 实际收到的 `0x28 / 0x0C / 0x01 / 0x06` | **0 / 0 / 0 / 0** | **全部收到，顺序正确**（`0x28 → 0x08 → 0x01 → 0x06 → 0x0C`） |
| 显示开关（D 位） | `0x0F` 被拆成 `NOP + 设DDRAM`：D3-D0 读低时**永远关显示**；读高时误开显示 | **D=1，正常开显示** |
| `ShowLCDChars(1,"Password:")` 后 DDRAM 第 1 行 | `00 50 00 00 60 10 00 70 30 00 70 30 00 70 70 00`（乱码） | `50 61 73 73 77 6F 72 64 3A 20 …` = **`"Password:"`** |
| I2C 协议违规 | **56 条**（28 次用写地址读忙 + 14 次 ACK 前重复 START + 14 次 E 边沿数据同时变化） | **0 条** |
| E 脉冲高电平宽度 | 282 µs（合格，≥450 ns） | 317 µs（合格，≥450 ns） |
| 清屏后到下一指令的间隔 | 从未真正执行 `0x01`，无等待 | **4364 µs ≥ 1520 µs**，合格 |

> 旧驱动乱码/不显示的根因**不是** E 脉冲宽度或 I2C 速率，而是：**初始化从未发送“切 4 位模式”的 `0x2` 半字节，LCD 一直停在 8 位模式；而 PCF8574 转接板只接了 D7-D4，8 位模式下 D3-D0 悬空**，于是所有“高低半字节”命令都被当作一条条残缺的 8 位指令执行。

---

## 1. 验证方法（为什么这不是“手写猜测”）

```
mokuai/MyPCF8574.c ─┐
mokuai/MyLCD1602A.c ├─ 用 g++ 原样编译为 C++（.c 文件不改一个字节）
system/Delay.c ─────┘        │
                             ├─ -I verify/fake/  → 影子 stm32f10x.h：
                             │     GPIOB->BSRR/BRR/CRL/IDR、RCC->APB2ENR、
                             │     SysTick->LOAD/VAL/CTRL 都是 C++ 代理对象，
                             │     每次 operator= / operator& 都记录到时间线。
                             └─ verify/src/harness.cpp：虚拟时钟 + 开源漏 I2C 总线
                                + PCF8574 + HD44780 仿真器
```

* **真实源码**：驱动和 `Delay.c` 都是原文件参与编译；基线来自 `git show HEAD:...`（已用 `git hash-object` 校验与 HEAD blob 完全一致，见 `verify/baseline/SOURCE_SHA256.txt`）。
* **虚拟时间**：`Delay_us(x)` 把虚拟时钟推进 `x` µs；所有 GPIO 写、E 边沿、指令锁存都带微秒时间戳，所以可以直接检查 E 宽度和清屏等待。
* **I2C 从机模型**：PCF8574 在 `0x27` 应答、其它地址 NACK；START/STOP/重复 START、每字节 8 位 + 第 9 位 ACK/NACK、读方向的移位输出全部建模。
* **PCF8574 模型**：8 位上电锁存 `0xFF`；写 = 更新锁存；读 = `latch AND 外部驱动`（准双向口）。
* **HD44780 模型**：8/4 位模式、DL 位、**E 下降沿锁存**、RS/RW、指令译码（`0x01/0x02/0x04-0x07/0x08-0x0F/0x10-0x1F/0x20-0x3F/0x40-0x7F/0x80-0xFF`）、AC 自增/减、DDRAM 2×16、CGRAM、显示开/关、**每条指令执行时间（37 µs / 清屏与回原点 1520 µs）与 BUSY 忽略语义**。
* **8 位模式诚实建模**：此时 D3-D0 未连接，锁存字节为 `D7-D4` + “低半字节未定义”。报告给出强制低（`0x0`）和强制高（`0xF`）两种确定值，覆盖悬空脚的两种实际可能。
* **场景**：逐字复刻旧 `User/main.c` 的两句有效调用：
  `LCD1602A_Init();` → `ShowLCDChars(1, {'P','a','s','s','w','o','r','d',':','\0'});`
  （未编译 `User/main.c`，因为它还依赖 OLED/按键；被测驱动 API 与日志完全一致。）

复现：

```powershell
cd D:\桌面\lcd1602a-repo
powershell -NoProfile -ExecutionPolicy Bypass -File verify\run.ps1
```

脚本会重新编译“基线 / 当前”两个可执行文件，跑 6 个用例（每边 3 个：低半字节=0、=F、上电 BUSY=40 ms），日志写到 `verify/out/`。

---

## 2. 用例矩阵总表

| 用例 | 模式（Init 后） | 2 行 | 显示开 | 0x28 | 0x0C | 0x01 | 0x06 | 0x0F | DDRAM 第 1 行（可打印化） | 违规数 | 结果 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| baseline, D3-D0=0x0 | 8-bit | 否 | 否 | ✗ | ✗ | ✗ | ✗ | ✗ | `.P..`..p0.p0.pp.` | 56 | FAIL（预期） |
| baseline, D3-D0=0xF | 8-bit | 是* | **是** | ✗ | ✗ | ✗ | ✗ | **是** | 15 个空格 + `0x0F`（仅末尾 1 个可见字符） | 56 | FAIL（预期） |
| baseline, 上电BUSY=40ms | 8-bit | 否 | 否 | ✗ | ✗ | ✗ | ✗ | ✗ | 同上（`.P..`..p0.p0.pp.`） | 58 | FAIL（预期） |
| current, D3-D0=0x0 | **4-bit** | **是** | **是** | **✓** | **✓** | **✓** | **✓** | ✗ | **`Password:`** + 7 空格 | **0** | **PASS** |
| current, D3-D0=0xF | **4-bit** | **是** | **是** | **✓** | **✓** | **✓** | **✓** | ✗ | **`Password:`** + 7 空格 | **0** | **PASS** |
| current, 上电BUSY=40ms | **4-bit** | **是** | **是** | **✓** | **✓** | **✓** | **✓** | ✗ | **`Password:`** + 7 空格 | **0** | **PASS** |

\* 旧驱动在 D3-D0 悬空读高时，`0x38→0x3F` 恰好把 N 置 1，所以“误打误撞 2 行”，但仍是 8 位总线、且内容不可控。

---

## 3. 基线（旧驱动，git HEAD）详细结果

### 3.1 `LCD1602A_Init()` 结束后的 LCD 状态

| 项 | D3-D0 = 0x0 | D3-D0 = 0xF |
|---|---|---|
| 总线模式 | **8-bit（DL=1）** | **8-bit（DL=1）** |
| 行数 N | 1 行 | 2 行（`0x3F` 副作用） |
| 显示开关 D | 0（关） | 1（开，且 C=1/B=1） |
| AC | `0x70` | `0x7F` |
| DDRAM 第 1 行 hex | `00 00 20 20 20 20 20 20 20 20 20 20 20 20 20 20` | `20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 0F` |
| Init 耗时 | 117 784 µs | 117 784 µs |

### 3.2 旧驱动实际“收到”的指令（去重）

| 字面值 | HD44780 译码 | 来源（意图） | D3-D0=0x0 | D3-D0=0xF |
|---|---|---|---|---|
| `0x30` | FUNCTION SET, DL=1(8-bit), N=0 | `0x38` 的高半字节 | ✓ | — |
| `0x3F` | FUNCTION SET, DL=1(8-bit), N=1, F=1 | `0x38` 的高半字节 | — | ✓ |
| `0x80` | SET DDRAM ADDR = 0x00 | `0x38` 的低半字节 | ✓ | — |
| `0x8F` | SET DDRAM ADDR = 0x0F | `0x38` 的低半字节 | — | ✓ |
| `0x00` | NOP | `0x01/0x06/0x0F` 的高半字节 | ✓ | — |
| `0x0F` | **DISPLAY ON/OFF, D=1,C=1,B=1** | `0x01/0x0F` 的高半字节（误开显示） | — | ✓ |
| `0x10` / `0x1F` | CURSOR/DISPLAY SHIFT | `0x01` 的低半字节 | ✓ | ✓ |
| `0x60` / `0x6F` | SET CGRAM ADDR | `0x06` 的低半字节 | ✓ | ✓ |
| `0xF0` / `0xFF` | SET DDRAM ADDR = 0x70 / 0x7F | `0x0F` 的低半字节 | ✓ | ✓ |

**旧驱动从未正确收到 `0x28`、`0x0C`、`0x01`、`0x06`。** 唯一“歪打正着”的 `0x0F`（开显示）只在 D3-D0 悬空读高时出现，且同时把光标/闪烁都打开（`C=1,B=1`）。

### 3.3 解码出的 I2C 事务（开头几笔，旧驱动）

第一笔 `MyPCF8574SendByte(0x38, 0, 0)`（`verify/out/baseline_undef0x0.log`，原始行）：

```
[   25020 us] I2C START (transaction 1)
[   25290 us]   WRITE addr 0x4E  ACK
[   25590 us]   0x34  (master -> slave)  ACK
[   25590 us] PCF8574 <= 0x34   (P7..P4=D7..D4=0x3  P3=BL=0  P2=E=1  P1=RW=0  P0=RS=0)
[   25872 us]   0x30  (master -> slave)  ACK
[   25872 us] PCF8574 <= 0x30   (P7..P4=D7..D4=0x3  P3=BL=0  P2=E=0  P1=RW=0  P0=RS=0)
[   25872 us] HD44780: latch 0x30 RS=0 mode=8-bit [D3-D0 UNDEFINED]
              -> FUNCTION SET (0x30) DL=1(8-bit) N=0(1-line) F=0  (E width 282 us)
[   25902 us] I2C STOP
[   25932 us] I2C START (transaction 2)
[   26202 us]   WRITE addr 0x4E  ACK
[   26502 us]   0x84  (master -> slave)  ACK
[   26502 us] PCF8574 <= 0x84   (... P2=E=1 ...)
[   26784 us]   0x80  (master -> slave)  ACK
[   26784 us] PCF8574 <= 0x80   (... P2=E=0 ...)
[   26784 us] HD44780: latch 0x80 RS=0 mode=8-bit [D3-D0 UNDEFINED]
              -> SET DDRAM ADDRESS (0x80) AC=0x00  (E width 282 us)
[   26814 us] I2C STOP
```

即：代码以为在发“0x38 = 8 位/2 行”，总线上实际是 **`0x3?`（保持 8 位）+ `0x8?`（设 DDRAM 地址）**；
**关键的那一个 `0x2` 半字节从未出现**，所以 DL 永远是 1。

“清屏 0x01”被拆成两条无效的 8 位指令：

```
[   108342 us] PCF8574 <= 0x04   (... P2=E=1 ...)        # 高半字节 0x0, E 拉高
[   108624 us] PCF8574 <= 0x00   (... P2=E=0 ...)        # E 下降沿
[   108624 us] HD44780: latch 0x00 RS=0 mode=8-bit ... -> NOP/unused (0x00)
[   109254 us] PCF8574 <= 0x14   (... P2=E=1 ...)        # 低半字节 0x1, E 拉高
[   109536 us] PCF8574 <= 0x10   (... P2=E=0 ...)        # E 下降沿
[   109536 us] HD44780: latch 0x10 RS=0 mode=8-bit ... -> CURSOR/DISPLAY SHIFT (0x10)
```

### 3.4 旧驱动的“读忙标志”：整段是非法总线事务

`ReadAndJudgeWhileWaiting_BUSY_Flag()` 每次调用产生 2 条违规事务（共 14 次调用 → 28 条）。原始日志：

```
[   105492 us] I2C START (transaction 7)
[   105762 us]   WRITE addr 0x4E  ACK          # 写地址！不是读地址 0x4F
[   106062 us]   0x06  (master -> slave)  ACK   # P2=E=1, P1=RW=1, P0=RS=0
[   106062 us] PCF8574 <= 0x06   (P7..P4=0x0  P2=E=1  P1=RW=1  P0=RS=0)
[   106342 us]   0xFF  (master -> slave)  ACK   # 主机的 8 个“读”时钟，PCF8574 不驱动 SDA
[   106342 us] ** VIOLATION [I2C] busy-flag read attempted with the WRITE address ...
[   106342 us] PCF8574 <= 0xFF   (P7..P4=0xF  P2=E=1  P1=RW=1  P0=RS=1)
[   106582 us] ** VIOLATION [I2C] repeated START before the ACK clock:
              8 data bits (0x01) were clocked where a single ACK/NACK bit belongs
[   106582 us] I2C repeated START
[   107152 us]   0x06  ACK ...
[   107712 us]   0x01  ACK                    # SendByte(0x00) 的 8 个时钟被从机当成新数据字节
[   107712 us] PCF8574 <= 0x01   (... P2=E=0 ...)
[   107712 us] ** VIOLATION [HD44780] E falling edge with simultaneous change of
              RS/D7-D4 (was RS=1 D=0xF, now RS=1 D=0x0) - byte may be garbage
[   107742 us] I2C STOP
```

要点（全部是硬事实，不是推断）：

1. **整条 trace 里没有一次读地址 `0x4F`**：`READ_TXNS_0x4F=0`、`I2C_BYTES_READ=0`。忙标志/DDRAM 根本不可能被读回。
2. 读完 8 个时钟后本应只发 **1 个 ACK/NACK 位**，旧代码却发了 `MyI2C_SendByte(0x00)`（8 个时钟），其中第 1 个时钟被当成 ACK，剩下 7 个被从机当成新数据字节的开头；接着又在 ACK 位之前发了重复 START → 从机看到“半个字节”。
3. 读之前写的是 `0x06`，**P4-P7 锁存为 0**（PCF8574 强驱动低），即使换成读地址 `0x4F`，读回的也是自己的输出，而不是 LCD 的忙标志。正确做法是写 `0xF6/0xFE`（P4-P7=1、RW=1）再读 `0x4F`。
4. 静态证明忙标志判断恒为假：`MyLCD1602A.c` 中
   ```c
   Hdata = Hdata & 0x0F;   /* 值 ≤ 0x0F */
   Hdata &= 0x80;          /* 与 0x80 为 0 —— 恒等于 0 */
   if (Hdata == 0x80) BUSY = 1;
   ```
   `Hdata` 先被 `&0x0F` 再 `&0x80`，数学上**永远为 0**，所以 `while(BUSY)` 第一次就退出，忙等待函数从不等待。

### 3.5 协议违规分类（旧驱动，D3-D0=0x0 与 0xF 相同）

| 类别 | 次数 | 说明 |
|---|---|---|
| 用写地址尝试读忙标志 | 28 | 14 次 `ReadAndJudge...` 调用 × 2 段；返回 `0xFF` |
| ACK 位前重复 START（多打 7 个时钟） | 14 | 每次 `ReadAndJudge...` 的第二段 |
| E 下降沿时 RS/D7-D4 同时变化（锁存数据不确定） | 14 | 从机会把 `0x01` 当成数据字节锁存，同时写 DDRAM |
| 被 BUSY 忽略的指令（仅 40 ms 上电用例） | 2 | 第一条 `0x38` 到达时 `t≈26.5 ms < 40 ms` |
| **合计** | **56**（上电用例 58） | 新驱动为 **0** |

### 3.6 为什么显示“有时乱码、有时什么都不显示”

8 位模式下 D3-D0 没接，读到的是悬空电平：

* 悬空**读低**（`undef=0x0`）：`0x01/0x06/0x0F` 的高半字节都变成 `0x00 = NOP`，显示开指令 `0x0F` 永远到不了 → **D=0，屏幕不显示任何内容**（“有时什么都不显示”）。
* 悬空**读高**（`undef=0xF`）：`0x0F` 高半字节变成 `0x0F = DISPLAY ON`，显示被误打开；但 DDRAM 里是 `0x5F/0x0F/0x6F…` 之类的错误字节，且 `0x80` 变成 `0x8F` 把 AC 设到 `0x0F`，后续数据写到不可见区域 → **只亮屏、几乎全是空白/个别乱码字符**（“乱码”）。

这就是“同一份代码，换一块 PCF8574 转接板/温度/供电就表现不同”的原因：决定结果是悬空脚读到的电平，而不是程序逻辑。

### 3.7 旧驱动根因清单（按对现象贡献排序）

1. **初始化没有进入 4 位模式**：`LCD1602A_Init()` 发送 `0x38` 三次，但 `MyPCF8574SendByte()` 把每个字节拆成高/低两个半字节（`0x34→0x30`、`0x84→0x80`）。8 位模式下这两次 E 下降沿各锁存一条 8 位指令，**`0x2` 这个“切 4 位”的半字节从未发送**，DL 位始终为 1。
2. **8 位模式 + D3-D0 未连接**：所有后续“两个半字节”的命令都被当成 2 条残缺 8 位指令，且低 4 位由悬空脚决定 → 随机性乱码。
3. **读忙标志函数非法**：写地址 `0x4E` 做读、P4-P7 未释放、读完发整字节而非 1 个 NACK、ACK 前重复 START、且判断式恒为 0。它既没等到忙，又在每行字符前搅乱总线。
4. **清屏 `0x01` 从未执行**，因此 1.52 ms 等待问题根本没机会暴露；`0x0F` 开显示被拆成 `NOP + 设 DDRAM`，显示开/关不可控。
5. **上电等待 25 ms < 手册 40 ms**：在“LCD 上电忙 40 ms”敏感性用例中，第一条 `0x38` 被忽略（`IGNORED_BUSY=2`）。但它只是三条重复命令中的第一条，**不改变最终 8 位模式/不显示的结论**——所以它不是主因，只是次因。
6. **E 脉冲宽度不是问题**：旧驱动 E 高电平 282 µs（最短）/1650 µs（最长），都远大于手册 ≥450 ns 要求。I2C 速率约 33 kHz，也在 PCF8574 100 kHz 上限内。

---

## 4. 修复后（当前工作区驱动）详细结果

### 4.1 `LCD1602A_Init()` 的指令序列（仿真译码，全部一次成功）

| 时间 (µs) | 锁存值 | 模式（收到时） | 译码 |
|---|---|---|---|
| 53 449 | `0x30` | 8-bit（D3-D0 未定义） | FUNCTION SET，DL=1（保持 8 位） |
| 59 131 | `0x30` | 8-bit（D3-D0 未定义） | FUNCTION SET，DL=1 |
| 60 813 | `0x30` | 8-bit（D3-D0 未定义） | FUNCTION SET，DL=1 |
| 62 495 | **`0x20`** | 8-bit（D3-D0 未定义） | **FUNCTION SET，DL=0 → 从这条起进入 4 位模式** |
| 64 859 | `0x28` | **4-bit** | FUNCTION SET，DL=0，**N=1（2 行）**，F=0 |
| 67 223 | `0x08` | 4-bit | DISPLAY ON/OFF：D=0（先关显示，避免闪乱码） |
| 69 587 | **`0x01`** | 4-bit | **CLEAR DISPLAY**（立即置 BUSY 1520 µs） |
| 73 951 | **`0x06`** | 4-bit | ENTRY MODE SET：I/D=1（AC 自增），S=0 |
| 76 315 | **`0x0C`** | 4-bit | **DISPLAY ON/OFF：D=1，C=0，B=0** |

命令顺序检查：`0x28`(idx 4) → `0x01`(idx 6) → `0x06`(idx 7) → `0x0C`(idx 8)，**PASS**（中间 idx 5 是 `0x08`，先关显示再清屏，符合手册推荐流程）。
`Init` 结束后：**4 位、2 行、显示开、AC=0x00**，DDRAM 全为空格；`Init` 耗时 77 385 µs（比旧驱动快 40 400 µs）。

### 4.2 4 位写入的 I2C 现场（`0x28`）

```
[   63575 us] I2C START (transaction 18)
[   63710 us]   WRITE addr 0x4E  ACK
[   63860 us]   0x2C  (master -> slave)  ACK    # D7-D4=0x2, E=1
[   63860 us] PCF8574 <= 0x2C   (P7..P4=0x2  P3=BL=1  P2=E=1  P1=RW=0  P0=RS=0)
[   63875 us] I2C STOP
[   63892 us] I2C START (transaction 19)
[   64027 us]   WRITE addr 0x4E  ACK
[   64177 us]   0x28  (master -> slave)  ACK    # E=0 → 锁存高半字节
[   64177 us]   0x28  ... P2=E=0 ...
[   64177 us] HD44780: [4-bit] latch HIGH nibble 0x2  (E width 317 us)
[   64192 us] I2C STOP
[   64257 us] I2C START (transaction 20)       # 低半字节: D7-D4=0x8, E=1
[   64392 us]   WRITE addr 0x4E  ACK
[   64542 us]   0x8C  (master -> slave)  ACK
[   64542 us] PCF8574 <= 0x8C   (P7..P4=0x8  P3=BL=1  P2=E=1  P1=RW=0  P0=RS=0)
[   64557 us] I2C STOP
[   64574 us] I2C START (transaction 21)
[   64709 us]   WRITE addr 0x4E  ACK
[   64859 us]   0x88  (master -> slave)  ACK    # E=0 → 锁存低半字节 = 完整字节 0x28
[   64859 us] HD44780: latch 0x28 RS=0 mode=4-bit
              -> FUNCTION SET (0x28) DL=0(4-bit) N=1(2-line) F=0  (E width 317 us)
[   64874 us] I2C STOP
```

### 4.3 `ShowLCDChars(1,"Password:")` 后

* 第 1 行 DDRAM hex：`50 61 73 73 77 6F 72 64 3A 20 20 20 20 20 20 20`
* 第 1 行显示：**`"Password:"` + 7 个空格补齐**
* 第 2 行：16 个空格（未被调用）
* AC 结束于 `0x10`，显示开，4 位 2 行

### 4.4 时序合规

| 检查项 | 实测 | 手册要求 | 结论 |
|---|---|---|---|
| E 高电平宽度（48 个脉冲） | **317 µs**（min=max） | ≥450 ns | PASS（余量很大：E 宽由两次 I2C 事务决定，不是问题） |
| 清屏 `0x01` → 下一条指令 `0x06` | **4364 µs** | ≥1520 µs | PASS |
| 4 位命令后固定延时 | 50 µs（代码 `Delay_us(50)`） | ≥37 µs | PASS |
| 上电等待 | 约 53.4 ms 才发第一条指令（`Delay_ms(50)` + 探测） | >40 ms | PASS |
| I2C 速率 | 半周期 5 µs → 约 66.7 kHz | ≤100 kHz（PCF8574 标准模式） | PASS |
| 协议违规 | **0** | 0 | PASS |

`ADDR_NACKS=7` 是 `MyPCF8574_AutoAddress()` 正常扫描 `0x20..0x26` 时“无人应答”，随后 `0x27` 应答；**不是错误**。

---

## 5. 敏感性与模型局限（诚实声明）

* **悬空低半字节**：8 位模式下 D3-D0 未连接，模型分别用 `0x0` / `0xF` 两个确定值覆盖。两种情况下旧驱动都停在 8 位、都收不到 4 条关键指令；新驱动两种情况下都 PASS（因为它在“低半字节未定义”时只依赖高半字节的 DL 位进入 4 位模式，之后低半字节由真实数据线决定）。
* **上电 BUSY**：默认 0（假设 LCD 先于 MCU 稳定）。增加 40 ms 敏感性用例：旧驱动仅丢失第一条重复的 `0x38`，结论不变；新驱动 `Delay_ms(50)` 已覆盖。
* **模型边界**：这是**逻辑级**仿真，不建模 LCD 模拟对比度/背光电路、LCD 上电 DDRAM 随机内容（统一填 `0x20`）、CGRAM 字模显示、AC 在 2 行边界的精确回绕（用 `&0x7F`）。模型只承诺“指令是否被正确接收/执行、DDRAM 内容、E 边沿与执行时间”。
* **读忙标志**：模型支持 RW=1 读（忙标志 + AC 高/低半字节），但新驱动故意不读（用固定延时），所以本报告不评价“读忙”实现。
* **未编译 `User/main.c`**：它依赖 OLED/按键模块；硬件无关验证只需驱动 API。新驱动新增的 `LCD1602A_ShowString()` 走同一套 `WriteNibble/WriteChar` 底层，本报告用旧接口 `ShowLCDChars(1,"Password:")` 做同场景对比。

---

## 6. 文件与源码指纹

```
verify/
├─ RESULTS.md                  ← 本报告
├─ run.ps1                     ← 一键重跑（编译基线+当前，跑 6 个用例）
├─ fake/                       ← 影子头文件（代理寄存器 + StdPeriph 子集）
│  ├─ stm32f10x.h  stm32f10x_gpio.h  stm32f10x_i2c.h
│  └─ stm32f10x_rcc.h  stm32f10x_tim.h
├─ src/harness.cpp             ← 虚拟时钟 + I2C/PCF8574/HD44780 仿真器 + 判定
├─ baseline/                   ← 旧驱动冻结副本（git HEAD 0d8444f，非工作区）
│  ├─ mokuai/{MyPCF8574,MyLCD1602A}.{c,h}  system/Delay.{c,h}  User/main.c
│  └─ SOURCE_SHA256.txt        ← 与 HEAD blob 一致性校验
└─ out/                        ← 每次运行的日志 + source-hashes.txt
```

基线（git HEAD blob，`blobmatch=True`）：

| 文件 | git blob |
|---|---|
| `mokuai/MyPCF8574.c` | `d3895dbd25a29c63b30174c16413b6632bdcb0bf` |
| `mokuai/MyPCF8574.h` | `057798cd9a1b208560ede9c8a1459836a0ab12ae` |
| `mokuai/MyLCD1602A.c` | `1adf19a8c5265a9814b61c820db5e303f8e71d80` |
| `mokuai/MyLCD1602A.h` | `34b52b00027efbc4293d7341e7abd4ea903a0b8e` |
| `system/Delay.c` | `009ba2f04f42e322da4d8cdfa7f9795ae70871e0` |
| `system/Delay.h` | `cef7c3c0e9176e6f54a4467d559bf0357ef4933a` |
| `User/main.c` | `4e8c468fdf988db2ad4fe7fc19c7d5aa1d063de7` |

当前被测源码 SHA-256 见 `verify/out/source-hashes.txt`（每次 `run.ps1` 重新生成），本次运行值：

```
mokuai/MyPCF8574.c   f151bbb94a4ecbb1ea3ffda317eb02c3287066d12a370d9f4a2a14945078e77a
mokuai/MyPCF8574.h   ce8acc9f7865238757a56f87deebf73b28bfcaec164b79ee7b2cb9e1b5f12469
mokuai/MyLCD1602A.c  c94f39b2078b8c728a8a1408241d8e260d4eea47b6aec846246690a3bcc70c7a
mokuai/MyLCD1602A.h  1c5978f8cca3670aa2bd795cf1747c2f69acdc8e409fdeb9d0e7cc1876914240
system/Delay.c       434b938f049e9859c25735a463603c04240e0264a01eb69566b9814da5c64519
system/Delay.h       f0bf8cbd91c797ef0e64860ae1d05dc310481fd97e72fc487401b131af1346ad
User/main.c          41bef0ba65bbf44e561e9dbe0ec35e74010909027b25ff782e957cf221d1376d
```

> 注：`verify/baseline/` 的副本按 git blob 原样保存（LF），工作区文件是 CRLF，因此同一文件的 SHA-256 不同；基线一致性以 `verify/baseline/SOURCE_SHA256.txt` 的 `blobmatch=True` 为准。

---

## 7. 复现与判定

```powershell
cd D:\桌面\lcd1602a-repo
powershell -NoProfile -ExecutionPolicy Bypass -File verify\run.ps1
```

输出末尾会给出 6 行 `RESULT=`：

```
baseline_undef0x0               RESULT=FAIL   LCD_MODE_FINAL=8-bit  ...
baseline_undef0xF               RESULT=FAIL   LCD_MODE_FINAL=8-bit  ...
baseline_undef0x0_poweron40ms   RESULT=FAIL   LCD_MODE_FINAL=8-bit  ...
current_undef0x0                RESULT=PASS   LCD_MODE_FINAL=4-bit  DDRAM_ROW1_TRIMMED=Password:
current_undef0xF                RESULT=PASS   LCD_MODE_FINAL=4-bit  DDRAM_ROW1_TRIMMED=Password:
current_undef0x0_poweron40ms    RESULT=PASS   LCD_MODE_FINAL=4-bit  DDRAM_ROW1_TRIMMED=Password:
```

**基线 FAIL 是预期且被证明的缺陷；新驱动 8/8 项检查 PASS**（4-bit/2 行、四条关键指令、指令顺序、显示开、DDRAM=`Password:`、E 宽度、清屏 1.52 ms、零协议违规）。
