/**
 * Model Selector Dialog
 *
 * Dialog for selecting AI models with provider grouping and details.
 */

import { Bot, Check, ChevronRight, Cpu, Search, Sparkles, Zap } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { Dialog } from '../ui/Dialog'

// ============================================================================
// Types
// ============================================================================

export interface ModelInfo {
  id: string
  name: string
  provider: 'anthropic' | 'openai' | 'openrouter' | 'ollama'
  contextWindow: number
  description?: string
  capabilities?: string[]
  isNew?: boolean
  isPremium?: boolean
}

export interface ModelSelectorDialogProps {
  /** Whether dialog is open */
  open: boolean
  /** Called when open state changes */
  onOpenChange: (open: boolean) => void
  /** Currently selected model ID */
  selectedId?: string
  /** Called when model is selected */
  onSelect: (model: ModelInfo) => void
  /** Available models */
  models: ModelInfo[]
}

// ============================================================================
// Provider Config
// ============================================================================

const providerConfig = {
  anthropic: {
    name: 'Anthropic',
    icon: Sparkles,
    color: 'var(--accent)',
    bg: 'var(--accent-subtle)',
  },
  openai: {
    name: 'OpenAI',
    icon: Cpu,
    color: 'var(--success)',
    bg: 'var(--success-subtle)',
  },
  openrouter: {
    name: 'OpenRouter',
    icon: Zap,
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  ollama: {
    name: 'Ollama',
    icon: Bot,
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
  },
}

// ============================================================================
// Format Helpers
// ============================================================================

const formatContextWindow = (tokens: number): string => {
  if (tokens >= 1000000) return `${(tokens / 1000000).toFixed(1)}M`
  if (tokens >= 1000) return `${(tokens / 1000).toFixed(0)}K`
  return tokens.toString()
}

// ============================================================================
// Model Selector Dialog
// ============================================================================

export const ModelSelectorDialog: Component<ModelSelectorDialogProps> = (props) => {
  const [searchQuery, setSearchQuery] = createSignal('')
  const [expandedProvider, setExpandedProvider] = createSignal<string | null>(null)

  // Filter models by search query
  const filteredModels = () => {
    const query = searchQuery().toLowerCase()
    if (!query) return props.models

    return props.models.filter(
      (m) =>
        m.name.toLowerCase().includes(query) ||
        m.id.toLowerCase().includes(query) ||
        m.provider.toLowerCase().includes(query)
    )
  }

  // Group models by provider
  const groupedModels = () => {
    const groups: Record<string, ModelInfo[]> = {}
    for (const model of filteredModels()) {
      if (!groups[model.provider]) {
        groups[model.provider] = []
      }
      groups[model.provider].push(model)
    }
    return groups
  }

  const handleSelect = (model: ModelInfo) => {
    props.onSelect(model)
    props.onOpenChange(false)
  }

  const toggleProvider = (provider: string) => {
    setExpandedProvider((current) => (current === provider ? null : provider))
  }

  return (
    <Dialog
      open={props.open}
      onOpenChange={props.onOpenChange}
      title="Select Model"
      description="Choose an AI model for your conversation"
      size="md"
    >
      {/* Search */}
      <div class="mb-4">
        <div class="relative">
          <Search class="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-muted)]" />
          <input
            type="text"
            placeholder="Search models..."
            value={searchQuery()}
            onInput={(e) => setSearchQuery(e.currentTarget.value)}
            class="
              w-full
              pl-10 pr-4 py-2.5
              bg-[var(--input-background)]
              border border-[var(--input-border)]
              rounded-[var(--radius-lg)]
              text-sm text-[var(--text-primary)]
              placeholder:text-[var(--text-muted)]
              focus:outline-none focus:border-[var(--accent)]
              transition-colors duration-[var(--duration-fast)]
            "
          />
        </div>
      </div>

      {/* Model List */}
      <div class="space-y-2 max-h-96 overflow-y-auto -mx-4 px-4">
        <For each={Object.entries(groupedModels())}>
          {([provider, models]) => {
            const config = providerConfig[provider as keyof typeof providerConfig]
            const isExpanded = () => expandedProvider() === provider || searchQuery().length > 0

            return (
              <div class="border border-[var(--border-subtle)] rounded-[var(--radius-lg)] overflow-hidden">
                {/* Provider Header */}
                <button
                  type="button"
                  onClick={() => toggleProvider(provider)}
                  class="
                    w-full flex items-center gap-3
                    px-3 py-2.5
                    bg-[var(--surface-raised)]
                    hover:bg-[var(--surface-sunken)]
                    transition-colors duration-[var(--duration-fast)]
                  "
                >
                  <div class="p-1.5 rounded-[var(--radius-md)]" style={{ background: config.bg }}>
                    <Dynamic
                      component={config.icon}
                      class="w-4 h-4"
                      style={{ color: config.color }}
                    />
                  </div>
                  <span class="flex-1 text-left text-sm font-medium text-[var(--text-primary)]">
                    {config.name}
                  </span>
                  <span class="text-xs text-[var(--text-muted)]">{models.length} models</span>
                  <ChevronRight
                    class={`
                      w-4 h-4 text-[var(--text-muted)]
                      transition-transform duration-[var(--duration-fast)]
                      ${isExpanded() ? 'rotate-90' : ''}
                    `}
                  />
                </button>

                {/* Models */}
                <Show when={isExpanded()}>
                  <div class="border-t border-[var(--border-subtle)]">
                    <For each={models}>
                      {(model) => {
                        const isSelected = () => props.selectedId === model.id

                        return (
                          <button
                            type="button"
                            onClick={() => handleSelect(model)}
                            class={`
                              w-full text-left
                              flex items-start gap-3
                              px-3 py-3
                              hover:bg-[var(--surface-raised)]
                              transition-colors duration-[var(--duration-fast)]
                              ${isSelected() ? 'bg-[var(--accent-subtle)]' : ''}
                            `}
                          >
                            {/* Selection Indicator */}
                            <div class="flex-shrink-0 mt-0.5">
                              <Show
                                when={isSelected()}
                                fallback={
                                  <div class="w-5 h-5 rounded-full border-2 border-[var(--border-default)]" />
                                }
                              >
                                <div class="w-5 h-5 rounded-full bg-[var(--accent)] flex items-center justify-center">
                                  <Check class="w-3 h-3 text-white" />
                                </div>
                              </Show>
                            </div>

                            {/* Model Info */}
                            <div class="flex-1 min-w-0">
                              <div class="flex items-center gap-2">
                                <span class="text-sm font-medium text-[var(--text-primary)]">
                                  {model.name}
                                </span>
                                <Show when={model.isNew}>
                                  <span class="px-1.5 py-0.5 text-[10px] font-medium rounded-full bg-[var(--success-subtle)] text-[var(--success)]">
                                    New
                                  </span>
                                </Show>
                                <Show when={model.isPremium}>
                                  <span class="px-1.5 py-0.5 text-[10px] font-medium rounded-full bg-[var(--warning-subtle)] text-[var(--warning)]">
                                    Premium
                                  </span>
                                </Show>
                              </div>
                              <Show when={model.description}>
                                <p class="mt-0.5 text-xs text-[var(--text-muted)] line-clamp-1">
                                  {model.description}
                                </p>
                              </Show>
                              <div class="mt-1.5 flex items-center gap-3 text-xs text-[var(--text-tertiary)]">
                                <span class="flex items-center gap-1">
                                  <Cpu class="w-3 h-3" />
                                  {formatContextWindow(model.contextWindow)} context
                                </span>
                                <Show when={model.capabilities?.length}>
                                  <span class="truncate">{model.capabilities?.join(', ')}</span>
                                </Show>
                              </div>
                            </div>
                          </button>
                        )
                      }}
                    </For>
                  </div>
                </Show>
              </div>
            )
          }}
        </For>

        {/* Empty State */}
        <Show when={Object.keys(groupedModels()).length === 0}>
          <div class="py-8 text-center text-sm text-[var(--text-muted)]">
            No models found matching "{searchQuery()}"
          </div>
        </Show>
      </div>
    </Dialog>
  )
}

// ============================================================================
// Default Models
// ============================================================================

export const defaultModels: ModelInfo[] = [
  {
    id: 'claude-3-5-sonnet-20241022',
    name: 'Claude 3.5 Sonnet',
    provider: 'anthropic',
    contextWindow: 200000,
    description: 'Best balance of intelligence and speed',
    capabilities: ['code', 'analysis', 'vision'],
    isNew: true,
  },
  {
    id: 'claude-3-opus-20240229',
    name: 'Claude 3 Opus',
    provider: 'anthropic',
    contextWindow: 200000,
    description: 'Most capable for complex tasks',
    capabilities: ['code', 'analysis', 'vision', 'research'],
    isPremium: true,
  },
  {
    id: 'claude-3-haiku-20240307',
    name: 'Claude 3 Haiku',
    provider: 'anthropic',
    contextWindow: 200000,
    description: 'Fastest responses, great for simple tasks',
    capabilities: ['code', 'analysis'],
  },
  {
    id: 'gpt-4-turbo',
    name: 'GPT-4 Turbo',
    provider: 'openai',
    contextWindow: 128000,
    description: "OpenAI's most capable model",
    capabilities: ['code', 'analysis', 'vision'],
    isPremium: true,
  },
  {
    id: 'gpt-4o',
    name: 'GPT-4o',
    provider: 'openai',
    contextWindow: 128000,
    description: 'Optimized for speed and cost',
    capabilities: ['code', 'analysis', 'vision'],
    isNew: true,
  },
  {
    id: 'gpt-3.5-turbo',
    name: 'GPT-3.5 Turbo',
    provider: 'openai',
    contextWindow: 16000,
    description: 'Fast and affordable',
    capabilities: ['code', 'analysis'],
  },
]
