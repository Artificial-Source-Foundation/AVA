import { ChevronDown, ChevronRight, Layers, Plus, Target } from 'lucide-solid'
import { type Component, createSignal, For } from 'solid-js'
import { useHq } from '../../../stores/hq'
import type { EpicStatus } from '../../../types/hq'

function statusBadge(status: EpicStatus): { label: string; color: string } {
  switch (status) {
    case 'in-progress':
      return { label: 'In Progress', color: '#06b6d4' }
    case 'completed':
      return { label: 'Completed', color: 'var(--success)' }
    case 'planning':
      return { label: 'Planning', color: 'var(--warning)' }
    case 'paused':
      return { label: 'Paused', color: 'var(--text-muted)' }
  }
}

const HqEpics: Component = () => {
  const { epics, navigateToEpic, openNewEpicModal } = useHq()
  const [expandedIds, setExpandedIds] = createSignal<Set<string>>(new Set())

  function toggleExpand(id: string): void {
    setExpandedIds((prev) => {
      const next = new Set(prev)
      if (next.has(id)) next.delete(id)
      else next.add(id)
      return next
    })
  }

  return (
    <div class="flex flex-col h-full" style={{ 'background-color': 'var(--background)' }}>
      {/* Header */}
      <div
        class="flex items-center justify-between shrink-0 px-6 h-14"
        style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
      >
        <div class="flex items-center gap-2.5">
          <Layers size={18} style={{ color: 'var(--text-muted)' }} />
          <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
            Epics
          </span>
        </div>
        <button
          type="button"
          class="flex items-center gap-1.5 h-8 px-3.5 rounded-md text-xs font-semibold"
          style={{ 'background-color': 'var(--accent)', color: 'white' }}
          onClick={openNewEpicModal}
        >
          <Plus size={14} />
          New Epic
        </button>
      </div>

      {/* Epic list */}
      <div class="flex-1 overflow-y-auto">
        <For each={epics()}>
          {(epic) => {
            const badge = statusBadge(epic.status)
            const isCompleted = epic.status === 'completed'
            const isExpanded = () => expandedIds().has(epic.id)

            return (
              <div style={{ opacity: isCompleted ? '0.6' : '1' }}>
                <div
                  class="flex items-center gap-3 px-6 h-14"
                  style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
                >
                  {/* Expand chevron */}
                  <button
                    type="button"
                    class="flex items-center justify-center w-5 h-5 shrink-0"
                    onClick={(e) => {
                      e.stopPropagation()
                      toggleExpand(epic.id)
                    }}
                  >
                    {isExpanded() ? (
                      <ChevronDown size={14} style={{ color: 'var(--text-muted)' }} />
                    ) : (
                      <ChevronRight size={14} style={{ color: 'var(--text-muted)' }} />
                    )}
                  </button>

                  {/* Icon */}
                  <button
                    type="button"
                    class="flex items-center gap-3 flex-1 min-w-0 text-left"
                    onClick={() => navigateToEpic(epic.id)}
                  >
                    {/* Icon */}
                    <Target size={14} style={{ color: 'var(--text-muted)' }} />

                    {/* Title */}
                    <span
                      class="text-sm font-medium flex-1 truncate"
                      style={{ color: 'var(--text-primary)' }}
                    >
                      {epic.title}
                    </span>

                    {/* Status badge */}
                    <span
                      class="text-[9px] font-semibold px-1.5 py-0.5 rounded shrink-0"
                      style={{ color: badge.color, 'background-color': `${badge.color}22` }}
                    >
                      {badge.label}
                    </span>

                    {/* Progress bar */}
                    <div
                      class="w-24 h-1.5 rounded-full shrink-0 overflow-hidden"
                      style={{ 'background-color': 'var(--surface)' }}
                    >
                      <div
                        class="h-full rounded-full"
                        style={{
                          width: `${epic.progress}%`,
                          'background-color': isCompleted ? 'var(--success)' : 'var(--accent)',
                        }}
                      />
                    </div>

                    {/* Issue count */}
                    <span
                      class="text-[11px] font-mono shrink-0 w-8 text-right"
                      style={{ color: 'var(--text-muted)' }}
                    >
                      {epic.issueIds.length}
                    </span>
                  </button>
                </div>

                {/* Expanded description */}
                {isExpanded() && (
                  <div
                    class="px-6 py-3 pl-16"
                    style={{
                      'background-color': 'var(--surface)',
                      'border-bottom': '1px solid var(--border-subtle)',
                    }}
                  >
                    <p class="text-xs leading-relaxed" style={{ color: 'var(--text-muted)' }}>
                      {epic.description}
                    </p>
                  </div>
                )}
              </div>
            )
          }}
        </For>
      </div>
    </div>
  )
}

export default HqEpics
