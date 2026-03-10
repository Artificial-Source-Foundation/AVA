# Sprint 55: Coding Plan Providers

## Goal

Add support for subscription-based "coding plan" providers that offer free/reduced pricing for coding workloads. These are all OpenAI-compatible or Anthropic-compatible APIs with dedicated endpoints.

## Providers to Add

| Provider ID | Display Name | Base URL | Env Var | SDK Compat | Key Models |
|---|---|---|---|---|---|
| `alibaba` | Alibaba Model Studio | `https://dashscope-intl.aliyuncs.com/compatible-mode/v1` | `DASHSCOPE_API_KEY` | OpenAI | Qwen3 Coder, DeepSeek R1 |
| `alibaba-cn` | Alibaba (China) | `https://dashscope.aliyuncs.com/compatible-mode/v1` | `DASHSCOPE_API_KEY` | OpenAI | Qwen3 Coder, Kimi K2.5 |
| `zai-coding-plan` | Z.AI Coding Plan | `https://api.z.ai/api/coding/paas/v4` | `ZHIPU_API_KEY` | OpenAI | GLM-4.7, GLM-4.5 |
| `zhipuai-coding-plan` | ZhipuAI Coding Plan | `https://open.bigmodel.cn/api/coding/paas/v4` | `ZHIPU_API_KEY` | OpenAI | GLM-4.7, GLM-4.5 |
| `kimi-for-coding` | Kimi For Coding | `https://api.kimi.com/coding/v1` | `KIMI_API_KEY` | Anthropic | K2.5, K2 Thinking |
| `minimax-coding-plan` | MiniMax Coding Plan | `https://api.minimax.io/anthropic/v1` | `MINIMAX_API_KEY` | Anthropic | M2, M2.1 |
| `minimax-cn-coding-plan` | MiniMax CN Coding Plan | `https://api.minimaxi.com/anthropic/v1` | `MINIMAX_API_KEY` | Anthropic | M2, M2.1 |

## Key Technical Notes

- **Alibaba/DashScope**: OpenAI-compatible. Needs `enable_thinking: true` in request body for reasoning models (qwen3, qwq, deepseek-r1, kimi-k2.5). Exception: `kimi-k2-thinking` returns reasoning_content by default.
- **ZAI/ZhipuAI**: OpenAI-compatible. Needs `thinking: { type: "enabled", clear_thinking: false }` for reasoning models (GLM-4.5+).
- **Kimi For Coding**: Uses **Anthropic-compatible** API (not OpenAI). Needs `thinking: { type: "enabled", budgetTokens: N }` for K2.5.
- **MiniMax Coding Plan**: Uses **Anthropic-compatible** API. No special thinking config needed.
- All coding plan models are **$0 input/$0 output** — free tier with subscription.

## Prompts

1. `01-coding-plan-providers.md` — Full implementation prompt

## Dependencies

- Sprint 53 (model catalog) — for fallback catalog entries
- Sprint 54 (thinking support) — for provider-specific thinking config

## Status: Complete
