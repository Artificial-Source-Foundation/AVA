/**
 * Terminal Execution Item
 *
 * Renders a single command execution with monospace output lines.
 * Design: monospace font, 11px, blue $ prompt, green success text, muted secondary lines.
 */

import { Check, ChevronRight, Clock, Copy } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { TerminalExecution } from '../../../types'
import { formatDuration, formatTimestamp, parseAnsi, statusConfig } from './terminal-helpers'

export interface ExecutionItemProps {
  execution: TerminalExecution
  isExpanded: boolean
  isCopied: boolean
  onToggle: () => void
  onCopy: () => void
}

export const ExecutionItem: Component<ExecutionItemProps> = (props) => {
  const config = () => statusConfig[props.execution.status]
  const StatusIcon = () => config().icon
  const duration = () =>
    props.execution.completedAt && props.execution.startedAt
      ? props.execution.completedAt - props.execution.startedAt
      : null

  return (
    <div
      class="rounded-[6px] overflow-hidden border transition-colors"
      classList={{
        'border-[var(--accent)]': props.isExpanded,
        'border-[var(--border-subtle)] hover:border-[var(--border-default)]': !props.isExpanded,
      }}
      style={
        {
          '--execution-accent': config().color,
          '--execution-accent-bg': config().bg,
        } as { '--execution-accent': string; '--execution-accent-bg': string }
      }
    >
      {/* Command Header */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button (copy inside) which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        onClick={() => props.onToggle()}
        onKeyDown={(e) => (e.key === 'Enter' || e.key === ' ') && props.onToggle()}
        class="w-full text-left flex items-center gap-2 px-2.5 py-2 bg-[var(--background-subtle)] hover:bg-[var(--surface-raised)] transition-colors cursor-pointer border-none"
      >
        {/* Status Icon */}
        <div class="p-1 rounded-[4px] flex-shrink-0 bg-[var(--execution-accent-bg)]">
          <Dynamic
            component={StatusIcon()}
            class={`w-3 h-3 ${props.execution.status === 'running' ? 'animate-spin' : ''}`}
            style={{ color: 'var(--execution-accent)' }}
          />
        </div>

        {/* Command */}
        <div class="flex-1 min-w-0">
          <code class="text-[11px] font-[var(--font-ui-mono)] text-[var(--text-primary)] truncate block">
            $ {props.execution.command}
          </code>
          <div class="flex items-center gap-2 mt-0.5 text-[10px] text-[var(--text-muted)]">
            <span class="flex items-center gap-1">
              <Clock class="w-2.5 h-2.5" />
              {formatTimestamp(props.execution.startedAt)}
            </span>
            <Show when={duration() !== null}>
              <span>{formatDuration(duration()!)}</span>
            </Show>
            <Show
              when={props.execution.exitCode !== undefined && props.execution.status !== 'running'}
            >
              <span
                class={
                  props.execution.exitCode === 0
                    ? 'text-[var(--system-green)]'
                    : 'text-[var(--system-red)]'
                }
              >
                exit {props.execution.exitCode}
              </span>
            </Show>
          </div>
        </div>

        {/* Expand/Copy */}
        <div class="flex items-center gap-0.5 flex-shrink-0">
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              props.onCopy()
            }}
            class="p-1 rounded-[4px] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--surface-raised)] transition-colors"
            title="Copy output"
          >
            <Show when={props.isCopied} fallback={<Copy class="w-3 h-3" />}>
              <Check class="w-3 h-3 text-[var(--system-green)]" />
            </Show>
          </button>
          <ChevronRight
            class="w-3.5 h-3.5 text-[var(--text-muted)] transition-transform"
            classList={{ 'rotate-90': props.isExpanded }}
          />
        </div>
      </div>

      {/* Output */}
      <Show when={props.isExpanded}>
        <div class="px-3 py-2 bg-[var(--surface)] border-t border-[var(--border-subtle)] max-h-64 overflow-y-auto">
          <pre class="text-[11px] font-[var(--font-ui-mono)] whitespace-pre-wrap leading-relaxed">
            <Show
              when={props.execution.output}
              fallback={
                <span class="text-[var(--text-muted)] italic">
                  {props.execution.status === 'running' ? 'Waiting for output...' : 'No output'}
                </span>
              }
            >
              <For each={parseAnsi(props.execution.output)}>
                {(part) => <span class={part.class}>{part.text}</span>}
              </For>
            </Show>
          </pre>
        </div>
      </Show>
    </div>
  )
}
