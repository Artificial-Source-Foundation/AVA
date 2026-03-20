/**
 * Diff Row
 *
 * File diff summary showing add/delete counts with expandable diff view.
 * Auto-expands for small diffs (< SMALL_DIFF_LINE_THRESHOLD changed lines).
 * For write/create tools with empty old content, shows "+N lines" new file badge.
 */

import { ChevronRight, FileDiff, FilePlus } from 'lucide-solid'
import { type Component, createEffect, createMemo, createSignal, Show } from 'solid-js'
import type { ToolCall } from '../../../types'
import { DiffViewer } from '../../ui/DiffViewer'

interface DiffRowProps {
  toolCall: ToolCall
}

interface DiffStats {
  additions: number
  deletions: number
  /** Total unified diff lines (add + remove, not unchanged) */
  changedLines: number
}

/**
 * Threshold for auto-expanding the diff view.
 * Diffs with fewer changed lines expand by default.
 */
const SMALL_DIFF_LINE_THRESHOLD = 30

function computeDiffStats(oldContent: string, newContent: string): DiffStats {
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
    const previous = oldCounts.get(line) ?? 0
    if (count > previous) additions += count - previous
  }
  for (const [line, count] of oldCounts) {
    const next = newCounts.get(line) ?? 0
    if (count > next) deletions += count - next
  }

  return { additions, deletions, changedLines: additions + deletions }
}

function shortFileName(filePath?: string): string {
  if (!filePath) return 'unknown file'
  const parts = filePath.split('/')
  return parts.slice(-2).join('/')
}

export const DiffRow: Component<DiffRowProps> = (props) => {
  const hasDiff = (): boolean =>
    !!(
      props.toolCall.diff?.oldContent !== undefined && props.toolCall.diff?.newContent !== undefined
    )

  /** True when old content is empty — this is a new file write */
  const isNewFile = (): boolean => hasDiff() && props.toolCall.diff!.oldContent === ''

  const stats = createMemo((): DiffStats => {
    if (!hasDiff()) return { additions: 0, deletions: 0, changedLines: 0 }
    return computeDiffStats(props.toolCall.diff!.oldContent, props.toolCall.diff!.newContent)
  })

  /** For new files, count lines in the new content */
  const newFileLineCount = createMemo((): number => {
    if (!isNewFile()) return 0
    const content = props.toolCall.diff!.newContent
    return content ? content.split('\n').length : 0
  })

  const isSmallDiff = createMemo(() =>
    isNewFile()
      ? newFileLineCount() <= SMALL_DIFF_LINE_THRESHOLD
      : stats().changedLines <= SMALL_DIFF_LINE_THRESHOLD
  )

  const [expanded, setExpanded] = createSignal(false)

  // Auto-expand small diffs once stats are computed
  createEffect(() => {
    if (isSmallDiff()) setExpanded(true)
  })

  const fileName = (): string => shortFileName(props.toolCall.filePath)

  return (
    <Show when={hasDiff()}>
      <div class="animate-tool-card-in rounded-[var(--radius-md)] border border-[var(--border-subtle)] overflow-hidden">
        {/* Summary header */}
        {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
        <div
          role="button"
          tabIndex={0}
          aria-expanded={expanded()}
          class="flex items-center gap-2.5 px-3 py-2 text-[13px] cursor-pointer select-none hover:bg-[var(--alpha-white-3)] transition-colors duration-[var(--duration-fast)]"
          onClick={() => setExpanded((v) => !v)}
          onKeyDown={(e) => {
            if (e.key === 'Enter' || e.key === ' ') {
              e.preventDefault()
              setExpanded((v) => !v)
            }
          }}
        >
          {/* Icon: FilePlus for new file, FileDiff for edits */}
          <Show
            when={isNewFile()}
            fallback={<FileDiff class="w-4 h-4 text-[var(--text-muted)] flex-shrink-0" />}
          >
            <FilePlus class="w-4 h-4 text-[var(--success)] flex-shrink-0" />
          </Show>

          <span class="text-[var(--text-secondary)] truncate">{fileName()}</span>

          <span class="flex-1" />

          {/* New file: show line count badge */}
          <Show when={isNewFile()}>
            <span class="text-[11px] text-[var(--success)] tabular-nums font-mono">
              +{newFileLineCount()} lines
            </span>
          </Show>

          {/* Edit: show +/- diff counts */}
          <Show when={!isNewFile()}>
            <Show when={stats().additions > 0}>
              <span class="text-[11px] text-[var(--success)] tabular-nums font-mono">
                +{stats().additions}
              </span>
            </Show>
            <Show when={stats().deletions > 0}>
              <span class="text-[11px] text-[var(--error)] tabular-nums font-mono">
                -{stats().deletions}
              </span>
            </Show>
          </Show>

          <ChevronRight
            class="w-4 h-4 flex-shrink-0 text-[var(--text-muted)] transition-transform duration-[var(--duration-fast)]"
            classList={{ 'rotate-90': expanded() }}
          />
        </div>

        {/* Expanded diff view */}
        <Show when={expanded()}>
          <div class="max-h-[320px] overflow-auto border-t border-[var(--border-subtle)]">
            <DiffViewer
              oldContent={props.toolCall.diff!.oldContent}
              newContent={props.toolCall.diff!.newContent}
              filename={props.toolCall.filePath}
              mode="unified"
              showLineNumbers={false}
              class="border-0 rounded-none"
            />
          </div>
        </Show>
      </div>
    </Show>
  )
}
