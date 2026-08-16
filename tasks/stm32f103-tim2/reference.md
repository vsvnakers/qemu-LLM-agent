# Curated hardware notes

Source: STMicroelectronics RM0008, STM32F10xxx reference manual, general-purpose
timers TIM2–TIM5. This is a task-focused transcription, not a replacement for
the manual.

## Integration facts

- TIM2 base address: `0x40000000`; register block allocation: `0x400` bytes.
- TIM2 global interrupt number: 28.
- Benchmark clock assumption: fixed 72 MHz timer input. RCC clock-tree and APB
  multiplier behavior are outside this task.
- Registers are 16-bit in effect and are conventionally accessed through
  32-bit aligned MMIO by firmware.

## Register offsets and reset values

| Register | Offset | Reset | Relevant meaning |
|---|---:|---:|---|
| CR1 | 0x00 | 0 | CEN bit 0, UDIS bit 1, URS bit 2, OPM bit 3, ARPE bit 7 |
| CR2 | 0x04 | 0 | stored only in this task |
| SMCR | 0x08 | 0 | stored only in this task |
| DIER | 0x0c | 0 | UIE bit 0; CC1IE..CC4IE bits 1..4 |
| SR | 0x10 | 0 | UIF bit 0; CC1IF..CC4IF bits 1..4; flags are rc_w0 |
| EGR | 0x14 | 0 | write-only events: UG bit 0; CC1G..CC4G bits 1..4 |
| CCMR1 | 0x18 | 0 | CC1S bits 1:0, CC2S bits 9:8; zero means output |
| CCMR2 | 0x1c | 0 | same layout for channels 3 and 4 |
| CCER | 0x20 | 0 | stored only in this task |
| CNT | 0x24 | 0 | current 16-bit counter |
| PSC | 0x28 | 0 | prescaler preload; divide by PSC + 1 |
| ARR | 0x2c | 0xffff | auto-reload preload |
| CCR1..4 | 0x34..0x40 | 0 | compare values, stride 4 |
| DCR | 0x48 | 0 | stored only in this task |
| DMAR | 0x4c | 0 | stored only in this task |

## Required behavior

Counting starts with CEN and pauses when CEN is cleared. One counter tick takes
`(PSC_active + 1) / 72 MHz`. Overflow occurs after ARR_active + 1 ticks, resets
the visible count to zero, creates an update event, and sets UIF unless updates
are disabled. The next deadline must round up so an interrupt is never observed
before its hardware tick.

PSC is always buffered. A UG or counter overflow transfers PSC preload to the
active divider. ARR writes are immediate when ARPE is zero; with ARPE set, ARR
is transferred on update. UG also resets CNT. UDIS suppresses update-event
effects including preload transfer. URS prevents software UG from setting UIF,
but does not block the preload transfer. OPM clears CEN after the next update.

An output-compare match on an output-configured channel sets its CCxIF flag.
Matches repeat once per counter period. CCR values greater than active ARR do
not match. A CCxG write sets the corresponding CCxIF flag.

The single interrupt output is level-sensitive and asserted when any supported
status flag is also enabled in DIER. Clearing the relevant SR flag by writing
zero must immediately lower the line when no other enabled flag remains.

Migration must preserve enough state to resume the count and next event without
using stale host time. Reset must delete a pending timer and lower the IRQ.

Official source URL:
https://www.st.com/resource/en/reference_manual/cd00171190-stm32f101-103-105-107-stm32f100-series-armbased-32bit-mcus-stmicroelectronics.pdf

