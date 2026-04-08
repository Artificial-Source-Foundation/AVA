/**
 * Subagent Detail View
 *
 * Read-only detail panel for delegated subagent tool calls.
 * Shows a compact status header and live/raw output from the tool call.
 */

import { ArrowLeft, Bot, Check, Loader2, XCircle } from 'lucide-solid'
import { type Component, createMemo, Show } from 'solid-js'
import { useSecondTicker } from '../../hooks/useElapsedTimer'
import { formatElapsedSince } from '../../lib/format-time'
import { useLayout } from '../../stores/layout'
import type { ToolCall } from '../../types'
import { formatDuration } from './tool-call-utils'

interface SubagentDetailViewProps {
  /** The tool call ID that triggered this delegation */
  toolCallId: string
  /** The original ToolCall object (from the parent chat) */
  toolCall: ToolCall | undefined
}

export const SubagentDetailView: Component<SubagentDetailViewProps> = (props) => {
  const { closeSubagentDetail } = useLayout()

  const isRunning = () =>
    props.toolCall?.status === 'running' || props.toolCall?.status === 'pending'
  const isDone = () => props.toolCall?.status === 'success' || props.toolCall?.status === 'error'
  const isError = () => props.toolCall?.status === 'error'

  const nowTick = useSecondTicker(isRunning)

  const agentName = () => {
    if (!props.toolCall) return 'Subagent'
    const args = props.toolCall.args as Record<string, unknown>
    const role = String(args.role ?? args.agent_type ?? 'Scout')
    const goal = String(args.goal ?? args.description ?? args.prompt ?? 'task')
    const truncated = goal.length > 50 ? `${goal.slice(0, 47)}...` : goal
    return `${role} — ${truncated}`
  }

  const metadata = createMemo(() => {
    const parts: string[] = []
    if (props.toolCall) {
      const args = props.toolCall.args as Record<string, unknown>
      if (args.model) parts.push(String(args.model))
      if (args.mode) parts.push(String(args.mode))
      if (isRunning()) {
        nowTick()
        parts.push(`delegated ${formatElapsedSince(props.toolCall.startedAt)} ago`)
      } else if (isDone() && props.toolCall.completedAt) {
        const dur = formatDuration(props.toolCall.completedAt - props.toolCall.startedAt)
        parts.push(`completed in ${dur}`)
      }
    }

    return parts.join(' · ')
  })

  const elapsed = createMemo(() => {
    if (!isRunning() || !props.toolCall) return ''
    nowTick()
    return formatElapsedSince(props.toolCall.startedAt)
  })

  const statusText = () => {
    if (isRunning()) return 'Running'
    if (isError()) return 'Error'
    return 'Done'
  }

  const statusColor = () => {
    if (isError()) return 'var(--error)'
    if (isDone()) return 'var(--success)'
    return 'var(--accent)'
  }

  return (
    <div class="flex min-h-0 h-full flex-col bg-[var(--background)]">
      <div
        class="flex items-center justify-between flex-shrink-0"
        style={{
          height: '56px',
          padding: '0 20px',
          background: 'color-mix(in srgb, var(--accent) 6%, transparent)',
          'border-bottom': '1px solid var(--accent-border)',
        }}
      >
        <div class="flex items-center gap-3 min-w-0 flex-1">
          <button
            type="button"
            onClick={closeSubagentDetail}
            class="flex-shrink-0 rounded p-0.5 transition-colors hover:bg-[var(--alpha-white-8)]"
            aria-label="Back to chat"
          >
            <ArrowLeft style={{ width: '16px', height: '16px', color: 'var(--text-tertiary)' }} />
          </button>

          <div
            class="flex items-center justify-center flex-shrink-0"
            style={{
              width: '28px',
              height: '28px',
              'border-radius': '7px',
              background: 'var(--accent-subtle)',
            }}
          >
            <Bot style={{ width: '14px', height: '14px', color: 'var(--accent)' }} />
          </div>

          <div class="flex flex-col gap-px min-w-0">
            <span
              class="truncate"
              style={{
                'font-family': 'var(--font-ui), Geist, sans-serif',
                'font-size': '14px',
                'font-weight': '500',
                color: 'var(--text-primary)',
              }}
            >
              {agentName()}
            </span>
            <Show when={metadata()}>
              <span
                class="truncate"
                style={{
                  'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                  'font-size': '10px',
                  color: 'var(--accent)',
                }}
              >
                {metadata()}
              </span>
            </Show>
          </div>
        </div>

        <div class="flex items-center gap-2 flex-shrink-0">
          <div
            class="flex items-center gap-1 rounded-md"
            style={{
              padding: '4px 8px',
              background: `${statusColor()}15`,
            }}
          >
            <Show when={isRunning()}>
              <Loader2
                class="animate-spin"
                style={{ width: '11px', height: '11px', color: statusColor() }}
              />
            </Show>
            <Show when={isDone() && !isError()}>
              <Check style={{ width: '11px', height: '11px', color: statusColor() }} />
            </Show>
            <Show when={isError()}>
              <XCircle style={{ width: '11px', height: '11px', color: statusColor() }} />
            </Show>
            <span
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '10px',
                'font-weight': '500',
                color: statusColor(),
              }}
            >
              {statusText()}
            </span>
          </div>

          <Show when={elapsed()}>
            <span
              class="tabular-nums"
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '11px',
                color: 'var(--accent)',
              }}
            >
              {elapsed()}
            </span>
          </Show>
        </div>
      </div>

      <div
        class="flex items-center justify-center flex-shrink-0"
        style={{
          height: '32px',
          background: 'color-mix(in srgb, var(--accent) 8%, transparent)',
          padding: '0 20px',
        }}
      >
        <span
          style={{
            'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
            'font-size': '10px',
            color: 'color-mix(in srgb, var(--accent) 70%, transparent)',
          }}
        >
          Read-only — this agent is working autonomously
        </span>
      </div>

      <div class="flex-1 overflow-y-auto scrollbar-thin" style={{ padding: '24px 0' }}>
        <div
          class="mx-auto flex flex-col gap-4"
          style={{ 'max-width': '800px', padding: '0 20px' }}
        >
          <Show when={props.toolCall?.streamingOutput}>
            <pre
              class="whitespace-pre-wrap break-all"
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '11px',
                color: 'var(--text-tertiary)',
                'line-height': '1.6',
              }}
            >
              {props.toolCall!.streamingOutput!.slice(-4000)}
            </pre>
          </Show>

          <Show when={isDone() && (props.toolCall?.output || props.toolCall?.error)}>
            <pre
              class="whitespace-pre-wrap break-all"
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '11px',
                color: props.toolCall?.error ? 'var(--error)' : 'var(--text-tertiary)',
                'line-height': '1.6',
              }}
            >
              {props.toolCall?.error || props.toolCall?.output}
            </pre>
          </Show>

          <Show
            when={
              !props.toolCall?.streamingOutput &&
              !(isDone() && (props.toolCall?.output || props.toolCall?.error))
            }
          >
            <div class="flex flex-col items-center justify-center py-16 gap-3">
              <Loader2
                class="animate-spin"
                style={{
                  width: '20px',
                  height: '20px',
                  color: 'color-mix(in srgb, var(--accent) 40%, transparent)',
                }}
              />
              <span
                style={{
                  'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                  'font-size': '11px',
                  color: 'var(--text-muted)',
                }}
              >
                Waiting for agent activity...
              </span>
            </div>
          </Show>
        </div>
      </div>
    </div>
  )
}
