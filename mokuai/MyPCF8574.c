#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "Delay.h"
#include "MyPCF8574.h"

/* ============================================================================
 *  软件 I2C + PCF8574 输出驱动
 *
 *  相比原来的写法, 这里修掉的问题:
 *   1) 原来直接写 GPIOB->CRL = 0x77000000, 会顺手把 PB0~PB5 的配置全清成
 *      模拟输入(改坏了同组的其它引脚)。这里改用标准库 GPIO_Init, 只动 PB6/PB7。
 *   2) 原来每次写引脚都 Delay_us(10), 一位要 30us, 一整条 I2C 事务接近 1ms,
 *      16 个字符要几十毫秒。这里改成 5us, 约 66kHz, 稳定且快一倍。
 *   3) 原来用 ^= 去"翻转" E 脚, 逻辑上依赖"该位本来是 0"; 一旦背光位/RS 位
 *      变化就会算错。这里改成显式按位或, 任何输入都不会算错。
 *   4) 原来完全不管从机应答。现在每次写都检查 ACK, 没有应答会报错,
 *      方便一眼看出是接线/地址问题还是时序问题。
 *   5) 增加总线恢复: 如果 MCU 复位时从机正卡在某个字节中间, 会把 SDA 一直拉低,
 *      表现为"有时能显示, 有时死机"。上电时打 9 个 SCL 可以把从机"喂"完一个字节。
 * ========================================================================== */

/* 半个 I2C 时钟周期的延时。5us -> SCL 约 66kHz (PCF8574 标准模式上限 100kHz) */
#define I2C_HALF_PERIOD_US   5u

static uint8_t s_addr7 = PCF8574_ADDR7;   /* 当前使用的 7 位地址 */
static uint8_t s_status = PCF8574_OK;     /* 最近一次错误码 */
static uint8_t s_backlight = 1u;          /* 背光状态 */

/* ============================ 软件 I2C 底层 ============================== */

void MyI2C_W_SCL(uint8_t BitValue)
{
    if (BitValue != Bit_RESET)
    {
        GPIOB->BSRR = GPIO_Pin_6;         /* 开漏输出写 1 = 释放总线(由上拉电阻拉高) */
    }
    else
    {
        GPIOB->BRR = GPIO_Pin_6;
    }
    Delay_us(I2C_HALF_PERIOD_US);
}

void MyI2C_W_SDA(uint8_t BitValue)
{
    if (BitValue != Bit_RESET)
    {
        GPIOB->BSRR = GPIO_Pin_7;
    }
    else
    {
        GPIOB->BRR = GPIO_Pin_7;
    }
    Delay_us(I2C_HALF_PERIOD_US);
}

uint8_t MyI2C_R_SDA(void)
{
    uint8_t BitValue;
    BitValue = ((GPIOB->IDR & GPIO_Pin_7) != 0u) ? 1u : 0u;
    Delay_us(I2C_HALF_PERIOD_US);
    return BitValue;
}

void MyI2C_Start(void)
{
    MyI2C_W_SDA(1);
    MyI2C_W_SCL(1);
    MyI2C_W_SDA(0);                       /* SCL 高电平期间 SDA 下降 = 起始 */
    MyI2C_W_SCL(0);
}

void MyI2C_Stop(void)
{
    MyI2C_W_SDA(0);
    MyI2C_W_SCL(1);
    MyI2C_W_SDA(1);                       /* SCL 高电平期间 SDA 上升 = 终止 */
}

void MyI2C_SendByte(uint8_t Byte)
{
    uint8_t i;
    for (i = 0u; i < 8u; i++)
    {
        MyI2C_W_SDA((uint8_t)((Byte & (0x80u >> i)) ? 1u : 0u));
        MyI2C_W_SCL(1);
        MyI2C_W_SCL(0);
    }
}

uint8_t MyI2C_ReceiveByte(void)
{
    uint8_t i;
    uint8_t Byte = 0x00u;
    MyI2C_W_SDA(1);                       /* 先释放 SDA, 交给从机驱动 */
    for (i = 0u; i < 8u; i++)
    {
        MyI2C_W_SCL(1);
        if (MyI2C_R_SDA())
        {
            Byte |= (uint8_t)(0x80u >> i);
        }
        MyI2C_W_SCL(0);
    }
    return Byte;
}

void MyI2C_SendAck(uint8_t AckBit)
{
    MyI2C_W_SDA(AckBit);
    MyI2C_W_SCL(1);
    MyI2C_W_SCL(0);
}

uint8_t MyI2C_ReceiveAck(void)
{
    uint8_t AckBit;
    MyI2C_W_SDA(1);
    MyI2C_W_SCL(1);
    AckBit = MyI2C_R_SDA();               /* 0 = 从机应答, 1 = 无人应答 */
    MyI2C_W_SCL(0);
    return AckBit;
}

/* 若上一次传输被打断(比如 MCU 中途复位), 从机会一直把 SDA 拉低,
 * 之后所有传输都失败。打最多 9 个 SCL 让从机把当前字节收完, 再补一个 STOP。 */
void MyI2C_BusRecover(void)
{
    uint8_t i;
    MyI2C_W_SDA(1);
    for (i = 0u; i < 9u; i++)
    {
        MyI2C_W_SCL(1);
        MyI2C_W_SCL(0);
    }
    MyI2C_W_SDA(0);
    MyI2C_W_SCL(1);
    MyI2C_W_SDA(1);
}

/* ============================ 初始化 ==================================== */

void MyPCF8574_I2C_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    Delay_us(2);                          /* 等外设时钟稳定 */

    gpio.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_Out_OD;   /* 开漏: 总线高电平靠转接板上的上拉电阻 */
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    GPIOB->BSRR = GPIO_Pin_6 | GPIO_Pin_7; /* 先释放总线, 保持空闲(都为高) */
    Delay_us(10);

    MyI2C_BusRecover();
    s_status = PCF8574_OK;
}

/* ============================ 器件探测 ================================== */

uint8_t MyPCF8574_Probe(uint8_t addr7)
{
    uint8_t nack;

    MyI2C_Start();
    MyI2C_SendByte((uint8_t)(addr7 << 1));
    nack = MyI2C_ReceiveAck();
    MyI2C_Stop();

    return (uint8_t)((nack == 0u) ? 1u : 0u);
}

uint8_t MyPCF8574_AutoAddress(void)
{
    uint8_t a;

    for (a = 0x20u; a <= 0x27u; a++)
    {
        if (MyPCF8574_Probe(a))
        {
            s_addr7 = a;
            return a;
        }
    }
    for (a = 0x38u; a <= 0x3Fu; a++)
    {
        if (MyPCF8574_Probe(a))
        {
            s_addr7 = a;
            return a;
        }
    }
    return 0u;
}

void MyPCF8574_SetAddress(uint8_t addr7)
{
    s_addr7 = (uint8_t)(addr7 & 0x7Fu);
}

uint8_t MyPCF8574_GetAddress(void)
{
    return s_addr7;
}

/* ============================ 端口/背光 ================================= */

uint8_t MyPCF8574_WritePort(uint8_t value)
{
    uint8_t nack;

    MyI2C_Start();
    MyI2C_SendByte((uint8_t)(s_addr7 << 1));
    nack = MyI2C_ReceiveAck();
    if (nack != 0u)
    {
        MyI2C_Stop();
        s_status = PCF8574_ERR_NACK;
        return 0u;
    }

    MyI2C_SendByte(value);
    nack = MyI2C_ReceiveAck();
    MyI2C_Stop();
    if (nack != 0u)
    {
        s_status = PCF8574_ERR_NACK;
        return 0u;
    }

    s_status = PCF8574_OK;
    return 1u;
}

void MyPCF8574_SetBacklight(uint8_t on)
{
    s_backlight = (on != 0u) ? 1u : 0u;
    (void)MyPCF8574_WritePort((uint8_t)(s_backlight ? PCF8574_BL_ON : PCF8574_BL_OFF));
}

uint8_t MyPCF8574_GetBacklight(void)
{
    return s_backlight;
}

/* ============================ 4 位并口时序 ============================== */
/*
 * HD44780 4 位模式时序 (数据手册):
 *   1. D7~D4 上放好半字节, RS/RW 稳定
 *   2. E 拉高, 保持 >= 450ns (这里给 2us)
 *   3. E 拉低, 这个下降沿 LCD 才把 D7~D4 锁存进去
 *   4. 指令执行 <= 37us (这里给 50us; 清屏单独给 1.5ms 以上)
 */
uint8_t MyPCF8574_WriteNibble(uint8_t nibble, uint8_t rs)
{
    uint8_t value;

    value = (uint8_t)((nibble & 0xF0u) | (s_backlight ? PCF8574_BL_ON : PCF8574_BL_OFF));
    if (rs != 0u)
    {
        value |= PCF8574_BIT_RS;
    }

    if (!MyPCF8574_WritePort((uint8_t)(value | PCF8574_BIT_EN)))
    {
        return 0u;
    }
    Delay_us(2);                          /* E 高电平时间 */
    if (!MyPCF8574_WritePort(value))      /* E 下降沿: 锁存 */
    {
        return 0u;
    }
    Delay_us(50);                         /* 等指令执行完 (37us) */
    return 1u;
}

void MyPCF8574SendByte(uint8_t data, uint8_t RW, uint8_t RS)
{
    if (RW != 0u)
    {
        /* 本驱动不读忙标志(RW=1), 用固定延时替代, 见 MyLCD1602A.c 里的说明 */
        s_status = PCF8574_ERR_BUS;
        return;
    }
    if (!MyPCF8574_WriteNibble((uint8_t)(data & 0xF0u), RS))
    {
        return;
    }
    (void)MyPCF8574_WriteNibble((uint8_t)(data << 4), RS);
}

uint8_t MyPCF8574_LastStatus(void)
{
    return s_status;
}
