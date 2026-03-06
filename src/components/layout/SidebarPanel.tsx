/**
 * Sidebar Panel Component
 *
 * Contextual sidebar wrapper that renders different content
 * based on the active activity bar selection.
 * Slim: only Sessions and Explorer.
 */

import { type Component, Match, Switch } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { SidebarExplorer } from '../sidebar/SidebarExplorer'
import { SidebarSessions } from '../sidebar/SidebarSessions'
import { PanelErrorBoundary } from '../ui/PanelErrorBoundary'

export const SidebarPanel: Component = () => {
  const { activeActivity } = useLayout()

  return (
    <aside class="flex flex-col h-full w-full glass-strong overflow-hidden">
      <Switch>
        <Match when={activeActivity() === 'sessions'}>
          <PanelErrorBoundary panelName="Sessions">
            <SidebarSessions />
          </PanelErrorBoundary>
        </Match>
        <Match when={activeActivity() === 'explorer'}>
          <PanelErrorBoundary panelName="Explorer">
            <SidebarExplorer />
          </PanelErrorBoundary>
        </Match>
      </Switch>
    </aside>
  )
}
