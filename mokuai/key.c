#include "stm32f10x.h"                  // Device header
#include "Delay.h"
void Key_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	
	GPIO_InitTypeDef GPIOA_KeyInitStruct;
	GPIOA_KeyInitStruct.GPIO_Mode=GPIO_Mode_Out_PP;
	GPIOA_KeyInitStruct.GPIO_Pin=GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
	GPIOA_KeyInitStruct.GPIO_Speed=GPIO_Speed_50MHz; 
	GPIO_Init(GPIOA,&GPIOA_KeyInitStruct);

	GPIOA_KeyInitStruct.GPIO_Mode=GPIO_Mode_IPU;
	GPIOA_KeyInitStruct.GPIO_Pin=GPIO_Pin_4 | GPIO_Pin_5 ;
	GPIOA_KeyInitStruct.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIOA_KeyInitStruct);
}

uint8_t KeyGetNum(void)
{
    uint8_t KeyNum;
	uint8_t i;
	for(i=1;i>3;i++)
	{
	if(i==1)
		{
		GPIO_ResetBits(GPIOA,GPIO_Pin_1);
		GPIO_SetBits(GPIOA,GPIO_Pin_2);
	    GPIO_SetBits(GPIOA,GPIO_Pin_3);
	       if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_4)==0)
	       {
	    Delay_ms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_4) == 0);
		Delay_ms(20);
		KeyNum = 1;
			   return KeyNum;
	       }
		   else if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_5)==0)
	       {
	    Delay_ms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0);
		Delay_ms(20);
		KeyNum = 2;
			   return KeyNum;
	       }
		   /*
		   else if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_6) == 0)
	       {
	    Delay_ms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_6) == 0);
		Delay_ms(20);
		KeyNum = 3;
			   return KeyNum;
	       }
		   */
		   else{return 0;}
		}
	else if(i==2)
		{
		GPIO_SetBits(GPIOA,GPIO_Pin_1);
        GPIO_ResetBits(GPIOA,GPIO_Pin_2);
	    GPIO_SetBits(GPIOA,GPIO_Pin_3);
	       if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_4)==0)
	       {
	    Delay_ms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_4) == 0);
		Delay_ms(20);
		KeyNum = 4;
			   return KeyNum;
	       }
		   else if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_5)==0)
	       {
	    Delay_ms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0);
		Delay_ms(20);
		KeyNum = 5;
			   return KeyNum;
	       }
		   /*
		   else if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_6) == 0)
	       {
	    Delay_ms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_6) == 0);
		Delay_ms(20);
		KeyNum = 6;
			   return KeyNum;
	       }
		   */
		   else{return 0;}
		}
	 else if(i==3)
		{
		GPIO_SetBits(GPIOA,GPIO_Pin_1);
		GPIO_SetBits(GPIOA,GPIO_Pin_2);
        GPIO_ResetBits(GPIOA,GPIO_Pin_3);
	       if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_4)==0)
	       {
	    Delay_ms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_4) == 0);
		Delay_ms(20);
		KeyNum = 7;
			   return KeyNum;
	       }
		   else if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_5)==0)
	       {
	    Delay_ms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0);
		Delay_ms(20);
		KeyNum = 8;
			   return KeyNum;
	       }
		   /*
		   else if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_6) == 0)
	       {
	    Delay_ms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_6) == 0);
		Delay_ms(20);
		KeyNum = 9;
			   return KeyNum;
	       }
		   */
		   else{return 0;}
		}

	}
}

