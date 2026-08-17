# QEMU Peripheral Modelling Agent

QEMU Camp 2026“大模型建模外设”第二阶段项目。Agent 读取硬件资料、驱动代码和
QEMU API 约束，生成 STM32F103 TIM2 模型、qtest 与验证报告，并通过固定 rubric
评分。任务与第一阶段 [QEMU PR #16](https://github.com/shandianchengzi/qemu/pull/16) 对齐。

## 结果

| 产物 | Benchmark | QEMU qtest |
|---|---:|---:|
| 原始 Agent | 77/100，FAIL | 未运行 |
| 人工审校版 | 100/100，PASS | 9/9 PASS |

原始失败项与人工修改均保留在 `results/stm32f103-tim2/`，没有将人工审校成绩
算作纯模型成绩。

## 运行

要求 WSL、Python 3.10+，无第三方 Python 依赖。

```bash
cd qemu-LLM-agent
export DEEPSEEK_API_KEY='your-key-here'
python3 agent.py tasks/stm32f103-tim2 \
  --output runs/stm32f103-tim2/my-run \
  --model deepseek-v4-flash
```

```bash
python3 benchmark.py results/stm32f103-tim2/assisted
python3 -m unittest discover -s tests -v
```

## 目录

- `agent.py`：分阶段生成、路径隔离和失败反馈。
- `benchmark.py`：100 分确定性 Benchmark。
- `tasks/`：任务输入。
- `results/`：原始与人工审校产物、评分和 qtest 日志。

API key、`.env*`、`runs/` 和 `.codex/` 均不会提交。Benchmark 是半量化检查，
最终结果以实际 QEMU qtest 为准。
