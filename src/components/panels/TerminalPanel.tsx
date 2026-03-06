/**
 * Terminal Panel Component
 *
 * Displays shell command execution output with ANSI color support.
 * Connected to the session store for real-time command tracking.
 */

import { Terminal, Trash2 } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useSession } from '../../stores/session'
import { ExecutionItem } from './terminal/ExecutionItem'

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
            density-section-px density-section-py
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
      <div class="flex-1 overflow-y-auto density-section space-y-2">
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
            {(execution) => (
              <ExecutionItem
                execution={execution}
                isExpanded={expandedIds().has(execution.id)}
                isCopied={copiedId() === execution.id}
                onToggle={() => toggleExpanded(execution.id)}
                onCopy={() => void copyToClipboard(execution.output, execution.id)}
              />
            )}
          </For>
        </Show>
      </div>

      {/* Footer (hidden in compact/embedded mode) */}
      <Show when={!props.compact}>
        <div
          class="
            density-section-px density-section-py
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
