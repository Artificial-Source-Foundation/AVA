/**
 * Completion Row
 *
 * Green-bordered success state for completed agent turns.
 * Shows a summary of what was accomplished.
 */

import { CheckCircle2 } from 'lucide-solid'
import { type Component, Show } from 'solid-js'

interface CompletionRowProps {
  /** Summary text for the completion */
  summary?: string
  /** Number of tool calls executed */
  toolCallCount?: number
  /** Total elapsed time in ms */
  elapsedMs?: number
}

function formatElapsedMs(ms: number): string {
  if (ms < 1000) return `${ms}ms`
  if (ms < 60_000) return `${(ms / 1000).toFixed(1)}s`
  const mins = Math.floor(ms / 60_000)
  const secs = Math.round((ms % 60_000) / 1000)
  return `${mins}m ${secs}s`
}

export const CompletionRow: Component<CompletionRowProps> = (props) => {
  return (
    <div class="mt-2 px-3 py-2 border border-[var(--success)]/30 bg-[var(--success)]/5 rounded-[var(--radius-md)] animate-fade-in">
      <div class="flex items-center gap-2">
        <CheckCircle2 class="w-4 h-4 text-[var(--success)] flex-shrink-0" />
        <span class="text-xs text-[var(--success)] font-medium">
          {props.summary ?? 'Task completed'}
        </span>

        <span class="flex-1" />

        <Show when={props.toolCallCount !== undefined && props.toolCallCount > 0}>
          <span class="text-[10px] text-[var(--text-muted)] tabular-nums">
            {props.toolCallCount} tool{props.toolCallCount !== 1 ? 's' : ''}
          </span>
        </Show>

        <Show when={props.elapsedMs !== undefined && props.elapsedMs > 0}>
          <span class="text-[10px] text-[var(--text-muted)] tabular-nums">
            {formatElapsedMs(props.elapsedMs!)}
          </span>
        </Show>
      </div>
    </div>
  )
}
