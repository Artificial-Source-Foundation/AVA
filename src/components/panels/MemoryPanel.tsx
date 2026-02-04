/**
 * Memory Panel Component
 *
 * Shows memory/context visualization including conversation history,
 * context window usage, and stored knowledge.
 * Premium design with visual indicators.
 */

import {
  AlertTriangle,
  Brain,
  ChevronRight,
  Code2,
  Database,
  FileText,
  History,
  Layers,
  MessageSquare,
  Trash2,
  Zap,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'

// Mock memory data for design preview
const mockMemoryItems = [
  {
    id: '1',
    type: 'conversation' as const,
    title: 'Design system discussion',
    tokens: 2450,
    timestamp: Date.now() - 300000,
    preview: 'Discussed the implementation of Glass, Minimal, Terminal, and Soft themes...',
  },
  {
    id: '2',
    type: 'file' as const,
    title: 'src/styles/tokens.css',
    tokens: 1820,
    timestamp: Date.now() - 600000,
    preview: 'CSS design tokens with OKLCH colors, spacing, typography...',
  },
  {
    id: '3',
    type: 'code' as const,
    title: 'MessageBubble component',
    tokens: 980,
    timestamp: Date.now() - 900000,
    preview: 'SolidJS component for chat message display with theming...',
  },
  {
    id: '4',
    type: 'knowledge' as const,
    title: 'Project architecture',
    tokens: 650,
    timestamp: Date.now() - 1800000,
    preview: 'Estela uses SolidJS with Tauri, multi-agent architecture...',
  },
]

type MemoryType = 'conversation' | 'file' | 'code' | 'knowledge'

const memoryTypeConfig: Record<
  MemoryType,
  { color: string; bg: string; icon: typeof Brain; label: string }
> = {
  conversation: {
    color: 'var(--accent)',
    bg: 'var(--accent-subtle)',
    icon: MessageSquare,
    label: 'Conversation',
  },
  file: { color: 'var(--success)', bg: 'var(--success-subtle)', icon: FileText, label: 'File' },
  code: { color: 'var(--warning)', bg: 'var(--warning-subtle)', icon: Code2, label: 'Code' },
  knowledge: { color: 'var(--info)', bg: 'var(--info-subtle)', icon: Database, label: 'Knowledge' },
}

export const MemoryPanel: Component = () => {
  const [selectedId, setSelectedId] = createSignal<string | null>(null)

  // Context window simulation
  const maxTokens = 128000
  const usedTokens = () => mockMemoryItems.reduce((sum, item) => sum + item.tokens, 0)
  const usagePercent = () => Math.round((usedTokens() / maxTokens) * 100)

  const formatTokens = (tokens: number): string => {
    if (tokens >= 1000) return `${(tokens / 1000).toFixed(1)}K`
    return tokens.toString()
  }

  const formatTime = (timestamp: number): string => {
    const minutes = Math.floor((Date.now() - timestamp) / 60000)
    if (minutes < 60) return `${minutes}m ago`
    const hours = Math.floor(minutes / 60)
    return `${hours}h ago`
  }

  const getUsageColor = () => {
    const percent = usagePercent()
    if (percent < 50) return 'var(--success)'
    if (percent < 80) return 'var(--warning)'
    return 'var(--error)'
  }

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div
        class="
          flex items-center justify-between
          px-4 py-3
          border-b border-[var(--border-subtle)]
        "
      >
        <div class="flex items-center gap-3">
          <div
            class="
              p-2
              bg-[var(--info-subtle)]
              rounded-[var(--radius-lg)]
            "
          >
            <Brain class="w-5 h-5 text-[var(--info)]" />
          </div>
          <div>
            <h2 class="text-sm font-semibold text-[var(--text-primary)]">Memory</h2>
            <p class="text-xs text-[var(--text-muted)]">Context & knowledge</p>
          </div>
        </div>
        <button
          type="button"
          class="
            p-2
            rounded-[var(--radius-md)]
            text-[var(--text-tertiary)]
            hover:text-[var(--error)]
            hover:bg-[var(--error-subtle)]
            transition-colors duration-[var(--duration-fast)]
          "
          title="Clear memory"
        >
          <Trash2 class="w-4 h-4" />
        </button>
      </div>

      {/* Context Window Usage */}
      <div class="px-4 py-4 border-b border-[var(--border-subtle)]">
        <div class="flex items-center justify-between mb-2">
          <span class="text-xs font-medium text-[var(--text-secondary)] flex items-center gap-1.5">
            <Layers class="w-3.5 h-3.5" />
            Context Window
          </span>
          <span class="text-xs text-[var(--text-muted)]">
            {formatTokens(usedTokens())} / {formatTokens(maxTokens)} tokens
          </span>
        </div>

        {/* Progress Bar */}
        <div class="h-3 bg-[var(--surface-sunken)] rounded-full overflow-hidden">
          <div
            class="h-full rounded-full transition-all duration-500"
            style={{
              width: `${usagePercent()}%`,
              background: getUsageColor(),
            }}
          />
        </div>

        {/* Usage Warning */}
        <Show when={usagePercent() > 80}>
          <div
            class="
              flex items-center gap-2
              mt-3 p-2
              bg-[var(--warning-subtle)]
              border border-[var(--warning-muted)]
              rounded-[var(--radius-md)]
            "
          >
            <AlertTriangle class="w-4 h-4 text-[var(--warning)] flex-shrink-0" />
            <span class="text-xs text-[var(--warning)]">
              Context window {usagePercent()}% full. Consider clearing old context.
            </span>
          </div>
        </Show>

        {/* Quick Stats */}
        <div class="flex items-center gap-4 mt-3">
          <div class="flex items-center gap-1.5 text-xs text-[var(--text-muted)]">
            <Zap class="w-3 h-3 text-[var(--warning)]" />
            <span>{usagePercent()}% used</span>
          </div>
          <div class="flex items-center gap-1.5 text-xs text-[var(--text-muted)]">
            <History class="w-3 h-3" />
            <span>{mockMemoryItems.length} items</span>
          </div>
        </div>
      </div>

      {/* Memory Items List */}
      <div class="flex-1 overflow-y-auto p-3 space-y-2">
        <For each={mockMemoryItems}>
          {(item) => {
            const config = memoryTypeConfig[item.type]
            const Icon = config.icon
            const isSelected = selectedId() === item.id

            return (
              <button
                type="button"
                onClick={() => setSelectedId(isSelected ? null : item.id)}
                class={`
                  w-full text-left
                  p-3
                  rounded-[var(--radius-lg)]
                  border
                  transition-all duration-[var(--duration-fast)]
                  ${
                    isSelected
                      ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                      : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                  }
                `}
              >
                <div class="flex items-start gap-3">
                  {/* Type Icon */}
                  <div
                    class="p-1.5 rounded-[var(--radius-md)] flex-shrink-0"
                    style={{ background: config.bg }}
                  >
                    <Icon class="w-4 h-4" style={{ color: config.color }} />
                  </div>

                  {/* Item Info */}
                  <div class="flex-1 min-w-0">
                    <div class="flex items-center justify-between gap-2">
                      <span class="text-sm font-medium text-[var(--text-primary)] truncate">
                        {item.title}
                      </span>
                      <span
                        class="
                          text-[10px] px-1.5 py-0.5
                          rounded-full
                          flex-shrink-0
                        "
                        style={{ background: config.bg, color: config.color }}
                      >
                        {config.label}
                      </span>
                    </div>

                    <div class="flex items-center gap-3 mt-1 text-xs text-[var(--text-muted)]">
                      <span class="flex items-center gap-1">
                        <Zap class="w-3 h-3" />
                        {formatTokens(item.tokens)}
                      </span>
                      <span>{formatTime(item.timestamp)}</span>
                    </div>

                    {/* Expanded preview */}
                    <Show when={isSelected}>
                      <p class="mt-3 text-xs text-[var(--text-secondary)] line-clamp-3">
                        {item.preview}
                      </p>
                    </Show>
                  </div>

                  {/* Expand indicator */}
                  <ChevronRight
                    class={`
                      w-4 h-4 flex-shrink-0
                      text-[var(--text-muted)]
                      transition-transform duration-[var(--duration-fast)]
                      ${isSelected ? 'rotate-90' : ''}
                    `}
                  />
                </div>
              </button>
            )
          }}
        </For>
      </div>

      {/* Footer */}
      <div
        class="
          px-4 py-3
          border-t border-[var(--border-subtle)]
          bg-[var(--surface-sunken)]
        "
      >
        <div class="flex items-center justify-between text-xs text-[var(--text-muted)]">
          <div class="flex items-center gap-3">
            <For each={Object.entries(memoryTypeConfig)}>
              {([type, config]) => {
                const count = () => mockMemoryItems.filter((i) => i.type === type).length
                return (
                  <span class="flex items-center gap-1" style={{ color: config.color }}>
                    <config.icon class="w-3 h-3" />
                    {count()}
                  </span>
                )
              }}
            </For>
          </div>
          <span class="font-mono">{formatTokens(maxTokens - usedTokens())} remaining</span>
        </div>
      </div>
    </div>
  )
}
