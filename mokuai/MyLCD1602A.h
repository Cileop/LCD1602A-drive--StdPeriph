#ifndef _MYLCD1602A_H
#define _MYLCD1602A_H

#include <stdint.h>

/* ============================================================================
 *  LCD1602A (PCF8574 4 线转接板) 显示驱动
 *  row: 1~2 (第 1 行 / 第 2 行)     col: 1~16
 * ========================================================================== */

/* 初始化: 配 I2C -> 等 40ms 上电复位 -> 进 4 位模式 -> 清屏 -> 开显示 */
void    LCD1602A_Init(void);

/* 1 = 初始化过程中 I2C 全部有应答(I2C 和地址都正常), 0 = 有问题 */
uint8_t LCD1602A_IsPresent(void);

/* 清屏(0x01, 需要 1.52ms) / 光标回原点(0x02) */
void    LCD1602A_Clear(void);
void    LCD1602A_ReturnHome(void);

/* 显示开关: on=1 开显示; cursor=1 显示光标; blink=1 光标闪烁 */
void    LCD1602A_Display(uint8_t on, uint8_t cursor, uint8_t blink);

/* 光标定位, 行列都从 1 开始, 越界会自动夹到有效范围 */
void    LCD1602A_SetCursor(uint8_t row, uint8_t col);

/* 写一个字符 / 一个字符串(不做行内清除) */
void    LCD1602A_WriteChar(char ch);
void    LCD1602A_WriteString(const char *str);

/* 从(row,col)开始写一整行, 不足 16 字符用空格补齐, 避免留下残影 */
void    LCD1602A_ShowString(uint8_t row, uint8_t col, const char *str);

/* 背光开关 (0/1)。如果亮灭是反的, 改 MyPCF8574.h 里的 PCF8574_BL_ACTIVE_HIGH */
void    LCD1602A_Backlight(uint8_t on);

/* 发一条原始指令(给需要自定义的用户) */
void    LCD1602A_Command(uint8_t cmd);

/* 最近一次错误码: 0 正常, 1 I2C 无应答, 2 没找到 PCF8574 */
uint8_t LCD1602A_LastError(void);

/* 等 LCD 空闲。本驱动不读忙标志(RW), 用固定延时代替, 保证总线时序简单可靠。 */
void    LCD1602A_WaitReady(void);

/* ---- 以下两个是兼容旧工程的接口, 新代码请用上面的函数 ---- */
void    ReadAndJudgeWhileWaiting_BUSY_Flag(void);
void    ShowLCDChars(uint8_t row, const char *Character);

#endif
