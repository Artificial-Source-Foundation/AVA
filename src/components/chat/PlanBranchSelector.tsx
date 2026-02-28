/**
 * Plan Branch Selector
 *
 * Compact inline control for managing plan mode branches.
 * Shows a dropdown of available branches, plus New/Compare/Merge actions.
 * Only visible when plan mode is active.
 */

import { GitBranch, GitMerge, Plus, Scale, Trash2 } from 'lucide-solid'
import { type Accessor, type Component, createSignal, For, Show } from 'solid-js'
import { usePlanBranches } from '../../stores/plan-branches'
import type { Message } from '../../types'

// ─── Props ──────────────────────────────────────────────────────────────────

export interface PlanBranchSelectorProps {
  isPlanMode: Accessor<boolean>
  messages: Accessor<Message[]>
  onMessagesChange: (messages: Message[]) => void
}

// ─── Component ──────────────────────────────────────────────────────────────

export const PlanBranchSelector: Component<PlanBranchSelectorProps> = (props) => {
  const store = usePlanBranches()
  const [showDropdown, setShowDropdown] = createSignal(false)
  const [showNewDialog, setShowNewDialog] = createSignal(false)
  const [newBranchName, setNewBranchName] = createSignal('')
  const [compareMode, setCompareMode] = createSignal(false)
  const [compareTarget, setCompareTarget] = createSignal<string | null>(null)

  const formatTime = (ts: number) =>
    new Date(ts).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })

  const handleCreateBranch = () => {
    const name = newBranchName().trim()
    if (!name) return
    store.createBranch(name, props.messages())
    setNewBranchName('')
    setShowNewDialog(false)
  }

  const handleSwitchBranch = (id: string) => {
    // Save current branch state before switching
    const activeId = store.activeBranchId()
    if (activeId) {
      store.updateBranchMessages(activeId, props.messages())
    }
    const messages = store.switchBranch(id)
    if (messages) {
      props.onMessagesChange(messages)
    }
    setShowDropdown(false)
  }

  const handleDeleteBranch = (id: string) => {
    store.deleteBranch(id)
  }

  const handleMerge = (sourceId: string) => {
    const merged = store.mergeBranch(sourceId, props.messages())
    if (merged) {
      props.onMessagesChange(merged)
    }
    setShowDropdown(false)
  }

  const handleCompare = (targetId: string) => {
    const activeId = store.activeBranchId()
    if (!activeId) return
    const diff = store.compareBranches(activeId, targetId)
    if (diff) {
      setCompareTarget(targetId)
      setCompareMode(true)
      setShowDropdown(false)
    }
  }

  return (
    <Show when={props.isPlanMode()}>
      <div class="relative inline-flex items-center gap-1">
        {/* Branch indicator / dropdown trigger */}
        <button
          type="button"
          onClick={() => setShowDropdown(!showDropdown())}
          class="inline-flex items-center gap-1 px-1.5 py-1 text-[10px] text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--surface-raised)] rounded-[var(--radius-md)] transition-colors"
          title="Plan branches"
        >
          <GitBranch class="w-3 h-3" />
          <Show
            when={store.activeBranch()}
            fallback={
              <span>
                {store.branches().length > 0 ? `${store.branches().length} branches` : 'Branches'}
              </span>
            }
          >
            {(branch) => <span class="max-w-[80px] truncate">{branch().name}</span>}
          </Show>
        </button>

        {/* New branch button */}
        <button
          type="button"
          onClick={() => setShowNewDialog(true)}
          class="p-1 text-[var(--text-muted)] hover:text-[var(--accent)] hover:bg-[var(--surface-raised)] rounded-[var(--radius-md)] transition-colors"
          title="Create new branch"
        >
          <Plus class="w-3 h-3" />
        </button>

        {/* Branch dropdown */}
        <Show when={showDropdown()}>
          <div class="absolute bottom-full left-0 mb-1 z-50 min-w-[200px] max-h-[240px] overflow-y-auto bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-md)] shadow-lg">
            <Show
              when={store.branches().length > 0}
              fallback={
                <p class="px-3 py-2 text-[10px] text-[var(--text-muted)]">
                  No branches yet. Create one to snapshot your plan.
                </p>
              }
            >
              <For each={store.branches()}>
                {(branch) => {
                  const isActive = () => store.activeBranchId() === branch.id
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
                        onClick={() => handleSwitchBranch(branch.id)}
                        class="flex-1 min-w-0 text-left"
                      >
                        <div class="flex items-center gap-1">
                          <GitBranch class="w-2.5 h-2.5 flex-shrink-0" />
                          <span class="truncate font-medium">{branch.name}</span>
                        </div>
                        <div class="text-[9px] text-[var(--text-muted)]">
                          {branch.messages.length} msgs &middot; {formatTime(branch.createdAt)}
                        </div>
                      </button>

                      <div class="flex items-center gap-0.5 flex-shrink-0">
                        {/* Compare button */}
                        <Show when={store.activeBranchId() && !isActive()}>
                          <button
                            type="button"
                            onClick={(e) => {
                              e.stopPropagation()
                              handleCompare(branch.id)
                            }}
                            class="p-0.5 text-[var(--text-muted)] hover:text-[var(--accent)] rounded transition-colors"
                            title="Compare with active branch"
                          >
                            <Scale class="w-2.5 h-2.5" />
                          </button>
                        </Show>

                        {/* Merge button */}
                        <Show when={!isActive()}>
                          <button
                            type="button"
                            onClick={(e) => {
                              e.stopPropagation()
                              handleMerge(branch.id)
                            }}
                            class="p-0.5 text-[var(--text-muted)] hover:text-[var(--success)] rounded transition-colors"
                            title="Merge into current"
                          >
                            <GitMerge class="w-2.5 h-2.5" />
                          </button>
                        </Show>

                        {/* Delete button */}
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation()
                            handleDeleteBranch(branch.id)
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
        </Show>

        {/* New branch mini-dialog */}
        <Show when={showNewDialog()}>
          <div class="absolute bottom-full left-0 mb-1 z-50 bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-md)] shadow-lg p-2 min-w-[200px]">
            <input
              type="text"
              placeholder="Branch name..."
              value={newBranchName()}
              onInput={(e) => setNewBranchName(e.currentTarget.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') handleCreateBranch()
                if (e.key === 'Escape') setShowNewDialog(false)
              }}
              class="w-full px-2 py-1 text-[11px] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] rounded-[var(--radius-sm)] focus:border-[var(--accent)] outline-none"
              autofocus
            />
            <div class="flex gap-1 mt-1.5 justify-end">
              <button
                type="button"
                onClick={() => setShowNewDialog(false)}
                class="px-2 py-0.5 text-[10px] text-[var(--text-muted)] hover:text-[var(--text-secondary)]"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={handleCreateBranch}
                disabled={!newBranchName().trim()}
                class="px-2 py-0.5 text-[10px] font-medium bg-[var(--accent)] text-white rounded-[var(--radius-sm)] disabled:opacity-50"
              >
                Create
              </button>
            </div>
          </div>
        </Show>

        {/* Compare mode overlay */}
        <Show when={compareMode() && compareTarget()}>
          {(_) => {
            const activeId = store.activeBranchId()
            const targetId = compareTarget()
            const diff = activeId && targetId ? store.compareBranches(activeId, targetId) : null
            const targetBranch = store.branches().find((b) => b.id === targetId)

            return (
              <div class="absolute bottom-full left-0 mb-1 z-50 bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-md)] shadow-lg p-3 min-w-[220px] max-w-[300px]">
                <div class="flex items-center justify-between mb-2">
                  <span class="text-[11px] font-medium text-[var(--text-primary)]">
                    Branch Comparison
                  </span>
                  <button
                    type="button"
                    onClick={() => {
                      setCompareMode(false)
                      setCompareTarget(null)
                    }}
                    class="text-[10px] text-[var(--text-muted)] hover:text-[var(--text-secondary)]"
                  >
                    Close
                  </button>
                </div>
                <Show when={diff}>
                  {(d) => (
                    <div class="space-y-1.5 text-[10px]">
                      <div class="flex items-center gap-1 text-[var(--accent)]">
                        <span class="font-medium">Active:</span>
                        <span>{store.activeBranch()?.name}</span>
                        <span class="text-[var(--text-muted)]">({d().onlyInA.length} unique)</span>
                      </div>
                      <div class="flex items-center gap-1 text-[var(--warning)]">
                        <span class="font-medium">Target:</span>
                        <span>{targetBranch?.name}</span>
                        <span class="text-[var(--text-muted)]">({d().onlyInB.length} unique)</span>
                      </div>
                      <div class="text-[var(--text-muted)]">
                        {d().shared.length} shared messages
                      </div>
                    </div>
                  )}
                </Show>
              </div>
            )
          }}
        </Show>

        {/* Click-away backdrop for dropdown */}
        <Show when={showDropdown() || showNewDialog() || compareMode()}>
          {/* biome-ignore lint/a11y/noStaticElementInteractions: click-away backdrop */}
          <div
            role="presentation"
            class="fixed inset-0 z-40"
            onClick={() => {
              setShowDropdown(false)
              setShowNewDialog(false)
              setCompareMode(false)
              setCompareTarget(null)
            }}
          />
        </Show>
      </div>
    </Show>
  )
}
