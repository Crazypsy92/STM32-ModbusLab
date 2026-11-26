# STM32-ModbusLab

基于 STM32 的 Modbus 真实硬件通信实验平台。以 STM32F103C8T6 裸机 LL 工程为起点，面向 UART TTL、后续 RS-485 的 Modbus RTU 移植、设备寄存器映射及硬件在环测试。

## 当前状态

**Stage 0：工程初始化。** 计划复用原 [AT24xx I2C 工程](https://github.com/Crazypsy92/stm32f103-at24xx-i2c-bus) 的源码、构建配置及外设驱动。当前尚未实现或验证 Modbus、UART 通信与 RS-485。

## 硬件与技术路线

| 项目 | 配置 |
|---|---|
| MCU | STM32F103C8T6 |
| 软件 | 裸机 + STM32 LL |
| 构建 | CMake + Ninja + ARM GCC |
| 第一阶段通信 | 3.3 V USB-TTL，UART 点对点 |
| 后续通信 | RS-485 半双工，按需接入收发器 |
| 现有外设 | SSD1306 OLED、AT24xx EEPROM、MultiButton、LED |
| 上位机 | [VirtualEmbeddedLab](https://github.com/Crazypsy92/VirtualEmbeddedLab)，作为独立测试端 |

## 开发规划

1. 复用原工程，保持既有外设功能。
2. 新增 UART 驱动并开展 TTL 真机收发验证。
3. 移植 Modbus RTU 从站中间件，完成寄存器映射。
4. 开展真实设备读写、异常恢复及稳定性测试。
5. 按需扩展 DMA、RS-485 及 RTOS 移植。

TTL 仅用于短距离点对点通信验证，不代表 RS-485 物理层已通过测试。原 I2C 工程历史验收不等同于本项目 Modbus 验收。
