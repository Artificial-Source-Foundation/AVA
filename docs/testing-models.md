# SOTA Models for Testing

Updated March 2026.

## OpenAI OAuth (ChatGPT account — `--provider openai`)

These models work with ChatGPT OAuth login (`ava auth login openai`).
Uses the Codex Responses API at `chatgpt.com/backend-api/codex/responses`.

| Model ID | Notes |
|----------|-------|
| `gpt-5.3-codex` | Best agentic coding model (recommended) |
| `gpt-5.3-codex-spark` | Real-time iteration, Pro only |
| `gpt-5.2-codex` | Previous gen, still excellent |
| `gpt-5.1-codex` | Stable |
| `gpt-5.1-codex-max` | Extended limits |
| `gpt-5.1-codex-mini` | Lightweight |
| `gpt-5-codex` | Original Codex |
| `gpt-5-codex-mini` | Lightweight original |
| `gpt-5.2` | General reasoning (also works) |
| `gpt-5.1` | General reasoning |
| `gpt-5` | General reasoning |

**NOT supported** with ChatGPT OAuth: `codex-mini-latest`, `o4-mini`, `gpt-4.1-mini`, `gpt-5-nano`.
These require an API key (`--provider openai` with `AVA_OPENAI_API_KEY` env var).

## OpenRouter (`--provider openrouter`)

| Family | Model ID (OpenRouter) | Notes |
|--------|-----------------------|-------|
| **OpenAI** | `openai/gpt-5.3-codex` | Best agentic coding model |
| | `openai/gpt-5.2` | Frontier reasoning, 400K context |
| | `openai/gpt-5-nano` | Fastest/cheapest GPT-5 |
| | `openai/gpt-4.1-mini` | Cost-efficient, good tool calling |
| | `openai/o4-mini` | Compact reasoning model |
| **Anthropic** | `anthropic/claude-opus-4.6` | Strongest coding/agents, 1M context |
| | `anthropic/claude-sonnet-4.6` | Mid-range, fast, great for agents |
| | `anthropic/claude-sonnet-4.5` | Previous gen, still excellent |
| | `anthropic/claude-haiku-4.5` | Fastest/cheapest Claude |
| **Google** | `google/gemini-3.1-pro-preview` | Frontier reasoning, 1M context |
| | `google/gemini-3-flash-preview` | Fast, near-Pro level |
| | `google/gemini-2.5-flash` | Stable, cost-efficient |
| **DeepSeek** | `deepseek/deepseek-v3.2` | GPT-5 class performance |
| | `deepseek/deepseek-chat-v3.1` | Previous gen, stable |
| **Meta** | `meta-llama/llama-4-maverick` | 400B MoE, 128 experts, 1M context |
| | `meta-llama/llama-4-scout` | 109B MoE, 10M context |
| **Mistral** | `mistralai/mistral-large-2512` | Frontier Mistral |
| | `mistralai/codestral-2508` | Code-specialized |
| | `mistralai/devstral-medium-2507` | Dev-focused |
| | `mistralai/mistral-small-3.2-24b-instruct` | Fast, Apache 2.0 |
| **Qwen** | `qwen/qwen3-coder` | 480B MoE, agentic coding |
| | `qwen/qwen3-max` | Optimized for RAG + tool calling |
| | `qwen/qwen3-coder-flash` | Fast coding model |
| **xAI** | `x-ai/grok-4.1-fast` | Best agentic tool calling, 2M ctx |
| | `x-ai/grok-code-fast-1` | Agentic coding specialist |
| | `x-ai/grok-4-fast` | Multimodal, 2M context |

## Quick test commands

```bash
# OpenAI OAuth (ChatGPT account)
node cli/dist/index.js run "Read package.json and tell me the version" --provider openai --model gpt-5.3-codex --yolo --verbose

# OpenRouter (API key)
node cli/dist/index.js run "Read README.md and tell me the project name" --provider openrouter --model "<model-id>" --max-turns 3 --verbose
```
