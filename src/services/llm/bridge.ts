/**
 * LLM Bridge for Tauri
 * Model-to-provider resolution for the frontend.
 */

import { defaultProviders } from '../../config/defaults/provider-defaults'
import { settings } from '../../stores/settings/settings-signal'
import type { LLMProvider } from '../../types/llm'

// ============================================================================
// Model to Provider Resolution
// ============================================================================

/**
 * Find the native provider for a model ID.
 * First checks provider model lists from settings (or defaults),
 * then falls back to prefix matching for unrecognized models.
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

  // ── Look up in provider model lists (settings first, then defaults) ──
  const providers = settings()?.providers ?? defaultProviders
  for (const provider of providers) {
    if (provider.models.some((m) => m.id === model)) {
      return provider.id as LLMProvider
    }
  }

  // ── Fallback: prefix matching for models not in any provider list ──
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
  if (model.startsWith('deepseek')) return 'deepseek'
  if (model.startsWith('command')) return 'cohere'
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

/**
 * Get the provider that will be used for a model
 */
export function getProviderForModel(model: string): LLMProvider {
  return resolveProvider(model)
}
