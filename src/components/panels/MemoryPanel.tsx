/**
 * Memory Panel Component
 *
 * Shows context window usage and memory items (conversation, files, code).
 * Connected to the session store for real-time context tracking.
 */

import { AlertTriangle, Brain, RefreshCw } from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { useSession } from '../../stores/session'
import type { MemoryItem } from '../../types'
import { MemoryItemCard } from './memory/MemoryItemCard'
import { formatTokens } from './memory/memory-config'

export const MemoryPanel: Component = () => {
  const { memoryItems, contextUsage, clearMemoryItems, removeMemoryItem, messages } = useSession()

  const usagePercentage = createMemo(() => contextUsage().percentage)
  const isHighUsage = createMemo(() => usagePercentage() > 80)
  const isWarningUsage = createMemo(() => usagePercentage() > 60)

  // Create memory items from messages if none exist
  const displayItems = createMemo(() => {
    const items = memoryItems()
    if (items.length > 0) return items

    const msgs = messages()
    if (msgs.length === 0) return []

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

  const totalTokens = createMemo(() => displayItems().reduce((sum, item) => sum + item.tokens, 0))

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
        <span
          class="
            p-2
            inline-flex items-center justify-center
            rounded-[var(--radius-md)]
            text-[var(--text-tertiary)]
          "
          aria-hidden="true"
        >
          <RefreshCw class="w-4 h-4" />
        </span>
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
            {(item) => (
              <MemoryItemCard
                item={item}
                onRemove={(id) => {
                  removeMemoryItem(id)
                }}
              />
            )}
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
            <span>&middot;</span>
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
