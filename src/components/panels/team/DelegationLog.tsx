/**
 * Delegation Log
 *
 * Chronological list of delegation events between team members.
 * Collapsible (default collapsed) with timestamp, from/to, task, and status badge.
 */

import { ChevronDown, ChevronRight } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { DelegationEvent } from '../../../types/team.js'

// ============================================================================
// Helpers
// ============================================================================

const statusColors: Record<DelegationEvent['status'], string> = {
  pending: 'var(--text-muted)',
  in_progress: 'var(--accent)',
  completed: 'var(--success)',
  failed: 'var(--error)',
}

const statusLabels: Record<DelegationEvent['status'], string> = {
  pending: 'Pending',
  in_progress: 'Active',
  completed: 'Done',
  failed: 'Failed',
}

function formatTime(timestamp: number): string {
  const d = new Date(timestamp)
  return d.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

function formatDuration(ms: number): string {
  if (ms >= 60_000) return `${(ms / 60_000).toFixed(1)}m`
  if (ms >= 1_000) return `${(ms / 1_000).toFixed(1)}s`
  return `${ms}ms`
}

// ============================================================================
// Main Component
// ============================================================================

export const DelegationLog: Component<{ events: DelegationEvent[] }> = (props) => {
  const [expanded, setExpanded] = createSignal(false)

  return (
    <div class="border-t border-[var(--border-subtle)]">
      {/* Toggle header */}
      <button
        type="button"
        onClick={() => setExpanded(!expanded())}
        class="w-full flex items-center gap-2 px-3 py-1.5 hover:bg-[var(--alpha-white-3)] transition-colors"
      >
        <Show
          when={expanded()}
          fallback={<ChevronRight class="w-3 h-3 text-[var(--text-muted)]" />}
        >
          <ChevronDown class="w-3 h-3 text-[var(--text-muted)]" />
        </Show>
        <span class="font-[var(--font-ui-mono)] text-[10px] font-medium text-[var(--text-secondary)] uppercase tracking-wider">
          Delegation Log
        </span>
        <span class="font-[var(--font-ui-mono)] text-[9px] text-[var(--text-muted)]">
          ({props.events.length})
        </span>
      </button>

      {/* Event list */}
      <Show when={expanded()}>
        <div class="max-h-[200px] overflow-y-auto scrollbar-none">
          <Show
            when={props.events.length > 0}
            fallback={
              <div class="px-3 py-2 text-center">
                <span class="font-[var(--font-ui-mono)] text-[10px] text-[var(--text-muted)]">
                  No delegations yet
                </span>
              </div>
            }
          >
            <For each={props.events}>
              {(event) => (
                <div class="flex items-start gap-2 px-3 py-1.5 border-b border-[var(--border-subtle)] last:border-b-0 hover:bg-[var(--alpha-white-3)]">
                  {/* Status dot */}
                  <span
                    class="w-1.5 h-1.5 rounded-full flex-shrink-0 mt-1"
                    style={{ background: statusColors[event.status] }}
                  />

                  {/* Content */}
                  <div class="flex-1 min-w-0">
                    <div class="flex items-center gap-1 flex-wrap">
                      <span class="font-[var(--font-ui-mono)] text-[10px] font-medium text-[var(--text-primary)]">
                        {event.fromMember}
                      </span>
                      <span class="font-[var(--font-ui-mono)] text-[9px] text-[var(--text-muted)]">
                        &rarr;
                      </span>
                      <span class="font-[var(--font-ui-mono)] text-[10px] font-medium text-[var(--text-primary)]">
                        {event.toMember}
                      </span>
                    </div>
                    <p class="font-[var(--font-ui-mono)] text-[9px] text-[var(--text-secondary)] truncate mt-0.5">
                      {event.task}
                    </p>
                  </div>

                  {/* Meta */}
                  <div class="flex flex-col items-end gap-0.5 flex-shrink-0">
                    <span
                      class="font-[var(--font-ui-mono)] text-[8px] tracking-wider font-medium px-1 py-px rounded-[var(--radius-sm)]"
                      style={{
                        color: statusColors[event.status],
                        background: `color-mix(in srgb, ${statusColors[event.status]} 15%, transparent)`,
                      }}
                    >
                      {statusLabels[event.status]}
                    </span>
                    <span class="font-[var(--font-ui-mono)] text-[8px] text-[var(--text-muted)]">
                      {formatTime(event.timestamp)}
                    </span>
                    <Show when={event.duration !== undefined}>
                      <span class="font-[var(--font-ui-mono)] text-[8px] text-[var(--text-muted)]">
                        {formatDuration(event.duration!)}
                      </span>
                    </Show>
                  </div>
                </div>
              )}
            </For>
          </Show>
        </div>
      </Show>
    </div>
  )
}
