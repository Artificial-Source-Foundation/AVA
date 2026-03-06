/**
 * Compare Overlay — branch comparison overlay showing diff stats
 */

import type { Component } from 'solid-js'
import { Show } from 'solid-js'

interface BranchDiff {
  onlyInA: unknown[]
  onlyInB: unknown[]
  shared: unknown[]
}

interface CompareOverlayProps {
  activeBranchName: string | undefined
  targetBranchName: string | undefined
  diff: BranchDiff | null
  onClose: () => void
}

export const CompareOverlay: Component<CompareOverlayProps> = (props) => {
  return (
    <div class="absolute bottom-full left-0 mb-1 z-50 bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-md)] shadow-lg p-3 min-w-[220px] max-w-[300px]">
      <div class="flex items-center justify-between mb-2">
        <span class="text-[11px] font-medium text-[var(--text-primary)]">Branch Comparison</span>
        <button
          type="button"
          onClick={props.onClose}
          class="text-[10px] text-[var(--text-muted)] hover:text-[var(--text-secondary)]"
        >
          Close
        </button>
      </div>
      <Show when={props.diff}>
        {(d) => (
          <div class="space-y-1.5 text-[10px]">
            <div class="flex items-center gap-1 text-[var(--accent)]">
              <span class="font-medium">Active:</span>
              <span>{props.activeBranchName}</span>
              <span class="text-[var(--text-muted)]">({d().onlyInA.length} unique)</span>
            </div>
            <div class="flex items-center gap-1 text-[var(--warning)]">
              <span class="font-medium">Target:</span>
              <span>{props.targetBranchName}</span>
              <span class="text-[var(--text-muted)]">({d().onlyInB.length} unique)</span>
            </div>
            <div class="text-[var(--text-muted)]">{d().shared.length} shared messages</div>
          </div>
        )}
      </Show>
    </div>
  )
}
