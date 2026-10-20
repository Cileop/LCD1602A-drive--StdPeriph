#include "stm32f10x.h"                  // Device header
//#include "stm32f10x_i2c.h"
#include "stm32f10x_gpio.h"
#include "Delay.h"
void MyPCF8574_I2C_Init(void)   //IO口及I2C的初始化
{   //RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1,ENABLE);   硬件I2C1
	
	//RCC_APB2Periph_GPIOB             ((uint32_t)0x00000008)
	//RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
RCC->APB2ENR |=0x00000008;        //开启GPIOB的时钟
	//
	/*
	GPIO_InitTypeDef GPIO_I2C_InitStruct;
	GPIO_I2C_InitStruct.GPIO_Mode=GPIO_Mode_Out_OD;
	GPIO_I2C_InitStruct.GPIO_Pin=GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_I2C_InitStruct.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_I2C_InitStruct);
	*/
	//Mode:OUT_OD  ;  speed:50MHz
	//GPIOB GPIO_6         0x07000000
	//GPIOB GPIO_7         0x70000000
	//GPIOB GPIO_6-7         0x77000000
GPIO_TypeDef* GPIOx =GPIOB;
GPIOx->CRL = 0x77000000;
	//以后像此处这样只是为了速度,可能出错了编译器都不知道那里出错

}

//------------------------------------------------------------------------------------
//---------------------------------软件I2C函数
void MyI2C_W_SCL(uint8_t BitValue);
void MyI2C_W_SDA(uint8_t BitValue);
uint8_t MyI2C_R_SDA(void);
void MyI2C_Start(void);
void MyI2C_Stop(void);
void MyI2C_SendByte(uint8_t Byte);
uint8_t MyI2C_ReceiveByte(void);
void MyI2C_SendAck(uint8_t AckBit);
uint8_t MyI2C_ReceiveAck(void);
//------------------------------------------------------------------------------------

void MyPCF8574SendByte(uint8_t data,uint8_t RW,uint8_t RS)//写命令或数据
{
 //RW 0:write(写) 1:read(读)    RS 0:instruct(命令) 1:data(数据)
//7-4 (data bits)
//P7  P6  P5  P4       P3      P2         P1  P0  //P3 default: 0
// |   |   |   |        |       |          |   |
//DB7 DB6 DB5 DB4      P3 CS(Enable foot) RW  RS
	
	
//因为I2C转接模块PCF8574只有4位数据位,所以只能使用LCD1602A的4位数据总线模式
//且I2C传数据从高位开始,所以先传高4位Hdata 再传低4位Ldata
	uint8_t Hdata,Ldata; 
	Hdata =data & 0xF0;  //将data的高4位移至Hdata
	Hdata^=0x04;        //使能P2  P2对应LCD1602A 读写 操作时序中的 E
	Ldata =  data<<4;   //将低4位移至有效4位数据位
	Ldata^=0x04;        //使能P2 
	
      if(RW == 1)      //判断是否是读位
	{
	   Hdata^=0x02;
	   Ldata^=0x02;
	}
	if(RS == 1)       //判断发送的是否是数据
	{
	 Hdata^=0x01;
	 Ldata^=0x01;
	}
	
	MyI2C_Start();
	MyI2C_SendByte(0x27<<1);
	MyI2C_ReceiveAck();	
	MyI2C_SendByte(Hdata);
	MyI2C_ReceiveAck();
	Delay_us(2);
	MyI2C_SendByte(Hdata^0x04);
	MyI2C_ReceiveAck();
	MyI2C_Stop();

	MyI2C_Start();
	MyI2C_SendByte(0x27<<1);
	MyI2C_ReceiveAck();
	MyI2C_SendByte(Ldata);
	MyI2C_ReceiveAck();
	Delay_us(2);
	MyI2C_SendByte(Ldata^0x04);
	MyI2C_ReceiveAck();
	MyI2C_Stop();

}
//--------------------------------------------------------------------------------------




void MyI2C_W_SCL(uint8_t BitValue)
{
	GPIO_TypeDef* GPIOx = GPIOB;
	//GPIO_WriteBit(GPIOB, GPIO_Pin_6, (BitAction)BitValue);		//根据BitValue，设置SCL引脚的电平
    //GPIO_Pin_6                 ((uint16_t)0x0040)
	if (BitValue != Bit_RESET)
    {
    GPIOx->BSRR = 0x0040;
    }
  else
    {
    GPIOx->BRR = 0x0040;
    }
	Delay_us(10);												//延时10us，防止时序频率超过要求
}

/**
  * 函    数：I2C写SDA引脚电平
  * 参    数：BitValue 协议层传入的当前需要写入SDA的电平，范围0~1
  * 返 回 值：无
  * 注意事项：此函数需要用户实现内容，当BitValue为0时，需要置SDA为低电平，当BitValue为1时，需要置SDA为高电平
  */
void MyI2C_W_SDA(uint8_t BitValue)
{
	GPIO_TypeDef* GPIOx =GPIOB;
	//GPIO_WriteBit(GPIOB, GPIO_Pin_7, (BitAction)BitValue);		//根据BitValue，设置SDA引脚的电平，BitValue要实现非0即1的特性
    //GPIO_Pin_7                 ((uint16_t)0x0080)
    if (BitValue != Bit_RESET)
    {
    GPIOx->BSRR = 0x0080;
    }
  else
    {
    GPIOx->BRR = 0x0080;
    }
	Delay_us(10);												//延时10us，防止时序频率超过要求
}

/**
  * 函    数：I2C读SDA引脚电平
  * 参    数：无
  * 返 回 值：协议层需要得到的当前SDA的电平，范围0~1
  * 注意事项：此函数需要用户实现内容，当前SDA为低电平时，返回0，当前SDA为高电平时，返回1
  */
uint8_t MyI2C_R_SDA(void)
{
	GPIO_TypeDef* GPIOx =GPIOB;
	uint8_t BitValue = 0x00;
	 if ((GPIOx->IDR & 0x0080) != (uint32_t)Bit_RESET)
  {
    BitValue = (uint8_t)Bit_SET;
  }
  else
  {
    BitValue = (uint8_t)Bit_RESET;
  }
	//BitValue = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7);		//读取SDA电平
	Delay_us(10);												//延时10us，防止时序频率超过要求
	return BitValue;											//返回SDA电平
}

/**
  * 函    数：I2C初始化
  * 参    数：无
  * 返 回 值：无
  * 注意事项：此函数需要用户实现内容，实现SCL和SDA引脚的初始化
  */

/*协议层*/

/**
  * 函    数：I2C起始
  * 参    数：无
  * 返 回 值：无
  */
void MyI2C_Start(void)
{
	MyI2C_W_SDA(1);							//释放SDA，确保SDA为高电平
	MyI2C_W_SCL(1);							//释放SCL，确保SCL为高电平
	MyI2C_W_SDA(0);							//在SCL高电平期间，拉低SDA，产生起始信号
	MyI2C_W_SCL(0);							//起始后把SCL也拉低，即为了占用总线，也为了方便总线时序的拼接
}

/**
  * 函    数：I2C终止
  * 参    数：无
  * 返 回 值：无
  */
void MyI2C_Stop(void)
{
	MyI2C_W_SDA(0);							//拉低SDA，确保SDA为低电平
	MyI2C_W_SCL(1);							//释放SCL，使SCL呈现高电平
	MyI2C_W_SDA(1);							//在SCL高电平期间，释放SDA，产生终止信号
}

/**
  * 函    数：I2C发送一个字节
  * 参    数：Byte 要发送的一个字节数据，范围：0x00~0xFF
  * 返 回 值：无
  */
void MyI2C_SendByte(uint8_t Byte)
{
	uint8_t i;
	for (i = 0; i < 8; i ++)				//循环8次，主机依次发送数据的每一位
	{
		/*两个!可以对数据进行两次逻辑取反，作用是把非0值统一转换为1，即：!!(0) = 0，!!(非0) = 1*/
		MyI2C_W_SDA(!!(Byte & (0x80 >> i)));//使用掩码的方式取出Byte的指定一位数据并写入到SDA线
		MyI2C_W_SCL(1);						//释放SCL，从机在SCL高电平期间读取SDA
		MyI2C_W_SCL(0);						//拉低SCL，主机开始发送下一位数据
	}
}

/**
  * 函    数：I2C接收一个字节
  * 参    数：无
  * 返 回 值：接收到的一个字节数据，范围：0x00~0xFF
  */
uint8_t MyI2C_ReceiveByte(void)
{
	uint8_t i, Byte = 0x00;					//定义接收的数据，并赋初值0x00，此处必须赋初值0x00，后面会用到
	MyI2C_W_SDA(1);							//接收前，主机先确保释放SDA，避免干扰从机的数据发送
	for (i = 0; i < 8; i ++)				//循环8次，主机依次接收数据的每一位
	{
		MyI2C_W_SCL(1);						//释放SCL，主机机在SCL高电平期间读取SDA
		if (MyI2C_R_SDA()){Byte |= (0x80 >> i);}	//读取SDA数据，并存储到Byte变量
													//当SDA为1时，置变量指定位为1，当SDA为0时，不做处理，指定位为默认的初值0
		MyI2C_W_SCL(0);						//拉低SCL，从机在SCL低电平期间写入SDA
	}
	return Byte;							//返回接收到的一个字节数据
}

/**
  * 函    数：I2C发送应答位
  * 参    数：Byte 要发送的应答位，范围：0~1，0表示应答，1表示非应答
  * 返 回 值：无
  */
void MyI2C_SendAck(uint8_t AckBit)
{
	MyI2C_W_SDA(AckBit);					//主机把应答位数据放到SDA线
	MyI2C_W_SCL(1);							//释放SCL，从机在SCL高电平期间，读取应答位
	MyI2C_W_SCL(0);							//拉低SCL，开始下一个时序模块
}

/**
  * 函    数：I2C接收应答位
  * 参    数：无
  * 返 回 值：接收到的应答位，范围：0~1，0表示应答，1表示非应答
  */
uint8_t MyI2C_ReceiveAck(void)
{
	uint8_t AckBit;							//定义应答位变量
	MyI2C_W_SDA(1);							//接收前，主机先确保释放SDA，避免干扰从机的数据发送
	MyI2C_W_SCL(1);							//释放SCL，主机机在SCL高电平期间读取SDA
	AckBit = MyI2C_R_SDA();					//将应答位存储到变量里
	MyI2C_W_SCL(0);							//拉低SCL，开始下一个时序模块
	return AckBit;							//返回定义应答位变量
}

