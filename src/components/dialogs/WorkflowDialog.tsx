/**
 * Workflow Dialog
 *
 * Modal dialog for saving a session as a reusable workflow/recipe.
 */

import { Dialog } from '@kobalte/core/dialog'
import { BookmarkPlus, X } from 'lucide-solid'
import { type Component, createEffect, createSignal, Show } from 'solid-js'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import { useWorkflows } from '../../stores/workflows'

export interface WorkflowDialogProps {
  open: boolean
  onClose: () => void
}

export const WorkflowDialog: Component<WorkflowDialogProps> = (props) => {
  const [name, setName] = createSignal('')
  const [description, setDescription] = createSignal('')
  const [tags, setTags] = createSignal('')
  const [promptPreview, setPromptPreview] = createSignal('')
  const [saving, setSaving] = createSignal(false)
  const [error, setError] = createSignal('')

  const { currentSession, messages } = useSession()
  const { currentProject } = useProject()
  const { createFromSession, loadWorkflows } = useWorkflows()

  // Load prompt preview when dialog opens
  createEffect(() => {
    if (props.open) {
      const userMsgs = messages().filter((m) => m.role === 'user')
      const preview = userMsgs.map((m) => m.content).join('\n\n---\n\n')
      setPromptPreview(preview)
      setName('')
      setDescription('')
      setTags('')
      setError('')
    }
  })

  const handleSave = async () => {
    const n = name().trim()
    if (!n) {
      setError('Name is required')
      return
    }
    const session = currentSession()
    if (!session) return

    setSaving(true)
    setError('')
    try {
      const tagList = tags()
        .split(',')
        .map((t) => t.trim())
        .filter(Boolean)
      await createFromSession(session.id, n, description().trim(), tagList, currentProject()?.id)
      await loadWorkflows(currentProject()?.id)
      props.onClose()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to save workflow')
    } finally {
      setSaving(false)
    }
  }

  const inputClass =
    'w-full px-2.5 py-1.5 text-xs rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none'

  return (
    <Dialog open={props.open} onOpenChange={(open) => !open && props.onClose()}>
      <Dialog.Portal>
        <Dialog.Overlay class="fixed inset-0 z-50 bg-black/60 data-[expanded]:animate-in data-[expanded]:fade-in-0 data-[closed]:animate-out data-[closed]:fade-out-0" />
        <Dialog.Content
          class="
            fixed left-1/2 top-1/2 z-50
            -translate-x-1/2 -translate-y-1/2
            w-full max-w-md
            bg-[var(--surface-overlay)]
            border border-[var(--border-default)]
            rounded-[var(--radius-xl)]
            shadow-2xl
            data-[expanded]:animate-in data-[expanded]:fade-in-0 data-[expanded]:zoom-in-95
            data-[closed]:animate-out data-[closed]:fade-out-0 data-[closed]:zoom-out-95
            duration-200
          "
        >
          {/* Header */}
          <div class="flex items-center justify-between px-4 py-3 border-b border-[var(--border-subtle)]">
            <div class="flex items-center gap-2">
              <BookmarkPlus class="w-4 h-4 text-[var(--accent)]" />
              <Dialog.Title class="text-sm font-semibold text-[var(--text-primary)]">
                Save as Workflow
              </Dialog.Title>
            </div>
            <Dialog.CloseButton class="p-1 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--surface-raised)] transition-colors">
              <X class="w-4 h-4" />
            </Dialog.CloseButton>
          </div>

          {/* Form */}
          <div class="px-4 py-4 space-y-3">
            <label class="block">
              <span class="text-[10px] font-medium text-[var(--text-muted)] uppercase tracking-wider">
                Name *
              </span>
              <input
                type="text"
                value={name()}
                onInput={(e) => {
                  setName(e.currentTarget.value)
                  setError('')
                }}
                class={inputClass}
                placeholder="e.g., Add Unit Tests"
              />
            </label>

            <label class="block">
              <span class="text-[10px] font-medium text-[var(--text-muted)] uppercase tracking-wider">
                Description
              </span>
              <textarea
                value={description()}
                onInput={(e) => setDescription(e.currentTarget.value)}
                class={`${inputClass} resize-y min-h-[60px]`}
                placeholder="What does this workflow do?"
              />
            </label>

            <label class="block">
              <span class="text-[10px] font-medium text-[var(--text-muted)] uppercase tracking-wider">
                Tags
              </span>
              <input
                type="text"
                value={tags()}
                onInput={(e) => setTags(e.currentTarget.value)}
                class={inputClass}
                placeholder="testing, react, refactor"
              />
              <p class="text-[10px] text-[var(--text-muted)] mt-0.5">Comma-separated</p>
            </label>

            <label class="block">
              <span class="text-[10px] font-medium text-[var(--text-muted)] uppercase tracking-wider">
                Extracted Prompt
              </span>
              <textarea
                value={promptPreview()}
                readOnly
                class={`${inputClass} font-mono resize-none max-h-40 min-h-[80px] text-[var(--text-muted)]`}
              />
              <p class="text-[10px] text-[var(--text-muted)] mt-0.5">
                Combined from {messages().filter((m) => m.role === 'user').length} user message(s)
              </p>
            </label>

            <Show when={error()}>
              <p class="text-[10px] text-[var(--error)]">{error()}</p>
            </Show>
          </div>

          {/* Footer */}
          <div class="flex justify-end gap-2 px-4 py-3 border-t border-[var(--border-subtle)]">
            <button
              type="button"
              onClick={() => props.onClose()}
              class="px-3 py-1.5 text-xs font-medium rounded-[var(--radius-md)] text-[var(--text-secondary)] hover:bg-[var(--surface-raised)] transition-colors"
            >
              Cancel
            </button>
            <button
              type="button"
              onClick={handleSave}
              disabled={saving() || !name().trim()}
              class="px-3 py-1.5 text-xs font-medium rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:bg-[var(--accent-hover)] transition-colors disabled:opacity-40 disabled:cursor-not-allowed"
            >
              {saving() ? 'Saving...' : 'Save Workflow'}
            </button>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog>
  )
}
