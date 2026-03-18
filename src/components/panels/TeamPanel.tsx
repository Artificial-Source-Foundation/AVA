import { ChevronDown, ChevronRight, Crown, Loader2, Octagon, Square } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useTeam } from '../../stores/team'
import type { TeamGroup, TeamMember } from '../../types/team'
import { DelegationLog } from './team/DelegationLog'
import { TeamMetrics } from './team/TeamMetrics'

// ============================================================================
// Sub-components
// ============================================================================

/** Status dot color based on TeamStatus */
function statusDotClass(status: TeamMember['status']): string {
  switch (status) {
    case 'working':
      return 'bg-[var(--accent)] animate-pulse'
    case 'done':
      return 'bg-[var(--success)]'
    case 'error':
      return 'bg-[var(--error)]'
    case 'reporting':
      return 'bg-[var(--warning)]'
    default:
      return 'bg-[var(--text-muted)]'
  }
}

/** Worker card inside a team group */
const WorkerCard: Component<{ member: TeamMember; onClick: () => void }> = (props) => {
  return (
    <button
      type="button"
      onClick={props.onClick}
      class="flex items-center gap-2 w-full text-left px-2 py-1 rounded-[var(--radius-md)] hover:bg-[var(--alpha-white-3)] transition-colors cursor-pointer"
    >
      <span
        class={`w-1.5 h-1.5 rounded-full flex-shrink-0 ${statusDotClass(props.member.status)}`}
      />
      <span class="font-[var(--font-ui-mono)] text-[10px] text-[var(--text-secondary)] truncate flex-1">
        {props.member.name}
      </span>
      <span class="font-[var(--font-ui-mono)] text-[9px] text-[var(--text-muted)] flex-shrink-0">
        {props.member.toolCalls.length > 0
          ? `${props.member.toolCalls.length} tools`
          : props.member.status}
      </span>
    </button>
  )
}

/** Team group card — one lead + its workers */
const TeamGroupCard: Component<{
  group: TeamGroup
  onSelectMember: (id: string) => void
  onStopLead: (id: string) => void
}> = (props) => {
  const [expanded, setExpanded] = createSignal(true)
  const workers = () => props.group.members

  return (
    <div class="rounded-[var(--radius-lg)] border border-[var(--border-default)] bg-[var(--surface)]">
      {/* Lead header */}
      <div class="flex items-center gap-2 px-3 py-2">
        <button
          type="button"
          onClick={() => setExpanded(!expanded())}
          class="flex items-center gap-2 flex-1 min-w-0 text-left hover:opacity-80 transition-opacity"
        >
          <span
            class="w-2 h-2 rounded-[2px] flex-shrink-0"
            style={{ background: props.group.config.color }}
          />
          <span class="font-[var(--font-ui-mono)] text-[11px] font-semibold text-[var(--text-primary)] truncate">
            {props.group.lead.name}
          </span>
          <span
            class={`w-1.5 h-1.5 rounded-full flex-shrink-0 ${statusDotClass(props.group.lead.status)}`}
          />
          <Show when={workers().length > 0}>
            <span class="font-[var(--font-ui-mono)] text-[9px] text-[var(--text-muted)] flex-shrink-0">
              {workers().length}
            </span>
          </Show>
          <Show
            when={expanded()}
            fallback={<ChevronRight class="w-3 h-3 text-[var(--text-muted)] flex-shrink-0" />}
          >
            <ChevronDown class="w-3 h-3 text-[var(--text-muted)] flex-shrink-0" />
          </Show>
        </button>

        {/* Progress bar */}
        <Show when={props.group.progress > 0 && props.group.progress < 1}>
          <div class="w-12 h-1 rounded-full bg-[var(--alpha-white-5)] flex-shrink-0">
            <div
              class="h-full rounded-full transition-all duration-300"
              style={{
                width: `${Math.round(props.group.progress * 100)}%`,
                background: props.group.config.color,
              }}
            />
          </div>
        </Show>

        {/* Stop button for working leads */}
        <Show when={props.group.lead.status === 'working'}>
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              props.onStopLead(props.group.lead.id)
            }}
            class="p-1 rounded-[var(--radius-sm)] text-[var(--error)] hover:bg-[var(--error)]/10 transition-colors flex-shrink-0"
            title={`Stop ${props.group.lead.name}`}
          >
            <Square class="w-2.5 h-2.5" />
          </button>
        </Show>

        {/* Click to view lead detail */}
        <button
          type="button"
          onClick={() => props.onSelectMember(props.group.lead.id)}
          class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-3)] transition-colors flex-shrink-0"
          title="View lead details"
        >
          <Loader2 class="w-2.5 h-2.5" />
        </button>
      </div>

      {/* Workers list */}
      <Show when={expanded() && workers().length > 0}>
        <div class="px-2 pb-2 space-y-0.5">
          <For each={workers()}>
            {(worker) => (
              <WorkerCard member={worker} onClick={() => props.onSelectMember(worker.id)} />
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
  const stats = () => team.teamStats()

  const handleStopLead = (leadId: string) => {
    props.onStopAgent?.(leadId)
  }

  const handleStopAll = () => {
    props.onStopAll?.()
  }

  const hasWorkingAgents = () => team.allMembers().some((m) => m.status === 'working')

  return (
    <div class="flex flex-col h-full bg-[var(--bg-primary)]">
      {/* Header */}
      <div class="h-10 px-3 flex items-center justify-between border-b border-[var(--border-subtle)]">
        <span class="font-[var(--font-ui-mono)] text-[11px] tracking-widest uppercase text-[var(--text-secondary)]">
          Team
        </span>
        <div class="flex items-center gap-2">
          <Show when={stats().totalTeams > 0}>
            <span class="font-[var(--font-ui-mono)] text-[9px] text-[var(--text-muted)]">
              {stats().activeTeams} active / {stats().totalTeams} teams
            </span>
          </Show>
          <Show when={hasWorkingAgents()}>
            <button
              type="button"
              onClick={handleStopAll}
              class="flex items-center gap-1 px-1.5 py-0.5 rounded-[var(--radius-md)] text-[9px] font-medium text-[var(--error)] bg-[var(--error)]/10 hover:bg-[var(--error)]/20 transition-colors"
              title="Stop all running agents"
            >
              <Octagon class="w-2.5 h-2.5" />
              Stop All
            </button>
          </Show>
        </div>
      </div>

      {/* Scrollable content */}
      <div class="flex-1 p-3 space-y-2 overflow-y-auto scrollbar-none">
        {/* Director card */}
        <Show when={director()}>
          {(dir) => (
            <button
              type="button"
              onClick={() => team.setSelectedMemberId(dir().id)}
              class="w-full rounded-[var(--radius-lg)] border border-[var(--border-default)] bg-[var(--surface)] px-3 py-2 flex items-center gap-2 hover:bg-[var(--alpha-white-3)] transition-colors cursor-pointer text-left"
            >
              <div class="p-1 rounded-[var(--radius-md)] bg-[var(--accent-subtle)]">
                <Crown class="w-3.5 h-3.5 text-[var(--accent)]" />
              </div>
              <div class="flex-1 min-w-0">
                <div class="text-[12px] font-semibold text-[var(--text-primary)]">Director</div>
                <div class="text-[10px] text-[var(--text-muted)] truncate">
                  {dir().task ?? 'Orchestrating team'}
                </div>
              </div>
              <span class={`w-2 h-2 rounded-full flex-shrink-0 ${statusDotClass(dir().status)}`} />
            </button>
          )}
        </Show>

        {/* Team groups */}
        <Show
          when={groups().length > 0}
          fallback={
            <Show when={director()}>
              <div class="text-[11px] text-[var(--text-muted)] px-1">
                Waiting for assignments...
              </div>
            </Show>
          }
        >
          <For each={groups()}>
            {(group) => (
              <TeamGroupCard
                group={group}
                onSelectMember={(id) => team.setSelectedMemberId(id)}
                onStopLead={handleStopLead}
              />
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
