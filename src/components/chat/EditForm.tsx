/**
 * Edit Form Component
 *
 * Inline form for editing user messages.
 * Premium styling with themed colors.
 */

import { Check, Loader2, X } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'

interface EditFormProps {
  initialContent: string
  onSave: (content: string) => Promise<void>
  onCancel: () => void
}

export const EditForm: Component<EditFormProps> = (props) => {
  const [content, setContent] = createSignal(props.initialContent)
  const [isSaving, setIsSaving] = createSignal(false)

  const handleSave = async () => {
    if (!content().trim() || isSaving()) return
    setIsSaving(true)
    try {
      await props.onSave(content())
    } finally {
      setIsSaving(false)
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault()
      handleSave()
    }
    if (e.key === 'Escape') {
      props.onCancel()
    }
  }

  return (
    <div
      class="
        max-w-[80%] ml-auto
        bg-[var(--accent-subtle)]
        border border-[var(--accent-muted)]
        rounded-[var(--radius-lg)]
        p-3
      "
    >
      <textarea
        value={content()}
        onInput={(e) => setContent(e.currentTarget.value)}
        onKeyDown={handleKeyDown}
        class="
          w-full p-3
          bg-[var(--input-background)]
          text-[var(--text-primary)]
          border border-[var(--input-border)]
          rounded-[var(--radius-md)]
          text-sm resize-none
          min-h-[80px]
          transition-all duration-[var(--duration-fast)]
          focus:outline-none focus:border-[var(--input-border-focus)] focus:ring-2 focus:ring-[var(--accent-subtle)]
          disabled:opacity-50
        "
        rows={3}
        autofocus
        disabled={isSaving()}
      />
      <div class="flex items-center justify-between mt-3">
        <p class="text-xs text-[var(--text-muted)]">Ctrl+Enter to save, Escape to cancel</p>
        <div class="flex gap-2">
          <button
            type="button"
            onClick={() => props.onCancel()}
            disabled={isSaving()}
            class="
              px-3 py-1.5
              text-[var(--text-secondary)] hover:text-[var(--text-primary)]
              text-sm font-medium
              rounded-[var(--radius-md)]
              transition-colors duration-[var(--duration-fast)]
              disabled:opacity-50
              flex items-center gap-1.5
            "
          >
            <X class="w-3.5 h-3.5" />
            Cancel
          </button>
          <button
            type="button"
            onClick={handleSave}
            disabled={isSaving() || !content().trim()}
            class="
              px-3 py-1.5
              bg-[var(--accent)] hover:bg-[var(--accent-hover)]
              text-white text-sm font-medium
              rounded-[var(--radius-md)]
              transition-all duration-[var(--duration-fast)]
              disabled:opacity-50 disabled:cursor-not-allowed
              flex items-center gap-1.5
            "
          >
            <Show
              when={isSaving()}
              fallback={
                <>
                  <Check class="w-3.5 h-3.5" />
                  Save & Resend
                </>
              }
            >
              <Loader2 class="w-3.5 h-3.5 animate-spin" />
              Saving...
            </Show>
          </button>
        </div>
      </div>
    </div>
  )
}
