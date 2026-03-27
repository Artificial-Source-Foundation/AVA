import { ArrowLeft, Search, X } from 'lucide-solid'
import { type Accessor, type Component, createEffect, createMemo, For, Show } from 'solid-js'
import { useSettings } from '../../stores/settings'
import { type SettingsTab, type TabGroup, tabGroups } from './settings-modal-config'

interface SettingsModalSidebarProps {
  activeTab: () => SettingsTab
  onSelectTab: (tab: SettingsTab) => void
  onBack: () => void
  search: Accessor<string>
  onSearchChange: (value: string) => void
}

export const SettingsModalSidebar: Component<SettingsModalSidebarProps> = (props) => {
  const { settings } = useSettings()

  const filteredGroups = createMemo((): TabGroup[] => {
    const q = props.search().toLowerCase().trim()
    const devMode = settings().devMode ?? false

    // Filter tabs: hide Developer when devMode is off
    const base = tabGroups.map((group) => ({
      ...group,
      tabs: group.tabs.filter((tab) => {
        if (tab.id === 'developer' && !devMode) return false
        return true
      }),
    }))

    if (!q) return base.filter((group) => group.tabs.length > 0)

    return base
      .map((group) => ({
        ...group,
        tabs: group.tabs.filter(
          (tab) => tab.label.toLowerCase().includes(q) || tab.keywords.some((kw) => kw.includes(q))
        ),
      }))
      .filter((group) => group.tabs.length > 0)
  })

  // Auto-switch to first matching tab when search filters results
  createEffect(() => {
    const q = props.search().toLowerCase().trim()
    if (!q) return

    const groups = filteredGroups()
    const allMatchingTabs = groups.flatMap((g) => g.tabs)
    if (allMatchingTabs.length > 0 && !allMatchingTabs.some((t) => t.id === props.activeTab())) {
      props.onSelectTab(allMatchingTabs[0].id)
    }
  })

  /** Separate the footer group (empty label) from named groups */
  const namedGroups = createMemo(() => filteredGroups().filter((g) => g.label !== ''))
  const footerGroup = createMemo(() => filteredGroups().find((g) => g.label === ''))

  return (
    <nav
      class="flex-shrink-0 flex flex-col border-r border-[var(--gray-5)]"
      style={{ width: '220px', background: '#0F0F12' }}
    >
      {/* Back link */}
      <button
        type="button"
        onClick={() => props.onBack()}
        class="flex items-center gap-2 px-4 py-3 text-[var(--settings-text-input)] text-[var(--gray-7)] hover:text-[var(--gray-9)] transition-colors"
      >
        <ArrowLeft class="w-3.5 h-3.5" />
        Back to Chat
      </button>

      {/* Search */}
      <div class="px-3 pb-2">
        <div class="relative">
          <Search class="absolute left-2.5 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-[var(--gray-7)]" />
          <input
            type="text"
            placeholder="Search settings..."
            value={props.search()}
            onInput={(e) => props.onSearchChange(e.currentTarget.value)}
            class="w-full pl-8 pr-7 py-1.5 text-[12px] text-[var(--text-primary)] bg-[var(--gray-3)] border border-[var(--gray-5)] rounded-[var(--radius-md)] placeholder:text-[var(--gray-7)] focus:border-[var(--accent)] outline-none"
          />
          <Show when={props.search()}>
            <button
              type="button"
              onClick={() => props.onSearchChange('')}
              class="absolute right-1.5 top-1/2 -translate-y-1/2 p-0.5 rounded-[var(--radius-sm)] text-[var(--gray-7)] hover:text-[var(--text-primary)]"
            >
              <X class="w-3 h-3" />
            </button>
          </Show>
        </div>
      </div>

      {/* Tab groups */}
      <div class="flex-1 overflow-y-auto px-3 pb-3 space-y-4">
        <For each={namedGroups()}>
          {(group) => (
            <div>
              <p
                class="px-2.5 mb-1.5 uppercase"
                style={{
                  'font-size': '10px',
                  'font-weight': '600',
                  color: 'var(--gray-6)',
                  'letter-spacing': '0.8px',
                }}
              >
                {group.label}
              </p>
              <div class="space-y-0.5">
                <For each={group.tabs}>
                  {(tab) => {
                    const Icon = tab.icon
                    const isActive = (): boolean => props.activeTab() === tab.id
                    return (
                      <button
                        type="button"
                        onClick={() => props.onSelectTab(tab.id)}
                        class="w-full flex items-center rounded-[10px] transition-colors duration-[var(--duration-fast)]"
                        style={{
                          gap: '10px',
                          padding: '10px 14px',
                          background: isActive() ? 'var(--gray-3)' : 'transparent',
                          color: isActive() ? 'var(--text-primary)' : 'var(--gray-9)',
                          'font-weight': isActive() ? '500' : '400',
                          'font-size': '13px',
                        }}
                      >
                        <Icon
                          class="w-4 h-4 flex-shrink-0"
                          style={{
                            color: isActive() ? 'var(--accent-hover)' : 'var(--gray-7)',
                          }}
                        />
                        {tab.label}
                      </button>
                    )
                  }}
                </For>
              </div>
            </div>
          )}
        </For>

        <Show when={filteredGroups().length === 0}>
          <p class="px-2.5 py-4 text-xs text-[var(--gray-8)] text-center">No matching settings</p>
        </Show>
      </div>

      {/* Footer tabs (Developer + About) — pinned to bottom */}
      <Show when={footerGroup()}>
        <div class="mt-auto border-t border-[var(--gray-5)] px-3 py-2">
          <For each={footerGroup()!.tabs}>
            {(tab) => {
              const Icon = tab.icon
              const isActive = (): boolean => props.activeTab() === tab.id
              return (
                <button
                  type="button"
                  onClick={() => props.onSelectTab(tab.id)}
                  class="w-full flex items-center rounded-[10px] transition-colors duration-[var(--duration-fast)]"
                  style={{
                    gap: '10px',
                    padding: '10px 14px',
                    background: isActive() ? 'var(--gray-3)' : 'transparent',
                    color: isActive() ? 'var(--text-primary)' : 'var(--gray-9)',
                    'font-weight': isActive() ? '500' : '400',
                    'font-size': '13px',
                  }}
                >
                  <Icon
                    class="w-4 h-4 flex-shrink-0"
                    style={{
                      color: isActive() ? 'var(--accent-hover)' : 'var(--gray-7)',
                    }}
                  />
                  {tab.label}
                </button>
              )
            }}
          </For>
        </div>
      </Show>
    </nav>
  )
}
