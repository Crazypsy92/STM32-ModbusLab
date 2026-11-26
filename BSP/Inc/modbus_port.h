#ifndef MODBUS_PORT_H
#define MODBUS_PORT_H

#include "modbus.h"

/* Default: USART1 PA9 TX / PA10 RX, 115200 8E1; TIM3 1 MHz.
 * RS485 direction control is opt-in at build time, PA8 DE and /RE tied. */
#ifndef BSP_MODBUS_RS485
#define BSP_MODBUS_RS485 0
#endif

void MbPort_Init(void);
size_t MbPort_Receive(uint8_t frame[MB_RTU_MAX_ADU]);
int MbPort_Send(const uint8_t *frame, size_t len);
uint32_t MbPort_TakeErrors(void);
void MbPort_UartIrq(void);
void MbPort_TimerIrq(void);
#endif
