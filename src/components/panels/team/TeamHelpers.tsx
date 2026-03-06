/**
 * Team Panel Helper Components
 *
 * Small UI pieces used in TeamPanel: status labels, SVG delegation
 * flow lines, parallel-execution badge, and phase timeline bar.
 */

import { GitBranch } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { TeamGroup } from '../../../types/team'

// ============================================================================
// Status helpers
// ============================================================================

export const statusLabel: Record<string, string> = {
  idle: 'Idle',
  working: 'Working',
  reporting: 'Reporting',
  done: 'Done',
  error: 'Error',
}

// ============================================================================
// Delegation Flow Lines (SVG)
// ============================================================================

export const DelegationLines: Component<{ teamCount: number; groups: TeamGroup[] }> = (props) => {
  // Simple vertical connector from Team Lead down to team cards
  // Each line gets animated dash pattern when the team is active
  const lineStartY = 4
  const lineEndY = 20
  const midX = 12

  return (
    <Show when={props.teamCount > 0}>
      <div class="px-4 py-0.5">
        <svg width="100%" height="24" class="overflow-visible" aria-hidden="true">
          {/* Vertical trunk line from Team Lead */}
          <line
            x1={midX}
            y1={lineStartY}
            x2={midX}
            y2={lineEndY}
            stroke="var(--border-default)"
            stroke-width="1.5"
            stroke-dasharray={props.groups.some((g) => g.status === 'working') ? '4 3' : 'none'}
            class={
              props.groups.some((g) => g.status === 'working') ? 'animate-delegation-flow' : ''
            }
          />
          {/* Small circle at junction */}
          <circle cx={midX} cy={lineEndY} r="2.5" fill="var(--border-default)" />
        </svg>
      </div>
    </Show>
  )
}

// ============================================================================
// Parallel Badge
// ============================================================================

export const ParallelBadge: Component<{ count: number }> = (props) => (
  <Show when={props.count >= 2}>
    <div class="flex items-center gap-1 px-2 py-0.5 mx-2 mb-1 rounded-[var(--radius-sm)] bg-[var(--accent-subtle)] border border-[var(--accent-border)]">
      <GitBranch class="w-3 h-3 text-[var(--accent)]" />
      <span class="font-[var(--font-ui-mono)] text-[9px] tracking-wider text-[var(--accent)] font-medium">
        {props.count} PARALLEL
      </span>
    </div>
  </Show>
)

// ============================================================================
// Phase Timeline
// ============================================================================

export type Phase = 'idle' | 'planning' | 'delegating' | 'executing' | 'validating' | 'done'

export const PhaseTimeline: Component<{ currentPhase: Phase }> = (props) => {
  const phases: { id: Phase; label: string }[] = [
    { id: 'planning', label: 'Plan' },
    { id: 'delegating', label: 'Delegate' },
    { id: 'executing', label: 'Execute' },
    { id: 'validating', label: 'Validate' },
    { id: 'done', label: 'Done' },
  ]

  const phaseIndex = () => {
    const idx = phases.findIndex((p) => p.id === props.currentPhase)
    return idx >= 0 ? idx : -1
  }

  return (
    <div class="flex items-center gap-0.5 px-3 py-1.5">
      <For each={phases}>
        {(_phase, idx) => {
          const isActive = () => idx() === phaseIndex()
          const isPast = () => idx() < phaseIndex()

          return (
            <>
              <div
                class={`
                  flex-1 h-1 rounded-full transition-colors duration-300
                  ${isPast() ? 'bg-[var(--accent)]' : isActive() ? 'bg-[var(--accent)]' : 'bg-[var(--surface-sunken)]'}
                `}
              />
              <Show when={idx() < phases.length - 1}>
                <div class="w-0.5" />
              </Show>
            </>
          )
        }}
      </For>
    </div>
  )
}
