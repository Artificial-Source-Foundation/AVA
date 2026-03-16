/**
 * LLM Bridge for Tauri
 * Bridges local model resolution with @ava/core-v2 LLM client
 */

import type { LLMProvider } from '../../types/llm'

/** Minimal LLMClient type (replaces @ava/core-v2/llm import) */
export interface LLMClient {
  chat(messages: unknown[], options?: unknown): Promise<unknown>
  [key: string]: unknown
}

// ============================================================================
// Model to Provider Resolution
// ============================================================================

/**
 * Find the native provider for a model ID using prefix matching.
 * Covers all 14 providers' model naming patterns.
 */
export function findProviderForModel(model: string): LLMProvider | null {
  // OpenRouter slash format: "anthropic/claude-sonnet-4.6"
  if (model.includes('/')) {
    // Groq and Together also use slash format (meta-llama/, qwen/, moonshotai/)
    // but those are hosted on those platforms. Check known hosted prefixes first.
    const hosted = model.split('/')[0].toLowerCase()
    if (['meta-llama', 'qwen', 'moonshotai'].includes(hosted)) {
      // Ambiguous — could be Groq or Together. Can't resolve without context.
      return null
    }
    return 'openrouter'
  }

  if (model.startsWith('claude')) return 'anthropic'
  if (
    model.startsWith('gpt') ||
    model.startsWith('o1') ||
    model.startsWith('o3') ||
    model.startsWith('o4') ||
    model.startsWith('codex')
  )
    return 'openai'
  if (model.startsWith('gemini')) return 'google'
  if (model.startsWith('grok')) return 'xai'
  if (
    model.startsWith('mistral') ||
    model.startsWith('magistral') ||
    model.endsWith('stral-latest')
  )
    return 'mistral'
  if (model.startsWith('llama')) return 'groq'
  if (model.startsWith('deepseek')) return 'deepseek'
  if (model.startsWith('command')) return 'cohere'
  if (model.startsWith('glm')) return 'glm'
  if (model.startsWith('moonshot') || model.startsWith('kimi')) return 'kimi'
  if (model.includes(':')) return 'ollama' // ollama uses "model:tag" format

  return null
}

/**
 * Resolve provider for a model, with OpenRouter fallback
 */
export function resolveProvider(model: string): LLMProvider {
  const native = findProviderForModel(model)
  // For now, always use the native provider or default to openrouter
  return native || 'openrouter'
}

// ============================================================================
// Client Factory
// ============================================================================

/**
 * Create LLM client for a model.
 * LLM client creation now happens in Rust via Tauri IPC.
 * This stub returns a placeholder — callers should use invoke() instead.
 */
export function createClient(_model: string): LLMClient {
  return {
    async chat() {
      throw new Error('LLM client creation is now handled by the Rust backend. Use Tauri invoke() instead.')
    },
  }
}

/**
 * Get the provider that will be used for a model
 */
export function getProviderForModel(model: string): LLMProvider {
  return resolveProvider(model)
}
