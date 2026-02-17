/**
 * Sidebar Panel Component
 *
 * Contextual sidebar wrapper that renders different content
 * based on the active activity bar selection.
 * Slim: only Sessions and Explorer.
 */

import { type Component, Match, Switch } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { ProjectSelector } from '../projects/ProjectSelector'
import { SidebarExplorer } from '../sidebar/SidebarExplorer'
import { SidebarPlugins } from '../sidebar/SidebarPlugins'
import { SidebarSessions } from '../sidebar/SidebarSessions'

export const SidebarPanel: Component = () => {
  const { activeActivity } = useLayout()

  return (
    <aside class="flex flex-col h-full w-full glass-strong overflow-hidden">
      <Switch>
        <Match when={activeActivity() === 'sessions'}>
          <SidebarSessions />
        </Match>
        <Match when={activeActivity() === 'projects'}>
          <ProjectSelector />
        </Match>
        <Match when={activeActivity() === 'explorer'}>
          <SidebarExplorer />
        </Match>
        <Match when={activeActivity() === 'plugins'}>
          <SidebarPlugins />
        </Match>
      </Switch>
    </aside>
  )
}
