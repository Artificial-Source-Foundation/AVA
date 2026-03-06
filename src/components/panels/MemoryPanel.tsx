/**
 * Memory Panel Component
 *
 * Shows context window usage and memory items (conversation, files, code).
 * Connected to the session store for real-time context tracking.
 */

import {
  AlertTriangle,
  Bookmark,
  Brain,
  ChevronRight,
  Code2,
  FileText,
  MessageSquare,
  RefreshCw,
  Sparkles,
  Trash2,
} from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useSession } from '../../stores/session'
import type { MemoryItem, MemoryItemType } from '../../types'

// ============================================================================
// Memory Item Configuration
// ============================================================================

const memoryTypeConfig: Record<
  MemoryItemType,
  { color: string; bg: string; icon: typeof MessageSquare; label: string }
> = {
  conversation: {
    color: 'var(--accent)',
    bg: 'var(--accent-subtle)',
    icon: MessageSquare,
    label: 'Conversation',
  },
  file: {
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
    icon: FileText,
    label: 'File',
  },
  code: {
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
    icon: Code2,
    label: 'Code',
  },
  knowledge: {
    color: 'var(--success)',
    bg: 'var(--success-subtle)',
    icon: Sparkles,
    label: 'Knowledge',
  },
  checkpoint: {
    color: 'var(--text-muted)',
    bg: 'var(--surface-raised)',
    icon: Bookmark,
    label: 'Checkpoint',
  },
}

// ============================================================================
// Component
// ============================================================================

export const MemoryPanel: Component = () => {
  const { memoryItems, contextUsage, clearMemoryItems, removeMemoryItem, messages } = useSession()
  const [selectedItem, setSelectedItem] = createSignal<string | null>(null)

  const formatTokens = (tokens: number): string => {
    if (tokens >= 1000000) return `${(tokens / 1000000).toFixed(1)}M`
    if (tokens >= 1000) return `${(tokens / 1000).toFixed(1)}K`
    return tokens.toString()
  }

  const usagePercentage = createMemo(() => contextUsage().percentage)
  const isHighUsage = createMemo(() => usagePercentage() > 80)
  const isWarningUsage = createMemo(() => usagePercentage() > 60)

  // Create memory items from messages if none exist
  const displayItems = createMemo(() => {
    const items = memoryItems()
    if (items.length > 0) return items

    // Generate items from messages for display
    const msgs = messages()
    if (msgs.length === 0) return []

    // Group messages as conversation memory
    const conversationItem: MemoryItem = {
      id: 'conversation-current',
      sessionId: msgs[0]?.sessionId || '',
      type: 'conversation',
      title: 'Current Conversation',
      preview: msgs
        .slice(-3)
        .map((m) => m.content.slice(0, 50))
        .join(' ... '),
      tokens: Math.ceil(msgs.reduce((sum, m) => sum + m.content.length / 4, 0)),
      createdAt: msgs[msgs.length - 1]?.createdAt || Date.now(),
    }

    return [conversationItem]
  })

  const totalTokens = createMemo(() => {
    return displayItems().reduce((sum, item) => sum + item.tokens, 0)
  })

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div
        class="
          flex items-center justify-between
          density-section-px density-section-py
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
            <h2 class="text-sm font-semibold text-[var(--text-primary)]">Context Memory</h2>
            <p class="text-xs text-[var(--text-muted)]">
              {formatTokens(contextUsage().used)} / {formatTokens(contextUsage().total)} tokens
            </p>
          </div>
        </div>
        <button
          type="button"
          class="
            p-2
            rounded-[var(--radius-md)]
            text-[var(--text-tertiary)]
            hover:text-[var(--text-primary)]
            hover:bg-[var(--surface-raised)]
            transition-colors duration-[var(--duration-fast)]
          "
          title="Refresh"
          aria-label="Refresh memory"
        >
          <RefreshCw class="w-4 h-4" />
        </button>
      </div>

      {/* Context Usage Bar */}
      <div class="density-section-px density-section-py border-b border-[var(--border-subtle)]">
        <div class="flex items-center justify-between mb-2">
          <span class="text-xs font-medium text-[var(--text-secondary)]">Context Window Usage</span>
          <span
            class={`text-xs font-medium ${
              isHighUsage()
                ? 'text-[var(--error)]'
                : isWarningUsage()
                  ? 'text-[var(--warning)]'
                  : 'text-[var(--text-muted)]'
            }`}
          >
            {usagePercentage().toFixed(1)}%
          </span>
        </div>
        <div class="h-2 bg-[var(--surface-sunken)] rounded-full overflow-hidden">
          <div
            class={`h-full rounded-full transition-[width] duration-300 ${
              isHighUsage()
                ? 'bg-[var(--error)]'
                : isWarningUsage()
                  ? 'bg-[var(--warning)]'
                  : 'bg-[var(--accent)]'
            }`}
            style={{ width: `${Math.min(100, usagePercentage())}%` }}
          />
        </div>

        {/* Warning for high usage */}
        <Show when={isHighUsage()}>
          <div class="flex items-center gap-2 mt-2 text-xs text-[var(--error)]">
            <AlertTriangle class="w-3.5 h-3.5" />
            <span>Context nearly full. Consider clearing some memory.</span>
          </div>
        </Show>
      </div>

      {/* Memory Items List */}
      <div class="flex-1 overflow-y-auto density-section space-y-2">
        <Show
          when={displayItems().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center p-6">
              <div class="p-4 bg-[var(--surface-raised)] rounded-full mb-4">
                <Brain class="w-8 h-8 text-[var(--text-muted)]" />
              </div>
              <h3 class="text-sm font-medium text-[var(--text-secondary)] mb-1">No memory items</h3>
              <p class="text-xs text-[var(--text-muted)]">
                Context and memory items will appear here as you chat
              </p>
            </div>
          }
        >
          <For each={displayItems()}>
            {(item) => {
              const config = memoryTypeConfig[item.type]
              const ItemIcon = config.icon

              return (
                <button
                  type="button"
                  onClick={() => setSelectedItem(selectedItem() === item.id ? null : item.id)}
                  class={`
                    w-full text-left
                    density-section-px density-section-py
                    rounded-[var(--radius-lg)]
                    border
                    transition-all duration-[var(--duration-fast)]
                    ${
                      selectedItem() === item.id
                        ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                        : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                    }
                  `}
                >
                  <div class="flex items-start gap-3">
                    {/* Item Icon */}
                    <div
                      class="p-2 rounded-[var(--radius-md)] flex-shrink-0"
                      style={{ background: config.bg }}
                    >
                      <ItemIcon class="w-4 h-4" style={{ color: config.color }} />
                    </div>

                    {/* Item Info */}
                    <div class="flex-1 min-w-0">
                      <div class="flex items-center justify-between gap-2">
                        <span class="text-sm font-medium text-[var(--text-primary)] truncate">
                          {item.title}
                        </span>
                        <span
                          class="flex items-center gap-1 px-1.5 py-0.5 text-[10px] font-medium rounded-full flex-shrink-0"
                          style={{ background: config.bg, color: config.color }}
                        >
                          {config.label}
                        </span>
                      </div>

                      <p class="text-xs text-[var(--text-muted)] mt-1 line-clamp-2">
                        {item.preview}
                      </p>

                      {/* Token count */}
                      <div class="flex items-center gap-2 mt-2">
                        <span class="text-xs text-[var(--text-muted)]">
                          {formatTokens(item.tokens)} tokens
                        </span>
                        <Show when={item.source}>
                          <span class="text-xs text-[var(--text-muted)]">·</span>
                          <span class="text-xs text-[var(--text-muted)] truncate">
                            {item.source}
                          </span>
                        </Show>
                      </div>

                      {/* Expanded details */}
                      <Show when={selectedItem() === item.id}>
                        <div class="mt-3 pt-3 border-t border-[var(--border-subtle)] space-y-2">
                          {/* Full preview */}
                          <div class="text-xs text-[var(--text-secondary)] p-2 bg-[var(--surface-sunken)] rounded-[var(--radius-md)] max-h-32 overflow-y-auto">
                            {item.preview}
                          </div>

                          {/* Actions */}
                          <Show when={item.id !== 'conversation-current'}>
                            <div class="flex items-center justify-end">
                              <button
                                type="button"
                                onClick={(e) => {
                                  e.stopPropagation()
                                  removeMemoryItem(item.id)
                                  setSelectedItem(null)
                                }}
                                class="
                                  flex items-center gap-1 px-2 py-1
                                  text-xs text-[var(--error)]
                                  hover:bg-[var(--error-subtle)]
                                  rounded-[var(--radius-md)]
                                  transition-colors
                                "
                              >
                                <Trash2 class="w-3 h-3" />
                                Remove
                              </button>
                            </div>
                          </Show>
                        </div>
                      </Show>
                    </div>

                    {/* Expand indicator */}
                    <ChevronRight
                      class={`
                        w-4 h-4 flex-shrink-0
                        text-[var(--text-muted)]
                        transition-transform duration-[var(--duration-fast)]
                        ${selectedItem() === item.id ? 'rotate-90' : ''}
                      `}
                    />
                  </div>
                </button>
              )
            }}
          </For>
        </Show>
      </div>

      {/* Footer */}
      <div
        class="
          density-section-px density-section-py
          border-t border-[var(--border-subtle)]
          bg-[var(--surface-sunken)]
        "
      >
        <div class="flex items-center justify-between text-xs text-[var(--text-muted)]">
          <div class="flex items-center gap-3">
            <span>{displayItems().length} items</span>
            <span>·</span>
            <span>{formatTokens(totalTokens())} tokens total</span>
          </div>
          <Show when={memoryItems().length > 0}>
            <button
              type="button"
              onClick={() => clearMemoryItems()}
              class="text-[var(--text-tertiary)] hover:text-[var(--error)] transition-colors"
            >
              Clear All
            </button>
          </Show>
        </div>
      </div>
    </div>
  )
}

/** Skeleton placeholder shown while memory data is loading */
export const MemoryPanelSkeleton: Component = () => (
  <div class="flex flex-col h-full animate-pulse">
    <div class="px-4 py-3 border-b border-[var(--border-subtle)]">
      <div class="flex items-center gap-3">
        <div class="w-9 h-9 bg-[var(--surface-raised)] rounded-[var(--radius-lg)]" />
        <div class="space-y-1.5">
          <div class="h-3.5 w-28 bg-[var(--surface-raised)] rounded" />
          <div class="h-2.5 w-36 bg-[var(--surface-raised)] rounded" />
        </div>
      </div>
    </div>
    <div class="px-4 py-3 border-b border-[var(--border-subtle)]">
      <div class="flex justify-between mb-2">
        <div class="h-3 w-28 bg-[var(--surface-raised)] rounded" />
        <div class="h-3 w-10 bg-[var(--surface-raised)] rounded" />
      </div>
      <div class="h-2 bg-[var(--surface-raised)] rounded-full" />
    </div>
    <div class="flex-1 p-4 space-y-2">
      <For each={[1, 2]}>
        {() => (
          <div class="p-3 rounded-[var(--radius-lg)] border border-[var(--border-subtle)]">
            <div class="flex items-start gap-3">
              <div class="w-8 h-8 bg-[var(--surface-raised)] rounded-[var(--radius-md)]" />
              <div class="flex-1 space-y-1.5">
                <div class="h-3.5 w-32 bg-[var(--surface-raised)] rounded" />
                <div class="h-2.5 w-full bg-[var(--surface-raised)] rounded" />
                <div class="h-2.5 w-16 bg-[var(--surface-raised)] rounded" />
              </div>
            </div>
          </div>
        )}
      </For>
    </div>
  </div>
)
