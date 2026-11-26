/**
 * @file app_test.c
 * @brief 应用层测试模块实现（裸机版）
 * @details 上电先做 OLED 自测，再对 AT24xx 做一次「读回比对（不一致才写入）」自测；
 *          之后进入交互测试主界面：
 *          行0 运行秒数 + LED 实际状态（1s 刷新，证明心跳与显示链路正常）
 *          行1 AT24xx 从机地址 / 页大小 / IIC 后端（上电检测，K2 长按重检）
 *          行2 最近一次按键事件（写/读/清除/重检结果）
 *          行3 读回数据预览
 *          按键任务：K1 单击=写测试串；K2 单击=读回校验并显示；
 *                    K1 长按=测试区填 0xFF；K2 长按=重新检测 EEPROM。
 *          （LED 动作保留在 key_port.c：K1 单击点亮，K2 单击熄灭）
 * @author 西伯利亚小菜狗
 * @version 3.0 (at24xx)
 */

#include "app_test.h"
#include "bsp_includes.h"

#if BSP_I2C_BACKEND_SOFT
#define BACKEND_STR  "SOFT"
#else
#define BACKEND_STR  "HARD"
#endif

/* AT24xx 测试参数：起始地址与测试数据（40 字节，跨 32 字节页，验证分页写） */
#define EE_TEST_ADDR        0x0000U
#define EE_TEST_LEN         40U
static const uint8_t s_ee_pattern[EE_TEST_LEN] = {
    'A', 'T', '2', '4', 'C', 'X', 'X', '-', 'T', 'E', 'S', 'T', '!',
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    0x5A, 0xA5, 0x0F, 0xF0, 0x3C, 0xC3, 0x96, 0x69,
    0x01, 0x80, 0x7E,
};

static uint8_t s_ee_read_buf[EE_TEST_LEN];  /* 最近一次读回内容 */

static uint32_t s_last_key_tick = 0;    /* 上次按键扫描时刻(ms) */
static uint32_t s_last_run_tick = 0;    /* 上次刷新运行指示时刻(ms) */
static uint8_t  s_last_c1 = 0, s_last_c2 = 0, s_last_l1 = 0, s_last_l2 = 0;

/* 内部函数声明 */
static void App_ShowLine(uint8_t y, const char *text);
static void App_OLED_SelfTest(void);
static void App_DashboardInit(void);
static void App_RefreshRunLine(void);
static void App_RefreshEELine(void);
static void App_ShowReadPreview(const uint8_t *buf, uint16_t len);
static void App_EE_SelfTest(void);
static void App_EE_WriteTest(void);
static void App_EE_ReadTest(void);
static void App_EE_ClearTest(void);
static void App_CheckKeyEvents(void);

/**
 * @brief 刷新一整行（8x16 字体，一行占 2 个 page，只局部更新）
 */
static void App_ShowLine(uint8_t y, const char *text)
{
    memset(g_oled.DisplayBuf[y / 8],     0, sizeof(g_oled.DisplayBuf[0]));
    memset(g_oled.DisplayBuf[y / 8 + 1], 0, sizeof(g_oled.DisplayBuf[0]));
    OLED_ShowString(&g_oled, 0, y, (char *)text, OLED_8X16);
    OLED_UpdateArea(&g_oled, 0, y, 128, 16);
}

/**
 * @brief OLED 上电自测：逐页真实调用驱动功能，每页停留 1s 供肉眼确认
 * @note  第1页 文本/无符号数/有符号数/十六进制/格式化输出
 *        第2页 汉字（纯中文 / 中英混排）
 *        第3页 直线/矩形/圆/三角
 *        第4页 标题栏反显（反白即正常）
 */
static void App_OLED_SelfTest(void)
{
    /* 第 1 页：文本与数字 */
    OLED_ShowString(&g_oled, 0,  0, "TEST1:TEXT NUM", OLED_8X16);
    OLED_ShowNum(&g_oled,       0, 16, 12345, 5, OLED_8X16);
    OLED_ShowSignedNum(&g_oled, 64, 16, -123, 4, OLED_8X16);
    OLED_ShowHexNum(&g_oled,    0, 32, 0xABCD, 4, OLED_8X16);
    OLED_Printf(&g_oled,        0, 48, OLED_8X16, "IIC:%s", BACKEND_STR);
    OLED_Update(&g_oled);
    Delay_ms(1000);

    /* 第 2 页：汉字显示（纯中文 + 中英混排） */
    OLED_Clear(&g_oled);
    OLED_ShowChinese(&g_oled, 0,  0, "汉字显示测试");
    OLED_ShowChinese(&g_oled, 0, 16, "中文显示正常");
    OLED_ShowChinese(&g_oled, 0, 32, "STM32单片机");
    OLED_ShowChinese(&g_oled, 0, 48, "西伯利亚小菜狗");
    OLED_Update(&g_oled);
    Delay_ms(1500);

    /* 第 3 页：图形绘制 */
    OLED_Clear(&g_oled);
    OLED_ShowString(&g_oled, 0, 0, "TEST3:GRAPHICS", OLED_8X16);
    OLED_DrawLine(&g_oled, 0, 20, 127, 20);
    OLED_DrawRectangle(&g_oled, 4, 28, 40, 24, OLED_UNFILLED);
    OLED_DrawCircle(&g_oled, 76, 40, 12, OLED_UNFILLED);
    OLED_DrawTriangle(&g_oled, 96, 52, 110, 28, 124, 52, OLED_UNFILLED);
    OLED_Update(&g_oled);
    Delay_ms(1000);

    /* 第 4 页：标题栏反显 */
    OLED_ReverseArea(&g_oled, 0, 0, 128, 16);
    OLED_Update(&g_oled);
    Delay_ms(1000);

    OLED_Clear(&g_oled);
}

/**
 * @brief 交互测试主界面（静态框架 + 初始值，整屏刷新一次）
 */
static void App_DashboardInit(void)
{
    OLED_ShowString(&g_oled, 0,  0, "RUN:0s LED:OFF", OLED_8X16);
    OLED_ShowString(&g_oled, 0, 32, "EVT:NONE", OLED_8X16);
    OLED_ShowString(&g_oled, 0, 48, "RD:------------", OLED_8X16);
    App_RefreshEELine();        /* 行1：EEPROM 地址/页大小/后端 */
    OLED_Update(&g_oled);
}

/** @brief 行0：运行秒数 + LED 实际状态（直接读输出引脚，低电平点亮） */
static void App_RefreshRunLine(void)
{
    char buf[24];
    const char *led = LL_GPIO_IsOutputPinSet(led_GPIO_Port, led_Pin) ? "OFF" : "ON";

    snprintf(buf, sizeof(buf), "RUN:%lus LED:%s",
             (unsigned long)(GetTick_ms() / 1000), led);
    App_ShowLine(0, buf);
}

/** @brief 行1：AT24xx 从机地址 / 页大小 / IIC 后端 */
static void App_RefreshEELine(void)
{
    char buf[24];

    if (g_at24xx_online) {
        snprintf(buf, sizeof(buf), "EE:%02X %uB %s",
                 g_at24cxx.dev_addr, g_at24cxx.addr_bytes, BACKEND_STR);
    } else {
        snprintf(buf, sizeof(buf), "EE:NOT FOUND");
    }
    App_ShowLine(16, buf);
}

/** @brief 行3：读回数据预览（前 13 字节，非可打印字符显示为 '.'） */
static void App_ShowReadPreview(const uint8_t *buf, uint16_t len)
{
    char line[20];

    memcpy(line, "RD:", 3);
    for (uint8_t i = 0; i < 13; i++) {
        if (i >= len) {
            line[3 + i] = ' ';
        } else {
            uint8_t ch = buf[i];
            line[3 + i] = (ch >= 32 && ch <= 126) ? (char)ch : '.';
        }
    }
    line[16] = '\0';
    App_ShowLine(48, line);
}

/**
 * @brief 上电自测：先读回比对，不一致才写入并复核
 * @note  先读而不是无条件先写，一方面可验证上次掉电前写入的数据是否保持，
 *        另一方面避免每次上电都擦写测试区。
 */
static void App_EE_SelfTest(void)
{
    AT24CXX_Result rd;

    if (!g_at24xx_online) {
        App_ShowLine(32, "EVT:EE NOT FOUND");
        return;
    }

    App_RefreshEELine();        /* 行1 显示当前字地址宽度 */
    memset(s_ee_read_buf, 0, sizeof(s_ee_read_buf));

    rd = AT24Cxx_ReadBuf(&g_at24cxx, EE_TEST_ADDR, s_ee_read_buf, EE_TEST_LEN);
    if (rd == AT24CXX_OK && memcmp(s_ee_read_buf, s_ee_pattern, EE_TEST_LEN) == 0) {
        App_ShowLine(32, "EVT:EE DATA OK");     /* 上一次写入的数据仍然保持 */
    } else {
        AT24CXX_Result wr = AT24Cxx_WriteBuf(&g_at24cxx, EE_TEST_ADDR,
                                             s_ee_pattern, EE_TEST_LEN);

        memset(s_ee_read_buf, 0, sizeof(s_ee_read_buf));
        rd = AT24Cxx_ReadBuf(&g_at24cxx, EE_TEST_ADDR, s_ee_read_buf, EE_TEST_LEN);

        if (wr != AT24CXX_OK) {
            App_ShowLine(32, "EVT:EE WR FAIL");
        } else if (rd != AT24CXX_OK) {
            App_ShowLine(32, "EVT:EE RD FAIL");
        } else if (memcmp(s_ee_read_buf, s_ee_pattern, EE_TEST_LEN) == 0) {
            App_ShowLine(32, "EVT:EE DATA OK");
        } else {
            App_ShowLine(32, "EVT:EE DATA ERR");
        }
    }

    App_ShowReadPreview(s_ee_read_buf, EE_TEST_LEN);
}

/** @brief K1 单击：把测试串写入 EEPROM（自动分页） */
static void App_EE_WriteTest(void)
{
    if (!g_at24xx_online) {
        App_ShowLine(32, "EVT:EE NOT FOUND");
        return;
    }
    AT24CXX_Result ret = AT24Cxx_WriteBuf(&g_at24cxx, EE_TEST_ADDR,
                                          s_ee_pattern, EE_TEST_LEN);
    App_ShowLine(32, (ret == AT24CXX_OK) ? "EVT:K1 WR DONE" : "EVT:K1 WR FAIL");
}

/** @brief K2 单击：读回测试区并校验 */
static void App_EE_ReadTest(void)
{
    uint8_t buf[EE_TEST_LEN] = {0};

    if (!g_at24xx_online) {
        App_ShowLine(32, "EVT:EE NOT FOUND");
        return;
    }
    AT24CXX_Result ret = AT24Cxx_ReadBuf(&g_at24cxx, EE_TEST_ADDR, buf, EE_TEST_LEN);

    if (ret != AT24CXX_OK) {
        App_ShowLine(32, "EVT:K2 RD FAIL");
    } else if (memcmp(buf, s_ee_pattern, EE_TEST_LEN) == 0) {
        App_ShowLine(32, "EVT:K2 RD OK");
    } else {
        App_ShowLine(32, "EVT:K2 RD DIFF");
    }
    App_ShowReadPreview(buf, EE_TEST_LEN);
}

/** @brief K1 长按：测试区填充 0xFF（EEPROM 无擦除，用覆盖写模拟） */
static void App_EE_ClearTest(void)
{
    uint8_t fill[EE_TEST_LEN];

    if (!g_at24xx_online) {
        App_ShowLine(32, "EVT:EE NOT FOUND");
        return;
    }
    memset(fill, 0xFF, sizeof(fill));

    AT24CXX_Result ret = AT24Cxx_WriteBuf(&g_at24cxx, EE_TEST_ADDR, fill, sizeof(fill));
    App_ShowLine(32, (ret == AT24CXX_OK) ? "EVT:K1 CLEARED" : "EVT:K1 CLR FAIL");
}

/**
 * @brief 检测按键计数变化并执行对应 EEPROM 任务
 * @note  K1 单击=写测试串；K2 单击=读回校验；K1 长按=填 0xFF；K2 长按=重新检测
 */
static void App_CheckKeyEvents(void)
{
    uint8_t c1 = Key_GetClickCount(KEY_ID_1);
    uint8_t c2 = Key_GetClickCount(KEY_ID_2);
    uint8_t l1 = Key_GetLongCount(KEY_ID_1);
    uint8_t l2 = Key_GetLongCount(KEY_ID_2);
    uint8_t acted = 0U;

    if (c1 == s_last_c1 && c2 == s_last_c2 &&
        l1 == s_last_l1 && l2 == s_last_l2) return;

    /* 各事件独立判断：同一扫描周期内多个计数变化时都执行，避免丢事件 */
    if (c1 != s_last_c1) {
        App_EE_WriteTest();
        acted = 1U;
    }
    if (c2 != s_last_c2) {
        App_EE_ReadTest();
        acted = 1U;
    }
    if (l1 != s_last_l1) {
        App_EE_ClearTest();
        acted = 1U;
    }
    if (l2 != s_last_l2) {
        if (AT24XX_PortInit() == AT24CXX_OK) {
            App_EE_SelfTest();      /* 重新检测并自动匹配字地址宽度 */
        } else {
            App_RefreshEELine();
            App_ShowLine(32, "EVT:K2 REINIT FAIL");
        }
        acted = 1U;
    }

    s_last_c1 = c1; s_last_c2 = c2;
    s_last_l1 = l1; s_last_l2 = l2;

    if (acted) {
        App_RefreshRunLine();   /* 行0：LED 状态立即同步 */
    }
}

/**
 * @brief 测试初始化：OLED 自测 → 主界面 → EEPROM 读写自测
 * @note  AT24xx 检测须由 main.c 先调用 AT24XX_PortInit() 完成
 */
void AppTest_Init(void)
{
    App_OLED_SelfTest();

    App_DashboardInit();
    App_EE_SelfTest();
}

/**
 * @brief 测试周期任务：按键扫描 + 运行指示，非阻塞
 */
void AppTest_Task(void)
{
    uint32_t now = GetTick_ms();

    /* 按键扫描：按 TICKS_INTERVAL(5ms) 驱动 MultiButton 状态机，
       事件回调与 LED 业务动作在本上下文执行，有事件则刷新对应显示行 */
    if (now - s_last_key_tick >= TICKS_INTERVAL)
    {
        s_last_key_tick = now;

        button_ticks();       /* MultiButton 消抖、状态机、事件回调 */
        Key_ProcessEvents();  /* 应用层业务动作（LED） */
        App_CheckKeyEvents(); /* EEPROM 测试任务 + 显示行按需刷新 */
    }

    /* 运行指示：1s 刷新行0，秒数递增即证明心跳与显示链路正常 */
    if (now - s_last_run_tick >= 1000)
    {
        s_last_run_tick = now;
        App_RefreshRunLine();
    }
}
