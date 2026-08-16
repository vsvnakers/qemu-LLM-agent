#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "qemu/bitops.h"

#define TIM2_BASE 0x40000000U
#define TIM2_CR1   (TIM2_BASE + 0x00)
#define TIM2_DIER  (TIM2_BASE + 0x0c)
#define TIM2_SR    (TIM2_BASE + 0x10)
#define TIM2_EGR   (TIM2_BASE + 0x14)
#define TIM2_CNT   (TIM2_BASE + 0x24)
#define TIM2_PSC   (TIM2_BASE + 0x28)
#define TIM2_ARR   (TIM2_BASE + 0x2c)
#define TIM2_CCR2  (TIM2_BASE + 0x38)

#define CR1_CEN   (1U << 0)
#define CR1_UDIS  (1U << 1)
#define CR1_URS   (1U << 2)
#define CR1_OPM   (1U << 3)
#define CR1_ARPE  (1U << 7)

#define DIER_UIE   (1U << 0)
#define DIER_CC2IE (1U << 2)

#define SR_UIF   (1U << 0)
#define SR_CC2IF (1U << 2)

#define EGR_UG   (1U << 0)
#define EGR_CC2G (1U << 2)

#define CLOCK_HZ 72000000ULL
#define NS_PER_SEC 1000000000ULL

static uint64_t ticks_to_ns(uint64_t ticks)
{
    return ticks * NS_PER_SEC / CLOCK_HZ;
}

static void test_reset(void)
{
    QTestState *qts = qtest_start("-machine stm32f103");
    qtest_irq_intercept_in(qts, "/machine/soc/armv7m");

    g_assert_cmphex(qtest_readl(qts, TIM2_CR1), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIM2_DIER), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIM2_SR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIM2_CNT), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIM2_PSC), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIM2_ARR), ==, 0xffff);
    g_assert_cmphex(qtest_readl(qts, TIM2_CCR2), ==, 0);
    g_assert_cmpint(get_irq(28), ==, 0);

    qtest_quit(qts);
}

static void test_update_irq_assert_clear(void)
{
    QTestState *qts = qtest_start("-machine stm32f103");
    qtest_irq_intercept_in(qts, "/machine/soc/armv7m");

    /* Enable update interrupt, set UIF via UG, check IRQ asserted. */
    qtest_writel(qts, TIM2_DIER, DIER_UIE);
    qtest_writel(qts, TIM2_EGR, EGR_UG);
    g_assert_cmpint(get_irq(28), ==, 1);
    g_assert_cmphex(qtest_readl(qts, TIM2_SR), ==, SR_UIF);

    /* Clear UIF by writing 1 to it (rc_w0), check IRQ deasserted. */
    qtest_writel(qts, TIM2_SR, SR_UIF);
    g_assert_cmpint(get_irq(28), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIM2_SR), ==, 0);

    qtest_quit(qts);
}

static void test_cc2(void)
{
    QTestState *qts = qtest_start("-machine stm32f103");
    qtest_irq_intercept_in(qts, "/machine/soc/armv7m");

    /* Configure timer: PSC=71, ARR=999, CCR2=250, enable CC2 interrupt. */
    qtest_writel(qts, TIM2_PSC, 71);
    qtest_writel(qts, TIM2_ARR, 999);
    qtest_writel(qts, TIM2_CCR2, 250);
    qtest_writel(qts, TIM2_EGR, EGR_UG); /* load prescaler and ARR */
    qtest_writel(qts, TIM2_SR, 0);
    qtest_writel(qts, TIM2_DIER, DIER_CC2IE);
    qtest_writel(qts, TIM2_CR1, CR1_CEN);

    /* At 250 us, CC2IF should be set and IRQ asserted. */
    clock_step(ticks_to_ns(250));
    g_assert_cmphex(qtest_readl(qts, TIM2_SR) & SR_CC2IF, ==, SR_CC2IF);
    g_assert_cmpint(get_irq(28), ==, 1);

    /* Clear CC2IF, IRQ should deassert. */
    qtest_writel(qts, TIM2_SR, SR_CC2IF);
    g_assert_cmpint(get_irq(28), ==, 0);

    qtest_quit(qts);
}

static void test_psc_preload(void)
{
    QTestState *qts = qtest_start("-machine stm32f103");
    qtest_irq_intercept_in(qts, "/machine/soc/armv7m");

    /* Start with PSC=0, ARR=9, so overflow every 10 ticks at 72 MHz. */
    qtest_writel(qts, TIM2_PSC, 0);
    qtest_writel(qts, TIM2_ARR, 9);
    qtest_writel(qts, TIM2_EGR, EGR_UG);
    qtest_writel(qts, TIM2_SR, 0);
    qtest_writel(qts, TIM2_DIER, DIER_UIE);
    qtest_writel(qts, TIM2_CR1, CR1_CEN);

    /* Advance 5 ticks (5 ns at 72 MHz). */
    clock_step(ticks_to_ns(5));
    g_assert_cmphex(qtest_readl(qts, TIM2_CNT), ==, 5);

    /* Write new PSC=71 while running; should not take effect until update. */
    qtest_writel(qts, TIM2_PSC, 71);
    clock_step(ticks_to_ns(5)); /* total 10 ticks, overflow occurs, update event */
    g_assert_cmphex(qtest_readl(qts, TIM2_CNT), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIM2_SR) & SR_UIF, ==, SR_UIF);

    /* Now PSC active should be 71, so next tick takes 72 ns. */
    clock_step(ticks_to_ns(1));
    g_assert_cmphex(qtest_readl(qts, TIM2_CNT), ==, 1);

    qtest_quit(qts);
}

static void test_arpe(void)
{
    QTestState *qts = qtest_start("-machine stm32f103");
    qtest_irq_intercept_in(qts, "/machine/soc/armv7m");

    /* ARPE=0: ARR write is immediate. */
    qtest_writel(qts, TIM2_ARR, 99);
    g_assert_cmphex(qtest_readl(qts, TIM2_ARR), ==, 99);
    qtest_writel(qts, TIM2_EGR, EGR_UG);
    qtest_writel(qts, TIM2_CR1, CR1_CEN);
    clock_step(ticks_to_ns(100)); /* overflow after 100 ticks */
    g_assert_cmphex(qtest_readl(qts, TIM2_CNT), ==, 0);

    /* ARPE=1: ARR write is buffered until update. */
    qtest_writel(qts, TIM2_CR1, CR1_CEN | CR1_ARPE);
    qtest_writel(qts, TIM2_ARR, 199);
    g_assert_cmphex(qtest_readl(qts, TIM2_ARR), ==, 199);
    /* Current active ARR is still 99, so overflow at 100 ticks. */
    clock_step(ticks_to_ns(100));
    g_assert_cmphex(qtest_readl(qts, TIM2_CNT), ==, 0);
    /* After update, active ARR becomes 199. */
    clock_step(ticks_to_ns(200));
    g_assert_cmphex(qtest_readl(qts, TIM2_CNT), ==, 0);

    qtest_quit(qts);
}

static void test_udis(void)
{
    QTestState *qts = qtest_start("-machine stm32f103");
    qtest_irq_intercept_in(qts, "/machine/soc/armv7m");

    /* Set UDIS, start timer, overflow should not set UIF or update ARR. */
    qtest_writel(qts, TIM2_ARR, 9);
    qtest_writel(qts, TIM2_EGR, EGR_UG);
    qtest_writel(qts, TIM2_SR, 0);
    qtest_writel(qts, TIM2_DIER, DIER_UIE);
    qtest_writel(qts, TIM2_CR1, CR1_CEN | CR1_UDIS);
    clock_step(ticks_to_ns(10));
    g_assert_cmphex(qtest_readl(qts, TIM2_SR) & SR_UIF, ==, 0);
    g_assert_cmpint(get_irq(28), ==, 0);

    qtest_quit(qts);
}

static void test_urs(void)
{
    QTestState *qts = qtest_start("-machine stm32f103");
    qtest_irq_intercept_in(qts, "/machine/soc/armv7m");

    /* URS=1: software UG should not set UIF, but hardware overflow should. */
    qtest_writel(qts, TIM2_CR1, CR1_URS);
    qtest_writel(qts, TIM2_DIER, DIER_UIE);
    qtest_writel(qts, TIM2_EGR, EGR_UG);
    g_assert_cmphex(qtest_readl(qts, TIM2_SR) & SR_UIF, ==, 0);
    g_assert_cmpint(get_irq(28), ==, 0);

    /* Start timer with ARR=9, overflow should set UIF. */
    qtest_writel(qts, TIM2_ARR, 9);
    qtest_writel(qts, TIM2_EGR, EGR_UG);
    qtest_writel(qts, TIM2_CR1, CR1_CEN | CR1_URS);
    clock_step(ticks_to_ns(10));
    g_assert_cmphex(qtest_readl(qts, TIM2_SR) & SR_UIF, ==, SR_UIF);
    g_assert_cmpint(get_irq(28), ==, 1);

    qtest_quit(qts);
}

static void test_one_pulse(void)
{
    QTestState *qts = qtest_start("-machine stm32f103");
    qtest_irq_intercept_in(qts, "/machine/soc/armv7m");

    /* OPM=1: after first update, CEN is cleared. */
    qtest_writel(qts, TIM2_ARR, 9);
    qtest_writel(qts, TIM2_EGR, EGR_UG);
    qtest_writel(qts, TIM2_SR, 0);
    qtest_writel(qts, TIM2_DIER, DIER_UIE);
    qtest_writel(qts, TIM2_CR1, CR1_CEN | CR1_OPM);
    clock_step(ticks_to_ns(10));
    g_assert_cmphex(qtest_readl(qts, TIM2_CR1) & CR1_CEN, ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIM2_SR) & SR_UIF, ==, SR_UIF);
    g_assert_cmpint(get_irq(28), ==, 1);

    /* Further clock steps should not change CNT. */
    clock_step(ticks_to_ns(100));
    g_assert_cmphex(qtest_readl(qts, TIM2_CNT), ==, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("stm32f103/timer/reset", test_reset);
    qtest_add_func("stm32f103/timer/update_irq_assert_clear", test_update_irq_assert_clear);
    qtest_add_func("stm32f103/timer/cc2", test_cc2);
    qtest_add_func("stm32f103/timer/psc_preload", test_psc_preload);
    qtest_add_func("stm32f103/timer/arpe", test_arpe);
    qtest_add_func("stm32f103/timer/udis", test_udis);
    qtest_add_func("stm32f103/timer/urs", test_urs);
    qtest_add_func("stm32f103/timer/one_pulse", test_one_pulse);

    return g_test_run();
}
