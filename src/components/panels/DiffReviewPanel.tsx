/**
 * Diff Review Panel
 *
 * Aggregate view of all file changes in the current session.
 * Shows per-file diffs and hunk-level review actions.
 * Latest operation per file wins (deduplication).
 */

import {
  ChevronDown,
  ChevronRight,
  ChevronsDownUp,
  ChevronsUpDown,
  ExternalLink,
  FileEdit,
  FilePlus2,
  FolderOpen,
  Minus,
  Plus,
  Trash2,
} from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { type EditorInfo, getAvailableEditors, openInEditor } from '../../services/ide-integration'
import { useSession } from '../../stores/session'
import type { FileOperation, FileOperationType } from '../../types'
import { DiffReview } from './DiffReview'

// ============================================================================
// Helpers
// ============================================================================

const opIcons: Record<
  FileOperationType,
  Component<{ class?: string; style?: Record<string, string> }>
> = {
  read: FolderOpen,
  write: FilePlus2,
  edit: FileEdit,
  delete: Trash2,
}

const opColors: Record<FileOperationType, string> = {
  read: 'var(--accent)',
  write: 'var(--success)',
  edit: 'var(--warning)',
  delete: 'var(--error)',
}

function getFileName(path: string): string {
  return path.split('/').pop() || path
}

function getDirectory(path: string): string {
  const parts = path.split('/')
  if (parts.length <= 1) return ''
  parts.pop()
  // Show last 2 segments for context
  return parts.slice(-2).join('/')
}

// ============================================================================
// Component
// ============================================================================

interface DiffReviewPanelProps {
  compact?: boolean
}

export const DiffReviewPanel: Component<DiffReviewPanelProps> = () => {
  const { fileOperations } = useSession()
  const [expandedFiles, setExpandedFiles] = createSignal<Set<string>>(new Set())
  const [editors, setEditors] = createSignal<EditorInfo[]>([])

  // Detect available editors once on mount
  void getAvailableEditors().then(setEditors)

  // Aggregate: latest operation per file, filter out reads and ops without content
  const reviewableOps = createMemo(() => {
    const ops = fileOperations()
    const byFile = new Map<string, FileOperation>()

    for (const op of ops) {
      if (op.type === 'read') continue
      if (!op.originalContent && !op.newContent) continue
      // Latest operation wins
      const existing = byFile.get(op.filePath)
      if (!existing || op.timestamp > existing.timestamp) {
        byFile.set(op.filePath, op)
      }
    }

    return Array.from(byFile.values()).sort((a, b) => b.timestamp - a.timestamp)
  })

  // Aggregate stats
  const stats = createMemo(() => {
    const ops = reviewableOps()
    let totalAdded = 0
    let totalRemoved = 0
    for (const op of ops) {
      totalAdded += op.linesAdded ?? 0
      totalRemoved += op.linesRemoved ?? 0
    }
    return { files: ops.length, added: totalAdded, removed: totalRemoved }
  })

  const toggleFile = (filePath: string) => {
    const current = new Set<string>(expandedFiles())
    if (current.has(filePath)) {
      current.delete(filePath)
    } else {
      current.add(filePath)
    }
    setExpandedFiles(current)
  }

  const expandAll = () => {
    setExpandedFiles(new Set<string>(reviewableOps().map((op) => op.filePath)))
  }

  const collapseAll = () => {
    setExpandedFiles(new Set<string>())
  }

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center justify-between px-3 py-2 border-b border-[var(--border-subtle)]">
        <div class="flex items-center gap-2 text-xs">
          <span class="text-[var(--text-secondary)] font-medium">
            {stats().files} file{stats().files !== 1 ? 's' : ''} changed
          </span>
          <Show when={stats().added > 0}>
            <span class="flex items-center gap-0.5 text-[var(--success)]">
              <Plus class="w-3 h-3" />
              {stats().added}
            </span>
          </Show>
          <Show when={stats().removed > 0}>
            <span class="flex items-center gap-0.5 text-[var(--error)]">
              <Minus class="w-3 h-3" />
              {stats().removed}
            </span>
          </Show>
        </div>
        <Show when={reviewableOps().length > 0}>
          <div class="flex items-center gap-1">
            <button
              type="button"
              onClick={expandAll}
              class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
              title="Expand all"
            >
              <ChevronsUpDown class="w-3.5 h-3.5" />
            </button>
            <button
              type="button"
              onClick={collapseAll}
              class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
              title="Collapse all"
            >
              <ChevronsDownUp class="w-3.5 h-3.5" />
            </button>
          </div>
        </Show>
      </div>

      {/* File list */}
      <div class="flex-1 overflow-y-auto">
        <Show
          when={reviewableOps().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center p-6">
              <div class="p-4 bg-[var(--surface-raised)] rounded-full mb-3">
                <FileEdit class="w-7 h-7 text-[var(--text-muted)]" />
              </div>
              <h3 class="text-sm font-medium text-[var(--text-secondary)] mb-1">No file changes</h3>
              <p class="text-xs text-[var(--text-muted)]">
                File edits and writes will appear here for review
              </p>
            </div>
          }
        >
          <For each={reviewableOps()}>
            {(op) => {
              const Icon = opIcons[op.type]
              const color = opColors[op.type]
              const isExpanded = () => expandedFiles().has(op.filePath)

              return (
                <div class="border-b border-[var(--border-subtle)]">
                  {/* File header row */}
                  <button
                    type="button"
                    onClick={() => toggleFile(op.filePath)}
                    class="w-full flex items-center gap-2 px-3 py-2 text-left hover:bg-[var(--surface-raised)] transition-colors"
                  >
                    <Show
                      when={isExpanded()}
                      fallback={
                        <ChevronRight class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />
                      }
                    >
                      <ChevronDown class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />
                    </Show>

                    <Icon class="w-3.5 h-3.5 flex-shrink-0" style={{ color }} />

                    <div class="flex-1 min-w-0 flex items-center gap-1.5">
                      <span class="text-xs font-medium text-[var(--text-primary)] truncate">
                        {getFileName(op.filePath)}
                      </span>
                      <span class="text-[10px] text-[var(--text-muted)] truncate">
                        {getDirectory(op.filePath)}
                      </span>
                    </div>

                    <div class="flex items-center gap-1.5 text-[10px] flex-shrink-0">
                      <Show when={op.isNew}>
                        <span class="px-1.5 py-0.5 bg-[var(--success-subtle)] text-[var(--success)] rounded-full font-medium">
                          new
                        </span>
                      </Show>
                      <Show when={op.linesAdded}>
                        <span class="text-[var(--success)]">+{op.linesAdded}</span>
                      </Show>
                      <Show when={op.linesRemoved}>
                        <span class="text-[var(--error)]">-{op.linesRemoved}</span>
                      </Show>
                      <Show when={editors().length > 0 && op.type !== 'delete'}>
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation()
                            void openInEditor(editors()[0]!.command, op.filePath)
                          }}
                          class="p-0.5 rounded hover:bg-[var(--alpha-white-05)] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors cursor-pointer"
                          title={`Open in ${editors()[0]!.name}`}
                        >
                          <ExternalLink class="w-3 h-3" />
                        </button>
                      </Show>
                    </div>
                  </button>

                  {/* Expanded diff view */}
                  <Show when={isExpanded()}>
                    <div class="px-2 pb-2">
                      <DiffReview
                        oldContent={op.originalContent ?? ''}
                        newContent={op.newContent ?? ''}
                        filename={getFileName(op.filePath)}
                      />
                    </div>
                  </Show>
                </div>
              )
            }}
          </For>
        </Show>
      </div>
    </div>
  )
}
