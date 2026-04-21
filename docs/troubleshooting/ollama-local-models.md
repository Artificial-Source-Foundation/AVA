---
title: "Troubleshooting: Ollama Local Models"
description: "Focused fixes for Ollama endpoint/model failures when running AVA with provider=ollama."
order: 3
updated: "2026-04-18"
---

# Troubleshooting: Ollama Local Models

Use this page when AVA is routed to `--provider ollama` and local-model runs fail.

See also: [How-to: Use local models with Ollama](../how-to/ollama-local-models.md), [Providers and auth](../reference/providers-and-auth.md), [Environment variables](../reference/environment-variables.md)

## 1) AVA cannot reach Ollama endpoint

**Symptoms**

1. Run fails quickly with a connection/network error.
2. You are targeting default `http://localhost:11434`, but Ollama is not listening there.

**Fix**

1. Confirm Ollama is running.
2. If Ollama is remote or bound to another host/port, set `OLLAMA_BASE_URL`.
3. Re-run with explicit routing:

```bash
export OLLAMA_BASE_URL="http://192.168.1.50:11434"
ava --provider ollama --model llama3.1 --headless --no-update-check --max-turns 1 "Reply with OK"
```

## 2) Model name is wrong or missing in Ollama

**Symptoms**

1. Endpoint is reachable, but generation fails for the chosen model.
2. You passed a model name that is not present in local Ollama.

**Fix**

1. Check installed models in Ollama.
2. Use an exact installed model name in `--model`.
3. Re-run with explicit provider + model flags.

```bash
ava --provider ollama --model <installed-model-name> "Summarize this repository"
```

## 3) `ava auth test ollama` is not the same as runtime verification

**Why this happens**

`ava auth test ollama` is a configuration check, not a network/model execution check.

For `ollama`, it only uses saved credential data and does not call `/api/chat`.

Important caveats:

1. A real Ollama runtime can still work without any saved Ollama credential entry.
2. `ava auth test ollama` does not consult `OLLAMA_BASE_URL`.
3. Runtime provider creation uses saved `base_url`, then `OLLAMA_BASE_URL`, then `http://localhost:11434`.

**Fix**

Use a runtime check instead:

```bash
ava --provider ollama --model llama3.1 --headless --no-update-check --max-turns 1 "Reply with OK"
```

## 4) Unexpected endpoint used after setting `OLLAMA_BASE_URL`

**Why this happens**

If `~/.ava/credentials.json` has `providers.ollama.base_url`, that saved value takes precedence over `OLLAMA_BASE_URL` in provider creation.

**Fix**

1. Update/remove the saved Ollama `base_url` entry in `~/.ava/credentials.json`, or
2. Keep the saved value aligned with the endpoint you expect.
