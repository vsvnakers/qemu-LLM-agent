# Benchmark result

- Benchmark: `stm32f103-tim2-agent` v2
- Score: **100/100 (PASS)**
- Pass threshold: 80

## Categories

| Category | Score |
|---|---:|
| QEMU structure | 13/13 |
| Registers | 16/16 |
| Timing behavior | 12/12 |
| Update control | 16/16 |
| Interrupts | 11/11 |
| Output compare | 7/7 |
| Validation | 19/19 |
| Traceability | 6/6 |

## Checks

- [PASS] complete-device (7/7): complete SysBus/QOM device with MMIO and a QEMU virtual timer — ok
- [PASS] register-map (6/6): all task register offsets are decoded — ok
- [PASS] mmio-safety (4/4): little-endian constrained accesses and guest-error logging — ok
- [PASS] reset (6/6): reset restores ARR=0xffff, deletes pending timer and lowers IRQ — ok
- [PASS] counter-time (8/8): counter derives elapsed ticks from virtual time with PSC+1 and ARR+1 — ok
- [PASS] round-up (4/4): deadline conversion rounds up without integer tick truncation — ok
- [PASS] cen-udis (5/5): CEN pauses/resumes and UDIS suppresses update effects — ok
- [PASS] urs (4/4): URS suppresses UIF for software UG but not overflow — ok
- [PASS] opm-arpe-preload (7/7): OPM, ARR preload and PSC preload have active shadow state — ok
- [PASS] irq-gating (6/6): level IRQ is the intersection of SR and DIER flags — ok
- [PASS] rc-w0 (5/5): SR implements rc_w0 and immediately recomputes IRQ — ok
- [PASS] compare (7/7): four output channels use the correct CCMR half and repeat per period — ok
- [PASS] migration (6/6): migration latches count then restores IRQ and deadline — ok
- [PASS] qtest-machine (4/4): qtest instantiates stm32f103 and intercepts the NVIC input — ok
- [PASS] qtest-timing (6/6): qtest covers reset and both sides of a timing boundary — ok
- [PASS] qtest-irq-compare (5/5): qtest covers IRQ assertion/clearing and output compare — ok
- [PASS] qtest-controls (4/4): qtest covers prescaler preload, ARPE and one-pulse — ok
- [PASS] manifest (3/3): machine-readable facts and scope follow the task schema — ok
- [PASS] report (3/3): report records sources, assumptions, validation and limitations — ok
