import { ArrowLeft, Search, X } from 'lucide-solid'
import { type Accessor, type Component, createEffect, createMemo, For, Show } from 'solid-js'
import { useSettings } from '../../stores/settings'
import {
  type SettingsSearchEntry,
  type SettingsTab,
  settingsSearchIndex,
  type TabGroup,
  tabGroups,
} from './settings-modal-config'

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

    const base = tabGroups.map((group) => ({
      ...group,
      tabs: group.tabs.filter((tab) => {
        if (tab.id === 'developer' && !devMode) return false
        return true
      }),
    }))

    if (!q) return base.filter((group) => group.tabs.length > 0)

    // Include tabs that match by name/keywords OR have matching deep settings
    const deepMatchTabs = new Set(
      settingsSearchIndex
        .filter(
          (entry) =>
            entry.label.toLowerCase().includes(q) ||
            (entry.description?.toLowerCase().includes(q) ?? false)
        )
        .map((entry) => entry.tab)
    )

    return base
      .map((group) => ({
        ...group,
        tabs: group.tabs.filter(
          (tab) =>
            tab.label.toLowerCase().includes(q) ||
            tab.keywords.some((kw) => kw.includes(q)) ||
            deepMatchTabs.has(tab.id)
        ),
      }))
      .filter((group) => group.tabs.length > 0)
  })

  /** Deep search results — individual settings matching the query */
  const searchResults = createMemo((): SettingsSearchEntry[] => {
    const q = props.search().toLowerCase().trim()
    if (!q || q.length < 2) return []

    return settingsSearchIndex
      .filter(
        (entry) =>
          entry.label.toLowerCase().includes(q) ||
          (entry.description?.toLowerCase().includes(q) ?? false)
      )
      .slice(0, 8)
  })

  // Auto-switch to first matching tab when search filters results
  createEffect(() => {
    const q = props.search().toLowerCase().trim()
    if (!q) return

    // Prefer deep search results
    const results = searchResults()
    if (results.length > 0 && results[0].tab !== props.activeTab()) {
      props.onSelectTab(results[0].tab)
      return
    }

    const groups = filteredGroups()
    const allMatchingTabs = groups.flatMap((g) => g.tabs)
    if (allMatchingTabs.length > 0 && !allMatchingTabs.some((t) => t.id === props.activeTab())) {
      props.onSelectTab(allMatchingTabs[0].id)
    }
  })

  const namedGroups = createMemo(() => filteredGroups())

  const handleResultClick = (entry: SettingsSearchEntry) => {
    props.onSelectTab(entry.tab)
    props.onSearchChange('')
  }

  return (
    <nav
      class="flex-shrink-0 flex flex-col min-h-0"
      style={{
        width: '220px',
        background: 'transparent',
        'border-right': '1px solid #ffffff06',
        padding: '24px 0 24px 20px',
      }}
    >
      {/* Back to Chat */}
      <button
        type="button"
        onClick={() => props.onBack()}
        class="flex items-center gap-2 mb-4 hover:text-[#F5F5F7] transition-colors"
        style={{
          padding: '0 0 0 12px',
          background: 'none',
          border: 'none',
          cursor: 'pointer',
          'font-family': 'Geist, sans-serif',
          'font-size': '13px',
          'font-weight': '400',
          color: '#48484A',
        }}
      >
        <ArrowLeft style={{ width: '14px', height: '14px' }} />
        Back to Chat
      </button>

      {/* Settings title */}
      <div style={{ padding: '0 0 0 12px', 'margin-bottom': '20px' }}>
        <span
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '18px',
            'font-weight': '600',
            color: '#F5F5F7',
          }}
        >
          Settings
        </span>
      </div>

      {/* Search */}
      <div class="pr-3 pb-3 relative" style={{ 'padding-left': '0' }}>
        <div class="relative">
          <Search
            class="absolute left-2.5 top-1/2 -translate-y-1/2 w-3.5 h-3.5"
            style={{ color: '#48484A' }}
          />
          <input
            type="text"
            placeholder="Search settings..."
            value={props.search()}
            onInput={(e) => props.onSearchChange(e.currentTarget.value)}
            class="w-full pl-8 pr-7 py-1.5 outline-none"
            style={{
              'font-size': '12px',
              color: '#F5F5F7',
              background: '#ffffff06',
              border: '1px solid #ffffff0a',
              'border-radius': '8px',
            }}
          />
          <Show when={props.search()}>
            <button
              type="button"
              onClick={() => props.onSearchChange('')}
              class="absolute right-1.5 top-1/2 -translate-y-1/2 p-0.5"
              style={{ color: '#48484A', 'border-radius': '4px' }}
            >
              <X class="w-3 h-3" />
            </button>
          </Show>
        </div>

        {/* Search results dropdown */}
        <Show when={searchResults().length > 0}>
          <div
            class="absolute left-0 right-3 mt-1 overflow-hidden z-10"
            style={{
              'background-color': '#111114',
              border: '1px solid #ffffff08',
              'border-radius': '8px',
            }}
          >
            <For each={searchResults()}>
              {(entry) => (
                <button
                  type="button"
                  onClick={() => handleResultClick(entry)}
                  class="w-full text-left px-3 py-2 flex flex-col gap-0.5 transition-colors"
                  style={{ 'border-bottom': '1px solid #ffffff06' }}
                  onMouseEnter={(e) => {
                    e.currentTarget.style.background = '#ffffff08'
                  }}
                  onMouseLeave={(e) => {
                    e.currentTarget.style.background = 'transparent'
                  }}
                >
                  <span style={{ 'font-size': '12px', color: '#F5F5F7' }}>{entry.label}</span>
                  <span style={{ 'font-size': '10px', color: '#86868B' }}>
                    {entry.tabLabel}
                    <Show when={entry.description}>
                      {' · '}
                      {entry.description}
                    </Show>
                  </span>
                </button>
              )}
            </For>
          </div>
        </Show>
      </div>

      {/* Tab groups */}
      <div
        class="settings-scroll-area flex-1 overflow-y-auto pr-3 pb-3"
        style={{ 'overscroll-behavior': 'contain', 'scrollbar-gutter': 'stable' }}
      >
        <div style={{ display: 'flex', 'flex-direction': 'column', gap: '24px' }}>
          <For each={namedGroups()}>
            {(group) => (
              <div>
                <div style={{ padding: '0 0 4px 12px' }}>
                  <span
                    style={{
                      'font-family': 'Geist Mono, monospace',
                      'font-size': '10px',
                      'font-weight': '500',
                      color: '#48484A',
                      'letter-spacing': '1px',
                      'text-transform': 'uppercase',
                    }}
                  >
                    {group.label}
                  </span>
                </div>
                <div style={{ display: 'flex', 'flex-direction': 'column', gap: '1px' }}>
                  <For each={group.tabs}>
                    {(tab) => {
                      const Icon = tab.icon
                      const isActive = (): boolean => props.activeTab() === tab.id
                      return (
                        <button
                          type="button"
                          onClick={() => props.onSelectTab(tab.id)}
                          class="w-full flex items-center"
                          style={{
                            gap: '8px',
                            padding: '0 12px',
                            height: '32px',
                            background: isActive() ? '#ffffff0c' : 'transparent',
                            color: isActive() ? '#F5F5F7' : '#48484A',
                            'font-family': 'Geist, sans-serif',
                            'font-weight': isActive() ? '500' : '400',
                            'font-size': '13px',
                            'border-radius': '6px',
                            transition: 'background 150ms, color 150ms',
                          }}
                        >
                          <Icon
                            class="flex-shrink-0"
                            style={{
                              width: '14px',
                              height: '14px',
                              color: isActive() ? '#F5F5F7' : '#48484A',
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
        </div>

        <Show when={filteredGroups().length === 0}>
          <p
            style={{
              padding: '16px 12px',
              'font-size': '12px',
              color: '#48484A',
              'text-align': 'center',
            }}
          >
            No matching settings
          </p>
        </Show>
      </div>

      {/* Footer group removed — Developer/About now render as normal groups */}
    </nav>
  )
}
