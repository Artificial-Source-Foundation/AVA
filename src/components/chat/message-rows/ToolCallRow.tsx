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
      class="chat-tool-shell animate-tool-card-in rounded-[10px] overflow-hidden transition-colors duration-[var(--duration-fast)]"
      style={{
        background: expanded() ? 'var(--tool-card-background)' : 'transparent',
        border: isRunning()
          ? '1px solid var(--tool-card-running-border)'
          : props.toolCall.status === 'error'
            ? '1px solid var(--error-border)'
            : '1px solid var(--border-default)',
      }}
    >
      {/* Header — 40px, bottom border when collapsed */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        aria-expanded={expanded()}
        class="tool-card-header flex h-10 cursor-pointer select-none items-center gap-2.5 px-3 text-[13px] transition-colors duration-[var(--duration-fast)] hover:bg-[var(--alpha-white-5)]"
        classList={{
          'border-b border-[var(--border-subtle)]': !expanded(),
          'bg-[var(--alpha-white-5)]': expanded(),
        }}
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
        <span
          class="truncate"
          style={{
            'font-family': 'var(--font-ui), Geist, sans-serif',
            'font-size': '13px',
            color: 'var(--text-primary)',
          }}
          title={summary()}
        >
          {summary()}
        </span>
        <span class="flex-1" />
        <Show when={!isRunning() && duration()}>
          <span
            class="tabular-nums whitespace-nowrap"
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '11px',
              color: 'var(--text-muted)',
            }}
          >
            {duration()}
          </span>
        </Show>
        <Show when={isRunning() && elapsed()}>
          <span
            class="tabular-nums whitespace-nowrap"
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '11px',
              color: 'var(--accent)',
            }}
          >
            {elapsed()}
          </span>
        </Show>
        <Show when={hasOutput()}>
          <ChevronRight
            class="flex-shrink-0 transition-transform duration-[var(--duration-fast)]"
            style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
            classList={{ 'rotate-90': expanded() }}
          />
        </Show>
      </div>

      {/* Live streaming output */}
      <Show when={isRunning() && !!props.toolCall.streamingOutput}>
        <div class="border-t border-[var(--border-default)] px-3 pb-2">
          <pre class="text-[11px] text-[var(--text-muted)] font-[var(--font-ui-mono)] whitespace-pre-wrap break-all max-h-32 overflow-y-auto scrollbar-none leading-relaxed mt-1.5">
            {props.toolCall.streamingOutput!.slice(-2000)}
            <span class="ml-px inline-block h-[14px] w-[6px] animate-pulse align-middle bg-[var(--chat-streaming-indicator)]" />
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
