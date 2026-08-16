# Human edits and provenance

## Baseline

The staged DeepSeek V4 Flash candidate scored 77/100 with rubric v2. It passed
register mapping, reset, virtual-time structure, update controls, migration and
traceability, but failed these semantic checks:

- SR used `flags &= ~written` instead of STM32 rc_w0 `flags &= written`.
- Compare scheduling did not use CCMR1/CCMR2 CCxS to reject input channels.
- The qtest cleared flags by writing one and lacked the `deadline - 1 ns`
  boundary proof.

## Manual intervention

The generated device source, public header and qtest were replaced with the
reviewed stage-one TIM2 implementation from commit
`75475e550fd6488d2a803d5f6b85d9f347e18c93`. The Agent-generated manifest and
report were retained. This is intentionally classified as an **assisted**
result, not a raw Agent result.

## Verification

- Rubric v2: 100/100 (PASS).
- QEMU source: adjacent stage-one tree, branch `stm32f103-timer-f1`.
- QEMU version: 11.0.50 development tree.
- Test binary: `tests/qtest/stm32f103-timer-test`.
- Result: 9/9 PASS on 2026-08-17 in WSL.

See `QTEST.log` for the exact TAP result.

