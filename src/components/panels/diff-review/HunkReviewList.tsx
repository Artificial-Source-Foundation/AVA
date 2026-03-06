/**
 * Hunk Review List — renders hunk-level review items with accept/reject
 */

import { type Component, For } from 'solid-js'
import { getFileName, type HunkReviewItem, type HunkReviewStatus } from './diff-review-helpers'

interface HunkReviewListProps {
  items: HunkReviewItem[]
  onSetStatus: (hunkId: string, status: HunkReviewStatus) => void
}

export const HunkReviewList: Component<HunkReviewListProps> = (props) => {
  return (
    <div class="p-2 space-y-2 border-b border-[var(--border-subtle)]">
      <For each={props.items}>
        {(hunk, index) => (
          <div class="rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-base)] overflow-hidden">
            <div class="flex items-center justify-between px-2 py-1.5 border-b border-[var(--border-subtle)]">
              <div class="text-[11px] text-[var(--text-secondary)]">
                Hunk {index() + 1} - {getFileName(hunk.path)}
              </div>
              <div class="flex items-center gap-1.5">
                <span class="text-[10px] uppercase tracking-wide text-[var(--text-muted)]">
                  {hunk.status}
                </span>
                <button
                  type="button"
                  onClick={() => props.onSetStatus(hunk.id, 'accepted')}
                  class="px-2 py-1 text-[10px] rounded-[var(--radius-md)] bg-[color-mix(in_srgb,var(--success)_18%,transparent)] text-[var(--success)] border border-[color-mix(in_srgb,var(--success)_35%,transparent)]"
                >
                  Accept
                </button>
                <button
                  type="button"
                  onClick={() => props.onSetStatus(hunk.id, 'rejected')}
                  class="px-2 py-1 text-[10px] rounded-[var(--radius-md)] bg-[color-mix(in_srgb,var(--error)_18%,transparent)] text-[var(--error)] border border-[color-mix(in_srgb,var(--error)_35%,transparent)]"
                >
                  Reject
                </button>
              </div>
            </div>
            <pre class="text-[11px] leading-relaxed text-[var(--text-secondary)] p-2 overflow-x-auto bg-[var(--surface-raised)]">
              {hunk.content}
            </pre>
          </div>
        )}
      </For>
    </div>
  )
}
