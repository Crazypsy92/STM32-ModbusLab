/**
 * @file    at24xx_port.c
 * @brief   AT24xx 移植层实现（STM32F1，复用 OLED 的共享 I2C 总线）
 * @details 负责把板级参数（地址 / 页大小 / 容量 / 速率）转换成协议层配置，
 *          支持通过 AT24XX_PortInitWithConfig 注入任意 I2C 总线与芯片参数。
 */
#include "at24xx_port.h"
#include "bsp_i2c.h"
#include <string.h>

/* ================================================================================== */
/* 全局对象                                                           */
/* ================================================================================== */

AT24CXX_Device g_at24cxx;
uint8_t        g_at24xx_online = 0;

/* ================================================================================== */
/* 移植接口实现                                                       */
/* ================================================================================== */

AT24CXX_Result AT24XX_PortInitWithConfig(AT24CXX_Device *dev,
                                         const AT24XX_PortConfig *config)
{
    AT24CXX_Config cfg = {0};

    if (dev == NULL || config == NULL) {
        return AT24CXX_ERROR_PARAM;
    }

    cfg.bus             = (config->bus != NULL) ? config->bus : BSP_I2C1_GetBus();
    cfg.bus_cfg.clock_hz = (config->clock_hz > 0U) ? config->clock_hz : BSP_I2C_CLK_SPEED;
    cfg.bus_cfg.addr_mode = config->addr_mode;
    cfg.dev_addr        = (config->dev_addr != 0U) ? config->dev_addr : AT24XX_I2C_ADDR;
    cfg.page_size       = (config->page_size != 0U) ? config->page_size : AT24XX_PAGE_SIZE;
    cfg.capacity        = (config->capacity != 0U) ? config->capacity : AT24XX_CAPACITY;
    cfg.write_cycle_ms  = (config->write_cycle_ms != 0U) ?
                          config->write_cycle_ms : AT24CXX_WRITE_CYCLE_MS;

    AT24CXX_Result res = AT24Cxx_Init(dev, &cfg);
    if (dev == &g_at24cxx) {
        g_at24xx_online = (res == AT24CXX_OK) ? 1U : 0U;
    }
    return res;
}

AT24CXX_Result AT24XX_PortInit(void)
{
    static const AT24XX_PortConfig def_cfg = {
        .bus            = NULL,             /* 自动绑定板级共享 I2C 总线 */
        .dev_addr       = AT24XX_I2C_ADDR,
        .page_size      = AT24XX_PAGE_SIZE,
        .capacity       = AT24XX_CAPACITY,
        .clock_hz       = BSP_I2C_CLK_SPEED,
        .addr_mode      = I2C_BUS_ADDR_7BIT,
        .write_cycle_ms = AT24CXX_WRITE_CYCLE_MS,
    };
    return AT24XX_PortInitWithConfig(&g_at24cxx, &def_cfg);
}

void AT24XX_Port_Reset(void)
{
    memset(&g_at24cxx, 0, sizeof(g_at24cxx));
    g_at24xx_online = 0U;
}
