/**
 * @file    oled_port.h
 * @brief   OLED 移植层接口（与 MCU 无关）
 * @details OLED.c 只依赖本文件的三个接口，不包含任何 MCU/总线头文件。
 *          板级总线（引脚 / 外设 / 软件或硬件 IIC）由 bsp_i2c.c 统一管理，
 *          与 AT24xx 共用同一条总线。
 */
#ifndef __OLED_PORT_H__
#define __OLED_PORT_H__

#include <stdint.h>

/* ==================== 移植接口（OLED.c 调用） ==================== */

/**
 * @brief 底层总线操作结果
 */
typedef enum {
    OLED_PORT_OK = 0,       /**< 发送成功 */
    OLED_PORT_ERROR         /**< 发送失败（参数错误 / ACK 失败 / 总线忙等） */
} OLED_PortResult;

/** @brief 初始化底层总线（共享 I2C），由 OLED_Init 自动调用 */
void OLED_PortInit(void);

/**
 * @brief  发送一条命令（自动附加控制字节 0x00）
 * @return OLED_PORT_OK 成功，OLED_PORT_ERROR 失败
 */
OLED_PortResult OLED_PortWriteCmd(uint8_t cmd);

/**
 * @brief  发送一段显存数据（自动附加控制字节 0x40）
 * @return OLED_PORT_OK 成功，OLED_PORT_ERROR 失败
 */
OLED_PortResult OLED_PortWriteData(const uint8_t *data, uint16_t len);

#endif /* __OLED_PORT_H__ */
