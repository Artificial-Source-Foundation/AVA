/**
 * Project Stats View
 *
 * Shows project-level usage statistics: total cost, tokens, sessions,
 * model breakdown table, and daily usage mini-chart.
 */

import { type Component, createMemo, For, Show } from 'solid-js'
import { formatCost } from '../../lib/cost'
import type {
  DailyUsageEntry,
  ModelBreakdownEntry,
  ProjectUsageStats,
} from '../../services/database'

interface ProjectStatsViewProps {
  stats: ProjectUsageStats | null
  modelBreakdown: ModelBreakdownEntry[]
  dailyUsage: DailyUsageEntry[]
  loading: boolean
}

const fmt = (n: number) => (n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n))

export const ProjectStatsView: Component<ProjectStatsViewProps> = (props) => {
  const maxTokens = createMemo(() => {
    const max = Math.max(...props.dailyUsage.map((d) => d.tokens), 1)
    return max
  })

  return (
    <div class="space-y-4">
      {/* Summary cards */}
      <Show
        when={!props.loading && props.stats}
        fallback={
          <div class="grid grid-cols-2 gap-2">
            <div class="h-14 animate-pulse rounded-[var(--radius-sm)] bg-[var(--surface-raised)]" />
            <div class="h-14 animate-pulse rounded-[var(--radius-sm)] bg-[var(--surface-raised)]" />
            <div class="h-14 animate-pulse rounded-[var(--radius-sm)] bg-[var(--surface-raised)]" />
            <div class="h-14 animate-pulse rounded-[var(--radius-sm)] bg-[var(--surface-raised)]" />
          </div>
        }
      >
        <div class="grid grid-cols-2 gap-2 text-[11px]">
          <div class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-3 py-2">
            <p class="text-[var(--text-muted)]">Total Cost</p>
            <p class="text-[var(--text-primary)] text-sm font-medium">
              {formatCost(props.stats!.totalCost)}
            </p>
          </div>
          <div class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-3 py-2">
            <p class="text-[var(--text-muted)]">Total Tokens</p>
            <p class="text-[var(--text-primary)] text-sm font-medium">
              {fmt(props.stats!.totalTokens)}
            </p>
          </div>
          <div class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-3 py-2">
            <p class="text-[var(--text-muted)]">Sessions</p>
            <p class="text-[var(--text-primary)] text-sm font-medium">
              {props.stats!.sessionCount}
            </p>
          </div>
          <div class="rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-3 py-2">
            <p class="text-[var(--text-muted)]">Messages</p>
            <p class="text-[var(--text-primary)] text-sm font-medium">
              {fmt(props.stats!.messageCount)}
            </p>
          </div>
        </div>
      </Show>

      {/* Model breakdown */}
      <Show when={props.modelBreakdown.length > 0}>
        <div>
          <p class="mb-2 text-[10px] uppercase tracking-wide text-[var(--text-muted)]">
            Model breakdown
          </p>
          <div class="space-y-1">
            <For each={props.modelBreakdown}>
              {(entry) => (
                <div class="flex items-center justify-between rounded-[var(--radius-sm)] border border-[var(--border-subtle)] px-2.5 py-1.5 text-[11px]">
                  <span class="text-[var(--text-secondary)] font-mono truncate max-w-[180px]">
                    {entry.model}
                  </span>
                  <div class="flex items-center gap-3 text-[var(--text-muted)] shrink-0">
                    <span>{entry.usageCount} calls</span>
                    <span>{fmt(entry.totalTokens)} tok</span>
                    <span>{formatCost(entry.totalCost)}</span>
                  </div>
                </div>
              )}
            </For>
          </div>
        </div>
      </Show>

      {/* Daily usage chart */}
      <Show when={props.dailyUsage.length > 0}>
        <div>
          <p class="mb-2 text-[10px] uppercase tracking-wide text-[var(--text-muted)]">
            Daily usage (tokens)
          </p>
          <div class="flex items-end gap-px h-16 rounded-[var(--radius-sm)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-2 py-2">
            <For each={props.dailyUsage}>
              {(day) => {
                const height = () => Math.max(2, (day.tokens / maxTokens()) * 100)
                return (
                  <div
                    class="flex-1 min-w-[3px] max-w-[12px] rounded-t-sm bg-[var(--accent)] opacity-70 hover:opacity-100 transition-opacity"
                    style={{ height: `${height()}%` }}
                    title={`${day.date}: ${fmt(day.tokens)} tokens, ${formatCost(day.cost)}`}
                  />
                )
              }}
            </For>
          </div>
        </div>
      </Show>
    </div>
  )
}
