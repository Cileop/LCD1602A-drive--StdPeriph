> ✅ **已修复 / Fixed**：问题已定位为软件 bug（初始化序列错误、读忙函数破坏 I2C 总线等），不是硬件问题。
> 修复说明与数据手册依据见 [修复说明.md](修复说明.md)；`verify/` 里附了一个不需要开发板的 I2C+LCD 仿真验证器
> （同一套测试：旧代码 56 处协议违规、8-bit 模式、DDRAM 乱码 → 新代码 0 违规、4-bit 双行、DDRAM 正确）。

---

stm32f103c8t6-LCD1602A-drive--java-standard-library-
我写了LCD1602A的驱动(LCD1602A是四针脚I2C,里面有PCF8574做I2C转接),但不知道为什么无法正常显示,字体有乱码,(用的是标准库),请求大佬们指点 (代码在master里) (接线图,现象,手册,都在master中的Wriring diagram and manual)       (软件I2C的代码用的江协科技的)

主要驱动代码在 mokuai里的 MyPCF8574.c/.h 和 MyLCD1602A.c/.h 部分配置采用寄存器,已经把正常配置的注释了

ENGLISH:

I wrote the driver for LCD1602A(The LCD1602A is a four-pin I2C ; Inside, there is a PCF8574 used for I2C conversion.), but I don't know why it can't display normally. There are garbled characters. (I used the standard library.) I'm asking the experts for guidance. (The code is in the master branch.) (The wiring diagram, phenomenon, and manual are all in the master.)            (The code for the software I2C uses the technology from Jiangxie Technology.)

The main driving code is located in MyPCF8574.c/.h and MyLCD1602A.c/.h in the mokuai directory. Some configurations use registers. The normal configurations have been commented out.
