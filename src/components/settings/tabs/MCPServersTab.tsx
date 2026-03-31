/**
 * MCP Servers Settings Tab
 *
 * Pencil macOS-inspired design with server cards, status dots, and action buttons.
 */

import { Plus, RefreshCw, Trash2 } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { MCPOAuthDialog } from '../../dialogs/MCPOAuthDialog'

// ============================================================================
// Types
// ============================================================================

export interface MCPServer {
  id: string
  name: string
  url: string
  status: 'connected' | 'disconnected' | 'error' | 'connecting' | 'disabled'
  enabled: boolean
  scope?: 'global' | 'local'
  toolCount?: number
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
  onToggle?: (name: string, enabled: boolean) => void
  onRefresh?: () => void
}

// ============================================================================
// Status helpers
// ============================================================================

const statusLabel: Record<MCPServer['status'], string> = {
  connected: 'Connected',
  disconnected: 'Disconnected',
  error: 'Error',
  connecting: 'Connecting',
  disabled: 'Disabled',
}

const statusDotColor: Record<MCPServer['status'], string> = {
  connected: '#34C759',
  disconnected: '#48484A',
  error: '#FF453A',
  connecting: '#F5A623',
  disabled: '#636366',
}

const statusTextColor: Record<MCPServer['status'], string> = {
  connected: '#34C759',
  disconnected: '#48484A',
  error: '#FF453A',
  connecting: '#F5A623',
  disabled: '#636366',
}

// ============================================================================
// MCP Servers Tab Component
// ============================================================================

export const MCPServersTab: Component<MCPServersTabProps> = (props) => {
  const connectedCount = () => props.servers.filter((s) => s.status === 'connected').length
  const [oauthTarget, setOauthTarget] = createSignal<string | null>(null)

  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: '24px' }}>
      {/* Title row */}
      <div class="flex items-center justify-between" style={{ width: '100%' }}>
        <div style={{ display: 'flex', 'flex-direction': 'column', gap: '4px' }}>
          <h1
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '22px',
              'font-weight': '600',
              color: '#F5F5F7',
            }}
          >
            MCP Servers
          </h1>
          <span
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '13px',
              color: '#48484A',
            }}
          >
            {connectedCount()} of {props.servers.length} connected
          </span>
        </div>
        <div class="flex items-center" style={{ gap: '8px' }}>
          <Show when={props.onRefresh}>
            <button
              type="button"
              onClick={() => props.onRefresh?.()}
              class="flex items-center justify-center transition-colors"
              style={{
                width: '32px',
                height: '32px',
                'border-radius': '8px',
                border: '1px solid #ffffff0a',
                color: '#48484A',
                background: 'transparent',
              }}
            >
              <RefreshCw style={{ width: '14px', height: '14px' }} />
            </button>
          </Show>
          <Show when={props.onAdd}>
            <button
              type="button"
              onClick={() => props.onAdd?.()}
              class="flex items-center justify-center transition-colors"
              style={{
                gap: '6px',
                height: '32px',
                padding: '0 14px',
                'border-radius': '8px',
                background: '#0A84FF',
                color: '#FFFFFF',
                'font-family': 'Geist, sans-serif',
                'font-size': '13px',
                'font-weight': '500',
                border: 'none',
              }}
            >
              <Plus style={{ width: '14px', height: '14px' }} />
              Add Server
            </button>
          </Show>
        </div>
      </div>

      {/* Server List */}
      <Show
        when={props.servers.length > 0}
        fallback={
          <p
            style={{
              'font-size': '13px',
              color: '#48484A',
              'text-align': 'center',
              padding: '24px 0',
            }}
          >
            No MCP servers configured. MCP servers extend AVA with external tools.
          </p>
        }
      >
        <div style={{ display: 'flex', 'flex-direction': 'column', gap: '12px' }}>
          <For each={props.servers}>
            {(server) => (
              <div
                style={{
                  'border-radius': '12px',
                  background: '#111114',
                  border: `1px solid ${server.status === 'error' ? '#FF453A18' : '#ffffff08'}`,
                  padding: '16px',
                  display: 'flex',
                  'flex-direction': 'column',
                  gap: '10px',
                }}
              >
                <div class="flex items-center justify-between" style={{ width: '100%' }}>
                  <div style={{ display: 'flex', 'flex-direction': 'column', gap: '4px' }}>
                    <div class="flex items-center" style={{ gap: '8px' }}>
                      <span
                        style={{
                          'font-family': 'Geist, sans-serif',
                          'font-size': '13px',
                          'font-weight': '500',
                          color: server.enabled ? '#F5F5F7' : '#636366',
                        }}
                      >
                        {server.name}
                      </span>
                      <Show when={server.scope}>
                        <span
                          style={{
                            'font-family': 'Geist Mono, monospace',
                            'font-size': '10px',
                            color: '#48484A',
                            background: '#ffffff08',
                            padding: '1px 6px',
                            'border-radius': '4px',
                          }}
                        >
                          {server.scope}
                        </span>
                      </Show>
                      <Show when={server.toolCount && server.toolCount > 0}>
                        <span
                          style={{
                            'font-family': 'Geist Mono, monospace',
                            'font-size': '10px',
                            color: '#34C759',
                          }}
                        >
                          {server.toolCount} tools
                        </span>
                      </Show>
                    </div>
                    <Show when={server.url}>
                      <span
                        style={{
                          'font-family': 'Geist Mono, monospace',
                          'font-size': '11px',
                          color: '#48484A',
                        }}
                      >
                        {server.url}
                      </span>
                    </Show>
                  </div>
                  <div class="flex items-center" style={{ gap: '12px' }}>
                    {/* Status */}
                    <div class="flex items-center" style={{ gap: '6px' }}>
                      <div
                        style={{
                          width: '6px',
                          height: '6px',
                          'border-radius': '50%',
                          background: statusDotColor[server.status],
                        }}
                      />
                      <span
                        style={{
                          'font-family': 'Geist, sans-serif',
                          'font-size': '11px',
                          color: statusTextColor[server.status],
                        }}
                      >
                        {statusLabel[server.status]}
                      </span>
                    </div>
                    {/* Toggle */}
                    <Show when={props.onToggle}>
                      <button
                        type="button"
                        onClick={() => props.onToggle?.(server.name, !server.enabled)}
                        style={{
                          width: '36px',
                          height: '20px',
                          'border-radius': '10px',
                          background: server.enabled ? '#0A84FF' : '#38383A',
                          border: 'none',
                          cursor: 'pointer',
                          position: 'relative',
                          transition: 'background 0.2s',
                          'flex-shrink': '0',
                        }}
                      >
                        <div
                          style={{
                            width: '16px',
                            height: '16px',
                            'border-radius': '50%',
                            background: '#FFFFFF',
                            position: 'absolute',
                            top: '2px',
                            left: server.enabled ? '18px' : '2px',
                            transition: 'left 0.2s',
                          }}
                        />
                      </button>
                    </Show>
                    {/* Actions */}
                    <div class="flex items-center" style={{ gap: '4px' }}>
                      <Show when={props.onRemove}>
                        <button
                          type="button"
                          onClick={() => props.onRemove?.(server.id)}
                          class="transition-colors"
                          style={{
                            color: '#48484A',
                            background: 'transparent',
                            border: 'none',
                            padding: '4px',
                            cursor: 'pointer',
                          }}
                        >
                          <Trash2 style={{ width: '14px', height: '14px' }} />
                        </button>
                      </Show>
                    </div>
                  </div>
                </div>
                {/* Error message */}
                <Show when={server.status === 'error' && server.error}>
                  <span
                    style={{
                      'font-family': 'Geist Mono, monospace',
                      'font-size': '11px',
                      color: '#FF453A',
                      'word-break': 'break-all',
                    }}
                  >
                    {server.error}
                  </span>
                </Show>
              </div>
            )}
          </For>
        </div>
      </Show>

      {/* OAuth Dialog */}
      <MCPOAuthDialog
        open={oauthTarget() !== null}
        serverName={oauthTarget() ?? ''}
        authUrl=""
        clientId=""
        scopes={['read', 'write']}
        redirectUri="http://localhost:1420/oauth/callback"
        onClose={() => setOauthTarget(null)}
        onAuthorized={() => {
          setOauthTarget(null)
        }}
      />
    </div>
  )
}
