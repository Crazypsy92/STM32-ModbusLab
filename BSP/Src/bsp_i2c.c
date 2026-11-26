/**
 * @file    bsp_i2c.c
 * @brief   板级 I2C 总线实现（STM32F1）
 * @details 全工程唯一的 I2C 引脚 / 外设配置点：OLED 与 AT24xx 共用。
 *          设备不再各自初始化 I2C，避免后初始化者覆盖前者的速率配置。
 */
#include "bsp_i2c.h"
#include "i2c_stm32_ll.h"
#include "main.h"
#include <string.h>

/* ================================================================================== */
/* 板级硬件描述                                                       */
/* ================================================================================== */

static const i2c_stm32_ll_hw_t s_i2c1_hw = {
#if BSP_I2C_BACKEND_SOFT
    .backend    = I2C_STM32_BACKEND_SOFT,
    .periph     = NULL,
#else
    .backend    = I2C_STM32_BACKEND_HW,
    .periph     = BSP_I2C_PERIPH,
#endif
    .scl_port   = BSP_I2C_SCL_PORT,
    .scl_pin    = BSP_I2C_SCL_PIN,
    .sda_port   = BSP_I2C_SDA_PORT,
    .sda_pin    = BSP_I2C_SDA_PIN,
    .sw_delay_us = BSP_I2C_SW_DELAY_US,
};

static i2c_stm32_ll_t s_i2c_instances[BSP_I2C_BUS_MAX];
static uint8_t        s_i2c_inited[BSP_I2C_BUS_MAX] = {0U};

/* ================================================================================== */
/* 对外接口                                                           */
/* ================================================================================== */

uint8_t BSP_I2C_IsBusActive(bsp_i2c_id_t id)
{
    if ((uint32_t)id >= (uint32_t)BSP_I2C_BUS_MAX) {
        return 0U;
    }
    return s_i2c_inited[id];
}

i2c_bus_t *BSP_I2C_GetBus(bsp_i2c_id_t id)
{
    if ((uint32_t)id >= (uint32_t)BSP_I2C_BUS_MAX) {
        return NULL;
    }
    if (!s_i2c_inited[id]) {
        i2c_stm32_ll_init(&s_i2c_instances[id], &s_i2c1_hw);
        s_i2c_inited[id] = 1U;
    }
    return i2c_stm32_ll_bus(&s_i2c_instances[id]);
}

i2c_bus_t *BSP_I2C1_GetBus(void)
{
    return BSP_I2C_GetBus(BSP_I2C_BUS_1);
}

int BSP_I2C_RecoverBus(i2c_bus_t *bus)
{
    if (bus == NULL) {
        return I2C_BUS_ERROR_PARAM;
    }
    /* 仅接受板级实际注册并已初始化的合法总线句柄，非法外来指针安全拒绝 */
    for (uint32_t i = 0; i < (uint32_t)BSP_I2C_BUS_MAX; i++) {
        if (s_i2c_inited[i] && bus == i2c_stm32_ll_bus(&s_i2c_instances[i])) {
            return i2c_stm32_ll_recover(&s_i2c_instances[i]);
        }
    }
    return I2C_BUS_ERROR_PARAM;
}

void BSP_I2C_ResetBuses(void)
{
    for (uint32_t i = 0; i < (uint32_t)BSP_I2C_BUS_MAX; i++) {
        s_i2c_inited[i] = 0U;
        memset(&s_i2c_instances[i], 0, sizeof(s_i2c_instances[i]));
    }
}
