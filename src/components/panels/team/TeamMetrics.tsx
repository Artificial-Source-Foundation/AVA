/**
 * Team Metrics Footer
 *
 * Aggregate summary: Tokens, Files, Cost, Success rate.
 * Matches Pencil design: TEAM METRICS header (9px, #3F3F46),
 * 4-column grid with JetBrains Mono values.
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

function formatCost(tokens: number): string {
  // Rough estimate: $3/M input, $15/M output — average ~$9/M
  const cost = (tokens / 1_000_000) * 9
  if (cost < 0.01) return '$0.00'
  return `$${cost.toFixed(2)}`
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

  const successRatio = createMemo(() => {
    const total = props.delegations.length
    if (total === 0) return null
    const completed = props.delegations.filter((d) => d.status === 'completed').length
    return Math.round((completed / total) * 100)
  })

  return (
    <div class="px-4 py-3" style={{ 'border-top': '1px solid #27272A' }}>
      {/* Section header */}
      <div
        class="text-[9px] font-semibold mb-2"
        style={{ color: '#3F3F46', 'letter-spacing': '0.8px' }}
      >
        TEAM METRICS
      </div>

      {/* 4-column grid */}
      <div class="flex gap-4">
        {/* Tokens */}
        <div class="flex flex-col gap-0.5">
          <span class="text-[9px]" style={{ color: '#52525B' }}>
            Tokens
          </span>
          <span class="text-[12px] font-semibold text-[#FAFAFA] font-['JetBrains_Mono',monospace]">
            {formatTokens(totalTokens().total)}
          </span>
        </div>

        {/* Files */}
        <div class="flex flex-col gap-0.5">
          <span class="text-[9px]" style={{ color: '#52525B' }}>
            Files
          </span>
          <span class="text-[12px] font-semibold text-[#FAFAFA] font-['JetBrains_Mono',monospace]">
            {totalFiles()}
          </span>
        </div>

        {/* Cost */}
        <div class="flex flex-col gap-0.5">
          <span class="text-[9px]" style={{ color: '#52525B' }}>
            Cost
          </span>
          <span class="text-[12px] font-semibold text-[#FAFAFA] font-['JetBrains_Mono',monospace]">
            {formatCost(totalTokens().total)}
          </span>
        </div>

        {/* Success */}
        <div class="flex flex-col gap-0.5">
          <span class="text-[9px]" style={{ color: '#52525B' }}>
            Success
          </span>
          <span
            class="text-[12px] font-semibold font-['JetBrains_Mono',monospace]"
            style={{
              color:
                successRatio() === null
                  ? '#52525B'
                  : successRatio()! >= 80
                    ? 'var(--success)'
                    : successRatio()! >= 50
                      ? 'var(--warning)'
                      : 'var(--error)',
            }}
          >
            {successRatio() === null ? '--' : `${successRatio()}%`}
          </span>
        </div>
      </div>
    </div>
  )
}
