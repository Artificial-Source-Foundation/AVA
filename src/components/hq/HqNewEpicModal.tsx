import { Layers, Paperclip, X } from 'lucide-solid'
import { type Component, createSignal } from 'solid-js'

export const HqNewEpicModal: Component<{
  onClose: () => void
  onCreate: (title: string, description: string) => void
}> = (props) => {
  const [title, setTitle] = createSignal('')
  const [description, setDescription] = createSignal('')

  const handleCreate = (): void => {
    const t = title().trim()
    if (!t) return
    props.onCreate(t, description().trim())
  }

  return (
    <div class="absolute inset-0 z-50 flex items-center justify-center">
      <button
        type="button"
        aria-label="Close new epic modal"
        class="absolute inset-0"
        style={{
          'background-color': 'color-mix(in srgb, var(--background) 78%, black)',
        }}
        onClick={props.onClose}
      />
      <div
        role="dialog"
        aria-modal="true"
        aria-labelledby="hq-new-epic-title"
        class="relative flex flex-col gap-6 w-[560px] max-w-[calc(100vw-32px)] p-8 rounded-2xl shadow-2xl"
        style={{
          background:
            'linear-gradient(180deg, color-mix(in srgb, var(--surface) 92%, white) 0%, var(--surface) 100%)',
          border: '1px solid color-mix(in srgb, var(--border-subtle) 85%, white)',
          'box-shadow': '0 30px 80px rgba(0, 0, 0, 0.45)',
        }}
      >
        {/* Header */}
        <div class="flex items-center justify-between">
          <span
            id="hq-new-epic-title"
            class="text-lg font-bold"
            style={{ color: 'var(--text-primary)' }}
          >
            New Epic
          </span>
          <button
            type="button"
            class="flex items-center justify-center w-7 h-7 rounded-md hover:bg-[var(--alpha-white-8)] transition-colors"
            onClick={props.onClose}
          >
            <X size={18} class="text-zinc-500" />
          </button>
        </div>

        {/* Description */}
        <p class="text-sm" style={{ color: 'var(--text-secondary)', 'line-height': '1.6' }}>
          Describe the feature or initiative. The Director will analyze it, create a plan, decompose
          into issues, and assign agents.
        </p>

        {/* Epic Title */}
        <div class="flex flex-col gap-2">
          <label
            for="hq-epic-title"
            class="text-[11px] font-semibold"
            style={{ color: 'var(--text-secondary)' }}
          >
            Epic
          </label>
          <textarea
            id="hq-epic-title"
            class="w-full h-[100px] px-3 py-2.5 rounded-lg text-xs resize-none outline-none"
            style={{
              'background-color': 'color-mix(in srgb, var(--surface) 82%, black)',
              color: 'var(--text-primary)',
              border: '1px solid var(--border-subtle)',
              'line-height': '1.5',
            }}
            placeholder="Implement user authentication with OAuth2 for the API. Include session management and rate limiting."
            value={title()}
            onInput={(e) => setTitle(e.currentTarget.value)}
          />
        </div>

        {/* Context */}
        <div class="flex flex-col gap-2">
          <label
            for="hq-epic-context"
            class="text-[11px] font-semibold"
            style={{ color: 'var(--text-secondary)' }}
          >
            Additional context (optional)
          </label>
          <div
            class="flex items-center gap-2 w-full h-9 px-3 rounded-lg"
            style={{
              'background-color': 'color-mix(in srgb, var(--surface) 82%, black)',
              border: '1px solid var(--border-subtle)',
            }}
          >
            <Paperclip size={13} style={{ color: 'var(--text-muted)' }} />
            <input
              id="hq-epic-context"
              class="flex-1 bg-transparent text-xs outline-none"
              style={{ color: 'var(--text-primary)' }}
              placeholder="Attach files, reference issues..."
              value={description()}
              onInput={(e) => setDescription(e.currentTarget.value)}
            />
          </div>
        </div>

        {/* Actions */}
        <div class="flex items-center justify-end gap-2">
          <button
            type="button"
            class="h-9 px-4 rounded-lg text-xs font-medium"
            style={{
              color: 'var(--text-secondary)',
              border: '1px solid var(--border-subtle)',
            }}
            onClick={props.onClose}
          >
            Cancel
          </button>
          <button
            type="button"
            class="flex items-center gap-1.5 h-9 px-5 rounded-lg text-xs font-semibold"
            style={{
              'background-color': title().trim() ? 'var(--accent)' : 'var(--surface)',
              color: title().trim() ? 'white' : 'var(--text-muted)',
              border: title().trim() ? 'none' : '1px solid var(--border-subtle)',
              opacity: '1',
            }}
            onClick={handleCreate}
            disabled={!title().trim()}
          >
            <Layers size={14} />
            Create Epic
          </button>
        </div>
      </div>
    </div>
  )
}
