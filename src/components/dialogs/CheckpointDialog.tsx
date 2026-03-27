/**
 * Checkpoint Dialog
 *
 * Two-tab dialog: Save a named checkpoint, or restore from a previous one.
 * Restore options: conversation only, files only, or both.
 */

import { Clock, File, Flag, History, MessageSquare, RotateCcw } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useSession } from '../../stores/session'

type RestoreScope = 'conversation' | 'files' | 'both'
type Tab = 'save' | 'restore'

interface CheckpointDialogProps {
  open: boolean
  onClose: () => void
  onSave: (description: string) => void
  onRestore?: (checkpointId: string, scope: RestoreScope) => void
}

export const CheckpointDialog: Component<CheckpointDialogProps> = (props) => {
  const [tab, setTab] = createSignal<Tab>('save')
  const [name, setName] = createSignal('')
  const [selectedId, setSelectedId] = createSignal<string | null>(null)
  const [restoreScope, setRestoreScope] = createSignal<RestoreScope>('conversation')
  const [restoring, setRestoring] = createSignal(false)

  const { checkpoints, rollbackToCheckpoint } = useSession()

  const handleSave = (): void => {
    const desc = name().trim()
    if (!desc) return
    props.onSave(desc)
    setName('')
    props.onClose()
  }

  const handleRestore = async (): Promise<void> => {
    const id = selectedId()
    if (!id) return

    setRestoring(true)
    try {
      if (props.onRestore) {
        props.onRestore(id, restoreScope())
      } else {
        // Default: rollback conversation via session store
        await rollbackToCheckpoint(id)
      }
      setSelectedId(null)
      props.onClose()
    } finally {
      setRestoring(false)
    }
  }

  const formatTime = (ts: number): string => {
    const d = new Date(ts)
    const now = new Date()
    const diffMs = now.getTime() - d.getTime()
    const diffMin = Math.floor(diffMs / 60_000)
    if (diffMin < 1) return 'Just now'
    if (diffMin < 60) return `${diffMin}m ago`
    const diffHr = Math.floor(diffMin / 60)
    if (diffHr < 24) return `${diffHr}h ago`
    return d.toLocaleDateString(undefined, {
      month: 'short',
      day: 'numeric',
      hour: '2-digit',
      minute: '2-digit',
    })
  }

  const scopeOptions: Array<{
    id: RestoreScope
    label: string
    desc: string
    icon: typeof MessageSquare
  }> = [
    {
      id: 'conversation',
      label: 'Conversation only',
      desc: 'Restore messages to this checkpoint',
      icon: MessageSquare,
    },
    {
      id: 'files',
      label: 'Files only',
      desc: 'Revert file changes made after this checkpoint',
      icon: File,
    },
    { id: 'both', label: 'Both', desc: 'Restore conversation and revert files', icon: RotateCcw },
  ]

  return (
    <Show when={props.open}>
      <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
        <div class="bg-[var(--surface-raised)] border border-[var(--border-default)] rounded-[var(--radius-xl)] max-w-md w-full shadow-2xl overflow-hidden">
          {/* Tab bar */}
          <div class="flex border-b border-[var(--border-subtle)]">
            <button
              type="button"
              onClick={() => setTab('save')}
              class={`flex-1 flex items-center justify-center gap-2 px-4 py-3 text-sm font-medium transition-colors ${
                tab() === 'save'
                  ? 'text-[var(--accent)] border-b-2 border-[var(--accent)] bg-[var(--accent-subtle)]'
                  : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)]'
              }`}
            >
              <Flag class="w-4 h-4" />
              Save Checkpoint
            </button>
            <button
              type="button"
              onClick={() => setTab('restore')}
              class={`flex-1 flex items-center justify-center gap-2 px-4 py-3 text-sm font-medium transition-colors ${
                tab() === 'restore'
                  ? 'text-[var(--accent)] border-b-2 border-[var(--accent)] bg-[var(--accent-subtle)]'
                  : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)]'
              }`}
            >
              <History class="w-4 h-4" />
              Restore
            </button>
          </div>

          <div class="p-5">
            {/* Save tab */}
            <Show when={tab() === 'save'}>
              <div class="space-y-4">
                <p class="text-xs text-[var(--text-secondary)]">
                  Create a snapshot of the current conversation that you can restore later.
                </p>
                <input
                  type="text"
                  placeholder="Checkpoint name..."
                  value={name()}
                  onInput={(e) => setName(e.currentTarget.value)}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter') handleSave()
                    if (e.key === 'Escape') props.onClose()
                  }}
                  class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                  autofocus
                />
                <div class="flex gap-2 justify-end">
                  <button
                    type="button"
                    onClick={() => props.onClose()}
                    class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
                  >
                    Cancel
                  </button>
                  <button
                    type="button"
                    onClick={handleSave}
                    disabled={!name().trim()}
                    class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors disabled:opacity-50"
                  >
                    Save
                  </button>
                </div>
              </div>
            </Show>

            {/* Restore tab */}
            <Show when={tab() === 'restore'}>
              <div class="space-y-4">
                <Show
                  when={checkpoints().length > 0}
                  fallback={
                    <div class="text-center py-8">
                      <History class="w-8 h-8 text-[var(--text-muted)] mx-auto mb-2 opacity-40" />
                      <p class="text-sm text-[var(--text-muted)]">No checkpoints saved yet</p>
                      <p class="text-xs text-[var(--text-muted)] mt-1 opacity-60">
                        Save a checkpoint first to enable restore
                      </p>
                    </div>
                  }
                >
                  {/* Checkpoint list */}
                  <div class="max-h-48 overflow-y-auto space-y-1.5 -mx-1 px-1">
                    <For each={[...checkpoints()].reverse()}>
                      {(cp) => (
                        <button
                          type="button"
                          onClick={() => setSelectedId(cp.id)}
                          class={`w-full text-left p-3 rounded-[var(--radius-lg)] border transition-colors ${
                            selectedId() === cp.id
                              ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                              : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                          }`}
                        >
                          <div class="flex items-center justify-between">
                            <span
                              class={`text-sm font-medium ${
                                selectedId() === cp.id
                                  ? 'text-[var(--accent)]'
                                  : 'text-[var(--text-primary)]'
                              }`}
                            >
                              {cp.description}
                            </span>
                            <span class="text-xs text-[var(--text-muted)] flex items-center gap-1">
                              <Clock class="w-3 h-3" />
                              {formatTime(cp.timestamp)}
                            </span>
                          </div>
                          <div class="text-xs text-[var(--text-muted)] mt-1">
                            {cp.messageCount} message{cp.messageCount !== 1 ? 's' : ''}
                          </div>
                        </button>
                      )}
                    </For>
                  </div>

                  {/* Restore scope */}
                  <Show when={selectedId()}>
                    <div class="pt-2 border-t border-[var(--border-subtle)]">
                      <p class="text-xs font-medium text-[var(--text-secondary)] mb-2">
                        Restore scope
                      </p>
                      <div class="space-y-1.5">
                        <For each={scopeOptions}>
                          {(opt) => (
                            <button
                              type="button"
                              onClick={() => setRestoreScope(opt.id)}
                              class={`w-full flex items-center gap-3 p-2.5 rounded-[var(--radius-md)] border text-left transition-colors ${
                                restoreScope() === opt.id
                                  ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                                  : 'border-[var(--border-subtle)] hover:border-[var(--border-default)]'
                              }`}
                            >
                              <opt.icon
                                class={`w-4 h-4 flex-shrink-0 ${
                                  restoreScope() === opt.id
                                    ? 'text-[var(--accent)]'
                                    : 'text-[var(--text-muted)]'
                                }`}
                              />
                              <div class="flex-1 min-w-0">
                                <div
                                  class={`text-xs font-medium ${
                                    restoreScope() === opt.id
                                      ? 'text-[var(--accent)]'
                                      : 'text-[var(--text-primary)]'
                                  }`}
                                >
                                  {opt.label}
                                </div>
                                <div class="text-[11px] text-[var(--text-muted)]">{opt.desc}</div>
                              </div>
                            </button>
                          )}
                        </For>
                      </div>
                    </div>
                  </Show>

                  {/* Actions */}
                  <div class="flex gap-2 justify-end pt-1">
                    <button
                      type="button"
                      onClick={() => props.onClose()}
                      class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
                    >
                      Cancel
                    </button>
                    <button
                      type="button"
                      onClick={() => void handleRestore()}
                      disabled={!selectedId() || restoring()}
                      class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors disabled:opacity-50 flex items-center gap-1.5"
                    >
                      <RotateCcw class="w-3 h-3" />
                      {restoring() ? 'Restoring...' : 'Restore'}
                    </button>
                  </div>
                </Show>
              </div>
            </Show>
          </div>
        </div>
      </div>
    </Show>
  )
}
