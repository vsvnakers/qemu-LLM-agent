# QEMU tree conventions supplied to the Agent

The target is the QEMU tree used by stage one, not an older downstream fork.
Use these current-tree conventions:

- Includes: `hw/core/irq.h`, `hw/core/qdev-properties.h`,
  `hw/core/sysbus.h`, `qemu/timer.h`, `migration/vmstate.h`.
- Put the device state structure in the public header. Declare its QOM type with
  `DECLARE_INSTANCE_CHECKER`; use `TYPE_STM32F1XX_TIMER` consistently.
- Initialize IRQ and the 0x400-byte MMIO region in instance init. Create the
  QEMU virtual timer in realize and validate the clock-frequency property.
- Use `device_class_set_legacy_reset`, `device_class_set_props`, and `dc->vmsd`.
- Virtual deadlines should use `muldiv64`/`muldiv64_round_up`; representing one
  72 MHz tick as an integer number of nanoseconds truncates and fires early.
- The qtest target machine is `stm32f103`. This tree's existing ARM qtests use
  `libqtest-single.h`, `qtest_start`, `clock_step`, `get_irq`, and
  `qtest_irq_intercept_in(global_qtest, "/machine/soc/armv7m")`.
- Do not use `-machine none`: that machine does not instantiate the peripheral.

These are build/API constraints, not peripheral behavior. Hardware behavior
still comes from `reference.md` and the driver.
