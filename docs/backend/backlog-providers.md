# Provider Status Backlog

> Tracking which LLM providers work, which need testing, and what's missing.
>
> **14 providers** implemented. All have unit tests (shared harness). Real-world E2E status varies.

---

## Architecture Overview

**Two implementation patterns:**

1. **Custom `client.ts`** (4 providers) — Anthropic, OpenAI, OpenRouter, Copilot
2. **`createOpenAICompatClient()` factory** (10 providers) — Everything else

All providers share: SSE streaming, tool call buffering, message conversion, error classification, abort signal support.

---

## Provider Status

### Verified Working

| Provider | Type | Default Model | Auth | E2E Status | Notes |
|----------|------|---------------|------|------------|-------|
| **OpenAI** | Custom | `gpt-4o` | API key + OAuth | **Working** | Dual-path: API key uses Chat Completions, OAuth uses Codex/Responses API. Device auth flow for OAuth. |
| **OpenRouter** | Custom | (per-model) | API key | **Working** | Tested with `anthropic/claude-sonnet-4`. Custom headers (`HTTP-Referer`, `X-Title`). Default temp 0.7, max_tokens 4096. |

### Likely Working (OpenAI-compat, untested E2E)

These all use the shared `createOpenAICompatClient()` factory. If OpenAI works, these should too — just need an API key.

| Provider | Default Model | Base URL | Auth Env | Free Tier? | Priority to Test |
|----------|---------------|----------|----------|------------|-----------------|
| **DeepSeek** | `deepseek-chat` | `api.deepseek.com/v1` | `AVA_DEEPSEEK_API_KEY` | No (cheap) | HIGH — very popular for coding |
| **Groq** | `llama-3.3-70b-versatile` | `api.groq.com/openai/v1` | `AVA_GROQ_API_KEY` | Yes | HIGH — free tier, fast inference |
| **Mistral** | `mistral-large-latest` | `api.mistral.ai/v1` | `AVA_MISTRAL_API_KEY` | Yes (limited) | MEDIUM |
| **Together** | `meta-llama/Meta-Llama-3.1-70B-Instruct-Turbo` | `api.together.xyz/v1` | `AVA_TOGETHER_API_KEY` | Yes (limited) | MEDIUM |
| **xAI** | `grok-2` | `api.x.ai/v1` | `AVA_XAI_API_KEY` | No | LOW |
| **Cohere** | `command-r-plus` | `api.cohere.com/v2` | `AVA_COHERE_API_KEY` | Yes (limited) | LOW |
| **GLM (Zhipu)** | `glm-4-flash` | `open.bigmodel.cn/api/paas/v4` | `AVA_GLM_API_KEY` | Unknown | LOW — China-focused |
| **Kimi (Moonshot)** | `moonshot-v1-8k` | `api.moonshot.cn/v1` | `AVA_KIMI_API_KEY` | Unknown | LOW — China-focused |

### Needs Real-World Testing

| Provider | Type | Default Model | Auth | Concern |
|----------|------|---------------|------|---------|
| **Anthropic** | Custom | `claude-3-opus` | API key + OAuth | Has extended thinking support, custom SSE parser. Likely works but untested E2E with agent-v2. |
| **Google Gemini** | OpenAI-compat | `gemini-2.0-flash` | API key | Uses custom endpoint `/openai/chat/completions`. May have quirks with tool calling format. |
| **Ollama** | OpenAI-compat | `llama3.2` | None (local) | Local server at `localhost:11434`. No auth needed. Depends on user having Ollama installed + model pulled. Tool calling support varies by model. |
| **GitHub Copilot** | Custom | `gpt-4o` | OAuth device flow | Strips `copilot-` model prefix. Uses `Copilot-Integration-Id: vscode-chat` header. OAuth flow untested. |

---

## Not Yet Implemented

| Provider | Priority | Effort | Notes |
|----------|----------|--------|-------|
| **AWS Bedrock** | MEDIUM | ~1 session | SigV4 auth, regional endpoints. Goose + OpenCode both have it. Would need custom client. |
| **Azure OpenAI** | MEDIUM | ~0.5 session | Custom auth + deployment IDs. Basically OpenAI-compat with different URL structure. |
| **Fireworks AI** | LOW | Quick | OpenAI-compat. Fast inference, popular for open-source models. |
| **Perplexity** | LOW | Quick | OpenAI-compat. Good for search-augmented generation. |
| **Cerebras** | LOW | Quick | OpenAI-compat. Extremely fast inference. |

---

## Credential Setup

All providers follow the pattern: `AVA_<PROVIDER>_API_KEY` env var → `ava:<provider>:api_key` in `~/.ava/credentials.json`.

```bash
# Example: set up DeepSeek
export AVA_DEEPSEEK_API_KEY="sk-..."

# Or store persistently
echo '{"ava:deepseek:api_key": "sk-..."}' >> ~/.ava/credentials.json
```

**Special cases:**
- **Ollama**: No auth. Just run `ollama serve` locally.
- **Copilot**: OAuth device flow (no API key). Token stored as `ava:copilot:oauth_token`.
- **OpenAI OAuth**: Device auth flow, stores token + accountId.
- **Anthropic OAuth**: Bearer token with `anthropic-beta` header.

---

## Known Issues / TODOs

- [ ] **Default model for Anthropic**: Should probably be `claude-sonnet-4` not `claude-3-opus` (verify)
- [ ] **Groq tool calling**: Some Groq models don't support tool use — need model-level capability flags
- [ ] **Ollama tool calling**: Only some models support tools (e.g., `llama3.2` does, `codellama` doesn't) — needs model capability detection
- [ ] **Google Gemini**: Their OpenAI-compat layer has known gaps with streaming tool calls — may need testing
- [ ] **Copilot OAuth flow**: Never tested end-to-end. Device flow may need UI for the verification code.
- [ ] **Bedrock + Azure**: The two most requested missing providers (both competitors have them)
- [ ] **Provider health checks**: Model availability extension exists but not wired to provider selection UI
- [ ] **Rate limit handling**: Shared error classifier detects 429s but retry logic is per-provider — should unify
- [ ] **Cost tracking**: No per-provider cost estimation yet (would need token pricing data)

---

## Testing Checklist

To verify a provider works E2E:

```bash
# 1. Set credential
export AVA_<PROVIDER>_API_KEY="..."

# 2. Run agent-v2 with provider
node cli/dist/index.js agent-v2 run "Say hello" \
  --provider <name> \
  --model <model-id> \
  --yolo

# 3. Run with tool use
node cli/dist/index.js agent-v2 run "List files in the current directory" \
  --provider <name> \
  --model <model-id> \
  --yolo

# 4. Check: streaming works, tool calls parse, response completes
```

**Unit tests (all pass):**
```bash
npx vitest run packages/extensions/providers/
```
