import { ArrowLeft, FileText } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { useHq } from '../../../stores/hq'
import type { EpicStatus, KanbanColumn } from '../../../types/hq'

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

function issueStatusDot(status: KanbanColumn): string {
  switch (status) {
    case 'backlog':
      return '#3b82f6'
    case 'in-progress':
      return '#06b6d4'
    case 'review':
      return '#eab308'
    case 'done':
      return 'var(--success)'
  }
}

function issueStatusLabel(status: KanbanColumn): string {
  switch (status) {
    case 'backlog':
      return 'Backlog'
    case 'in-progress':
      return 'In Progress'
    case 'review':
      return 'Review'
    case 'done':
      return 'Done'
  }
}

const HqEpicDetail: Component = () => {
  const { selectedEpic, issues, navigateBack, navigateToIssue, navigateTo } = useHq()

  const epicIssues = () => {
    const epic = selectedEpic()
    if (!epic) return []
    return issues().filter((i) => epic.issueIds.includes(i.id))
  }

  const phasesTotal = () => {
    const epic = selectedEpic()
    if (!epic) return { completed: 0, total: 0 }
    const all = epicIssues()
    const done = all.filter((i) => i.status === 'done').length
    return { completed: done, total: all.length }
  }

  const workingAgents = () => {
    return epicIssues().filter((i) => i.isLive).length
  }

  return (
    <div class="flex flex-col h-full" style={{ 'background-color': 'var(--background)' }}>
      <Show
        when={selectedEpic()}
        fallback={
          <div
            class="flex items-center justify-center h-full"
            style={{ color: 'var(--text-muted)' }}
          >
            No epic selected
          </div>
        }
      >
        {(epic) => {
          const badge = statusBadge(epic().status)
          return (
            <>
              {/* Header */}
              <div
                class="flex items-center justify-between shrink-0 px-6 h-14"
                style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
              >
                <div class="flex items-center gap-3">
                  <button
                    type="button"
                    class="flex items-center justify-center w-7 h-7 rounded-md"
                    style={{
                      'background-color': 'var(--surface)',
                      border: '1px solid var(--border-subtle)',
                    }}
                    onClick={navigateBack}
                  >
                    <ArrowLeft size={14} style={{ color: 'var(--text-secondary)' }} />
                  </button>
                  <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
                    {epic().title}
                  </span>
                  <span
                    class="text-[9px] font-semibold px-1.5 py-0.5 rounded"
                    style={{ color: badge.color, 'background-color': `${badge.color}22` }}
                  >
                    {badge.label}
                  </span>
                </div>
                <Show when={epic().planId}>
                  <button
                    type="button"
                    class="flex items-center gap-1.5 h-8 px-3.5 rounded-md text-xs font-semibold"
                    style={{
                      'background-color': 'var(--surface)',
                      border: '1px solid var(--border-subtle)',
                      color: 'var(--text-secondary)',
                    }}
                    onClick={() => navigateTo('plan-review', 'Plan Review')}
                  >
                    <FileText size={14} />
                    View Plan
                  </button>
                </Show>
              </div>

              {/* Metric cards */}
              <div class="grid grid-cols-3 gap-3 px-6 py-4">
                <div
                  class="flex flex-col gap-2 p-4 rounded-lg"
                  style={{
                    'background-color': 'var(--surface)',
                    border: '1px solid var(--border-subtle)',
                  }}
                >
                  <span class="text-[11px] font-medium" style={{ color: 'var(--text-muted)' }}>
                    Overall Progress
                  </span>
                  <span class="text-2xl font-bold" style={{ color: 'var(--text-primary)' }}>
                    {epic().progress}%
                  </span>
                  <div
                    class="w-full h-1.5 rounded-full overflow-hidden"
                    style={{ 'background-color': 'var(--background)' }}
                  >
                    <div
                      class="h-full rounded-full"
                      style={{ width: `${epic().progress}%`, 'background-color': 'var(--accent)' }}
                    />
                  </div>
                </div>
                <div
                  class="flex flex-col gap-2 p-4 rounded-lg"
                  style={{
                    'background-color': 'var(--surface)',
                    border: '1px solid var(--border-subtle)',
                  }}
                >
                  <span class="text-[11px] font-medium" style={{ color: 'var(--text-muted)' }}>
                    Agents Working
                  </span>
                  <span class="text-2xl font-bold" style={{ color: 'var(--text-primary)' }}>
                    {workingAgents()}
                  </span>
                </div>
                <div
                  class="flex flex-col gap-2 p-4 rounded-lg"
                  style={{
                    'background-color': 'var(--surface)',
                    border: '1px solid var(--border-subtle)',
                  }}
                >
                  <span class="text-[11px] font-medium" style={{ color: 'var(--text-muted)' }}>
                    Phases
                  </span>
                  <span class="text-2xl font-bold" style={{ color: 'var(--text-primary)' }}>
                    {phasesTotal().completed}/{phasesTotal().total}
                  </span>
                </div>
              </div>

              {/* Issue list */}
              <div class="flex-1 overflow-y-auto px-6">
                <For each={epicIssues()}>
                  {(issue) => (
                    <button
                      type="button"
                      class="flex items-center gap-3 h-12 cursor-pointer"
                      style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
                      onClick={() => navigateToIssue(issue.id)}
                    >
                      <div
                        class="w-2 h-2 rounded-full shrink-0"
                        style={{ 'background-color': issueStatusDot(issue.status) }}
                      />
                      <span
                        class="text-xs font-mono shrink-0 w-12"
                        style={{ color: 'var(--text-muted)' }}
                      >
                        {issue.identifier}
                      </span>
                      <span
                        class="text-sm flex-1 truncate"
                        style={{ color: 'var(--text-primary)' }}
                      >
                        {issue.title}
                      </span>
                      <Show when={issue.assigneeName}>
                        <span class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
                          {issue.assigneeName}
                        </span>
                      </Show>
                      <span
                        class="text-[9px] font-semibold px-1.5 py-0.5 rounded shrink-0"
                        style={{
                          color: issueStatusDot(issue.status),
                          'background-color': `${issueStatusDot(issue.status)}22`,
                        }}
                      >
                        {issueStatusLabel(issue.status)}
                      </span>
                    </button>
                  )}
                </For>
              </div>
            </>
          )
        }}
      </Show>
    </div>
  )
}

export default HqEpicDetail
