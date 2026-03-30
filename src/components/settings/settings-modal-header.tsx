import type { Component } from 'solid-js'

interface SettingsModalHeaderProps {
  onClose: () => void
}

export const SettingsModalHeader: Component<SettingsModalHeaderProps> = () => {
  return (
    <div
      class="flex items-center justify-center flex-shrink-0"
      style={{ height: '40px', background: '#0A0A0C', 'border-bottom': '1px solid #ffffff06' }}
      data-tauri-drag-region
    />
  )
}
