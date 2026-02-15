import { type Component, For, Show } from 'solid-js'
import { type SettingsTab, tabGroups } from './settings-modal-config'

interface SettingsModalSidebarProps {
  activeTab: () => SettingsTab
  onSelectTab: (tab: SettingsTab) => void
}

export const SettingsModalSidebar: Component<SettingsModalSidebarProps> = (props) => {
  return (
    <nav class="w-44 flex-shrink-0 border-r border-[var(--border-subtle)] bg-[var(--gray-1)] flex flex-col py-3">
      <div class="px-4 mb-3">
        <h2 class="text-sm font-semibold text-[var(--text-primary)]">Settings</h2>
      </div>

      <div class="flex-1 overflow-y-auto space-y-3 px-2" style={{ transform: 'translateZ(0)' }}>
        <For each={tabGroups}>
          {(group) => (
            <div>
              <Show when={group.label}>
                <p class="px-2 mb-1 text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider">
                  {group.label}
                </p>
              </Show>
              <div class="space-y-0.5">
                <For each={group.tabs}>
                  {(tab) => {
                    const Icon = tab.icon
                    return (
                      <button
                        type="button"
                        onClick={() => props.onSelectTab(tab.id)}
                        class={`w-full flex items-center gap-2 px-2 py-1.5 text-xs rounded-[var(--radius-md)] transition-colors duration-[var(--duration-fast)] ${props.activeTab() === tab.id ? 'text-[var(--text-primary)] bg-[var(--alpha-white-8)]' : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)]'}`}
                      >
                        <Icon class="w-3.5 h-3.5" />
                        {tab.label}
                      </button>
                    )
                  }}
                </For>
              </div>
            </div>
          )}
        </For>
      </div>
    </nav>
  )
}
