/**
 * Tool Call Row
 *
 * Displays tool name + args summary with expandable result.
 * Collapsed by default; auto-expands when output is <=5 lines.
 */

import { ChevronRight } from 'lucide-solid'
import { type Component, createEffect, createMemo, createSignal, Show } from 'solid-js'
import { useSecondTicker } from '../../../hooks/useElapsedTimer'
import type { ToolCall } from '../../../types'
import { ToolIcon } from '../tool-call-icon'
import { ToolCallOutput } from '../tool-call-output'
import { formatDuration, formatElapsed, summarizeAction } from '../tool-call-utils'

interface ToolCallRowProps {
  toolCall: ToolCall
}

/** Number of output lines at which we auto-expand */
const AUTO_EXPAND_LINE_THRESHOLD = 5

export const ToolCallRow: Component<ToolCallRowProps> = (props) => {
  const outputLineCount = createMemo(() => {
    const output = props.toolCall.output ?? ''
    if (!output) return 0
    return output.split('\n').length
  })

  const shouldAutoExpand = createMemo(() => {
    if (props.toolCall.status === 'error') return true
    if (!props.toolCall.output) return false
    return outputLineCount() <= AUTO_EXPAND_LINE_THRESHOLD
  })

  const [expanded, setExpanded] = createSignal(false)

  const summary = (): string => summarizeAction(props.toolCall.name, props.toolCall.args)
  const isRunning = (): boolean =>
    props.toolCall.status === 'running' || props.toolCall.status === 'pending'
  const nowTick = useSecondTicker(isRunning)
  const hasOutput = (): boolean =>
    !!(props.toolCall.output || props.toolCall.error || props.toolCall.diff)

  const duration = (): string | null => {
    if (!props.toolCall.completedAt) return null
    return formatDuration(props.toolCall.completedAt - props.toolCall.startedAt)
  }

  createEffect(() => {
    if (shouldAutoExpand()) setExpanded(true)
  })

  const elapsed = createMemo(() => {
    if (!isRunning()) return ''
    nowTick()
    return formatElapsed(props.toolCall.startedAt)
  })

  return (
    <div
      class="animate-tool-card-in rounded-[var(--radius-md)] border overflow-hidden transition-colors duration-[var(--duration-fast)]"
      classList={{
        'border-[var(--error)]/30': props.toolCall.status === 'error',
        'border-[var(--accent)]/30': isRunning(),
        'border-[var(--border-subtle)]': !isRunning() && props.toolCall.status !== 'error',
      }}
    >
      {/* Header */}
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
        <ToolIcon name={props.toolCall.name} status={props.toolCall.status} />
        <span class="text-[var(--text-secondary)] truncate" title={summary()}>
          {summary()}
        </span>
        <span class="flex-1" />
        <Show when={duration() || (isRunning() && elapsed())}>
          <span class="text-[11px] text-[var(--text-muted)] tabular-nums whitespace-nowrap">
            {duration() ?? elapsed()}
          </span>
        </Show>
        <Show when={hasOutput()}>
          <ChevronRight
            class="w-4 h-4 flex-shrink-0 text-[var(--text-muted)] transition-transform duration-[var(--duration-fast)]"
            classList={{ 'rotate-90': expanded() }}
          />
        </Show>
      </div>

      {/* Live streaming output */}
      <Show when={isRunning() && !!props.toolCall.streamingOutput}>
        <div class="px-3 pb-2 border-t border-[var(--border-subtle)]">
          <pre class="text-[11px] text-[var(--text-muted)] font-mono whitespace-pre-wrap break-all max-h-32 overflow-y-auto scrollbar-none leading-relaxed mt-1.5">
            {props.toolCall.streamingOutput!.slice(-2000)}
          </pre>
        </div>
      </Show>

      {/* Expanded output */}
      <Show when={expanded() && hasOutput()}>
        <ToolCallOutput toolCall={props.toolCall} />
      </Show>
    </div>
  )
}
