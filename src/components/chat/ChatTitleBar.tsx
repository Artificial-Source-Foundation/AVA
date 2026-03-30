/**
 * Chat Title Bar
 *
 * Minimal header: session title + mode badge on left, token counter on right.
 * 52px height, subtle bottom border.
 */

import type { Component } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { ChatHeaderBar } from './ChatHeaderBar'

/** Format token count as compact string: 12.4k, 200k, 1.2m */
function fmtTokens(n: number): string {
  if (n >= 1_000_000) return `${(n / 1_000_000).toFixed(1)}m`
  if (n >= 1_000) return `${(n / 1_000).toFixed(1)}k`
  return n.toString()
}

export const ChatTitleBar: Component = () => {
  const { currentSession, contextUsage } = useSession()
  const { settings } = useSettings()
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

  const contextPercent = (): number => Math.round(contextUsage().percentage)

  const usageColor = (): string => {
    const pct = contextUsage().percentage
    const threshold = settings().generation.compactionThreshold
    if (pct >= threshold) return 'var(--error)'
    if (pct >= Math.max(60, threshold - 10)) return 'var(--warning)'
    return 'var(--success)'
  }

  return (
    <ChatHeaderBar
      title={
        <span
          class="truncate text-sm font-medium text-[var(--text-primary)]"
          style={{ 'font-family': "var(--font-ui, 'Geist', system-ui, sans-serif)" }}
        >
          {sessionTitle()}
        </span>
      }
      leftMeta={
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
      }
      right={
        <div class="flex items-center gap-3">
          <div
            class="flex items-center gap-2"
            title={`${contextPercent()}% of context window used`}
          >
            <div class="h-1.5 w-20 overflow-hidden rounded-full bg-[var(--surface-raised)]">
              <div
                class="h-full rounded-full transition-[width,background-color] duration-200"
                style={{
                  width: `${Math.min(100, contextUsage().percentage)}%`,
                  background: usageColor(),
                }}
              />
            </div>
            <span
              class="tabular-nums text-[11px]"
              style={{
                color: usageColor(),
                'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
              }}
            >
              {contextPercent()}%
            </span>
          </div>
          <span
            class="tabular-nums text-[11px] text-[var(--text-muted)]"
            style={{ 'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)" }}
          >
            {tokenDisplay()}
          </span>
        </div>
      }
    />
  )
}
