/**
 * Team Panel Component
 *
 * Shows the dev team hierarchy as real teams with delegation flow.
 * Team Lead at top, SVG lines to active Senior Leads, then team cards.
 * Each team shows its Senior Lead and Junior Devs.
 *
 * Features:
 * - SVG delegation flow lines (animated dash for active, static for done)
 * - Delegation context (what task was assigned)
 * - Parallel execution indicator when 2+ teams work simultaneously
 * - Phase timeline at bottom
 *
 * Click a team member to view their scoped chat.
 */

import {
  AlertCircle,
  CheckCircle2,
  ChevronDown,
  ChevronRight,
  Crown,
  GitBranch,
  Loader2,
  Users,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useTeam } from '../../stores/team'
import { TEAM_DOMAINS, type TeamGroup, type TeamMember } from '../../types/team'
import { DelegationLog } from './team/DelegationLog'
import { TeamMetrics } from './team/TeamMetrics'
import { WorkerDetail } from './team/WorkerDetail'

// ============================================================================
// Status helpers
// ============================================================================

const statusLabel: Record<string, string> = {
  idle: 'Idle',
  working: 'Working',
  reporting: 'Reporting',
  done: 'Done',
  error: 'Error',
}

// ============================================================================
// Delegation Flow Lines (SVG)
// ============================================================================

const DelegationLines: Component<{ teamCount: number; groups: TeamGroup[] }> = (props) => {
  // Simple vertical connector from Team Lead down to team cards
  // Each line gets animated dash pattern when the team is active
  const lineStartY = 4
  const lineEndY = 20
  const midX = 12

  return (
    <Show when={props.teamCount > 0}>
      <div class="px-4 py-0.5">
        <svg width="100%" height="24" class="overflow-visible" aria-hidden="true">
          {/* Vertical trunk line from Team Lead */}
          <line
            x1={midX}
            y1={lineStartY}
            x2={midX}
            y2={lineEndY}
            stroke="var(--border-default)"
            stroke-width="1.5"
            stroke-dasharray={props.groups.some((g) => g.status === 'working') ? '4 3' : 'none'}
            class={
              props.groups.some((g) => g.status === 'working') ? 'animate-delegation-flow' : ''
            }
          />
          {/* Small circle at junction */}
          <circle cx={midX} cy={lineEndY} r="2.5" fill="var(--border-default)" />
        </svg>
      </div>
    </Show>
  )
}

// ============================================================================
// Parallel Badge
// ============================================================================

const ParallelBadge: Component<{ count: number }> = (props) => (
  <Show when={props.count >= 2}>
    <div class="flex items-center gap-1 px-2 py-0.5 mx-2 mb-1 rounded-[var(--radius-sm)] bg-[var(--accent-subtle)] border border-[var(--accent-border)]">
      <GitBranch class="w-3 h-3 text-[var(--accent)]" />
      <span class="font-[var(--font-ui-mono)] text-[9px] tracking-wider text-[var(--accent)] font-medium">
        {props.count} PARALLEL
      </span>
    </div>
  </Show>
)

// ============================================================================
// Phase Timeline
// ============================================================================

type Phase = 'idle' | 'planning' | 'delegating' | 'executing' | 'validating' | 'done'

const PhaseTimeline: Component<{ currentPhase: Phase }> = (props) => {
  const phases: { id: Phase; label: string }[] = [
    { id: 'planning', label: 'Plan' },
    { id: 'delegating', label: 'Delegate' },
    { id: 'executing', label: 'Execute' },
    { id: 'validating', label: 'Validate' },
    { id: 'done', label: 'Done' },
  ]

  const phaseIndex = () => {
    const idx = phases.findIndex((p) => p.id === props.currentPhase)
    return idx >= 0 ? idx : -1
  }

  return (
    <div class="flex items-center gap-0.5 px-3 py-1.5">
      <For each={phases}>
        {(_phase, idx) => {
          const isActive = () => idx() === phaseIndex()
          const isPast = () => idx() < phaseIndex()

          return (
            <>
              <div
                class={`
                  flex-1 h-1 rounded-full transition-colors duration-300
                  ${isPast() ? 'bg-[var(--accent)]' : isActive() ? 'bg-[var(--accent)]' : 'bg-[var(--surface-sunken)]'}
                `}
              />
              <Show when={idx() < phases.length - 1}>
                <div class="w-0.5" />
              </Show>
            </>
          )
        }}
      </For>
    </div>
  )
}

// ============================================================================
// Team Member Row
// ============================================================================

const MemberRow: Component<{
  member: TeamMember
  isLead?: boolean
  onClick: () => void
  isSelected: boolean
}> = (props) => {
  const config = () => TEAM_DOMAINS[props.member.domain]
  const isWorking = () => props.member.status === 'working'

  return (
    <button
      type="button"
      onClick={() => props.onClick()}
      class={`
        w-full flex items-center gap-2
        px-2.5 py-1.5
        rounded-[var(--radius-md)]
        text-left
        transition-colors duration-[var(--duration-fast)]
        ${
          props.isSelected
            ? 'bg-[var(--accent-subtle)] border border-[var(--accent-border)]'
            : 'hover:bg-[var(--alpha-white-5)] border border-transparent'
        }
      `}
    >
      {/* Status dot */}
      <span
        class={`
          w-1.5 h-1.5 rounded-full flex-shrink-0
          ${isWorking() ? 'animate-pulse' : ''}
        `}
        style={{
          background: isWorking()
            ? config().color
            : props.member.status === 'done'
              ? 'var(--success)'
              : props.member.status === 'error'
                ? 'var(--error)'
                : 'var(--gray-6)',
        }}
      />

      {/* Name */}
      <div class="flex-1 min-w-0">
        <div class="flex items-center gap-1.5">
          <Show when={props.isLead}>
            <Crown class="w-3 h-3 flex-shrink-0" style={{ color: config().color }} />
          </Show>
          <span class="font-[var(--font-ui-mono)] text-[11px] tracking-wide text-[var(--text-primary)] truncate">
            {props.member.name}
          </span>
        </div>
        <Show when={props.member.task}>
          <p class="text-[10px] text-[var(--text-muted)] truncate mt-0.5">{props.member.task}</p>
        </Show>
      </div>

      {/* Status icon */}
      <Show when={isWorking()}>
        <Loader2 class="w-3 h-3 flex-shrink-0 animate-spin" style={{ color: config().color }} />
      </Show>
      <Show when={props.member.status === 'done'}>
        <CheckCircle2 class="w-3 h-3 flex-shrink-0 text-[var(--success)]" />
      </Show>
      <Show when={props.member.status === 'error'}>
        <AlertCircle class="w-3 h-3 flex-shrink-0 text-[var(--error)]" />
      </Show>
    </button>
  )
}

// ============================================================================
// Team Group Card
// ============================================================================

const TeamCard: Component<{
  group: TeamGroup
  selectedId: string | null
  onSelectMember: (id: string) => void
}> = (props) => {
  const [expanded, setExpanded] = createSignal(true)
  const config = () => props.group.config
  const isActive = () => props.group.status === 'working'
  const progressPct = () => Math.round(props.group.progress * 100)

  return (
    <div
      class={`
        rounded-[var(--radius-lg)]
        border
        overflow-hidden
        transition-colors duration-[var(--duration-fast)]
        ${
          isActive()
            ? 'border-[var(--border-strong)] bg-[var(--surface)]'
            : props.group.status === 'done'
              ? 'border-[var(--success-border)] bg-[var(--surface)]'
              : props.group.status === 'error'
                ? 'border-[var(--error-border)] bg-[var(--surface)]'
                : 'border-[var(--border-subtle)] bg-[var(--surface)]'
        }
      `}
    >
      {/* Team header */}
      <button
        type="button"
        onClick={() => setExpanded(!expanded())}
        class="
          w-full flex items-center gap-2
          px-3 py-2
          text-left
          hover:bg-[var(--alpha-white-3)]
          transition-colors
        "
      >
        {/* Team color accent */}
        <span class="w-2 h-2 rounded-[2px] flex-shrink-0" style={{ background: config().color }} />

        {/* Team name + badge */}
        <div class="flex-1 min-w-0 flex items-center gap-2">
          <span class="font-[var(--font-ui-mono)] text-[12px] tracking-wide font-semibold text-[var(--text-primary)] truncate">
            {config().label}
          </span>
          <span
            class="font-[var(--font-ui-mono)] text-[9px] tracking-widest px-1.5 py-px rounded-[var(--radius-sm)] font-medium"
            style={{ background: config().colorSubtle, color: config().color }}
          >
            {config().short}
          </span>
        </div>

        {/* Member count */}
        <span class="font-[var(--font-ui-mono)] text-[10px] text-[var(--text-muted)]">
          {props.group.members.length + 1}
        </span>

        {/* Expand chevron */}
        <Show
          when={expanded()}
          fallback={<ChevronRight class="w-3.5 h-3.5 text-[var(--text-muted)]" />}
        >
          <ChevronDown class="w-3.5 h-3.5 text-[var(--text-muted)]" />
        </Show>
      </button>

      {/* Progress bar */}
      <Show when={isActive() || progressPct() > 0}>
        <div class="h-[2px] bg-[var(--surface-sunken)]">
          <div
            class="h-full transition-[width] duration-300"
            style={{ width: `${progressPct()}%`, background: config().color }}
          />
        </div>
      </Show>

      {/* Delegation context */}
      <Show when={expanded() && props.group.lead.delegationContext}>
        <div class="px-3 py-1 border-b border-[var(--border-subtle)]">
          <p class="text-[9px] text-[var(--text-muted)] italic truncate">
            Delegated: "{props.group.lead.delegationContext}"
          </p>
        </div>
      </Show>

      {/* Team members */}
      <Show when={expanded()}>
        <div class="px-1.5 py-1 space-y-0.5">
          {/* Senior Lead */}
          <MemberRow
            member={props.group.lead}
            isLead
            onClick={() => props.onSelectMember(props.group.lead.id)}
            isSelected={props.selectedId === props.group.lead.id}
          />

          {/* Junior Devs */}
          <For each={props.group.members}>
            {(member) => (
              <MemberRow
                member={member}
                onClick={() => props.onSelectMember(member.id)}
                isSelected={props.selectedId === member.id}
              />
            )}
          </For>
        </div>
      </Show>
    </div>
  )
}

// ============================================================================
// Main Panel
// ============================================================================

export const TeamPanel: Component = () => {
  const team = useTeam()

  const handleSelectMember = (id: string) => {
    team.setSelectedMemberId(team.selectedMemberId() === id ? null : id)
  }

  /** Determine current phase from team state */
  const currentPhase = (): Phase => {
    const lead = team.teamLead()
    if (!lead) return 'idle'

    const groups = team.teamGroups()
    const stats = team.teamStats()

    // All teams done
    if (stats.totalTeams > 0 && stats.doneTeams === stats.totalTeams) return 'done'

    // Teams are executing
    if (stats.activeTeams > 0) return 'executing'

    // Teams exist but none active — could be validating or delegating
    if (stats.totalTeams > 0) return 'validating'

    // Team lead working but no teams yet — planning
    if (lead.status === 'working' && groups.length === 0) return 'planning'

    // Lead done, teams exist — delegating
    if (lead.status === 'done') return 'done'

    return 'planning'
  }

  return (
    <div class="flex h-full">
      {/* Left: Team hierarchy */}
      <div class="flex flex-col flex-1 min-w-0 h-full">
        {/* Header */}
        <div class="flex items-center justify-between px-3 h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
          <span class="font-[var(--font-ui-mono)] text-[11px] font-semibold text-[var(--text-secondary)] uppercase tracking-widest">
            Dev Team
          </span>
          <Show when={team.teamStats().activeTeams > 0}>
            <div class="flex items-center gap-1">
              <Loader2 class="w-3 h-3 text-[var(--accent)] animate-spin" />
              <span class="font-[var(--font-ui-mono)] text-[10px] text-[var(--accent)]">
                {team.teamStats().activeTeams}
              </span>
            </div>
          </Show>
        </div>

        {/* Team Lead */}
        <Show when={team.teamLead()}>
          <div class="px-2 pt-2 pb-0">
            <button
              type="button"
              onClick={() => handleSelectMember(team.teamLead()!.id)}
              class={`
                w-full flex items-center gap-2.5
                px-3 py-2
                rounded-[var(--radius-lg)]
                border
                transition-colors duration-[var(--duration-fast)]
                ${
                  team.selectedMemberId() === team.teamLead()?.id
                    ? 'border-[var(--accent-border)] bg-[var(--accent-subtle)]'
                    : 'border-[var(--border-default)] bg-[var(--surface-raised)] hover:border-[var(--border-strong)]'
                }
              `}
            >
              <div
                class="p-1.5 rounded-[var(--radius-md)]"
                style={{ background: 'var(--accent-subtle)' }}
              >
                <Crown class="w-4 h-4 text-[var(--accent)]" />
              </div>
              <div class="flex-1 min-w-0 text-left">
                <div class="font-[var(--font-ui-mono)] text-[12px] tracking-wide font-semibold text-[var(--text-primary)]">
                  Team Lead
                </div>
                <div class="font-[var(--font-ui-mono)] text-[10px] text-[var(--text-muted)]">
                  {statusLabel[team.teamLead()!.status]} · {team.teamLead()!.model}
                </div>
              </div>
              <Show when={team.teamLead()!.status === 'working'}>
                <Loader2 class="w-3.5 h-3.5 text-[var(--accent)] animate-spin" />
              </Show>
              <Show when={team.teamLead()!.status === 'done'}>
                <CheckCircle2 class="w-3.5 h-3.5 text-[var(--success)]" />
              </Show>
            </button>
          </div>

          {/* Delegation flow lines */}
          <DelegationLines teamCount={team.teamGroups().length} groups={team.teamGroups()} />
        </Show>

        {/* Parallel execution indicator */}
        <ParallelBadge count={team.teamStats().activeTeams} />

        {/* Team Groups */}
        <div class="flex-1 overflow-y-auto px-2 py-1.5 space-y-2 scrollbar-none">
          <Show
            when={team.teamGroups().length > 0}
            fallback={
              <Show
                when={!team.teamLead()}
                fallback={
                  <div class="text-center py-6 px-4 text-[var(--text-muted)]">
                    <p class="text-[11px]">Team Lead is planning...</p>
                    <p class="text-[10px] mt-1">Teams will appear when work is delegated</p>
                  </div>
                }
              >
                <div class="flex flex-col items-center justify-center h-full text-center p-6">
                  <div class="p-3 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] mb-3">
                    <Users class="w-6 h-6 text-[var(--text-muted)]" />
                  </div>
                  <h3 class="font-[var(--font-ui-mono)] text-[11px] font-medium text-[var(--text-secondary)] mb-1">
                    No team active
                  </h3>
                  <p class="text-[10px] text-[var(--text-muted)] max-w-[180px]">
                    Start a coding task and your dev team will spin up automatically
                  </p>
                </div>
              </Show>
            }
          >
            <For each={team.teamGroups()}>
              {(group) => (
                <TeamCard
                  group={group}
                  selectedId={team.selectedMemberId()}
                  onSelectMember={handleSelectMember}
                />
              )}
            </For>
          </Show>
        </div>

        {/* Delegation Log (collapsible) */}
        <Show when={team.delegationLog().length > 0}>
          <DelegationLog events={team.delegationLog()} />
        </Show>

        {/* Phase Timeline + Team Metrics Footer */}
        <Show when={team.teamLead()}>
          <div class="border-t border-[var(--border-subtle)]">
            {/* Phase timeline */}
            <PhaseTimeline currentPhase={currentPhase()} />

            {/* Team Metrics */}
            <Show when={team.teamStats().totalTeams > 0}>
              <TeamMetrics members={team.allMembers()} delegations={team.delegationLog()} />
            </Show>
          </div>
        </Show>
      </div>

      {/* Right: Worker detail split panel */}
      <Show when={team.selectedMember()}>
        <div class="w-[240px] flex-shrink-0">
          <WorkerDetail member={team.selectedMember()!} />
        </div>
      </Show>
    </div>
  )
}
