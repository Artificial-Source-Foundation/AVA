import { Crosshair, MessageSquare, MousePointer2, Pencil, Scissors, Tag } from 'lucide-solid'
import type { Component } from 'solid-js'
import type { EditorMode, InputMethod } from './types'
import { PLAN_ACCENT, PLAN_ACCENT_SUBTLE } from './types'

export const AnnotationToolstrip: Component<{
  editorMode: EditorMode
  inputMethod: InputMethod
  onEditorModeChange: (mode: EditorMode) => void
  onInputMethodChange: (method: InputMethod) => void
}> = (props) => {
  return (
    <div class="flex items-center justify-center py-2">
      <div
        class="inline-flex items-center gap-1 rounded-full border px-1.5 py-1"
        style={{
          background: 'var(--surface-raised)',
          'border-color': 'var(--border-subtle)',
          'box-shadow': '0 2px 8px rgba(0,0,0,0.12)',
        }}
      >
        {/* Group 1: Input methods — Select | Pinpoint */}
        <div
          class="flex items-center gap-0.5 rounded-full px-0.5 py-0.5"
          style={{ background: 'var(--alpha-white-3)' }}
        >
          <button
            type="button"
            onClick={() => props.onInputMethodChange('drag')}
            class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-full text-[11px] font-medium transition-colors"
            style={{
              background: props.inputMethod === 'drag' ? PLAN_ACCENT_SUBTLE : 'transparent',
              color: props.inputMethod === 'drag' ? PLAN_ACCENT : 'var(--text-muted)',
            }}
          >
            <MousePointer2 class="w-3.5 h-3.5" />
            Select
          </button>
          <button
            type="button"
            onClick={() => props.onInputMethodChange('pinpoint')}
            class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-full text-[11px] font-medium transition-colors"
            style={{
              background: props.inputMethod === 'pinpoint' ? PLAN_ACCENT_SUBTLE : 'transparent',
              color: props.inputMethod === 'pinpoint' ? PLAN_ACCENT : 'var(--text-muted)',
            }}
          >
            <Crosshair class="w-3.5 h-3.5" />
            Pinpoint
          </button>
        </div>

        <div class="w-px h-5" style={{ background: 'var(--border-subtle)' }} />

        {/* Group 2: Action modes — Markup | Comment | Redline | Label */}
        <div
          class="flex items-center gap-0.5 rounded-full px-0.5 py-0.5"
          style={{ background: 'var(--alpha-white-3)' }}
        >
          <button
            type="button"
            onClick={() => props.onEditorModeChange('selection')}
            class="flex items-center gap-1.5 px-2.5 py-1.5 rounded-full text-[11px] font-medium transition-colors"
            style={{
              background: props.editorMode === 'selection' ? PLAN_ACCENT_SUBTLE : 'transparent',
              color: props.editorMode === 'selection' ? PLAN_ACCENT : 'var(--text-muted)',
            }}
          >
            <Pencil class="w-3.5 h-3.5" />
            Markup
          </button>
          <button
            type="button"
            onClick={() => props.onEditorModeChange('comment')}
            class="p-1.5 rounded-full transition-colors"
            style={{
              background: props.editorMode === 'comment' ? PLAN_ACCENT_SUBTLE : 'transparent',
              color: props.editorMode === 'comment' ? PLAN_ACCENT : 'var(--text-muted)',
            }}
            title="Comment mode"
          >
            <MessageSquare class="w-3.5 h-3.5" />
          </button>
          <button
            type="button"
            onClick={() => props.onEditorModeChange('redline')}
            class="p-1.5 rounded-full transition-colors"
            style={{
              background:
                props.editorMode === 'redline' ? 'rgba(239, 68, 68, 0.12)' : 'transparent',
              color: props.editorMode === 'redline' ? '#EF4444' : 'var(--text-muted)',
            }}
            title="Redline mode (instant deletion)"
          >
            <Scissors class="w-3.5 h-3.5" />
          </button>
          <button
            type="button"
            onClick={() => props.onEditorModeChange('quickLabel')}
            class="p-1.5 rounded-full transition-colors"
            style={{
              background:
                props.editorMode === 'quickLabel' ? 'rgba(245, 158, 11, 0.12)' : 'transparent',
              color: props.editorMode === 'quickLabel' ? '#F59E0B' : 'var(--text-muted)',
            }}
            title="Quick label mode"
          >
            <Tag class="w-3.5 h-3.5" />
          </button>
        </div>

        {/* Help link */}
        <button
          type="button"
          class="text-[10px] px-2 transition-colors"
          style={{ color: 'var(--text-muted)', background: 'none', border: 'none' }}
        >
          how does this work?
        </button>
      </div>
    </div>
  )
}
