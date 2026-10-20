#include "stm32f10x.h"                  // Device header
#include "fengmingqi.h"
#include "Key.h"
#include "MyLCD1602A.h"
#include <string.h>
#include "OLED.h"
int main(void)
{
	//int i=0;
	LCD1602A_Init();
//	OLED_Init();
	Key_Init();
	char Password_DisplayedCharacters[]={'P','a','s','s','w','o','r','d',':','\0'};
    char   Password_input[17];  //16 characters
	memset(Password_input,0,sizeof(Password_input));//将Password_input中的17位都替换为'\0'字符
	
	ShowLCDChars(1,Password_DisplayedCharacters);
	
	//Password_input[0]='1';
	while (1)
	{
		
	ShowLCDChars(2,Password_input);
		
		
		
		
		
		
		
//		OLED_ShowString(1,1, Password_DisplayedCharacters);
//		for(i=0;i>=17;i++)
//		{
//		Password_input[i]=KeyGetNum();
//	    OLED_ShowNum(2,1, Password_input[i],1);
//		if(i>16){i=0;}
//	    }

		                                                                                                                                                       
	}
}
