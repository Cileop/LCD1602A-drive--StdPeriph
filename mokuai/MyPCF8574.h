#ifndef _MYPCF8574_H
#define _MYPCF8574_H

#include <stdint.h>

/* ============================================================================
 *  PCF8574 底层驱动 (I2C -> 4 位并口)
 *  硬件: STM32F103C8T6 + PCF8574 转接板 + LCD1602A
 *  软件 I2C: PB6 = SCL, PB7 = SDA (开漏输出)
 *
 *  转接板内部接线 (主流 4 线模块, 与本项目实物一致):
 *      PCF8574 P0 -> LCD RS
 *      PCF8574 P1 -> LCD RW
 *      PCF8574 P2 -> LCD E   (使能, 上升沿准备数据, 下降沿锁存)
 *      PCF8574 P3 -> 背光控制三极管
 *      PCF8574 P4 -> LCD D4
 *      PCF8574 P5 -> LCD D5
 *      PCF8574 P6 -> LCD D6
 *      PCF8574 P7 -> LCD D7
 *
 *  一次 I2C 写就是把 P0~P7 这 8 个脚同时刷新成新值。
 *  所以"给 LCD 送一个半字节"= 写一个字节: 高 4 位放数据, P2(E) 拉高,
 *  再写一个字节把 P2(E) 拉低 -> 这个下降沿时 LCD 才真正把数据读进去。
 * ========================================================================== */

/* ---------------- 用户可配置项 ---------------- */

/* PCF8574 的 7 位从机地址:
 *   PCF8574  (A2A1A0 = 111) -> 0x27
 *   PCF8574A (A2A1A0 = 111) -> 0x3F
 * 不确定时不用改, 初始化时会自动扫描 0x20~0x27 / 0x38~0x3F。 */
#ifndef PCF8574_ADDR7
#define PCF8574_ADDR7             0x27u
#endif

/* 背光有效电平: 1 = P3 为高时背光点亮 (NPN 三极管, 最常见)
 *               0 = P3 为低时背光点亮 (PNP 三极管)
 * 如果上电后背光是反的(亮/灭相反), 把下面这个 1 改成 0 即可。 */
#ifndef PCF8574_BL_ACTIVE_HIGH
#define PCF8574_BL_ACTIVE_HIGH    1
#endif

/* ---------------- 位定义 (不要改) ---------------- */
#define PCF8574_BIT_RS            0x01u   /* P0 */
#define PCF8574_BIT_RW            0x02u   /* P1 */
#define PCF8574_BIT_EN            0x04u   /* P2 */
#define PCF8574_BIT_BL            0x08u   /* P3 */

#if PCF8574_BL_ACTIVE_HIGH
#define PCF8574_BL_ON             PCF8574_BIT_BL
#define PCF8574_BL_OFF            0x00u
#else
#define PCF8574_BL_ON             0x00u
#define PCF8574_BL_OFF            PCF8574_BIT_BL
#endif

/* ---------------- 错误码 (MyPCF8574_LastStatus 返回值) ---------------- */
#define PCF8574_OK                0u
#define PCF8574_ERR_NACK          1u   /* 从机没有应答: 没接线 / 地址不对 / 没供电 */
#define PCF8574_ERR_BUS           2u   /* SDA 被拉死, 总线恢复失败 */

/* ---------------- 初始化 ---------------- */
void    MyPCF8574_I2C_Init(void);          /* 配好 PB6/PB7 并释放总线 */

/* ---------------- 器件/地址 ---------------- */
uint8_t MyPCF8574_Probe(uint8_t addr7);    /* 1 = 该地址有应答 */
uint8_t MyPCF8574_AutoAddress(void);       /* 扫描并自动选中地址, 0 = 没找到 */
void    MyPCF8574_SetAddress(uint8_t addr7);
uint8_t MyPCF8574_GetAddress(void);

/* ---------------- 端口与背光 ---------------- */
uint8_t MyPCF8574_WritePort(uint8_t value); /* 直接写 P0~P7, 1 = 成功 */
void    MyPCF8574_SetBacklight(uint8_t on);
uint8_t MyPCF8574_GetBacklight(void);

/* ---------------- 4 位并口时序 ---------------- */
/* 把一个半字节放到 D7~D4, 并产生一次 E 脉冲; rs=1 表示数据, 0 表示指令。
 * 返回值 1 = 成功, 0 = I2C 失败。 */
uint8_t MyPCF8574_WriteNibble(uint8_t nibble, uint8_t rs);

/* 兼容旧接口: 一个完整字节拆成高/低两个半字节发送。
 * 注意: RW 只支持 0(写), 传 1 会返回错误(本驱动不读忙标志, 改用固定延时)。 */
void    MyPCF8574SendByte(uint8_t data, uint8_t RW, uint8_t RS);

uint8_t MyPCF8574_LastStatus(void);

/* ---------------- 软件 I2C 底层 ---------------- */
void    MyI2C_W_SCL(uint8_t BitValue);
void    MyI2C_W_SDA(uint8_t BitValue);
uint8_t MyI2C_R_SDA(void);
void    MyI2C_Start(void);
void    MyI2C_Stop(void);
void    MyI2C_SendByte(uint8_t Byte);
uint8_t MyI2C_ReceiveByte(void);
void    MyI2C_SendAck(uint8_t AckBit);
uint8_t MyI2C_ReceiveAck(void);
void    MyI2C_BusRecover(void);            /* SDA 被拉死时打 9 个时钟救总线 */

#endif
