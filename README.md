# QEMU Peripheral Modelling Agent

这是 QEMU Camp 2026“大模型建模外设”第二阶段的可复现实验仓库。它提供一个
无第三方 Python 依赖的 Agent：读取硬件摘要、驱动访问模式和 QEMU API 约束，
调用 OpenAI-compatible 模型生成外设模型、qtest、manifest 与报告，再用固定 rubric
给出量化结果并记录失败项。

本仓库的闭环任务是 STM32F103 TIM2，与第一阶段
[QEMU PR #16](https://github.com/shandianchengzi/qemu/pull/16) 对齐。

## 结果

| 产物 | Benchmark | 真实 qtest | 说明 |
|---|---:|---:|---|
| `raw-agent` | 77/100，FAIL | 未运行 | DeepSeek V4 Flash 分阶段生成后的原始结果 |
| `assisted` | 100/100，PASS | 9/9 PASS | 保留 Agent 文档，使用第一阶段已审校实现修复代码与测试 |

原始结果没有被隐藏或包装成成功：它仍错误实现 SR 的 rc_w0，未按 CCMR1/2
筛选 output compare，并且缺少严格边界测试。人工修改及原因记录在
[`results/stm32f103-tim2/assisted/HUMAN_EDITS.md`](results/stm32f103-tim2/assisted/HUMAN_EDITS.md)。

## 工作流

1. 按文件名稳定排序读取 `tasks/<task>/` 中的资料并计算 SHA-256。
2. 生成结构化计划 `plan.json`。
3. 默认采用 staged 策略，依次生成 model/header、qtest、manifest/report；避免一次
   输出五个大文件造成退化。
4. 只允许模型写入五个白名单路径，拒绝绝对路径、`..` 和额外文件。
5. 运行隐藏于任务输入之外的 100 分 rubric；结果写入 Markdown 和 JSON。
6. 可使用失败项做定向修复；monolithic 策略会保留最高分候选，防止修订退化。

## WSL 运行

要求 Python 3.10+。代码只使用标准库，不需要 `pip install`。

```bash
cd /mnt/d/qemu/project/agent-STM32F103-qemu/qemu-LLM-agent
export DEEPSEEK_API_KEY='your-key-here'

python3 agent.py tasks/stm32f103-tim2 \
  --output runs/stm32f103-tim2/my-run \
  --model deepseek-v4-flash \
  --strategy staged
```

单独复评已有候选：

```bash
python3 benchmark.py results/stm32f103-tim2/raw-agent \
  --output /tmp/raw-benchmark.md
```

运行 Agent 自身测试：

```bash
python3 -m unittest discover -s tests -v
```

真实 QEMU qtest（第一阶段 QEMU 位于相邻目录时）：

```bash
cd ../qemu
ninja -C build-stm32 qemu-system-arm tests/qtest/stm32f103-timer-test
QTEST_QEMU_BINARY="$(pwd)/build-stm32/qemu-system-arm" \
  build-stm32/tests/qtest/stm32f103-timer-test
```

## 目录

- `agent.py`：API 调用、分阶段生成、路径隔离、响应复用和失败反馈。
- `benchmark.py`：确定性半量化评分器。
- `tasks/stm32f103-tim2/`：硬件摘要、驱动样例与 QEMU API 约束。
- `benchmarks/stm32f103-tim2/rubric.json`：19 项、总分 100 的 rubric。
- `results/stm32f103-tim2/raw-agent/`：原始模型产物和失败记录。
- `results/stm32f103-tim2/assisted/`：明确标注人工介入的通过版本。

## 安全与限制

- API key 只从 `DEEPSEEK_API_KEY` 读取，不写入请求记录、结果或 Git。
- `.env*`、`runs/`、`.codex/` 和 Python cache 均被忽略。
- 当前 Benchmark 是静态/半量化检查；最终可信度来自 QEMU 编译与 qtest，不能只看分数。
- 任务输入是 RM0008 的精简转述。完整硬件事实以
  [ST RM0008](https://www.st.com/resource/en/reference_manual/cd00171190-stm32f101-103-105-107-stm32f100-series-armbased-32bit-mcus-stmicroelectronics.pdf)
  为准。

