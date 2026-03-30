/**
 * useModelState Hook
 *
 * Encapsulates model selection, provider resolution, reasoning effort, and
 * delegation toggle logic. Consumed by both the composition shell and ToolbarStrip.
 */

import { type Accessor, createEffect, createMemo } from 'solid-js'
import { useChatMode } from '../../../contexts/chat-mode'
import { updateCoreBudgetLimit } from '../../../services/core-bridge'
import { getModelFromCatalog } from '../../../services/providers/models-dev-catalog'
import { useSession } from '../../../stores/session'
import { useSettings } from '../../../stores/settings'
import type { LLMProvider } from '../../../types/llm'
import { cycleReasoningEffort } from './toolbar-buttons'

// ---------------------------------------------------------------------------
// Return type
// ---------------------------------------------------------------------------

export interface ModelState {
  enabledProviders: Accessor<ReturnType<ReturnType<typeof useSettings>['settings']>['providers']>
  currentModelDisplay: Accessor<string>
  activeProviderId: Accessor<string | null>
  modelSupportsReasoning: Accessor<boolean>
  handleCycleReasoning: () => void
  toggleDelegation: () => void
}

// ---------------------------------------------------------------------------
// Hook
// ---------------------------------------------------------------------------

export function useModelState(): ModelState {
  const chatMode = useChatMode()
  const sessionStore = useSession()
  const { selectedModel, selectedProvider, setSelectedModel } = sessionStore
  const { settings, updateSettings } = useSettings()

  const enabledProviders = createMemo(() =>
    settings().providers.filter((p) => p.enabled && p.models.length > 0)
  )

  // Auto-select a valid model when current selection doesn't match any enabled provider
  createEffect(() => {
    const providers = enabledProviders()
    if (providers.length === 0) return
    const modelId = selectedModel()
    const modelExists = providers.some((p) => p.models.some((m) => m.id === modelId))
    if (!modelExists) {
      const first = providers[0]
      const defaultModel = first.defaultModel || first.models[0]?.id
      if (defaultModel) setSelectedModel(defaultModel)
    }
  })

  // Sync the context budget limit to the selected model's actual context window.
  // This ensures the status bar percentage is calculated against the real context size
  // (e.g. GPT-5.4 = 1M tokens) rather than the default 200K fallback.
  createEffect(() => {
    const modelId = selectedModel()
    const provId = selectedProvider()

    // 1. Try the provider model list first (most specific)
    if (provId) {
      const provider = settings().providers.find((p) => p.id === provId)
      const model = provider?.models.find((m) => m.id === modelId)
      if (model?.contextWindow && model.contextWindow > 0) {
        updateCoreBudgetLimit(model.contextWindow)
        return
      }
    }
    for (const provider of settings().providers) {
      const model = provider.models.find((m) => m.id === modelId)
      if (model?.contextWindow && model.contextWindow > 0) {
        updateCoreBudgetLimit(model.contextWindow)
        return
      }
    }

    // 2. Fall back to the models.dev catalog (raw entry uses limit.context)
    const catalogModel = getModelFromCatalog(modelId, provId as LLMProvider | undefined)
    const catalogContextWindow = catalogModel?.limit?.context
    if (catalogContextWindow && catalogContextWindow > 0) {
      updateCoreBudgetLimit(catalogContextWindow)
    }
  })

  const currentModelDisplay = createMemo(() => {
    // Director mode can override the model display label
    if (chatMode?.modelDisplay) return chatMode.modelDisplay()

    const modelId = selectedModel()
    const provId = selectedProvider()
    if (provId) {
      const provider = settings().providers.find((p) => p.id === provId)
      const model = provider?.models.find((m) => m.id === modelId)
      if (provider && model) return `${provider.name} | ${model.name}`
    }
    for (const provider of settings().providers) {
      const model = provider.models.find((m) => m.id === modelId)
      if (model) return `${provider.name} | ${model.name}`
    }
    if (modelId.length > 30) return `${modelId.slice(0, 27)}...`
    return modelId
  })

  const activeProviderId = createMemo(() => {
    const provId = selectedProvider()
    if (provId) return provId
    const modelId = selectedModel()
    for (const provider of settings().providers) {
      if (provider.models.some((m) => m.id === modelId)) return provider.id
    }
    return null
  })

  const modelSupportsReasoning = createMemo(() => {
    const modelId = selectedModel()
    const provId = activeProviderId()

    // 1. Check capabilities from the provider model list (settings store)
    const checkCaps = (caps?: string[]): boolean =>
      caps?.some((c) => c === 'thinking' || c === 'reasoning') ?? false

    if (provId) {
      const provider = settings().providers.find((p) => p.id === provId)
      const model = provider?.models.find((m) => m.id === modelId)
      if (model?.capabilities?.length && checkCaps(model.capabilities)) return true
    }
    for (const provider of settings().providers) {
      const model = provider.models.find((m) => m.id === modelId)
      if (model?.capabilities?.length && checkCaps(model.capabilities)) return true
    }

    // 2. Check models.dev catalog (authoritative source for capabilities)
    const catalogModel = getModelFromCatalog(modelId, provId as LLMProvider | undefined)
    if (catalogModel?.reasoning === true) return true

    // 3. Infer from model name for unknown models
    return /claude|sonnet|opus|gpt-5|o3-|o4-|codex|gemini|deepseek-r/i.test(modelId)
  })

  const handleCycleReasoning = (): void => {
    const current = settings().generation.reasoningEffort
    const next = cycleReasoningEffort(current, activeProviderId() ?? undefined)
    updateSettings({
      generation: {
        ...settings().generation,
        reasoningEffort: next,
        thinkingEnabled: next !== 'off',
      },
    })
  }

  const toggleDelegation = (): void => {
    updateSettings({
      generation: {
        ...settings().generation,
        delegationEnabled: !settings().generation.delegationEnabled,
      },
    })
  }

  return {
    enabledProviders,
    currentModelDisplay,
    activeProviderId,
    modelSupportsReasoning,
    handleCycleReasoning,
    toggleDelegation,
  }
}
