/**
 * Terminal Panel Component
 *
 * Displays shell command execution output with ANSI color support.
 * Shows commands being executed and their results.
 */

import {
  Check,
  ChevronDown,
  ChevronRight,
  Clock,
  Copy,
  Loader2,
  Play,
  Terminal,
  Trash2,
  X,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'

// ANSI color code mapping to CSS classes
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
  // biome-ignore lint/suspicious/noControlCharactersInRegex: ANSI escape codes require control characters
  const regex = /\x1b\[(\d+)m/g
  let lastIndex = 0
  let currentClass = ''
  let match: RegExpExecArray | null

  // biome-ignore lint/suspicious/noAssignInExpressions: standard regex iteration pattern
  while ((match = regex.exec(text)) !== null) {
    if (match.index > lastIndex) {
      parts.push({ text: text.slice(lastIndex, match.index), class: currentClass })
    }
    const code = match[1]
    if (code === '0') {
      currentClass = ''
    } else if (ansiToClass[code]) {
      currentClass = ansiToClass[code]
    }
    lastIndex = regex.lastIndex
  }

  if (lastIndex < text.length) {
    parts.push({ text: text.slice(lastIndex), class: currentClass })
  }

  return parts.length > 0 ? parts : [{ text, class: '' }]
}

// Mock command execution data
const mockExecutions = [
  {
    id: '1',
    command: 'npm run build',
    status: 'success' as const,
    startTime: Date.now() - 45000,
    endTime: Date.now() - 42000,
    output: `\x1b[32m>\x1b[0m estela@0.1.0 build
\x1b[32m>\x1b[0m vite build

vite v6.4.1 building for production...
\x1b[32m✓\x1b[0m 2510 modules transformed.
dist/index.html                  0.46 kB
dist/assets/index-abc123.css    45.23 kB
dist/assets/index-def456.js    312.45 kB

\x1b[32m✓\x1b[0m built in 3.2s`,
  },
  {
    id: '2',
    command: 'git status',
    status: 'success' as const,
    startTime: Date.now() - 30000,
    endTime: Date.now() - 29800,
    output: `On branch main
Your branch is up to date with 'origin/main'.

Changes not staged for commit:
  \x1b[31mmodified:   src/App.tsx\x1b[0m
  \x1b[31mmodified:   src/components/ui/Button.tsx\x1b[0m

Untracked files:
  \x1b[31msrc/components/panels/TerminalPanel.tsx\x1b[0m`,
  },
  {
    id: '3',
    command: 'npm run lint',
    status: 'error' as const,
    startTime: Date.now() - 15000,
    endTime: Date.now() - 12000,
    output: `\x1b[31m✖\x1b[0m ESLint found 3 errors

\x1b[31msrc/App.tsx\x1b[0m
  12:5  \x1b[31merror\x1b[0m  'unused' is defined but never used  no-unused-vars

\x1b[31msrc/utils.ts\x1b[0m
  8:10  \x1b[31merror\x1b[0m  Missing return type  @typescript-eslint/explicit-function-return-type`,
    exitCode: 1,
  },
  {
    id: '4',
    command: 'npm install lodash',
    status: 'running' as const,
    startTime: Date.now() - 2000,
    output: `\x1b[36madded\x1b[0m 1 package in 1.2s`,
  },
]

type ExecutionStatus = 'running' | 'success' | 'error'

interface Execution {
  id: string
  command: string
  status: ExecutionStatus
  startTime: number
  endTime?: number
  output: string
  exitCode?: number
}

export const TerminalPanel: Component = () => {
  const [executions, setExecutions] = createSignal<Execution[]>(mockExecutions)
  const [expandedId, setExpandedId] = createSignal<string | null>(mockExecutions[0]?.id || null)
  const [copiedId, setCopiedId] = createSignal<string | null>(null)

  const formatDuration = (start: number, end?: number): string => {
    const duration = (end || Date.now()) - start
    if (duration < 1000) return `${duration}ms`
    return `${(duration / 1000).toFixed(1)}s`
  }

  const copyOutput = async (execution: Execution) => {
    // Strip ANSI codes for clipboard
    // biome-ignore lint/suspicious/noControlCharactersInRegex: ANSI escape codes require control characters
    const plainText = execution.output.replace(/\x1b\[\d+m/g, '')
    await navigator.clipboard.writeText(plainText)
    setCopiedId(execution.id)
    setTimeout(() => setCopiedId(null), 2000)
  }

  const clearExecutions = () => {
    setExecutions([])
    setExpandedId(null)
  }

  const statusConfig: Record<ExecutionStatus, { color: string; bg: string; icon: typeof Check }> = {
    running: { color: 'var(--info)', bg: 'var(--info-subtle)', icon: Loader2 },
    success: { color: 'var(--success)', bg: 'var(--success-subtle)', icon: Check },
    error: { color: 'var(--error)', bg: 'var(--error-subtle)', icon: X },
  }

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div
        class="
          flex items-center justify-between
          px-4 py-3
          border-b border-[var(--border-subtle)]
        "
      >
        <div class="flex items-center gap-3">
          <div
            class="
              p-2
              bg-[var(--accent-subtle)]
              rounded-[var(--radius-lg)]
            "
          >
            <Terminal class="w-5 h-5 text-[var(--accent)]" />
          </div>
          <div>
            <h2 class="text-sm font-semibold text-[var(--text-primary)]">Terminal</h2>
            <p class="text-xs text-[var(--text-muted)]">
              {executions().length} command{executions().length !== 1 ? 's' : ''}
            </p>
          </div>
        </div>
        <button
          type="button"
          onClick={clearExecutions}
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
      </div>

      {/* Executions List */}
      <div class="flex-1 overflow-y-auto">
        <Show
          when={executions().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center p-6">
              <Terminal class="w-10 h-10 text-[var(--text-muted)] mb-3" />
              <p class="text-sm text-[var(--text-secondary)]">No commands executed</p>
              <p class="text-xs text-[var(--text-muted)] mt-1">Command output will appear here</p>
            </div>
          }
        >
          <For each={executions()}>
            {(execution) => {
              const config = statusConfig[execution.status]
              const StatusIcon = config.icon
              const isExpanded = () => expandedId() === execution.id

              return (
                <div class="border-b border-[var(--border-subtle)]">
                  {/* Command Header */}
                  <button
                    type="button"
                    onClick={() => setExpandedId(isExpanded() ? null : execution.id)}
                    class="
                      w-full text-left
                      px-4 py-3
                      hover:bg-[var(--surface-raised)]
                      transition-colors duration-[var(--duration-fast)]
                    "
                  >
                    <div class="flex items-center gap-3">
                      {/* Expand Icon */}
                      {isExpanded() ? (
                        <ChevronDown class="w-4 h-4 text-[var(--text-muted)]" />
                      ) : (
                        <ChevronRight class="w-4 h-4 text-[var(--text-muted)]" />
                      )}

                      {/* Status Icon */}
                      <div class="p-1 rounded-full flex-shrink-0" style={{ background: config.bg }}>
                        <StatusIcon
                          class={`w-3.5 h-3.5 ${execution.status === 'running' ? 'animate-spin' : ''}`}
                          style={{ color: config.color }}
                        />
                      </div>

                      {/* Command */}
                      <div class="flex-1 min-w-0">
                        <div class="flex items-center gap-2">
                          <Play class="w-3 h-3 text-[var(--text-muted)]" />
                          <code class="text-sm font-mono text-[var(--text-primary)] truncate">
                            {execution.command}
                          </code>
                        </div>
                      </div>

                      {/* Duration */}
                      <span class="text-xs text-[var(--text-muted)] flex items-center gap-1 flex-shrink-0">
                        <Clock class="w-3 h-3" />
                        {formatDuration(execution.startTime, execution.endTime)}
                      </span>
                    </div>
                  </button>

                  {/* Output */}
                  <Show when={isExpanded()}>
                    <div class="px-4 pb-4">
                      <div
                        class="
                          relative
                          bg-[var(--surface-sunken)]
                          border border-[var(--border-subtle)]
                          rounded-[var(--radius-lg)]
                          overflow-hidden
                        "
                      >
                        {/* Output Header */}
                        <div class="flex items-center justify-between px-3 py-2 border-b border-[var(--border-subtle)]">
                          <span class="text-xs text-[var(--text-muted)]">Output</span>
                          <button
                            type="button"
                            onClick={() => copyOutput(execution)}
                            class="
                              flex items-center gap-1.5
                              px-2 py-1
                              text-xs
                              rounded-[var(--radius-md)]
                              text-[var(--text-tertiary)]
                              hover:text-[var(--text-primary)]
                              hover:bg-[var(--surface-raised)]
                              transition-colors duration-[var(--duration-fast)]
                            "
                          >
                            <Copy class="w-3 h-3" />
                            {copiedId() === execution.id ? 'Copied!' : 'Copy'}
                          </button>
                        </div>

                        {/* Output Content */}
                        <pre
                          class="
                            p-3
                            text-xs font-mono
                            text-[var(--text-secondary)]
                            overflow-x-auto
                            max-h-64
                            whitespace-pre-wrap
                            break-all
                          "
                        >
                          <For each={execution.output.split('\n')}>
                            {(line) => (
                              <div>
                                <For each={parseAnsi(line)}>
                                  {(part) => <span class={part.class}>{part.text}</span>}
                                </For>
                              </div>
                            )}
                          </For>
                        </pre>

                        {/* Exit Code */}
                        <Show when={execution.exitCode !== undefined}>
                          <div
                            class="
                              px-3 py-2
                              border-t border-[var(--border-subtle)]
                              text-xs
                            "
                            style={{
                              color: execution.exitCode === 0 ? 'var(--success)' : 'var(--error)',
                            }}
                          >
                            Exit code: {execution.exitCode}
                          </div>
                        </Show>
                      </div>
                    </div>
                  </Show>
                </div>
              )
            }}
          </For>
        </Show>
      </div>

      {/* Footer */}
      <div
        class="
          px-4 py-3
          border-t border-[var(--border-subtle)]
          bg-[var(--surface-sunken)]
        "
      >
        <div class="flex items-center justify-between text-xs text-[var(--text-muted)]">
          <div class="flex items-center gap-3">
            <span class="flex items-center gap-1" style={{ color: statusConfig.success.color }}>
              <Check class="w-3 h-3" />
              {executions().filter((e) => e.status === 'success').length}
            </span>
            <span class="flex items-center gap-1" style={{ color: statusConfig.error.color }}>
              <X class="w-3 h-3" />
              {executions().filter((e) => e.status === 'error').length}
            </span>
            <span class="flex items-center gap-1" style={{ color: statusConfig.running.color }}>
              <Loader2 class="w-3 h-3" />
              {executions().filter((e) => e.status === 'running').length}
            </span>
          </div>
          <span>Session commands</span>
        </div>
      </div>
    </div>
  )
}
