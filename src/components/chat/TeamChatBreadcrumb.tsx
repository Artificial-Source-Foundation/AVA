/**
 * Team Chat Breadcrumb
 *
 * Clickable breadcrumb trail showing the hierarchy path from Main Chat
 * through Team Lead to the currently viewed team member.
 * Click any ancestor to navigate to their chat view.
 */

import { ChevronRight } from 'lucide-solid'
import { type Component, For } from 'solid-js'
import { useTeam } from '../../stores/team'
import type { TeamMember } from '../../types/team'

interface BreadcrumbItem {
  id: string | null
  label: string
}

export const TeamChatBreadcrumb: Component = () => {
  const team = useTeam()

  /** Build the breadcrumb chain from current member up to root. */
  const breadcrumbs = (): BreadcrumbItem[] => {
    const member = team.selectedMember()
    if (!member) return []

    const chain: TeamMember[] = []
    let current: TeamMember | undefined = member

    // Walk up the parent chain
    while (current) {
      chain.unshift(current)
      current = current.parentId ? team.teamMembers().get(current.parentId) : undefined
    }

    // Build items: Main Chat → ancestors → current
    const items: BreadcrumbItem[] = [{ id: null, label: 'Main Chat' }]
    for (const m of chain) {
      items.push({ id: m.id, label: m.name })
    }
    return items
  }

  return (
    <div class="flex items-center gap-1 px-3 py-2 text-[12px] border-b border-[var(--border-subtle)] bg-[var(--bg-subtle)] overflow-x-auto scrollbar-none">
      <For each={breadcrumbs()}>
        {(item, index) => (
          <>
            {index() > 0 && <ChevronRight class="w-3 h-3 flex-shrink-0 text-[var(--text-muted)]" />}
            <button
              type="button"
              class="whitespace-nowrap rounded px-1.5 py-0.5 transition-colors duration-[var(--duration-fast)]"
              classList={{
                'text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-3)] cursor-pointer':
                  item.id !== team.selectedMemberId(),
                'text-[var(--text-primary)] font-medium cursor-default':
                  item.id === team.selectedMemberId(),
              }}
              onClick={() => {
                if (item.id !== team.selectedMemberId()) {
                  team.setSelectedMemberId(item.id)
                }
              }}
            >
              {item.label}
            </button>
          </>
        )}
      </For>
    </div>
  )
}
