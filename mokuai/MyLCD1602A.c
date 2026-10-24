#include "stm32f10x.h"
#include "Delay.h"
#include "MyPCF8574.h"
#include "MyLCD1602A.h"

/* ============================================================================
 *  LCD1602A (HD44780 兼容) 驱动, 4 位数据总线, 经 PCF8574 转接板
 *
 *  原代码为什么会乱码 / 时好时坏, 以及这里的修法:
 *
 *  [1] 初始化序列不对 (乱码主因)
 *      原代码发的是 0x38, 而且经过"4 位拆分"函数, 实际总线上出现的是
 *      0x3 -> 0x8 -> 0x3 -> 0x8 ... 而 LCD 上电时还处在 8 位模式,
 *      0x8X 会被当成"设置 DDRAM 地址 0x00"执行, 于是:
 *        - 永远进不了 4 位模式;
 *        - 之后的 0x01(清屏)/0x0F(开显示) 也都被拆成两个 8 位指令, 全部失效。
 *      LCD 一直被当成 8 位模式用, 而 D3~D0 根本没接 -> 收到的是残缺字节,
 *      所以显示出来是乱码。
 *      正确做法(数据手册): 上电 -> 0x3, 0x3, 0x3 -> 0x2(切 4 位) -> 0x28 ...
 *
 *  [2] 上电等待时间不够 (时好时坏的原因之一)
 *      手册要求 VDD 上升后 > 40ms 才能发指令, 原代码只 Delay_ms(25)。
 *      电源上升速度、电容大小不同, 就会表现为"有时能显示, 有时不能"。
 *
 *  [3] 读忙标志的函数是错的 (会破坏总线, 时好时坏/乱码的原因之一)
 *      原 ReadAndJudgeWhileWaiting_BUSY_Flag():
 *        - 读之前没有把 P4~P7 写成 1(输入), PCF8574 仍在强驱动, 读回的是自己的输出;
 *        - 读完一个字节后应该发 NACK + STOP, 它却发了整整一个字节 0x00,
 *          等于在总线上多打了 8 个时钟, 从机会把它当成后续数据;
 *        - 中间还出现了没有 STOP 的连续 START。
 *      每一行字符前都调用一次, 于是总线经常被搅乱, 表现为随机乱码/闪断。
 *      而且最后 `Hdata &= 0x0F; Hdata &= 0x80;` 恒等于 0, 忙判断根本不生效,
 *      等于既破坏了总线, 又没起到等待作用。
 *      -> 本驱动不再读忙标志, 改为按手册给足固定延时(LiquidCrystal_I2C 等
 *         成熟库也是这么做的), 总线只有"写", 简单可靠。
 *
 *  [4] 清屏后没有等到 1.52ms 就发下一条指令 -> 清屏可能只执行了一半。
 *
 *  [5] ShowLCDChars 的循环判断放在 ++T 之后, 且把 '\0' 当字符写进了 DDRAM。
 * ========================================================================== */

#define LCD_ERR_NONE      0u
#define LCD_ERR_I2C       1u   /* I2C 无应答 / 写失败 */
#define LCD_ERR_NODEV     2u   /* 总线上找不到 PCF8574 */

static uint8_t s_err = LCD_ERR_NONE;

/* 写一个完整字节: 先发高 4 位, 再发低 4 位, 每个半字节各产生一次 E 脉冲 */
static void LCD_WriteByte(uint8_t value, uint8_t rs)
{
    if (!MyPCF8574_WriteNibble((uint8_t)(value & 0xF0u), rs))
    {
        s_err = LCD_ERR_I2C;
        return;
    }
    if (!MyPCF8574_WriteNibble((uint8_t)(value << 4), rs))
    {
        s_err = LCD_ERR_I2C;
        return;
    }
}

/* ============================ 初始化 ==================================== */

void LCD1602A_Init(void)
{
    uint8_t found;
    uint8_t retry;

    s_err = LCD_ERR_NONE;

    MyPCF8574_I2C_Init();

    /* 上电后必须等 > 40ms 再发第一条指令 */
    Delay_ms(50);

    /* 自动识别 0x27 / 0x3F。
     * 刚上电时电源可能还在爬升(特别是 USB 供电/大电容), 探不到就重试几次,
     * 这是"有时能显示有时不能"的一种常见情形。 */
    found = 0u;
    for (retry = 0u; (retry < 3u) && (found == 0u); retry++)
    {
        found = MyPCF8574_AutoAddress();
        if (found == 0u)
        {
            MyI2C_BusRecover();
            Delay_ms(100);
        }
    }
    if (found == 0u)
    {
        s_err = LCD_ERR_NODEV;
        MyPCF8574_SetAddress(PCF8574_ADDR7);
    }

    /* PCF8574 上电时 8 个输出默认全是 1(RS=RW=EN=1, D=0xF), 先写成确定状态:
       RS=0 RW=0 EN=0 D=0, 只保留背光位 */
    MyPCF8574_SetBacklight(1u);
    Delay_ms(1);

    /* --- 第 1 步: 让 LCD 完成复位, 此时它还是 8 位模式, 只发高 4 位 --- */
    (void)MyPCF8574_WriteNibble(0x30u, 0u);
    Delay_ms(5);                        /* 手册要求 > 4.1ms */
    (void)MyPCF8574_WriteNibble(0x30u, 0u);
    Delay_ms(1);                        /* > 100us */
    (void)MyPCF8574_WriteNibble(0x30u, 0u);
    Delay_ms(1);

    /* --- 第 2 步: 发 0x2X, 其中 DL=0, 从这一条起 LCD 进入 4 位模式 --- */
    (void)MyPCF8574_WriteNibble(0x20u, 0u);
    Delay_ms(1);

    /* --- 第 3 步: 4 位模式下发完整指令 --- */
    LCD_WriteByte(0x28u, 0u);           /* 4 位总线, 双行显示, 5x8 点阵 */
    Delay_ms(1);
    LCD_WriteByte(0x08u, 0u);           /* 先关显示, 避免初始化时闪乱码 */
    Delay_ms(1);
    LCD_WriteByte(0x01u, 0u);           /* 清屏 */
    Delay_ms(3);                        /* 清屏要 1.52ms */
    LCD_WriteByte(0x06u, 0u);           /* 输入模式: 光标右移, 屏幕不滚动 */
    Delay_ms(1);
    LCD_WriteByte(0x0Cu, 0u);           /* 开显示, 关光标, 不闪烁 */
    Delay_ms(1);

    if ((s_err == LCD_ERR_NONE) && (MyPCF8574_LastStatus() != PCF8574_OK))
    {
        s_err = LCD_ERR_I2C;
    }
}

uint8_t LCD1602A_IsPresent(void)
{
    return (uint8_t)((s_err == LCD_ERR_NONE) ? 1u : 0u);
}

/* ============================ 基本指令 ================================== */

void LCD1602A_Command(uint8_t cmd)
{
    LCD_WriteByte(cmd, 0u);
}

void LCD1602A_Clear(void)
{
    LCD_WriteByte(0x01u, 0u);
    Delay_ms(3);                        /* 清屏 1.52ms */
}

void LCD1602A_ReturnHome(void)
{
    LCD_WriteByte(0x02u, 0u);
    Delay_ms(3);                        /* 回原点 1.52ms */
}

void LCD1602A_Display(uint8_t on, uint8_t cursor, uint8_t blink)
{
    uint8_t cmd;

    cmd = 0x08u;                        /* 显示关, 光标关, 闪烁关 */
    if (on != 0u)     { cmd |= 0x04u; }
    if (cursor != 0u) { cmd |= 0x02u; }
    if (blink != 0u)  { cmd |= 0x01u; }
    LCD_WriteByte(cmd, 0u);
}

/* ============================ 光标与显示 ================================ */

void LCD1602A_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t addr;

    if (col < 1u)  { col = 1u; }
    if (col > 16u) { col = 16u; }

    if (row == 2u)
    {
        addr = (uint8_t)(0x40u + col - 1u);   /* 第 2 行 DDRAM 起始 0x40 */
    }
    else
    {
        addr = (uint8_t)(0x00u + col - 1u);   /* 第 1 行 DDRAM 起始 0x00 */
    }
    LCD_WriteByte((uint8_t)(0x80u | addr), 0u);
}

void LCD1602A_WriteChar(char ch)
{
    LCD_WriteByte((uint8_t)ch, 1u);
}

void LCD1602A_WriteString(const char *str)
{
    while (*str != '\0')
    {
        LCD_WriteByte((uint8_t)(*str), 1u);
        if (s_err != LCD_ERR_NONE)
        {
            return;
        }
        str++;
    }
}

void LCD1602A_ShowString(uint8_t row, uint8_t col, const char *str)
{
    uint8_t used = 0u;

    if ((col < 1u) || (col > 16u))
    {
        return;
    }

    LCD1602A_SetCursor(row, col);
    while ((*str != '\0') && ((uint8_t)(col + used) <= 16u))
    {
        LCD1602A_WriteChar(*str);
        str++;
        used++;
        if (s_err != LCD_ERR_NONE)
        {
            return;
        }
    }
    /* 用空格补齐本行剩余位置, 避免上一次的长字符留下残影 */
    while ((uint8_t)(col + used) <= 16u)
    {
        LCD1602A_WriteChar(' ');
        used++;
        if (s_err != LCD_ERR_NONE)
        {
            return;
        }
    }
}

void LCD1602A_Backlight(uint8_t on)
{
    MyPCF8574_SetBacklight(on);
    Delay_us(50);
}

/* ============================ CGRAM / 对比度自检 ======================== */

/* 自定义字符: 写 CGRAM 的 slot 号(0~7), 每个字符 8 行, 每行用低 5 位 */
void LCD1602A_DefineChar(uint8_t slot, const uint8_t rows[8])
{
    uint8_t i;

    if (slot > 7u)
    {
        return;
    }
    /* 设置 CGRAM 地址: 0x40 | (slot * 8), 然后连续写 8 行点阵 */
    LCD_WriteByte((uint8_t)(0x40u | (uint8_t)(slot << 3)), 0u);
    for (i = 0u; i < 8u; i++)
    {
        LCD_WriteByte((uint8_t)(rows[i] & 0x1Fu), 1u);
    }
}

/* 对比度自检: 第 1 行 16 个实心黑块, 第 2 行 "CONTRAST TEST"。
 * 判断标准: 黑块应该是"纯黑"的; 如果发灰/发绿、边缘模糊, 说明 V0 偏了。 */
void LCD1602A_SelfTest(void)
{
    uint8_t block[8];
    uint8_t i;

    for (i = 0u; i < 8u; i++)
    {
        block[i] = 0x1Fu;               /* 5 个点全亮 = 实心块 */
    }
    LCD1602A_DefineChar(0u, block);

    LCD1602A_Clear();
    LCD1602A_SetCursor(1u, 1u);
    for (i = 0u; i < 16u; i++)
    {
        LCD1602A_WriteChar('\0');       /* 字符 0 = 刚才定义的实心块 */
    }
    LCD1602A_ShowString(2u, 1u, "CONTRAST TEST");
}

/* ============================ 其它 ====================================== */

uint8_t LCD1602A_LastError(void)
{
    return s_err;
}

/* 不读忙标志, 用延时保证 LCD 已经执行完上一条指令 */
void LCD1602A_WaitReady(void)
{
    Delay_ms(2);
}

/* 兼容旧名字: 原函数是"读忙标志并等待", 现在改成纯延时 */
void ReadAndJudgeWhileWaiting_BUSY_Flag(void)
{
    LCD1602A_WaitReady();
}

/* 兼容旧接口: 显示一整行, 不足 16 字符补空格 */
void ShowLCDChars(uint8_t row, const char *Character)
{
    uint8_t i;

    LCD1602A_SetCursor(row, 1u);
    for (i = 0u; (i < 16u) && (Character[i] != '\0'); i++)
    {
        LCD1602A_WriteChar(Character[i]);
        if (s_err != LCD_ERR_NONE)
        {
            return;
        }
    }
    for (; i < 16u; i++)
    {
        LCD1602A_WriteChar(' ');
        if (s_err != LCD_ERR_NONE)
        {
            return;
        }
    }
}
