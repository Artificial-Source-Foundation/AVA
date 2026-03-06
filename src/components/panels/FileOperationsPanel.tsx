/**
 * File Operations Panel Component
 *
 * Shows file read/write/edit operations performed by agents.
 * Connected to the session store for real-time file tracking.
 */

import { FolderOpen } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { type EditorInfo, getAvailableEditors } from '../../services/ide-integration'
import { useSession } from '../../stores/session'
import type { FileOperationType } from '../../types'
import { FilterDropdown } from './file-operations/FilterDropdown'
import {
  type FileOperationsPanelProps,
  operationConfig,
} from './file-operations/file-operations-helpers'
import { OperationCard } from './file-operations/OperationCard'

export const FileOperationsPanel: Component<FileOperationsPanelProps> = (props) => {
  const { fileOperations, clearFileOperations } = useSession()
  const [selectedOperation, setSelectedOperation] = createSignal<string | null>(null)
  const [filterType, setFilterType] = createSignal<FileOperationType | 'all'>('all')
  const [showFilterMenu, setShowFilterMenu] = createSignal(false)
  const [editors, setEditors] = createSignal<EditorInfo[]>([])

  // Detect available editors once on mount
  void getAvailableEditors().then(setEditors)

  const filteredOperations = createMemo(() => {
    const ops = fileOperations()
    if (filterType() === 'all') return ops
    return ops.filter((op) => op.type === filterType())
  })

  const operationCounts = createMemo(() => ({
    all: fileOperations().length,
    read: fileOperations().filter((op) => op.type === 'read').length,
    write: fileOperations().filter((op) => op.type === 'write').length,
    edit: fileOperations().filter((op) => op.type === 'edit').length,
    delete: fileOperations().filter((op) => op.type === 'delete').length,
  }))

  return (
    <div class="flex flex-col h-full">
      {/* Header (hidden in compact/embedded mode) */}
      <Show when={!props.compact}>
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

          <FilterDropdown
            filterType={filterType()}
            showMenu={showFilterMenu()}
            counts={operationCounts()}
            onFilterChange={(type) => {
              setFilterType(type)
              setShowFilterMenu(false)
            }}
            onToggleMenu={() => setShowFilterMenu(!showFilterMenu())}
          />
        </div>
      </Show>

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
            {(operation) => (
              <OperationCard
                operation={operation}
                isSelected={selectedOperation() === operation.id}
                editors={editors()}
                onToggle={() =>
                  setSelectedOperation(selectedOperation() === operation.id ? null : operation.id)
                }
              />
            )}
          </For>
        </Show>
      </div>

      {/* Footer (hidden in compact/embedded mode) */}
      <Show when={!props.compact}>
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
      </Show>
    </div>
  )
}
