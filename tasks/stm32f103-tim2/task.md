# Task: STM32F103 TIM2 model

Create a minimal QEMU SysBus model for the STM32F103 general-purpose TIM2
peripheral and libqtest coverage. The target QEMU machine already wires the
device at `0x40000000`, uses a fixed 72 MHz input clock, and connects its one
interrupt output to NVIC IRQ 28.

The useful scope is a 16-bit upcounter, update events, and four output-compare
channels. Input capture, PWM pin output, slave/external-clock modes, DMA,
down/center-aligned counting, and RCC clock gating are explicitly out of scope.

Required artifacts and constraints:

- A complete `SysBusDevice` C source and header using virtual time, not a host
  thread or wall clock.
- Little-endian MMIO for the documented register window; unsupported accesses
  must be safe and observable through QEMU guest-error logging.
- Reset state, interrupt level, pending virtual timer, and all guest-visible
  register/preload state must be deterministic.
- A qtest suite must prove timing boundaries and interrupt clearing, not just
  read/write register storage.
- A machine-readable manifest and a report must record facts, choices,
  limitations, and what was or was not actually validated.
- The manifest schema uses top-level `peripheral`, `base_address`, `irq`,
  `clock_hz`, `registers`, `behaviors`, and `limitations` fields.

The implementation should follow normal QEMU conventions and remain small.

