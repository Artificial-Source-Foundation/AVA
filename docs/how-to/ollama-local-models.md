---
title: "How-to: Use Local Models With Ollama"
description: "Run AVA against local Ollama models with the current provider defaults, env overrides, and verification steps."
order: 7
updated: "2026-04-18"
---

# How-to: Use Local Models With Ollama

Use this page when you want AVA to run against an Ollama model on your machine or local network.

Scope note: this page is intentionally Ollama-only.

See also: [Providers and auth](../reference/providers-and-auth.md), [Environment variables](../reference/environment-variables.md), [Troubleshooting: Ollama local models](../troubleshooting/ollama-local-models.md)

## 1) Confirm Ollama is available

AVA's Ollama provider defaults to:

1. Provider ID: `ollama`
2. Base URL: `http://localhost:11434`

Before testing AVA, confirm Ollama itself is running and has at least one local model available.

## 2) Run AVA against Ollama explicitly

Start with explicit routing so there is no ambiguity:

```bash
ava --provider ollama --model llama3.1 --headless --no-update-check --max-turns 1 "Reply with: OK"
```

Replace `llama3.1` with a model that exists in your local Ollama instance.

This is the most useful verification path because it exercises a real request, not just local config.

## 3) Understand current Ollama credential behavior

Ollama differs from cloud providers in a few practical ways:

1. Provider creation can succeed with no API key.
2. AVA still supports `AVA_OLLAMA_API_KEY` and `OLLAMA_API_KEY` in the general credential lookup flow.
3. `ollama` can be treated as configured when a `base_url` exists even if `api_key` is empty.

## 4) Override the Ollama endpoint when needed

Current base URL precedence for Ollama is:

1. `credentials.json` provider entry `base_url`
2. `OLLAMA_BASE_URL`
3. default `http://localhost:11434`

Example env override:

```bash
export OLLAMA_BASE_URL="http://192.168.1.50:11434"
ava --provider ollama --model llama3.1 "Summarize this repository"
```

Optional persistent credential entry (`$XDG_CONFIG_HOME/ava/credentials.json`, legacy `~/.ava/credentials.json`):

```json
{
  "providers": {
    "ollama": {
      "api_key": "",
      "base_url": "http://192.168.1.50:11434"
    }
  }
}
```

## 5) Prefer runtime verification over auth-test output

For Ollama, the authoritative check is a real runtime prompt:

1. **Runtime check**: run a real prompt with `--provider ollama --model <your-model>`
   - This proves AVA can actually call Ollama's chat endpoint for that model.
2. **Optional config check**: `ava auth test ollama`
   - Useful only when you have a saved `providers.ollama` credential entry.
   - This does **not** prove endpoint reachability or model availability.
   - It also does **not** follow runtime endpoint resolution through `OLLAMA_BASE_URL`.
   - If you want `ava auth test ollama` to show a non-default endpoint, persist `providers.ollama.base_url` in `$XDG_CONFIG_HOME/ava/credentials.json` (legacy `~/.ava/credentials.json` also works).

## 6) Related troubleshooting

If the runtime check fails, use:

1. [Troubleshooting: Ollama local models](../troubleshooting/ollama-local-models.md)
2. [Troubleshooting: Common errors](../troubleshooting/common-errors.md)
