/**
 * Command Output Row
 *
 * Terminal-style output display for bash/command tool calls.
 * Expandable with monospace font and dark background.
 */

import { ChevronRight, Terminal } from 'lucide-solid'
import { type Component, createMemo, createSignal, Show } from 'solid-js'
import { useSecondTicker } from '../../../hooks/useElapsedTimer'
import type { ToolCall } from '../../../types'
import { formatDuration, formatElapsed } from '../tool-call-utils'

interface CommandOutputRowProps {
  toolCall: ToolCall
}

/** Max lines to show before collapsing */
const COLLAPSED_LINE_LIMIT = 8

export const CommandOutputRow: Component<CommandOutputRowProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)

  const command = (): string => {
    const cmd = String(props.toolCall.args.command ?? '')
    return cmd.length > 120 ? `${cmd.slice(0, 117)}...` : cmd
  }

  const isRunning = (): boolean =>
    props.toolCall.status === 'running' || props.toolCall.status === 'pending'
  const nowTick = useSecondTicker(isRunning)

  const output = (): string => props.toolCall.output ?? props.toolCall.streamingOutput ?? ''

  const outputLines = createMemo(() => output().split('\n'))
  const isLong = (): boolean => outputLines().length > COLLAPSED_LINE_LIMIT
  const hasOutput = (): boolean => !!output()

  const displayOutput = createMemo((): string => {
    if (!isLong() || expanded()) return output()
    return outputLines().slice(0, COLLAPSED_LINE_LIMIT).join('\n')
  })

  const duration = (): string | null => {
    if (!props.toolCall.completedAt) return null
    return formatDuration(props.toolCall.completedAt - props.toolCall.startedAt)
  }

  const exitCode = (): number | undefined => {
    // Try to extract from output or metadata
    const meta = props.toolCall.args as Record<string, unknown>
    return meta.exitCode as number | undefined
  }

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
        <Terminal class="w-4 h-4 text-[var(--text-muted)] flex-shrink-0" />
        <code class="text-[12px] text-[var(--text-secondary)] truncate font-mono bg-transparent p-0">
          {command()}
        </code>

        <span class="flex-1" />

        <Show when={exitCode() !== undefined}>
          <span
            class="text-[10px] tabular-nums font-mono"
            classList={{
              'text-[var(--success)]': exitCode() === 0,
              'text-[var(--error)]': exitCode() !== 0,
            }}
          >
            exit {exitCode()}
          </span>
        </Show>

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

      {/* Terminal output */}
      <Show when={expanded() || (isRunning() && !!props.toolCall.streamingOutput)}>
        <div class="bg-[var(--gray-1)] border-t border-[var(--border-subtle)] px-3 py-2">
          <pre class="text-[11px] font-mono text-[var(--text-muted)] whitespace-pre-wrap break-all leading-relaxed max-h-[300px] overflow-y-auto scrollbar-thin">
            {displayOutput()}
            <Show when={isRunning()}>
              <span class="streaming-cursor">&#9613;</span>
            </Show>
          </pre>
          <Show when={isLong() && !isRunning()}>
            <button
              type="button"
              onClick={() => setExpanded((v) => !v)}
              class="mt-1 text-[10px] text-[var(--accent)] hover:text-[var(--accent-hover)] transition-colors"
            >
              {expanded() ? 'Show less' : `Show all (${outputLines().length} lines)`}
            </button>
          </Show>
        </div>
      </Show>
    </div>
  )
}
