/**
 * Team Member Row Component
 *
 * Displays a single team member with status dot, name, task, and status icon.
 */

import { AlertCircle, CheckCircle2, Crown, Loader2 } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { TEAM_DOMAINS, type TeamMember } from '../../../types/team'

export const MemberRow: Component<{
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
          ${isWorking() ? 'animate-pulse-subtle' : ''}
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
