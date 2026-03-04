/**
 * Team Status Strip
 *
 * Compact horizontal strip shown in the main chat when a team is active.
 * Shows mini status dots for each active team member with click-to-enter.
 * Displays overall progress and "N agents working" summary.
 */

import { type Component, For, Show } from 'solid-js'
import { useTeam } from '../../stores/team'
import { TEAM_DOMAINS, type TeamMember } from '../../types/team'

export const TeamStatusStrip: Component = () => {
  const team = useTeam()

  const isActive = () => team.hierarchy() !== null
  const workingCount = () => team.allMembers().filter((m) => m.status === 'working').length
  const doneCount = () => team.allMembers().filter((m) => m.status === 'done').length
  const totalCount = () => team.allMembers().length

  return (
    <Show when={isActive()}>
      <div class="flex items-center gap-2 px-3 py-1.5 border-t border-[var(--border-subtle)] bg-[var(--bg-subtle)]/50">
        {/* Member dots */}
        <div class="flex items-center gap-1">
          <For each={team.allMembers()}>
            {(member) => (
              <MemberDot member={member} onClick={() => team.setSelectedMemberId(member.id)} />
            )}
          </For>
        </div>

        <span class="flex-1" />

        {/* Summary text */}
        <Show when={workingCount() > 0}>
          <span class="text-[11px] text-[var(--accent-text)] tabular-nums">
            {workingCount()} working
          </span>
        </Show>
        <Show when={doneCount() > 0}>
          <span class="text-[11px] text-[var(--text-muted)] tabular-nums">
            {doneCount()}/{totalCount()} done
          </span>
        </Show>
      </div>
    </Show>
  )
}

const MemberDot: Component<{ member: TeamMember; onClick: () => void }> = (props) => {
  const config = () => TEAM_DOMAINS[props.member.domain]

  return (
    <button
      type="button"
      onClick={props.onClick}
      class="group relative flex items-center justify-center w-6 h-6 rounded-full transition-all duration-[var(--duration-fast)] hover:scale-110 cursor-pointer"
      style={{ background: config().colorSubtle }}
      title={`${props.member.name} — ${props.member.task ?? 'idle'}`}
    >
      {/* Domain short label */}
      <span class="text-[8px] font-bold" style={{ color: config().color }}>
        {config().short}
      </span>

      {/* Status indicator dot */}
      <span
        class="absolute -bottom-0.5 -right-0.5 w-2 h-2 rounded-full border border-[var(--bg-subtle)]"
        classList={{
          'bg-[var(--accent)] animate-pulse-subtle': props.member.status === 'working',
          'bg-[var(--success)]': props.member.status === 'done',
          'bg-[var(--error)]': props.member.status === 'error',
          'bg-[var(--text-muted)]': props.member.status === 'idle',
          'bg-[var(--warning)]': props.member.status === 'reporting',
        }}
      />
    </button>
  )
}
