/**
 * Plan Branch Selector
 *
 * Compact inline control for managing plan mode branches.
 * Shows a dropdown of available branches, plus New/Compare/Merge actions.
 * Only visible when plan mode is active.
 */

import { GitBranch, Plus } from 'lucide-solid'
import { type Accessor, type Component, createSignal, Show } from 'solid-js'
import { usePlanBranches } from '../../stores/plan-branches'
import type { Message } from '../../types'
import { BranchDropdown } from './plan-branch/BranchDropdown'
import { CompareOverlay } from './plan-branch/CompareOverlay'

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
    const activeId = store.activeBranchId()
    if (activeId) store.updateBranchMessages(activeId, props.messages())
    const messages = store.switchBranch(id)
    if (messages) props.onMessagesChange(messages)
    setShowDropdown(false)
  }

  const handleMerge = (sourceId: string) => {
    const merged = store.mergeBranch(sourceId, props.messages())
    if (merged) props.onMessagesChange(merged)
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

  const closeAllPopups = () => {
    setShowDropdown(false)
    setShowNewDialog(false)
    setCompareMode(false)
    setCompareTarget(null)
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
          <BranchDropdown
            branches={store.branches()}
            activeBranchId={store.activeBranchId()}
            formatTime={formatTime}
            onSwitch={handleSwitchBranch}
            onCompare={handleCompare}
            onMerge={handleMerge}
            onDelete={(id) => store.deleteBranch(id)}
          />
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
              <CompareOverlay
                activeBranchName={store.activeBranch()?.name}
                targetBranchName={targetBranch?.name}
                diff={diff}
                onClose={() => {
                  setCompareMode(false)
                  setCompareTarget(null)
                }}
              />
            )
          }}
        </Show>

        {/* Click-away backdrop for dropdown */}
        <Show when={showDropdown() || showNewDialog() || compareMode()}>
          {/* biome-ignore lint/a11y/noStaticElementInteractions: click-away backdrop */}
          <div role="presentation" class="fixed inset-0 z-40" onClick={closeAllPopups} />
        </Show>
      </div>
    </Show>
  )
}
