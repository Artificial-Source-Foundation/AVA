/**
 * Team Group Card Component
 *
 * Displays a collapsible card for a single team (Senior Lead + Junior Devs)
 * with progress bar, delegation context, and expand/collapse.
 */

import { ChevronDown, ChevronRight } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { TeamGroup } from '../../../types/team'
import { MemberRow } from './MemberRow'

export const TeamCard: Component<{
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
