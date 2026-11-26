/**
 * @file    at24cxx.h
 * @brief   AT24Cxx 系列 I2C EEPROM 驱动（与 MCU 无关）
 * @details 纯协议层：不包含任何 MCU / LL / HAL 头文件，只依赖 i2c_bus.h
 *          定义的抽象总线与事务消息。板级地址 / 页大小见 at24xx_port.c。
 *
 *          字地址宽度（1/2 字节）属于芯片属性，但只在单次事务消息中传递，
 *          不修改共享总线状态，避免与 OLED 等其它设备互相污染。
 */
#ifndef __AT24CXX_H__
#define __AT24CXX_H__

#include <stdint.h>
#include "i2c_bus.h"

/* 常用 AT24CXX 型号页写大小（按实际芯片选择） */
#define AT24C02_PAGE_SIZE    8    /* AT24C02/04/08A 页大小：8 字节 */
#define AT24C16_PAGE_SIZE    16   /* AT24C16 页大小：16 字节 */
#define AT24C32_PAGE_SIZE    32   /* AT24C32/64 页大小：32 字节 */
#define AT24C128_PAGE_SIZE   64   /* AT24C128/256 页大小：64 字节 */

/* 写周期等待（毫秒），AT24Cxx 典型 5ms，取 10ms 留余量 */
#ifndef AT24CXX_WRITE_CYCLE_MS
#define AT24CXX_WRITE_CYCLE_MS   10U
#endif

/**
 * @brief 操作结果（负值为错误，与总线状态码风格一致）
 */
typedef enum {
    AT24CXX_OK              = 0,    /**< 成功 */
    AT24CXX_ERROR_PARAM     = -1,   /**< 参数非法 */
    AT24CXX_ERROR_TIMEOUT   = -2,   /**< 总线等待超时 */
    AT24CXX_ERROR_NACK      = -3,   /**< 设备无应答 */
    AT24CXX_ERROR_BUS       = -4,   /**< 总线占用 / 传输失败 */
    AT24CXX_ERROR_OVERFLOW  = -5    /**< 地址或长度越界 */
} AT24CXX_Result;

/**
 * @brief 设备初始化配置（由移植层填充，支持运行时注入）
 */
typedef struct {
    i2c_bus_t       *bus;            /**< 绑定的抽象 I2C 总线（需可写以完成占用/释放） */
    i2c_bus_config_t bus_cfg;        /**< 总线速率 / 地址位宽 */
    uint16_t         dev_addr;       /**< 7 位从机地址（0x50 起） */
    uint16_t         page_size;      /**< 页写大小 */
    uint16_t         capacity;       /**< 芯片容量（字节），用于越界保护 */
    uint16_t         write_cycle_ms; /**< 内部写周期等待（毫秒） */
} AT24CXX_Config;

/**
 * @brief AT24CXX 设备对象
 */
typedef struct {
    i2c_bus_t       *bus;            /**< 绑定的抽象 I2C 总线 */
    i2c_bus_config_t bus_cfg;        /**< 本设备使用的事务配置 */
    uint16_t         dev_addr;       /**< 7 位从机地址 */
    uint16_t         page_size;      /**< 页写大小 */
    uint16_t         capacity;       /**< 容量（字节） */
    uint16_t         write_cycle_ms; /**< 内部写周期等待（毫秒） */
    uint8_t          addr_bytes;     /**< 字地址字节数：1 或 2 */
    AT24CXX_Result   last_result;    /**< 最近一次操作结果 */
} AT24CXX_Device;

/**
 * @brief  初始化 AT24CXX 设备并检测在线
 * @param  dev 设备对象（不能为 NULL）
 * @param  cfg 初始化配置（内部持久化保存，入参可为临时变量）
 * @return AT24CXX_OK 成功；其他为错误码
 * @note   字地址字节数按页大小推导：8/16 页 -> 1 字节，32/64 页 -> 2 字节
 */
AT24CXX_Result AT24Cxx_Init(AT24CXX_Device *dev, const AT24CXX_Config *cfg);

/**
 * @brief  写入 1 字节（阻塞，内部等待写周期）
 */
AT24CXX_Result AT24Cxx_WriteByte(AT24CXX_Device *dev, uint16_t addr, uint8_t data);

/**
 * @brief  批量写入（按页自动拆分，跨页不会回卷覆盖）
 */
AT24CXX_Result AT24Cxx_WriteBuf(AT24CXX_Device *dev, uint16_t addr,
                                const uint8_t *buf, uint16_t len);

/**
 * @brief  读取 1 字节
 * @param  data 输出缓冲区（不能为 NULL）
 */
AT24CXX_Result AT24Cxx_ReadByte(AT24CXX_Device *dev, uint16_t addr, uint8_t *data);

/**
 * @brief  批量读取（顺序读，自动处理 NACK / STOP）
 */
AT24CXX_Result AT24Cxx_ReadBuf(AT24CXX_Device *dev, uint16_t addr,
                               uint8_t *buf, uint16_t len);

#endif /* __AT24CXX_H__ */
