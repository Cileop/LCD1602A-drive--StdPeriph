#include "stm32f10x.h"
#include "Delay.h"
#include "MyLCD1602A.h"

/* ============================================================================
 *  LCD1602A 驱动自检程序
 *
 *  第 1 行固定显示 "LCD1602A 4-bit OK"
 *  第 2 行显示一个每 0.5s 加 1 的计数器
 *
 *  这样做的目的: 如果显示是稳定的, 说明 4 位初始化/时序/总线全部正常;
 *  如果出现乱码、跳字、某个字符固定错位, 一眼就能看出来。
 *
 *  原来的密码锁界面在文件末尾(注释掉的部分), 驱动调好后再取消注释即可。
 * ========================================================================== */

/* 把 0~65535 转成十进制字符串, 避免引入 printf 增加代码体积 */
static void U16ToStr(uint16_t value, char *buf)
{
    char tmp[6];
    uint8_t n = 0u;
    uint8_t i;

    if (value == 0u)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while ((value > 0u) && (n < 5u))
    {
        tmp[n] = (char)('0' + (value % 10u));
        value = (uint16_t)(value / 10u);
        n++;
    }
    for (i = 0u; i < n; i++)
    {
        buf[i] = tmp[n - 1u - i];
    }
    buf[n] = '\0';
}

int main(void)
{
    uint16_t count = 0u;
    char line[17];

    LCD1602A_Init();

    if (LCD1602A_IsPresent() == 0u)
    {
        /* 总线没有应答: 屏幕不会显示任何东西。
           先按 修复说明.md 里的"硬件排查清单"检查接线、供电、地址。 */
        LCD1602A_ShowString(1, 1, "NO PCF8574 ACK");
        LCD1602A_ShowString(2, 1, "check SDA/SCL");
        while (1)
        {
        }
    }

    LCD1602A_ShowString(1, 1, "LCD1602A 4-bit OK");

    while (1)
    {
        /* 拼出 "CNT: 00000" */
        line[0] = 'C';
        line[1] = 'N';
        line[2] = 'T';
        line[3] = ':';
        line[4] = ' ';
        U16ToStr(count, &line[5]);

        LCD1602A_ShowString(2, 1, line);

        count++;
        Delay_ms(500);
    }
}

/* ============================================================================
 *  原来的密码锁界面 (驱动调好后取消注释, 并把 Key_Init() 一起放开)
 *
 *  #include "Key.h"
 *
 *  int main(void)
 *  {
 *      char Password_DisplayedCharacters[] = "Password:";
 *      char Password_input[17];
 *      uint8_t i;
 *
 *      LCD1602A_Init();
 *      Key_Init();
 *      memset(Password_input, 0, sizeof(Password_input));
 *
 *      LCD1602A_ShowString(1, 1, Password_DisplayedCharacters);
 *
 *      while (1)
 *      {
 *          i = 0;
 *          while (i < 16)
 *          {
 *              Password_input[i] = KeyGetNum();     // 按键取值
 *              Password_input[i + 1] = '\0';
 *              LCD1602A_ShowString(2, 1, Password_input);
 *              i++;
 *          }
 *      }
 *  }
 *
 *  注意: mokuai/key.c 里把 PB6 当成按键输入在用, 而 PB6 是 LCD 的 I2C SCL,
 *  两者会打架, 按键必须换到别的引脚。
 * ========================================================================== */
