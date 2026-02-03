/**
 * Status Bar Component
 *
 * Bottom status bar showing agent status, token usage, and app info.
 * Clean, minimal design with themed colors.
 */

import { Activity, Circle, Zap } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { useSession } from '../../stores/session'

export const StatusBar: Component = () => {
  const { sessionTokenStats, currentSession } = useSession()

  // Format token count with K/M suffixes
  const formatTokens = (count: number): string => {
    if (count >= 1000000) return `${(count / 1000000).toFixed(1)}M`
    if (count >= 1000) return `${(count / 1000).toFixed(1)}K`
    return count.toString()
  }

  return (
    <div
      class="
        flex items-center justify-between
        h-8 px-4
        bg-[var(--surface-sunken)]
        border-t border-[var(--border-subtle)]
        text-xs text-[var(--text-tertiary)]
        transition-colors duration-[var(--duration-normal)]
      "
    >
      {/* Left side - Agent status */}
      <div class="flex items-center gap-4">
        <div class="flex items-center gap-2">
          <span class="relative flex h-2 w-2">
            <span class="animate-ping absolute inline-flex h-full w-full rounded-full bg-[var(--success)] opacity-75" />
            <span class="relative inline-flex rounded-full h-2 w-2 bg-[var(--success)]" />
          </span>
          <span class="font-medium">Ready</span>
        </div>
      </div>

      {/* Center - Current model (optional) */}
      <div class="flex items-center gap-2 text-[var(--text-muted)]">
        <Activity class="w-3 h-3" />
        <span>Claude 3.5 Sonnet</span>
      </div>

      {/* Right side - Token count and version */}
      <div class="flex items-center gap-4">
        {/* Session token counter */}
        <Show when={currentSession() && sessionTokenStats().total > 0}>
          <div class="flex items-center gap-1.5" title="Session token usage">
            <Zap class="w-3 h-3 text-[var(--warning)]" />
            <span>{formatTokens(sessionTokenStats().total)} tokens</span>
          </div>
        </Show>

        {/* Separator */}
        <Circle class="w-1 h-1 fill-current opacity-30" />

        {/* Version */}
        <span class="text-[var(--text-muted)]">v0.1.0</span>
      </div>
    </div>
  )
}
