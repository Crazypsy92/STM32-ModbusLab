#include "modbus_port.h"
#include "main.h"

/* USART/TIM register-level port: original LL package does not include the
 * USART/TIM LL headers. GPIO and peripheral clock setup use the existing LL.
 * Fixed 72 MHz APB2 and 72 MHz TIM3 input clock (APB1 /2 with timer x2).
 * RXNE timestamps are end-of-character observations, not physical edge capture.
 * Add one character time to distinguish inter-character idle approximately. */
#define BAUD 115200u
#define CHAR_US 96u
#define GAP1_US (750u + CHAR_US)
#define GAP3_US (1750u + CHAR_US)
#define CYCLES_PER_US 72u
#define UART_ERRORS (USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE)

static volatile uint8_t rx[MB_RTU_MAX_ADU], ready[MB_RTU_MAX_ADU];
static volatile uint16_t rx_len, ready_len;
static volatile uint8_t active, invalid;
static volatile uint32_t last_cycle, errors;

static void direction(int send)
{
#if BSP_MODBUS_RS485
    if (send) LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_8);
    else LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_8);
#else
    (void)send;
#endif
}
static void timer_arm(void)
{
    TIM3->CR1 = TIM_CR1_OPM;
    TIM3->CNT = 0u;
    TIM3->SR = 0u;
    TIM3->CR1 = TIM_CR1_OPM | TIM_CR1_CEN;
}
static void finish_rx(void)
{
    if (!active) return;
    if (invalid || !rx_len || ready_len) ++errors;
    else {
        for (uint16_t i = 0; i < rx_len; ++i) ready[i] = rx[i];
        ready_len = rx_len;
    }
    active = invalid = 0u;
    rx_len = 0u;
}
void MbPort_Init(void)
{
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOA | LL_APB2_GRP1_PERIPH_USART1);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_9, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_9, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_9, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_10, LL_GPIO_MODE_FLOATING);
#if BSP_MODBUS_RS485
    LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_8);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_8, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_8, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_8, LL_GPIO_SPEED_FREQ_HIGH);
#endif
    direction(0);
    rx_len = ready_len = 0u;
    active = invalid = 0u; errors = 0u;
    TIM3->CR1 = 0u;
    TIM3->PSC = 71u;
    TIM3->ARR = GAP3_US - 1u;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0u;
    TIM3->DIER = TIM_DIER_UIE;

    USART1->CR1 = 0u; USART1->CR2 = 0u;
    USART1->CR3 = USART_CR3_EIE;
    USART1->BRR = (72000000u + BAUD/2u) / BAUD;
    /* STM32F1 M=1 + PCE=1 gives 8 payload bits and even parity. STOP=00: 1. */
    (void)USART1->SR; (void)USART1->DR;
    NVIC_SetPriority(USART1_IRQn, 5u);
    NVIC_SetPriority(TIM3_IRQn, 5u);
    NVIC_ClearPendingIRQ(USART1_IRQn); NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(USART1_IRQn); NVIC_EnableIRQ(TIM3_IRQn);
    USART1->CR1 = USART_CR1_UE | USART_CR1_M | USART_CR1_PCE |
        USART_CR1_PEIE | USART_CR1_RE | USART_CR1_TE | USART_CR1_RXNEIE;
}
void MbPort_UartIrq(void)
{
    uint32_t sr = USART1->SR;
    if (!(sr & (USART_SR_RXNE | UART_ERRORS))) return;
    uint8_t b = (uint8_t)USART1->DR;
    uint32_t now = DWT->CYCCNT;
    if (active) {
        uint32_t elapsed = now - last_cycle;
        if (elapsed >= GAP3_US*CYCLES_PER_US) finish_rx();
        else if (elapsed > GAP1_US*CYCLES_PER_US) invalid = 1u;
    }
    active = 1u;
    last_cycle = now;
    if (sr & UART_ERRORS) invalid = 1u;
    if (sr & USART_SR_RXNE) {
        if (rx_len < MB_RTU_MAX_ADU) rx[rx_len++] = b;
        else invalid = 1u;
    }
    timer_arm();
}
void MbPort_TimerIrq(void)
{
    if (!(TIM3->SR & TIM_SR_UIF)) return;
    TIM3->SR = 0u;
    /* Same IRQ priority: service a pending received byte before closing a frame. */
    if (USART1->SR & (USART_SR_RXNE | UART_ERRORS)) {
        MbPort_UartIrq();
        return;
    }
    finish_rx();
}
size_t MbPort_Receive(uint8_t *frame)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint16_t n = ready_len;
    for (uint16_t i = 0; i < n; ++i) frame[i] = ready[i];
    ready_len = 0u;
    __set_PRIMASK(mask);
    return n;
}
uint32_t MbPort_TakeErrors(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint32_t count = errors; errors = 0u;
    __set_PRIMASK(mask);
    return count;
}
int MbPort_Send(const uint8_t *frame, size_t len)
{
    if (!len || len > MB_RTU_MAX_ADU) return 0;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    /* Single-master request/response contract: don't overwrite a newer request. */
    if (active || ready_len || (USART1->SR & (USART_SR_RXNE | UART_ERRORS))) {
        __set_PRIMASK(mask);
        return 0;
    }
    USART1->CR1 &= ~(USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_PEIE);
    USART1->CR3 &= ~USART_CR3_EIE;
    __set_PRIMASK(mask);
    direction(1);
    uint32_t start = DWT->CYCCNT;
    for (size_t i = 0; i < len; ++i) {
        while (!(USART1->SR & USART_SR_TXE))
            if (DWT->CYCCNT - start > 30000u*CYCLES_PER_US) goto failed;
        USART1->DR = frame[i];
    }
    while (!(USART1->SR & USART_SR_TC))
        if (DWT->CYCCNT - start > 30000u*CYCLES_PER_US) goto failed;
    direction(0);  /* release only after final stop bit (TC), not TXE */
    USART1->CR3 |= USART_CR3_EIE;
    USART1->CR1 |= USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_PEIE;
    return 1;
failed:
    /* Abort the transmitter on timeout before releasing the bus. */
    USART1->CR1 &= ~USART_CR1_UE;
    direction(0);
    (void)USART1->SR; (void)USART1->DR;
    USART1->CR3 |= USART_CR3_EIE;
    USART1->CR1 |= USART_CR1_UE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_PEIE;
    return 0;
}
