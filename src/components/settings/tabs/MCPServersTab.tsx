/**
 * MCP Servers Settings Tab
 *
 * Flat, minimal design matching GeneralSection.
 * Manage Model Context Protocol (MCP) server connections.
 */

import { type Component, For, Show } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

export interface MCPServer {
  id: string
  name: string
  url: string
  status: 'connected' | 'disconnected' | 'error' | 'connecting'
  description?: string
  capabilities?: string[]
  lastConnected?: Date
  error?: string
}

export interface MCPServersTabProps {
  servers: MCPServer[]
  onAdd?: () => void
  onEdit?: (id: string) => void
  onRemove?: (id: string) => void
  onConnect?: (id: string) => void
  onDisconnect?: (id: string) => void
  onRefresh?: () => void
}

// ============================================================================
// Status helpers
// ============================================================================

const statusLabel: Record<MCPServer['status'], string> = {
  connected: 'connected',
  disconnected: 'offline',
  error: 'error',
  connecting: 'connecting',
}

const statusColor: Record<MCPServer['status'], string> = {
  connected: 'var(--success)',
  disconnected: 'var(--text-muted)',
  error: 'var(--error)',
  connecting: 'var(--warning)',
}

// ============================================================================
// MCP Servers Tab Component
// ============================================================================

export const MCPServersTab: Component<MCPServersTabProps> = (props) => {
  const connectedCount = () => props.servers.filter((s) => s.status === 'connected').length

  return (
    <div class="space-y-4">
      {/* Header */}
      <div class="flex items-center justify-between">
        <p class="text-[10px] text-[var(--text-muted)]">
          {connectedCount()} of {props.servers.length} connected
        </p>
        <div class="flex items-center gap-2">
          <Show when={props.onRefresh}>
            <button
              type="button"
              onClick={() => props.onRefresh?.()}
              class="text-[10px] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
            >
              Refresh
            </button>
          </Show>
          <Show when={props.onAdd}>
            <button
              type="button"
              onClick={() => props.onAdd?.()}
              class="px-2.5 py-1.5 text-[11px] font-medium text-[var(--accent)] hover:bg-[var(--accent-subtle)] rounded-[var(--radius-md)] transition-colors"
            >
              + Add
            </button>
          </Show>
        </div>
      </div>

      {/* Server List */}
      <Show
        when={props.servers.length > 0}
        fallback={
          <p class="text-xs text-[var(--text-muted)] text-center py-6">No MCP servers configured</p>
        }
      >
        <div class="space-y-0.5">
          <For each={props.servers}>
            {(server) => (
              <div class="flex items-center justify-between py-2 group">
                <div class="flex-1 min-w-0">
                  <div class="flex items-center gap-1.5">
                    <span class="text-xs text-[var(--text-secondary)]">{server.name}</span>
                    <span
                      class={`w-1.5 h-1.5 rounded-full ${server.status === 'connecting' ? 'animate-pulse' : ''}`}
                      style={{ background: statusColor[server.status] }}
                    />
                    <span class="text-[9px]" style={{ color: statusColor[server.status] }}>
                      {statusLabel[server.status]}
                    </span>
                  </div>
                  <p class="text-[10px] text-[var(--text-muted)] font-mono truncate">
                    {server.url}
                  </p>
                  <Show when={server.status === 'error' && server.error}>
                    <p class="text-[10px] text-[var(--error)]">{server.error}</p>
                  </Show>
                </div>
                <div class="flex items-center gap-1.5">
                  <Show when={server.status === 'connected' && props.onDisconnect}>
                    <button
                      type="button"
                      onClick={() => props.onDisconnect?.(server.id)}
                      class="text-[10px] text-[var(--text-muted)] hover:text-[var(--error)] opacity-0 group-hover:opacity-100 transition-all"
                    >
                      Stop
                    </button>
                  </Show>
                  <Show
                    when={
                      (server.status === 'disconnected' || server.status === 'error') &&
                      props.onConnect
                    }
                  >
                    <button
                      type="button"
                      onClick={() => props.onConnect?.(server.id)}
                      class="text-[10px] text-[var(--text-muted)] hover:text-[var(--success)] opacity-0 group-hover:opacity-100 transition-all"
                    >
                      Start
                    </button>
                  </Show>
                  <Show when={props.onEdit}>
                    <button
                      type="button"
                      onClick={() => props.onEdit?.(server.id)}
                      class="text-[10px] text-[var(--text-muted)] hover:text-[var(--accent)] opacity-0 group-hover:opacity-100 transition-all"
                    >
                      Edit
                    </button>
                  </Show>
                  <Show when={props.onRemove}>
                    <button
                      type="button"
                      onClick={() => props.onRemove?.(server.id)}
                      class="text-[10px] text-[var(--text-muted)] hover:text-[var(--error)] opacity-0 group-hover:opacity-100 transition-all"
                    >
                      Remove
                    </button>
                  </Show>
                </div>
              </div>
            )}
          </For>
        </div>
      </Show>
    </div>
  )
}
