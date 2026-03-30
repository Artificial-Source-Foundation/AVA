import { Activity, ArrowRight, Bot, CheckCircle2, DollarSign, Gauge, Plus } from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { useHq } from '../../../stores/hq'

function formatCost(value: number): string {
  return `$${value.toFixed(2)}`
}

function formatTimeAgo(timestamp: number): string {
  const seconds = Math.max(1, Math.floor((Date.now() - timestamp) / 1000))
  if (seconds < 60) return 'just now'
  const minutes = Math.floor(seconds / 60)
  if (minutes < 60) return `${minutes}m ago`
  const hours = Math.floor(minutes / 60)
  if (hours < 24) return `${hours}h ago`
  return `${Math.floor(hours / 24)}d ago`
}

const HqDashboard: Component = () => {
  const { metrics, agents, epics, activity, openNewEpicModal, navigateTo, navigateToAgent, plan } =
    useHq()

  const activeEpic = createMemo(
    () => epics().find((epic) => epic.status === 'in-progress') || epics()[0] || null
  )
  const runningAgents = createMemo(() => agents().filter((agent) => agent.status === 'running'))
  const leadRoster = createMemo(() =>
    agents()
      .filter((agent) => agent.tier !== 'director')
      .slice()
      .sort((a, b) => Number(b.status === 'running') - Number(a.status === 'running'))
      .slice(0, 5)
  )
  const completionRatio = createMemo(() => {
    const done = metrics().issuesDone
    const total =
      done + metrics().issuesOpen + metrics().issuesInProgress + metrics().issuesInReview
    return total > 0 ? Math.round((done / total) * 100) : 0
  })

  return (
    <div class="flex h-full flex-col bg-[var(--background)]">
      <div class="flex items-center justify-between px-6 pt-6">
        <div>
          <h2 class="text-base font-semibold" style={{ color: 'var(--text-primary)' }}>
            Overview
          </h2>
          <p class="mt-1 text-[12px]" style={{ color: 'var(--text-muted)' }}>
            Live HQ mission control for the Director and active team.
          </p>
        </div>
        <button
          type="button"
          class="flex h-9 items-center gap-2 rounded-lg px-3.5 text-xs font-semibold text-white"
          style={{ 'background-color': 'var(--accent)' }}
          onClick={openNewEpicModal}
        >
          <Plus size={14} />
          New Initiative
        </button>
      </div>

      <div class="grid grid-cols-4 gap-3 px-6 pt-5">
        <MetricCard
          label="Agents Active"
          value={String(metrics().agentsActive)}
          detail={`${metrics().agentsRunning} running · ${metrics().agentsIdle} idle`}
          icon={Bot}
        />
        <MetricCard
          label="Tasks Live"
          value={String(metrics().issuesInProgress + metrics().issuesInReview)}
          detail={`${metrics().issuesOpen} still queued`}
          icon={Activity}
        />
        <MetricCard
          label="Cost"
          value={formatCost(metrics().totalCostUsd)}
          detail={`${metrics().paygAgentsTracked} PAYG runs reporting exact spend`}
          icon={DollarSign}
        />
        <MetricCard
          label="Success"
          value={`${metrics().successRate}%`}
          detail={`${metrics().issuesDone} tasks landed cleanly`}
          icon={CheckCircle2}
          accent="var(--success)"
        />
      </div>

      <div class="flex min-h-0 flex-1 gap-4 px-6 pb-6 pt-5">
        <div class="flex min-w-0 flex-1 flex-col gap-4">
          <section
            class="rounded-xl border bg-[var(--surface)] p-4"
            style={{ 'border-color': 'var(--border-subtle)' }}
          >
            <div class="flex items-center justify-between gap-3">
              <div>
                <div
                  class="text-[10px] font-semibold uppercase tracking-[0.18em]"
                  style={{ color: 'var(--text-muted)' }}
                >
                  Mission Progress
                </div>
                <div class="mt-2 text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
                  {activeEpic()?.title || 'Waiting for the next mission'}
                </div>
                <div class="mt-1 text-[12px]" style={{ color: 'var(--text-muted)' }}>
                  {activeEpic()
                    ? `${activeEpic()?.description || 'Director is coordinating the current initiative.'}`
                    : 'Start from chat or create a new initiative to see mission progress here.'}
                </div>
              </div>
              <button
                type="button"
                class="flex items-center gap-1 text-[11px] font-medium"
                style={{ color: 'var(--accent)' }}
                onClick={() => navigateTo('plan-review', 'Plan Review')}
              >
                Open plan
                <ArrowRight size={12} />
              </button>
            </div>

            <div class="mt-4 h-2 rounded-full bg-[rgba(255,255,255,0.06)]">
              <div
                class="h-full rounded-full bg-[var(--accent)] transition-transform"
                style={{ width: `${completionRatio()}%` }}
              />
            </div>

            <div
              class="mt-3 flex items-center justify-between text-[11px]"
              style={{ color: 'var(--text-muted)' }}
            >
              <span>{plan()?.phases.length || 0} phases tracked</span>
              <span>
                {metrics().issuesDone} done · {metrics().issuesInProgress} in progress
              </span>
            </div>
          </section>

          <section
            class="min-h-0 flex-1 rounded-xl border bg-[var(--surface)] p-4"
            style={{ 'border-color': 'var(--border-subtle)' }}
          >
            <div class="mb-3 flex items-center justify-between">
              <div>
                <div
                  class="text-[10px] font-semibold uppercase tracking-[0.18em]"
                  style={{ color: 'var(--text-muted)' }}
                >
                  Activity Feed
                </div>
                <div class="mt-1 text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
                  What HQ is doing now
                </div>
              </div>
              <span class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
                {activity().length} recent events
              </span>
            </div>

            <div class="flex max-h-full flex-col overflow-y-auto rounded-lg border border-[var(--border-subtle)] bg-[rgba(255,255,255,0.015)]">
              <For each={activity().slice(0, 12)}>
                {(event) => (
                  <div
                    class="flex items-center gap-3 border-b px-3 py-2.5 last:border-b-0"
                    style={{ 'border-color': 'var(--border-subtle)' }}
                  >
                    <div
                      class="mt-0.5 h-2 w-2 rounded-full"
                      style={{ 'background-color': event.color }}
                    />
                    <div class="min-w-0 flex-1">
                      <div class="truncate text-[12px]" style={{ color: 'var(--text-secondary)' }}>
                        {event.message}
                      </div>
                      <Show when={event.agentName}>
                        <div class="mt-0.5 text-[10px]" style={{ color: 'var(--text-muted)' }}>
                          {event.agentName}
                        </div>
                      </Show>
                    </div>
                    <div class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                      {formatTimeAgo(event.timestamp)}
                    </div>
                  </div>
                )}
              </For>
            </div>
          </section>
        </div>

        <div class="flex w-[340px] shrink-0 flex-col gap-4">
          <section
            class="rounded-xl border bg-[var(--surface)] p-4"
            style={{ 'border-color': 'var(--border-subtle)' }}
          >
            <div class="flex items-center justify-between">
              <div>
                <div
                  class="text-[10px] font-semibold uppercase tracking-[0.18em]"
                  style={{ color: 'var(--text-muted)' }}
                >
                  Active Team
                </div>
                <div class="mt-1 text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
                  People currently in motion
                </div>
              </div>
              <Gauge size={14} style={{ color: 'var(--text-muted)' }} />
            </div>

            <div class="mt-3 flex flex-col gap-2">
              <For each={leadRoster()}>
                {(agent) => (
                  <button
                    type="button"
                    class="flex items-start gap-3 rounded-lg border px-3 py-2.5 text-left transition-colors hover:bg-[rgba(255,255,255,0.02)]"
                    style={{ 'border-color': 'var(--border-subtle)' }}
                    onClick={() => navigateToAgent(agent.id)}
                  >
                    <div
                      class="mt-1 h-2 w-2 rounded-full"
                      style={{
                        'background-color':
                          agent.status === 'running'
                            ? 'var(--success)'
                            : agent.status === 'error'
                              ? 'var(--error)'
                              : 'var(--warning)',
                      }}
                    />
                    <div class="min-w-0 flex-1">
                      <div class="flex items-center justify-between gap-2">
                        <span
                          class="truncate text-[12px] font-semibold"
                          style={{ color: 'var(--text-primary)' }}
                        >
                          {agent.name}
                        </span>
                        <span class="text-[10px] font-mono" style={{ color: 'var(--text-muted)' }}>
                          {agent.turn ?? 0}/{agent.maxTurns ?? 0}
                        </span>
                      </div>
                      <div class="mt-1 text-[11px]" style={{ color: 'var(--text-muted)' }}>
                        {agent.currentTask || agent.role}
                      </div>
                    </div>
                  </button>
                )}
              </For>
            </div>
          </section>

          <section
            class="rounded-xl border bg-[var(--surface)] p-4"
            style={{ 'border-color': 'var(--border-subtle)' }}
          >
            <div
              class="text-[10px] font-semibold uppercase tracking-[0.18em]"
              style={{ color: 'var(--text-muted)' }}
            >
              Live Snapshot
            </div>
            <div class="mt-3 grid grid-cols-1 gap-2">
              <MiniStat
                label="Running"
                value={String(runningAgents().length)}
                tone="var(--success)"
              />
              <MiniStat
                label="Review queue"
                value={String(metrics().issuesInReview)}
                tone="var(--warning)"
              />
              <MiniStat
                label="Epics in progress"
                value={String(metrics().epicsInProgress)}
                tone="var(--accent)"
              />
            </div>
          </section>
        </div>
      </div>
    </div>
  )
}

const MetricCard: Component<{
  label: string
  value: string
  detail: string
  icon: Component<{ size?: number; class?: string }>
  accent?: string
}> = (props) => (
  <div
    class="rounded-xl border bg-[var(--surface)] px-4 py-3.5"
    style={{ 'border-color': 'var(--border-subtle)' }}
  >
    <div class="flex items-center justify-between">
      <span
        class="text-[10px] font-semibold uppercase tracking-[0.16em]"
        style={{ color: 'var(--text-muted)' }}
      >
        {props.label}
      </span>
      <props.icon size={14} class="text-[var(--text-muted)]" />
    </div>
    <div
      class="mt-3 text-[28px] font-bold leading-none"
      style={{ color: props.accent ?? 'var(--text-primary)' }}
    >
      {props.value}
    </div>
    <div class="mt-2 text-[11px]" style={{ color: 'var(--text-muted)' }}>
      {props.detail}
    </div>
  </div>
)

const MiniStat: Component<{ label: string; value: string; tone: string }> = (props) => (
  <div
    class="flex items-center justify-between rounded-lg border px-3 py-2"
    style={{
      'border-color': 'var(--border-subtle)',
      'background-color': 'rgba(255,255,255,0.015)',
    }}
  >
    <span class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
      {props.label}
    </span>
    <span class="text-[12px] font-semibold" style={{ color: props.tone }}>
      {props.value}
    </span>
  </div>
)

export default HqDashboard
