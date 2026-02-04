/**
 * File Operations Panel Component
 *
 * Shows file read/write/edit operations performed by agents.
 * Premium design with operation timeline and file previews.
 */

import {
  Check,
  ChevronDown,
  Clock,
  Eye,
  FileEdit,
  FilePlus2,
  FileText,
  Filter,
  FolderOpen,
  Trash2,
  X,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'

// Mock file operation data for design preview
const mockOperations = [
  {
    id: '1',
    type: 'read' as const,
    file: 'src/components/chat/MessageBubble.tsx',
    timestamp: Date.now() - 5000,
    agent: 'Code Analyzer',
    lines: 104,
  },
  {
    id: '2',
    type: 'write' as const,
    file: 'src/components/ui/NewComponent.tsx',
    timestamp: Date.now() - 15000,
    agent: 'Code Generator',
    lines: 87,
    isNew: true,
  },
  {
    id: '3',
    type: 'edit' as const,
    file: 'src/styles/tokens.css',
    timestamp: Date.now() - 45000,
    agent: 'Style Optimizer',
    linesAdded: 12,
    linesRemoved: 3,
  },
  {
    id: '4',
    type: 'read' as const,
    file: 'package.json',
    timestamp: Date.now() - 60000,
    agent: 'Dependency Checker',
    lines: 52,
  },
  {
    id: '5',
    type: 'delete' as const,
    file: 'src/deprecated/OldComponent.tsx',
    timestamp: Date.now() - 120000,
    agent: 'Cleanup Agent',
    lines: 156,
  },
]

type OperationType = 'read' | 'write' | 'edit' | 'delete'

const operationConfig: Record<
  OperationType,
  { color: string; bg: string; icon: typeof FileText; label: string }
> = {
  read: { color: 'var(--accent)', bg: 'var(--accent-subtle)', icon: Eye, label: 'Read' },
  write: { color: 'var(--success)', bg: 'var(--success-subtle)', icon: FilePlus2, label: 'Write' },
  edit: { color: 'var(--warning)', bg: 'var(--warning-subtle)', icon: FileEdit, label: 'Edit' },
  delete: { color: 'var(--error)', bg: 'var(--error-subtle)', icon: Trash2, label: 'Delete' },
}

export const FileOperationsPanel: Component = () => {
  const [filter, setFilter] = createSignal<OperationType | 'all'>('all')
  const [expandedId, setExpandedId] = createSignal<string | null>(null)

  const formatTime = (timestamp: number): string => {
    const seconds = Math.floor((Date.now() - timestamp) / 1000)
    if (seconds < 60) return `${seconds}s ago`
    const minutes = Math.floor(seconds / 60)
    if (minutes < 60) return `${minutes}m ago`
    const hours = Math.floor(minutes / 60)
    return `${hours}h ago`
  }

  const getFileName = (path: string): string => {
    return path.split('/').pop() || path
  }

  const getDirectory = (path: string): string => {
    const parts = path.split('/')
    parts.pop()
    return parts.join('/') || '/'
  }

  const filteredOperations = () => {
    if (filter() === 'all') return mockOperations
    return mockOperations.filter((op) => op.type === filter())
  }

  const operationCounts = () => ({
    all: mockOperations.length,
    read: mockOperations.filter((o) => o.type === 'read').length,
    write: mockOperations.filter((o) => o.type === 'write').length,
    edit: mockOperations.filter((o) => o.type === 'edit').length,
    delete: mockOperations.filter((o) => o.type === 'delete').length,
  })

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
              bg-[var(--success-subtle)]
              rounded-[var(--radius-lg)]
            "
          >
            <FolderOpen class="w-5 h-5 text-[var(--success)]" />
          </div>
          <div>
            <h2 class="text-sm font-semibold text-[var(--text-primary)]">File Operations</h2>
            <p class="text-xs text-[var(--text-muted)]">{mockOperations.length} operations</p>
          </div>
        </div>
      </div>

      {/* Filter Tabs */}
      <div class="flex gap-1 px-3 py-2 border-b border-[var(--border-subtle)] overflow-x-auto">
        <For each={['all', 'read', 'write', 'edit', 'delete'] as const}>
          {(type) => {
            const isActive = () => filter() === type
            const count = () => operationCounts()[type]
            const config = type !== 'all' ? operationConfig[type] : null

            return (
              <button
                type="button"
                onClick={() => setFilter(type)}
                class={`
                  flex items-center gap-1.5
                  px-3 py-1.5
                  rounded-[var(--radius-md)]
                  text-xs font-medium
                  whitespace-nowrap
                  transition-all duration-[var(--duration-fast)]
                  ${
                    isActive()
                      ? 'bg-[var(--accent)] text-white'
                      : 'text-[var(--text-tertiary)] hover:text-[var(--text-primary)] hover:bg-[var(--surface-raised)]'
                  }
                `}
              >
                {config ? <config.icon class="w-3 h-3" /> : <Filter class="w-3 h-3" />}
                {type === 'all' ? 'All' : config?.label}
                <span
                  class={`
                    px-1.5 py-0.5
                    rounded-full
                    text-[10px]
                    ${isActive() ? 'bg-white/20' : 'bg-[var(--surface-raised)]'}
                  `}
                >
                  {count()}
                </span>
              </button>
            )
          }}
        </For>
      </div>

      {/* Operations List */}
      <div class="flex-1 overflow-y-auto">
        <For
          each={filteredOperations()}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center p-6">
              <FileText class="w-10 h-10 text-[var(--text-muted)] mb-3" />
              <p class="text-sm text-[var(--text-secondary)]">No operations found</p>
              <p class="text-xs text-[var(--text-muted)] mt-1">File operations will appear here</p>
            </div>
          }
        >
          {(operation) => {
            const config = operationConfig[operation.type]
            const Icon = config.icon
            const isExpanded = expandedId() === operation.id

            return (
              <button
                type="button"
                onClick={() => setExpandedId(isExpanded ? null : operation.id)}
                class="
                  w-full text-left
                  px-4 py-3
                  border-b border-[var(--border-subtle)]
                  hover:bg-[var(--surface-raised)]
                  transition-colors duration-[var(--duration-fast)]
                "
              >
                <div class="flex items-start gap-3">
                  {/* Operation Icon */}
                  <div
                    class="p-1.5 rounded-[var(--radius-md)] flex-shrink-0"
                    style={{ background: config.bg }}
                  >
                    <Icon class="w-4 h-4" style={{ color: config.color }} />
                  </div>

                  {/* Operation Info */}
                  <div class="flex-1 min-w-0">
                    <div class="flex items-center gap-2">
                      <span class="text-sm font-medium text-[var(--text-primary)] truncate">
                        {getFileName(operation.file)}
                      </span>
                      <Show when={operation.isNew}>
                        <span
                          class="
                            px-1.5 py-0.5
                            bg-[var(--success-subtle)]
                            text-[var(--success)]
                            text-[10px] font-medium
                            rounded-full
                          "
                        >
                          New
                        </span>
                      </Show>
                    </div>
                    <p class="text-xs text-[var(--text-muted)] truncate mt-0.5">
                      {getDirectory(operation.file)}
                    </p>

                    {/* Expanded details */}
                    <Show when={isExpanded}>
                      <div class="mt-3 space-y-2">
                        <div class="flex items-center gap-4 text-xs">
                          <span class="text-[var(--text-tertiary)]">
                            Agent:{' '}
                            <span class="text-[var(--text-secondary)]">{operation.agent}</span>
                          </span>
                          <Show when={operation.lines}>
                            <span class="text-[var(--text-tertiary)]">
                              Lines:{' '}
                              <span class="text-[var(--text-secondary)]">{operation.lines}</span>
                            </span>
                          </Show>
                        </div>
                        <Show when={operation.type === 'edit'}>
                          <div class="flex items-center gap-3 text-xs">
                            <span class="flex items-center gap-1 text-[var(--success)]">
                              <Check class="w-3 h-3" />+{operation.linesAdded}
                            </span>
                            <span class="flex items-center gap-1 text-[var(--error)]">
                              <X class="w-3 h-3" />-{operation.linesRemoved}
                            </span>
                          </div>
                        </Show>
                      </div>
                    </Show>
                  </div>

                  {/* Timestamp & Expand */}
                  <div class="flex items-center gap-2 flex-shrink-0">
                    <span class="text-xs text-[var(--text-muted)] flex items-center gap-1">
                      <Clock class="w-3 h-3" />
                      {formatTime(operation.timestamp)}
                    </span>
                    <ChevronDown
                      class={`
                        w-4 h-4 text-[var(--text-muted)]
                        transition-transform duration-[var(--duration-fast)]
                        ${isExpanded ? 'rotate-180' : ''}
                      `}
                    />
                  </div>
                </div>
              </button>
            )
          }}
        </For>
      </div>

      {/* Summary Footer */}
      <div
        class="
          px-4 py-3
          border-t border-[var(--border-subtle)]
          bg-[var(--surface-sunken)]
        "
      >
        <div class="flex items-center justify-between text-xs text-[var(--text-muted)]">
          <div class="flex items-center gap-3">
            <span style={{ color: operationConfig.read.color }}>
              {operationCounts().read} reads
            </span>
            <span style={{ color: operationConfig.write.color }}>
              {operationCounts().write} writes
            </span>
            <span style={{ color: operationConfig.edit.color }}>
              {operationCounts().edit} edits
            </span>
          </div>
          <span>Session total: {mockOperations.length}</span>
        </div>
      </div>
    </div>
  )
}
