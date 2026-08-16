#include "qemu/osdep.h"
#include "hw/timer/stm32f1xx_timer.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/timer.h"

#define STM32F1XX_TIMER_CLOCK_FREQ_DEFAULT 72000000ULL

static void stm32f1xx_timer_update_irq(STM32F1XXTimerState *s)
{
    bool level = (s->sr & s->dier & STM32F1XX_TIMER_UPDATE_FLAGS) != 0;
    qemu_set_irq(s->irq, level);
}

static uint64_t stm32f1xx_timer_tick_ns(STM32F1XXTimerState *s)
{
    return muldiv64(s->psc_active + 1, NANOSECONDS_PER_SECOND, s->clock_freq_hz);
}

static uint64_t stm32f1xx_timer_next_deadline_ns(STM32F1XXTimerState *s)
{
    uint64_t tick_ns = stm32f1xx_timer_tick_ns(s);
    uint64_t ticks_to_overflow = (uint64_t)s->arr_active + 1 - s->cnt;
    uint64_t overflow_ns = muldiv64_round_up(ticks_to_overflow, tick_ns, 1);
    uint64_t earliest_ns = overflow_ns;
    int i;

    for (i = 0; i < 4; i++) {
        uint16_t ccr = s->ccr[i];
        if (ccr > s->arr_active) {
            continue;
        }
        if (ccr >= s->cnt) {
            uint64_t ticks = ccr - s->cnt;
            uint64_t match_ns = muldiv64_round_up(ticks, tick_ns, 1);
            if (match_ns < earliest_ns) {
                earliest_ns = match_ns;
            }
        }
    }
    return earliest_ns;
}

static void stm32f1xx_timer_rearm(STM32F1XXTimerState *s)
{
    if (s->timer_pending) {
        timer_del(s->timer);
        s->timer_pending = false;
    }
    if (!(s->cr1 & STM32F1XX_TIMER_CR1_CEN)) {
        return;
    }
    uint64_t deadline_ns = stm32f1xx_timer_next_deadline_ns(s);
    timer_mod_ns(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + deadline_ns);
    s->timer_pending = true;
}

static void stm32f1xx_timer_update_event(STM32F1XXTimerState *s, bool from_overflow)
{
    bool udis = (s->cr1 & STM32F1XX_TIMER_CR1_UDIS) != 0;
    bool urs = (s->cr1 & STM32F1XX_TIMER_CR1_URS) != 0;

    if (!udis) {
        s->psc_active = s->psc;
        s->arr_active = s->arr;
        if (!urs || from_overflow) {
            s->sr |= STM32F1XX_TIMER_SR_UIF;
        }
    }

    if (s->cr1 & STM32F1XX_TIMER_CR1_OPM) {
        s->cr1 &= ~STM32F1XX_TIMER_CR1_CEN;
    }

    stm32f1xx_timer_update_irq(s);
    stm32f1xx_timer_rearm(s);
}

static void stm32f1xx_timer_overflow(STM32F1XXTimerState *s)
{
    s->cnt = 0;
    stm32f1xx_timer_update_event(s, true);
}

static void stm32f1xx_timer_compare_match(STM32F1XXTimerState *s, int channel)
{
    s->sr |= (1U << (STM32F1XX_TIMER_SR_CC1IF + channel));
    stm32f1xx_timer_update_irq(s);
}

static void stm32f1xx_timer_tick(void *opaque)
{
    STM32F1XXTimerState *s = opaque;
    s->timer_pending = false;

    uint64_t tick_ns = stm32f1xx_timer_tick_ns(s);
    uint64_t ticks_to_overflow = (uint64_t)s->arr_active + 1 - s->cnt;
    uint64_t overflow_ns = muldiv64_round_up(ticks_to_overflow, tick_ns, 1);
    uint64_t elapsed_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - timer_expired_time_ns(s->timer);

    if (elapsed_ns >= overflow_ns) {
        stm32f1xx_timer_overflow(s);
        return;
    }

    uint64_t ticks_elapsed = elapsed_ns / tick_ns;
    s->cnt += ticks_elapsed;

    for (int i = 0; i < 4; i++) {
        if (s->ccr[i] <= s->arr_active && s->ccr[i] >= s->cnt) {
            uint64_t match_ns = muldiv64_round_up(s->ccr[i] - s->cnt, tick_ns, 1);
            if (elapsed_ns >= match_ns) {
                stm32f1xx_timer_compare_match(s, i);
            }
        }
    }

    stm32f1xx_timer_rearm(s);
}

static void stm32f1xx_timer_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    STM32F1XXTimerState *s = opaque;
    uint16_t val = value & 0xffff;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported size %u\n", __func__, size);
        return;
    }

    switch (offset) {
    case STM32F1XX_TIMER_CR1:
        s->cr1 = val;
        if (!(s->cr1 & STM32F1XX_TIMER_CR1_CEN)) {
            if (s->timer_pending) {
                timer_del(s->timer);
                s->timer_pending = false;
            }
        } else {
            stm32f1xx_timer_rearm(s);
        }
        break;
    case STM32F1XX_TIMER_CR2:
        s->cr2 = val;
        break;
    case STM32F1XX_TIMER_SMCR:
        s->smcr = val;
        break;
    case STM32F1XX_TIMER_DIER:
        s->dier = val;
        stm32f1xx_timer_update_irq(s);
        break;
    case STM32F1XX_TIMER_SR:
        s->sr &= ~val;
        stm32f1xx_timer_update_irq(s);
        break;
    case STM32F1XX_TIMER_EGR:
        if (val & STM32F1XX_TIMER_EGR_UG) {
            s->cnt = 0;
            stm32f1xx_timer_update_event(s, false);
        }
        if (val & STM32F1XX_TIMER_EGR_CC1G) {
            stm32f1xx_timer_compare_match(s, 0);
        }
        if (val & STM32F1XX_TIMER_EGR_CC2G) {
            stm32f1xx_timer_compare_match(s, 1);
        }
        if (val & STM32F1XX_TIMER_EGR_CC3G) {
            stm32f1xx_timer_compare_match(s, 2);
        }
        if (val & STM32F1XX_TIMER_EGR_CC4G) {
            stm32f1xx_timer_compare_match(s, 3);
        }
        break;
    case STM32F1XX_TIMER_CCMR1:
        s->ccmr1 = val;
        break;
    case STM32F1XX_TIMER_CCMR2:
        s->ccmr2 = val;
        break;
    case STM32F1XX_TIMER_CCER:
        s->ccer = val;
        break;
    case STM32F1XX_TIMER_CNT:
        s->cnt = val;
        stm32f1xx_timer_rearm(s);
        break;
    case STM32F1XX_TIMER_PSC:
        s->psc = val;
        break;
    case STM32F1XX_TIMER_ARR:
        s->arr = val;
        if (!(s->cr1 & STM32F1XX_TIMER_CR1_ARPE)) {
            s->arr_active = val;
            stm32f1xx_timer_rearm(s);
        }
        break;
    case STM32F1XX_TIMER_CCR1:
    case STM32F1XX_TIMER_CCR2:
    case STM32F1XX_TIMER_CCR3:
    case STM32F1XX_TIMER_CCR4:
        s->ccr[(offset - STM32F1XX_TIMER_CCR1) / 4] = val;
        stm32f1xx_timer_rearm(s);
        break;
    case STM32F1XX_TIMER_DCR:
        s->dcr = val;
        break;
    case STM32F1XX_TIMER_DMAR:
        s->dmar = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx "\n", __func__, offset);
        break;
    }
}

static uint64_t stm32f1xx_timer_read(void *opaque, hwaddr offset, unsigned size)
{
    STM32F1XXTimerState *s = opaque;
    uint16_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported size %u\n", __func__, size);
        return 0;
    }

    switch (offset) {
    case STM32F1XX_TIMER_CR1:
        val = s->cr1;
        break;
    case STM32F1XX_TIMER_CR2:
        val = s->cr2;
        break;
    case STM32F1XX_TIMER_SMCR:
        val = s->smcr;
        break;
    case STM32F1XX_TIMER_DIER:
        val = s->dier;
        break;
    case STM32F1XX_TIMER_SR:
        val = s->sr;
        break;
    case STM32F1XX_TIMER_EGR:
        val = 0;
        break;
    case STM32F1XX_TIMER_CCMR1:
        val = s->ccmr1;
        break;
    case STM32F1XX_TIMER_CCMR2:
        val = s->ccmr2;
        break;
    case STM32F1XX_TIMER_CCER:
        val = s->ccer;
        break;
    case STM32F1XX_TIMER_CNT:
        val = s->cnt;
        break;
    case STM32F1XX_TIMER_PSC:
        val = s->psc;
        break;
    case STM32F1XX_TIMER_ARR:
        val = s->arr;
        break;
    case STM32F1XX_TIMER_CCR1:
    case STM32F1XX_TIMER_CCR2:
    case STM32F1XX_TIMER_CCR3:
    case STM32F1XX_TIMER_CCR4:
        val = s->ccr[(offset - STM32F1XX_TIMER_CCR1) / 4];
        break;
    case STM32F1XX_TIMER_DCR:
        val = s->dcr;
        break;
    case STM32F1XX_TIMER_DMAR:
        val = s->dmar;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx "\n", __func__, offset);
        break;
    }
    return val;
}

static const MemoryRegionOps stm32f1xx_timer_ops = {
    .read = stm32f1xx_timer_read,
    .write = stm32f1xx_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void stm32f1xx_timer_reset(DeviceState *dev)
{
    STM32F1XXTimerState *s = STM32F1XX_TIMER(dev);

    if (s->timer_pending) {
        timer_del(s->timer);
        s->timer_pending = false;
    }

    s->cr1 = 0;
    s->cr2 = 0;
    s->smcr = 0;
    s->dier = 0;
    s->sr = 0;
    s->ccmr1 = 0;
    s->ccmr2 = 0;
    s->ccer = 0;
    s->cnt = 0;
    s->psc = 0;
    s->arr = STM32F1XX_TIMER_ARR_RESET;
    s->ccr[0] = 0;
    s->ccr[1] = 0;
    s->ccr[2] = 0;
    s->ccr[3] = 0;
    s->dcr = 0;
    s->dmar = 0;

    s->psc_active = 0;
    s->arr_active = STM32F1XX_TIMER_ARR_RESET;

    qemu_set_irq(s->irq, 0);
}

static void stm32f1xx_timer_realize(DeviceState *dev, Error **errp)
{
    STM32F1XXTimerState *s = STM32F1XX_TIMER(dev);

    if (s->clock_freq_hz == 0) {
        error_setg(errp, "%s: clock-frequency must be non-zero", __func__);
        return;
    }

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, stm32f1xx_timer_tick, s);
}

static void stm32f1xx_timer_init(Object *obj)
{
    STM32F1XXTimerState *s = STM32F1XX_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &stm32f1xx_timer_ops, s,
                          TYPE_STM32F1XX_TIMER, STM32F1XX_TIMER_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static int stm32f1xx_timer_pre_save(void *opaque)
{
    STM32F1XXTimerState *s = opaque;
    /* Latch current count based on virtual time before saving. */
    if (s->timer_pending && (s->cr1 & STM32F1XX_TIMER_CR1_CEN)) {
        uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        uint64_t elapsed = now - timer_expired_time_ns(s->timer);
        uint64_t tick_ns = stm32f1xx_timer_tick_ns(s);
        uint64_t ticks = elapsed / tick_ns;
        s->cnt = (s->cnt + ticks) & 0xffff;
    }
    return 0;
}

static int stm32f1xx_timer_post_load(void *opaque, int version_id)
{
    STM32F1XXTimerState *s = opaque;
    /* Recompute IRQ and rearm timer after state is restored. */
    stm32f1xx_timer_update_irq(s);
    stm32f1xx_timer_rearm(s);
    return 0;
}

static const VMStateDescription vmstate_stm32f1xx_timer = {
    .name = TYPE_STM32F1XX_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = stm32f1xx_timer_pre_save,
    .post_load = stm32f1xx_timer_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(cr1, STM32F1XXTimerState),
        VMSTATE_UINT16(cr2, STM32F1XXTimerState),
        VMSTATE_UINT16(smcr, STM32F1XXTimerState),
        VMSTATE_UINT16(dier, STM32F1XXTimerState),
        VMSTATE_UINT16(sr, STM32F1XXTimerState),
        VMSTATE_UINT16(ccmr1, STM32F1XXTimerState),
        VMSTATE_UINT16(ccmr2, STM32F1XXTimerState),
        VMSTATE_UINT16(ccer, STM32F1XXTimerState),
        VMSTATE_UINT16(cnt, STM32F1XXTimerState),
        VMSTATE_UINT16(psc, STM32F1XXTimerState),
        VMSTATE_UINT16(arr, STM32F1XXTimerState),
        VMSTATE_UINT16_ARRAY(ccr, STM32F1XXTimerState, 4),
        VMSTATE_UINT16(dcr, STM32F1XXTimerState),
        VMSTATE_UINT16(dmar, STM32F1XXTimerState),
        VMSTATE_UINT16(psc_active, STM32F1XXTimerState),
        VMSTATE_UINT16(arr_active, STM32F1XXTimerState),
        VMSTATE_BOOL(timer_pending, STM32F1XXTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static Property stm32f1xx_timer_properties[] = {
    DEFINE_PROP_UINT32("clock-frequency", STM32F1XXTimerState, clock_freq_hz,
                       STM32F1XX_TIMER_CLOCK_FREQ_DEFAULT),
    DEFINE_PROP_END_OF_LIST()
};

static void stm32f1xx_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, stm32f1xx_timer_reset);
    device_class_set_props(dc, stm32f1xx_timer_properties);
    dc->vmsd = &vmstate_stm32f1xx_timer;
    dc->realize = stm32f1xx_timer_realize;
}

static const TypeInfo stm32f1xx_timer_info = {
    .name = TYPE_STM32F1XX_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F1XXTimerState),
    .instance_init = stm32f1xx_timer_init,
    .class_init = stm32f1xx_timer_class_init,
};

static void stm32f1xx_timer_register_types(void)
{
    type_register_static(&stm32f1xx_timer_info);
}

type_init(stm32f1xx_timer_register_types)
