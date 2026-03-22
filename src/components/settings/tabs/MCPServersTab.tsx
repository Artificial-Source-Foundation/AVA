/**
 * MCP Servers Settings Tab
 *
 * Flat, minimal design using SettingsCard design system.
 * Manage Model Context Protocol (MCP) server connections.
 */

import { Server } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { getOAuthStatus, type OAuthStatus, revokeAuth } from '../../../services/mcp-oauth'
import { MCPOAuthDialog } from '../../dialogs/MCPOAuthDialog'
import { SettingsCard } from '../SettingsCard'
import { SETTINGS_CARD_GAP } from '../settings-constants'

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

const oauthStatusLabel: Record<OAuthStatus, string> = {
  none: '',
  authorized: 'Authorized',
  expired: 'Token Expired',
  error: 'Auth Error',
}

const oauthStatusColor: Record<OAuthStatus, string> = {
  none: 'var(--text-muted)',
  authorized: 'var(--success)',
  expired: 'var(--warning)',
  error: 'var(--error)',
}

export const MCPServersTab: Component<MCPServersTabProps> = (props) => {
  const connectedCount = () => props.servers.filter((s) => s.status === 'connected').length
  const [oauthTarget, setOauthTarget] = createSignal<string | null>(null)
  const [, setOauthRefresh] = createSignal(0)

  const serverOAuthStatus = (name: string): OAuthStatus => {
    // Force reactivity
    void setOauthRefresh
    return getOAuthStatus(name)
  }

  const handleRevoke = (name: string) => {
    revokeAuth(name)
    setOauthRefresh((v) => v + 1)
  }

  return (
    <div class="grid grid-cols-1" style={{ gap: SETTINGS_CARD_GAP }}>
      <SettingsCard
        icon={Server}
        title="MCP Servers"
        description={`${connectedCount()} of ${props.servers.length} connected`}
      >
        {/* Actions */}
        <div class="flex items-center justify-end gap-2">
          <Show when={props.onRefresh}>
            <button
              type="button"
              onClick={() => props.onRefresh?.()}
              class="text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
            >
              Refresh
            </button>
          </Show>
          <Show when={props.onAdd}>
            <button
              type="button"
              onClick={() => props.onAdd?.()}
              class="px-2.5 py-1.5 text-[var(--settings-text-button)] font-medium text-[var(--accent)] hover:bg-[var(--accent-subtle)] rounded-[var(--radius-md)] transition-colors"
            >
              + Add
            </button>
          </Show>
        </div>

        {/* Server List */}
        <Show
          when={props.servers.length > 0}
          fallback={
            <p class="text-[var(--settings-text-description)] text-[var(--text-muted)] text-center py-6">
              No MCP servers configured. MCP servers extend AVA with external tools.
            </p>
          }
        >
          <div class="space-y-0.5">
            <For each={props.servers}>
              {(server) => (
                <div class="flex items-center justify-between py-2 group">
                  <div class="flex-1 min-w-0">
                    <div class="flex items-center gap-1.5">
                      <span class="text-[var(--settings-text-label)] text-[var(--text-secondary)]">
                        {server.name}
                      </span>
                      <span
                        class={`w-1.5 h-1.5 rounded-full ${server.status === 'connecting' ? 'animate-pulse' : ''}`}
                        style={{ background: statusColor[server.status] }}
                      />
                      <span
                        class="text-[var(--settings-text-caption)]"
                        style={{ color: statusColor[server.status] }}
                      >
                        {statusLabel[server.status]}
                      </span>
                    </div>
                    <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)] font-mono truncate">
                      {server.url}
                    </p>
                    <Show when={server.status === 'error' && server.error}>
                      <p class="text-[var(--settings-text-badge)] text-[var(--error)]">
                        {server.error}
                      </p>
                    </Show>
                  </div>
                  <div class="flex items-center gap-1.5">
                    {/* OAuth status */}
                    <Show when={serverOAuthStatus(server.name) !== 'none'}>
                      <span
                        class="text-[var(--settings-text-caption)]"
                        style={{ color: oauthStatusColor[serverOAuthStatus(server.name)] }}
                      >
                        {oauthStatusLabel[serverOAuthStatus(server.name)]}
                      </span>
                    </Show>
                    <Show
                      when={serverOAuthStatus(server.name) === 'authorized'}
                      fallback={
                        <button
                          type="button"
                          onClick={() => setOauthTarget(server.name)}
                          class="text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--accent)] opacity-0 group-hover:opacity-100 transition-[color,opacity]"
                        >
                          Authorize
                        </button>
                      }
                    >
                      <button
                        type="button"
                        onClick={() => handleRevoke(server.name)}
                        class="text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--warning)] opacity-0 group-hover:opacity-100 transition-[color,opacity]"
                      >
                        Revoke
                      </button>
                    </Show>
                    <Show when={server.status === 'connected' && props.onDisconnect}>
                      <button
                        type="button"
                        onClick={() => props.onDisconnect?.(server.id)}
                        class="text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--error)] opacity-0 group-hover:opacity-100 transition-[color,opacity]"
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
                        class="text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--success)] opacity-0 group-hover:opacity-100 transition-[color,opacity]"
                      >
                        Start
                      </button>
                    </Show>
                    <Show when={props.onEdit}>
                      <button
                        type="button"
                        onClick={() => props.onEdit?.(server.id)}
                        class="text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--accent)] opacity-0 group-hover:opacity-100 transition-[color,opacity]"
                      >
                        Edit
                      </button>
                    </Show>
                    <Show when={props.onRemove}>
                      <button
                        type="button"
                        onClick={() => props.onRemove?.(server.id)}
                        class="text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--error)] opacity-0 group-hover:opacity-100 transition-[color,opacity]"
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
      </SettingsCard>

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
          setOauthRefresh((v) => v + 1)
        }}
      />
    </div>
  )
}
