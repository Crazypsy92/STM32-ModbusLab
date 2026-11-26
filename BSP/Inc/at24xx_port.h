/**
 * @file    at24xx_port.h
 * @brief   AT24xx 移植层接口（板级地址 / 页大小 / 配置注入）
 * @details 板级参数集中在 at24xx_port.c 顶部；驱动 at24cxx.c/h 与 MCU 无关。
 */
#ifndef __AT24XX_PORT_H__
#define __AT24XX_PORT_H__

#include "at24cxx.h"
#include "i2c_bus.h"

/* ==================== 板级默认参数（按实际硬件修改） ==================== */

/* 7 位从机地址：默认 0x50（A0/A1/A2 接地） */
#define AT24XX_I2C_ADDR         0x50
/* 页写大小：32/64 字节页对应 2 字节字地址，8/16 字节页对应 1 字节字地址
   实测本模块为 2 字节字地址型号（AT24C32 及以上） */
#define AT24XX_PAGE_SIZE        AT24C32_PAGE_SIZE
/* 芯片容量（字节）：AT24C32 = 4096，AT24C64 = 8192 */
#define AT24XX_CAPACITY         4096U

/**
 * @brief 板级端口配置（支持运行时注入，0/NULL 字段使用板级默认值）
 */
typedef struct {
    i2c_bus_t          *bus;            /**< 绑定的 I2C 总线；NULL 表示使用板级共享总线 */
    uint8_t             dev_addr;       /**< 7 位从机地址；0 表示使用默认 0x50 */
    uint16_t            page_size;      /**< 页写大小；0 表示使用默认值 */
    uint16_t            capacity;       /**< 芯片容量（字节）；0 表示使用默认值 */
    uint32_t            clock_hz;       /**< 总线速率（Hz）；0 表示使用板级默认速率 */
    i2c_bus_addr_mode_t addr_mode;      /**< 地址位宽，默认 I2C_BUS_ADDR_7BIT */
    uint16_t            write_cycle_ms; /**< 写周期等待（毫秒）；0 表示使用默认值 */
} AT24XX_PortConfig;

/** @brief 全局 AT24xx 设备句柄（在 at24xx_port.c 中定义） */
extern AT24CXX_Device g_at24cxx;

/** @brief 设备在线状态：1=检测到，0=未检测到 */
extern uint8_t g_at24xx_online;

/**
 * @brief  通过外部配置注入初始化 AT24xx 设备实例
 * @param  dev    要初始化的设备对象（不能为 NULL）
 * @param  config 总线与芯片参数（内部持久化保存，入参可为临时局部变量）
 * @return AT24CXX_OK 成功；其他为失败错误码
 */
AT24CXX_Result AT24XX_PortInitWithConfig(AT24CXX_Device *dev,
                                         const AT24XX_PortConfig *config);

/**
 * @brief  默认初始化入口：绑定板级共享 I2C 总线并初始化 g_at24cxx
 * @note   需在 Timebase_Init() 之后调用；结果同步写入 g_at24xx_online
 */
AT24CXX_Result AT24XX_PortInit(void);

/**
 * @brief  重置端口层全局状态（主要供单元测试复位环境使用）
 */
void AT24XX_Port_Reset(void);

#endif /* __AT24XX_PORT_H__ */
