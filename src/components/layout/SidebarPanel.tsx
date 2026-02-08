/**
 * Sidebar Panel Component
 *
 * Contextual sidebar wrapper that renders different content
 * based on the active activity bar selection.
 */

import { type Component, Match, Show, Switch } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { useTeam } from '../../stores/team'
import { AgentActivityPanel } from '../panels/AgentActivityPanel'
import { TeamMemberChat } from '../panels/TeamMemberChat'
import { TeamPanel } from '../panels/TeamPanel'
import { SidebarAgents } from '../sidebar/SidebarAgents'
import { SidebarExplorer } from '../sidebar/SidebarExplorer'
import { SidebarMemory } from '../sidebar/SidebarMemory'
import { SidebarPlugins } from '../sidebar/SidebarPlugins'
import { SidebarSessions } from '../sidebar/SidebarSessions'

export const SidebarPanel: Component = () => {
  const { activeActivity } = useLayout()
  const team = useTeam()

  return (
    <aside class="flex flex-col h-full w-full glass-strong overflow-hidden">
      <Switch>
        <Match when={activeActivity() === 'sessions'}>
          <SidebarSessions />
        </Match>
        <Match when={activeActivity() === 'explorer'}>
          <SidebarExplorer />
        </Match>
        <Match when={activeActivity() === 'agents'}>
          <SidebarAgents />
        </Match>
        <Match when={activeActivity() === 'team'}>
          <Show when={team.selectedMember()} fallback={<TeamPanel />}>
            <TeamMemberChat
              member={team.selectedMember()!}
              onBack={() => team.setSelectedMemberId(null)}
            />
          </Show>
        </Match>
        <Match when={activeActivity() === 'memory'}>
          <SidebarMemory />
        </Match>
        <Match when={activeActivity() === 'activity'}>
          <div class="flex flex-col h-full">
            <div class="px-3 py-2 border-b border-[var(--border-subtle)] flex-shrink-0">
              <span class="text-sm font-medium text-[var(--text-primary)]">Activity</span>
            </div>
            <div class="flex-1 overflow-hidden">
              <AgentActivityPanel />
            </div>
          </div>
        </Match>
        <Match when={activeActivity() === 'plugins'}>
          <SidebarPlugins />
        </Match>
      </Switch>
    </aside>
  )
}
