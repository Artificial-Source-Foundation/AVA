import type { Component } from 'solid-js'
import { createSignal } from 'solid-js'
import { PLAN_ACCENT } from './types'

export const CommentPopover: Component<{
  contextText: string
  top: number
  left: number
  onSave: (comment: string) => void
  onCancel: () => void
}> = (props) => {
  const [text, setText] = createSignal('')

  return (
    <div
      role="dialog"
      aria-modal="true"
      aria-label="Add comment"
      data-comment-popover
      class="fixed z-[110] rounded-xl border w-[320px]"
      style={{
        top: `${props.top}px`,
        left: `${props.left}px`,
        transform: 'translateX(-50%)',
        background: 'var(--surface-raised)',
        'border-color': 'var(--border-subtle)',
        'box-shadow': '0 20px 40px -10px rgba(0,0,0,0.3)',
        animation: 'selectionToolbarIn 150ms ease-out',
      }}
    >
      {/* Context quote */}
      <div
        class="px-4 pt-3 pb-2 text-[11px] italic border-b"
        style={{
          color: 'var(--text-muted)',
          'border-color': 'var(--border-subtle)',
          'max-height': '60px',
          overflow: 'hidden',
          'text-overflow': 'ellipsis',
        }}
      >
        &ldquo;{props.contextText.slice(0, 120)}
        {props.contextText.length > 120 ? '...' : ''}&rdquo;
      </div>

      {/* Textarea */}
      <div class="p-3">
        <textarea
          value={text()}
          onInput={(e) => setText(e.currentTarget.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
              e.preventDefault()
              const val = text().trim()
              if (val) props.onSave(val)
            }
            if (e.key === 'Escape') {
              e.preventDefault()
              props.onCancel()
            }
          }}
          ref={(el) => setTimeout(() => el.focus(), 50)}
          placeholder="Add a comment..."
          rows={3}
          class="w-full text-[13px] rounded-lg border px-3 py-2 outline-none resize-none"
          style={{
            color: 'var(--text-primary)',
            background: 'var(--alpha-white-3)',
            'border-color': 'var(--border-subtle)',
          }}
        />
        <div class="flex items-center justify-between mt-2">
          <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
            Ctrl+Enter to save
          </span>
          <div class="flex items-center gap-2">
            <button
              type="button"
              onClick={() => props.onCancel()}
              class="text-[12px] px-3 py-1 rounded-md transition-colors"
              style={{ color: 'var(--text-muted)' }}
            >
              Cancel
            </button>
            <button
              type="button"
              onClick={() => {
                const val = text().trim()
                if (val) props.onSave(val)
              }}
              class="text-[12px] px-3 py-1 rounded-md font-medium transition-colors"
              style={{ color: '#fff', background: PLAN_ACCENT }}
            >
              Save
            </button>
          </div>
        </div>
      </div>
    </div>
  )
}
