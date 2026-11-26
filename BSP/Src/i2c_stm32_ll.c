/**
 * @file    i2c_stm32_ll.c
 * @brief   I2C 总线的 STM32F1 LL 实现（软件模拟 + 硬件 I2C）
 * @details 本文件是全工程唯一产生 I2C 时序的地方，上层只看到 i2c_bus.h。
 *
 *          安全策略：
 *          - 所有等待标志都带 DWT 周期超时，异常时不会死锁主循环；
 *          - 硬件后端的错误路径一定发 STOP（清 AF + STOP），避免 SCL 被永久拉低；
 *          - 上电与恢复阶段执行 9 个时钟脉冲 + STOP，解救被从机拉死的总线；
 *          - 配置切换、传输、探测都先做参数与状态校验。
 */
#include "i2c_stm32_ll.h"
#include "delay.h"
#include <string.h>

/* ================================================================================== */
/* 超时预算（微秒，基于 72MHz DWT 周期计数，与 CPU 主频无关）                          */
/* ================================================================================== */

#define I2C_FLAG_TIMEOUT_US      5000U    /**< 单标志等待（SB/ADDR/TXE/BTF/RXNE） */
#define I2C_IDLE_TIMEOUT_US      50000U   /**< 等待总线 BUSY 清零 */
#define I2C_UNSTUCK_DELAY_US     5U       /**< 9 时钟恢复时的半周期 */

extern uint32_t SystemCoreClock;

/* ================================================================================== */
/* 时间与 DWT                                                         */
/* ================================================================================== */

static inline uint32_t ll_cycles(void)
{
    return DWT->CYCCNT;
}

/** @brief 距 start 是否已超过 us 微秒（无符号差值自动处理回绕） */
static inline int ll_timeout_expired(uint32_t start, uint32_t us)
{
    return ((ll_cycles() - start) > (us * (SystemCoreClock / 1000000U))) ? 1 : 0;
}

/** @brief 确保 DWT 周期计数器已启动（Timebase_Init 之前调用也不会死等） */
static void ll_dwt_enable(void)
{
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

/* ================================================================================== */
/* 时钟与 GPIO 辅助                                                   */
/* ================================================================================== */

static void gpio_clock_enable(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOA);
    } else if (port == GPIOB) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOB);
    } else if (port == GPIOC) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOC);
    }
}

static void i2c_clock_enable(I2C_TypeDef *periph)
{
    if (periph == I2C1) {
        LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);
    } else if (periph == I2C2) {
        LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C2);
    }
}

/** @brief 开漏输出（软件 I2C / 硬件解死锁阶段） */
static void gpio_init_open_drain(const i2c_stm32_ll_hw_t *hw)
{
    LL_GPIO_InitTypeDef gpio = {0};

    gpio.Mode       = LL_GPIO_MODE_OUTPUT;
    gpio.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
    gpio.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    gpio.Pull       = LL_GPIO_PULL_UP;

    gpio.Pin = hw->scl_pin;
    LL_GPIO_Init(hw->scl_port, &gpio);
    gpio.Pin = hw->sda_pin;
    LL_GPIO_Init(hw->sda_port, &gpio);

    LL_GPIO_SetOutputPin(hw->scl_port, hw->scl_pin);
    LL_GPIO_SetOutputPin(hw->sda_port, hw->sda_pin);
}

/** @brief 复用开漏（硬件 I2C） */
static void gpio_init_alternate_od(const i2c_stm32_ll_hw_t *hw)
{
    LL_GPIO_InitTypeDef gpio = {0};

    gpio.Mode       = LL_GPIO_MODE_ALTERNATE;
    gpio.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
    gpio.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    gpio.Pull       = LL_GPIO_PULL_UP;

    gpio.Pin = hw->scl_pin;
    LL_GPIO_Init(hw->scl_port, &gpio);
    gpio.Pin = hw->sda_pin;
    LL_GPIO_Init(hw->sda_port, &gpio);
}

/**
 * @brief 9 个时钟 + STOP，解救被从机拉死的总线
 * @note  调用前引脚必须已配置为开漏输出
 */
static void gpio_bus_unstick(const i2c_stm32_ll_hw_t *hw)
{
    uint32_t i;

    /* 释放 SDA，让从机有机会把数据线放开 */
    LL_GPIO_SetOutputPin(hw->sda_port, hw->sda_pin);
    Delay_us(hw->sw_delay_us > 0U ? hw->sw_delay_us : I2C_UNSTUCK_DELAY_US);

    for (i = 0U; i < 9U; i++) {
        LL_GPIO_ResetOutputPin(hw->scl_port, hw->scl_pin);
        Delay_us(hw->sw_delay_us > 0U ? hw->sw_delay_us : I2C_UNSTUCK_DELAY_US);
        LL_GPIO_SetOutputPin(hw->scl_port, hw->scl_pin);
        Delay_us(hw->sw_delay_us > 0U ? hw->sw_delay_us : I2C_UNSTUCK_DELAY_US);
    }

    /* 手工 STOP：SCL 高电平期间 SDA 由低变高 */
    LL_GPIO_ResetOutputPin(hw->sda_port, hw->sda_pin);
    Delay_us(hw->sw_delay_us > 0U ? hw->sw_delay_us : I2C_UNSTUCK_DELAY_US);
    LL_GPIO_SetOutputPin(hw->scl_port, hw->scl_pin);
    Delay_us(hw->sw_delay_us > 0U ? hw->sw_delay_us : I2C_UNSTUCK_DELAY_US);
    LL_GPIO_SetOutputPin(hw->sda_port, hw->sda_pin);
    Delay_us(hw->sw_delay_us > 0U ? hw->sw_delay_us : I2C_UNSTUCK_DELAY_US);
}

/* ================================================================================== */
/* 软件模拟 I2C                                                       */
/* ================================================================================== */

static void sw_delay(const i2c_stm32_ll_t *ll)
{
    Delay_us(ll->sw_half_us);
}

static void sw_scl(i2c_stm32_ll_t *ll, uint8_t level)
{
    if (level) {
        uint32_t start;

        LL_GPIO_SetOutputPin(ll->hw->scl_port, ll->hw->scl_pin);
        /*
         * 时钟延展：开漏输出拉高只是"释放"，从机仍可继续拉低 SCL。
         * 必须等到引脚真正变高再进入下一个半周期，否则高速从机会被提前打断。
         * 超时后放行，避免从机异常时死锁主循环。
         */
        start = ll_cycles();
        while (!LL_GPIO_IsInputPinSet(ll->hw->scl_port, ll->hw->scl_pin)) {
            if (ll_timeout_expired(start, I2C_FLAG_TIMEOUT_US)) {
                break;
            }
        }
    } else {
        LL_GPIO_ResetOutputPin(ll->hw->scl_port, ll->hw->scl_pin);
    }
    sw_delay(ll);
}

static void sw_sda(i2c_stm32_ll_t *ll, uint8_t level)
{
    if (level) {
        LL_GPIO_SetOutputPin(ll->hw->sda_port, ll->hw->sda_pin);
    } else {
        LL_GPIO_ResetOutputPin(ll->hw->sda_port, ll->hw->sda_pin);
    }
    sw_delay(ll);
}

static uint8_t sw_read_sda(const i2c_stm32_ll_t *ll)
{
    return LL_GPIO_IsInputPinSet(ll->hw->sda_port, ll->hw->sda_pin) ? 1U : 0U;
}

static void sw_start(i2c_stm32_ll_t *ll)
{
    sw_sda(ll, 1);
    sw_scl(ll, 1);
    sw_sda(ll, 0);
    sw_scl(ll, 0);
}

static void sw_stop(i2c_stm32_ll_t *ll)
{
    sw_sda(ll, 0);
    sw_scl(ll, 1);
    sw_sda(ll, 1);
}

static void sw_send_byte(i2c_stm32_ll_t *ll, uint8_t byte)
{
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        sw_sda(ll, (byte & (0x80U >> i)) ? 1U : 0U);
        sw_scl(ll, 1);
        sw_scl(ll, 0);
    }
}

static uint8_t sw_receive_byte(i2c_stm32_ll_t *ll)
{
    uint8_t byte = 0U;
    uint8_t i;

    sw_sda(ll, 1);              /* 释放 SDA，交由从机驱动 */
    for (i = 0U; i < 8U; i++) {
        sw_scl(ll, 1);
        if (sw_read_sda(ll)) {
            byte |= (uint8_t)(0x80U >> i);
        }
        sw_scl(ll, 0);
    }
    return byte;
}

/** @return 0 从机应答；1 从机 NACK */
static uint8_t sw_receive_ack(i2c_stm32_ll_t *ll)
{
    uint8_t nack;

    sw_sda(ll, 1);
    sw_scl(ll, 1);
    nack = sw_read_sda(ll);
    sw_scl(ll, 0);
    return nack;
}

static void sw_send_ack(i2c_stm32_ll_t *ll, uint8_t nack)
{
    sw_sda(ll, nack ? 1U : 0U);
    sw_scl(ll, 1);
    sw_scl(ll, 0);
}

/** @brief 发送从机地址字节并检查应答 */
static int sw_send_addr(i2c_stm32_ll_t *ll, uint8_t addr_byte)
{
    sw_send_byte(ll, addr_byte);
    return sw_receive_ack(ll) ? I2C_BUS_ERROR_NACK : I2C_BUS_OK;
}

/** @brief 发送 1/2 字节字地址 */
static int sw_send_mem_addr(i2c_stm32_ll_t *ll, const i2c_bus_msg_t *msg)
{
    if (msg->mem_addr_size == 2U) {
        sw_send_byte(ll, (uint8_t)(msg->mem_addr >> 8));
        if (sw_receive_ack(ll)) {
            return I2C_BUS_ERROR_NACK;
        }
    }
    sw_send_byte(ll, (uint8_t)(msg->mem_addr & 0xFFU));
    if (sw_receive_ack(ll)) {
        return I2C_BUS_ERROR_NACK;
    }
    return I2C_BUS_OK;
}

static int ll_transfer_sw(i2c_stm32_ll_t *ll, const i2c_bus_msg_t *msg)
{
    uint8_t addr_w = (uint8_t)(msg->dev_addr << 1);
    uint16_t i;
    int ret;

    sw_start(ll);
    ret = sw_send_addr(ll, addr_w);
    if (ret != I2C_BUS_OK) {
        sw_stop(ll);
        return ret;
    }

    if (msg->mode == I2C_BUS_XFER_MEM) {
        ret = sw_send_mem_addr(ll, msg);
        if (ret != I2C_BUS_OK) {
            sw_stop(ll);
            return ret;
        }
    }

    if (msg->rx != NULL) {
        /* 读事务：重复 START + 读地址 */
        sw_start(ll);
        ret = sw_send_addr(ll, (uint8_t)(addr_w | 1U));
        if (ret != I2C_BUS_OK) {
            sw_stop(ll);
            return ret;
        }
        for (i = 0U; i < msg->len; i++) {
            msg->rx[i] = sw_receive_byte(ll);
            sw_send_ack(ll, (uint8_t)((i == (uint16_t)(msg->len - 1U)) ? 1U : 0U));
        }
    } else {
        for (i = 0U; i < msg->len; i++) {
            sw_send_byte(ll, msg->tx[i]);
            if (sw_receive_ack(ll)) {
                sw_stop(ll);
                return I2C_BUS_ERROR_NACK;
            }
        }
    }

    sw_stop(ll);
    return I2C_BUS_OK;
}

static int ll_probe_sw(i2c_stm32_ll_t *ll, uint16_t dev_addr)
{
    uint8_t nack;

    sw_start(ll);
    sw_send_byte(ll, (uint8_t)(dev_addr << 1));
    nack = sw_receive_ack(ll);
    sw_stop(ll);
    return nack ? I2C_BUS_ERROR_NACK : I2C_BUS_OK;
}

static void sw_recover(i2c_stm32_ll_t *ll)
{
    gpio_bus_unstick(ll->hw);
}

/* ================================================================================== */
/* 硬件 I2C                                                           */
/* ================================================================================== */

/** @brief 错误路径统一释放总线：清 NACK 标志并产生 STOP */
static void hw_release(I2C_TypeDef *i2c)
{
    LL_I2C_ClearFlag_AF(i2c);
    LL_I2C_GenerateStopCondition(i2c);
}

static int hw_wait_idle(I2C_TypeDef *i2c)
{
    uint32_t start = ll_cycles();

    while (LL_I2C_IsActiveFlag_BUSY(i2c)) {
        if (ll_timeout_expired(start, I2C_IDLE_TIMEOUT_US)) {
            return I2C_BUS_ERROR_BUSY;
        }
    }
    return I2C_BUS_OK;
}

static int hw_wait_sb(I2C_TypeDef *i2c)
{
    uint32_t start = ll_cycles();

    while (!LL_I2C_IsActiveFlag_SB(i2c)) {
        if (ll_timeout_expired(start, I2C_FLAG_TIMEOUT_US)) {
            return I2C_BUS_ERROR_TIMEOUT;
        }
    }
    return I2C_BUS_OK;
}

/** @brief 等待 ADDR；若从机 NACK（AF 置位）返回 NACK */
static int hw_wait_addr(I2C_TypeDef *i2c)
{
    uint32_t start = ll_cycles();

    while (!LL_I2C_IsActiveFlag_ADDR(i2c)) {
        if (LL_I2C_IsActiveFlag_AF(i2c)) {
            return I2C_BUS_ERROR_NACK;
        }
        if (ll_timeout_expired(start, I2C_FLAG_TIMEOUT_US)) {
            return I2C_BUS_ERROR_TIMEOUT;
        }
    }
    return I2C_BUS_OK;
}

static int hw_wait_txe(I2C_TypeDef *i2c)
{
    uint32_t start = ll_cycles();

    while (!LL_I2C_IsActiveFlag_TXE(i2c)) {
        if (LL_I2C_IsActiveFlag_AF(i2c)) {
            return I2C_BUS_ERROR_NACK;
        }
        if (ll_timeout_expired(start, I2C_FLAG_TIMEOUT_US)) {
            return I2C_BUS_ERROR_TIMEOUT;
        }
    }
    return I2C_BUS_OK;
}

static int hw_wait_btf(I2C_TypeDef *i2c)
{
    uint32_t start = ll_cycles();

    while (!LL_I2C_IsActiveFlag_BTF(i2c)) {
        if (LL_I2C_IsActiveFlag_AF(i2c)) {
            return I2C_BUS_ERROR_NACK;
        }
        if (ll_timeout_expired(start, I2C_FLAG_TIMEOUT_US)) {
            return I2C_BUS_ERROR_TIMEOUT;
        }
    }
    return I2C_BUS_OK;
}

static int hw_wait_rxne(I2C_TypeDef *i2c)
{
    uint32_t start = ll_cycles();

    while (!LL_I2C_IsActiveFlag_RXNE(i2c)) {
        if (ll_timeout_expired(start, I2C_FLAG_TIMEOUT_US)) {
            return I2C_BUS_ERROR_TIMEOUT;
        }
    }
    return I2C_BUS_OK;
}

/**
 * @brief 发送 START + 地址字节
 * @param leave_addr_set 1: 不清 ADDR（读接收需保留到读函数启动时钟）
 */
static int hw_start_addr(i2c_stm32_ll_t *ll, uint8_t addr_byte, uint8_t leave_addr_set)
{
    I2C_TypeDef *i2c = ll->hw->periph;
    int ret;

    ret = hw_wait_idle(i2c);
    if (ret != I2C_BUS_OK) {
        return ret;
    }

    LL_I2C_GenerateStartCondition(i2c);
    ret = hw_wait_sb(i2c);
    if (ret != I2C_BUS_OK) {
        hw_release(i2c);
        return ret;
    }

    LL_I2C_TransmitData8(i2c, addr_byte);
    ret = hw_wait_addr(i2c);
    if (ret != I2C_BUS_OK) {
        hw_release(i2c);
        return ret;
    }

    if (!leave_addr_set) {
        LL_I2C_ClearFlag_ADDR(i2c);
    }
    return I2C_BUS_OK;
}

/** @brief 发送重复 START + 读地址，ADDR 保持置位交给读函数处理 */
static int hw_repeated_start_read(i2c_stm32_ll_t *ll, uint8_t addr_r)
{
    I2C_TypeDef *i2c = ll->hw->periph;
    int ret;

    LL_I2C_GenerateStartCondition(i2c);
    ret = hw_wait_sb(i2c);
    if (ret != I2C_BUS_OK) {
        hw_release(i2c);
        return ret;
    }

    LL_I2C_TransmitData8(i2c, addr_r);
    ret = hw_wait_addr(i2c);
    if (ret != I2C_BUS_OK) {
        hw_release(i2c);
        return ret;
    }
    return I2C_BUS_OK;
}

/** @brief 发送 MEM 模式字地址（1 或 2 字节） */
static int hw_send_mem_addr(i2c_stm32_ll_t *ll, const i2c_bus_msg_t *msg)
{
    I2C_TypeDef *i2c = ll->hw->periph;
    int ret;

    if (msg->mem_addr_size == 2U) {
        LL_I2C_TransmitData8(i2c, (uint8_t)(msg->mem_addr >> 8));
        ret = hw_wait_txe(i2c);
        if (ret != I2C_BUS_OK) {
            hw_release(i2c);
            return ret;
        }
    }

    LL_I2C_TransmitData8(i2c, (uint8_t)(msg->mem_addr & 0xFFU));
    ret = hw_wait_btf(i2c);
    if (ret != I2C_BUS_OK) {
        hw_release(i2c);
        return ret;
    }
    return I2C_BUS_OK;
}

static int ll_transfer_hw(i2c_stm32_ll_t *ll, const i2c_bus_msg_t *msg)
{
    I2C_TypeDef *i2c = ll->hw->periph;
    uint8_t addr_w = (uint8_t)(msg->dev_addr << 1);
    uint16_t i;
    int ret;

    if (msg->rx == NULL) {
        /* 写事务：地址（写）-> 字地址 -> 数据 -> STOP */
        ret = hw_start_addr(ll, addr_w, 0U);
        if (ret != I2C_BUS_OK) {
            return ret;
        }
        if (msg->mode == I2C_BUS_XFER_MEM) {
            ret = hw_send_mem_addr(ll, msg);
            if (ret != I2C_BUS_OK) {
                return ret;
            }
        }
        for (i = 0U; i < msg->len; i++) {
            LL_I2C_TransmitData8(i2c, msg->tx[i]);
            ret = hw_wait_txe(i2c);
            if (ret != I2C_BUS_OK) {
                hw_release(i2c);
                return ret;
            }
        }
        ret = hw_wait_btf(i2c);
        if (ret != I2C_BUS_OK) {
            hw_release(i2c);
            return ret;
        }
        LL_I2C_GenerateStopCondition(i2c);
        return I2C_BUS_OK;
    }

    /* 读事务：MEM 模式先以写地址发送字地址，再重复 START 切到读 */
    if (msg->mode == I2C_BUS_XFER_MEM) {
        ret = hw_start_addr(ll, addr_w, 0U);
        if (ret != I2C_BUS_OK) {
            return ret;
        }
        ret = hw_send_mem_addr(ll, msg);
        if (ret != I2C_BUS_OK) {
            return ret;
        }
        ret = hw_repeated_start_read(ll, (uint8_t)(addr_w | 1U));
        if (ret != I2C_BUS_OK) {
            return ret;
        }
    } else {
        ret = hw_start_addr(ll, (uint8_t)(addr_w | 1U), 1U);
        if (ret != I2C_BUS_OK) {
            return ret;
        }
    }

    if (msg->len == 1U) {
        /* 单字节：先 NACK 再清 ADDR + STOP，最后读数据 */
        LL_I2C_AcknowledgeNextData(i2c, LL_I2C_NACK);
        LL_I2C_ClearFlag_ADDR(i2c);
        LL_I2C_GenerateStopCondition(i2c);

        ret = hw_wait_rxne(i2c);
        if (ret != I2C_BUS_OK) {
            hw_release(i2c);
            return ret;
        }
        msg->rx[0] = LL_I2C_ReceiveData8(i2c);
    } else {
        LL_I2C_AcknowledgeNextData(i2c, LL_I2C_ACK);
        LL_I2C_ClearFlag_ADDR(i2c);

        for (i = 0U; i < msg->len; i++) {
            if (i == (uint16_t)(msg->len - 1U)) {
                LL_I2C_AcknowledgeNextData(i2c, LL_I2C_NACK);
                LL_I2C_GenerateStopCondition(i2c);
            }
            ret = hw_wait_rxne(i2c);
            if (ret != I2C_BUS_OK) {
                hw_release(i2c);
                return ret;
            }
            msg->rx[i] = LL_I2C_ReceiveData8(i2c);
        }
    }

    /* 恢复默认 ACK，方便下次通信 */
    LL_I2C_AcknowledgeNextData(i2c, LL_I2C_ACK);
    return I2C_BUS_OK;
}

static int ll_probe_hw(i2c_stm32_ll_t *ll, uint16_t dev_addr)
{
    I2C_TypeDef *i2c = ll->hw->periph;
    int ret;

    ret = hw_wait_idle(i2c);
    if (ret != I2C_BUS_OK) {
        return ret;
    }

    LL_I2C_GenerateStartCondition(i2c);
    ret = hw_wait_sb(i2c);
    if (ret != I2C_BUS_OK) {
        hw_release(i2c);
        return ret;
    }

    LL_I2C_TransmitData8(i2c, (uint8_t)(dev_addr << 1));
    ret = hw_wait_addr(i2c);
    if (ret != I2C_BUS_OK) {
        hw_release(i2c);
        return ret;
    }

    LL_I2C_ClearFlag_ADDR(i2c);
    LL_I2C_GenerateStopCondition(i2c);
    return I2C_BUS_OK;
}

/* ================================================================================== */
/* 配置与恢复                                                         */
/* ================================================================================== */

static int ll_configure(i2c_bus_t *bus, const i2c_bus_config_t *cfg)
{
    i2c_stm32_ll_t *ll = (i2c_stm32_ll_t *)bus->ctx;

    if (ll == NULL || ll->hw == NULL || cfg == NULL) {
        return I2C_BUS_ERROR_PARAM;
    }
    /* STM32F1 标准模式 100k / 快速模式 400k */
    if (cfg->clock_hz == 0U || cfg->clock_hz > 400000U) {
        return I2C_BUS_ERROR_PARAM;
    }
    /* 当前实现只支持 7 位地址 */
    if (cfg->addr_mode != I2C_BUS_ADDR_7BIT) {
        return I2C_BUS_ERROR_PARAM;
    }

    if (ll->hw->backend == I2C_STM32_BACKEND_HW) {
        I2C_TypeDef *i2c = ll->hw->periph;

        if (i2c == NULL) {
            return I2C_BUS_ERROR_PARAM;
        }
        if (LL_I2C_IsActiveFlag_BUSY(i2c)) {
            /* 刚发完 STOP 时 BUSY 可能还要几个周期才清，短等一会儿再判失败 */
            uint32_t start = ll_cycles();

            while (LL_I2C_IsActiveFlag_BUSY(i2c)) {
                if (ll_timeout_expired(start, I2C_IDLE_TIMEOUT_US)) {
                    return I2C_BUS_ERROR_BUSY;
                }
            }
        }
        if (!ll->configured || ll->current.clock_hz != cfg->clock_hz) {
            LL_I2C_InitTypeDef init = {0};

            LL_I2C_Disable(i2c);
            LL_I2C_StructInit(&init);
            init.PeripheralMode  = LL_I2C_MODE_I2C;
            init.ClockSpeed      = cfg->clock_hz;
            init.DutyCycle       = LL_I2C_DUTYCYCLE_2;
            init.TypeAcknowledge = LL_I2C_ACK;
            init.OwnAddrSize     = LL_I2C_OWNADDRESS1_7BIT;
            LL_I2C_Init(i2c, &init);
            LL_I2C_Enable(i2c);
        }
    } else {
        /* 软件后端：半周期 = 500000 / SCL 频率（us） */
        uint32_t half = 500000U / cfg->clock_hz;

        if (half == 0U) {
            half = 1U;
        }
        ll->sw_half_us = (uint16_t)half;
    }

    ll->current = *cfg;
    ll->configured = 1U;
    return I2C_BUS_OK;
}

static int ll_transfer(i2c_bus_t *bus, const i2c_bus_msg_t *msg)
{
    i2c_stm32_ll_t *ll = (i2c_stm32_ll_t *)bus->ctx;
    int ret;

    if (ll == NULL || ll->hw == NULL || msg == NULL || !ll->configured) {
        return I2C_BUS_ERROR_PARAM;
    }
    if (msg->dev_addr > 0x7FU || msg->len == 0U) {
        return I2C_BUS_ERROR_PARAM;
    }
    if (msg->mode != I2C_BUS_XFER_STREAM && msg->mode != I2C_BUS_XFER_MEM) {
        return I2C_BUS_ERROR_PARAM;
    }
    if (msg->mode == I2C_BUS_XFER_MEM &&
        msg->mem_addr_size != 1U && msg->mem_addr_size != 2U) {
        return I2C_BUS_ERROR_PARAM;
    }
    /* I2C 同一时刻只能一个方向，且必须有一端有数据 */
    if ((msg->tx == NULL) == (msg->rx == NULL)) {
        return I2C_BUS_ERROR_PARAM;
    }

    if (ll->hw->backend == I2C_STM32_BACKEND_HW) {
        ret = ll_transfer_hw(ll, msg);
    } else {
        ret = ll_transfer_sw(ll, msg);
    }

    ll->last_error = ret;
    if (ret == I2C_BUS_ERROR_TIMEOUT || ret == I2C_BUS_ERROR_BUSY) {
        /* 超时 / 总线卡死后来一次恢复，保证下一次事务仍可执行 */
        (void)i2c_stm32_ll_recover(ll);
    }
    return ret;
}

static int ll_probe(i2c_bus_t *bus, uint16_t dev_addr)
{
    i2c_stm32_ll_t *ll = (i2c_stm32_ll_t *)bus->ctx;
    int ret;

    if (ll == NULL || ll->hw == NULL || !ll->configured || dev_addr > 0x7FU) {
        return I2C_BUS_ERROR_PARAM;
    }

    if (ll->hw->backend == I2C_STM32_BACKEND_HW) {
        ret = ll_probe_hw(ll, dev_addr);
    } else {
        ret = ll_probe_sw(ll, dev_addr);
    }

    ll->last_error = ret;
    return ret;
}

static int ll_in_isr(i2c_bus_t *bus)
{
    (void)bus;
    return (__get_IPSR() != 0U) ? 1 : 0;
}

static const i2c_bus_ops_t s_i2c_ll_ops = {
    .configure = ll_configure,
    .transfer  = ll_transfer,
    /* 本后端只有轮询实现，暂不提供异步（DMA/中断）路径；
       接入 DMA 时在此填充 transfer_async，并在 TC 中断调用 i2c_bus_async_finish() */
    .transfer_async = NULL,
    .probe     = ll_probe,
    .in_isr    = ll_in_isr,
};

/* ================================================================================== */
/* 对外接口                                                           */
/* ================================================================================== */

void i2c_stm32_ll_init(i2c_stm32_ll_t *ll, const i2c_stm32_ll_hw_t *hw)
{
    if (ll == NULL || hw == NULL) {
        return;
    }

    memset(ll, 0, sizeof(*ll));
    ll->hw = hw;
    ll->bus.ops = &s_i2c_ll_ops;
    ll->bus.ctx = ll;
    ll->sw_half_us = (hw->sw_delay_us > 0U) ? hw->sw_delay_us : I2C_UNSTUCK_DELAY_US;

    ll_dwt_enable();

    gpio_clock_enable(hw->scl_port);
    gpio_clock_enable(hw->sda_port);

    if (hw->backend == I2C_STM32_BACKEND_HW) {
        if (hw->periph == NULL) {
            return;
        }

        i2c_clock_enable(hw->periph);

        /* 先把引脚当普通开漏输出，执行 9 时钟 + STOP 解救可能死锁的总线 */
        gpio_init_open_drain(hw);
        gpio_bus_unstick(hw);

        /* 再切回复用开漏，初始化硬件外设 */
        gpio_init_alternate_od(hw);

        {
            LL_I2C_InitTypeDef init = {0};

            LL_I2C_Disable(hw->periph);
            LL_I2C_StructInit(&init);
            init.PeripheralMode  = LL_I2C_MODE_I2C;
            init.ClockSpeed      = 100000U;     /* 默认 100k，首次 acquire 时按配置切换 */
            init.DutyCycle       = LL_I2C_DUTYCYCLE_2;
            init.TypeAcknowledge = LL_I2C_ACK;
            init.OwnAddrSize     = LL_I2C_OWNADDRESS1_7BIT;
            LL_I2C_Init(hw->periph, &init);
            LL_I2C_Enable(hw->periph);
        }
    } else {
        gpio_init_open_drain(hw);
    }

    ll->configured = 0U;
    ll->current.clock_hz = 0U;
    ll->current.addr_mode = I2C_BUS_ADDR_7BIT;
    ll->recover_count = 0U;
    ll->last_error = I2C_BUS_OK;
}

i2c_bus_t *i2c_stm32_ll_bus(i2c_stm32_ll_t *ll)
{
    return (ll == NULL) ? NULL : &ll->bus;
}

int i2c_stm32_ll_recover(i2c_stm32_ll_t *ll)
{
    if (ll == NULL || ll->hw == NULL) {
        return I2C_BUS_ERROR_PARAM;
    }

    ll->recover_count++;

    if (ll->hw->backend == I2C_STM32_BACKEND_HW) {
        I2C_TypeDef *i2c = ll->hw->periph;

        if (i2c == NULL) {
            ll->last_error = I2C_BUS_ERROR_PARAM;
            return I2C_BUS_ERROR_PARAM;
        }

        /* 关闭外设并释放引脚，用 9 时钟把从机状态机拉回空闲 */
        LL_I2C_Disable(i2c);
        gpio_init_open_drain(ll->hw);
        gpio_bus_unstick(ll->hw);
        gpio_init_alternate_od(ll->hw);

        {
            LL_I2C_InitTypeDef init = {0};

            LL_I2C_StructInit(&init);
            init.PeripheralMode  = LL_I2C_MODE_I2C;
            init.ClockSpeed      = (ll->current.clock_hz > 0U) ? ll->current.clock_hz : 100000U;
            init.DutyCycle       = LL_I2C_DUTYCYCLE_2;
            init.TypeAcknowledge = LL_I2C_ACK;
            init.OwnAddrSize     = LL_I2C_OWNADDRESS1_7BIT;
            LL_I2C_Init(i2c, &init);
            LL_I2C_Enable(i2c);
        }
        (void)hw_wait_idle(i2c);
    } else {
        sw_recover(ll);
    }

    /* 按恢复前的配置重新配置一次，确保寄存器处于已知状态 */
    if (ll->current.clock_hz > 0U) {
        i2c_bus_config_t saved = ll->current;

        ll->configured = 0U;
        if (ll_configure(&ll->bus, &saved) != I2C_BUS_OK) {
            ll->last_error = I2C_BUS_ERROR_RECOVERY;
            return I2C_BUS_ERROR_RECOVERY;
        }
    }

    ll->last_error = I2C_BUS_OK;
    return I2C_BUS_OK;
}

uint32_t i2c_stm32_ll_get_recover_count(const i2c_stm32_ll_t *ll)
{
    return (ll == NULL) ? 0U : ll->recover_count;
}

int32_t i2c_stm32_ll_get_last_error(const i2c_stm32_ll_t *ll)
{
    return (ll == NULL) ? 0 : ll->last_error;
}

void i2c_stm32_ll_clear_error(i2c_stm32_ll_t *ll)
{
    if (ll != NULL) {
        ll->last_error = I2C_BUS_OK;
    }
}
