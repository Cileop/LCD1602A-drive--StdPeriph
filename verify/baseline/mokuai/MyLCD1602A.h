#ifndef _MYLCD1602A_H
#define _MYLCD1602A_H

void ReadAndJudgeWhileWaiting_BUSY_Flag(void);
void LCD1602A_Init(void);
void ShowLCDChars(uint8_t row,char *Character);

#endif
