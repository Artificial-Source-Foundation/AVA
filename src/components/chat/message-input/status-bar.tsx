/**
 * Status Bar
 *
 * Right-side context/token info strip — priority-based visibility:
 *   Streaming: elapsed · turn · tool · [queued] · progress bar
 *   Idle:      token count · cost · context % bar · [compact] · [diagnostics]
 *
 * ≤5 items visible at once. Font: 11px.
 */

import { Activity, AlertCircle, AlertTriangle, Archive, Loader2, MessageSquare } from 'lucide-solid'
import { type Accessor, type Component, createMemo, createSignal, Show } from 'solid-js'
import { useNotification } from '../../../contexts/notification'
import { useAgent } from '../../../hooks/useAgent'
import { useElapsedTimer } from '../../../hooks/useElapsedTimer'
import { formatCost } from '../../../lib/cost'
import { formatSeconds } from '../../../lib/format-time'
import { getCoreBudget } from '../../../services/core-bridge'
import { useDiagnostics } from '../../../stores/diagnostics'
import { useSession } from '../../../stores/session'
import { useSettings } from '../../../stores/settings'
import { summarizeAction } from '../tool-call-utils'

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const fmt = (n: number): string => (n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n))

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface StatusBarProps {
  stashSize: Accessor<number>
  isStreaming: Accessor<boolean>
  streamingTokenEstimate: Accessor<number>
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const StatusBar: Component<StatusBarProps> = (props) => {
  const sessionStore = useSession()
  const { settings, updateSettings } = useSettings()
  const { diagnostics, hasDiagnostics } = useDiagnostics()
  const agent = useAgent()
  const notify = useNotification()
  const [isCompacting, setIsCompacting] = createSignal(false)

  const { contextUsage, sessionTokenStats, messages } = sessionStore

  // Elapsed time during streaming
  const elapsedSec = useElapsedTimer(() => agent.streamingStartedAt())

  // Current active tool name
  const activeToolLabel = createMemo((): string | null => {
    const calls = agent.activeToolCalls()
    if (!calls?.length) return null
    for (let i = calls.length - 1; i >= 0; i--) {
      const tc = calls[i]
      if (tc.status === 'running' || tc.status === 'pending') {
        return summarizeAction(tc.name, tc.args)
      }
    }
    return null
  })

  const showTokens = (): boolean => settings().ui.showTokenCount
  const toggleTokens = (): void => {
    updateSettings({ ui: { ...settings().ui, showTokenCount: !showTokens() } })
  }

  const tokenDisplay = (): string => {
    const budgetUsed = contextUsage().used
    if (budgetUsed > 0) return fmt(budgetUsed)
    return '0'
  }

  const percentage = (): number => contextUsage().percentage

  const barColor = (): string => {
    const pct = percentage()
    if (pct >= 90) return 'var(--error)'
    if (pct >= 70) return 'var(--warning)'
    return 'var(--success, #22c55e)'
  }

  const contextTooltip = (): string => {
    const used = contextUsage().used
    const limit = contextUsage().total
    const pct = percentage().toFixed(0)
    const fmtK = (n: number) => (n >= 1000 ? `${(n / 1000).toFixed(1)}K` : String(n))
    if (limit > 0) {
      return `Context: ${fmtK(used)} / ${fmtK(limit)} tokens (${pct}% used)`
    }
    return `Context: ${fmtK(used)} tokens used (${pct}%)`
  }

  const isRunning = () => agent.isRunning()

  // Shared text size class — uses design token
  const textSm = 'text-[var(--text-xs)]'

  return (
    <div class={`flex items-center gap-1.5 shrink-0 ${textSm}`}>
      {/* ── STREAMING STATE: elapsed · turn · tool · queued · progress bar ── */}
      <Show when={isRunning()}>
        {/* 1. Elapsed + spinner */}
        <span class="inline-flex items-center gap-1 text-[var(--accent)]">
          <Loader2 class="w-2.5 h-2.5 animate-spin" />
          <span class="tabular-nums">{formatSeconds(elapsedSec())}</span>
        </span>

        {/* 2. Turn counter */}
        <Show when={agent.currentTurn() > 0}>
          <span class="text-[var(--text-muted)]">&middot;</span>
          <span class="tabular-nums text-[var(--accent)]">T{agent.currentTurn()}</span>
        </Show>

        {/* 3. Active tool label (max-w expanded, basename for edit/write) */}
        <Show when={activeToolLabel()}>
          <span class="text-[var(--text-muted)]">&middot;</span>
          <span class="text-[var(--text-muted)] truncate max-w-[200px]" title={activeToolLabel()!}>
            {activeToolLabel()}
          </span>
        </Show>

        {/* 4. Queued messages */}
        <Show when={agent.queuedCount() > 0}>
          <span class="text-[var(--text-muted)]">&middot;</span>
          <span
            class="inline-flex items-center gap-0.5 text-[var(--accent)]"
            title={`${agent.queuedCount()} queued message(s) — /queue to view`}
          >
            <MessageSquare class="w-2.5 h-2.5" />
            <span class="tabular-nums">{agent.queuedCount()}</span>
          </span>
        </Show>

        {/* 5. Context progress bar (always visible during streaming) */}
        <span class="text-[var(--text-muted)]">&middot;</span>
        <div
          class="w-16 h-1.5 bg-[var(--surface-raised)] rounded-full overflow-hidden cursor-default"
          title={contextTooltip()}
        >
          <div
            class="h-full rounded-full transition-all duration-500"
            style={{
              width: `${Math.min(100, percentage())}%`,
              'background-color': barColor(),
            }}
          />
        </div>
      </Show>

      {/* ── IDLE STATE: token count · cost · context % · [compact] · [diagnostics] ── */}
      <Show when={!isRunning()}>
        {/* 1. Token count (togglable) */}
        <button
          type="button"
          onClick={toggleTokens}
          class="inline-flex items-center gap-1 hover:text-[var(--text-secondary)] transition-colors"
          title={showTokens() ? 'Hide token details' : 'Show token details'}
        >
          <Activity class="w-3 h-3" />
          <span class="tabular-nums">{tokenDisplay()}</span>
        </button>

        {/* 2. Session cost */}
        <Show when={sessionTokenStats().totalCost > 0}>
          <span class="text-[var(--text-muted)]">&middot;</span>
          <span
            class="tabular-nums text-[var(--success)]"
            title={`Session total: ${formatCost(sessionTokenStats().totalCost)}`}
          >
            {formatCost(sessionTokenStats().totalCost)}
          </span>
        </Show>

        {/* 3. Context bar + % (always visible when showTokens) */}
        <Show when={showTokens()}>
          <span class="text-[var(--text-muted)]">&middot;</span>
          <div
            class="w-16 h-1.5 bg-[var(--surface-raised)] rounded-full overflow-hidden cursor-default"
            title={contextTooltip()}
          >
            <div
              class="h-full rounded-full transition-all duration-500"
              style={{
                width: `${Math.min(100, percentage())}%`,
                'background-color': barColor(),
              }}
            />
          </div>
          <span
            class="tabular-nums cursor-default"
            title={contextTooltip()}
            classList={{
              'text-[var(--error)]': percentage() >= 90,
              'text-[var(--warning)]': percentage() >= 70 && percentage() < 90,
            }}
          >
            {percentage().toFixed(0)}%
          </span>
        </Show>

        {/* 4. Compact button (pill style, shown at threshold) */}
        <Show when={percentage() >= settings().generation.compactionThreshold}>
          <button
            type="button"
            disabled={isCompacting()}
            onClick={async () => {
              if (isCompacting()) return
              setIsCompacting(true)
              try {
                const budget = getCoreBudget()
                if (!budget) return
                const msgs = messages()
                if (msgs.length <= 4) return
                const coreMessages = msgs.map((m) => ({
                  id: m.id,
                  role: m.role as 'user' | 'assistant' | 'system',
                  content: m.content,
                }))
                const result = await budget.compact(coreMessages)
                if (result.tokensSaved === 0) return
                const keptIds = new Set(result.messages.map((m) => m.id))
                sessionStore.setMessages(msgs.filter((m) => keptIds.has(m.id)))
                budget.clear()
                for (const m of result.messages) budget.addMessage(m.id, m.content)
                window.dispatchEvent(
                  new CustomEvent('ava:compacted', {
                    detail: {
                      removed: result.originalCount - result.compactedCount,
                      tokensSaved: result.tokensSaved,
                    },
                  })
                )
              } catch (err) {
                const msg = err instanceof Error ? err.message : 'Unknown error'
                notify.error('Compaction failed', msg)
              } finally {
                setIsCompacting(false)
              }
            }}
            class="px-2 py-0.5 text-[var(--text-xs)] border border-[var(--warning)] rounded-full bg-[var(--warning-subtle,rgba(234,179,8,0.1))] text-[var(--warning)] hover:text-[var(--accent)] transition-colors disabled:opacity-50 disabled:cursor-not-allowed"
            title="Compact context now"
          >
            <Show when={isCompacting()} fallback="Compact">
              <Loader2 class="w-2.5 h-2.5 animate-spin inline" />
            </Show>
          </button>
        </Show>

        {/* 5. LSP diagnostics (errors/warnings only — low noise) */}
        <Show when={hasDiagnostics()}>
          <span class="text-[var(--text-muted)]">&middot;</span>
          <span class="inline-flex items-center gap-1">
            <Show when={diagnostics().errors > 0}>
              <span class="inline-flex items-center gap-0.5 text-[var(--error)]">
                <AlertCircle class="w-2.5 h-2.5" />
                {diagnostics().errors}
              </span>
            </Show>
            <Show when={diagnostics().warnings > 0}>
              <span class="inline-flex items-center gap-0.5 text-[var(--warning)]">
                <AlertTriangle class="w-2.5 h-2.5" />
                {diagnostics().warnings}
              </span>
            </Show>
          </span>
        </Show>
      </Show>

      {/* Stash indicator — shown in both states when present */}
      <Show when={props.stashSize() > 0}>
        <span class="text-[var(--text-muted)]">&middot;</span>
        <span
          class="inline-flex items-center gap-0.5 text-[var(--accent)]"
          title={`${props.stashSize()} stashed prompt(s) — Ctrl+Shift+R to restore`}
        >
          <Archive class="w-2.5 h-2.5" />
          <span class="tabular-nums">{props.stashSize()}</span>
        </span>
      </Show>
    </div>
  )
}
