# STM32F103 TIM2 experiment

- `raw-agent/`：分阶段 DeepSeek V4 Flash 原始产物。最终 rubric v2 为
  77/100，低于 80 分门槛；失败项保留在 `BENCHMARK.md`。
- `assisted/`：人工审校产物。同一 rubric 为 100/100，真实 QEMU qtest
  9/9 通过。

两组产物使用同一任务输入和评分器。`assisted` 不是纯模型成绩，人工改动见
`assisted/HUMAN_EDITS.md`。

