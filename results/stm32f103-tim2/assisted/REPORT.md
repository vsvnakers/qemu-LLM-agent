# Report: STM32F103 TIM2 QEMU Model

## Sources

- STMicroelectronics RM0008, STM32F10xxx reference manual, general-purpose timers TIM2–TIM5 (task-focused transcription in `reference.md`).
- Driver access pattern in `driver.c`.
- QEMU tree conventions in `qemu-api-notes.md`.
- Task specification in `task.md`.

## Assumptions

- The target QEMU machine already instantiates TIM2 at `0x40000000` with a fixed 72 MHz input clock and connects its interrupt output to NVIC IRQ 28.
- The timer input clock is fixed at 72 MHz; no RCC clock tree or APB multiplier is modeled.
- Registers are 16-bit in effect but are accessed through 32-bit aligned MMIO; 32-bit accesses are split into two 16-bit halves.
- All four channels are assumed to be output-configured; CCMR1/CCMR2 channel mode bits are stored but not used to alter behavior.
- CCER is stored but not used to enable/disable channels; output compare behavior is always active for all channels.
- The interrupt output is level-sensitive and asserted when any enabled status flag is set.
- PSC is always buffered; ARR writes are immediate when ARPE is zero, otherwise transferred on update.
- UDIS suppresses update-event effects including preload transfer and UIF setting.
- URS prevents software UG from setting UIF but does not block preload transfer.
- OPM clears CEN after the next update event.
- CCR values greater than active ARR do not match.
- Unsupported accesses are logged via `qemu_log_mask(GUEST_ERROR)` and are otherwise ignored.

## Validation

- The generated C source and qtest code have **not yet been compiled or run**.
- The qtest suite is designed to prove timing boundaries and interrupt clearing, not just register storage.
- Planned qtest scenarios include:
  - Reset state: all registers at reset values, IRQ low.
  - Counter starts when CEN is set and pauses when cleared.
  - Overflow timing: with PSC=71 and ARR=999, overflow occurs after 1 ms (72,000,000 / 72 = 1,000,000 ticks/s; 1000 ticks = 1 ms).
  - Compare match timing: with CCR2=250, CC2IF is set at 250 us.
  - Interrupt assertion and clearing: writing zero to SR clears flags and lowers IRQ.
  - UG behavior: resets CNT, sets UIF unless URS is set, transfers preloads.
  - ARPE behavior: ARR write is immediate when ARPE=0; buffered when ARPE=1.
  - OPM behavior: CEN cleared after update.
  - UDIS behavior: no update event effects when set.
  - CCxG behavior: sets corresponding CCxIF.
  - Unsupported access logging.

## Limitations

- Input capture, PWM output, slave/external clock modes, DMA, down/center-aligned counting, and RCC clock gating are not modeled.
- Only TIM2 is implemented; other timers are out of scope.
- Clock frequency is fixed at 72 MHz; no RCC clock tree or APB multiplier.
- CCMR1/CCMR2 channel mode bits are stored but not used to alter behavior; channels are assumed output-configured.
- CCER is stored but not used to enable/disable channels.
- DCR and DMAR are stored only; no DMA functionality.
- The generated C and qtest code have not yet been compiled or run.
