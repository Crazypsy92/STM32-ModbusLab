/**
 * @file    i2c_bus.h
 * @brief   I2C 总线抽象接口（MCU 无关）
 * @details 纯接口层：不包含任何 MCU / LL / HAL 头文件。
 *          驱动与移植层只依赖本文件；具体实现（如 i2c_stm32_ll.c）负责产生
 *          真实 I2C 时序。换 MCU 时替换实现文件即可。
 *
 *          总线按"物理外设"建模：同一 I2C 外设上的多个设备共享一个 i2c_bus_t，
 *          每次事务前通过 i2c_bus_acquire() 占用总线（切速率），事务结束后
 *          i2c_bus_release() 释放，避免两个设备互相打断。
 *
 *          与 SPI 总线不同的是，I2C 的「设备地址 / 字地址 / 读或写」属于
 *          单次事务参数，统一放在 i2c_bus_msg_t 中按消息传递，不再使用
 *          共享可变字段，避免 OLED 与 EEPROM 互相污染配置。
 *
 *          并发模型：
 *          1. 独占式：一次 acquire..release 之间，总线只属于一个设备；
 *             期间 transfer() 是阻塞的，同一时刻只有一条 I2C 事务在跑。
 *          2. 中断安全：ISR 内调用 acquire() 会被直接拒绝。
 *          3. 同 owner 允许嵌套 acquire，但配置必须与首次一致。
 *          4. 异步事务：transfer_async() 成功启动后，总线所有权保留到事务真正
 *             结束；实现方在完成（DMA TC / I2C 中断）时调用
 *             i2c_bus_async_finish() 自动归还总线并触发完成回调。等待期间
 *             任何 acquire / transfer / release 请求都会被拒绝，避免另一条
 *             事务插到还在飞行中的 DMA 中间。
 */
#ifndef __I2C_BUS_H__
#define __I2C_BUS_H__

#include <stdint.h>
#include <stddef.h>

/* ==================== 统一状态码 ==================== */

/**
 * @brief 总线操作结果（负值为错误，便于与返回 0 成功的协议层统一）
 */
typedef enum {
    I2C_BUS_OK              = 0,    /**< 成功 */
    I2C_BUS_ERROR_PARAM     = -1,   /**< 参数非法 */
    I2C_BUS_ERROR_TIMEOUT   = -2,   /**< 等待标志超时 */
    I2C_BUS_ERROR_NACK      = -3,   /**< 从机无应答 */
    I2C_BUS_ERROR_BUSY      = -4,   /**< 总线忙 / 被其它设备占用 */
    I2C_BUS_ERROR_RECOVERY  = -5,   /**< 硬件恢复失败 */
    I2C_BUS_ERROR_UNSUPPORTED = -6  /**< 实现方未提供该能力（如异步路径） */
} i2c_bus_status_t;

/* ==================== 设备级配置 ==================== */

/**
 * @brief 从机地址位宽
 * @note  当前 STM32 LL 后端只实现 7 位地址，10 位会返回 I2C_BUS_ERROR_PARAM
 */
typedef enum {
    I2C_BUS_ADDR_7BIT = 0,          /**< 7 位从机地址（最常用） */
    I2C_BUS_ADDR_10BIT              /**< 10 位从机地址（预留） */
} i2c_bus_addr_mode_t;

/**
 * @brief 设备级总线参数（占用总线时切换）
 */
typedef struct {
    uint32_t            clock_hz;   /**< 目标 SCL 频率（Hz），实现方可向下取整 */
    i2c_bus_addr_mode_t addr_mode;  /**< 从机地址位宽 */
} i2c_bus_config_t;

/* ==================== 单次事务消息 ==================== */

/**
 * @brief 事务模式
 */
typedef enum {
    I2C_BUS_XFER_STREAM = 0,        /**< 流模式：直接收发，无字地址（如 OLED） */
    I2C_BUS_XFER_MEM                /**< 内存模式：先发 1/2 字节字地址（如 EEPROM） */
} i2c_bus_xfer_mode_t;

/**
 * @brief 一次 I2C 事务描述
 * @note  tx 与 rx 不能同时非 NULL（I2C 同一时刻只有一个方向）。
 *        MEM 模式下 mem_addr_size 必须为 1 或 2。
 */
typedef struct {
    uint16_t        dev_addr;       /**< 7 位从机地址（不含读写位） */
    uint16_t        mem_addr;       /**< 字地址（MEM 模式使用） */
    uint8_t         mem_addr_size;  /**< 字地址字节数：1 或 2（MEM 模式使用） */
    uint8_t         mode;           /**< i2c_bus_xfer_mode_t */
    const uint8_t  *tx;             /**< 发送缓冲区，可为 NULL */
    uint8_t        *rx;             /**< 接收缓冲区，可为 NULL */
    uint16_t        len;            /**< 传输字节数 */
} i2c_bus_msg_t;

/* ==================== 总线对象与操作表 ==================== */

/** @brief 总线对象（不透明） */
typedef struct i2c_bus i2c_bus_t;

/**
 * @brief 异步事务完成回调
 * @param owner  发起事务的设备上下文指针
 * @param result 事务结果码（I2C_BUS_OK 或负值错误）
 * @note  通常在中断上下文（DMA TC / I2C 中断）被调用，回调内只做置标志等轻量操作，
 *        耗时处理放回主循环。
 */
typedef void (*i2c_bus_done_cb_t)(const void *owner, int result);

/**
 * @brief 总线操作表：由具体实现填充
 */
typedef struct {
    /**
     * @brief 切换总线参数
     * @return I2C_BUS_OK 成功，负值失败
     * @note  实现方应缓存当前参数，未变化时直接返回，避免重复配置外设
     */
    int (*configure)(i2c_bus_t *bus, const i2c_bus_config_t *cfg);

    /**
     * @brief 执行一次完整 I2C 事务（含 START / 地址 / 数据 / STOP）
     * @return I2C_BUS_OK 成功，负值失败
     * @note  必须在 acquire 之后调用，由实现方保证错误路径一定发 STOP
     */
    int (*transfer)(i2c_bus_t *bus, const i2c_bus_msg_t *msg);

    /**
     * @brief 启动一次异步事务（DMA / 中断方式），可为 NULL 表示不支持
     * @return I2C_BUS_OK 已成功启动；负值表示启动失败（总线未接管）
     * @note  返回 OK 后总线所有权仍由发起者持有，实现方必须在事务真正结束时
     *        （DMA TC / I2C STOP 中断）调用 i2c_bus_async_finish() 完成归还，
     *        不得在"启动成功"时当作已完成。msg 指向总线内部的副本，异步期间有效。
     */
    int (*transfer_async)(i2c_bus_t *bus, const i2c_bus_msg_t *msg);

    /**
     * @brief 探测从机是否应答（仅发送地址，不读写数据）
     * @return I2C_BUS_OK 有应答；I2C_BUS_ERROR_NACK 无应答
     */
    int (*probe)(i2c_bus_t *bus, uint16_t dev_addr);

    /**
     * @brief 可选：返回 1 表示当前处于中断上下文（ISR）
     * @note  NULL 表示不做上下文检查
     */
    int (*in_isr)(i2c_bus_t *bus);
} i2c_bus_ops_t;

struct i2c_bus {
    const i2c_bus_ops_t *ops;          /**< 操作表 */
    void                *ctx;          /**< 实现方私有上下文 */
    const void          *owner;        /**< 当前事务占用者，NULL=空闲 */
    uint16_t             depth;        /**< 事务嵌套深度：同一 owner 可重入 */
    volatile uint32_t    rejects;      /**< 被拒绝的占用请求计数（调试/测试用） */
    i2c_bus_config_t     current_cfg;  /**< 当前事务生效的配置 */

    /* 异步事务状态（pending != 0 时总线保持被占用直到事务真正结束） */
    volatile uint8_t     pending;      /**< 1=有异步事务在飞行中 */
    const void          *pending_owner;/**< 异步事务发起者（完成回调回传） */
    i2c_bus_done_cb_t    pending_cb;   /**< 完成回调，可为 NULL */
    volatile int         async_result; /**< 最近一次异步事务结果 */
    i2c_bus_msg_t        pending_msg;  /**< 事务消息副本，覆盖整个异步生命周期 */
};

/* ==================== 内联仲裁实现 ==================== */

/**
 * @brief 切换总线参数（空指针安全）
 * @note  仅允许总线空闲时调用；同 owner 嵌套由 acquire() 处理
 */
static inline int i2c_bus_configure(i2c_bus_t *bus, const i2c_bus_config_t *cfg)
{
    if (bus == NULL || bus->ops == NULL || bus->ops->configure == NULL || cfg == NULL) {
        return I2C_BUS_ERROR_PARAM;
    }
    if (bus->owner != NULL) {
        return I2C_BUS_ERROR_BUSY;      /* 总线已被占用，禁止改配置 */
    }
    return bus->ops->configure(bus, cfg);
}

/** @brief 两个配置是否完全一致 */
static inline int i2c_bus_cfg_equal(const i2c_bus_config_t *a, const i2c_bus_config_t *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    return (a->clock_hz  == b->clock_hz) &&
           (a->addr_mode == b->addr_mode);
}

/**
 * @brief 占用总线：切到本设备参数并标记占用者
 * @param bus    总线句柄
 * @param owner  设备上下文指针（唯一标识一个设备，不能为 NULL）
 * @param cfg    本设备的速率 / 地址位宽配置
 * @return I2C_BUS_OK 成功；负值表示参数非法 / 被占用 / ISR / 嵌套配置不一致
 * @note  首次占用才 configure；同 owner 嵌套不重配，且 cfg 必须与首次一致。
 */
static inline int i2c_bus_acquire(i2c_bus_t *bus, const void *owner, const i2c_bus_config_t *cfg)
{
    if (bus == NULL || bus->ops == NULL || owner == NULL || cfg == NULL) {
        return I2C_BUS_ERROR_PARAM;
    }
    /* 异步事务在飞行中：总线必须保持被占用，任何占用请求一律拒绝 */
    if (bus->pending) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }
    /* 中断上下文不允许占用总线 */
    if (bus->ops->in_isr != NULL && bus->ops->in_isr(bus) != 0) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }
    /* 不变式：owner 与 depth 必须同时为零；违反则拒绝 */
    if ((bus->owner == NULL) != (bus->depth == 0U)) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }
    /* 已被其它设备占用，拒绝抢占 */
    if (bus->owner != NULL && bus->owner != owner) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }
    /* depth 溢出保护 */
    if (bus->depth == UINT16_MAX) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }

    if (bus->owner == NULL) {
        /* 首次占用：切到本设备参数，失败则不占用 */
        int ret = i2c_bus_configure(bus, cfg);
        if (ret != I2C_BUS_OK) {
            return ret;
        }
        bus->owner       = owner;
        bus->depth       = 1U;
        bus->current_cfg = *cfg;
    } else {
        /* 同 owner 嵌套：配置必须与首次完全一致 */
        if (!i2c_bus_cfg_equal(&bus->current_cfg, cfg)) {
            bus->rejects++;
            return I2C_BUS_ERROR_PARAM;
        }
        bus->depth++;
    }
    return I2C_BUS_OK;
}

/**
 * @brief 释放总线（与 acquire 成对调用）
 * @return 1 总线已完全交还（depth 归零）；
 *         0 仍为同 owner 嵌套占用；
 *         负值表示参数非法 / 非占用者 / depth 已为 0
 */
static inline int i2c_bus_release(i2c_bus_t *bus, const void *owner)
{
    if (bus == NULL || owner == NULL) {
        return I2C_BUS_ERROR_PARAM;
    }
    /* 异步事务未结束：由 i2c_bus_async_finish() 归还，调用方不得提前释放 */
    if (bus->pending) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }
    if ((bus->owner == NULL) != (bus->depth == 0U)) {
        return I2C_BUS_ERROR_BUSY;
    }
    if (bus->owner != owner) {
        return I2C_BUS_ERROR_BUSY;      /* 非占用者无权释放 */
    }
    if (bus->depth == 0U) {
        return I2C_BUS_ERROR_BUSY;      /* 没有可释放的嵌套层级 */
    }

    bus->depth--;
    if (bus->depth == 0U) {
        bus->owner = NULL;
        return 1;                       /* 完全释放 */
    }
    return 0;                           /* 仍有嵌套 */
}

/** @brief 查询当前占用者（无占用返回 NULL） */
static inline const void *i2c_bus_owner(const i2c_bus_t *bus)
{
    return (bus == NULL) ? NULL : bus->owner;
}

/** @brief 查询当前嵌套深度 */
static inline uint16_t i2c_bus_depth(const i2c_bus_t *bus)
{
    return (bus == NULL) ? 0U : bus->depth;
}

/**
 * @brief 执行一次 I2C 事务（空指针安全）
 * @param owner 必须是当前总线占用者，且 depth > 0
 * @note  必须先 i2c_bus_acquire() 取得总线，否则返回 I2C_BUS_ERROR_BUSY
 */
static inline int i2c_bus_transfer(i2c_bus_t *bus, const void *owner,
                                   const i2c_bus_msg_t *msg)
{
    if (bus == NULL || bus->ops == NULL || bus->ops->transfer == NULL ||
        owner == NULL || msg == NULL) {
        return I2C_BUS_ERROR_PARAM;
    }
    if (bus->pending) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }
    if (bus->owner != owner || bus->depth == 0U) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }
    return bus->ops->transfer(bus, msg);
}

/**
 * @brief 探测从机是否应答（必须持有总线）
 */
static inline int i2c_bus_probe(i2c_bus_t *bus, const void *owner, uint16_t dev_addr)
{
    if (bus == NULL || bus->ops == NULL || bus->ops->probe == NULL ||
        owner == NULL) {
        return I2C_BUS_ERROR_PARAM;
    }
    if (bus->pending) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }
    if (bus->owner != owner || bus->depth == 0U) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }
    return bus->ops->probe(bus, dev_addr);
}

/* ==================== 异步事务 ==================== */

/**
 * @brief 启动一次异步事务（DMA / 中断方式）
 * @param bus     总线句柄
 * @param owner   必须是当前总线占用者
 * @param msg     事务描述（内部会拷贝，调用方可用栈上临时变量）
 * @param done_cb 完成回调，可为 NULL；回调在事务真正结束时调用
 * @return I2C_BUS_OK 已成功启动；I2C_BUS_ERROR_UNSUPPORTED 后端不支持；
 *         I2C_BUS_ERROR_BUSY 已有异步事务在飞行 / 非独占占用 / 非占用者
 * @note  成功返回后**总线不再由调用方 release**，由实现方在完成时调用
 *        i2c_bus_async_finish() 自动归还并触发回调；等待期间其它设备
 *        的 acquire / transfer / probe / release 全部被拒绝。
 */
static inline int i2c_bus_transfer_async(i2c_bus_t *bus, const void *owner,
                                         const i2c_bus_msg_t *msg,
                                         i2c_bus_done_cb_t done_cb)
{
    int ret;

    if (bus == NULL || bus->ops == NULL || owner == NULL || msg == NULL) {
        return I2C_BUS_ERROR_PARAM;
    }
    if (bus->ops->transfer_async == NULL) {
        return I2C_BUS_ERROR_UNSUPPORTED;   /* 当前后端没有异步路径 */
    }
    if (bus->pending) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;          /* 已有异步事务在飞行 */
    }
    if (bus->owner != owner || bus->depth == 0U) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }
    /* 异步要求独占：嵌套 depth>1 时无法确定何时真正交还总线 */
    if (bus->depth != 1U) {
        bus->rejects++;
        return I2C_BUS_ERROR_BUSY;
    }
    /* 与同步事务相同的消息约束 */
    if (msg->dev_addr > 0x7FU || msg->len == 0U ||
        (msg->tx == NULL) == (msg->rx == NULL)) {
        return I2C_BUS_ERROR_PARAM;
    }
    if (msg->mode == I2C_BUS_XFER_MEM &&
        msg->mem_addr_size != 1U && msg->mem_addr_size != 2U) {
        return I2C_BUS_ERROR_PARAM;
    }

    /* 消息必须拷贝：异步期间调用方传入的栈上消息可能已失效 */
    bus->pending_msg   = *msg;
    bus->pending_owner = owner;
    bus->pending_cb    = done_cb;
    bus->async_result  = I2C_BUS_OK;
    bus->pending       = 1U;

    ret = bus->ops->transfer_async(bus, &bus->pending_msg);
    if (ret != I2C_BUS_OK) {
        /* 启动失败：立即归还占用，不触发完成回调 */
        bus->pending       = 0U;
        bus->pending_owner = NULL;
        bus->pending_cb    = NULL;
        if (ret == I2C_BUS_ERROR_BUSY) {
            bus->rejects++;
        }
    }
    return ret;
}

/**
 * @brief 由实现方在异步事务真正结束时调用（DMA TC / I2C STOP 中断）
 * @param bus    总线句柄
 * @param result 事务结果码
 * @note  自动把总线交还给发起者并触发完成回调；未处于 pending 时安全返回。
 *        允许在中断上下文调用。
 */
static inline void i2c_bus_async_finish(i2c_bus_t *bus, int result)
{
    const void *owner;
    i2c_bus_done_cb_t cb;

    if (bus == NULL || !bus->pending) {
        return;
    }

    owner = bus->pending_owner;
    cb    = bus->pending_cb;

    /* 先归还占用，再清 pending：任一中间态都会被其它请求安全拒绝 */
    if (bus->depth > 0U) {
        bus->depth--;
        if (bus->depth == 0U) {
            bus->owner = NULL;
        }
    }

    bus->async_result  = result;
    bus->pending_owner = NULL;
    bus->pending_cb    = NULL;
    bus->pending       = 0U;

    if (cb != NULL) {
        cb(owner, result);
    }
}

/** @brief 是否有异步事务在飞行中（1=有） */
static inline int i2c_bus_is_pending(const i2c_bus_t *bus)
{
    return (bus != NULL && bus->pending != 0U) ? 1 : 0;
}

/**
 * @brief 同步等待异步事务结束（给习惯阻塞语义的调用方用）
 * @param bus        总线句柄
 * @param timeout_us 超时（微秒），0 表示只检查一次
 * @param time_us    单调递增的微秒时间源（如 DWT 换算），不能为 NULL
 * @return 事务结果码；等待超时返回 I2C_BUS_ERROR_TIMEOUT
 * @note  当前没有事务在飞行时立即返回最近一次结果；完成回调仍会照常触发。
 */
static inline int i2c_bus_wait(i2c_bus_t *bus, uint32_t timeout_us,
                               uint32_t (*time_us)(void))
{
    uint32_t start;

    if (bus == NULL || time_us == NULL) {
        return I2C_BUS_ERROR_PARAM;
    }

    start = time_us();
    while (bus->pending) {
        if ((uint32_t)(time_us() - start) > timeout_us) {
            return I2C_BUS_ERROR_TIMEOUT;
        }
    }
    return bus->async_result;
}

#endif /* __I2C_BUS_H__ */
