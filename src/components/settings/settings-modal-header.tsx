import { Search, X } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'
import { type SettingsTab, tabGroups } from './settings-modal-config'

interface SettingsModalHeaderProps {
  activeTab: () => SettingsTab
  onClose: () => void
  search: Accessor<string>
  onSearchChange: (value: string) => void
}

export const SettingsModalHeader: Component<SettingsModalHeaderProps> = (props) => {
  const currentLabel = () =>
    tabGroups.flatMap((g) => g.tabs).find((t) => t.id === props.activeTab())?.label

  return (
    <div class="flex items-center justify-between px-5 py-3 border-b border-[var(--border-subtle)] flex-shrink-0">
      <span class="text-sm font-medium text-[var(--text-primary)] capitalize">
        {currentLabel()}
      </span>

      <div class="flex items-center gap-2">
        <div class="relative">
          <Search class="absolute left-2 top-1/2 -translate-y-1/2 w-3 h-3 text-[var(--text-muted)]" />
          <input
            type="text"
            placeholder="Search settings..."
            value={props.search()}
            onInput={(e) => props.onSearchChange(e.currentTarget.value)}
            class="
              w-40 pl-7 pr-7 py-1
              text-xs text-[var(--text-primary)]
              bg-[var(--surface-sunken)]
              border border-[var(--border-subtle)]
              rounded-[var(--radius-md)]
              placeholder:text-[var(--text-muted)]
              focus-glow
            "
          />
          <Show when={props.search()}>
            <button
              type="button"
              onClick={() => props.onSearchChange('')}
              class="absolute right-1.5 top-1/2 -translate-y-1/2 p-0.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)]"
            >
              <X class="w-3 h-3" />
            </button>
          </Show>
        </div>
        <button
          type="button"
          onClick={props.onClose}
          class="p-1.5 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
        >
          <X class="w-4 h-4" />
        </button>
      </div>
    </div>
  )
}
