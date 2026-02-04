/**
 * MainContent Component
 *
 * Tab content router - switches between Chat, Agents, Files, and Memory panels.
 * Uses SolidJS Switch/Match for efficient rendering.
 */

import { type Component, Match, Switch } from 'solid-js'
import { activeTab } from '../../stores/session'
import { ChatView } from '../chat'
import { AgentActivityPanel, FileOperationsPanel, MemoryPanel, TerminalPanel } from '../panels'

export const MainContent: Component = () => {
  return (
    <div class="flex-1 overflow-hidden">
      <Switch>
        <Match when={activeTab() === 'chat'}>
          <ChatView />
        </Match>
        <Match when={activeTab() === 'agents'}>
          <AgentActivityPanel />
        </Match>
        <Match when={activeTab() === 'files'}>
          <FileOperationsPanel />
        </Match>
        <Match when={activeTab() === 'terminal'}>
          <TerminalPanel />
        </Match>
        <Match when={activeTab() === 'memory'}>
          <MemoryPanel />
        </Match>
      </Switch>
    </div>
  )
}
