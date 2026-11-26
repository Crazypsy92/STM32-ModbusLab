/**
 * @file bsp_includes.h
 * @brief BSP 总头文件（原 MyApplication.h）
 * @details 裸机版统一头文件入口：包含系统基础头文件、标准库头文件及全部 BSP 模块头文件。
 *          已移除 FreeRTOS 相关头文件，工程不再依赖任何 RTOS。
 * @author 西伯利亚小菜狗
 * @version 2.0 (bare-metal)
 */
#ifndef __BSP_INCLUDES_H__
#define __BSP_INCLUDES_H__

/* Includes ------------------------------------------------------------------*/
#include "main.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

/* BSP 模块 */
#include "delay.h"          /* 统一时间基准 Timebase */
#include "multi_button.h"   /* MultiButton 状态机（裸机） */
#include "key_port.h"       /* 按键硬件适配层 */
#include "i2c_bus.h"        /* 抽象 I2C 总线（仲裁 / 事务 / 统一错误码） */
#include "i2c_stm32_ll.h"   /* STM32 LL 软 / 硬件 I2C 后端 */
#include "bsp_i2c.h"        /* 板级共享 I2C 总线（OLED + AT24xx） */
#include "oled_port.h"      /* OLED 移植层（共享总线收发） */
#include "oled.h"           /* OLED 设备驱动 */
#include "at24cxx.h"        /* AT24Cxx EEPROM 驱动 */
#include "at24xx_port.h"    /* AT24xx 移植层（地址/页大小/在线状态） */
#include "app_test.h"       /* 应用层测试模块 */

#ifndef DEBUG
#define DEBUG 0
#endif
#if DEBUG
#define DEBUG_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...) do {} while (0)
#endif

#ifndef App_Printf
#define App_Printf(...) printf(__VA_ARGS__)
#endif

#endif /* __BSP_INCLUDES_H__ */
