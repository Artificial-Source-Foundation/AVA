/**
 * Diff Review Panel
 *
 * Aggregate view of all file changes in the current session.
 * Shows per-file diffs and hunk-level review actions.
 * Latest operation per file wins (deduplication).
 */

import { ChevronsDownUp, ChevronsUpDown, FileEdit, Minus, Plus } from 'lucide-solid'

/** Emit event via DOM CustomEvent (replaces @ava/core-v2/extensions emitEvent) */
function emitEvent(eventName: string, data: unknown): void {
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent(`ava:${eventName}`, { detail: data }))
  }
}

import { type Component, createEffect, createMemo, createSignal, For, Show } from 'solid-js'
import { useExtensionEvent } from '../../hooks/useExtensionEvents'
import { type EditorInfo, getAvailableEditors } from '../../services/ide-integration'
import { useSession } from '../../stores/session'
import type { FileOperation } from '../../types'
import type {
  DiffHunksUpdatedEvent,
  DiffReviewPanelProps,
  HunkReviewItem,
  HunkReviewStatus,
} from './diff-review/diff-review-helpers'
import { FileChangeRow } from './diff-review/FileChangeRow'
import { HunkReviewList } from './diff-review/HunkReviewList'

// ============================================================================
// Component
// ============================================================================

export const DiffReviewPanel: Component<DiffReviewPanelProps> = () => {
  const session = useSession()
  const currentSession =
    typeof session.currentSession === 'function' ? session.currentSession : () => null
  const fileOperations = session.fileOperations
  const [expandedFiles, setExpandedFiles] = createSignal<Set<string>>(new Set())
  const [editors, setEditors] = createSignal<EditorInfo[]>([])
  const [hunkItems, setHunkItems] = createSignal<HunkReviewItem[]>([])
  const [hunkSummary, setHunkSummary] = createSignal({
    total: 0,
    pending: 0,
    accepted: 0,
    rejected: 0,
  })
  const latestHunkEvent = useExtensionEvent<DiffHunksUpdatedEvent>('diff:hunks-updated')

  void getAvailableEditors().then(setEditors)

  createEffect(() => {
    const event = latestHunkEvent()
    const activeSessionId = currentSession()?.id
    if (!event || !activeSessionId || event.sessionId !== activeSessionId) return
    setHunkItems(event.items)
    setHunkSummary(event.summary)
  })

  const reviewableOps = createMemo(() => {
    const ops = fileOperations()
    const byFile = new Map<string, FileOperation>()
    for (const op of ops) {
      if (op.type === 'read') continue
      if (!op.originalContent && !op.newContent) continue
      const existing = byFile.get(op.filePath)
      if (!existing || op.timestamp > existing.timestamp) {
        byFile.set(op.filePath, op)
      }
    }
    return Array.from(byFile.values()).sort((a, b) => b.timestamp - a.timestamp)
  })

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
    if (current.has(filePath)) current.delete(filePath)
    else current.add(filePath)
    setExpandedFiles(current)
  }

  const expandAll = () => {
    setExpandedFiles(new Set<string>(reviewableOps().map((op) => op.filePath)))
  }

  const collapseAll = () => setExpandedFiles(new Set<string>())

  const setHunkStatus = (hunkId: string, status: HunkReviewStatus) => {
    const sessionId = currentSession()?.id
    if (!sessionId) return
    emitEvent('diff:hunk-status:update', { sessionId, hunkId, status })
  }

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center justify-between px-3 py-2 border-b border-[var(--border-subtle)]">
        <div class="flex items-center gap-2 text-xs">
          <Show
            when={hunkSummary().total > 0}
            fallback={
              <span class="text-[var(--text-secondary)] font-medium">
                {stats().files} file{stats().files !== 1 ? 's' : ''} changed
              </span>
            }
          >
            <span class="text-[var(--text-secondary)] font-medium">
              {hunkSummary().total} hunks
            </span>
            <span class="text-[var(--warning)]">{hunkSummary().pending} pending</span>
            <span class="text-[var(--success)]">{hunkSummary().accepted} accepted</span>
            <span class="text-[var(--error)]">{hunkSummary().rejected} rejected</span>
          </Show>
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
        <Show when={hunkItems().length === 0 && reviewableOps().length > 0}>
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
        <Show when={hunkItems().length > 0}>
          <HunkReviewList items={hunkItems()} onSetStatus={setHunkStatus} />
        </Show>
        <Show
          when={hunkItems().length === 0 && reviewableOps().length > 0}
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
            {(op) => (
              <FileChangeRow
                op={op}
                isExpanded={expandedFiles().has(op.filePath)}
                editors={editors()}
                onToggle={() => toggleFile(op.filePath)}
              />
            )}
          </For>
        </Show>
      </div>
    </div>
  )
}
