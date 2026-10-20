#ifndef _MYPCF8574_H
#define _MYPCF8574_H
void MyPCF8574_I2C_Init(void);
void MyPCF8574SendByte(uint8_t data,uint8_t RW,uint8_t RS);

//----------------------------------------------------------------------
//------------------------------------------------软件I2C函数
void MyI2C_W_SCL(uint8_t BitValue);
void MyI2C_W_SDA(uint8_t BitValue);
uint8_t MyI2C_R_SDA(void);
void MyI2C_Start(void);
void MyI2C_Stop(void);
void MyI2C_SendByte(uint8_t Byte);
uint8_t MyI2C_ReceiveByte(void);
void MyI2C_SendAck(uint8_t AckBit);
uint8_t MyI2C_ReceiveAck(void);
#endif
