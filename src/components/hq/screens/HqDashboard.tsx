import { Bot, CircleDot, Layers, LayoutDashboard, Plus, ShieldCheck } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { useHq } from '../../../stores/hq'

function formatCostUsd(value: number): string {
  if (value < 0.01) return '$0.00'
  return `$${value.toFixed(2)}`
}

const HqDashboard: Component = () => {
  const { metrics, runningAgents, activity, navigateToAgent, openNewEpicModal, hqSettings } =
    useHq()

  const hqRunning = () => runningAgents().length > 0

  return (
    <div class="flex flex-col h-full overflow-y-auto" style={{ padding: '24px', gap: '20px' }}>
      {/* Header */}
      <div class="flex items-center justify-between">
        <div class="flex items-center gap-2.5">
          <LayoutDashboard size={20} class="text-zinc-500" />
          <span class="text-lg font-semibold" style={{ color: 'var(--text-primary)' }}>
            Dashboard
          </span>
        </div>
        <div class="flex items-center gap-2">
          <div
            class="w-2 h-2 rounded-full"
            style={{ 'background-color': hqRunning() ? 'var(--success)' : 'var(--text-muted)' }}
          />
          <span
            class="text-xs font-medium"
            style={{ color: hqRunning() ? 'var(--success)' : 'var(--text-muted)' }}
          >
            {hqRunning() ? 'HQ Running' : 'HQ Idle'}
          </span>
          <button
            type="button"
            class="flex items-center gap-1.5 h-8 px-3.5 rounded-md text-xs font-semibold"
            style={{
              'background-color': 'var(--accent)',
              color: 'white',
            }}
            onClick={openNewEpicModal}
          >
            <Plus size={14} />
            New Epic
          </button>
        </div>
      </div>

      {/* Metric Cards */}
      <div class="grid grid-cols-4 gap-3">
        <MetricCard
          label="Agents Active"
          value={String(metrics().agentsActive)}
          sub={`${metrics().agentsRunning} running, ${metrics().agentsIdle} idle`}
          icon={Bot}
        />
        <MetricCard
          label="Tasks In Progress"
          value={String(metrics().issuesInProgress)}
          sub={`${metrics().issuesOpen} open, ${metrics().issuesInReview} in review`}
          icon={CircleDot}
        />
        <MetricCard
          label={hqSettings().showCosts ? 'Estimated Cost' : 'Epics In Progress'}
          value={
            hqSettings().showCosts
              ? formatCostUsd(metrics().totalCostUsd)
              : String(metrics().epicsInProgress)
          }
          sub={
            hqSettings().showCosts
              ? metrics().paygAgentsTracked > 0
                ? `${metrics().paygAgentsTracked} PAYG agent runs reported exact spend`
                : 'No PAYG cost telemetry reported yet'
              : `${metrics().issuesDone} done, ${metrics().issuesOpen} still open`
          }
          icon={Layers}
        />
        <MetricCard
          label="Success Rate"
          value={`${metrics().successRate}%`}
          sub={`${metrics().issuesDone}/${metrics().issuesOpen} tasks succeeded`}
          icon={ShieldCheck}
          valueColor="var(--success)"
        />
      </div>

      {/* Active Agents */}
      <div class="flex flex-col gap-3">
        <span class="text-sm font-semibold" style={{ color: 'var(--text-secondary)' }}>
          Active Agents
        </span>
        <div class="grid grid-cols-2 gap-3">
          <For each={runningAgents()}>
            {(agent) => (
              <button
                type="button"
                class="flex flex-col gap-2.5 p-3.5 rounded-lg cursor-pointer"
                style={{
                  'background-color': 'var(--surface)',
                  border: '1px solid var(--border-subtle)',
                  'text-align': 'left',
                }}
                onClick={() => navigateToAgent(agent.id)}
              >
                <div class="flex items-center gap-2">
                  <div class="w-2 h-2 rounded-full" style={{ 'background-color': '#06b6d4' }} />
                  <span class="text-xs font-semibold" style={{ color: 'var(--text-primary)' }}>
                    {agent.name} ({agent.model})
                  </span>
                  <span
                    class="text-[9px] font-semibold px-1.5 py-0.5 rounded"
                    style={{
                      color: '#06b6d4',
                      'background-color': 'rgba(6,182,212,0.15)',
                    }}
                  >
                    running
                  </span>
                </div>
                <Show when={agent.currentTask}>
                  <span class="text-[11px] leading-snug" style={{ color: 'var(--text-muted)' }}>
                    {agent.currentTask}
                  </span>
                </Show>
                <div class="border-t pt-2" style={{ 'border-color': 'var(--border-subtle)' }}>
                  <div class="flex flex-col gap-1">
                    <For each={agent.transcript.slice(-3)}>
                      {(entry) => (
                        <span
                          class="text-[10px] font-mono"
                          style={{
                            color:
                              entry.toolName === 'edit' || entry.toolName === 'write'
                                ? 'var(--success)'
                                : entry.toolName === 'bash'
                                  ? 'var(--warning)'
                                  : 'var(--text-muted)',
                          }}
                        >
                          {'> '}
                          {entry.toolName ?? ''} {entry.toolPath ?? entry.content.slice(0, 40)}
                        </span>
                      )}
                    </For>
                  </div>
                </div>
              </button>
            )}
          </For>
        </div>
      </div>

      {/* Activity Feed */}
      <div class="flex flex-col gap-2.5">
        <span class="text-sm font-semibold" style={{ color: 'var(--text-secondary)' }}>
          Recent Activity
        </span>
        <div class="flex flex-col">
          <For each={activity()}>
            {(event) => (
              <div
                class="flex items-center gap-2 h-9 px-2 border-b"
                style={{ 'border-color': 'var(--border-subtle)' }}
              >
                <div
                  class="w-1.5 h-1.5 rounded-full shrink-0"
                  style={{ 'background-color': event.color }}
                />
                <span class="text-xs flex-1" style={{ color: 'var(--text-secondary)' }}>
                  {event.message}
                </span>
                <span class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
                  {formatTimeAgo(event.timestamp)}
                </span>
              </div>
            )}
          </For>
        </div>
      </div>
    </div>
  )
}

// ── Helpers ───────────────────────────────────────────────────────────

interface MetricCardProps {
  label: string
  value: string
  sub: string
  icon: Component<{ size?: number; class?: string }>
  valueColor?: string
}

const MetricCard: Component<MetricCardProps> = (props) => (
  <div
    class="flex flex-col gap-2.5 p-4 rounded-lg"
    style={{
      'background-color': 'var(--surface)',
      border: '1px solid var(--border-subtle)',
    }}
  >
    <div class="flex items-center justify-between">
      <span class="text-[11px] font-medium" style={{ color: 'var(--text-muted)' }}>
        {props.label}
      </span>
      <props.icon size={16} class="text-zinc-600" />
    </div>
    <span
      class="text-[28px] font-bold leading-none"
      style={{ color: props.valueColor ?? 'var(--text-primary)' }}
    >
      {props.value}
    </span>
    <span class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
      {props.sub}
    </span>
  </div>
)

function formatTimeAgo(timestamp: number): string {
  const seconds = Math.floor((Date.now() - timestamp) / 1000)
  if (seconds < 60) return 'just now'
  const minutes = Math.floor(seconds / 60)
  if (minutes < 60) return `${minutes}m ago`
  const hours = Math.floor(minutes / 60)
  return `${hours}h ago`
}

export default HqDashboard
