/**
 * Context Bar
 * Token usage indicator shown below the message input.
 */

import { formatCost } from '@ava/core'
import { Activity } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { useChat } from '../../hooks/useChat'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'

const fmt = (n: number) => (n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n))

export const ContextBar: Component = () => {
  const { contextUsage, sessionTokenStats } = useSession()
  const { settings } = useSettings()
  const { isStreaming, streamingTokenEstimate } = useChat()

  return (
    <Show when={settings().ui.showTokenCount}>
      <div class="flex items-center justify-between density-section-px density-py text-[10px] text-[var(--text-tertiary)] border-t border-[var(--border-subtle)]">
        <div class="flex items-center gap-3">
          <Activity class="w-3 h-3" aria-hidden="true" />
          <span>
            {fmt(contextUsage().used)} / {fmt(contextUsage().total)}
          </span>
          <div class="w-16 h-1 bg-[var(--surface-raised)] rounded-full overflow-hidden">
            <div
              class="h-full rounded-full transition-colors"
              style={{
                width: `${Math.min(100, contextUsage().percentage)}%`,
                'background-color':
                  contextUsage().percentage > 80
                    ? 'var(--warning)'
                    : contextUsage().percentage > 60
                      ? 'var(--text-muted)'
                      : 'var(--accent)',
              }}
            />
          </div>
          <span>{contextUsage().percentage.toFixed(0)}%</span>

          {/* Session cost */}
          <Show when={sessionTokenStats().totalCost > 0}>
            <span class="ml-1 tabular-nums">{formatCost(sessionTokenStats().totalCost)}</span>
          </Show>

          {/* Streaming token estimate */}
          <Show when={isStreaming() && streamingTokenEstimate() > 0}>
            <span class="ml-1 text-[var(--accent)] animate-pulse">
              +{fmt(streamingTokenEstimate())} tokens
            </span>
          </Show>
        </div>
      </div>
    </Show>
  )
}
