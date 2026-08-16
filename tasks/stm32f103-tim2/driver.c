/* Minimal firmware access pattern supplied as Agent input. */
#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(address))
#define TIM2_BASE      0x40000000U
#define TIM2_CR1       REG32(TIM2_BASE + 0x00)
#define TIM2_DIER      REG32(TIM2_BASE + 0x0c)
#define TIM2_SR        REG32(TIM2_BASE + 0x10)
#define TIM2_EGR       REG32(TIM2_BASE + 0x14)
#define TIM2_PSC       REG32(TIM2_BASE + 0x28)
#define TIM2_ARR       REG32(TIM2_BASE + 0x2c)
#define TIM2_CCR2      REG32(TIM2_BASE + 0x38)

void timer_start(void)
{
    /* 72 MHz / 72 = 1 MHz; overflow at 1 ms; CC2 at 250 us. */
    TIM2_CR1 = 0;
    TIM2_PSC = 71;
    TIM2_ARR = 999;
    TIM2_CCR2 = 250;
    TIM2_EGR = 1;
    TIM2_SR = 0;
    TIM2_DIER = (1U << 0) | (1U << 2);
    TIM2_CR1 = 1;
}

void timer_irq(void)
{
    uint32_t pending = TIM2_SR;
    TIM2_SR = ~pending; /* rc_w0: clear only the flags that were observed. */
}

