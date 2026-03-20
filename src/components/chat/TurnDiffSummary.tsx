/**
 * TurnDiffSummary
 *
 * Per-turn file modification summary shown after each assistant message that
 * modified files. Displays a "Modified N files" header with a 5-block +/- bar
 * chart and an expandable per-file list with +N/-N counts.
 *
 * Inspired by OpenCode's session-turn diffs pattern (packages/ui/src/components/session-turn.tsx).
 *
 * Usage: place after assistant message content, before the next user message.
 * Only renders when there are completed file-modifying tool calls.
 */

import { ChevronRight, FileEdit } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import type { ToolCall } from '../../types'

// ============================================================================
// Constants
// ============================================================================

/** Tool names that modify files and should be counted in the diff summary */
const FILE_MODIFY_TOOLS = new Set([
  'write',
  'write_file',
  'create',
  'create_file',
  'edit',
  'apply_patch',
  'multiedit',
  'delete',
  'delete_file',
])

const TOTAL_BLOCKS = 5

// ============================================================================
// Types
// ============================================================================

interface FileDiffStats {
  filePath: string
  additions: number
  deletions: number
  isNewFile: boolean
}

// ============================================================================
// Diff computation
// ============================================================================

function computeLineDiff(
  oldContent: string,
  newContent: string
): { additions: number; deletions: number } {
  const oldLines = oldContent.split('\n')
  const newLines = newContent.split('\n')

  const oldCounts = new Map<string, number>()
  const newCounts = new Map<string, number>()

  for (const line of oldLines) {
    oldCounts.set(line, (oldCounts.get(line) ?? 0) + 1)
  }
  for (const line of newLines) {
    newCounts.set(line, (newCounts.get(line) ?? 0) + 1)
  }

  let additions = 0
  let deletions = 0

  for (const [line, count] of newCounts) {
    const prev = oldCounts.get(line) ?? 0
    if (count > prev) additions += count - prev
  }
  for (const [line, count] of oldCounts) {
    const next = newCounts.get(line) ?? 0
    if (count > next) deletions += count - next
  }

  return { additions, deletions }
}

/**
 * Extract per-file diff stats from a list of tool calls.
 * Deduplicates by file path, keeping the last modification per file.
 */
function extractFileDiffs(toolCalls: ToolCall[]): FileDiffStats[] {
  const fileMap = new Map<string, FileDiffStats>()

  for (const tc of toolCalls) {
    if (!FILE_MODIFY_TOOLS.has(tc.name)) continue
    if (tc.status !== 'success') continue

    const filePath =
      tc.filePath ??
      (tc.args?.path as string | undefined) ??
      (tc.args?.filePath as string | undefined) ??
      (tc.args?.file_path as string | undefined)

    if (!filePath) continue

    if (tc.diff) {
      const isNewFile = tc.diff.oldContent === ''
      const stats = isNewFile
        ? {
            additions: tc.diff.newContent.split('\n').length,
            deletions: 0,
          }
        : computeLineDiff(tc.diff.oldContent, tc.diff.newContent)

      fileMap.set(filePath, {
        filePath,
        additions: stats.additions,
        deletions: stats.deletions,
        isNewFile,
      })
    } else {
      // No diff data — record the file with zero counts so it still appears
      if (!fileMap.has(filePath)) {
        fileMap.set(filePath, {
          filePath,
          additions: 0,
          deletions: 0,
          isNewFile:
            tc.name === 'write' ||
            tc.name === 'write_file' ||
            tc.name === 'create' ||
            tc.name === 'create_file',
        })
      }
    }
  }

  return Array.from(fileMap.values())
}

// ============================================================================
// 5-block proportional bar
// ============================================================================

interface BlockCounts {
  added: number
  deleted: number
  neutral: number
}

function computeBlockCounts(totalAdditions: number, totalDeletions: number): BlockCounts {
  const adds = totalAdditions
  const dels = totalDeletions

  if (adds === 0 && dels === 0) {
    return { added: 0, deleted: 0, neutral: TOTAL_BLOCKS }
  }

  const total = adds + dels

  if (total < 5) {
    const added = adds > 0 ? 1 : 0
    const deleted = dels > 0 ? 1 : 0
    const neutral = TOTAL_BLOCKS - added - deleted
    return { added, deleted, neutral }
  }

  const ratio = adds > dels ? adds / dels : dels / adds
  let blocksForColors = TOTAL_BLOCKS

  if (total < 20) {
    blocksForColors = TOTAL_BLOCKS - 1
  } else if (ratio < 4) {
    blocksForColors = TOTAL_BLOCKS - 1
  }

  const percentAdded = adds / total
  const percentDeleted = dels / total

  const addedRaw = percentAdded * blocksForColors
  const deletedRaw = percentDeleted * blocksForColors

  let added = adds > 0 ? Math.max(1, Math.round(addedRaw)) : 0
  let deleted = dels > 0 ? Math.max(1, Math.round(deletedRaw)) : 0

  // Cap bars based on actual change magnitude
  if (adds > 0 && adds <= 5) added = Math.min(added, 1)
  if (adds > 5 && adds <= 10) added = Math.min(added, 2)
  if (dels > 0 && dels <= 5) deleted = Math.min(deleted, 1)
  if (dels > 5 && dels <= 10) deleted = Math.min(deleted, 2)

  let totalAllocated = added + deleted
  if (totalAllocated > blocksForColors) {
    if (addedRaw > deletedRaw) {
      added = blocksForColors - deleted
    } else {
      deleted = blocksForColors - added
    }
    totalAllocated = added + deleted
  }

  const neutral = Math.max(0, TOTAL_BLOCKS - totalAllocated)

  return { added, deleted, neutral }
}

// ============================================================================
// DiffBar — 5 colored blocks
// ============================================================================

const DiffBar: Component<{ additions: number; deletions: number }> = (props) => {
  const counts = createMemo(() => computeBlockCounts(props.additions, props.deletions))

  const blocks = createMemo(() => {
    const { added, deleted, neutral } = counts()
    return [
      ...Array<'add'>(added).fill('add'),
      ...Array<'del'>(deleted).fill('del'),
      ...Array<'neutral'>(neutral).fill('neutral'),
    ].slice(0, TOTAL_BLOCKS) as Array<'add' | 'del' | 'neutral'>
  })

  return (
    <div class="flex items-center gap-[2px]">
      <For each={blocks()}>
        {(kind) => (
          <span
            class="inline-block w-[8px] h-[12px] rounded-[2px] flex-shrink-0"
            style={{
              background:
                kind === 'add'
                  ? 'var(--success)'
                  : kind === 'del'
                    ? 'var(--error)'
                    : 'var(--gray-6)',
            }}
          />
        )}
      </For>
    </div>
  )
}

// ============================================================================
// Short file name helper
// ============================================================================

function shortFileName(filePath: string): { dir: string; name: string } {
  const parts = filePath.split('/')
  const name = parts[parts.length - 1] ?? filePath
  const dir = parts.length > 1 ? `${parts.slice(0, -1).join('/')}/` : ''
  return { dir, name }
}

// ============================================================================
// TurnDiffSummary
// ============================================================================

export interface TurnDiffSummaryProps {
  /** All tool calls from the assistant turn */
  toolCalls: ToolCall[]
  /** Whether the turn is still streaming (hide summary while running) */
  isStreaming?: boolean
}

export const TurnDiffSummary: Component<TurnDiffSummaryProps> = (props) => {
  const [open, setOpen] = createSignal(false)

  const fileDiffs = createMemo(() => extractFileDiffs(props.toolCalls ?? []))

  const totalAdditions = createMemo(() => fileDiffs().reduce((sum, f) => sum + f.additions, 0))
  const totalDeletions = createMemo(() => fileDiffs().reduce((sum, f) => sum + f.deletions, 0))

  const fileCount = createMemo(() => fileDiffs().length)
  const shouldShow = createMemo(() => !props.isStreaming && fileCount() > 0)

  return (
    <Show when={shouldShow()}>
      <div class="mt-2 rounded-[var(--radius-md)] border border-[var(--border-subtle)] overflow-hidden">
        {/* Collapsed header / trigger */}
        {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
        <div
          role="button"
          tabIndex={0}
          aria-expanded={open()}
          class="flex items-center gap-2 px-3 py-2 text-[12px] cursor-pointer select-none hover:bg-[var(--alpha-white-3)] transition-colors duration-[var(--duration-fast)]"
          onClick={() => setOpen((v) => !v)}
          onKeyDown={(e) => {
            if (e.key === 'Enter' || e.key === ' ') {
              e.preventDefault()
              setOpen((v) => !v)
            }
          }}
        >
          <FileEdit class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0" />

          <span class="text-[var(--text-muted)]">Modified</span>
          <span class="text-[var(--text-secondary)] font-medium tabular-nums">{fileCount()}</span>
          <span class="text-[var(--text-muted)]">{fileCount() === 1 ? 'file' : 'files'}</span>

          <span class="flex-1" />

          {/* 5-block proportional bar */}
          <DiffBar additions={totalAdditions()} deletions={totalDeletions()} />

          <ChevronRight
            class="w-3.5 h-3.5 flex-shrink-0 text-[var(--text-muted)] transition-transform duration-[var(--duration-fast)]"
            classList={{ 'rotate-90': open() }}
          />
        </div>

        {/* Expanded file list */}
        <Show when={open()}>
          <div class="border-t border-[var(--border-subtle)]">
            <For each={fileDiffs()}>
              {(file) => {
                const { dir, name } = shortFileName(file.filePath)
                return (
                  <div class="flex items-center gap-2 px-3 py-1.5 text-[11px] hover:bg-[var(--alpha-white-3)] transition-colors duration-[var(--duration-fast)]">
                    {/* File path: dim directory, bold filename */}
                    <span class="flex-1 truncate min-w-0">
                      <Show when={dir}>
                        <span class="text-[var(--text-muted)]">{dir}</span>
                      </Show>
                      <span class="text-[var(--text-secondary)]">{name}</span>
                    </span>

                    {/* +/- counts */}
                    <div class="flex items-center gap-1.5 flex-shrink-0 font-mono tabular-nums">
                      <Show when={file.additions > 0}>
                        <span class="text-[var(--success)]">+{file.additions}</span>
                      </Show>
                      <Show when={file.deletions > 0}>
                        <span class="text-[var(--error)]">-{file.deletions}</span>
                      </Show>
                      <Show when={file.additions === 0 && file.deletions === 0}>
                        <span class="text-[var(--text-muted)]">modified</span>
                      </Show>
                    </div>
                  </div>
                )
              }}
            </For>
          </div>
        </Show>
      </div>
    </Show>
  )
}
