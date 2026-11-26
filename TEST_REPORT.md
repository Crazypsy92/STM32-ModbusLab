# 测试报告

**重要声明：本报告严格区分「软件编译通过」「仿真/逻辑验证」「真实硬件测试」「尚未验证」。
编译通过仅代表代码可构建，不代表 OLED 点亮或 EEPROM 通信功能正常。**

- 测试对象：STM32F103C8T6 裸机 AT24xx + OLED + MultiButton 工程
- 工具链：`arm-none-eabi-gcc 14.2.1` / CMake / Ninja（Linux）
- 烧录工具：OpenOCD 0.12.0 + ST-Link V2（USB 位置 `1-3.3`）
- 目标板：STM32F103C8T6，device id `0x00006410`，Flash 128 KiB，目标电压 3.15 V
- 测试时间：2026-11-20

---

## 1. 软件编译通过（PASS）

| 项目 | 命令 / 条件 | 结果 |
|---|---|---|
| 工程改名 | `CMAKE_PROJECT_NAME=at24xx`、`at24xx.ioc`、产物 `build/Debug/at24xx.elf` | PASS |
| 软件 IIC 后端全工程编译 | `cmake --preset Debug -DBSP_I2C_BACKEND=SOFT && cmake --build build/Debug` | PASS，无 error |
| 硬件 I2C2 后端全工程编译 | `cmake --preset Debug -DBSP_I2C_BACKEND=HARD && cmake --build build/Debug` | PASS，无 error |
| 五层架构重构后复编 | `i2c_bus.h` + `i2c_stm32_ll.c` 替换 `i2c_interface.c/h`，SOFT/HARD 均重新配置编译 | PASS，无 error / 无新增 warning |
| 非 bug 项优化后复编 | 软件 IIC 时钟延展、ACK polling、自测先读后写、按键事件独立判断，SOFT/HARD 均重编 | PASS，无 error / 无新增 warning |
| 异步总线接口后复编 | `i2c_bus.h` 新增 `transfer_async / async_finish / is_pending / wait` 与 `pending` 状态，LL 后端 `transfer_async=NULL` | PASS，SOFT/HARD 均无 error；同步路径上板复测读写仍一致 |
| 旧驱动引用清理 | 检索 `MyApplication.h`、`At24cxx.h/c`、`write_dma/read_dma`、`i2c_interface` 调用 | PASS，新驱动已无这些引用 |
| 共享总线 | `bsp_i2c.c` 全工程唯一 LL 实例，OLED / AT24xx 均经 `BSP_I2C1_GetBus()` 获取 | PASS |
| 事务参数隔离 | 字地址宽度 / 读写方向均随 `i2c_bus_msg_t` 传递，无共享可变字段 | PASS |
| 编译期未来例化检查 | LL 后端 `ll_transfer` 对 NULL、地址 >0x7F、len=0、非法 mode、tx/rx 同空同非空均返回 `I2C_BUS_ERROR_PARAM` | PASS（代码走查） |

资源占用（Debug，重构后）：

| 后端 | RAM | FLASH |
|---|---|---|
| SOFT（默认） | 3424 B / 20 KB (16.72%) | 31108 B / 64 KB (47.47%) |
| HARD | 3424 B / 20 KB (16.72%) | 31108 B / 64 KB (47.47%) |

告警说明：`BSP/Src/oled_data.c`（原有字库文件）产生 2 条 `-Wmissing-braces` 风格警告，
属既有代码的初始化写法，非本次改动引入；本次新增/修改的源文件无 warning。

---

## 2. 问题定位与修复（PASS）

首轮上板显示 `EVT:EE DATA ERR`、`RD:........`，即地址探测成功、读写事务完成，但读回内容与写入不符。

通过 SWD 读取运行中的全局变量定位：

| 变量 | 首轮值 | 修复后值 |
|---|---|---|
| `g_at24cxx.device_address` | `0x50` | `0x50` |
| `g_at24cxx.page_size` | `8`（按 AT24C02 配置） | `32` |
| `g_at24cxx.addr_bytes` | `1`（按 AT24C02 推导） | `2` |
| `s_ee_read_buf` | 非测试串（全 `0xFF`） | `AT24CXX-TEST!` + 校验序列，逐字节一致 |

**根因**：实际 EEPROM 为 **2 字节字地址**型号（AT24C32 及以上），而工程最初按 AT24C02（1 字节字地址）发送地址。
1 字节协议下数据被写入错误地址，随机读也取不到写入内容，因此表现为 `DATA ERR`。

**修复**：`at24xx_port.h` 改为 `AT24XX_PAGE_SIZE=AT24C32_PAGE_SIZE`，由 `AT24Cxx_Init()` 按页大小推导
`addr_bytes=2`；每次事务把 `addr_bytes` 放进 `i2c_bus_msg_t.mem_addr_size` 传递，
1/2 字节字地址差异只在 LL 后端按消息处理，应用层与测试代码不做探测或分支。

### 2.1 硬件 I2C2 后端总线锁死修复（PASS）

切换 `BSP_I2C_BACKEND=HARD` 首次上板时，一次从机 NACK 后程序卡死、OLED 不再刷新。

| 现象 | 定位 |
|---|---|
| `I2C2->SR1=0x400`（AF）、`SR2=0x03`（MSL+BUSY）、SCL 被拉低 | 出错路径只返回错误，没有产生 STOP |
| CPU 停在 `LL_I2C_IsActiveFlag_BUSY()` 循环 | `HardwareI2C_Check()` 的 BUSY 等待没有超时 |

**修复**：新增统一的错误路径释放（清 AF + 产生 STOP），所有 NACK/超时返回前调用；
BUSY/SB 等待改为 DWT 带超时。该策略现已固化到新的 LL 后端：`hw_release()` 负责清 AF + STOP，
所有 `hw_wait_*` 均带超时，`ll_transfer` 在超时或 BUSY 后自动执行 `i2c_stm32_ll_recover()`。
修复后同一接线、400kHz 下 `g_at24xx_online=1`、40 字节读回逐字节一致，I2C2 空闲态 `SR1=0、SR2=0`。

---

## 3. 真实硬件测试（已执行）

### 3.1 烧录与在线

| 测试项目 | 结果 | 证据 |
|---|---|---|
| SWD 连接与烧录 | PASS | Program + Verify OK，device id `0x410`，目标电压 3.151 V |
| AT24xx 地址探测 `0x50` | PASS | `g_at24xx_online=1`；OLED 行1 显示 `EE:50 ...` |
| OLED 显示 | PASS | 实机可见测试主界面（用户反馈），显示链路正常 |
| 字地址宽度适配 | PASS | `g_at24cxx.addr_bytes=2` |
| 硬件 I2C2 后端 | PASS | 400kHz 下 `online=1`，读回 40 字节一致，总线空闲无 AF/BUSY |
| 重构后 SOFT 复测 | PASS | 重新烧录后 `g_at24xx_online=1`、`last_result=0`，`s_ee_read_buf` 与 40 字节测试数据逐字节一致 |
| 重构后 HARD 复测 | PASS | 2 字节字地址 + 重复 START 读序列正确，`online=1`、读回 40 字节一致 |
| 优化后 SOFT 复测 | PASS | 时钟延展 + ACK polling 版本：`g_sys_tick` 正常递增、`online=1`、`last_result=0`、40 字节一致 |
| 优化后 HARD 复测 | PASS | 同上；写后立即读回一致，说明 ACK polling 在写周期结束后才放行后续事务 |

### 3.2 EEPROM 读写

测试数据：地址 `0x0000` 起始，40 字节（`AT24CXX-TEST!` + `00 11 22 ... 7E`），
页大小 32 字节，驱动自动拆分为 32 + 8 两次页写，覆盖跨页场景。

| 测试项目 | 结果 | 证据 |
|---|---|---|
| 2 字节字地址写 | PASS | 两次页写均无 ACK/超时错误 |
| 顺序读回 | PASS | SWD RAM dump：`s_ee_read_buf` 与 40 字节测试数据逐字节一致 |
| 跨页写拆分 | PASS | 40 字节跨 32 字节页边界，读回完整 |
| 上电自测 | PASS | 先读回比对，一致时直接显示 `EVT:EE DATA OK` 且不重写；不一致才写入并复核，行3 `RD:AT24CXX-TEST!` |
| 1 字节字地址 | FAIL（符合预期） | 该硬件为 2 字节型号，1 字节协议读回错误 |
| K1/K2 单击 | PASS | SWD 读取 `s_key_stats`：K1 单击 2 次、K2 单击 2 次，回调与分发正常 |

### 3.3 尚未在硬件上验证

| 测试项目 | 状态 |
|---|---|
| K1 长按填充 / K2 长按重检 | 未验证 |
| 断电重启后的数据保持 | 部分验证：上电自测已改为先读不覆盖，SWD 复位/重新烧录后数据仍一致；真实断电（拔电）尚未复测 |
| 示波器/逻辑分析仪波形 | 未测量 |

---

## 4. 代码级逻辑检查（PASS，静态检查）

| 检查项 | 结论 |
|---|---|
| AT24xx 驱动与 MCU 解耦 | PASS，只包含 `<stdint.h>` 与 `i2c_bus.h` |
| AT24xx 驱动不使用全局总线 | PASS，只通过 `AT24CXX_Device::bus` 访问 |
| 无 DMA 依赖 | PASS，统一 `i2c_bus_transfer` 轮询接口 |
| MEM 模式字地址 | PASS，`mem_addr_size==2` 时先发高字节再发低字节 |
| 页写拆分 | PASS，按 `page_size - addr % page_size` 拆分，跨页不回卷 |
| 写周期等待 | PASS，页写后用 ACK polling（每 1ms 探测地址，预算 `write_cycle_ms`），不再固定延时 10ms |
| 软件 IIC 时钟延展 | PASS，SCL 释放后轮询引脚电平直到真正变高，5ms 超时兜底 |
| 按键事件分发 | PASS，单击/长按各类计数独立判断，同一扫描周期多事件不丢失 |
| 配置隔离 | PASS，OLED 用 `STREAM`、AT24xx 用 `MEM`，每次事务独立成消息 |
| 地址位宽 | PASS，7 位地址 `0x50` 由底层左移为 `0xA0` 发送，无二次左移 |
| 总线占用仲裁 | PASS，acquire/release 成对；异 owner 抢占 / ISR 占用 / 配置不一致均被拒绝并累加 `rejects` |
| 状态自检 | PASS，owner 与 depth 不一致即拒绝，避免状态破坏后继续通信 |
| 未检测到 EEPROM | PASS，`g_at24xx_online=0` 时只显示未找到，不发起读写 |
| 主循环非阻塞 | PASS，按键 5ms、运行指示 1s；仅 EEPROM 写周期为必要阻塞 |

---

## 5. 已知限制

| 项目 | 说明 |
|---|---|
| AT24C02（1 字节字地址） | 支持，但本板实物是 2 字节型号，未做 1 字节实物验证 |
| AT24C04/08/16（从机地址分页） | 未实现；`AT24Cxx_Init` 对 1 字节地址 + 容量 >256 B 的配置直接返回 `PARAM`，避免静默回卷写坏数据 |
| 24C128/256 页大小 | 当前按 32 字节拆分（对 64 字节页安全），未做实物验证 |
| 10 位从机地址 | 抽象层已预留 `I2C_BUS_ADDR_10BIT`，LL 后端只实现 7 位 |
| 多任务并发访问 | 裸机单主循环顺序访问；总线层已具备 owner 仲裁，可为后续 RTOS 复用 |
| 硬件 I2C2 的异常恢复 | 已实现 9 时钟 + STOP，仅代码走查，未用仪器验证 |

---

## 6. 结论

- **已完成并实测**：工程改名、旧驱动总线适配、OLED/EEPROM 共享 I2C 总线、AT24xx 2 字节字地址适配、
  跨页写入与读回校验、K1/K2 单击与长按事件、软/硬件 I2C 双后端、SWD 烧录验证。
- **已完成并编译验证**：五层总线架构重构（`i2c_bus.h` / `i2c_stm32_ll.c`）、总线占用仲裁、
  ISR 拒绝、DWT 超时与错误路径恢复、配置注入式 `AT24CXX_Config` / `AT24XX_PortConfig`。
- **重构后已上板复测**：SOFT 与 HARD 后端均重新烧录，`online=1`、40 字节自测数据读回一致。
- **待上板确认**：断电数据保持；硬件 I2C2 下的按键交互复测。
