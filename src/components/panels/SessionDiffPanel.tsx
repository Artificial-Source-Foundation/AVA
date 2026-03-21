/**
 * Session Diff Panel
 *
 * "Changes" tab for the right panel — shows all files modified during the
 * current session as an expandable accordion, sourced from tool calls in
 * messages (edit, write, apply_patch).
 *
 * Features:
 * - Aggregates multiple edits to the same file (latest diff wins)
 * - Shows +N / -N counts per file
 * - Each file row expands to a unified diff via DiffViewer
 * - Large diffs (>500 lines) collapsed by default with an explicit reveal button
 * - Expand all / Collapse all header controls
 */

import {
  ChevronDown,
  ChevronRight,
  ChevronsDownUp,
  ChevronsUpDown,
  FileEdit,
  FilePlus2,
  Minus,
  Plus,
} from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { useSession } from '../../stores/session'
import type { Message, ToolCall } from '../../types'
import { computeDiff, DiffViewer } from '../ui/DiffViewer'

// ============================================================================
// Constants
// ============================================================================

/** Tool names that produce file writes/edits */
const FILE_WRITE_TOOLS = new Set(['write', 'edit', 'apply_patch', 'multiedit'])

/** Diffs longer than this are collapsed by default */
const LARGE_DIFF_LINE_THRESHOLD = 500

// ============================================================================
// Types
// ============================================================================

interface SessionFileDiff {
  /** Absolute or relative file path */
  filePath: string
  /** Pre-change content (may be empty string for new files) */
  oldContent: string
  /** Post-change content */
  newContent: string
  /** Operation type derived from tool name */
  operationType: 'write' | 'edit'
  /** Timestamp of the latest edit (for sort order) */
  timestamp: number
  /** Lines added */
  linesAdded: number
  /** Lines removed */
  linesRemoved: number
}

// ============================================================================
// Helpers
// ============================================================================

function getFileName(path: string): string {
  return path.split('/').pop() || path
}

function getDirectory(path: string): string {
  const parts = path.split('/')
  if (parts.length <= 1) return ''
  parts.pop()
  return parts.slice(-2).join('/')
}

/** Extract file path from a tool call's args if the tool doesn't store filePath directly. */
function resolveFilePath(toolCall: ToolCall): string | null {
  if (toolCall.filePath) return toolCall.filePath
  const args = toolCall.args
  if (typeof args.path === 'string') return args.path
  if (typeof args.file_path === 'string') return args.file_path
  if (typeof args.filename === 'string') return args.filename
  return null
}

/** Derive old/new content from a tool call's diff field or args. */
function resolveContents(toolCall: ToolCall): { oldContent: string; newContent: string } | null {
  if (toolCall.diff) {
    return { oldContent: toolCall.diff.oldContent, newContent: toolCall.diff.newContent }
  }
  // For write tools that don't set diff, use empty old + new from args
  const args = toolCall.args
  if (toolCall.name === 'write' && typeof args.content === 'string') {
    return { oldContent: '', newContent: args.content }
  }
  return null
}

/** Extract all qualifying tool calls from a message list and aggregate per file. */
function extractFileDiffs(messages: Message[]): SessionFileDiff[] {
  const byFile = new Map<string, SessionFileDiff>()

  for (const message of messages) {
    if (!message.toolCalls) continue
    for (const toolCall of message.toolCalls) {
      if (!FILE_WRITE_TOOLS.has(toolCall.name)) continue
      if (toolCall.status !== 'success') continue

      const filePath = resolveFilePath(toolCall)
      if (!filePath) continue

      const contents = resolveContents(toolCall)
      if (!contents) continue

      const diffLines = computeDiff(contents.oldContent, contents.newContent)
      const linesAdded = diffLines.filter((l) => l.type === 'add').length
      const linesRemoved = diffLines.filter((l) => l.type === 'remove').length

      const existing = byFile.get(filePath)
      const timestamp = toolCall.completedAt ?? toolCall.startedAt

      // Keep the latest edit per file (merge: old = original, new = final)
      if (!existing || timestamp > existing.timestamp) {
        byFile.set(filePath, {
          filePath,
          oldContent: existing ? existing.oldContent : contents.oldContent,
          newContent: contents.newContent,
          operationType: toolCall.name === 'write' ? 'write' : 'edit',
          timestamp,
          linesAdded,
          linesRemoved,
        })
      }
    }
  }

  return Array.from(byFile.values()).sort((a, b) => b.timestamp - a.timestamp)
}

// ============================================================================
// Sub-component: File Diff Row
// ============================================================================

interface FileDiffRowProps {
  diff: SessionFileDiff
  isExpanded: boolean
  onToggle: () => void
}

const FileDiffRow: Component<FileDiffRowProps> = (props) => {
  const totalLines = createMemo(() => {
    const lines = computeDiff(props.diff.oldContent, props.diff.newContent)
    return lines.length
  })

  const isLarge = createMemo(() => totalLines() > LARGE_DIFF_LINE_THRESHOLD)
  const [revealLarge, setRevealLarge] = createSignal(false)

  const opIcon = () => (props.diff.operationType === 'write' ? FilePlus2 : FileEdit)
  const opColor = () => (props.diff.operationType === 'write' ? 'var(--success)' : 'var(--warning)')

  return (
    <div class="border-b border-[var(--border-subtle)]">
      {/* File header row */}
      <button
        type="button"
        onClick={props.onToggle}
        class="w-full flex items-center gap-2 px-3 py-2 text-left hover:bg-[var(--surface-raised)] transition-colors"
      >
        <Show
          when={props.isExpanded}
          fallback={<ChevronRight class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />}
        >
          <ChevronDown class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />
        </Show>

        <Dynamic
          component={opIcon()}
          class="w-3.5 h-3.5 flex-shrink-0"
          style={{ color: opColor() }}
        />

        <div class="flex-1 min-w-0 flex items-center gap-1.5">
          <span class="text-xs font-medium text-[var(--text-primary)] truncate">
            {getFileName(props.diff.filePath)}
          </span>
          <span class="text-[10px] text-[var(--text-muted)] truncate">
            {getDirectory(props.diff.filePath)}
          </span>
        </div>

        <div class="flex items-center gap-1.5 text-[10px] flex-shrink-0">
          <Show when={props.diff.operationType === 'write' && !props.diff.oldContent}>
            <span class="px-1.5 py-0.5 bg-[var(--success-subtle)] text-[var(--success)] rounded-full font-medium">
              new
            </span>
          </Show>
          <Show when={props.diff.linesAdded > 0}>
            <span class="text-[var(--success)]">+{props.diff.linesAdded}</span>
          </Show>
          <Show when={props.diff.linesRemoved > 0}>
            <span class="text-[var(--error)]">-{props.diff.linesRemoved}</span>
          </Show>
        </div>
      </button>

      {/* Expanded diff section */}
      <Show when={props.isExpanded}>
        <div class="px-2 pb-2">
          <Show
            when={!isLarge() || revealLarge()}
            fallback={
              <div class="flex flex-col items-center gap-2 py-4 rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-raised)]">
                <p class="text-xs text-[var(--text-muted)]">Large diff — {totalLines()} lines</p>
                <button
                  type="button"
                  onClick={(e) => {
                    e.stopPropagation()
                    setRevealLarge(true)
                  }}
                  class="px-3 py-1 text-xs rounded-[var(--radius-md)] bg-[var(--accent-muted)] text-[var(--accent)] border border-[color-mix(in_srgb,var(--accent)_30%,transparent)] hover:bg-[color-mix(in_srgb,var(--accent)_25%,transparent)] transition-colors"
                >
                  Show large diff
                </button>
              </div>
            }
          >
            <DiffViewer
              oldContent={props.diff.oldContent}
              newContent={props.diff.newContent}
              filename={getFileName(props.diff.filePath)}
              mode="unified"
            />
          </Show>
        </div>
      </Show>
    </div>
  )
}

// ============================================================================
// Main Component
// ============================================================================

export const SessionDiffPanel: Component = () => {
  const { messages } = useSession()
  const [expandedFiles, setExpandedFiles] = createSignal<Set<string>>(new Set())

  const fileDiffs = createMemo(() => extractFileDiffs(messages()))

  const stats = createMemo(() => {
    let added = 0
    let removed = 0
    for (const d of fileDiffs()) {
      added += d.linesAdded
      removed += d.linesRemoved
    }
    return { files: fileDiffs().length, added, removed }
  })

  const toggleFile = (filePath: string): void => {
    const next = new Set<string>(expandedFiles())
    if (next.has(filePath)) next.delete(filePath)
    else next.add(filePath)
    setExpandedFiles(next)
  }

  const expandAll = (): void => {
    setExpandedFiles(new Set<string>(fileDiffs().map((d) => d.filePath)))
  }

  const collapseAll = (): void => {
    setExpandedFiles(new Set<string>())
  }

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center justify-between px-3 py-2 border-b border-[var(--border-subtle)] flex-shrink-0">
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
        <Show when={fileDiffs().length > 0}>
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
          when={fileDiffs().length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full text-center p-6">
              <div class="p-4 bg-[var(--surface-raised)] rounded-full mb-3">
                <FileEdit class="w-7 h-7 text-[var(--text-muted)]" />
              </div>
              <h3 class="text-sm font-medium text-[var(--text-secondary)] mb-1">No changes yet</h3>
              <p class="text-xs text-[var(--text-muted)]">
                File edits and writes during this session will appear here
              </p>
            </div>
          }
        >
          <For each={fileDiffs()}>
            {(diff) => (
              <FileDiffRow
                diff={diff}
                isExpanded={expandedFiles().has(diff.filePath)}
                onToggle={() => toggleFile(diff.filePath)}
              />
            )}
          </For>
        </Show>
      </div>
    </div>
  )
}
