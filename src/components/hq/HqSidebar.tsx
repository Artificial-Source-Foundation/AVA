import {
  ArrowLeft,
  Bot,
  Building2,
  Crown,
  DollarSign,
  LayoutDashboard,
  ListChecks,
  Sparkles,
  Users,
} from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { useHq } from '../../stores/hq'
import { useProject } from '../../stores/project'
import type { HqPage } from '../../types/hq'

interface NavItem {
  id: HqPage
  label: string
  icon: Component<{ size?: number; class?: string }>
}

const NAV_ITEMS: NavItem[] = [
  { id: 'director-chat', label: 'Chat', icon: Crown },
  { id: 'dashboard', label: 'Overview', icon: LayoutDashboard },
  { id: 'team', label: 'Team', icon: Users },
]

function metricValue(value: number, suffix = ''): string {
  return `${value}${suffix}`
}

function statusColor(status: string): string {
  if (status === 'running' || status === 'active') return 'var(--success)'
  if (status === 'paused') return 'var(--warning)'
  if (status === 'error') return 'var(--error)'
  return 'var(--text-muted)'
}

export const HqSidebar: Component = () => {
  const { hqPage, navigateTo, navigateToAgent, toggleHqMode, agents, epics, plan, metrics } =
    useHq()
  const { currentProject } = useProject()

  const projectLabel = () => {
    const project = currentProject()
    if (!project) return 'No workspace selected'
    return project.name || project.directory.split('/').filter(Boolean).pop() || 'Workspace'
  }

  const mission = createMemo(() => {
    const activeEpic =
      epics().find((epic) => epic.status === 'in-progress') ||
      epics().find((epic) => epic.status === 'planning') ||
      epics()[0]
    if (activeEpic) {
      return {
        title: activeEpic.title,
        subtitle:
          activeEpic.status === 'planning'
            ? 'Director is shaping the next plan.'
            : `${activeEpic.progress}% complete across active HQ work.`,
      }
    }

    const currentPlan = plan()
    if (currentPlan) {
      return {
        title: currentPlan.title,
        subtitle: `${currentPlan.phases.length} phases currently tracked in review/execution.`,
      }
    }

    return {
      title: 'Ready for your next initiative',
      subtitle: 'Message the Director to start a new mission.',
    }
  })

  const visibleAgents = createMemo(() =>
    agents()
      .filter((agent) => agent.tier !== 'director')
      .slice()
      .sort((a, b) => {
        const rank = (status: string) => {
          if (status === 'running') return 0
          if (status === 'active') return 1
          if (status === 'paused') return 2
          if (status === 'error') return 3
          return 4
        }
        return rank(a.status) - rank(b.status)
      })
      .slice(0, 6)
  )

  const planStatus = createMemo(() => {
    const currentPlan = plan()
    if (!currentPlan) return null
    if (currentPlan.status === 'awaiting-approval') {
      return { label: 'Plan awaiting review', color: 'var(--warning)' }
    }
    if (currentPlan.status === 'approved') {
      return { label: 'Plan approved', color: 'var(--success)' }
    }
    if (currentPlan.status === 'executing') {
      return { label: 'Plan executing', color: 'var(--accent)' }
    }
    return { label: 'Plan needs revision', color: 'var(--error)' }
  })

  return (
    <aside
      class="flex h-full w-[260px] shrink-0 flex-col border-r"
      style={{
        'background-color': '#111114',
        'border-color': 'var(--border-subtle)',
      }}
    >
      <div class="flex h-[52px] items-center justify-between px-4">
        <div class="flex items-center gap-2.5">
          <div
            class="flex h-8 w-8 items-center justify-center rounded-xl"
            style={{ background: 'rgba(245, 166, 35, 0.12)' }}
          >
            <Building2 size={16} style={{ color: 'var(--warning)' }} />
          </div>
          <div class="flex flex-col gap-0.5">
            <span class="text-xs font-semibold" style={{ color: 'var(--text-primary)' }}>
              Director
            </span>
            <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
              {projectLabel()}
            </span>
          </div>
        </div>
        <Sparkles size={14} style={{ color: 'var(--text-muted)' }} />
      </div>

      <div class="px-2 pb-2">
        <div class="flex h-8 items-center gap-0.5 rounded-lg bg-[rgba(255,255,255,0.03)] p-1">
          <For each={NAV_ITEMS}>
            {(item) => {
              const active = () => hqPage() === item.id
              const highlighted = () =>
                active() || (hqPage() === 'plan-review' && item.id === 'director-chat')
              return (
                <button
                  type="button"
                  class="flex h-full flex-1 items-center justify-center gap-1.5 rounded-md text-[11px] font-medium transition-colors"
                  style={{
                    'background-color': highlighted() ? 'rgba(255,255,255,0.08)' : 'transparent',
                    color: highlighted() ? 'var(--text-primary)' : 'var(--text-muted)',
                  }}
                  onClick={() => navigateTo(item.id, item.label)}
                >
                  <item.icon size={13} />
                  {item.label}
                </button>
              )
            }}
          </For>
        </div>
      </div>

      <div class="h-px w-full bg-[var(--border-subtle)] opacity-60" />

      <div class="flex flex-col gap-1.5 px-3 py-3">
        <span
          class="text-[9px] font-semibold uppercase tracking-[0.18em]"
          style={{ color: 'var(--text-muted)' }}
        >
          Current Mission
        </span>
        <div
          class="rounded-xl border px-3 py-3"
          style={{
            'background-color': '#0f0f12',
            'border-color': 'rgba(245, 166, 35, 0.14)',
          }}
        >
          <div class="flex items-start gap-2.5">
            <Crown size={14} style={{ color: 'var(--warning)', 'margin-top': '2px' }} />
            <div class="min-w-0 flex-1">
              <div class="text-[12px] font-semibold" style={{ color: 'var(--text-primary)' }}>
                {mission().title}
              </div>
              <div class="mt-1 text-[11px] leading-relaxed" style={{ color: 'var(--text-muted)' }}>
                {mission().subtitle}
              </div>
              <Show when={planStatus()}>
                {(status) => (
                  <div class="mt-2 text-[10px] font-medium" style={{ color: status().color }}>
                    {status().label}
                  </div>
                )}
              </Show>
            </div>
          </div>
        </div>
      </div>

      <div class="h-px w-full bg-[var(--border-subtle)] opacity-60" />

      <div class="flex items-center justify-between px-3 pb-1 pt-3">
        <span
          class="text-[9px] font-semibold uppercase tracking-[0.18em]"
          style={{ color: 'var(--text-muted)' }}
        >
          Team
        </span>
        <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
          {visibleAgents().length} visible
        </span>
      </div>

      <div class="flex min-h-0 flex-1 flex-col gap-1 overflow-y-auto px-2 pb-3">
        <For each={visibleAgents()}>
          {(agent) => (
            <button
              type="button"
              class="flex items-center gap-2 rounded-lg px-2.5 py-2 text-left transition-colors hover:bg-[rgba(255,255,255,0.03)]"
              onClick={() => navigateToAgent(agent.id)}
            >
              <div
                class="h-2 w-2 rounded-full"
                style={{ 'background-color': statusColor(agent.status) }}
              />
              <div class="min-w-0 flex-1">
                <div
                  class="truncate text-[11px] font-medium"
                  style={{ color: 'var(--text-primary)' }}
                >
                  {agent.name}
                </div>
                <div class="truncate text-[10px]" style={{ color: statusColor(agent.status) }}>
                  {agent.currentTask || agent.status}
                </div>
              </div>
              <div class="text-[9px] font-mono" style={{ color: 'var(--text-muted)' }}>
                {agent.turn ?? 0}/{agent.maxTurns ?? 0}
              </div>
            </button>
          )}
        </For>
      </div>

      <div class="h-px w-full bg-[var(--border-subtle)] opacity-60" />

      <div class="grid grid-cols-4 gap-2 px-4 py-3">
        <Metric label="Act" value={metricValue(metrics().agentsRunning)} icon={Bot} />
        <Metric label="Task" value={metricValue(metrics().issuesInProgress)} icon={ListChecks} />
        <Metric label="Done" value={metricValue(metrics().successRate, '%')} icon={Sparkles} />
        <Metric label="$" value={metrics().totalCostUsd.toFixed(2)} icon={DollarSign} />
      </div>

      <button
        type="button"
        class="mx-3 mb-3 flex h-9 items-center gap-2 rounded-lg px-3 text-left text-[11px] font-medium transition-colors hover:bg-[rgba(255,255,255,0.04)]"
        style={{ color: 'var(--text-muted)' }}
        onClick={toggleHqMode}
      >
        <ArrowLeft size={13} />
        Back to Chat
      </button>
    </aside>
  )
}

const Metric: Component<{
  label: string
  value: string
  icon: Component<{ size?: number; class?: string }>
}> = (props) => (
  <div class="flex flex-col items-center gap-1">
    <props.icon size={12} class="text-[var(--text-muted)]" />
    <span class="text-[10px] font-semibold" style={{ color: 'var(--text-primary)' }}>
      {props.value}
    </span>
    <span class="text-[9px] uppercase tracking-[0.12em]" style={{ color: 'var(--text-muted)' }}>
      {props.label}
    </span>
  </div>
)
