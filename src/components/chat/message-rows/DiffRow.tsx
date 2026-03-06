/**
 * Diff Row
 *
 * File diff summary showing add/delete counts with expandable diff view.
 * Collapsed by default; shows file name and +/- line counts.
 */

import { ChevronRight, FileDiff } from 'lucide-solid'
import { type Component, createMemo, createSignal, Show } from 'solid-js'
import type { ToolCall } from '../../../types'
import { DiffViewer } from '../../ui/DiffViewer'

interface DiffRowProps {
  toolCall: ToolCall
}

interface DiffStats {
  additions: number
  deletions: number
}

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

  return { additions, deletions }
}

function shortFileName(filePath?: string): string {
  if (!filePath) return 'unknown file'
  const parts = filePath.split('/')
  return parts.slice(-2).join('/')
}

export const DiffRow: Component<DiffRowProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)

  const hasDiff = (): boolean =>
    !!(
      props.toolCall.diff?.oldContent !== undefined && props.toolCall.diff?.newContent !== undefined
    )

  const stats = createMemo((): DiffStats => {
    if (!hasDiff()) return { additions: 0, deletions: 0 }
    return computeDiffStats(props.toolCall.diff!.oldContent, props.toolCall.diff!.newContent)
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
          <FileDiff class="w-4 h-4 text-[var(--text-muted)] flex-shrink-0" />
          <span class="text-[var(--text-secondary)] truncate">{fileName()}</span>

          <span class="flex-1" />

          {/* +/- counts */}
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
