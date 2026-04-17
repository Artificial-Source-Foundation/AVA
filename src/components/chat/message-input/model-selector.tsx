/**
 * Model Selector Button
 *
 * Pill-shaped button that opens the Model Browser dialog.
 * Shows the actual provider logo + model name.
 */

import { type Accessor, type Component, createMemo } from 'solid-js'
import { useSession } from '../../../stores/session'
import { useSettings } from '../../../stores/settings'
import { ProviderLogo } from '../../icons/ProviderLogo'

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface ModelSelectorProps {
  onToggle: () => void
  currentModelDisplay: Accessor<string>
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const ModelSelector: Component<ModelSelectorProps> = (props) => {
  const { selectedProvider, selectedModel } = useSession()
  const { settings } = useSettings()

  const providerId = createMemo(() => {
    // 1. Check session-level selected provider
    const sessionProv = selectedProvider()
    if (sessionProv) return sessionProv

    // 2. Infer from model ID by checking provider model lists
    const modelId = selectedModel()
    if (modelId) {
      for (const prov of settings().providers) {
        if (prov.models.some((m) => m.id === modelId)) return prov.id
      }
      // 3. Infer from model name patterns
      if (/gpt|chatgpt/i.test(modelId)) return 'openai'
      if (/claude|sonnet|opus|haiku/i.test(modelId)) return 'anthropic'
      if (/gemini/i.test(modelId)) return 'google'
      if (/llama|qwen/i.test(modelId)) return 'ollama'
    }

    return 'anthropic'
  })

  return (
    <button
      type="button"
      onClick={() => props.onToggle()}
      class="
        flex items-center
        bg-[var(--alpha-white-5)]
        rounded-[6px]
        hover:bg-[var(--alpha-white-8)]
        transition-colors
      "
      aria-label="Open model selector"
      style={{
        gap: '4px',
        padding: '4px 8px',
      }}
    >
      <ProviderLogo providerId={providerId()} class="w-[11px] h-[11px] shrink-0" />
      <span
        class="truncate max-w-[160px]"
        style={{
          'font-size': '10px',
          'font-weight': '500',
          color: 'var(--text-tertiary)',
          'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
        }}
      >
        {props.currentModelDisplay()}
      </span>
    </button>
  )
}
