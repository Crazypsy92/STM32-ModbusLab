# 裸机 Modbus RTU 与 VirtualEmbeddedLab 联调

## 范围和来源

复用 VirtualEmbeddedLab 的 [寄存器映射](https://github.com/Crazypsy92/VirtualEmbeddedLab/blob/3faa91ea59f4dbb4b44205aa730bc2eb7d14771d/docs/stage8_2_register_map.md) 与上位机协议契约。其从站基于 Zephyr 官方协议栈；本项目并非直接移植 Zephyr 或 FreeModbus，而是按 RTU 规范独立实现限定功能码的轻量 C 从站。参考 [Modbus Serial Line V1.02](https://www.modbus.org/file/secure/modbusoverserial.pdf) 与 [FreeModbus port 分层说明](https://www.embedded-solutions.at/files/freemodbus-v1.6-apidoc/sec_porting_serial.html)。

| 模块 | 职责 |
|---|---|
| BSP/Inc/modbus.h, BSP/Src/modbus.c | CRC16、ADU 校验、功能码分派、异常、广播写及事务预校验，不依赖 STM32 |
| modbus_port.h/.c | USART1、TIM3、收发缓冲、错误统计、超时、可选方向控制 |
| modbus_device.h/.c | 设备寄存器映射、物理 LED、按键与 EEPROM 在线状态 |
| Core/Src/main.c | 默认 Modbus profile，旧 I2C 自测可通过构建选项运行 |

## 接线与配置

| USB-TTL（3.3 V 逻辑） | STM32F103C8T6 |
|---|---|
| TXD | PA10 / USART1_RX |
| RXD | PA9 / USART1_TX |
| GND | GND |
| VCC | 默认不接，开发板独立供电 |

115200 baud、8 数据位、偶校验 E、1 停止位、无流控，从站地址 1。TX/RX 交叉，共地；不要使用 RS232 电平或 5 V TTL 作为默认接线。TIM3 采用 1 MHz 时基；USART RXNE 时间戳加字符时长近似判断 t1.5，单次定时器约 1846 us 结束帧（1750 us 加一个字符保护时间）。实际串口时序须用逻辑分析仪验证，暂不宣称完整物理层一致性。

RS485 后续构建选项 -DBSP_MODBUS_RS485=ON，PA8 预留 DE 与低有效 /RE 并接，默认 TTL 不使用 PA8。须另配与 MCU 电平匹配的收发器及正确终端、偏置；TX 完成 TC 后释放方向。未验证实际收发器，不建议现阶段启用。

## 构建

```sh
cmake --preset Debug -DBSP_MODBUS_ENABLE=ON -DBSP_MODBUS_RS485=OFF
cmake --build --preset Debug
```

旧 I2C 自测使用 -DBSP_MODBUS_ENABLE=OFF。默认 Modbus profile 仅在启动前初始化 OLED 与探测 EEPROM，通信期间不运行会阻塞的旧 EEPROM/OLED 自测任务；不远程写 EEPROM。保留按键扫描与 LED 控制。IOC 尚未声明 USART1/TIM3，BSP 手工配置，重新生成须复核配置。

## 兼容的寄存器表

所有地址为零基偏移，站号 1。

| 区域 | 地址 | 含义 |
|---|---|---|
| Coil | 0 | PC13 真实 LED，1=亮 |
| Coil | 1–7 | RAM 逻辑输出，用于原上位机 8 位协议验证，不是物理 GPIO |
| DI | 0–1 | KEY1/KEY2 原始按下状态 |
| DI | 2 | 启动时 EEPROM 在线探测结果 |
| DI | 3 | 保留，0 |
| HR | 0 | 采样模式 0–2 |
| HR | 1 | 软件采样周期 1–1000 ms |
| HR | 2 | 软件采样告警阈值 0–1000 |
| HR | 3 | 8 位输出掩码，与 Coil/IR7 一致 |
| HR | 4 | 心跳间隔参数 1–1000 ms（软件计时占位，不驱动硬件） |
| HR | 5 | 成功写请求累计次数，模 65536，只读 |
| HR | 6–7 | 保留，只读 0 |
| IR | 0 | 模式及 bit8 软件告警 |
| IR | 1–2 | 运行时间 ms，低/高 16 位 |
| IR | 3–5 | 处理请求数、已准备回复数、错误数 |
| IR | 6–7 | DI/DO 位图 |

支持 FC01/02/03/04/05/06/0F/10，CRC 错误与其他站号不回复，广播只执行有效写入且不回复；非法功能、地址、值分别返回异常 01/02/03。多写先验证全部值，再提交。诊断计数仅软件观测，不等于物理发送成功。

## 上位机

VirtualEmbeddedLab 在 WSL2 运行 `python3 scripts/vel.py dashboard`，选择 Modbus → Modbus RTU → 连接/传输配置 → Serial 设备，点击 STM32-ModbusLab · TTL 预设（或手动关闭 native_sim PTY 选项），选择实际 /dev/ttyUSB0 或 /dev/ttyACM0（由系统枚举确定），填 115200/8/E/1、读写超时 0.15 s，先测试 FC03，HR0 初始值 1。通过 FC05 写 Coil0 后 FC01 与 IR7 回读，再物理观察 LED；FC02 观察真实按键。上位机仿真专用故障注入和 DI 刺激不能用于实物模式。

## 验收边界

源码与配置已提交。独立 Host 协议测试已用 GCC、-Wall -Wextra -Werror、ASan 和 UBSan 编译运行通过；三份测试源码的 Git blob SHA 与提交 b14ad7f 一致。ARM 编译、TTL 实际通信、真实串口时序、RS485 仍待测试。初始 RTU 核心限定 8 个功能码，不是完整通用 Modbus 栈认证；不宣称生产环境可靠性。后续优先验证旧 profile 编译、Modbus profile 编译、接线、FC03、FC05、FC02、连续收发，再测试异常及定时边界。
