/**
 * Team Metrics Footer
 *
 * Aggregate summary for the entire dev team:
 * total tokens, files changed, tool calls, and delegation success ratio.
 */

import { type Component, createMemo } from 'solid-js'
import type { DelegationEvent, TeamMember } from '../../../types/team.js'

// ============================================================================
// Helpers
// ============================================================================

function formatTokens(n: number): string {
  if (n >= 1_000_000) return `${(n / 1_000_000).toFixed(1)}M`
  if (n >= 1_000) return `${(n / 1_000).toFixed(1)}K`
  return String(n)
}

// ============================================================================
// Main Component
// ============================================================================

export const TeamMetrics: Component<{
  members: TeamMember[]
  delegations: DelegationEvent[]
}> = (props) => {
  const totalTokens = createMemo(() => {
    let input = 0
    let output = 0
    for (const m of props.members) {
      if (m.tokenUsage) {
        input += m.tokenUsage.input
        output += m.tokenUsage.output
      }
    }
    return { input, output, total: input + output }
  })

  const totalFiles = createMemo(() => {
    const files = new Set<string>()
    for (const m of props.members) {
      if (m.filesChanged) {
        for (const f of m.filesChanged) files.add(f)
      }
    }
    return files.size
  })

  const totalToolCalls = createMemo(() => {
    let count = 0
    for (const m of props.members) {
      count += m.toolCalls.length
    }
    return count
  })

  const successRatio = createMemo(() => {
    const total = props.delegations.length
    if (total === 0) return null
    const completed = props.delegations.filter((d) => d.status === 'completed').length
    return Math.round((completed / total) * 100)
  })

  return (
    <div class="border-t border-[var(--border-subtle)] px-3 py-1.5">
      <div class="grid grid-cols-4 gap-2">
        {/* Total tokens */}
        <div class="text-center">
          <div class="font-[var(--font-ui-mono)] text-[11px] font-semibold text-[var(--text-primary)]">
            {formatTokens(totalTokens().total)}
          </div>
          <div class="font-[var(--font-ui-mono)] text-[8px] text-[var(--text-muted)] uppercase tracking-wider">
            Tokens
          </div>
        </div>

        {/* Files changed */}
        <div class="text-center">
          <div class="font-[var(--font-ui-mono)] text-[11px] font-semibold text-[var(--text-primary)]">
            {totalFiles()}
          </div>
          <div class="font-[var(--font-ui-mono)] text-[8px] text-[var(--text-muted)] uppercase tracking-wider">
            Files
          </div>
        </div>

        {/* Tool calls */}
        <div class="text-center">
          <div class="font-[var(--font-ui-mono)] text-[11px] font-semibold text-[var(--text-primary)]">
            {totalToolCalls()}
          </div>
          <div class="font-[var(--font-ui-mono)] text-[8px] text-[var(--text-muted)] uppercase tracking-wider">
            Tools
          </div>
        </div>

        {/* Success ratio */}
        <div class="text-center">
          <div
            class="font-[var(--font-ui-mono)] text-[11px] font-semibold"
            style={{
              color:
                successRatio() === null
                  ? 'var(--text-muted)'
                  : successRatio()! >= 80
                    ? 'var(--success)'
                    : successRatio()! >= 50
                      ? 'var(--warning)'
                      : 'var(--error)',
            }}
          >
            {successRatio() === null ? '--' : `${successRatio()}%`}
          </div>
          <div class="font-[var(--font-ui-mono)] text-[8px] text-[var(--text-muted)] uppercase tracking-wider">
            Success
          </div>
        </div>
      </div>
    </div>
  )
}
