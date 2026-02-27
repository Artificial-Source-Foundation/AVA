/**
 * Context Bar
 *
 * Goose-inspired status strip below the message input.
 * Shows token usage, cost, model, and message count.
 * Click the token icon to toggle the progress bar detail.
 */

import { formatCost } from '@ava/core'
import { Activity, MessageSquare } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { useChat } from '../../hooks/useChat'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'

const fmt = (n: number) => (n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n))

export const ContextBar: Component = () => {
  const { contextUsage, sessionTokenStats, selectedModel, messages } = useSession()
  const { settings, updateSettings } = useSettings()
  const { isStreaming, streamingTokenEstimate } = useChat()
  const { currentProject } = useProject()

  const showTokens = () => settings().ui.showTokenCount

  const toggleTokens = () => {
    updateSettings({ ui: { ...settings().ui, showTokenCount: !showTokens() } })
  }

  // Use real session stats (from API responses) as primary token source,
  // fall back to context estimate only when no real data exists
  const tokenDisplay = () => {
    const real = sessionTokenStats().total
    if (real > 0) return fmt(real)
    return fmt(contextUsage().used)
  }

  const percentage = () => {
    const real = sessionTokenStats().total
    const limit = contextUsage().total
    if (real > 0 && limit > 0) return Math.min(100, (real / limit) * 100)
    return contextUsage().percentage
  }

  const barColor = () => {
    const pct = percentage()
    if (pct > 80) return 'var(--warning)'
    if (pct > 60) return 'var(--text-muted)'
    return 'var(--accent)'
  }

  const projectPath = () => {
    const dir = currentProject()?.directory
    if (!dir || dir === '~') return null
    const parts = dir.split('/')
    if (parts.length > 3) return `.../${parts.slice(-2).join('/')}`
    return dir
  }

  const msgCount = () => messages().length

  return (
    <div class="flex items-center density-section-px py-1 text-[10px] text-[var(--text-tertiary)] border-t border-[var(--border-subtle)] font-[var(--font-ui-mono)] select-none">
      <div class="flex items-center gap-2 min-w-0">
        {/* Project path */}
        <Show when={projectPath()}>
          <span
            class="truncate max-w-[160px] text-[var(--text-muted)]"
            title={currentProject()?.directory}
          >
            {projectPath()}
          </span>
          <Dot />
        </Show>

        {/* Token usage — click to toggle progress bar */}
        <button
          type="button"
          onClick={toggleTokens}
          class="inline-flex items-center gap-1.5 hover:text-[var(--text-secondary)] transition-colors"
          title={showTokens() ? 'Hide token details' : 'Show token details'}
        >
          <Activity class="w-3 h-3" />
          <span class="tabular-nums">{tokenDisplay()}</span>
        </button>

        {/* Progress bar + percentage (togglable) */}
        <Show when={showTokens()}>
          <div class="w-12 h-1 bg-[var(--surface-raised)] rounded-full overflow-hidden">
            <div
              class="h-full rounded-full transition-all duration-300"
              style={{
                width: `${Math.min(100, percentage())}%`,
                'background-color': barColor(),
              }}
            />
          </div>
          <span class="tabular-nums">{percentage().toFixed(0)}%</span>
        </Show>

        {/* Session cost */}
        <Show when={sessionTokenStats().totalCost > 0}>
          <Dot />
          <span class="tabular-nums text-[var(--success)]">
            {formatCost(sessionTokenStats().totalCost)}
          </span>
        </Show>

        {/* Streaming indicator */}
        <Show when={isStreaming() && streamingTokenEstimate() > 0}>
          <Dot />
          <span class="text-[var(--accent)] animate-pulse tabular-nums">
            +{fmt(streamingTokenEstimate())}
          </span>
        </Show>

        {/* Current model */}
        <Dot />
        <span class="truncate max-w-[140px]" title={selectedModel()}>
          {selectedModel()}
        </span>

        {/* Message count */}
        <Show when={msgCount() > 0}>
          <Dot />
          <span class="inline-flex items-center gap-1 tabular-nums">
            <MessageSquare class="w-2.5 h-2.5" />
            {msgCount()}
          </span>
        </Show>
      </div>
    </div>
  )
}

/** Tiny separator dot */
const Dot: Component = () => <span class="text-[var(--border-muted)]">&middot;</span>
