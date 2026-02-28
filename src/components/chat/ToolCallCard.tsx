/**
 * Tool Call Card
 *
 * Expandable inline card showing a single tool call with:
 * - Tool-specific icon (spinning when running, red when error)
 * - Human-readable action summary
 * - Live elapsed timer while running
 * - Rich expanded output (syntax-highlighted, diff view, error categorization)
 */

import { ChevronRight } from 'lucide-solid'
import { type Component, createSignal, onCleanup, Show } from 'solid-js'
import type { ToolCall } from '../../types'
import { SubagentCard } from './SubagentCard'
import { ToolIcon } from './tool-call-icon'
import { ToolCallOutput } from './tool-call-output'
import { formatDuration, formatElapsed, summarizeAction } from './tool-call-utils'

// ============================================================================
// Component
// ============================================================================

interface ToolCallCardProps {
  toolCall: ToolCall
}

export const ToolCallCard: Component<ToolCallCardProps> = (props) => {
  // Render specialized card for subagent task tool
  if (props.toolCall.name === 'task') {
    return <SubagentCard toolCall={props.toolCall} />
  }

  const [expanded, setExpanded] = createSignal(false)
  const [elapsed, setElapsed] = createSignal('')

  const summary = () => summarizeAction(props.toolCall.name, props.toolCall.args)
  const isRunning = () => props.toolCall.status === 'running' || props.toolCall.status === 'pending'
  const hasOutput = () => !!(props.toolCall.output || props.toolCall.error || props.toolCall.diff)
  const hasStreamingOutput = () => isRunning() && !!props.toolCall.streamingOutput

  const duration = () => {
    if (!props.toolCall.completedAt) return null
    return formatDuration(props.toolCall.completedAt - props.toolCall.startedAt)
  }

  // Live elapsed timer — updates every second while running
  const timer = setInterval(() => {
    if (isRunning()) {
      setElapsed(formatElapsed(props.toolCall.startedAt))
    }
  }, 1000)

  onCleanup(() => clearInterval(timer))

  return (
    <div
      class="animate-tool-card-in rounded-[var(--radius-md)] border border-[var(--border-subtle)] overflow-hidden transition-colors duration-[var(--duration-fast)]"
      classList={{ 'border-[var(--error)]/30': props.toolCall.status === 'error' }}
    >
      {/* Single-line header */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        aria-expanded={expanded()}
        class="flex items-center gap-2.5 px-3 py-2 text-[13px] cursor-pointer select-none hover:bg-[var(--alpha-white-3)] transition-colors duration-[var(--duration-fast)]"
        onClick={() => {
          if (hasOutput()) setExpanded((v) => !v)
        }}
        onKeyDown={(e) => {
          if ((e.key === 'Enter' || e.key === ' ') && hasOutput()) {
            e.preventDefault()
            setExpanded((v) => !v)
          }
        }}
      >
        {/* Tool icon */}
        <ToolIcon name={props.toolCall.name} status={props.toolCall.status} />

        {/* Human-readable summary */}
        <span class="text-[var(--text-secondary)] truncate" title={summary()}>
          {summary()}
        </span>

        <span class="flex-1" />

        {/* Live elapsed / completed duration */}
        <Show when={duration() || (isRunning() && elapsed())}>
          <span class="text-[11px] text-[var(--text-muted)] tabular-nums whitespace-nowrap">
            {duration() ?? elapsed()}
          </span>
        </Show>

        {/* Expand chevron */}
        <Show when={hasOutput()}>
          <ChevronRight
            class="w-4 h-4 flex-shrink-0 text-[var(--text-muted)] transition-transform duration-[var(--duration-fast)]"
            classList={{ 'rotate-90': expanded() }}
          />
        </Show>
      </div>

      {/* Live streaming output while running */}
      <Show when={hasStreamingOutput()}>
        <div class="px-3 pb-2 border-t border-[var(--border-subtle)]">
          <pre class="text-[11px] text-[var(--text-muted)] font-mono whitespace-pre-wrap break-all max-h-32 overflow-y-auto scrollbar-none leading-relaxed mt-1.5">
            {props.toolCall.streamingOutput!.slice(-2000)}
          </pre>
        </div>
      </Show>

      {/* Expanded output — rich rendering */}
      <Show when={expanded() && hasOutput()}>
        <ToolCallOutput toolCall={props.toolCall} />
      </Show>
    </div>
  )
}
