/**
 * Chat Title Bar
 *
 * Minimal header: session title + mode badge on left, token counter on right.
 * 52px height, subtle bottom border.
 */

import type { Component } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useSession } from '../../stores/session'

/** Format token count as compact string: 12.4k, 200k, 1.2m */
function fmtTokens(n: number): string {
  if (n >= 1_000_000) return `${(n / 1_000_000).toFixed(1)}m`
  if (n >= 1_000) return `${(n / 1_000).toFixed(1)}k`
  return n.toString()
}

export const ChatTitleBar: Component = () => {
  const { currentSession, contextUsage } = useSession()
  const agent = useAgent()

  const sessionTitle = (): string => {
    const s = currentSession()
    if (s?.name) return s.name
    return 'New Chat'
  }

  const modeLabel = (): string => (agent.isPlanMode() ? 'Plan' : 'Code')

  const tokenDisplay = (): string => {
    const ctx = contextUsage()
    if (!ctx.total) return ''
    return `${fmtTokens(ctx.used)} / ${fmtTokens(ctx.total)}`
  }

  return (
    <div
      class="
        flex items-center justify-between
        h-[52px] min-h-[52px]
        px-5
        border-b border-[var(--border-subtle)]
        select-none
      "
    >
      {/* Left: session title + mode badge */}
      <div class="flex items-center gap-2.5 min-w-0">
        <span
          class="truncate text-sm font-medium text-[var(--text-primary)]"
          style={{ 'font-family': "var(--font-ui, 'Geist', system-ui, sans-serif)" }}
        >
          {sessionTitle()}
        </span>

        <span
          class="
            shrink-0
            rounded px-1.5 py-[2px]
            bg-[var(--alpha-white-8)]
            text-[10px] leading-tight text-[var(--text-muted)]
          "
          style={{ 'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)" }}
        >
          {modeLabel()}
        </span>
      </div>

      {/* Right: token counter only */}
      <span
        class="tabular-nums text-[11px] text-[var(--text-muted)]"
        style={{ 'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)" }}
      >
        {tokenDisplay()}
      </span>
    </div>
  )
}
