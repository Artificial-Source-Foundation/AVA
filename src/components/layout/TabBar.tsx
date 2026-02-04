/**
 * TabBar Component
 *
 * Navigation tabs for switching between Chat, Agents, Files, and Memory panels.
 * Uses design system tokens for consistent theming.
 */

import { Bot, Brain, FolderOpen, MessageSquare, Terminal } from 'lucide-solid'
import { type Component, For } from 'solid-js'
import { activeTab, setActiveTab, type TabId } from '../../stores/session'

interface TabDefinition {
  id: TabId
  label: string
  icon: typeof MessageSquare
}

const tabs: TabDefinition[] = [
  { id: 'chat', label: 'Chat', icon: MessageSquare },
  { id: 'agents', label: 'Agents', icon: Bot },
  { id: 'files', label: 'Files', icon: FolderOpen },
  { id: 'terminal', label: 'Terminal', icon: Terminal },
  { id: 'memory', label: 'Memory', icon: Brain },
]

export const TabBar: Component = () => {
  return (
    <div
      class="
        flex items-center gap-1
        h-12 px-3
        bg-[var(--surface-raised)]
        border-b border-[var(--border-subtle)]
      "
    >
      <For each={tabs}>
        {(tab) => {
          const Icon = tab.icon
          const isActive = () => activeTab() === tab.id

          return (
            <button
              type="button"
              onClick={() => setActiveTab(tab.id)}
              class={`
                flex items-center gap-2
                px-4 py-2
                rounded-[var(--radius-md)]
                text-sm font-medium
                transition-all duration-[var(--duration-fast)]
                ${
                  isActive()
                    ? 'bg-[var(--accent)] text-white shadow-sm'
                    : 'text-[var(--text-tertiary)] hover:text-[var(--text-primary)] hover:bg-[var(--surface-sunken)]'
                }
              `}
            >
              <Icon class="w-4 h-4" />
              <span>{tab.label}</span>
            </button>
          )
        }}
      </For>
    </div>
  )
}
