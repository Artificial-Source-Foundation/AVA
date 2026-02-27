/**
 * Provider Card Status Dot
 *
 * Colored dot indicating provider connection status.
 * No label text — status is obvious from the dot color + toggle state.
 */

import type { Component } from 'solid-js'

interface ProviderCardStatusProps {
  status: 'connected' | 'disconnected' | 'error'
}

const statusColors = {
  connected: 'var(--success)',
  disconnected: 'var(--text-muted)',
  error: 'var(--error)',
}

export const ProviderCardStatus: Component<ProviderCardStatusProps> = (props) => (
  <span
    class="w-1.5 h-1.5 rounded-full flex-shrink-0"
    style={{ background: statusColors[props.status] }}
    title={props.status === 'connected' ? 'Connected' : props.status === 'error' ? 'Error' : ''}
  />
)
