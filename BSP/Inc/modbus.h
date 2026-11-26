#ifndef MODBUS_H
#define MODBUS_H

#include <stddef.h>
#include <stdint.h>

#define MB_RTU_MAX_ADU 256u
#define MB_UNIT_ID 1u
#define MB_COIL_COUNT 8u
#define MB_DI_COUNT 4u
#define MB_REG_COUNT 8u

/* Fixed device contract shared with VirtualEmbeddedLab. No MCU/RTOS dependency.
 * The application supplies input snapshots and applies output changes. */
typedef struct {
    uint8_t coils;
    uint8_t discrete;
    uint16_t holding[MB_REG_COUNT];
    uint16_t input[MB_REG_COUNT];
    uint32_t requests;
    uint32_t responses;
    uint32_t errors;
} MbDevice;

void Mb_Init(MbDevice *dev);
uint16_t Mb_Crc16(const uint8_t *data, size_t size);
/* Separate buffers, each at least MB_RTU_MAX_ADU bytes for TX.
 * Returns complete response length, or 0 for ignored/invalid/broadcast frames.
 * All multi-write parameters are checked before any mutation. */
size_t Mb_Process(MbDevice *dev, uint8_t unit, const uint8_t *rx, size_t len,
                  uint8_t tx[MB_RTU_MAX_ADU]);
#endif
