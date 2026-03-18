/**
 * Bottom Status Bar
 *
 * Persistent bar at the bottom of the app shell with:
 * - Left: Model selector pill, thinking level badge, separator, timer/share/mic icons
 * - Right: Token count, context %, cost, Act/Plan toggle, Team button
 *
 * Matches the Soft Zinc design system.
 */

import { ArrowDown, ChevronDown, Clock, Mic, Share2, Users } from 'lucide-solid'
import { type Component, createMemo, Show } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { formatCost } from '../../lib/cost'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import type { ReasoningEffort } from '../../stores/settings/settings-types'

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const THINKING_LABELS: Record<ReasoningEffort, string> = {
  off: 'Off',
  none: 'None',
  minimal: 'Min',
  low: 'Low',
  medium: 'Med',
  high: 'High',
  xhigh: 'XHigh',
  max: 'Max',
}

const THINKING_CYCLE: ReasoningEffort[] = ['off', 'low', 'medium', 'high', 'max']

/** Thin vertical separator */
const Sep: Component = () => <span class="w-px h-3.5 bg-[var(--border-subtle)] shrink-0 mx-1" />

const fmtTokens = (n: number): string => (n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n))

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const BottomStatusBar: Component = () => {
  const { settings, updateSettings } = useSettings()
  const { openModelBrowser } = useLayout()
  const sessionStore = useSession()
  const agent = useAgent()

  const { contextUsage, sessionTokenStats, selectedModel, selectedProvider } = sessionStore

  // Model display: "Provider | Model"
  const modelDisplay = createMemo((): string => {
    const modelId = selectedModel()
    const provId = selectedProvider()
    if (provId) {
      const provider = settings().providers.find((p) => p.id === provId)
      const model = provider?.models.find((m) => m.id === modelId)
      if (provider && model) return `${provider.name} | ${model.name}`
    }
    for (const provider of settings().providers) {
      const model = provider.models.find((m) => m.id === modelId)
      if (model) return `${provider.name} | ${model.name}`
    }
    if (modelId.length > 30) return `${modelId.slice(0, 27)}...`
    return modelId || 'No model'
  })

  // Thinking level
  const thinkingLevel = (): ReasoningEffort => settings().generation.reasoningEffort

  const cycleThinking = (): void => {
    const current = thinkingLevel()
    const idx = THINKING_CYCLE.indexOf(current)
    const next = THINKING_CYCLE[(idx + 1) % THINKING_CYCLE.length]!
    updateSettings({ generation: { ...settings().generation, reasoningEffort: next } })
  }

  // Context percentage
  const contextPct = createMemo((): number => {
    const real = sessionTokenStats().total
    const limit = contextUsage().total
    if (real > 0 && limit > 0) return Math.min(100, (real / limit) * 100)
    return contextUsage().percentage
  })

  // Token count (input tokens)
  const tokenCount = createMemo((): number => {
    const real = sessionTokenStats().total
    if (real > 0) return real
    return contextUsage().used
  })

  // Session cost
  const cost = createMemo((): number => sessionTokenStats().totalCost)

  // Plan mode
  const isPlanMode = (): boolean => agent.isPlanMode()

  return (
    <div
      class="
        flex items-center justify-between
        h-7 px-3 flex-shrink-0
        bg-[var(--gray-1)]
        border-t border-[var(--border-subtle)]
        text-[11px] font-[var(--font-ui-mono)]
        text-[var(--text-muted)]
        select-none
      "
    >
      {/* Left section */}
      <div class="flex items-center gap-1.5">
        {/* Model selector pill */}
        <button
          type="button"
          onClick={openModelBrowser}
          class="
            flex items-center gap-1 px-2 py-0.5
            bg-[var(--surface-raised)]
            border border-[var(--border-subtle)]
            rounded-[var(--radius-full)]
            hover:border-[var(--accent-muted)]
            transition-colors
            text-[var(--text-secondary)]
          "
          title="Switch model"
        >
          <ChevronDown class="w-3 h-3" />
          <span class="truncate max-w-[200px]">{modelDisplay()}</span>
        </button>

        {/* Thinking level badge */}
        <Show when={thinkingLevel() !== 'off'}>
          <button
            type="button"
            onClick={cycleThinking}
            class="
              flex items-center gap-1 px-2 py-0.5
              rounded-[var(--radius-full)]
              bg-[var(--accent-subtle)]
              text-[var(--accent)]
              hover:bg-[var(--violet-alpha-30)]
              transition-colors
            "
            title={`Thinking: ${THINKING_LABELS[thinkingLevel()]} (click to cycle)`}
          >
            <span style={{ 'font-size': '11px' }}>&#x1F9E0;</span>
            <span class="font-medium">{THINKING_LABELS[thinkingLevel()]}</span>
          </button>
        </Show>
        <Show when={thinkingLevel() === 'off'}>
          <button
            type="button"
            onClick={cycleThinking}
            class="
              flex items-center gap-1 px-2 py-0.5
              rounded-[var(--radius-full)]
              hover:bg-[var(--alpha-white-5)]
              transition-colors
            "
            title="Enable thinking (click to cycle)"
          >
            <span style={{ 'font-size': '11px', opacity: '0.5' }}>&#x1F9E0;</span>
            <span>Off</span>
          </button>
        </Show>

        <Sep />

        {/* Utility icons: timer, share, mic */}
        <Show when={agent.isRunning()}>
          <span class="flex items-center gap-1 text-[var(--accent)]" title="Agent running">
            <Clock class="w-3 h-3" />
          </span>
        </Show>
        <button
          type="button"
          class="p-0.5 rounded-[var(--radius-sm)] hover:bg-[var(--alpha-white-5)] hover:text-[var(--text-secondary)] transition-colors"
          title="Share conversation"
        >
          <Share2 class="w-3 h-3" />
        </button>
        <button
          type="button"
          class="p-0.5 rounded-[var(--radius-sm)] hover:bg-[var(--alpha-white-5)] hover:text-[var(--text-secondary)] transition-colors"
          title="Voice input"
        >
          <Mic class="w-3 h-3" />
        </button>
      </div>

      {/* Right section */}
      <div class="flex items-center gap-1.5">
        {/* Token count */}
        <span
          class="inline-flex items-center gap-0.5 tabular-nums"
          title={`${tokenCount()} tokens used`}
        >
          <ArrowDown class="w-3 h-3" />
          {fmtTokens(tokenCount())}
        </span>

        {/* Context percentage */}
        <span
          class="tabular-nums"
          classList={{
            'text-[var(--warning)]': contextPct() >= 80,
          }}
          title={`Context ${contextPct().toFixed(0)}% used`}
        >
          {contextPct().toFixed(0)}%
        </span>

        {/* Session cost */}
        <Show when={cost() > 0}>
          <span
            class="tabular-nums text-[var(--success)]"
            title={`Session cost: ${formatCost(cost())}`}
          >
            {formatCost(cost())}
          </span>
        </Show>

        <Sep />

        {/* Act / Plan toggle */}
        <button
          type="button"
          onClick={() => agent.togglePlanMode()}
          disabled={agent.isRunning()}
          class="
            relative flex items-center
            h-[20px] w-[72px] rounded-[var(--radius-full)]
            bg-[var(--surface-raised)] border border-[var(--border-subtle)]
            text-[10px] font-semibold
            disabled:opacity-50 disabled:cursor-not-allowed
            overflow-hidden
            transition-colors
          "
          title={isPlanMode() ? 'Plan mode' : 'Act mode'}
        >
          {/* Sliding highlight */}
          <div
            class="
              absolute top-[1px] bottom-[1px] w-[36px]
              rounded-[var(--radius-full)]
              transition-all duration-300 ease-[cubic-bezier(0.4,0,0.2,1)]
            "
            style={{
              left: isPlanMode() ? '1px' : '34px',
              'background-color': 'var(--accent)',
            }}
          />
          <span
            class="relative z-10 flex-1 text-center transition-colors duration-200"
            style={{
              color: !isPlanMode() ? 'white' : 'var(--text-muted)',
            }}
          >
            Act
          </span>
          <span
            class="relative z-10 flex-1 text-center transition-colors duration-200"
            style={{
              color: isPlanMode() ? 'white' : 'var(--text-muted)',
            }}
          >
            Plan
          </span>
        </button>

        {/* Team button */}
        <button
          type="button"
          class="
            flex items-center gap-1 px-2 py-0.5
            rounded-[var(--radius-full)]
            hover:bg-[var(--alpha-white-5)]
            hover:text-[var(--text-secondary)]
            transition-colors
          "
          title="Team (Praxis multi-agent)"
        >
          <Users class="w-3 h-3" />
          <span>Team</span>
        </button>
      </div>
    </div>
  )
}
