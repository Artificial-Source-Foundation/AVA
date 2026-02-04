/**
 * MCP Servers Settings Tab
 *
 * Manage Model Context Protocol (MCP) server connections.
 */

import {
  Activity,
  AlertTriangle,
  Check,
  Circle,
  ExternalLink,
  Plus,
  RefreshCw,
  Server,
  Settings,
  Trash2,
  X,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { Button } from '../../ui/Button'

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
// Status Config
// ============================================================================

const statusConfig = {
  connected: {
    label: 'Connected',
    color: 'var(--success)',
    bg: 'var(--success-subtle)',
    icon: Check,
  },
  disconnected: {
    label: 'Disconnected',
    color: 'var(--text-muted)',
    bg: 'var(--surface-sunken)',
    icon: Circle,
  },
  error: {
    label: 'Error',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
    icon: AlertTriangle,
  },
  connecting: {
    label: 'Connecting',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
    icon: Activity,
  },
}

// ============================================================================
// MCP Servers Tab Component
// ============================================================================

export const MCPServersTab: Component<MCPServersTabProps> = (props) => {
  const connectedCount = () => props.servers.filter((s) => s.status === 'connected').length

  return (
    <div class="space-y-6">
      {/* Header */}
      <div class="flex items-center justify-between">
        <div>
          <h3 class="text-sm font-medium text-[var(--text-primary)]">MCP Servers</h3>
          <p class="text-xs text-[var(--text-muted)] mt-0.5">
            {connectedCount()} of {props.servers.length} connected
          </p>
        </div>
        <div class="flex items-center gap-2">
          <Show when={props.onRefresh}>
            <Button variant="ghost" size="sm" onClick={props.onRefresh}>
              <RefreshCw class="w-4 h-4" />
            </Button>
          </Show>
          <Show when={props.onAdd}>
            <Button variant="primary" size="sm" onClick={props.onAdd}>
              <Plus class="w-4 h-4 mr-1" />
              Add Server
            </Button>
          </Show>
        </div>
      </div>

      {/* Server List */}
      <div class="space-y-3">
        <Show
          when={props.servers.length > 0}
          fallback={
            <div class="py-8 text-center">
              <Server class="w-10 h-10 mx-auto mb-3 text-[var(--text-muted)]" />
              <p class="text-sm text-[var(--text-secondary)]">No MCP servers configured</p>
              <p class="text-xs text-[var(--text-muted)] mt-1">
                Add a server to enable extended capabilities
              </p>
            </div>
          }
        >
          <For each={props.servers}>
            {(server) => (
              <ServerCard
                server={server}
                onEdit={() => props.onEdit?.(server.id)}
                onRemove={() => props.onRemove?.(server.id)}
                onConnect={() => props.onConnect?.(server.id)}
                onDisconnect={() => props.onDisconnect?.(server.id)}
              />
            )}
          </For>
        </Show>
      </div>

      {/* Info */}
      <div class="flex items-start gap-3 p-3 bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-lg)]">
        <Server class="w-5 h-5 text-[var(--info)] flex-shrink-0 mt-0.5" />
        <div class="text-sm text-[var(--text-secondary)]">
          <p>
            MCP servers extend Estela's capabilities with additional tools, data sources, and
            integrations.
          </p>
          <a
            href="https://modelcontextprotocol.io"
            target="_blank"
            rel="noopener noreferrer"
            class="inline-flex items-center gap-1 mt-1 text-[var(--accent)] hover:underline"
          >
            Learn more about MCP
            <ExternalLink class="w-3 h-3" />
          </a>
        </div>
      </div>
    </div>
  )
}

// ============================================================================
// Server Card Component
// ============================================================================

interface ServerCardProps {
  server: MCPServer
  onEdit?: () => void
  onRemove?: () => void
  onConnect?: () => void
  onDisconnect?: () => void
}

const ServerCard: Component<ServerCardProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)
  const status = () => statusConfig[props.server.status]

  return (
    <div class="border border-[var(--border-subtle)] rounded-[var(--radius-lg)] overflow-hidden">
      {/* Main Row */}
      <div class="flex items-center gap-3 p-3 bg-[var(--surface-raised)]">
        {/* Status Indicator */}
        <div class="p-2 rounded-[var(--radius-md)]" style={{ background: status().bg }}>
          <Server class="w-4 h-4" style={{ color: status().color }} />
        </div>

        {/* Info */}
        <div class="flex-1 min-w-0">
          <div class="flex items-center gap-2">
            <span class="text-sm font-medium text-[var(--text-primary)]">{props.server.name}</span>
            <span
              class="flex items-center gap-1 px-1.5 py-0.5 text-[10px] font-medium rounded-full"
              style={{ background: status().bg, color: status().color }}
            >
              <Dynamic component={status().icon} class="w-2.5 h-2.5" />
              {status().label}
            </span>
          </div>
          <div class="text-xs text-[var(--text-muted)] truncate">{props.server.url}</div>
        </div>

        {/* Actions */}
        <div class="flex items-center gap-1">
          <Show when={props.server.status === 'connected'}>
            <Button variant="ghost" size="sm" onClick={props.onDisconnect}>
              <X class="w-4 h-4" />
            </Button>
          </Show>
          <Show when={props.server.status === 'disconnected' || props.server.status === 'error'}>
            <Button variant="ghost" size="sm" onClick={props.onConnect}>
              <Activity class="w-4 h-4" />
            </Button>
          </Show>
          <Show when={props.onEdit}>
            <Button variant="ghost" size="sm" onClick={props.onEdit}>
              <Settings class="w-4 h-4" />
            </Button>
          </Show>
          <Show when={props.onRemove}>
            <Button variant="ghost" size="sm" onClick={props.onRemove}>
              <Trash2 class="w-4 h-4 text-[var(--error)]" />
            </Button>
          </Show>
        </div>
      </div>

      {/* Error Message */}
      <Show when={props.server.status === 'error' && props.server.error}>
        <div class="px-3 py-2 bg-[var(--error-subtle)] border-t border-[var(--error)]">
          <p class="text-xs text-[var(--error)]">{props.server.error}</p>
        </div>
      </Show>

      {/* Capabilities (expandable) */}
      <Show when={props.server.capabilities && props.server.capabilities.length > 0}>
        <button
          type="button"
          onClick={() => setExpanded(!expanded())}
          class="w-full px-3 py-2 text-left text-xs text-[var(--text-muted)] hover:bg-[var(--surface-sunken)] border-t border-[var(--border-subtle)]"
        >
          {expanded() ? 'Hide' : 'Show'} {props.server.capabilities!.length} capabilities
        </button>
        <Show when={expanded()}>
          <div class="px-3 py-2 border-t border-[var(--border-subtle)] bg-[var(--surface-sunken)]">
            <div class="flex flex-wrap gap-1">
              <For each={props.server.capabilities}>
                {(cap) => (
                  <span class="px-2 py-0.5 text-xs bg-[var(--surface-raised)] text-[var(--text-secondary)] rounded-full">
                    {cap}
                  </span>
                )}
              </For>
            </div>
          </div>
        </Show>
      </Show>
    </div>
  )
}

// ============================================================================
// Default/Example Servers
// ============================================================================

export const defaultMCPServers: MCPServer[] = [
  {
    id: 'filesystem',
    name: 'Filesystem',
    url: 'mcp://localhost:3001/filesystem',
    status: 'connected',
    description: 'File system operations',
    capabilities: ['read', 'write', 'list', 'search'],
  },
  {
    id: 'git',
    name: 'Git',
    url: 'mcp://localhost:3002/git',
    status: 'connected',
    description: 'Git repository operations',
    capabilities: ['status', 'diff', 'commit', 'branch'],
  },
  {
    id: 'web',
    name: 'Web Browser',
    url: 'mcp://localhost:3003/web',
    status: 'disconnected',
    description: 'Web browsing and scraping',
    capabilities: ['fetch', 'screenshot', 'extract'],
  },
]
