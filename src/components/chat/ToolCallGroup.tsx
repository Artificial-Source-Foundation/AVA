/**
 * Tool Call Group
 *
 * Renders a group of tool calls for a single assistant message.
 * Shows summary header (count, status) and lists individual ToolCallCards.
 * Collapsed by default for completed groups, expanded for active ones.
 */

import { ChevronDown, ChevronRight, Loader2, Wrench } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import type { ToolCall } from '../../types'
import { ToolCallCard } from './ToolCallCard'

interface ToolCallGroupProps {
  toolCalls: ToolCall[]
  isStreaming?: boolean
}

export const ToolCallGroup: Component<ToolCallGroupProps> = (props) => {
  const isActive = createMemo(
    () =>
      props.isStreaming ||
      props.toolCalls.some((t) => t.status === 'running' || t.status === 'pending')
  )

  // Expand by default while active, collapse when done
  const [manualToggle, setManualToggle] = createSignal<boolean | null>(null)
  const expanded = () => manualToggle() ?? isActive()

  const counts = createMemo(() => {
    let success = 0
    let error = 0
    let running = 0
    let pending = 0
    for (const tc of props.toolCalls) {
      if (tc.status === 'success') success++
      else if (tc.status === 'error') error++
      else if (tc.status === 'running') running++
      else pending++
    }
    return { success, error, running, pending, total: props.toolCalls.length }
  })

  const statusSummary = () => {
    const c = counts()
    if (c.running > 0 || c.pending > 0) {
      return `${c.running + c.pending} running`
    }
    const parts: string[] = []
    if (c.success > 0) parts.push(`${c.success} passed`)
    if (c.error > 0) parts.push(`${c.error} failed`)
    return parts.join(', ')
  }

  const totalDuration = createMemo(() => {
    if (isActive()) return null
    let min = Number.POSITIVE_INFINITY
    let max = 0
    for (const tc of props.toolCalls) {
      if (tc.startedAt < min) min = tc.startedAt
      if (tc.completedAt && tc.completedAt > max) max = tc.completedAt
    }
    if (max === 0) return null
    const ms = max - min
    return ms < 1000 ? `${ms}ms` : `${(ms / 1000).toFixed(1)}s`
  })

  return (
    <div class="my-2">
      {/* Group header */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        class="flex items-center gap-2 px-2 py-1.5 rounded-[var(--radius-md)] cursor-pointer select-none hover:bg-[var(--bg-hover)] transition-colors duration-[var(--duration-fast)]"
        onClick={() => setManualToggle((v) => (v === null ? !isActive() : !v))}
        onKeyDown={(e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault()
            setManualToggle((v) => (v === null ? !isActive() : !v))
          }
        }}
      >
        <Show
          when={!isActive()}
          fallback={<Loader2 class="w-4 h-4 animate-spin text-[var(--accent-text)]" />}
        >
          <Wrench class="w-4 h-4 text-[var(--text-muted)]" />
        </Show>

        <span class="text-xs font-medium text-[var(--text-secondary)]">
          {counts().total} tool {counts().total === 1 ? 'call' : 'calls'}
        </span>

        <span class="text-xs text-[var(--text-muted)]">{statusSummary()}</span>

        <span class="flex-1" />

        <Show when={totalDuration()}>
          <span class="text-xs text-[var(--text-muted)] tabular-nums">{totalDuration()}</span>
        </Show>

        <Show
          when={expanded()}
          fallback={<ChevronRight class="w-3.5 h-3.5 text-[var(--text-muted)]" />}
        >
          <ChevronDown class="w-3.5 h-3.5 text-[var(--text-muted)]" />
        </Show>
      </div>

      {/* Tool cards */}
      <Show when={expanded()}>
        <div class="flex flex-col gap-1 mt-1 ml-2">
          <For each={props.toolCalls}>{(tc) => <ToolCallCard toolCall={tc} />}</For>
        </div>
      </Show>
    </div>
  )
}
