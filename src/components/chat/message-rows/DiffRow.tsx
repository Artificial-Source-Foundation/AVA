/**
 * Diff Row
 *
 * Matches Pencil "Tool States" design for edit diffs:
 *
 * Header: 40px, fill #ffffff04, pencil icon (12px #0A84FF),
 *         filename (Geist Mono 11px weight 500 #86868B),
 *         check icon (12px #34C759) + "Applied" (Geist Mono 10px #34C759)
 * Body: side-by-side with Before (#1a0a0a) / After (#0a1a0a) panels
 *
 * Write/create: file-plus icon (14px #34C759), "+N lines" badge
 */

import { Check, ChevronDown, ChevronRight, FilePlus, Pencil } from 'lucide-solid'
import { type Component, createEffect, createMemo, createSignal, Show } from 'solid-js'
import type { ToolCall } from '../../../types'
import { DiffViewer } from '../../ui/DiffViewer'

interface DiffRowProps {
  toolCall: ToolCall
}

interface DiffStats {
  additions: number
  deletions: number
  changedLines: number
}

const SMALL_DIFF_LINE_THRESHOLD = 30
const LARGE_DIFF_LINE_THRESHOLD = 500

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

export const DiffRow: Component<DiffRowProps> = (props) => {
  const hasDiff = (): boolean =>
    !!(
      props.toolCall.diff?.oldContent !== undefined && props.toolCall.diff?.newContent !== undefined
    )

  const isNewFile = (): boolean => hasDiff() && props.toolCall.diff!.oldContent === ''
  const isSuccess = (): boolean => props.toolCall.status === 'success'

  const stats = createMemo((): DiffStats => {
    if (!hasDiff()) return { additions: 0, deletions: 0, changedLines: 0 }
    return computeDiffStats(props.toolCall.diff!.oldContent, props.toolCall.diff!.newContent)
  })

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

  const isLargeDiff = createMemo(() =>
    isNewFile()
      ? newFileLineCount() > LARGE_DIFF_LINE_THRESHOLD
      : stats().changedLines > LARGE_DIFF_LINE_THRESHOLD
  )

  const [expanded, setExpanded] = createSignal(false)

  createEffect(() => {
    if (isSmallDiff() && !isLargeDiff()) setExpanded(true)
  })

  const fileName = (): string => {
    const toolName = props.toolCall.name
    const path = props.toolCall.filePath ?? 'unknown'
    return `${toolName} ${path}`
  }

  return (
    <Show when={hasDiff()}>
      <div
        class="chat-tool-shell animate-tool-card-in rounded-[10px] overflow-hidden"
        style={{
          background: 'var(--tool-card-background)',
          border: '1px solid var(--border-default)',
        }}
      >
        {/* Header -- 40px */}
        {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
        <div
          role="button"
          tabIndex={0}
          aria-expanded={expanded()}
          class="tool-card-header flex cursor-pointer select-none items-center justify-between px-3.5 transition-colors duration-[var(--duration-fast)] hover:bg-[var(--alpha-white-5)] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-inset"
          style={{
            height: '40px',
          }}
          onClick={() => setExpanded((v) => !v)}
          onKeyDown={(e) => {
            if (e.key === 'Enter' || e.key === ' ') {
              e.preventDefault()
              setExpanded((v) => !v)
            }
          }}
        >
          {/* Left: icon + filename */}
          <div class="flex items-center gap-1.5 min-w-0 flex-1">
            <Show
              when={!isNewFile()}
              fallback={
                <FilePlus
                  class="flex-shrink-0"
                  style={{ width: '14px', height: '14px', color: 'var(--success)' }}
                />
              }
            >
              <Pencil
                class="flex-shrink-0"
                style={{ width: '12px', height: '12px', color: 'var(--accent)' }}
              />
            </Show>
            <span
              class="truncate"
              style={{
                'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                'font-size': '11px',
                'font-weight': '500',
                color: 'var(--text-tertiary)',
              }}
            >
              {fileName()}
            </span>
          </div>

          {/* Right: status badge + lines badge + chevron */}
          <div class="flex items-center gap-1.5 flex-shrink-0">
            {/* Applied badge for successful edits */}
            <Show when={isSuccess() && !isNewFile()}>
              <div class="flex items-center gap-1.5">
                <Check
                  class="flex-shrink-0"
                  style={{ width: '12px', height: '12px', color: 'var(--success)' }}
                />
                <span
                  style={{
                    'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                    'font-size': '10px',
                    'font-weight': '500',
                    color: 'var(--success)',
                  }}
                >
                  Applied
                </span>
              </div>
            </Show>

            {/* New file: +N lines badge */}
            <Show when={isNewFile()}>
              <span
                style={{
                  'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                  'font-size': '10px',
                  'font-weight': '500',
                  color: 'var(--success)',
                }}
              >
                +{newFileLineCount()} lines
              </span>
            </Show>

            {/* Large diff hint */}
            <Show when={isLargeDiff() && !expanded()}>
              <span
                class="tabular-nums"
                style={{
                  'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                  'font-size': '11px',
                  color: 'var(--text-muted)',
                }}
              >
                Show large diff ({isNewFile() ? newFileLineCount() : stats().changedLines} lines)
              </span>
            </Show>

            {/* Duration placeholder */}
            <Show when={props.toolCall.completedAt}>
              <span
                class="tabular-nums whitespace-nowrap"
                style={{
                  'font-family': 'var(--font-ui-mono), Geist Mono, monospace',
                  'font-size': '11px',
                  color: 'var(--text-muted)',
                }}
              >
                {(() => {
                  const ms = props.toolCall.completedAt! - props.toolCall.startedAt
                  if (ms < 1000) return `${ms}ms`
                  return `${(ms / 1000).toFixed(1)}s`
                })()}
              </span>
            </Show>

            {/* Chevron */}
            <Show
              when={expanded()}
              fallback={
                <ChevronRight
                  class="flex-shrink-0"
                  style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
                />
              }
            >
              <ChevronDown
                class="flex-shrink-0"
                style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
              />
            </Show>
          </div>
        </div>

        {/* Expanded: side-by-side diff view */}
        <Show when={expanded()}>
          <section
            class="max-h-[400px] overflow-auto focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-inset"
            data-scrollable
            aria-label={`Diff view for ${fileName()}`}
          >
            <DiffViewer
              oldContent={props.toolCall.diff!.oldContent}
              newContent={props.toolCall.diff!.newContent}
              filename={props.toolCall.filePath}
              mode="split"
              showLineNumbers={true}
              class="border-0 rounded-none"
            />
          </section>
        </Show>
      </div>
    </Show>
  )
}
