import type { Component } from 'solid-js'
import { For } from 'solid-js'
import { QUICK_LABELS } from './types'

export const QuickLabelPicker: Component<{
  text: string
  top: number
  left: number
  onSelect: (labelId: string, labelText: string) => void
  onCancel: () => void
}> = (props) => {
  // Handle escape key
  const handleKeyDown = (e: KeyboardEvent): void => {
    if (e.key === 'Escape') {
      e.preventDefault()
      props.onCancel()
    }
  }

  return (
    <div
      role="dialog"
      aria-modal="true"
      aria-label="Quick labels"
      onKeyDown={handleKeyDown}
      data-quick-label-picker
      class="fixed z-[110] rounded-xl border w-[260px] overflow-hidden"
      style={{
        top: `${props.top}px`,
        left: `${props.left}px`,
        background: 'var(--surface-raised)',
        'border-color': 'var(--border-subtle)',
        'box-shadow': '0 20px 40px -10px rgba(0,0,0,0.3)',
        animation: 'selectionToolbarIn 150ms ease-out',
      }}
    >
      <div
        class="px-3 py-2 text-[10px] font-semibold tracking-widest uppercase border-b"
        style={{ color: 'var(--text-muted)', 'border-color': 'var(--border-subtle)' }}
      >
        Quick Labels
      </div>
      <div class="py-1 max-h-[320px] overflow-y-auto">
        <For each={QUICK_LABELS}>
          {(label, i) => (
            <button
              type="button"
              onClick={() => props.onSelect(label.id, label.text)}
              class="w-full text-left flex items-center gap-2.5 px-3 py-2 transition-colors text-[12px]"
              style={{ color: 'var(--text-primary)' }}
              title={label.tip}
            >
              <span
                class="w-1 h-5 rounded-full flex-shrink-0"
                style={{ background: label.color }}
              />
              <span class="flex-shrink-0">{label.emoji}</span>
              <span class="flex-1 truncate">{label.text}</span>
              <span
                class="text-[10px] font-mono flex-shrink-0"
                style={{ color: 'var(--text-muted)' }}
              >
                {i() === 9 ? '0' : i() + 1}
              </span>
            </button>
          )}
        </For>
      </div>
    </div>
  )
}
