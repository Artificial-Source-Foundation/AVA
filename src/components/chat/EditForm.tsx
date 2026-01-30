/**
 * EditForm Component
 * Inline form for editing user messages
 */

import { type Component, createSignal } from 'solid-js'

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
    // Save on Ctrl+Enter or Cmd+Enter
    if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault()
      handleSave()
    }
    // Cancel on Escape
    if (e.key === 'Escape') {
      props.onCancel()
    }
  }

  return (
    <div class="max-w-[80%] bg-blue-900/50 rounded-lg p-3 ml-auto">
      <textarea
        value={content()}
        onInput={(e) => setContent(e.currentTarget.value)}
        onKeyDown={handleKeyDown}
        class="w-full bg-gray-800 text-white rounded p-2 resize-none focus:outline-none focus:ring-2 focus:ring-blue-500 min-h-[80px]"
        rows={3}
        autofocus
        disabled={isSaving()}
      />
      <div class="flex justify-end gap-2 mt-2">
        <button
          type="button"
          onClick={props.onCancel}
          disabled={isSaving()}
          class="px-3 py-1 text-gray-400 hover:text-white disabled:opacity-50"
        >
          Cancel
        </button>
        <button
          type="button"
          onClick={handleSave}
          disabled={isSaving() || !content().trim()}
          class="px-3 py-1 bg-blue-600 hover:bg-blue-500 text-white rounded disabled:opacity-50"
        >
          {isSaving() ? 'Saving...' : 'Save & Resend'}
        </button>
      </div>
      <p class="text-xs text-gray-500 mt-1">Ctrl+Enter to save, Escape to cancel</p>
    </div>
  )
}
