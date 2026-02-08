/**
 * Terminal Panel Component
 *
 * Displays shell command execution output with ANSI color support.
 * Connected to the session store for real-time command tracking.
 */

import { Check, ChevronRight, Clock, Copy, Loader2, Terminal, Trash2, X } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useSession } from '../../stores/session'
import type { ExecutionStatus } from '../../types'

// ============================================================================
// ANSI Color Parsing
// ============================================================================

const ansiToClass: Record<string, string> = {
  '30': 'text-gray-900 dark:text-gray-100',
  '31': 'text-red-600 dark:text-red-400',
  '32': 'text-green-600 dark:text-green-400',
  '33': 'text-yellow-600 dark:text-yellow-400',
  '34': 'text-blue-600 dark:text-blue-400',
  '35': 'text-purple-600 dark:text-purple-400',
  '36': 'text-cyan-600 dark:text-cyan-400',
  '37': 'text-gray-600 dark:text-gray-300',
  '90': 'text-gray-500',
  '91': 'text-red-500',
  '92': 'text-green-500',
  '93': 'text-yellow-500',
  '94': 'text-blue-500',
  '95': 'text-purple-500',
  '96': 'text-cyan-500',
  '97': 'text-white',
}

// Parse ANSI codes and convert to styled spans
const parseAnsi = (text: string): { text: string; class: string }[] => {
  const parts: { text: string; class: string }[] = []
  // oxlint-disable-next-line no-control-regex -- ANSI escape codes require control characters
  // biome-ignore lint/suspicious/noControlCharactersInRegex: ANSI escape codes require control characters by definition
  const regex = /\x1b\[(\d+)m/g
  let lastIndex = 0
  let currentClass = ''
  let match: RegExpExecArray | null

  // biome-ignore lint/suspicious/noAssignInExpressions: Standard regex iteration pattern
  while ((match = regex.exec(text)) !== null) {
    // Add text before this match
    if (match.index > lastIndex) {
      parts.push({ text: text.slice(lastIndex, match.index), class: currentClass })
    }

    // Update current class
    const code = match[1]
    if (code === '0') {
      currentClass = '' // Reset
    } else if (ansiToClass[code]) {
      currentClass = ansiToClass[code]
    }

    lastIndex = regex.lastIndex
  }

  // Add remaining text
  if (lastIndex < text.length) {
    parts.push({ text: text.slice(lastIndex), class: currentClass })
  }

  return parts.length > 0 ? parts : [{ text, class: '' }]
}

// ============================================================================
// Status Configuration
// ============================================================================

const statusConfig: Record<
  ExecutionStatus,
  { color: string; bg: string; icon: typeof Check; label: string }
> = {
  running: { color: 'var(--accent)', bg: 'var(--accent-subtle)', icon: Loader2, label: 'Running' },
  success: { color: 'var(--success)', bg: 'var(--success-subtle)', icon: Check, label: 'Success' },
  error: { color: 'var(--error)', bg: 'var(--error-subtle)', icon: X, label: 'Error' },
}

// ============================================================================
// Component
// ============================================================================

interface TerminalPanelProps {
  compact?: boolean
}

export const TerminalPanel: Component<TerminalPanelProps> = (props) => {
  const { terminalExecutions, clearTerminalExecutions } = useSession()
  const [expandedIds, setExpandedIds] = createSignal<Set<string>>(new Set())
  const [copiedId, setCopiedId] = createSignal<string | null>(null)

  const toggleExpanded = (id: string) => {
    setExpandedIds((prev) => {
      const next = new Set(prev)
      if (next.has(id)) {
        next.delete(id)
      } else {
        next.add(id)
      }
      return next
    })
  }

  const copyToClipboard = async (text: string, id: string) => {
    await navigator.clipboard.writeText(text)
    setCopiedId(id)
    setTimeout(() => setCopiedId(null), 2000)
  }

  const formatDuration = (ms: number): string => {
    if (ms < 1000) return `${ms}ms`
    return `${(ms / 1000).toFixed(1)}s`
  }

  const formatTimestamp = (ts: number): string => {
    const diff = Date.now() - ts
    if (diff < 60000) return 'Just now'
    if (diff < 3600000) return `${Math.floor(diff / 60000)}m ago`
    return new Date(ts).toLocaleTimeString()
  }

  const executionStats = createMemo(() => ({
    running: terminalExecutions().filter((e) => e.status === 'running').length,
    success: terminalExecutions().filter((e) => e.status === 'success').length,
    error: terminalExecutions().filter((e) => e.status === 'error').length,
    total: terminalExecutions().length,
  }))

  return (
    <div class="flex flex-col h-full bg-[var(--surface-sunken)]">
      {/* Header (hidden in compact/embedded mode) */}
      <Show when={!props.compact}>
        <div
          class="
            flex items-center justify-between
            px-4 py-3
            border-b border-[var(--border-subtle)]
            bg-[var(--surface)]
          "
        >
          <div class="flex items-center gap-3">
            <div
              class="
                p-2
                bg-[var(--surface-raised)]
                rounded-[var(--radius-lg)]
              "
            >
              <Terminal class="w-5 h-5 text-[var(--text-primary)]" />
            </div>
            <div>
              <h2 class="text-sm font-semibold text-[var(--text-primary)]">Terminal Output</h2>
              <p class="text-xs text-[var(--text-muted)]">
                {executionStats().running > 0 ? `${executionStats().running} running · ` : ''}
                {executionStats().total} executions
              </p>
            </div>
          </div>
          <Show when={terminalExecutions().length > 0}>
            <button
              type="button"
              onClick={() => clearTerminalExecutions()}
              class="
                p-2
                rounded-[var(--radius-md)]
                text-[var(--text-tertiary)]
                hover:text-[var(--error)]
                hover:bg-[var(--error-subtle)]
                transition-colors duration-[var(--duration-fast)]
              "
              title="Clear all"
            >
              <Trash2 class="w-4 h-4" />
            </button>
          </Show>
        </div>
      </Show>

      {/* Executions List */}
      <div class="flex-1 overflow-y-auto p-3 space-y-2">
        <Show
          when={terminalExecutions().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center p-6">
              <div class="p-4 bg-[var(--surface-raised)] rounded-full mb-4">
                <Terminal class="w-8 h-8 text-[var(--text-muted)]" />
              </div>
              <h3 class="text-sm font-medium text-[var(--text-secondary)] mb-1">
                No terminal output
              </h3>
              <p class="text-xs text-[var(--text-muted)]">
                Command execution results will appear here
              </p>
            </div>
          }
        >
          <For each={terminalExecutions()}>
            {(execution) => {
              const config = statusConfig[execution.status]
              const StatusIcon = config.icon
              const isExpanded = () => expandedIds().has(execution.id)
              const duration =
                execution.completedAt && execution.startedAt
                  ? execution.completedAt - execution.startedAt
                  : null

              return (
                <div
                  class={`
                    rounded-[var(--radius-lg)]
                    border
                    overflow-hidden
                    transition-all duration-[var(--duration-fast)]
                    ${
                      isExpanded()
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
                    onClick={() => toggleExpanded(execution.id)}
                    onKeyDown={(e) =>
                      (e.key === 'Enter' || e.key === ' ') && toggleExpanded(execution.id)
                    }
                    class="
                      w-full text-left
                      flex items-center gap-3
                      p-3
                      bg-[var(--surface)]
                      hover:bg-[var(--surface-raised)]
                      transition-colors duration-[var(--duration-fast)]
                      cursor-pointer border-none
                    "
                  >
                    {/* Status Icon */}
                    <div
                      class="p-1.5 rounded-[var(--radius-md)] flex-shrink-0"
                      style={{ background: config.bg }}
                    >
                      <StatusIcon
                        class={`w-3.5 h-3.5 ${execution.status === 'running' ? 'animate-spin' : ''}`}
                        style={{ color: config.color }}
                      />
                    </div>

                    {/* Command */}
                    <div class="flex-1 min-w-0">
                      <div class="flex items-center gap-2">
                        <code class="text-sm font-mono text-[var(--text-primary)] truncate">
                          $ {execution.command}
                        </code>
                      </div>
                      <div class="flex items-center gap-3 mt-1 text-xs text-[var(--text-muted)]">
                        <span class="flex items-center gap-1">
                          <Clock class="w-3 h-3" />
                          {formatTimestamp(execution.startedAt)}
                        </span>
                        <Show when={duration !== null}>
                          <span>{formatDuration(duration!)}</span>
                        </Show>
                        <Show
                          when={execution.exitCode !== undefined && execution.status !== 'running'}
                        >
                          <span
                            class={
                              execution.exitCode === 0
                                ? 'text-[var(--success)]'
                                : 'text-[var(--error)]'
                            }
                          >
                            exit {execution.exitCode}
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
                          copyToClipboard(execution.output, execution.id)
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
                        <Show
                          when={copiedId() === execution.id}
                          fallback={<Copy class="w-3.5 h-3.5" />}
                        >
                          <Check class="w-3.5 h-3.5 text-[var(--success)]" />
                        </Show>
                      </button>
                      <ChevronRight
                        class={`
                          w-4 h-4
                          text-[var(--text-muted)]
                          transition-transform duration-[var(--duration-fast)]
                          ${isExpanded() ? 'rotate-90' : ''}
                        `}
                      />
                    </div>
                  </div>

                  {/* Output */}
                  <Show when={isExpanded()}>
                    <div
                      class="
                        p-3
                        bg-[#0a0a0b]
                        border-t border-[var(--border-subtle)]
                        max-h-64 overflow-y-auto
                      "
                    >
                      <pre class="text-xs font-mono whitespace-pre-wrap">
                        <Show
                          when={execution.output}
                          fallback={
                            <span class="text-[var(--text-muted)] italic">
                              {execution.status === 'running'
                                ? 'Waiting for output...'
                                : 'No output'}
                            </span>
                          }
                        >
                          <For each={parseAnsi(execution.output)}>
                            {(part) => <span class={part.class}>{part.text}</span>}
                          </For>
                        </Show>
                      </pre>
                    </div>
                  </Show>
                </div>
              )
            }}
          </For>
        </Show>
      </div>

      {/* Footer (hidden in compact/embedded mode) */}
      <Show when={!props.compact}>
        <div
          class="
            px-4 py-3
            border-t border-[var(--border-subtle)]
            bg-[var(--surface)]
          "
        >
          <div class="flex items-center justify-between text-xs text-[var(--text-muted)]">
            <div class="flex items-center gap-3">
              <Show when={executionStats().running > 0}>
                <span class="flex items-center gap-1">
                  <span class="w-2 h-2 rounded-full bg-[var(--accent)] animate-pulse" />
                  {executionStats().running} running
                </span>
              </Show>
              <Show when={executionStats().success > 0}>
                <span class="flex items-center gap-1">
                  <span class="w-2 h-2 rounded-full bg-[var(--success)]" />
                  {executionStats().success} success
                </span>
              </Show>
              <Show when={executionStats().error > 0}>
                <span class="flex items-center gap-1">
                  <span class="w-2 h-2 rounded-full bg-[var(--error)]" />
                  {executionStats().error} errors
                </span>
              </Show>
            </div>
            <span>{executionStats().total} total</span>
          </div>
        </div>
      </Show>
    </div>
  )
}
