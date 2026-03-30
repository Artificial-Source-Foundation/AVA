/**
 * Memory Panel Component
 *
 * Shows context window usage and memory items (conversation, files, code).
 * Connected to the session store for real-time context tracking.
 *
 * Design: rounded-12 card, #111114 bg, #0F0F12 header, purple brain icon,
 * purple "N items" badge, memory cards with type badges.
 */

import { Brain, Plus } from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { useSession } from '../../stores/session'
import type { MemoryItem } from '../../types'
import { MemoryItemCard } from './memory/MemoryItemCard'

export const MemoryPanel: Component = () => {
  const { memoryItems, removeMemoryItem, messages } = useSession()

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

  return (
    <div class="flex flex-col h-full overflow-hidden rounded-[10px] bg-[var(--surface)] border border-[var(--border-subtle)]">
      {/* Header */}
      <div class="flex items-center justify-between h-10 px-3 bg-[var(--background-subtle)] shrink-0">
        <div class="flex items-center gap-2">
          <Brain class="w-3.5 h-3.5 text-[var(--system-purple)]" />
          <span class="text-xs font-medium text-[var(--text-secondary)]">Memory</span>
          <span class="inline-flex items-center px-1.5 py-0.5 text-[10px] font-medium rounded-md bg-[var(--system-purple)]/20 text-[var(--system-purple)]">
            {displayItems().length} item{displayItems().length !== 1 ? 's' : ''}
          </span>
        </div>
        <div class="flex items-center gap-1">
          <button
            type="button"
            class="p-1 rounded-[6px] text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors"
            title="Add memory item"
          >
            <Plus class="w-[13px] h-[13px]" />
          </button>
        </div>
      </div>

      {/* Memory Items List */}
      <div class="flex-1 overflow-y-auto p-2.5 space-y-1.5">
        <Show
          when={displayItems().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center">
              <Brain class="w-6 h-6 text-[var(--text-muted)] mb-2" />
              <p class="text-[11px] text-[var(--text-muted)]">
                Memory items will appear here as you chat
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
    </div>
  )
}

/** Skeleton placeholder shown while memory data is loading */
export const MemoryPanelSkeleton: Component = () => (
  <div class="flex flex-col h-full animate-pulse-subtle rounded-[10px] bg-[var(--surface)] border border-[var(--border-subtle)]">
    <div class="h-10 px-3 bg-[var(--background-subtle)] flex items-center gap-2">
      <div class="w-3.5 h-3.5 bg-[var(--surface-raised)] rounded" />
      <div class="h-3 w-16 bg-[var(--surface-raised)] rounded" />
      <div class="h-4 w-14 bg-[var(--surface-raised)] rounded-md" />
    </div>
    <div class="flex-1 p-2.5 space-y-1.5">
      <For each={[1, 2]}>
        {() => (
          <div class="p-2.5 rounded-[6px] bg-[var(--background-subtle)] border border-[var(--border-subtle)]">
            <div class="flex items-center justify-between mb-1.5">
              <div class="h-3 w-28 bg-[var(--surface-raised)] rounded" />
              <div class="h-4 w-12 bg-[var(--surface-raised)] rounded-md" />
            </div>
            <div class="h-2.5 w-full bg-[var(--surface-raised)] rounded" />
          </div>
        )}
      </For>
    </div>
  </div>
)
