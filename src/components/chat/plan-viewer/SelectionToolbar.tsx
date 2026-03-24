import { Copy, MessageSquare, Trash2, X, Zap } from 'lucide-solid'
import type { Component } from 'solid-js'
import { PLAN_ACCENT } from './types'

export const SelectionToolbar: Component<{
  text: string
  top: number
  left: number
  onCopy: () => void
  onDelete: () => void
  onComment: () => void
  onQuickLabel: () => void
  onClose: () => void
}> = (props) => {
  return (
    <div
      data-selection-toolbar
      class="fixed z-[100] flex items-center gap-0.5 rounded-lg border p-1"
      style={{
        top: `${props.top}px`,
        left: `${props.left}px`,
        transform: 'translateX(-50%)',
        background: 'var(--surface-raised)',
        'border-color': 'var(--border-subtle)',
        'box-shadow': '0 10px 25px -5px rgba(0,0,0,0.25), 0 8px 10px -6px rgba(0,0,0,0.15)',
        animation: 'selectionToolbarIn 150ms ease-out',
      }}
    >
      <button
        type="button"
        onClick={() => props.onCopy()}
        class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-md text-[12px] font-medium transition-colors"
        style={{ color: 'var(--text-secondary)' }}
        title="Copy selected text"
      >
        <Copy class="w-3.5 h-3.5" />
        Copy
      </button>

      <div class="w-px h-5 mx-0.5" style={{ background: 'var(--border-subtle)' }} />

      <button
        type="button"
        onClick={() => props.onDelete()}
        class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-md text-[12px] font-medium transition-colors"
        style={{ color: '#EF4444' }}
        title="Mark for deletion"
      >
        <Trash2 class="w-3.5 h-3.5" />
        Delete
      </button>

      <button
        type="button"
        onClick={() => props.onComment()}
        class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-md text-[12px] font-medium transition-colors"
        style={{ color: PLAN_ACCENT }}
        title="Add comment"
      >
        <MessageSquare class="w-3.5 h-3.5" />
        Comment
      </button>

      <button
        type="button"
        onClick={() => props.onQuickLabel()}
        class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-md text-[12px] font-medium transition-colors"
        style={{ color: '#F59E0B' }}
        title="Quick label"
      >
        <Zap class="w-3.5 h-3.5" />
      </button>

      <button
        type="button"
        onClick={() => props.onClose()}
        class="flex items-center px-1.5 py-1.5 rounded-md text-[12px] transition-colors"
        style={{ color: 'var(--text-muted)' }}
        title="Dismiss"
      >
        <X class="w-3.5 h-3.5" />
      </button>
    </div>
  )
}
