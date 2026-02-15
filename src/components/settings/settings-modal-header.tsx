import { Check, X } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { type SettingsTab, tabGroups } from './settings-modal-config'

interface SettingsModalHeaderProps {
  activeTab: () => SettingsTab
  saveStatus: () => 'idle' | 'saved'
  onSave: () => void
  onClose: () => void
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
        <Show when={props.activeTab() === 'providers'}>
          <button
            type="button"
            onClick={props.onSave}
            disabled={props.saveStatus() === 'saved'}
            class={`flex items-center gap-1.5 px-3 py-1.5 rounded-[var(--radius-md)] text-xs font-medium transition-colors duration-[var(--duration-fast)] ${props.saveStatus() === 'saved' ? 'bg-[var(--success)] text-white' : 'bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white'}`}
          >
            <Show when={props.saveStatus() === 'saved'} fallback="Save">
              <Check class="w-3 h-3" />
              Saved
            </Show>
          </button>
        </Show>
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
