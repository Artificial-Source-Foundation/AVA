/**
 * Checkpoint Dialog
 * Simple dialog to save a named checkpoint of the current conversation.
 */

import { Flag } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'

interface CheckpointDialogProps {
  open: boolean
  onClose: () => void
  onSave: (description: string) => void
}

export const CheckpointDialog: Component<CheckpointDialogProps> = (props) => {
  const [name, setName] = createSignal('')

  const handleSave = () => {
    const desc = name().trim()
    if (!desc) return
    props.onSave(desc)
    setName('')
    props.onClose()
  }

  return (
    <Show when={props.open}>
      <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
        <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-sm w-full shadow-2xl space-y-4">
          <div class="flex items-center gap-2">
            <Flag class="w-4 h-4 text-[var(--accent)]" />
            <h3 class="text-sm font-semibold text-[var(--text-primary)]">Save Checkpoint</h3>
          </div>
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
              onClick={props.onClose}
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
      </div>
    </Show>
  )
}
