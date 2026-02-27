import { X } from 'lucide-solid'
import type { Component } from 'solid-js'
import { type SettingsTab, tabGroups } from './settings-modal-config'

interface SettingsModalHeaderProps {
  activeTab: () => SettingsTab
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
      <button
        type="button"
        onClick={props.onClose}
        class="p-1.5 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
      >
        <X class="w-4 h-4" />
      </button>
    </div>
  )
}
