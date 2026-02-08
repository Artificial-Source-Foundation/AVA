/**
 * Context Bar
 * Token usage indicator shown below the message input.
 */

import { Activity } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'

const fmt = (n: number) => (n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n))

export const ContextBar: Component = () => {
  const { contextUsage } = useSession()
  const { settings } = useSettings()

  return (
    <Show when={settings().ui.showTokenCount}>
      <div class="flex items-center justify-between px-4 py-1 text-[10px] text-[var(--text-tertiary)] border-t border-[var(--border-subtle)]">
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
        </div>
      </div>
    </Show>
  )
}
