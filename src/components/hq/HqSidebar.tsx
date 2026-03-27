import { Crown, KanbanSquare, Layers, LayoutDashboard, ListChecks, Network } from 'lucide-solid'
import { type Component, For } from 'solid-js'
import { useHq } from '../../stores/hq'
import { useProject } from '../../stores/project'
import type { HqPage } from '../../types/hq'

interface NavItem {
  id: HqPage
  label: string
  icon: Component<{ size?: number; class?: string }>
}

const NAV_ITEMS: NavItem[] = [
  { id: 'dashboard', label: 'Dashboard', icon: LayoutDashboard },
  { id: 'director-chat', label: 'Director', icon: Crown },
  { id: 'epics', label: 'Epics', icon: Layers },
  { id: 'issues', label: 'Issues', icon: KanbanSquare },
  { id: 'org-chart', label: 'Org Chart', icon: Network },
  { id: 'plan-review', label: 'Plan', icon: ListChecks },
]

export const HqSidebar: Component = () => {
  const { hqPage, navigateTo, runningAgents } = useHq()
  const { currentProject } = useProject()

  const projectLabel = () => {
    const project = currentProject()
    if (!project) return 'No workspace'
    return project.name || project.directory.split('/').filter(Boolean).pop() || 'Workspace'
  }

  return (
    <div
      class="flex flex-col h-full w-[200px] shrink-0 border-r"
      style={{
        'background-color': 'var(--sidebar-background)',
        'border-color': 'var(--border-subtle)',
      }}
    >
      {/* Header */}
      <div
        class="flex items-center gap-2 px-4 py-3 border-b"
        style={{ 'border-color': 'var(--border-subtle)' }}
      >
        <span
          class="text-xs font-bold tracking-wider px-2 py-0.5 rounded"
          style={{
            color: 'var(--accent)',
            'background-color': 'var(--accent-subtle)',
          }}
        >
          HQ
        </span>
        <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
          {projectLabel()}
        </span>
      </div>

      {/* Nav Items */}
      <nav class="flex flex-col gap-0.5 p-2 flex-1">
        <For each={NAV_ITEMS}>
          {(item) => {
            const isActive = () => {
              const page = hqPage()
              if (item.id === 'epics') return page === 'epics' || page === 'epic-detail'
              if (item.id === 'issues') return page === 'issues' || page === 'issue-detail'
              if (item.id === 'org-chart') return page === 'org-chart' || page === 'agent-detail'
              return page === item.id
            }

            return (
              <button
                type="button"
                class="flex items-center gap-2.5 w-full h-8 px-2.5 rounded-md text-left transition-colors"
                style={{
                  color: isActive() ? 'var(--text-primary)' : 'var(--text-secondary)',
                  'background-color': isActive() ? 'var(--accent-subtle)' : 'transparent',
                  'font-size': '13px',
                  'font-weight': isActive() ? '500' : '400',
                }}
                onClick={() => navigateTo(item.id, item.label)}
              >
                <item.icon size={16} class={isActive() ? 'text-violet-400' : 'text-zinc-500'} />
                {item.label}
              </button>
            )
          }}
        </For>
      </nav>

      {/* Agent Status Footer */}
      <div class="px-3 py-3 border-t" style={{ 'border-color': 'var(--border-subtle)' }}>
        <div class="flex items-center gap-2">
          <div
            class="w-2 h-2 rounded-full"
            style={{
              'background-color':
                runningAgents().length > 0 ? 'var(--success)' : 'var(--text-muted)',
            }}
          />
          <span class="text-xs" style={{ color: 'var(--text-tertiary)' }}>
            {runningAgents().length > 0 ? `${runningAgents().length} agents running` : 'HQ idle'}
          </span>
        </div>
      </div>
    </div>
  )
}
