/**
 * Terminal Execution Item
 *
 * Renders a single command execution with expandable output.
 * Extracted from TerminalPanel.tsx.
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
      class={`
        rounded-[var(--radius-lg)]
        border
        overflow-hidden
        transition-all duration-[var(--duration-fast)]
        ${
          props.isExpanded
            ? 'border-[var(--accent)]'
            : 'border-[var(--border-subtle)] hover:border-[var(--border-default)]'
        }
      `}
    >
      {/* Command Header — div (not button) to avoid nested button with copy */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button (copy inside) which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        onClick={() => props.onToggle()}
        onKeyDown={(e) => (e.key === 'Enter' || e.key === ' ') && props.onToggle()}
        class="
          w-full text-left
          flex items-center density-gap
          density-section-px density-section-py
          bg-[var(--surface)]
          hover:bg-[var(--surface-raised)]
          transition-colors duration-[var(--duration-fast)]
          cursor-pointer border-none
        "
      >
        {/* Status Icon */}
        <div
          class="p-1.5 rounded-[var(--radius-md)] flex-shrink-0"
          style={{ background: config().bg }}
        >
          <Dynamic
            component={StatusIcon()}
            class={`w-3.5 h-3.5 ${props.execution.status === 'running' ? 'animate-spin' : ''}`}
            style={{ color: config().color }}
          />
        </div>

        {/* Command */}
        <div class="flex-1 min-w-0">
          <div class="flex items-center gap-2">
            <code class="text-sm font-mono text-[var(--text-primary)] truncate">
              $ {props.execution.command}
            </code>
          </div>
          <div class="flex items-center gap-3 mt-1 text-xs text-[var(--text-muted)]">
            <span class="flex items-center gap-1">
              <Clock class="w-3 h-3" />
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
                  props.execution.exitCode === 0 ? 'text-[var(--success)]' : 'text-[var(--error)]'
                }
              >
                exit {props.execution.exitCode}
              </span>
            </Show>
          </div>
        </div>

        {/* Expand/Copy */}
        <div class="flex items-center gap-1 flex-shrink-0">
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              props.onCopy()
            }}
            class="
              p-1.5
              rounded-[var(--radius-md)]
              text-[var(--text-muted)]
              hover:text-[var(--text-primary)]
              hover:bg-[var(--surface-raised)]
              transition-colors
            "
            title="Copy output"
          >
            <Show when={props.isCopied} fallback={<Copy class="w-3.5 h-3.5" />}>
              <Check class="w-3.5 h-3.5 text-[var(--success)]" />
            </Show>
          </button>
          <ChevronRight
            class={`
              w-4 h-4
              text-[var(--text-muted)]
              transition-transform duration-[var(--duration-fast)]
              ${props.isExpanded ? 'rotate-90' : ''}
            `}
          />
        </div>
      </div>

      {/* Output */}
      <Show when={props.isExpanded}>
        <div
          class="
            p-3
            bg-[var(--code-background)]
            border-t border-[var(--border-subtle)]
            max-h-64 overflow-y-auto
          "
        >
          <pre class="text-xs font-mono whitespace-pre-wrap">
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
