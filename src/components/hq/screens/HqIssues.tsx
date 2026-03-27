import { ArrowDown, ArrowRight, ArrowUp, CircleDot, Filter, Plus } from 'lucide-solid'
import { type Component, createSignal, For } from 'solid-js'
import { useHq } from '../../../stores/hq'
import type { HqIssue, IssuePriority, KanbanColumn } from '../../../types/hq'

type ViewMode = 'board' | 'list'

interface ColumnDef {
  key: KanbanColumn
  name: string
  dotColor: string
}

const COLUMNS: ColumnDef[] = [
  { key: 'backlog', name: 'Backlog', dotColor: '#3b82f6' },
  { key: 'in-progress', name: 'In Progress', dotColor: '#06b6d4' },
  { key: 'review', name: 'Review', dotColor: '#eab308' },
  { key: 'done', name: 'Done', dotColor: 'var(--success)' },
]

function priorityIcon(p: IssuePriority): {
  Icon: Component<{ size?: number; style?: Record<string, string> }>
  color: string
} {
  switch (p) {
    case 'urgent':
      return { Icon: ArrowUp, color: 'var(--error)' }
    case 'high':
      return { Icon: ArrowUp, color: 'var(--warning)' }
    case 'medium':
      return { Icon: ArrowRight, color: 'var(--text-muted)' }
    case 'low':
      return { Icon: ArrowDown, color: 'var(--text-muted)' }
  }
}

const HqIssues: Component = () => {
  const { issues, issuesByColumn, navigateToIssue } = useHq()
  const [viewMode, setViewMode] = createSignal<ViewMode>('board')
  const [draggedId, setDraggedId] = createSignal<string | null>(null)

  function handleDragStart(e: DragEvent, issueId: string): void {
    setDraggedId(issueId)
    if (e.dataTransfer) {
      e.dataTransfer.effectAllowed = 'move'
      e.dataTransfer.setData('text/plain', issueId)
    }
  }

  function handleDragOver(e: DragEvent): void {
    e.preventDefault()
    if (e.dataTransfer) e.dataTransfer.dropEffect = 'move'
  }

  function handleDrop(e: DragEvent, column: KanbanColumn): void {
    e.preventDefault()
    const id = draggedId()
    if (id) {
      void useHq().moveIssue(id, column)
      setDraggedId(null)
    }
  }

  return (
    <div class="flex flex-col h-full" style={{ 'background-color': 'var(--background)' }}>
      {/* Header */}
      <div
        class="flex items-center justify-between shrink-0 px-6 h-14"
        style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
      >
        <div class="flex items-center gap-2.5">
          <CircleDot size={18} style={{ color: 'var(--text-muted)' }} />
          <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
            Issues
          </span>
          <span
            class="text-[11px] font-mono px-1.5 py-0.5 rounded"
            style={{ color: 'var(--text-muted)', 'background-color': 'var(--surface)' }}
          >
            {issues().length}
          </span>
        </div>
        <div class="flex items-center gap-2">
          {/* View toggle */}
          <div
            class="flex items-center rounded-md overflow-hidden"
            style={{ border: '1px solid var(--border-subtle)' }}
          >
            <button
              type="button"
              class="px-2.5 h-7 text-[11px] font-medium"
              style={{
                'background-color': viewMode() === 'board' ? 'var(--accent)' : 'var(--surface)',
                color: viewMode() === 'board' ? 'white' : 'var(--text-muted)',
              }}
              onClick={() => setViewMode('board')}
            >
              Board
            </button>
            <button
              type="button"
              class="px-2.5 h-7 text-[11px] font-medium"
              style={{
                'background-color': viewMode() === 'list' ? 'var(--accent)' : 'var(--surface)',
                color: viewMode() === 'list' ? 'white' : 'var(--text-muted)',
              }}
              onClick={() => setViewMode('list')}
            >
              List
            </button>
          </div>
          <button
            type="button"
            class="flex items-center gap-1.5 h-8 px-3 rounded-md text-xs font-medium"
            style={{
              'background-color': 'var(--surface)',
              border: '1px solid var(--border-subtle)',
              color: 'var(--text-secondary)',
            }}
          >
            <Filter size={13} />
            Filter
          </button>
          <button
            type="button"
            class="flex items-center gap-1.5 h-8 px-3.5 rounded-md text-xs font-semibold"
            style={{ 'background-color': 'var(--accent)', color: 'white' }}
          >
            <Plus size={14} />
            New Issue
          </button>
        </div>
      </div>

      {/* Kanban Board */}
      <div class="flex-1 overflow-x-auto px-4 py-4">
        <div class="flex gap-3 h-full" style={{ 'min-width': 'max-content' }}>
          <For each={COLUMNS}>
            {(col) => {
              const colIssues = () => issuesByColumn()[col.key]
              return (
                // biome-ignore lint/a11y/noStaticElementInteractions: drag-and-drop kanban column needs pointer drop handlers
                <section
                  class="flex flex-col w-[260px] shrink-0 rounded-lg"
                  style={{
                    'background-color': 'var(--surface)',
                    border: '1px solid var(--border-subtle)',
                  }}
                  onDragOver={handleDragOver}
                  onDrop={(e) => handleDrop(e, col.key)}
                >
                  {/* Column header */}
                  <div
                    class="flex items-center gap-2 px-3 h-10 shrink-0"
                    style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
                  >
                    <div
                      class="w-2 h-2 rounded-full"
                      style={{ 'background-color': col.dotColor }}
                    />
                    <span class="text-xs font-semibold" style={{ color: 'var(--text-primary)' }}>
                      {col.name}
                    </span>
                    <span class="text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
                      {colIssues().length}
                    </span>
                  </div>

                  {/* Cards */}
                  <div
                    class="flex-1 overflow-y-auto p-2"
                    style={{ display: 'flex', 'flex-direction': 'column', gap: '6px' }}
                  >
                    <For each={colIssues()}>
                      {(issue) => (
                        <IssueCard
                          issue={issue}
                          borderColor={col.key === 'in-progress' ? '#06b6d4' : undefined}
                          onClick={() => navigateToIssue(issue.id)}
                          onDragStart={(e) => handleDragStart(e, issue.id)}
                        />
                      )}
                    </For>
                  </div>
                </section>
              )
            }}
          </For>
        </div>
      </div>
    </div>
  )
}

interface IssueCardProps {
  issue: HqIssue
  borderColor?: string
  onClick: () => void
  onDragStart: (e: DragEvent) => void
}

const IssueCard: Component<IssueCardProps> = (props) => {
  const pi = () => priorityIcon(props.issue.priority)

  return (
    <button
      type="button"
      class="flex flex-col gap-1.5 p-2.5 rounded-md cursor-pointer"
      style={{
        'background-color': 'var(--background)',
        border: `1px solid ${props.borderColor ?? 'var(--border-subtle)'}`,
        'text-align': 'left',
      }}
      draggable="true"
      onClick={props.onClick}
      onDragStart={props.onDragStart}
    >
      <div class="flex items-center gap-1.5">
        <span class="text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
          {props.issue.identifier}
        </span>
        {props.issue.isLive && (
          <div
            class="w-1.5 h-1.5 rounded-full"
            style={{ 'background-color': '#06b6d4' }}
            title="Live"
          />
        )}
      </div>
      <span class="text-xs font-medium leading-snug" style={{ color: 'var(--text-primary)' }}>
        {props.issue.title}
      </span>
      <div class="flex items-center justify-between">
        {(() => {
          const { Icon, color } = pi()
          return <Icon size={12} style={{ color }} />
        })()}
        {props.issue.assigneeName && (
          <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
            {props.issue.assigneeName}
          </span>
        )}
      </div>
    </button>
  )
}

export default HqIssues
