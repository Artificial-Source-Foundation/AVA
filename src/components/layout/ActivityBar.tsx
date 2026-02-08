/**
 * Activity Bar Component
 *
 * Thin vertical icon strip on the far left — the #1 visual signal
 * that says "this is a developer tool." Switches sidebar content.
 * Inspired by VS Code / Cursor activity bar.
 */

import {
  Activity,
  Bot,
  Brain,
  FolderTree,
  MessageSquare,
  PanelLeftClose,
  PanelLeftOpen,
  Puzzle,
  Settings,
  Sparkles,
  Users,
} from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { type ActivityId, useLayout } from '../../stores/layout'
import { useNavigation } from '../../stores/navigation'

interface ActivityItem {
  id: ActivityId
  icon: typeof MessageSquare
  label: string
}

const activities: ActivityItem[] = [
  { id: 'sessions', icon: MessageSquare, label: 'Sessions' },
  { id: 'explorer', icon: FolderTree, label: 'Explorer' },
  { id: 'agents', icon: Bot, label: 'Agents' },
  { id: 'team', icon: Users, label: 'Dev Team' },
  { id: 'memory', icon: Brain, label: 'Memory' },
  { id: 'activity', icon: Activity, label: 'Activity' },
  { id: 'plugins', icon: Puzzle, label: 'Plugins' },
]

export const ActivityBar: Component = () => {
  const { handleActivityClick, activeActivity, sidebarVisible, toggleSidebar } = useLayout()
  const { goToSettings, currentView } = useNavigation()

  const isActive = (id: ActivityId) => activeActivity() === id && sidebarVisible()

  return (
    <div
      class="
        flex flex-col items-center
        w-12 h-full flex-shrink-0
        bg-[var(--gray-1)]
        border-r border-[var(--border-subtle)]
      "
    >
      {/* Logo */}
      <div class="flex items-center justify-center w-12 h-12 flex-shrink-0">
        <div class="w-6 h-6 rounded-md bg-[var(--accent)] flex items-center justify-center">
          <Sparkles class="w-3.5 h-3.5 text-white" />
        </div>
      </div>

      {/* Activity Icons */}
      <div class="flex flex-col items-center gap-0.5 mt-1 flex-1">
        <For each={activities}>
          {(item) => {
            const Icon = item.icon
            const active = () => isActive(item.id)

            return (
              <button
                type="button"
                onClick={() => handleActivityClick(item.id)}
                class={`
                  relative flex items-center justify-center
                  w-12 h-10
                  transition-colors duration-[var(--duration-fast)]
                  ${
                    active()
                      ? 'text-[var(--text-primary)]'
                      : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)]'
                  }
                `}
                title={item.label}
              >
                {/* Active indicator — left accent border */}
                <Show when={active()}>
                  <span
                    class="
                      absolute left-0 top-1.5 bottom-1.5 w-[3px]
                      rounded-r-full bg-[var(--accent)]
                    "
                  />
                </Show>

                <span
                  class={`
                    flex items-center justify-center
                    w-8 h-8 rounded-[var(--radius-md)]
                    transition-colors duration-[var(--duration-fast)]
                    ${active() ? 'bg-[var(--alpha-white-8)]' : 'hover:bg-[var(--alpha-white-5)]'}
                  `}
                >
                  <Icon class="w-[18px] h-[18px]" />
                </span>
              </button>
            )
          }}
        </For>
      </div>

      {/* Bottom icons: Sidebar toggle + Settings */}
      <div class="flex flex-col items-center gap-0.5 mb-2">
        {/* Sidebar toggle */}
        <button
          type="button"
          onClick={toggleSidebar}
          class="
            flex items-center justify-center
            w-12 h-10
            transition-colors duration-[var(--duration-fast)]
            text-[var(--text-muted)] hover:text-[var(--text-secondary)]
          "
          title="Toggle Sidebar (Ctrl+B)"
        >
          <span class="flex items-center justify-center w-8 h-8 rounded-[var(--radius-md)] hover:bg-[var(--alpha-white-5)] transition-colors">
            {sidebarVisible() ? (
              <PanelLeftClose class="w-[18px] h-[18px]" />
            ) : (
              <PanelLeftOpen class="w-[18px] h-[18px]" />
            )}
          </span>
        </button>

        {/* Settings */}
        <button
          type="button"
          onClick={goToSettings}
          class={`
            flex items-center justify-center
            w-12 h-10
            transition-colors duration-[var(--duration-fast)]
            ${
              currentView() === 'settings'
                ? 'text-[var(--text-primary)]'
                : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)]'
            }
          `}
          title="Settings"
        >
          <span
            class={`
              flex items-center justify-center
              w-8 h-8 rounded-[var(--radius-md)]
              transition-colors duration-[var(--duration-fast)]
              ${currentView() === 'settings' ? 'bg-[var(--alpha-white-8)]' : 'hover:bg-[var(--alpha-white-5)]'}
            `}
          >
            <Settings class="w-[18px] h-[18px]" />
          </span>
        </button>
      </div>
    </div>
  )
}
