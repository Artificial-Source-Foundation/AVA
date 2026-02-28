/**
 * Sandbox Review Dialog
 *
 * Modal for reviewing all pending sandbox changes with per-file diff view,
 * accept/reject checkboxes, and bulk actions.
 */

import { Check, ChevronDown, ChevronRight, File, FilePlus, FileX, Shield, X } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { computeSimpleDiff, type DiffLine } from '../../lib/simple-diff'
import type { PendingChange } from '../../stores/sandbox'

interface SandboxReviewDialogProps {
  open: boolean
  changes: PendingChange[]
  onApplySelected: (paths: string[]) => Promise<void>
  onApplyAll: () => Promise<void>
  onRejectAll: () => void
  onClose: () => void
}

const FileIcon: Component<{ type: PendingChange['type'] }> = (props) => (
  <Show
    when={props.type === 'create'}
    fallback={
      <Show
        when={props.type === 'delete'}
        fallback={<File class="w-3.5 h-3.5 text-[var(--accent)]" />}
      >
        <FileX class="w-3.5 h-3.5 text-[var(--error)]" />
      </Show>
    }
  >
    <FilePlus class="w-3.5 h-3.5 text-[var(--success)]" />
  </Show>
)

const shortName = (path: string) => {
  const parts = path.split('/')
  return parts[parts.length - 1] || path
}

const displayPath = (path: string) => {
  const parts = path.split('/')
  if (parts.length <= 3) return path
  return '.../' + parts.slice(-3).join('/')
}

function buildDiff(change: PendingChange): DiffLine[] {
  if (change.type === 'delete') {
    return [{ type: 'removed', content: '(entire file deleted)', lineNum: null }]
  }
  if (change.type === 'create') {
    return change.newContent.split('\n').map((line, i) => ({
      type: 'added' as const,
      content: line,
      lineNum: i + 1,
    }))
  }
  return computeSimpleDiff(change.originalContent, change.newContent)
}

export const SandboxReviewDialog: Component<SandboxReviewDialogProps> = (props) => {
  const [selected, setSelected] = createSignal<Set<string>>(new Set())
  const [expandedFiles, setExpandedFiles] = createSignal<Set<string>>(new Set())
  const [applying, setApplying] = createSignal(false)

  const allPaths = createMemo(() => props.changes.map((c) => c.filePath))
  const selectAll = () => setSelected(new Set(allPaths()))
  const deselectAll = () => setSelected(new Set<string>())
  const selectedCount = createMemo(() => selected().size)

  const toggleFile = (path: string) => {
    setSelected((prev) => {
      const next = new Set(prev)
      if (next.has(path)) next.delete(path)
      else next.add(path)
      return next
    })
  }

  const toggleExpand = (path: string) => {
    setExpandedFiles((prev) => {
      const next = new Set(prev)
      if (next.has(path)) next.delete(path)
      else next.add(path)
      return next
    })
  }

  const summary = createMemo(() => {
    const c = props.changes
    return {
      total: c.length,
      created: c.filter((x) => x.type === 'create').length,
      modified: c.filter((x) => x.type === 'modify').length,
      deleted: c.filter((x) => x.type === 'delete').length,
    }
  })

  const handleApplySelected = async () => {
    setApplying(true)
    try {
      await props.onApplySelected(Array.from(selected()))
      setSelected(new Set<string>())
    } finally {
      setApplying(false)
    }
  }

  const handleApplyAll = async () => {
    setApplying(true)
    try {
      await props.onApplyAll()
    } finally {
      setApplying(false)
    }
  }

  return (
    <Show when={props.open}>
      <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
        <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] shadow-2xl w-full max-w-2xl max-h-[80vh] flex flex-col">
          {/* Header */}
          <div class="flex items-center justify-between px-5 py-4 border-b border-[var(--border-subtle)]">
            <div class="flex items-center gap-2.5">
              <Shield class="w-4.5 h-4.5 text-[var(--accent)]" />
              <div>
                <h3 class="text-sm font-semibold text-[var(--text-primary)]">Sandbox Review</h3>
                <p class="text-[10px] text-[var(--text-muted)]">
                  {summary().total} file{summary().total !== 1 ? 's' : ''} changed
                  <Show when={summary().created > 0}>
                    <span class="text-[var(--success)]"> +{summary().created} new</span>
                  </Show>
                  <Show when={summary().modified > 0}>
                    <span class="text-[var(--accent)]"> ~{summary().modified} modified</span>
                  </Show>
                  <Show when={summary().deleted > 0}>
                    <span class="text-[var(--error)]"> -{summary().deleted} deleted</span>
                  </Show>
                </p>
              </div>
            </div>
            <button
              type="button"
              onClick={props.onClose}
              class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--surface-raised)]"
              aria-label="Close"
            >
              <X class="w-4 h-4" />
            </button>
          </div>

          {/* Selection controls */}
          <div class="flex items-center justify-between px-5 py-2 border-b border-[var(--border-subtle)] bg-[var(--surface-sunken)]">
            <div class="flex items-center gap-3 text-[10px] text-[var(--text-muted)]">
              <button type="button" onClick={selectAll} class="hover:text-[var(--accent)]">
                Select all
              </button>
              <button type="button" onClick={deselectAll} class="hover:text-[var(--accent)]">
                Deselect all
              </button>
              <span>
                {selectedCount()} of {summary().total} selected
              </span>
            </div>
          </div>

          {/* File list */}
          <div class="flex-1 overflow-y-auto min-h-0">
            <Show
              when={props.changes.length > 0}
              fallback={
                <p class="text-sm text-[var(--text-muted)] text-center py-8">No pending changes.</p>
              }
            >
              <For each={props.changes}>
                {(change) => {
                  const isSelected = () => selected().has(change.filePath)
                  const isExpanded = () => expandedFiles().has(change.filePath)
                  const diff = createMemo(() => buildDiff(change))
                  return (
                    <div class="border-b border-[var(--border-subtle)]">
                      <div class="flex items-center gap-2 px-4 py-2 hover:bg-[var(--surface-raised)]">
                        <button
                          type="button"
                          onClick={() => toggleFile(change.filePath)}
                          class={`w-4 h-4 rounded border flex items-center justify-center flex-shrink-0 ${isSelected() ? 'bg-[var(--accent)] border-[var(--accent)] text-white' : 'border-[var(--border-default)] text-transparent'}`}
                        >
                          <Check class="w-2.5 h-2.5" />
                        </button>
                        <button
                          type="button"
                          onClick={() => toggleExpand(change.filePath)}
                          class="flex items-center gap-2 flex-1 min-w-0 text-left"
                        >
                          <span class="text-[var(--text-muted)]">
                            {isExpanded() ? (
                              <ChevronDown class="w-3 h-3" />
                            ) : (
                              <ChevronRight class="w-3 h-3" />
                            )}
                          </span>
                          <FileIcon type={change.type} />
                          <span class="text-xs text-[var(--text-primary)] font-medium truncate">
                            {shortName(change.filePath)}
                          </span>
                          <span class="text-[9px] text-[var(--text-muted)] truncate">
                            {displayPath(change.filePath)}
                          </span>
                        </button>
                        <span
                          class={`text-[9px] px-1.5 py-0.5 rounded-full ${change.type === 'create' ? 'bg-[var(--success-subtle)] text-[var(--success)]' : change.type === 'delete' ? 'bg-[var(--error-subtle)] text-[var(--error)]' : 'bg-[var(--accent-subtle)] text-[var(--accent)]'}`}
                        >
                          {change.type}
                        </span>
                      </div>
                      <Show when={isExpanded()}>
                        <div class="mx-4 mb-2 rounded-[var(--radius-md)] overflow-hidden border border-[var(--border-subtle)] bg-[var(--surface-sunken)]">
                          <div class="max-h-64 overflow-y-auto">
                            <pre class="text-[10px] font-mono leading-relaxed">
                              <For each={diff()}>
                                {(line) => (
                                  <div
                                    class={`px-3 py-px ${line.type === 'added' ? 'bg-[rgba(34,197,94,0.1)] text-[var(--success)]' : line.type === 'removed' ? 'bg-[rgba(239,68,68,0.1)] text-[var(--error)]' : 'text-[var(--text-muted)]'}`}
                                  >
                                    <span class="inline-block w-4 text-right mr-2 text-[var(--text-muted)] opacity-50 select-none">
                                      {line.lineNum ?? ' '}
                                    </span>
                                    <span class="select-none mr-1">
                                      {line.type === 'added'
                                        ? '+'
                                        : line.type === 'removed'
                                          ? '-'
                                          : ' '}
                                    </span>
                                    {line.content}
                                  </div>
                                )}
                              </For>
                            </pre>
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
          <div class="flex items-center justify-between px-5 py-3 border-t border-[var(--border-subtle)] bg-[var(--surface-sunken)]">
            <button
              type="button"
              onClick={props.onRejectAll}
              class="px-3 py-1.5 text-xs text-[var(--error)] hover:bg-[var(--error-subtle)] rounded-[var(--radius-md)]"
            >
              Reject All
            </button>
            <div class="flex items-center gap-2">
              <button
                type="button"
                onClick={props.onClose}
                class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)]"
              >
                Close
              </button>
              <button
                type="button"
                onClick={() => void handleApplySelected()}
                disabled={selectedCount() === 0 || applying()}
                class="px-3 py-1.5 text-xs font-medium border border-[var(--accent)] text-[var(--accent)] rounded-[var(--radius-md)] hover:bg-[var(--accent-subtle)] disabled:opacity-50"
              >
                {applying() ? 'Applying...' : `Apply Selected (${selectedCount()})`}
              </button>
              <button
                type="button"
                onClick={() => void handleApplyAll()}
                disabled={props.changes.length === 0 || applying()}
                class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 disabled:opacity-50"
              >
                {applying() ? 'Applying...' : 'Apply All'}
              </button>
            </div>
          </div>
        </div>
      </div>
    </Show>
  )
}
