/**
 * File Operations Panel Component
 *
 * Shows file read/write/edit operations performed by agents.
 * Connected to the session store for real-time file tracking.
 */

import {
  ChevronDown,
  Clock,
  Eye,
  FileEdit,
  FilePlus2,
  type FileText,
  Filter,
  FolderOpen,
  Trash2,
} from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useSession } from '../../stores/session'
import type { FileOperationType } from '../../types'

// ============================================================================
// Operation Configuration
// ============================================================================

const operationConfig: Record<
  FileOperationType,
  { color: string; bg: string; icon: typeof FileText; label: string }
> = {
  read: { color: 'var(--accent)', bg: 'var(--accent-subtle)', icon: Eye, label: 'Read' },
  write: { color: 'var(--success)', bg: 'var(--success-subtle)', icon: FilePlus2, label: 'Write' },
  edit: { color: 'var(--warning)', bg: 'var(--warning-subtle)', icon: FileEdit, label: 'Edit' },
  delete: { color: 'var(--error)', bg: 'var(--error-subtle)', icon: Trash2, label: 'Delete' },
}

// ============================================================================
// Component
// ============================================================================

export const FileOperationsPanel: Component = () => {
  const { fileOperations, clearFileOperations } = useSession()
  const [selectedOperation, setSelectedOperation] = createSignal<string | null>(null)
  const [filterType, setFilterType] = createSignal<FileOperationType | 'all'>('all')
  const [showFilterMenu, setShowFilterMenu] = createSignal(false)

  const filteredOperations = createMemo(() => {
    const ops = fileOperations()
    if (filterType() === 'all') return ops
    return ops.filter((op) => op.type === filterType())
  })

  const formatTimestamp = (ts: number): string => {
    const diff = Date.now() - ts
    if (diff < 60000) return 'Just now'
    if (diff < 3600000) return `${Math.floor(diff / 60000)}m ago`
    if (diff < 86400000) return `${Math.floor(diff / 3600000)}h ago`
    return new Date(ts).toLocaleDateString()
  }

  const getFileName = (path: string): string => {
    return path.split('/').pop() || path
  }

  const getDirectory = (path: string): string => {
    const parts = path.split('/')
    if (parts.length <= 1) return ''
    parts.pop()
    return parts.join('/')
  }

  const operationCounts = createMemo(() => ({
    all: fileOperations().length,
    read: fileOperations().filter((op) => op.type === 'read').length,
    write: fileOperations().filter((op) => op.type === 'write').length,
    edit: fileOperations().filter((op) => op.type === 'edit').length,
    delete: fileOperations().filter((op) => op.type === 'delete').length,
  }))

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
              bg-[var(--warning-subtle)]
              rounded-[var(--radius-lg)]
            "
          >
            <FolderOpen class="w-5 h-5 text-[var(--warning)]" />
          </div>
          <div>
            <h2 class="text-sm font-semibold text-[var(--text-primary)]">File Operations</h2>
            <p class="text-xs text-[var(--text-muted)]">
              {operationCounts().all} operations · {operationCounts().edit} edits
            </p>
          </div>
        </div>

        {/* Filter Button */}
        <div class="relative">
          <button
            type="button"
            onClick={() => setShowFilterMenu(!showFilterMenu())}
            class={`
              flex items-center gap-1.5 px-2.5 py-1.5
              rounded-[var(--radius-md)]
              text-xs font-medium
              transition-colors duration-[var(--duration-fast)]
              ${
                filterType() !== 'all'
                  ? 'bg-[var(--accent-subtle)] text-[var(--accent)]'
                  : 'text-[var(--text-tertiary)] hover:text-[var(--text-primary)] hover:bg-[var(--surface-raised)]'
              }
            `}
          >
            <Filter class="w-3.5 h-3.5" />
            {filterType() === 'all'
              ? 'All'
              : operationConfig[filterType() as FileOperationType].label}
            <ChevronDown
              class={`w-3 h-3 transition-transform ${showFilterMenu() ? 'rotate-180' : ''}`}
            />
          </button>

          {/* Filter Dropdown */}
          <Show when={showFilterMenu()}>
            <div
              class="
                absolute right-0 top-full mt-1
                bg-[var(--surface-overlay)]
                border border-[var(--border-default)]
                rounded-[var(--radius-lg)]
                shadow-lg
                py-1 min-w-[140px]
                z-10
              "
            >
              <button
                type="button"
                onClick={() => {
                  setFilterType('all')
                  setShowFilterMenu(false)
                }}
                class={`
                  w-full flex items-center justify-between gap-2 px-3 py-2
                  text-xs text-left
                  ${filterType() === 'all' ? 'bg-[var(--accent-subtle)] text-[var(--accent)]' : 'text-[var(--text-secondary)] hover:bg-[var(--surface-raised)]'}
                `}
              >
                <span>All Operations</span>
                <span class="text-[var(--text-muted)]">{operationCounts().all}</span>
              </button>
              <For each={Object.entries(operationConfig)}>
                {([type, config]) => (
                  <button
                    type="button"
                    onClick={() => {
                      setFilterType(type as FileOperationType)
                      setShowFilterMenu(false)
                    }}
                    class={`
                      w-full flex items-center justify-between gap-2 px-3 py-2
                      text-xs text-left
                      ${filterType() === type ? 'bg-[var(--accent-subtle)] text-[var(--accent)]' : 'text-[var(--text-secondary)] hover:bg-[var(--surface-raised)]'}
                    `}
                  >
                    <span class="flex items-center gap-2">
                      <config.icon class="w-3.5 h-3.5" style={{ color: config.color }} />
                      {config.label}
                    </span>
                    <span class="text-[var(--text-muted)]">
                      {operationCounts()[type as FileOperationType]}
                    </span>
                  </button>
                )}
              </For>
            </div>
          </Show>
        </div>
      </div>

      {/* Operations List */}
      <div class="flex-1 overflow-y-auto p-3 space-y-2">
        <Show
          when={filteredOperations().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center p-6">
              <div class="p-4 bg-[var(--surface-raised)] rounded-full mb-4">
                <FolderOpen class="w-8 h-8 text-[var(--text-muted)]" />
              </div>
              <h3 class="text-sm font-medium text-[var(--text-secondary)] mb-1">
                No file operations
              </h3>
              <p class="text-xs text-[var(--text-muted)]">
                File reads, writes, and edits will appear here
              </p>
            </div>
          }
        >
          <For each={filteredOperations()}>
            {(operation) => {
              const config = operationConfig[operation.type]
              const OperationIcon = config.icon

              return (
                <button
                  type="button"
                  onClick={() =>
                    setSelectedOperation(selectedOperation() === operation.id ? null : operation.id)
                  }
                  class={`
                    w-full text-left
                    p-3
                    rounded-[var(--radius-lg)]
                    border
                    transition-all duration-[var(--duration-fast)]
                    ${
                      selectedOperation() === operation.id
                        ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                        : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                    }
                  `}
                >
                  <div class="flex items-start gap-3">
                    {/* Operation Icon */}
                    <div
                      class="p-2 rounded-[var(--radius-md)] flex-shrink-0"
                      style={{ background: config.bg }}
                    >
                      <OperationIcon class="w-4 h-4" style={{ color: config.color }} />
                    </div>

                    {/* Operation Info */}
                    <div class="flex-1 min-w-0">
                      <div class="flex items-center justify-between gap-2">
                        <span class="text-sm font-medium text-[var(--text-primary)] truncate">
                          {getFileName(operation.filePath)}
                        </span>
                        <span
                          class="flex items-center gap-1 px-1.5 py-0.5 text-[10px] font-medium rounded-full flex-shrink-0"
                          style={{ background: config.bg, color: config.color }}
                        >
                          {config.label}
                        </span>
                      </div>

                      <p class="text-xs text-[var(--text-muted)] mt-0.5 truncate">
                        {getDirectory(operation.filePath) || '/'}
                      </p>

                      {/* Meta info */}
                      <div class="flex items-center gap-3 mt-2 text-xs text-[var(--text-muted)]">
                        <span class="flex items-center gap-1">
                          <Clock class="w-3 h-3" />
                          {formatTimestamp(operation.timestamp)}
                        </span>
                        <Show when={operation.lines}>
                          <span>{operation.lines} lines</span>
                        </Show>
                        <Show
                          when={
                            operation.linesAdded !== undefined ||
                            operation.linesRemoved !== undefined
                          }
                        >
                          <span class="flex items-center gap-1">
                            <Show when={operation.linesAdded}>
                              <span class="text-[var(--success)]">+{operation.linesAdded}</span>
                            </Show>
                            <Show when={operation.linesRemoved}>
                              <span class="text-[var(--error)]">-{operation.linesRemoved}</span>
                            </Show>
                          </span>
                        </Show>
                      </div>

                      {/* Expanded details */}
                      <Show when={selectedOperation() === operation.id}>
                        <div class="mt-3 pt-3 border-t border-[var(--border-subtle)] space-y-2">
                          {/* Agent info */}
                          <Show when={operation.agentName}>
                            <div class="flex items-center gap-1.5 text-xs text-[var(--text-muted)]">
                              <span>Agent: {operation.agentName}</span>
                            </div>
                          </Show>

                          {/* Full path */}
                          <div class="text-xs text-[var(--text-secondary)] p-2 bg-[var(--surface-sunken)] rounded-[var(--radius-md)] font-mono break-all">
                            {operation.filePath}
                          </div>

                          {/* New file badge */}
                          <Show when={operation.isNew}>
                            <div class="flex items-center gap-1.5">
                              <span class="px-2 py-0.5 text-[10px] font-medium bg-[var(--success-subtle)] text-[var(--success)] rounded-full">
                                New File
                              </span>
                            </div>
                          </Show>
                        </div>
                      </Show>
                    </div>
                  </div>
                </button>
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
            <For each={Object.entries(operationConfig)}>
              {([type, config]) => (
                <Show when={operationCounts()[type as FileOperationType] > 0}>
                  <span class="flex items-center gap-1">
                    <span class="w-2 h-2 rounded-full" style={{ background: config.color }} />
                    {operationCounts()[type as FileOperationType]} {config.label}
                  </span>
                </Show>
              )}
            </For>
          </div>
          <Show when={fileOperations().length > 0}>
            <button
              type="button"
              onClick={() => clearFileOperations()}
              class="text-[var(--text-tertiary)] hover:text-[var(--error)] transition-colors"
            >
              Clear All
            </button>
          </Show>
        </div>
      </div>
    </div>
  )
}
