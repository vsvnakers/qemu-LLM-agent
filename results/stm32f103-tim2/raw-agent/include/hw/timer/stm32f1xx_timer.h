#ifndef HW_TIMER_STM32F1XX_TIMER_H
#define HW_TIMER_STM32F1XX_TIMER_H

#include "hw/sysbus.h"
#include "qemu/timer.h"

#define TYPE_STM32F1XX_TIMER "stm32f1xx-timer"
#define STM32F1XX_TIMER(obj) OBJECT_CHECK(STM32F1XXTimerState, (obj), TYPE_STM32F1XX_TIMER)

#define STM32F1XX_TIMER_MMIO_SIZE 0x400
#define STM32F1XX_TIMER_IRQ_COUNT 1

#define STM32F1XX_TIMER_CR1      0x00
#define STM32F1XX_TIMER_CR2      0x04
#define STM32F1XX_TIMER_SMCR     0x08
#define STM32F1XX_TIMER_DIER     0x0c
#define STM32F1XX_TIMER_SR       0x10
#define STM32F1XX_TIMER_EGR      0x14
#define STM32F1XX_TIMER_CCMR1    0x18
#define STM32F1XX_TIMER_CCMR2    0x1c
#define STM32F1XX_TIMER_CCER     0x20
#define STM32F1XX_TIMER_CNT      0x24
#define STM32F1XX_TIMER_PSC      0x28
#define STM32F1XX_TIMER_ARR      0x2c
#define STM32F1XX_TIMER_CCR1     0x34
#define STM32F1XX_TIMER_CCR2     0x38
#define STM32F1XX_TIMER_CCR3     0x3c
#define STM32F1XX_TIMER_CCR4     0x40
#define STM32F1XX_TIMER_DCR      0x48
#define STM32F1XX_TIMER_DMAR     0x4c

#define STM32F1XX_TIMER_CR1_CEN  (1U << 0)
#define STM32F1XX_TIMER_CR1_UDIS (1U << 1)
#define STM32F1XX_TIMER_CR1_URS  (1U << 2)
#define STM32F1XX_TIMER_CR1_OPM  (1U << 3)
#define STM32F1XX_TIMER_CR1_ARPE (1U << 7)

#define STM32F1XX_TIMER_DIER_UIE   (1U << 0)
#define STM32F1XX_TIMER_DIER_CC1IE (1U << 1)
#define STM32F1XX_TIMER_DIER_CC2IE (1U << 2)
#define STM32F1XX_TIMER_DIER_CC3IE (1U << 3)
#define STM32F1XX_TIMER_DIER_CC4IE (1U << 4)

#define STM32F1XX_TIMER_SR_UIF   (1U << 0)
#define STM32F1XX_TIMER_SR_CC1IF (1U << 1)
#define STM32F1XX_TIMER_SR_CC2IF (1U << 2)
#define STM32F1XX_TIMER_SR_CC3IF (1U << 3)
#define STM32F1XX_TIMER_SR_CC4IF (1U << 4)

#define STM32F1XX_TIMER_EGR_UG   (1U << 0)
#define STM32F1XX_TIMER_EGR_CC1G (1U << 1)
#define STM32F1XX_TIMER_EGR_CC2G (1U << 2)
#define STM32F1XX_TIMER_EGR_CC3G (1U << 3)
#define STM32F1XX_TIMER_EGR_CC4G (1U << 4)

#define STM32F1XX_TIMER_CCMR_CC1S_MASK 0x0003
#define STM32F1XX_TIMER_CCMR_CC2S_MASK 0x0300
#define STM32F1XX_TIMER_CCMR_CC3S_MASK 0x0003
#define STM32F1XX_TIMER_CCMR_CC4S_MASK 0x0300

#define STM32F1XX_TIMER_CCMR1_CC1S_SHIFT 0
#define STM32F1XX_TIMER_CCMR1_CC2S_SHIFT 8
#define STM32F1XX_TIMER_CCMR2_CC3S_SHIFT 0
#define STM32F1XX_TIMER_CCMR2_CC4S_SHIFT 8

#define STM32F1XX_TIMER_CCER_CC1E (1U << 0)
#define STM32F1XX_TIMER_CCER_CC2E (1U << 4)
#define STM32F1XX_TIMER_CCER_CC3E (1U << 8)
#define STM32F1XX_TIMER_CCER_CC4E (1U << 12)

#define STM32F1XX_TIMER_ARR_RESET 0xffff

#define STM32F1XX_TIMER_UPDATE_FLAGS (STM32F1XX_TIMER_SR_UIF | \
                                      STM32F1XX_TIMER_SR_CC1IF | \
                                      STM32F1XX_TIMER_SR_CC2IF | \
                                      STM32F1XX_TIMER_SR_CC3IF | \
                                      STM32F1XX_TIMER_SR_CC4IF)

typedef struct STM32F1XXTimerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    QEMUTimer *timer;
    uint32_t clock_freq_hz;

    uint16_t cr1;
    uint16_t cr2;
    uint16_t smcr;
    uint16_t dier;
    uint16_t sr;
    uint16_t ccmr1;
    uint16_t ccmr2;
    uint16_t ccer;
    uint16_t cnt;
    uint16_t psc;
    uint16_t arr;
    uint16_t ccr[4];
    uint16_t dcr;
    uint16_t dmar;

    uint16_t psc_active;
    uint16_t arr_active;
    bool timer_pending;
} STM32F1XXTimerState;

#endif
