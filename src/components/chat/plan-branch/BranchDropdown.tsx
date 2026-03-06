/**
 * Branch Dropdown — list of branches with switch, compare, merge, and delete actions
 */

import { GitBranch, GitMerge, Scale, Trash2 } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { PlanBranch } from '../../../stores/plan-branches'

interface BranchDropdownProps {
  branches: PlanBranch[]
  activeBranchId: string | null
  formatTime: (ts: number) => string
  onSwitch: (id: string) => void
  onCompare: (id: string) => void
  onMerge: (id: string) => void
  onDelete: (id: string) => void
}

export const BranchDropdown: Component<BranchDropdownProps> = (props) => {
  return (
    <div class="absolute bottom-full left-0 mb-1 z-50 min-w-[200px] max-h-[240px] overflow-y-auto bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-md)] shadow-lg">
      <Show
        when={props.branches.length > 0}
        fallback={
          <p class="px-3 py-2 text-[10px] text-[var(--text-muted)]">
            No branches yet. Create one to snapshot your plan.
          </p>
        }
      >
        <For each={props.branches}>
          {(branch) => {
            const isActive = () => props.activeBranchId === branch.id
            return (
              <div
                class={`flex items-center gap-2 px-2 py-1.5 text-[10px] border-b border-[var(--border-subtle)] last:border-b-0 ${
                  isActive()
                    ? 'bg-[var(--accent-subtle)] text-[var(--accent)]'
                    : 'text-[var(--text-secondary)] hover:bg-[var(--surface-raised)]'
                }`}
              >
                <button
                  type="button"
                  onClick={() => props.onSwitch(branch.id)}
                  class="flex-1 min-w-0 text-left"
                >
                  <div class="flex items-center gap-1">
                    <GitBranch class="w-2.5 h-2.5 flex-shrink-0" />
                    <span class="truncate font-medium">{branch.name}</span>
                  </div>
                  <div class="text-[9px] text-[var(--text-muted)]">
                    {branch.messages.length} msgs &middot; {props.formatTime(branch.createdAt)}
                  </div>
                </button>

                <div class="flex items-center gap-0.5 flex-shrink-0">
                  <Show when={props.activeBranchId && !isActive()}>
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation()
                        props.onCompare(branch.id)
                      }}
                      class="p-0.5 text-[var(--text-muted)] hover:text-[var(--accent)] rounded transition-colors"
                      title="Compare with active branch"
                    >
                      <Scale class="w-2.5 h-2.5" />
                    </button>
                  </Show>
                  <Show when={!isActive()}>
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation()
                        props.onMerge(branch.id)
                      }}
                      class="p-0.5 text-[var(--text-muted)] hover:text-[var(--success)] rounded transition-colors"
                      title="Merge into current"
                    >
                      <GitMerge class="w-2.5 h-2.5" />
                    </button>
                  </Show>
                  <button
                    type="button"
                    onClick={(e) => {
                      e.stopPropagation()
                      props.onDelete(branch.id)
                    }}
                    class="p-0.5 text-[var(--text-muted)] hover:text-[var(--error)] rounded transition-colors"
                    title="Delete branch"
                  >
                    <Trash2 class="w-2.5 h-2.5" />
                  </button>
                </div>
              </div>
            )
          }}
        </For>
      </Show>
    </div>
  )
}
