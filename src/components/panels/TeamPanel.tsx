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

import { CheckCircle2, Crown, Loader2, Users } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { useTeam } from '../../stores/team'
import { DelegationLog } from './team/DelegationLog'
import { TeamCard } from './team/TeamCard'
import {
  DelegationLines,
  ParallelBadge,
  type Phase,
  PhaseTimeline,
  statusLabel,
} from './team/TeamHelpers'
import { TeamMetrics } from './team/TeamMetrics'
import { WorkerDetail } from './team/WorkerDetail'

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
