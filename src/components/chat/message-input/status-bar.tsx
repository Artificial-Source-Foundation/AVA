/**
 * Status Bar
 *
 * Right-side context/token info strip showing token usage, progress bar,
 * stash indicator, diagnostics, session cost, streaming estimates, message count,
 * and agent execution info (elapsed time, turn count, current tool).
 */

import { Activity, AlertCircle, AlertTriangle, Archive, Loader2, MessageSquare } from 'lucide-solid'
import {
  type Accessor,
  type Component,
  createEffect,
  createMemo,
  createSignal,
  on,
  onCleanup,
  Show,
} from 'solid-js'
import { useAgent } from '../../../hooks/useAgent'
import { formatCost } from '../../../lib/cost'
import { getCoreBudget } from '../../../services/core-bridge'
import { useDiagnostics } from '../../../stores/diagnostics'
import { useSession } from '../../../stores/session'
import { useSettings } from '../../../stores/settings'
import { summarizeAction } from '../tool-call-utils'

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const fmt = (n: number): string => (n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n))

function formatElapsedSeconds(sec: number): string {
  if (sec < 60) return `${sec}s`
  const m = Math.floor(sec / 60)
  const s = sec % 60
  return `${m}m${s > 0 ? ` ${s}s` : ''}`
}

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

  const { contextUsage, sessionTokenStats, messages } = sessionStore

  // Elapsed time during streaming
  const [elapsedSec, setElapsedSec] = createSignal(0)
  createEffect(
    on(
      () => agent.streamingStartedAt(),
      (startedAt) => {
        if (!startedAt) {
          setElapsedSec(0)
          return
        }
        setElapsedSec(0)
        const interval = setInterval(() => {
          setElapsedSec(Math.floor((Date.now() - startedAt) / 1000))
        }, 1000)
        onCleanup(() => clearInterval(interval))
      }
    )
  )

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
    const real = sessionTokenStats().total
    if (real > 0) return fmt(real)
    return fmt(contextUsage().used)
  }

  const percentage = (): number => {
    const real = sessionTokenStats().total
    const limit = contextUsage().total
    if (real > 0 && limit > 0) return Math.min(100, (real / limit) * 100)
    return contextUsage().percentage
  }

  const barColor = (): string => {
    const pct = percentage()
    if (pct > 80) return 'var(--warning)'
    if (pct > 60) return 'var(--text-muted)'
    return 'var(--accent)'
  }

  const msgCount = (): number => messages().length

  return (
    <div class="flex items-center gap-1.5 shrink-0">
      {/* Agent execution info — elapsed, turn, tool */}
      <Show when={agent.isRunning()}>
        <span class="inline-flex items-center gap-1 text-[var(--accent)]">
          <Loader2 class="w-2.5 h-2.5 animate-spin" />
          <span class="tabular-nums">{formatElapsedSeconds(elapsedSec())}</span>
        </span>
        <Show when={agent.currentTurn() > 0}>
          <span class="text-[var(--text-muted)]">&middot;</span>
          <span class="tabular-nums text-[var(--accent)]">T{agent.currentTurn()}</span>
        </Show>
        <Show when={activeToolLabel()}>
          <span class="text-[var(--text-muted)]">&middot;</span>
          <span class="text-[var(--text-muted)] truncate max-w-[120px]" title={activeToolLabel()!}>
            {activeToolLabel()}
          </span>
        </Show>
        <span class="text-[var(--text-muted)]">&middot;</span>
      </Show>

      <button
        type="button"
        onClick={toggleTokens}
        class="inline-flex items-center gap-1 hover:text-[var(--text-secondary)] transition-colors"
        title={showTokens() ? 'Hide token details' : 'Show token details'}
      >
        <Activity class="w-3 h-3" />
        <span class="tabular-nums">{tokenDisplay()}</span>
      </button>

      {/* Stash indicator */}
      <Show when={props.stashSize() > 0}>
        <span
          class="inline-flex items-center gap-0.5 text-[var(--accent)]"
          title={`${props.stashSize()} stashed prompt(s) — Ctrl+Shift+R to restore`}
        >
          <Archive class="w-2.5 h-2.5" />
          <span class="tabular-nums">{props.stashSize()}</span>
        </span>
        <span class="text-[var(--text-muted)]">&middot;</span>
      </Show>

      {/* Context warning icon at 80%+ */}
      <Show when={percentage() >= 80}>
        <span title={`Context ${percentage().toFixed(0)}% full`}>
          <AlertTriangle class="w-3 h-3 text-[var(--warning)]" />
        </span>
      </Show>

      {/* Compact button */}
      <Show when={percentage() >= settings().generation.compactionThreshold}>
        <button
          type="button"
          onClick={async () => {
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
          }}
          class="text-[10px] text-[var(--warning)] hover:text-[var(--accent)] transition-colors"
          title="Compact context now"
        >
          Compact
        </button>
      </Show>

      {/* Progress bar + percentage (togglable) */}
      <Show when={showTokens()}>
        <div class="w-10 h-1 bg-[var(--surface-raised)] rounded-full overflow-hidden">
          <div
            class="h-full rounded-full transition-all duration-300"
            style={{
              width: `${Math.min(100, percentage())}%`,
              'background-color': barColor(),
            }}
          />
        </div>
        <span class="tabular-nums" classList={{ 'text-[var(--warning)]': percentage() >= 80 }}>
          {percentage().toFixed(0)}%
        </span>
      </Show>

      {/* LSP diagnostics */}
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

      {/* Session cost */}
      <Show when={sessionTokenStats().totalCost > 0}>
        <span class="text-[var(--text-muted)]">&middot;</span>
        <span class="tabular-nums text-[var(--success)]">
          {formatCost(sessionTokenStats().totalCost)}
        </span>
      </Show>

      {/* Streaming token estimate */}
      <Show when={props.isStreaming() && props.streamingTokenEstimate() > 0}>
        <span class="text-[var(--text-muted)]">&middot;</span>
        <span class="text-[var(--accent)] animate-pulse tabular-nums">
          +{fmt(props.streamingTokenEstimate())}
        </span>
      </Show>

      {/* Message count */}
      <Show when={msgCount() > 0}>
        <span class="text-[var(--text-muted)]">&middot;</span>
        <span class="inline-flex items-center gap-0.5 tabular-nums">
          <MessageSquare class="w-2.5 h-2.5" />
          {msgCount()}
        </span>
      </Show>
    </div>
  )
}
