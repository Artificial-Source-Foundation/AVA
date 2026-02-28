/**
 * Subagent Card
 *
 * Specialized card for `task` tool calls showing subagent delegation.
 * Shows agent goal, status badge, elapsed time, and nested tool calls.
 */

import { CheckCircle, ChevronRight, Loader2, Users, XCircle } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, onCleanup, Show } from 'solid-js'
import type { ToolCall } from '../../types'
import { formatDuration, formatElapsed } from './tool-call-utils'

interface SubagentCardProps {
  toolCall: ToolCall
}

interface NestedToolCall {
  name: string
  summary: string
  status: 'success' | 'error'
}

function parseNestedToolCalls(output: string): NestedToolCall[] {
  const calls: NestedToolCall[] = []
  const lines = output.split('\n')
  for (const line of lines) {
    // Match common patterns like "✓ read_file src/foo.ts" or "✗ bash failed"
    const successMatch = line.match(/[✓✔☑]\s*(\w+)\s*(.*)/)
    const errorMatch = line.match(/[✗✘☒]\s*(\w+)\s*(.*)/)
    if (successMatch) {
      calls.push({ name: successMatch[1], summary: successMatch[2].trim(), status: 'success' })
    } else if (errorMatch) {
      calls.push({ name: errorMatch[1], summary: errorMatch[2].trim(), status: 'error' })
    }
  }
  return calls
}

export const SubagentCard: Component<SubagentCardProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)
  const [elapsed, setElapsed] = createSignal('')

  const isRunning = () => props.toolCall.status === 'running' || props.toolCall.status === 'pending'
  const isError = () => props.toolCall.status === 'error'
  const isSuccess = () => props.toolCall.status === 'success'
  const hasOutput = () => !!(props.toolCall.output || props.toolCall.error)

  const goal = () => {
    const args = props.toolCall.args
    const g = String(args.goal ?? args.description ?? args.prompt ?? '')
    return g.length > 80 ? `${g.slice(0, 77)}...` : g || 'subagent task'
  }

  const duration = () => {
    if (!props.toolCall.completedAt) return null
    return formatDuration(props.toolCall.completedAt - props.toolCall.startedAt)
  }

  const nestedCalls = createMemo(() => {
    if (!props.toolCall.output) return []
    return parseNestedToolCalls(props.toolCall.output)
  })

  const timer = setInterval(() => {
    if (isRunning()) {
      setElapsed(formatElapsed(props.toolCall.startedAt))
    }
  }, 1000)

  onCleanup(() => clearInterval(timer))

  return (
    <div
      class="animate-tool-card-in rounded-[var(--radius-md)] border overflow-hidden transition-colors duration-[var(--duration-fast)]"
      classList={{
        'border-[var(--accent)]/30 bg-[var(--accent-subtle)]/30': isRunning(),
        'border-[var(--error)]/30': isError(),
        'border-[var(--border-subtle)]': isSuccess(),
      }}
    >
      {/* Header */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button crash in WebKitGTK */}
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
        {/* Status icon */}
        <Show when={isRunning()}>
          <Loader2 class="w-4 h-4 flex-shrink-0 animate-spin text-[var(--accent-text)]" />
        </Show>
        <Show when={isSuccess()}>
          <CheckCircle class="w-4 h-4 flex-shrink-0 text-[var(--success)]" />
        </Show>
        <Show when={isError()}>
          <XCircle class="w-4 h-4 flex-shrink-0 text-[var(--error)]" />
        </Show>

        {/* Agent icon + goal */}
        <Users class="w-3.5 h-3.5 flex-shrink-0 text-[var(--text-muted)]" />
        <span class="text-[var(--text-secondary)] truncate" title={goal()}>
          {goal()}
        </span>

        <span class="flex-1" />

        {/* Status badge */}
        <Show when={isRunning()}>
          <span class="text-[10px] px-1.5 py-0.5 rounded-full bg-[var(--accent)]/15 text-[var(--accent-text)] font-medium">
            running
          </span>
        </Show>

        {/* Duration / elapsed */}
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

      {/* Streaming output */}
      <Show when={isRunning() && props.toolCall.streamingOutput}>
        <div class="px-3 pb-2 border-t border-[var(--border-subtle)]">
          <pre class="text-[11px] text-[var(--text-muted)] font-mono whitespace-pre-wrap break-all max-h-32 overflow-y-auto scrollbar-none leading-relaxed mt-1.5">
            {props.toolCall.streamingOutput!.slice(-2000)}
          </pre>
        </div>
      </Show>

      {/* Expanded body */}
      <Show when={expanded() && hasOutput()}>
        <div class="border-t border-[var(--border-subtle)] px-3 py-2 space-y-1.5">
          {/* Nested tool calls timeline */}
          <Show when={nestedCalls().length > 0}>
            <div class="space-y-1">
              <For each={nestedCalls()}>
                {(call) => (
                  <div class="flex items-center gap-2 text-[11px]">
                    <span
                      class="w-1.5 h-1.5 rounded-full flex-shrink-0"
                      classList={{
                        'bg-[var(--success)]': call.status === 'success',
                        'bg-[var(--error)]': call.status === 'error',
                      }}
                    />
                    <span class="text-[var(--text-muted)] font-mono">{call.name}</span>
                    <Show when={call.summary}>
                      <span class="text-[var(--text-muted)] truncate">{call.summary}</span>
                    </Show>
                  </div>
                )}
              </For>
            </div>
          </Show>

          {/* Raw output fallback */}
          <Show
            when={nestedCalls().length === 0 && (props.toolCall.output || props.toolCall.error)}
          >
            <pre class="text-[11px] font-mono whitespace-pre-wrap break-all max-h-48 overflow-y-auto scrollbar-none leading-relaxed text-[var(--text-muted)]">
              {props.toolCall.error || props.toolCall.output}
            </pre>
          </Show>
        </div>
      </Show>
    </div>
  )
}
