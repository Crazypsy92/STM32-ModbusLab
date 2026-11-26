/**
 * @file    at24cxx.c
 * @brief   AT24Cxx 系列 I2C EEPROM 驱动实现
 * @details 每次事务前 acquire 总线，事务结束立即 release。
 *          字地址宽度与读写方向都放在 i2c_bus_msg_t 中传递，
 *          驱动内部不修改任何共享总线状态。
 */
#include "at24cxx.h"
#include "delay.h"

/* ================================================================================== */
/* 内部辅助                                                           */
/* ================================================================================== */

/** @brief 记录并返回结果 */
static AT24CXX_Result at24cxx_set_result(AT24CXX_Device *dev, AT24CXX_Result res)
{
    if (dev != NULL) {
        dev->last_result = res;
    }
    return res;
}

/** @brief 总线状态码 -> EEPROM 结果码 */
static AT24CXX_Result at24cxx_map_bus(int ret)
{
    switch (ret) {
        case I2C_BUS_OK:             return AT24CXX_OK;
        case I2C_BUS_ERROR_PARAM:    return AT24CXX_ERROR_PARAM;
        case I2C_BUS_ERROR_TIMEOUT:  return AT24CXX_ERROR_TIMEOUT;
        case I2C_BUS_ERROR_NACK:     return AT24CXX_ERROR_NACK;
        default:                     return AT24CXX_ERROR_BUS;
    }
}

/** @brief 地址 + 长度是否落在芯片容量内 */
static int at24cxx_range_ok(const AT24CXX_Device *dev, uint16_t addr, uint16_t len)
{
    return ((uint32_t)addr + (uint32_t)len <= (uint32_t)dev->capacity) ? 1 : 0;
}

/**
 * @brief 等待内部写周期结束（ACK polling）
 * @details 写周期内芯片对地址不应答，一旦应答即表示擦写完成。
 *          每 1ms 探测一次，总预算约 write_cycle_ms。
 * @return AT24CXX_OK 已就绪；AT24CXX_ERROR_TIMEOUT 预算耗尽
 */
static AT24CXX_Result at24cxx_wait_ready(AT24CXX_Device *dev)
{
    uint16_t waited;

    /* 预算 write_cycle_ms，多探一次留余量（AT24C32 写周期上限即为 10ms） */
    for (waited = 0U; waited <= dev->write_cycle_ms; waited++) {
        int ret;

        Delay_ms(1U);
        if (i2c_bus_acquire(dev->bus, dev, &dev->bus_cfg) != I2C_BUS_OK) {
            return AT24CXX_ERROR_BUS;
        }
        ret = i2c_bus_probe(dev->bus, dev, dev->dev_addr);
        (void)i2c_bus_release(dev->bus, dev);

        if (ret == I2C_BUS_OK) {
            return AT24CXX_OK;
        }
    }
    return AT24CXX_ERROR_TIMEOUT;
}

/**
 * @brief 写一页（不超过 page_size），并等待内部写周期结束
 */
static AT24CXX_Result at24cxx_write_page(AT24CXX_Device *dev, uint16_t addr,
                                         const uint8_t *buf, uint16_t len)
{
    i2c_bus_msg_t msg;
    int ret;

    if (dev == NULL || dev->bus == NULL || buf == NULL || len == 0U) {
        return AT24CXX_ERROR_PARAM;
    }
    if (len > dev->page_size) {
        return AT24CXX_ERROR_PARAM;
    }
    if (!at24cxx_range_ok(dev, addr, len)) {
        return AT24CXX_ERROR_OVERFLOW;
    }
    /* 页写不能跨越页边界（AT24Cxx 会回卷覆盖页首数据） */
    if ((uint16_t)(addr % dev->page_size) + len > dev->page_size) {
        return AT24CXX_ERROR_PARAM;
    }

    if (i2c_bus_acquire(dev->bus, dev, &dev->bus_cfg) != I2C_BUS_OK) {
        return AT24CXX_ERROR_BUS;
    }

    msg.dev_addr      = dev->dev_addr;
    msg.mem_addr      = addr;
    msg.mem_addr_size = dev->addr_bytes;
    msg.mode          = I2C_BUS_XFER_MEM;
    msg.tx            = buf;
    msg.rx            = NULL;
    msg.len           = len;

    ret = i2c_bus_transfer(dev->bus, dev, &msg);
    (void)i2c_bus_release(dev->bus, dev);

    if (ret != I2C_BUS_OK) {
        return at24cxx_map_bus(ret);
    }

    /* 必须等待内部擦写完成，否则后续读写会失败；用 ACK polling 替代固定延时 */
    return at24cxx_wait_ready(dev);
}

/* ================================================================================== */
/* 对外接口                                                           */
/* ================================================================================== */

AT24CXX_Result AT24Cxx_Init(AT24CXX_Device *dev, const AT24CXX_Config *cfg)
{
    int ret;

    if (dev == NULL) {
        return AT24CXX_ERROR_PARAM;
    }
    if (cfg == NULL || cfg->bus == NULL) {
        return at24cxx_set_result(dev, AT24CXX_ERROR_PARAM);
    }
    if (cfg->dev_addr > 0x7FU || cfg->page_size == 0U || cfg->capacity == 0U ||
        cfg->write_cycle_ms == 0U || cfg->bus_cfg.clock_hz == 0U) {
        return at24cxx_set_result(dev, AT24CXX_ERROR_PARAM);
    }
    /* 1 字节字地址只支持 256 B 寻址（AT24C02）；04/08/16 需要切换从机地址分页，未实现 */
    if (cfg->page_size < AT24C32_PAGE_SIZE && cfg->capacity > 256U) {
        return at24cxx_set_result(dev, AT24CXX_ERROR_PARAM);
    }

    dev->bus            = cfg->bus;
    dev->bus_cfg        = cfg->bus_cfg;
    dev->dev_addr       = cfg->dev_addr;
    dev->page_size      = cfg->page_size;
    dev->capacity       = cfg->capacity;
    dev->write_cycle_ms = cfg->write_cycle_ms;
    /* AT24C32 及以上使用 2 字节字地址，AT24C02/04/08/16 使用 1 字节 */
    dev->addr_bytes     = (cfg->page_size >= AT24C32_PAGE_SIZE) ? 2U : 1U;
    dev->last_result    = AT24CXX_OK;

    if (i2c_bus_acquire(dev->bus, dev, &dev->bus_cfg) != I2C_BUS_OK) {
        return at24cxx_set_result(dev, AT24CXX_ERROR_BUS);
    }
    ret = i2c_bus_probe(dev->bus, dev, dev->dev_addr);
    (void)i2c_bus_release(dev->bus, dev);

    return at24cxx_set_result(dev, at24cxx_map_bus(ret));
}

AT24CXX_Result AT24Cxx_WriteByte(AT24CXX_Device *dev, uint16_t addr, uint8_t data)
{
    AT24CXX_Result res = at24cxx_write_page(dev, addr, &data, 1U);
    return at24cxx_set_result(dev, res);
}

AT24CXX_Result AT24Cxx_WriteBuf(AT24CXX_Device *dev, uint16_t addr,
                                const uint8_t *buf, uint16_t len)
{
    uint16_t remaining;
    uint16_t curr_addr;
    uint16_t offset;

    if (dev == NULL || dev->bus == NULL || buf == NULL || len == 0U) {
        return at24cxx_set_result(dev, AT24CXX_ERROR_PARAM);
    }
    if (!at24cxx_range_ok(dev, addr, len)) {
        return at24cxx_set_result(dev, AT24CXX_ERROR_OVERFLOW);
    }

    remaining = len;
    curr_addr = addr;
    offset    = 0U;

    while (remaining > 0U) {
        /* 当前页剩余空间，跨页时拆成多次页写 */
        uint16_t page_remain = (uint16_t)(dev->page_size - (curr_addr % dev->page_size));
        uint16_t chunk = (remaining > page_remain) ? page_remain : remaining;
        AT24CXX_Result res = at24cxx_write_page(dev, curr_addr, &buf[offset], chunk);

        if (res != AT24CXX_OK) {
            return at24cxx_set_result(dev, res);
        }

        curr_addr += chunk;
        offset    += chunk;
        remaining -= chunk;
    }

    return at24cxx_set_result(dev, AT24CXX_OK);
}

AT24CXX_Result AT24Cxx_ReadByte(AT24CXX_Device *dev, uint16_t addr, uint8_t *data)
{
    if (data == NULL) {
        return at24cxx_set_result(dev, AT24CXX_ERROR_PARAM);
    }
    return AT24Cxx_ReadBuf(dev, addr, data, 1U);
}

AT24CXX_Result AT24Cxx_ReadBuf(AT24CXX_Device *dev, uint16_t addr,
                               uint8_t *buf, uint16_t len)
{
    i2c_bus_msg_t msg;
    int ret;

    if (dev == NULL || dev->bus == NULL || buf == NULL || len == 0U) {
        return at24cxx_set_result(dev, AT24CXX_ERROR_PARAM);
    }
    if (!at24cxx_range_ok(dev, addr, len)) {
        return at24cxx_set_result(dev, AT24CXX_ERROR_OVERFLOW);
    }

    if (i2c_bus_acquire(dev->bus, dev, &dev->bus_cfg) != I2C_BUS_OK) {
        return at24cxx_set_result(dev, AT24CXX_ERROR_BUS);
    }

    msg.dev_addr      = dev->dev_addr;
    msg.mem_addr      = addr;
    msg.mem_addr_size = dev->addr_bytes;
    msg.mode          = I2C_BUS_XFER_MEM;
    msg.tx            = NULL;
    msg.rx            = buf;
    msg.len           = len;

    ret = i2c_bus_transfer(dev->bus, dev, &msg);
    (void)i2c_bus_release(dev->bus, dev);

    return at24cxx_set_result(dev, at24cxx_map_bus(ret));
}
