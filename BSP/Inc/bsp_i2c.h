/**
 * @file    bsp_i2c.h
 * @brief   板级 I2C 总线接口（OLED 与 AT24xx 共用）
 * @details 同一物理 I2C 外设只在这里创建一次，多个设备通过
 *          BSP_I2C1_GetBus() 取得同一个 i2c_bus_t 共享使用。
 *          引脚 / 外设 / 软件或硬件后端统一在 bsp_i2c.c 顶部配置。
 */
#ifndef __BSP_I2C_H__
#define __BSP_I2C_H__

#include "i2c_bus.h"
#include "main.h"       /* GPIOB / I2C2 / LL_GPIO_PIN_x */

/* ==================== 编译选项 ==================== */

/* 1: 软件 IIC（GPIO 模拟，默认）  0: 硬件 IIC（I2C2）
   可在 CMake 命令行覆盖：-DBSP_I2C_BACKEND=HARD */
#ifndef BSP_I2C_BACKEND_SOFT
#define BSP_I2C_BACKEND_SOFT    1
#endif

/* ==================== 板级接线 ==================== */

/* OLED 与 AT24xx 共用 PB10(SCL) / PB11(SDA) */
#define BSP_I2C_SCL_PORT        GPIOB
#define BSP_I2C_SCL_PIN         LL_GPIO_PIN_10
#define BSP_I2C_SDA_PORT        GPIOB
#define BSP_I2C_SDA_PIN         LL_GPIO_PIN_11

#define BSP_I2C_PERIPH          I2C2            /* 硬件 IIC 外设 */
#define BSP_I2C_SW_DELAY_US     5U              /* 软件 IIC 默认半周期 */

/* 总线速率：软件后端按 5us 半周期保持 100k；硬件后端用 400k 快速模式 */
#if BSP_I2C_BACKEND_SOFT
#define BSP_I2C_CLK_SPEED       100000U
#else
#define BSP_I2C_CLK_SPEED       400000U
#endif

/** @brief 板级 I2C 总线标识（当前板卡仅一条物理总线） */
typedef enum {
    BSP_I2C_BUS_1 = 0,
    BSP_I2C_BUS_MAX
} bsp_i2c_id_t;

/**
 * @brief  获取指定 ID 的 I2C 抽象总线（按需惰性初始化对应硬件）
 * @param  id BSP_I2C_BUS_1
 * @return 总线句柄；参数非法返回 NULL
 */
i2c_bus_t *BSP_I2C_GetBus(bsp_i2c_id_t id);

/**
 * @brief  获取板级唯一 I2C 总线（OLED 与 AT24xx 共用）
 */
i2c_bus_t *BSP_I2C1_GetBus(void);

/**
 * @brief  查询指定总线是否已完成硬件初始化并处于就绪态
 */
uint8_t BSP_I2C_IsBusActive(bsp_i2c_id_t id);

/**
 * @brief  对指定总线执行硬件级异常恢复
 * @param  bus 目标抽象总线指针
 * @return I2C_BUS_OK 成功，负值失败
 */
int BSP_I2C_RecoverBus(i2c_bus_t *bus);

/**
 * @brief  重置板级总线单例状态（主要供单元测试复位环境使用）
 */
void BSP_I2C_ResetBuses(void);

#endif /* __BSP_I2C_H__ */
