#include "modbus_device.h"
#include "modbus_port.h"
#include "bsp_includes.h"

static MbDevice dev;
static uint8_t rx[MB_RTU_MAX_ADU], tx[MB_RTU_MAX_ADU];
static uint32_t sample_at, samples, heartbeat_at, key_at;

static void refresh(uint32_t now)
{
    if (dev.holding[0] && now - sample_at >= dev.holding[1]) {
        sample_at = now; ++samples;
    }
    if (now - heartbeat_at >= dev.holding[4]) {
        heartbeat_at = now;
    }
    if (now - key_at >= TICKS_INTERVAL) {
        key_at = now;
        button_ticks();
        Key_ProcessEvents();
    }
    /* Bit 0 is the actual active-low PC13 output; other bits are RAM fixtures. */
    dev.coils = (uint8_t)((dev.coils & 0xfeu) |
        (!LL_GPIO_IsOutputPinSet(led_GPIO_Port, led_Pin) ? 1u : 0u));
    dev.discrete = (uint8_t)((!Read_Pin_Level(KEY_ID_1) ? 1u : 0u) |
        (!Read_Pin_Level(KEY_ID_2) ? 2u : 0u) |
        (g_at24xx_online ? 4u : 0u));
    dev.input[0] = (uint16_t)(dev.holding[0] | (samples > dev.holding[2] ? 256u : 0u));
    dev.input[1] = (uint16_t)now;
    dev.input[2] = (uint16_t)(now >> 16);
}
void MbApp_Init(void)
{
    Mb_Init(&dev);
    sample_at = heartbeat_at = key_at = GetTick_ms();
    samples = 0u;
    refresh(key_at);
    MbPort_Init();
}
void MbApp_Poll(void)
{
    size_t n, reply;
    uint8_t old_coils;
    dev.errors += MbPort_TakeErrors();
    refresh(GetTick_ms());
    n = MbPort_Receive(rx);
    if (!n) return;
    old_coils = dev.coils;
    reply = Mb_Process(&dev, MB_UNIT_ID, rx, n, tx);
    if ((old_coils ^ dev.coils) & 1u) {
        if (dev.coils & 1u) LL_GPIO_ResetOutputPin(led_GPIO_Port, led_Pin);
        else LL_GPIO_SetOutputPin(led_GPIO_Port, led_Pin);
    }
    if (reply && !MbPort_Send(tx, reply)) ++dev.errors;
}
