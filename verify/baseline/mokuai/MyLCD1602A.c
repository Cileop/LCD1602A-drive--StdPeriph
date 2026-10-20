#include "stm32f10x.h"                  // Device header
#include "stm32f10x_i2c.h"
#include "Delay.h"
#include "MyPCF8574.h"

void ReadAndJudgeWhileWaiting_BUSY_Flag(void);

void LCD1602A_Init(void)
{
	
    MyPCF8574_I2C_Init();
    Delay_ms(25);
	MyPCF8574SendByte(0x38,0,0);   //总线模式及屏幕字符像素设置 
	Delay_ms(25);
	MyPCF8574SendByte(0x38,0,0);   
	Delay_ms(25);
	MyPCF8574SendByte(0x38,0,0);   
	Delay_ms(25);
    //(这一句话有待考究)8位数据总线,切换4位数据总线以符合LCD1602A 的I2C发送模式
	ReadAndJudgeWhileWaiting_BUSY_Flag();
	MyPCF8574SendByte(0x01,0,0);   //清屏
	//Delay_ms(15);
	ReadAndJudgeWhileWaiting_BUSY_Flag();
	MyPCF8574SendByte(0x06,0,0);   //置输入模式设置
	//Delay_ms(15);
	ReadAndJudgeWhileWaiting_BUSY_Flag();
	MyPCF8574SendByte(0x0F,0,0);   //显示开关控制设置 
	//Delay_ms(15);
}


//判断忙标志位,忙时自动等待
void ReadAndJudgeWhileWaiting_BUSY_Flag(void)  //D7(P7) is BUSY   D6-D0(P6-P0) is COUNTER(AC)
{
	uint8_t Hdata,Ldata,BUSY=1  /*,t=1,Data*/  ;
	
	while(BUSY)//等待忙状态 //这里加上了判断并等待忙标志位,直到不忙才退出循环
  {
	  MyI2C_Start();
	  MyI2C_SendByte(0x27<<1);
	  MyI2C_ReceiveAck();
	  MyI2C_SendByte(0x04 | 0x02);
	  MyI2C_ReceiveAck();	
	  Hdata= MyI2C_ReceiveByte(); 
	  MyI2C_SendByte(0x00);   //失能P2 表示数据已接收完毕
      Ldata =Hdata>>4;     //接收低4位 

	  MyI2C_Start();
	  MyI2C_SendByte(0x27<<1);
      MyI2C_ReceiveAck();
	  MyI2C_SendByte(0x04 | 0x02);
	  MyI2C_ReceiveAck();
	  Hdata=MyI2C_ReceiveByte();
	 // Ldata =Ldata>>4;
	  MyI2C_SendByte(0x00);   //失能P2 表示数据已接收完毕
	  MyI2C_ReceiveAck();
	  MyI2C_SendAck(1);	
      MyI2C_Stop();	

	  Hdata=Hdata & 0x0F; 
	  //Data=Hdata | Ldata; //合并数据
	  Hdata&=0x80;        //只保留忙数据位
	  
	if(/*(t==1) && */Hdata == 0x80) //判断忙标志位
    {
		BUSY=1;        //1: busy(忙)
    } 
	else if(/*(t==1) && */Hdata == 0x00)
    {
		BUSY=0;        //0: free(不忙)
	}
	
	//t++;
	//if(t ==4){t=1;}
  }
	
}


void ShowLCDChars(uint8_t row,char Character[17])  //row(行):1 and 2
{
	uint8_t row_address,T=0;
	if(row == 1)      //判断显示在第几行
	{
	 row_address=0x00;
	}
	if(row == 2)
	{
	 row_address=0x40;
	}
	
	ReadAndJudgeWhileWaiting_BUSY_Flag();
	MyPCF8574SendByte(0x0F,0,0);    //开启屏幕显示
	
	ReadAndJudgeWhileWaiting_BUSY_Flag();
    MyPCF8574SendByte(0x80 + row_address,0,0);//使用说明规定输入字符数据时P7置1
	
	while(1)
	{
	ReadAndJudgeWhileWaiting_BUSY_Flag();
	MyPCF8574SendByte(Character[T],0,1);  //将指定数组中的字符(按键输入的数据)显示在屏幕上
	T++;
	Delay_ms(5);
	if(Character[T] == '\0') //当字符为'\0'时表示输入的字符已传输完毕
	  {  
          T = 0;
	    break;   //跳出循环
	  }
	}

}

