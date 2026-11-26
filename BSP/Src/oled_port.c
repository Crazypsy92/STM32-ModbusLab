/**
 * @file    oled_port.c
 * @brief   OLED 移植层实现（STM32F1 + LL 库，经共享 I2C 总线访问）
 * @details 总线由 bsp_i2c.c 统一管理，本文件只负责附加控制字节并发送。
 *          换 MCU 时保留 oled_port.h 的接口，重写本文件即可；
 *          i2c_stm32_ll.c 是 STM32F1 的总线适配层，换平台时需一并替换。
 */
#include "oled_port.h"
#include "bsp_i2c.h"
#include <string.h>

/* ================================================================================== */
/* 板级配置（按实际硬件修改）                                         */
/* ================================================================================== */

/* SSD1306 7 位从机地址（底层发送时左移一位得到 0x78） */
#define OLED_I2C_DEV_ADDR       0x3C

/* SSD1306 控制字节：Co=0, D/C#=0 命令 / D/C#=1 数据 */
#define OLED_CTRL_CMD           0x00
#define OLED_CTRL_DATA          0x40

/* 最大一帧：1 字节控制 + 128 字节数据 */
#define OLED_PORT_BUF_SIZE      129U

/* ================================================================================== */
/* 内部变量                                                           */
/* ================================================================================== */

/** @brief OLED 在共享总线上的唯一身份标识（取静态变量地址，非 NULL 即可） */
static const uint8_t s_oled_owner_tag;

/** @brief OLED 使用的事务配置：与 AT24xx 共用速率与 7 位地址 */
static const i2c_bus_config_t s_oled_cfg = {
    .clock_hz  = BSP_I2C_CLK_SPEED,
    .addr_mode = I2C_BUS_ADDR_7BIT,
};

static uint8_t s_buf[OLED_PORT_BUF_SIZE];

/* ================================================================================== */
/* 移植接口实现                                                       */
/* ================================================================================== */

void OLED_PortInit(void)
{
    /* 与 AT24xx 共用同一条总线，首次调用时由 bsp_i2c.c 完成初始化 */
    (void)BSP_I2C1_GetBus();
}

/**
 * @brief 发送一帧：控制字节 + 数据
 * @param ctrl 控制字节（0x00 命令 / 0x40 数据）
 * @return OLED_PORT_OK 成功，OLED_PORT_ERROR 失败
 */
static OLED_PortResult OLED_PortSend(uint8_t ctrl, const uint8_t *data, uint16_t len)
{
    i2c_bus_msg_t msg;
    i2c_bus_t *bus;
    int ret;

    if (data == NULL || len == 0U || (uint16_t)(len + 1U) > OLED_PORT_BUF_SIZE) {
        return OLED_PORT_ERROR;
    }

    bus = BSP_I2C1_GetBus();
    if (bus == NULL) {
        return OLED_PORT_ERROR;
    }

    s_buf[0] = ctrl;
    memcpy(&s_buf[1], data, len);

    /* 占用共享总线，事务结束后立即释放，避免与 AT24xx 交叉 */
    if (i2c_bus_acquire(bus, &s_oled_owner_tag, &s_oled_cfg) != I2C_BUS_OK) {
        return OLED_PORT_ERROR;
    }

    msg.dev_addr      = OLED_I2C_DEV_ADDR;
    msg.mem_addr      = 0U;
    msg.mem_addr_size = 0U;
    msg.mode          = I2C_BUS_XFER_STREAM;
    msg.tx            = s_buf;
    msg.rx            = NULL;
    msg.len           = (uint16_t)(len + 1U);

    ret = i2c_bus_transfer(bus, &s_oled_owner_tag, &msg);
    (void)i2c_bus_release(bus, &s_oled_owner_tag);

    return (ret == I2C_BUS_OK) ? OLED_PORT_OK : OLED_PORT_ERROR;
}

OLED_PortResult OLED_PortWriteCmd(uint8_t cmd)
{
    return OLED_PortSend(OLED_CTRL_CMD, &cmd, 1);
}

OLED_PortResult OLED_PortWriteData(const uint8_t *data, uint16_t len)
{
    return OLED_PortSend(OLED_CTRL_DATA, data, len);
}
