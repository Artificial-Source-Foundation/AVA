import type { Component } from 'solid-js'

interface SettingsModalHeaderProps {
  onClose: () => void
}

export const SettingsModalHeader: Component<SettingsModalHeaderProps> = () => {
  return (
    <div
      class="flex items-center justify-center flex-shrink-0 border-b border-[var(--gray-5)]"
      style={{ height: '40px', background: 'var(--gray-0)' }}
      data-tauri-drag-region
    >
      <span class="text-[var(--settings-text-description)] font-medium text-[var(--text-primary)]">
        Settings
      </span>
    </div>
  )
}
