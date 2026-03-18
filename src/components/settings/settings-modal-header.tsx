import { Search, X } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'
import type { SettingsTab } from './settings-modal-config'

interface SettingsModalHeaderProps {
  activeTab: () => SettingsTab
  onClose: () => void
  search: Accessor<string>
  onSearchChange: (value: string) => void
}

export const SettingsModalHeader: Component<SettingsModalHeaderProps> = (props) => {
  return (
    <div
      class="flex items-center justify-center flex-shrink-0 border-b border-[var(--gray-5)] relative"
      style={{ height: '40px', background: 'var(--gray-0)' }}
      data-tauri-drag-region
    >
      <span class="text-[13px] font-medium text-[var(--text-primary)]">Settings</span>

      {/* Search — right side */}
      <div class="absolute right-3 flex items-center gap-2">
        <div class="relative">
          <Search class="absolute left-2 top-1/2 -translate-y-1/2 w-3 h-3 text-[var(--gray-7)]" />
          <input
            type="text"
            placeholder="Search settings..."
            value={props.search()}
            onInput={(e) => props.onSearchChange(e.currentTarget.value)}
            class="
              w-40 pl-7 pr-7 py-1
              text-xs text-[var(--text-primary)]
              bg-[var(--gray-3)]
              border border-[var(--gray-5)]
              rounded-[var(--radius-md)]
              placeholder:text-[var(--gray-7)]
              focus:border-[var(--accent)] outline-none
            "
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
    </div>
  )
}
