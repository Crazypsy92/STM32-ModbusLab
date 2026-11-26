# STM32-ModbusLab

基于 STM32 的 Modbus 真实硬件通信实验平台。以 STM32F103C8T6 裸机 LL 工程为起点，开展 UART TTL、Modbus RTU 中间件移植、实际设备数据交互及硬件在环测试，并按需扩展 RS-485。

## 当前状态

**Stage 1：裸机 Modbus RTU 初始实现及 TTL 接口已加入，独立 Host 协议测试通过，待 ARM 编译、烧录和上位机联调。**

基线来源：[stm32f103-at24xx-i2c-bus](https://github.com/Crazypsy92/stm32f103-at24xx-i2c-bus/tree/8000521f62abc04d72417cac80e53ad82bf7c4bb)。原工程的驱动、外设配置和应用逻辑保持不变；本仓库已统一 CMake 工程名、构建目标与 CubeMX 工程文件名。原仓库不作修改。

已新增独立 C 协议层、USART1/TIM3 移植层及设备映射。默认 USB-TTL，RS-485 方向控制可选。尚无本项目真机验收结果。详见 [Modbus 接线、寄存器与测试说明](docs/modbus_rtu.md)。

## 硬件与技术路线

| 项目 | 配置 |
|---|---|
| MCU | STM32F103C8T6，72 MHz |
| 软件 | 裸机 + STM32 LL |
| 构建 | CMake + Ninja + ARM GCC |
| 首阶段通信 | 3.3 V USB-TTL，UART 点对点 |
| 后续通信 | RS-485 半双工，按需接入收发器 |
| 既有外设 | SSD1306 OLED、AT24xx EEPROM、MultiButton、LED |
| PC 测试端 | [VirtualEmbeddedLab](https://github.com/Crazypsy92/VirtualEmbeddedLab) |

现有外设接线沿用原工程：I2C2 PB10/PB11，KEY1 PB12，KEY2 PB14，LED PC13（低电平有效）。Modbus 使用 USART1 PA9/PA10、TIM3、115200 8E1、从站 1；PA8 预留 RS485 DE 与 /RE。

## 目录结构

```text
BSP/                   现有板级驱动、I2C 抽象、OLED、EEPROM、按键
Core/                  STM32 启动、GPIO、中断及主程序
Drivers/               CMSIS、STM32F1 LL 及原厂许可
cmake/                 工具链及 CubeMX 构建配置
CMakeLists.txt
CMakePresets.json
STM32-ModbusLab.ioc     CubeMX 配置（原硬件参数保持不变）
TEST_REPORT.md         原工程历史测试记录
```

## 基线构建

准备 CMake、Ninja 和 arm-none-eabi-gcc，在仓库根目录执行：

```bash
cmake --preset Debug
cmake --build --preset Debug
```

Debug 构建目标为 `STM32-ModbusLab`，预期固件和链接映射文件为 `build/Debug/STM32-ModbusLab.elf`、`build/Debug/STM32-ModbusLab.map`。Release 构建使用 `Release` 预设，输出位于 `build/Release/`。如从原 AT24xx 工程迁移本地构建目录，请清理旧 CMake 缓存后重新配置。

默认编译 Modbus 实验应用（`-DBSP_MODBUS_ENABLE=ON`，实际参数见下方文档）。原 I2C 自测应用保留，可通过 `-DBSP_MODBUS_ENABLE=OFF` 切换。UART/TIM3 由 BSP 初始化，不由 IOC 生成；不要重新生成覆盖用户代码。实际编译与真机验证待执行。

## 开发规划

| 阶段 | 内容 |
|---|---|
| Stage 0 | 原工程导入、基线编译和真机复验 |
| Stage 1 | UART LL 驱动与 TTL 真机收发 |
| Stage 2 | 中断/DMA 收发、缓冲区与帧时序 |
| Stage 3 | Modbus RTU 从站中间件移植 |
| Stage 4 | OLED、EEPROM、按键和 LED 的设备寄存器映射 |
| Stage 5 | 上位机联调及自动化硬件测试 |
| Stage 6 | 压力测试、错误注入和异常恢复 |
| Stage 7 | RS-485 物理层适配（按需） |
| 后续可选 | MCU 主站、多节点、RTOS 移植、Modbus TCP |

TTL 测试使用匹配 MCU 的 3.3 V 逻辑电平、TX/RX 交叉连接及共地，仅证明点对点 UART 与 RTU 通信能力；不能替代 RS-485 物理层、电气时序及工业环境验证。

原 [TEST_REPORT.md](TEST_REPORT.md) 仅作为导入基线的历史记录，其中 `at24xx` 工程名和旧产物路径保留原样，不代表当前工程命名或本项目已通过 Modbus 验收。第三方驱动许可保留在原目录。
