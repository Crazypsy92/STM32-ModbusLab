/**
 * @file    i2c_stm32_ll.h
 * @brief   I2C 总线的 STM32F1 LL 后端接口
 * @details 板级只在本文件的 hw 描述结构体中填写引脚 / 外设 / 后端类型，
 *          具体时序全部由 i2c_stm32_ll.c 产生。提供软件模拟与硬件 I2C2
 *          两种后端，运行时通过 hw->backend 选择。
 */
#ifndef __I2C_STM32_LL_H__
#define __I2C_STM32_LL_H__

#include "i2c_bus.h"
#include "main.h"
#include "stm32f1xx_ll_i2c.h"

/**
 * @brief 后端类型
 */
typedef enum {
    I2C_STM32_BACKEND_SOFT = 0,     /**< GPIO 软件模拟 I2C */
    I2C_STM32_BACKEND_HW            /**< STM32 硬件 I2C 外设 */
} i2c_stm32_backend_t;

/**
 * @brief 板级硬件描述（只读，生命周期覆盖整个运行期）
 */
typedef struct {
    i2c_stm32_backend_t backend;    /**< 软件 / 硬件后端 */
    I2C_TypeDef        *periph;     /**< 硬件后端使用，如 I2C2 */
    GPIO_TypeDef       *scl_port;   /**< SCL 端口 */
    uint32_t            scl_pin;    /**< SCL 引脚 */
    GPIO_TypeDef       *sda_port;   /**< SDA 端口 */
    uint32_t            sda_pin;    /**< SDA 引脚 */
    uint16_t            sw_delay_us;/**< 软件后端默认半周期（us），按 clock_hz 会自动重算 */
} i2c_stm32_ll_hw_t;

/**
 * @brief LL 后端实例（内含抽象总线对象）
 */
typedef struct {
    i2c_bus_t                bus;           /**< 对外暴露的抽象总线 */
    const i2c_stm32_ll_hw_t *hw;            /**< 板级硬件描述 */
    i2c_bus_config_t         current;       /**< 当前生效配置 */
    uint8_t                  configured;    /**< 是否已完成一次 configure */
    uint16_t                 sw_half_us;    /**< 软件后端当前半周期 */
    uint32_t                 recover_count; /**< 硬件恢复次数（调试用） */
    int32_t                  last_error;    /**< 最近一次错误码 */
} i2c_stm32_ll_t;

/**
 * @brief 初始化 LL 后端并填充抽象总线
 * @param ll 后端实例（不能为 NULL）
 * @param hw 板级硬件描述（只读，生命周期需覆盖 ll）
 */
void i2c_stm32_ll_init(i2c_stm32_ll_t *ll, const i2c_stm32_ll_hw_t *hw);

/**
 * @brief 取得实例内的抽象总线句柄
 */
i2c_bus_t *i2c_stm32_ll_bus(i2c_stm32_ll_t *ll);

/**
 * @brief 硬件级异常恢复（软件后端：9 时钟 + STOP；硬件后端：复位外设并重配）
 * @return I2C_BUS_OK 成功，负值失败
 */
int i2c_stm32_ll_recover(i2c_stm32_ll_t *ll);

/** @brief 查询恢复次数 */
uint32_t i2c_stm32_ll_get_recover_count(const i2c_stm32_ll_t *ll);

/** @brief 查询最近一次错误码 */
int32_t i2c_stm32_ll_get_last_error(const i2c_stm32_ll_t *ll);

/** @brief 清除最近一次错误码 */
void i2c_stm32_ll_clear_error(i2c_stm32_ll_t *ll);

#endif /* __I2C_STM32_LL_H__ */
