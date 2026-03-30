/**
 * Command Output Row
 *
 * Terminal-style output display for bash/command tool calls.
 * Matches Pencil "Tool States" design:
 *
 * Success: rounded-10, fill #0F0F12, border #ffffff08
 *   Header: 40px, fill #ffffff04, terminal icon (14px #0A84FF),
 *           command text (Geist Mono 12px weight 500 #F5F5F7),
 *           exit badge (rounded-4, fill #34C75915, "exit 0" Geist Mono 10px #34C759),
 *           duration (Geist Mono 11px #48484A), chevron-down
 *   Body: bg #0A0A0C, padding 10px 14px, output lines (Geist Mono 11px #86868B)
 *
 * Error: border #FF453A30, terminal icon #FF453A,
 *        exit badge (fill #FF453A15, "exit 1" #FF453A),
 *        error text #FF6961
 */

import { ChevronDown, ChevronRight, Loader2, Terminal } from 'lucide-solid'
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
  const isError = (): boolean => props.toolCall.status === 'error'
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
    const meta = props.toolCall.args as Record<string, unknown>
    return meta.exitCode as number | undefined
  }

  const elapsed = createMemo(() => {
    if (!isRunning()) return ''
    nowTick()
    return formatElapsed(props.toolCall.startedAt)
  })

  /** Terminal icon color: blue normally, red on error */
  const terminalColor = (): string => {
    if (isError()) return 'var(--error)'
    if (isRunning()) return 'var(--accent)'
    return 'var(--accent)'
  }

  return (
    <div
      class="chat-tool-shell animate-tool-card-in rounded-[10px] overflow-hidden transition-colors duration-[var(--duration-fast)]"
      style={{
        background: 'var(--tool-card-background)',
        border: isError()
          ? '1px solid var(--error-border)'
          : isRunning()
            ? '1px solid var(--tool-card-running-border)'
            : '1px solid var(--border-default)',
      }}
    >
      {/* Header -- 40px */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        aria-expanded={expanded()}
        class="tool-card-header flex cursor-pointer select-none items-center justify-between px-3.5 transition-colors duration-[var(--duration-fast)] hover:bg-[var(--alpha-white-5)]"
        style={{
          height: '40px',
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
        {/* Left: icon + command */}
        <div class="flex items-center gap-2 min-w-0 flex-1">
          <Show
            when={!isRunning()}
            fallback={
              <Loader2
                class="flex-shrink-0 animate-spin"
                style={{ width: '14px', height: '14px', color: 'var(--accent)' }}
              />
            }
          >
            <Terminal
              class="flex-shrink-0"
              style={{ width: '14px', height: '14px', color: terminalColor() }}
            />
          </Show>
          <span
            class="truncate"
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '12px',
              'font-weight': '500',
              color: 'var(--text-primary)',
            }}
          >
            {command()}
          </span>
        </div>

        {/* Right: exit badge + duration + chevron */}
        <div class="flex items-center gap-2 flex-shrink-0">
          {/* Exit code badge */}
          <Show when={!isRunning() && exitCode() !== undefined}>
            <span
              class="inline-flex items-center tabular-nums"
              style={{
                'border-radius': '4px',
                background: exitCode() === 0 ? 'var(--success-subtle)' : 'var(--error-subtle)',
                padding: '2px 6px',
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '10px',
                'font-weight': '500',
                color: exitCode() === 0 ? 'var(--success)' : 'var(--error)',
              }}
            >
              exit {exitCode()}
            </span>
          </Show>

          {/* Duration / elapsed */}
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

          {/* Chevron */}
          <Show when={hasOutput()}>
            <Show
              when={expanded()}
              fallback={
                <ChevronRight
                  class="flex-shrink-0 transition-transform duration-[var(--duration-fast)]"
                  style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
                />
              }
            >
              <ChevronDown
                class="flex-shrink-0"
                style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
              />
            </Show>
          </Show>
        </div>
      </div>

      {/* Terminal output body */}
      <Show when={expanded() || (isRunning() && !!props.toolCall.streamingOutput)}>
        <div
          style={{
            background: 'var(--background)',
            padding: '10px 14px',
          }}
        >
          <pre
            class="whitespace-pre-wrap break-all max-h-[300px] overflow-y-auto scrollbar-thin"
            style={{
              'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
              'font-size': '11px',
              color: isError() ? 'var(--error)' : 'var(--text-tertiary)',
              'line-height': '1.6',
              gap: '2px',
            }}
          >
            {displayOutput()}
            <Show when={isRunning()}>
              <span
                class="inline-block animate-pulse ml-px align-middle"
                style={{
                  width: '6px',
                  height: '14px',
                  background: 'var(--chat-streaming-indicator)',
                }}
              />
            </Show>
          </pre>
          <Show when={isLong() && !isRunning()}>
            <button
              type="button"
              onClick={() => setExpanded((v) => !v)}
              class="mt-2 text-[10px] text-[var(--accent)] transition-colors hover:text-[var(--accent-hover)]"
              style={{ 'font-family': 'var(--font-ui-mono), Geist Mono, monospace' }}
              aria-label={expanded() ? 'Collapse command output' : 'Expand command output'}
            >
              {expanded() ? 'Show less' : `Show all (${outputLines().length} lines)`}
            </button>
          </Show>
        </div>
      </Show>
    </div>
  )
}
