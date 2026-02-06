/**
 * MCP Servers Settings Tab
 *
 * Modern 2026 aesthetic using semantic CSS tokens.
 * Manage Model Context Protocol (MCP) server connections.
 */

import {
  Activity,
  AlertTriangle,
  ChevronDown,
  ChevronRight,
  ExternalLink,
  Play,
  Plus,
  RefreshCw,
  Server,
  Settings,
  Square,
  Trash2,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'

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
// Status Configuration (uses CSS variables from tokens.css)
// ============================================================================

const statusConfig = {
  connected: {
    label: 'Connected',
    colorVar: '--success',
    bgVar: '--success-subtle',
    borderVar: '--success-border',
  },
  disconnected: {
    label: 'Offline',
    colorVar: '--text-muted',
    bgVar: '--alpha-white-5',
    borderVar: '--border-subtle',
  },
  error: {
    label: 'Error',
    colorVar: '--error',
    bgVar: '--error-subtle',
    borderVar: '--error-border',
  },
  connecting: {
    label: 'Connecting',
    colorVar: '--warning',
    bgVar: '--warning-subtle',
    borderVar: '--warning-border',
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
          <h3 class="text-[var(--text-lg)] font-semibold text-[var(--text-primary)]">
            MCP Servers
          </h3>
          <p class="text-[var(--text-xs)] text-[var(--text-tertiary)] mt-[var(--space-0_5)]">
            {connectedCount()} of {props.servers.length} connected
          </p>
        </div>
        <div class="flex items-center gap-[var(--space-2)]">
          <Show when={props.onRefresh}>
            <button
              type="button"
              onClick={() => props.onRefresh?.()}
              class="
                p-[var(--space-2)] rounded-[var(--radius-lg)]
                text-[var(--text-muted)]
                hover:text-[var(--text-primary)]
                hover:bg-[var(--button-ghost-hover)]
                transition-colors duration-[var(--duration-fast)]
              "
              title="Refresh servers"
            >
              <RefreshCw class="w-4 h-4" />
            </button>
          </Show>
          <Show when={props.onAdd}>
            <button
              type="button"
              onClick={() => props.onAdd?.()}
              class="
                flex items-center gap-[var(--space-1_5)] px-[var(--space-3)] py-[var(--space-1_5)]
                bg-[var(--button-primary-bg)] hover:bg-[var(--button-primary-hover)]
                text-[var(--button-primary-text)] text-[var(--text-sm)] font-medium
                rounded-[var(--radius-lg)]
                transition-colors duration-[var(--duration-fast)]
              "
            >
              <Plus class="w-4 h-4" />
              Add Server
            </button>
          </Show>
        </div>
      </div>

      {/* Server List */}
      <div class="space-y-[var(--space-3)]">
        <Show
          when={props.servers.length > 0}
          fallback={
            <div class="py-[var(--space-12)] text-center">
              <div class="w-12 h-12 mx-auto mb-[var(--space-3)] rounded-full bg-[var(--alpha-white-5)] flex items-center justify-center">
                <Server class="w-6 h-6 text-[var(--text-muted)]" />
              </div>
              <p class="text-[var(--text-sm)] text-[var(--text-secondary)]">
                No MCP servers configured
              </p>
              <p class="text-[var(--text-xs)] text-[var(--text-muted)] mt-[var(--space-1)]">
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

      {/* Info Banner */}
      <div
        class="
          flex items-start gap-[var(--space-3)] p-[var(--space-4)]
          bg-[var(--accent-subtle)]
          border border-[var(--accent-border)]
          rounded-[var(--radius-xl)]
        "
      >
        <div class="w-8 h-8 rounded-full bg-[var(--accent-subtle)] flex items-center justify-center flex-shrink-0">
          <Server class="w-4 h-4 text-[var(--accent)]" />
        </div>
        <div class="text-[var(--text-sm)] text-[var(--text-secondary)] leading-relaxed">
          <p>
            MCP servers extend Estela's capabilities with additional tools, data sources, and
            integrations.
          </p>
          <a
            href="https://modelcontextprotocol.io"
            target="_blank"
            rel="noopener noreferrer"
            class="
              inline-flex items-center gap-[var(--space-1)] mt-[var(--space-2)]
              text-[var(--accent)] hover:text-[var(--accent-hover)]
              transition-colors duration-[var(--duration-fast)]
            "
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

  const getCardClasses = () => {
    switch (props.server.status) {
      case 'connected':
        return 'border-[var(--success-border)] bg-[var(--success-subtle)]'
      case 'error':
        return 'border-[var(--error-border)] bg-[var(--error-subtle)]'
      default:
        return 'border-[var(--card-border)] bg-[var(--card-background)] hover:border-[var(--card-hover-border)]'
    }
  }

  return (
    <div
      class={`
        relative overflow-hidden
        rounded-[var(--radius-xl)]
        border transition-colors duration-[var(--duration-fast)]
        ${getCardClasses()}
      `}
    >
      {/* Main Row */}
      <div class="flex items-center gap-[var(--space-3)] p-[var(--space-3)]">
        {/* Status Icon */}
        <div
          class="p-[var(--space-2_5)] rounded-[var(--radius-lg)] transition-colors duration-[var(--duration-fast)]"
          style={{ background: `var(${status().bgVar})` }}
        >
          <Server class="w-4 h-4" style={{ color: `var(${status().colorVar})` }} />
        </div>

        {/* Info */}
        <div class="flex-1 min-w-0">
          <div class="flex items-center gap-[var(--space-2)]">
            <span class="text-[var(--text-sm)] font-medium text-[var(--text-primary)]">
              {props.server.name}
            </span>
            {/* Status dot */}
            <div
              class={`w-2 h-2 rounded-full ${props.server.status === 'connecting' ? 'animate-pulse' : ''}`}
              style={{ background: `var(${status().colorVar})` }}
            />
            <span class="text-[var(--text-xs)] text-[var(--text-muted)]">{status().label}</span>
          </div>
          <div class="text-[var(--text-xs)] text-[var(--text-muted)] truncate mt-[var(--space-0_5)] font-[var(--font-mono)]">
            {props.server.url}
          </div>
        </div>

        {/* Actions */}
        <div class="flex items-center gap-[var(--space-1)]">
          <Show when={props.server.status === 'connected' && props.onDisconnect}>
            <button
              type="button"
              onClick={() => props.onDisconnect?.()}
              class="
                p-[var(--space-1_5)] rounded-[var(--radius-md)]
                text-[var(--text-muted)]
                hover:text-[var(--error)]
                hover:bg-[var(--error-subtle)]
                transition-colors duration-[var(--duration-fast)]
              "
              title="Disconnect"
            >
              <Square class="w-4 h-4" />
            </button>
          </Show>
          <Show
            when={
              (props.server.status === 'disconnected' || props.server.status === 'error') &&
              props.onConnect
            }
          >
            <button
              type="button"
              onClick={() => props.onConnect?.()}
              class="
                p-[var(--space-1_5)] rounded-[var(--radius-md)]
                text-[var(--text-muted)]
                hover:text-[var(--success)]
                hover:bg-[var(--success-subtle)]
                transition-colors duration-[var(--duration-fast)]
              "
              title="Connect"
            >
              <Play class="w-4 h-4" />
            </button>
          </Show>
          <Show when={props.server.status === 'connecting'}>
            <div class="p-[var(--space-1_5)]">
              <Activity class="w-4 h-4 text-[var(--warning)] animate-pulse" />
            </div>
          </Show>
          <Show when={props.onEdit}>
            <button
              type="button"
              onClick={() => props.onEdit?.()}
              class="
                p-[var(--space-1_5)] rounded-[var(--radius-md)]
                text-[var(--text-muted)]
                hover:text-[var(--text-primary)]
                hover:bg-[var(--button-ghost-hover)]
                transition-colors duration-[var(--duration-fast)]
              "
              title="Settings"
            >
              <Settings class="w-4 h-4" />
            </button>
          </Show>
          <Show when={props.onRemove}>
            <button
              type="button"
              onClick={() => props.onRemove?.()}
              class="
                p-[var(--space-1_5)] rounded-[var(--radius-md)]
                text-[var(--text-muted)]
                hover:text-[var(--error)]
                hover:bg-[var(--error-subtle)]
                transition-colors duration-[var(--duration-fast)]
              "
              title="Remove"
            >
              <Trash2 class="w-4 h-4" />
            </button>
          </Show>
          <ChevronRight class="w-4 h-4 text-[var(--text-muted)] ml-[var(--space-1)]" />
        </div>
      </div>

      {/* Error Message */}
      <Show when={props.server.status === 'error' && props.server.error}>
        <div class="px-[var(--space-4)] py-[var(--space-2)] bg-[var(--error-subtle)] border-t border-[var(--error-border)]">
          <div class="flex items-center gap-[var(--space-2)]">
            <AlertTriangle class="w-3.5 h-3.5 text-[var(--error)] flex-shrink-0" />
            <p class="text-[var(--text-xs)] text-[var(--error)]">{props.server.error}</p>
          </div>
        </div>
      </Show>

      {/* Capabilities (expandable) */}
      <Show when={props.server.capabilities && props.server.capabilities.length > 0}>
        <button
          type="button"
          onClick={() => setExpanded(!expanded())}
          class="
            w-full px-[var(--space-4)] py-[var(--space-2)] text-left text-[var(--text-xs)]
            text-[var(--text-muted)]
            hover:bg-[var(--alpha-white-3)]
            border-t border-[var(--border-subtle)]
            transition-colors duration-[var(--duration-fast)]
            flex items-center justify-between
          "
        >
          <span>{props.server.capabilities!.length} capabilities</span>
          <ChevronDown
            class={`w-3.5 h-3.5 transition-transform duration-[var(--duration-fast)] ${expanded() ? 'rotate-180' : ''}`}
          />
        </button>
        <Show when={expanded()}>
          <div class="px-[var(--space-4)] py-[var(--space-3)] border-t border-[var(--border-subtle)] bg-[var(--alpha-white-3)]">
            <div class="flex flex-wrap gap-[var(--space-1_5)]">
              <For each={props.server.capabilities}>
                {(cap) => (
                  <span
                    class="
                      px-[var(--space-2)] py-[var(--space-1)] text-[var(--text-xs)]
                      bg-[var(--alpha-white-5)]
                      text-[var(--text-secondary)]
                      rounded-full
                      border border-[var(--border-subtle)]
                    "
                  >
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
