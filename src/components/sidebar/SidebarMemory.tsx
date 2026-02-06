/**
 * Sidebar Memory View
 *
 * Compact view of context window usage and memory items.
 * Extracted from MemoryPanel for sidebar display.
 */

import { Brain, Code2, FileText, MessageSquare, Sparkles, Trash2 } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { useSession } from '../../stores/session'
import type { MemoryItem } from '../../types'

const typeConfig: Record<string, { icon: typeof Brain; color: string }> = {
  conversation: { icon: MessageSquare, color: 'var(--accent)' },
  file: { icon: FileText, color: 'var(--info)' },
  code: { icon: Code2, color: 'var(--warning)' },
  knowledge: { icon: Sparkles, color: 'var(--success)' },
}

export const SidebarMemory: Component = () => {
  const { memoryItems, contextUsage, sessionTokenStats, removeMemoryItem, clearMemoryItems } =
    useSession()

  const usage = () => contextUsage()
  const tokens = () => sessionTokenStats()

  const usageColor = () => {
    if (usage().percentage > 80) return 'var(--error)'
    if (usage().percentage > 60) return 'var(--warning)'
    return 'var(--success)'
  }

  const formatTokens = (count: number): string => {
    if (count >= 1000000) return `${(count / 1000000).toFixed(1)}M`
    if (count >= 1000) return `${(count / 1000).toFixed(1)}K`
    return count.toString()
  }

  const getItemConfig = (item: MemoryItem) => typeConfig[item.type] ?? typeConfig.knowledge

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center justify-between px-3 h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
        <span class="text-xs font-semibold text-[var(--text-secondary)] uppercase tracking-wider">
          Memory
        </span>
        <span class="text-[10px] text-[var(--text-muted)]">{formatTokens(tokens().total)}</span>
      </div>

      {/* Context Usage Bar */}
      <div class="px-3 py-2 flex-shrink-0">
        <div class="flex items-center justify-between mb-1">
          <span class="text-[10px] text-[var(--text-muted)]">Context</span>
          <span class="text-[10px] font-medium" style={{ color: usageColor() }}>
            {usage().percentage.toFixed(0)}%
          </span>
        </div>
        <div class="h-1.5 bg-[var(--surface-sunken)] rounded-full overflow-hidden">
          <div
            class="h-full rounded-full transition-[width] duration-300"
            style={{
              width: `${Math.min(usage().percentage, 100)}%`,
              background: usageColor(),
            }}
          />
        </div>
        <Show when={usage().percentage > 80}>
          <p class="text-[10px] text-[var(--error)] mt-1">Context nearly full</p>
        </Show>
      </div>

      {/* Memory Items */}
      <div class="flex-1 overflow-y-auto px-1.5 scrollbar-none">
        <Show
          when={memoryItems().length > 0}
          fallback={
            <div class="text-center py-6 px-4 text-[var(--text-muted)]">
              <Brain class="w-5 h-5 mx-auto mb-2 opacity-50" />
              <p class="text-[10px]">No memory items</p>
            </div>
          }
        >
          <div class="space-y-0.5">
            <For each={memoryItems()}>
              {(item) => {
                const config = getItemConfig(item)
                const Icon = config.icon

                return (
                  <div
                    class="
                      group flex items-center gap-2 px-2 py-1.5
                      rounded-[var(--radius-md)]
                      hover:bg-[var(--alpha-white-3)]
                    "
                  >
                    <Icon class="w-3.5 h-3.5 flex-shrink-0" style={{ color: config.color }} />
                    <div class="flex-1 min-w-0">
                      <div class="text-xs text-[var(--text-primary)] truncate">{item.title}</div>
                      <div class="text-[10px] text-[var(--text-muted)] truncate">
                        {formatTokens(item.tokens ?? 0)} tokens
                      </div>
                    </div>
                    <button
                      type="button"
                      onClick={() => removeMemoryItem(item.id)}
                      class="
                        opacity-0 group-hover:opacity-100
                        flex-shrink-0 p-0.5
                        text-[var(--text-muted)] hover:text-[var(--error)]
                        transition-all
                      "
                      title="Remove"
                    >
                      <Trash2 class="w-3 h-3" />
                    </button>
                  </div>
                )
              }}
            </For>
          </div>
        </Show>
      </div>

      {/* Footer */}
      <Show when={memoryItems().length > 0}>
        <div class="px-3 py-1.5 border-t border-[var(--border-subtle)] flex items-center justify-between">
          <span class="text-[10px] text-[var(--text-muted)]">{memoryItems().length} items</span>
          <button
            type="button"
            onClick={clearMemoryItems}
            class="text-[10px] text-[var(--text-muted)] hover:text-[var(--error)] transition-colors"
          >
            Clear All
          </button>
        </div>
      </Show>
    </div>
  )
}
