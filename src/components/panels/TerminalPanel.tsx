/**
 * Terminal Panel Component
 *
 * Displays shell command execution output with ANSI color support.
 * Connected to the session store for real-time command tracking.
 *
 * Design: rounded-12 card, #111114 bg, #0F0F12 header, green terminal icon,
 * green "N runs" badge, monospace output, blue $ prompt + cursor.
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
    <div class="flex flex-col h-full overflow-hidden rounded-[10px] bg-[var(--surface)] border border-[var(--border-subtle)]">
      {/* Header */}
      <Show when={!props.compact}>
        <div class="flex items-center justify-between h-10 px-3 bg-[var(--background-subtle)] shrink-0">
          <div class="flex items-center gap-2">
            <Terminal class="w-3.5 h-3.5 text-[var(--system-green)]" />
            <span class="text-xs font-medium text-[var(--text-secondary)]">Terminal</span>
            <Show when={executionStats().total > 0}>
              <span class="inline-flex items-center px-1.5 py-0.5 text-[10px] font-medium rounded-md bg-[var(--system-green)]/20 text-[var(--system-green)]">
                {executionStats().total} run{executionStats().total !== 1 ? 's' : ''}
              </span>
            </Show>
          </div>
          <div class="flex items-center gap-1">
            <Show when={terminalExecutions().length > 0}>
              <button
                type="button"
                onClick={() => clearTerminalExecutions()}
                class="p-1 rounded-[6px] text-[var(--text-muted)] hover:text-[var(--system-red)] transition-colors"
                title="Clear all"
              >
                <Trash2 class="w-[13px] h-[13px]" />
              </button>
            </Show>
          </div>
        </div>
      </Show>

      {/* Executions List */}
      <div class="flex-1 overflow-y-auto p-3 space-y-1.5">
        <Show
          when={terminalExecutions().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center">
              <Terminal class="w-6 h-6 text-[var(--text-muted)] mb-2" />
              <p class="text-[11px] text-[var(--text-muted)]">Command output will appear here</p>
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
    </div>
  )
}
