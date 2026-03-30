import { Crown, Square, Users } from 'lucide-solid'
import type { JSX } from 'solid-js'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useTeam } from '../../stores/team'
import { DOMAIN_COLORS } from '../../stores/team-helpers'
import type { TeamGroup, TeamMember } from '../../types/team'
import { DelegationLog } from './team/DelegationLog'
import { TeamMetrics } from './team/TeamMetrics'

// ============================================================================
// Sub-components
// ============================================================================

/** Status badge with domain-aware coloring */
function StatusBadge(props: { status: TeamMember['status']; domainColor?: string }): JSX.Element {
  const color = (): string => {
    switch (props.status) {
      case 'working':
        return props.domainColor ?? 'var(--accent)'
      case 'done':
        return 'var(--success)'
      case 'error':
        return 'var(--error)'
      case 'reporting':
      case 'idle':
        return 'var(--warning)'
      default:
        return 'var(--text-muted)'
    }
  }

  return (
    <span
      class="text-[9px] font-medium px-1.5 py-0.5 rounded"
      style={{
        color: color(),
        background: `${color()}20`,
      }}
    >
      {props.status}
    </span>
  )
}

/** Worker sub-card: #18181B bg, rounded-8, 6px padding */
const WorkerCard: Component<{ member: TeamMember; domainColor: string; onClick: () => void }> = (
  props
) => {
  return (
    <button
      type="button"
      onClick={() => props.onClick()}
      class="flex items-center gap-2 w-full text-left rounded-lg bg-[var(--gray-3)] px-2.5 py-1.5 hover:bg-[var(--surface-raised)] transition-colors cursor-pointer"
      aria-label={`View ${props.member.name} details`}
    >
      <span
        class="w-1.5 h-1.5 rounded-full flex-shrink-0"
        style={{ background: props.domainColor }}
      />
      <span class="text-[10px] text-[var(--gray-9)] truncate flex-1">{props.member.name}</span>
      <span class="text-[10px] text-[var(--gray-6)] flex-shrink-0 tabular-nums">
        {props.member.toolCalls.length > 0
          ? `${props.member.toolCalls.filter((tc) => tc.status === 'success').length}/${props.member.toolCalls.length}`
          : props.member.status}
      </span>
    </button>
  )
}

/** Lead card with domain-colored dot, status badge, stop button, progress bar, and workers */
const TeamGroupCard: Component<{
  group: TeamGroup
  onSelectMember: (id: string) => void
  onStopLead: (id: string) => void
}> = (props) => {
  const [expanded, setExpanded] = createSignal(true)
  const workers = () => props.group.members
  const domainColor = () => DOMAIN_COLORS[props.group.lead.domain]

  return (
    <div class="space-y-2">
      {/* Lead header row */}
      <div class="flex items-center justify-between w-full">
        <button
          type="button"
          onClick={() => {
            setExpanded(!expanded())
            props.onSelectMember(props.group.lead.id)
          }}
          class="flex items-center gap-2 min-w-0 text-left hover:opacity-80 transition-opacity"
        >
          <span class="w-2 h-2 rounded-full flex-shrink-0" style={{ background: domainColor() }} />
          <span class="text-[12px] font-semibold text-[var(--text-primary)] truncate">
            {props.group.lead.name}
          </span>
        </button>

        <div class="flex items-center gap-2 flex-shrink-0">
          <StatusBadge status={props.group.lead.status} domainColor={domainColor()} />

          {/* Stop button for working leads */}
          <Show when={props.group.lead.status === 'working'}>
            <button
              type="button"
              onClick={(e) => {
                e.stopPropagation()
                props.onStopLead(props.group.lead.id)
              }}
              class="flex items-center gap-1 px-1.5 py-0.5 rounded text-[9px] font-medium text-[var(--error)] transition-colors"
              style={{ background: '#EF444420' }}
              title={`Stop ${props.group.lead.name}`}
              aria-label={`Stop ${props.group.lead.name}`}
            >
              <Square class="w-2 h-2" />
              stop
            </button>
          </Show>
        </div>
      </div>

      {/* Progress bar: 3px, domain color fill on var(--gray-3) track */}
      <div class="w-full h-[3px] rounded-sm bg-[var(--gray-3)]">
        <div
          class="h-full w-full origin-left rounded-sm transition-transform duration-300"
          style={{
            transform: `scaleX(${Math.round(props.group.progress * 100) / 100})`,
            background: props.group.lead.status === 'done' ? 'var(--success)' : domainColor(),
          }}
        />
      </div>

      {/* Task description */}
      <Show when={props.group.lead.task}>
        <p class="text-[10px] text-[var(--gray-7)]">{props.group.lead.task}</p>
      </Show>

      {/* Worker sub-cards */}
      <Show when={expanded() && workers().length > 0}>
        <div class="space-y-1">
          <For each={workers()}>
            {(worker) => (
              <WorkerCard
                member={worker}
                domainColor={domainColor()}
                onClick={() => props.onSelectMember(worker.id)}
              />
            )}
          </For>
        </div>
      </Show>
    </div>
  )
}

// ============================================================================
// Main Component
// ============================================================================

export interface TeamPanelProps {
  onStopAgent?: (memberId: string) => void
  onStopAll?: () => void
}

export const TeamPanel: Component<TeamPanelProps> = (props) => {
  const team = useTeam()

  const director = () => team.teamLead()
  const groups = () => team.teamGroups()
  const workingCount = () => team.allMembers().filter((m) => m.status === 'working').length

  const handleStopLead = (leadId: string) => {
    props.onStopAgent?.(leadId)
  }

  const handleStopAll = () => {
    props.onStopAll?.()
  }

  const hasWorkingAgents = () => team.allMembers().some((m) => m.status === 'working')

  return (
    <div class="flex flex-col h-full" style={{ background: 'var(--background)' }}>
      {/* Header: "HQ Team" with users icon + "N working" badge */}
      <div
        class="flex items-center justify-between px-4 py-3.5"
        style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
      >
        <div class="flex items-center gap-2">
          <Users class="w-4 h-4" style={{ color: 'var(--accent)' }} />
          <span class="text-[13px] font-semibold text-[var(--text-primary)]">HQ Team</span>
        </div>
        <div class="flex items-center gap-2">
          <Show when={workingCount() > 0}>
            <span
              class="text-[10px] font-medium px-2 py-0.5 rounded-md"
              style={{ color: 'var(--accent)', background: 'var(--accent-subtle)' }}
            >
              {workingCount()} working
            </span>
          </Show>
          <Show when={hasWorkingAgents()}>
            <button
              type="button"
              onClick={handleStopAll}
              class="flex items-center gap-1 px-1.5 py-0.5 rounded text-[9px] font-medium text-[var(--error)] hover:opacity-80 transition-opacity"
              style={{ background: '#EF444420' }}
              title="Stop all running agents"
              aria-label="Stop all running agents"
            >
              <Square class="w-2.5 h-2.5" />
              Stop All
            </button>
          </Show>
        </div>
      </div>

      {/* Director section */}
      <Show when={director()}>
        {(dir) => (
          <div
            class="flex flex-col gap-1.5 px-4 py-3"
            style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
          >
            <div class="flex items-center gap-2">
              <Crown class="w-3.5 h-3.5" style={{ color: '#F59E0B' }} />
              <span class="text-[12px] font-semibold text-[var(--text-primary)]">Director</span>
            </div>
            <p class="text-[11px] text-[var(--text-muted)]">{dir().task ?? 'Orchestrating team'}</p>
            <span class="text-[10px] font-['JetBrains_Mono',monospace] text-[var(--gray-7)]">
              {dir().model !== 'unknown' ? dir().model : 'claude-opus'} &middot; Budget: $5.00
            </span>
          </div>
        )}
      </Show>

      {/* Scrollable content: team groups separated by dividers */}
      <div class="flex-1 overflow-y-auto scrollbar-none">
        <Show
          when={groups().length > 0}
          fallback={
            <Show when={director()}>
              <div class="text-[11px] text-[var(--text-muted)] px-4 py-3">
                Waiting for assignments...
              </div>
            </Show>
          }
        >
          <For each={groups()}>
            {(group, index) => (
              <>
                <Show when={index() > 0}>
                  <div class="w-full h-px" style={{ background: 'var(--border-subtle)' }} />
                </Show>
                <div class="px-4 py-3">
                  <TeamGroupCard
                    group={group}
                    onSelectMember={(id) => team.setSelectedMemberId(id)}
                    onStopLead={handleStopLead}
                  />
                </div>
              </>
            )}
          </For>
        </Show>

        {/* Empty state */}
        <Show when={!director()}>
          <div class="flex flex-col items-center justify-center py-8 text-center">
            <Crown class="w-8 h-8 text-[var(--text-muted)] opacity-30 mb-2" />
            <p class="text-[11px] text-[var(--text-muted)]">
              No team active. Enable Team mode to start.
            </p>
          </div>
        </Show>
      </div>

      {/* Delegation log (collapsible) */}
      <DelegationLog events={team.delegationLog()} />

      {/* Metrics footer */}
      <TeamMetrics members={team.allMembers()} delegations={team.delegationLog()} />
    </div>
  )
}
