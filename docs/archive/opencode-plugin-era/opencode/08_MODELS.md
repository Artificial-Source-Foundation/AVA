# OpenCode Model Configuration

> Configure LLM providers and models for OpenCode.

---

## Supported Providers

OpenCode integrates with **75+ LLM providers** via the AI SDK and Models.dev. Most popular providers are preloaded by default.

### Major Providers

| Provider | Models |
|----------|--------|
| **Anthropic** | Claude Opus 4.5, Sonnet 4, Haiku 4.5 |
| **OpenAI** | GPT 5.2, GPT 5.1 Codex |
| **Google** | Gemini 3 Pro, Gemini 3 Flash |
| **DeepSeek** | DeepSeek v3 |
| **Minimax** | M2.1 |

---

## Model Format

Models use `provider_id/model_id` format:

```
anthropic/claude-opus-4-5
anthropic/claude-sonnet-4
anthropic/claude-haiku-4-5
openai/gpt-5.2-codex
openai/gpt-5.1-codex
google/gemini-3-pro
google/gemini-3-flash
deepseek/deepseek-v3
```

---

## Configuration

### Default Model

```json
{
  "model": "anthropic/claude-sonnet-4-5"
}
```

### Small Model (for lightweight tasks)

```json
{
  "small_model": "anthropic/claude-haiku-4-5"
}
```

### Per-Agent Override

```json
{
  "agent": {
    "commander": {
      "model": "anthropic/claude-opus-4-5"
    },
    "operator": {
      "model": "anthropic/claude-sonnet-4"
    },
    "validator": {
      "model": "anthropic/claude-haiku-4-5"
    }
  }
}
```

---

## Model Selection Priority

1. Command-line flag (`--model`)
2. Config file (`opencode.json`)
3. Last used model (persisted)
4. System default

---

## Connecting Providers

Use the `/connect` command in OpenCode:

```
/connect anthropic
/connect openai
/connect google
```

Credentials are stored securely and auto-loaded on startup.

---

## Model Variants

Some providers support reasoning/thinking variants:

### Anthropic

| Variant | Description |
|---------|-------------|
| `high` | Default reasoning level |
| `max` | Maximum reasoning |

### OpenAI

| Variant | Description |
|---------|-------------|
| `none` | No extended thinking |
| `minimal` | Minimal reasoning |
| `low` | Low reasoning |
| `medium` | Medium reasoning |
| `high` | High reasoning |
| `xhigh` | Maximum reasoning |

### Google

| Variant | Description |
|---------|-------------|
| `low` | Low thinking budget |
| `high` | High thinking budget |

---

## Custom Providers

Add custom providers (like local LLMs):

```json
{
  "provider": {
    "lmstudio": {
      "baseUrl": "http://localhost:1234/v1",
      "models": ["google/gemma-3n-e4b"]
    }
  }
}
```

Use as: `lmstudio/google/gemma-3n-e4b`

---

## Provider Settings

```json
{
  "provider": {
    "timeout": 30000,
    "cache": true
  }
}
```

---

## Recommended Models for Delta9

| Agent | Recommended Model | Rationale |
|-------|-------------------|-----------|
| **Commander** | `anthropic/claude-opus-4-5` | Deep reasoning for planning |
| **Oracle-Claude** | `anthropic/claude-opus-4-5` | Architecture expertise |
| **Oracle-GPT** | `openai/gpt-5.2-codex` | Code patterns |
| **Oracle-Gemini** | `google/gemini-3-pro` | UI/UX creativity |
| **Oracle-DeepSeek** | `deepseek/deepseek-v3` | Performance focus |
| **Operator** | `anthropic/claude-sonnet-4` | Fast, capable execution |
| **Validator** | `anthropic/claude-haiku-4-5` | Quick verification |
| **Patcher** | `anthropic/claude-haiku-4-5` | Small fixes |
| **Scout** | `anthropic/claude-haiku-4-5` | Fast codebase search |

---

## Viewing Available Models

Use the `/models` command in OpenCode to see all configured providers and their available models.

---

## Model Capabilities

Not all models are equally good at:
- Code generation
- Tool calling
- Long context
- Reasoning

The documentation notes: "only a few of them are good at both generating code and tool calling."

### Recommended for Code + Tools

- Claude Opus 4.5
- Claude Sonnet 4
- GPT 5.2 Codex
- Gemini 3 Pro

---

## Reference

- [Official Models Docs](https://opencode.ai/docs/models/)
- [Models.dev](https://models.dev/) - Model catalog
