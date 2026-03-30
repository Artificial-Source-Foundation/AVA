import { MessageCircle, X } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { PlanAnnotation } from '../../../stores/planOverlayStore'
import { PLAN_ACCENT, PLAN_ACCENT_SUBTLE } from './types'

export const AnnotationsPanel: Component<{
  annotations: PlanAnnotation[]
  focusedId: string | null
  onFocus: (id: string) => void
  onRemove: (id: string) => void
}> = (props) => {
  return (
    <aside
      class="flex flex-col h-full border-l flex-shrink-0 overflow-hidden"
      style={{
        width: '288px',
        background: 'var(--surface)',
        'border-color': 'var(--border-subtle)',
      }}
    >
      {/* Header */}
      <div
        class="flex items-center gap-2 px-4 py-3 border-b flex-shrink-0"
        style={{ 'border-color': 'var(--border-subtle)' }}
      >
        <span
          class="text-[11px] font-semibold tracking-widest uppercase"
          style={{ color: 'var(--text-muted)' }}
        >
          Annotations
        </span>
        <Show when={props.annotations.length > 0}>
          <span
            class="inline-flex items-center justify-center min-w-[18px] h-[18px] rounded-full text-[10px] font-bold px-1"
            style={{ background: PLAN_ACCENT_SUBTLE, color: PLAN_ACCENT }}
          >
            {props.annotations.length}
          </span>
        </Show>
      </div>

      {/* Content */}
      <div class="flex-1 overflow-y-auto p-3 space-y-2">
        <Show
          when={props.annotations.length > 0}
          fallback={
            <div class="flex flex-col items-center justify-center h-full gap-3 text-center px-4">
              <div
                class="w-10 h-10 rounded-full flex items-center justify-center"
                style={{ background: 'var(--alpha-white-5)' }}
              >
                <MessageCircle class="w-5 h-5" style={{ color: 'var(--text-muted)' }} />
              </div>
              <span class="text-[12px]" style={{ color: 'var(--text-muted)' }}>
                Select text to add annotations
              </span>
            </div>
          }
        >
          <For each={props.annotations}>
            {(ann) => (
              <article
                onClick={() => props.onFocus(ann.id)}
                onKeyDown={(e) => {
                  if (e.key === 'Enter') props.onFocus(ann.id)
                }}
                class="w-full cursor-pointer rounded-lg border p-3 transition-[background-color,border-color]"
                style={{
                  background:
                    props.focusedId === ann.id
                      ? 'rgba(59, 130, 246, 0.08)'
                      : 'var(--alpha-white-3)',
                  'border-color':
                    props.focusedId === ann.id ? 'rgba(59, 130, 246, 0.3)' : 'var(--border-subtle)',
                }}
              >
                <div class="flex items-center justify-between mb-1.5">
                  <span
                    class="inline-flex items-center px-1.5 py-0.5 rounded text-[9px] font-semibold uppercase tracking-wider"
                    style={{
                      background:
                        ann.type === 'deletion'
                          ? 'rgba(239, 68, 68, 0.12)'
                          : ann.type === 'comment'
                            ? 'rgba(234, 179, 8, 0.12)'
                            : 'rgba(139, 92, 246, 0.12)',
                      color:
                        ann.type === 'deletion'
                          ? '#EF4444'
                          : ann.type === 'comment'
                            ? '#EAB308'
                            : PLAN_ACCENT,
                    }}
                  >
                    {ann.type === 'deletion'
                      ? 'Deletion'
                      : ann.type === 'comment'
                        ? 'Comment'
                        : 'Global'}
                  </span>
                  <button
                    type="button"
                    onClick={(e) => {
                      e.stopPropagation()
                      props.onRemove(ann.id)
                    }}
                    class="p-0.5 rounded transition-colors"
                    style={{ color: 'var(--text-muted)' }}
                    title="Remove annotation"
                  >
                    <X class="w-3 h-3" />
                  </button>
                </div>
                <p
                  class="text-[11px] leading-relaxed mb-1"
                  style={{
                    color: 'var(--text-secondary)',
                    'text-decoration': ann.type === 'deletion' ? 'line-through' : 'none',
                    'text-decoration-color': ann.type === 'deletion' ? '#EF4444' : undefined,
                  }}
                >
                  {ann.originalText.slice(0, 80)}
                  {ann.originalText.length > 80 ? '...' : ''}
                </p>
                <Show when={ann.commentText}>
                  <p class="text-[11px] italic" style={{ color: 'var(--text-muted)' }}>
                    {ann.commentText}
                  </p>
                </Show>
              </article>
            )}
          </For>
        </Show>
      </div>
    </aside>
  )
}
