/**
 * MCP Servers Settings Tab
 *
 * Modern 2026 aesthetic with glass effects, status dots, and smooth animations.
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
// Status Configuration (2026 Design System)
// ============================================================================

const statusConfig = {
  connected: {
    label: 'Connected',
    color: '#22C55E',
    bgColor: 'rgba(34, 197, 94, 0.15)',
    dotClass: 'bg-[#22C55E]',
  },
  disconnected: {
    label: 'Offline',
    color: '#71717A',
    bgColor: 'rgba(113, 113, 122, 0.15)',
    dotClass: 'bg-[#71717A]',
  },
  error: {
    label: 'Error',
    color: '#EF4444',
    bgColor: 'rgba(239, 68, 68, 0.15)',
    dotClass: 'bg-[#EF4444]',
  },
  connecting: {
    label: 'Connecting',
    color: '#EAB308',
    bgColor: 'rgba(234, 179, 8, 0.15)',
    dotClass: 'bg-[#EAB308] animate-pulse',
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
          <h3 class="text-base font-semibold text-[var(--text-primary)]">MCP Servers</h3>
          <p class="text-xs text-[var(--text-tertiary)] mt-0.5">
            {connectedCount()} of {props.servers.length} connected
          </p>
        </div>
        <div class="flex items-center gap-2">
          <Show when={props.onRefresh}>
            <button
              type="button"
              onClick={props.onRefresh}
              class="
                p-2 rounded-[var(--radius-lg)]
                text-[var(--text-muted)]
                hover:text-[var(--text-primary)]
                hover:bg-[rgba(255,255,255,0.08)]
                transition-all duration-[var(--duration-fast)]
              "
              title="Refresh servers"
            >
              <RefreshCw class="w-4 h-4" />
            </button>
          </Show>
          <Show when={props.onAdd}>
            <button
              type="button"
              onClick={props.onAdd}
              class="
                flex items-center gap-1.5 px-3 py-1.5
                bg-[var(--accent)] hover:bg-[var(--accent-hover)]
                text-white text-sm font-medium
                rounded-[var(--radius-lg)]
                transition-all duration-[var(--duration-fast)]
                hover:-translate-y-0.5
              "
            >
              <Plus class="w-4 h-4" />
              Add Server
            </button>
          </Show>
        </div>
      </div>

      {/* Server List */}
      <div class="space-y-3">
        <Show
          when={props.servers.length > 0}
          fallback={
            <div class="py-12 text-center">
              <div class="w-12 h-12 mx-auto mb-3 rounded-full bg-[rgba(255,255,255,0.05)] flex items-center justify-center">
                <Server class="w-6 h-6 text-[var(--text-muted)]" />
              </div>
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

      {/* Info Banner */}
      <div
        class="
          flex items-start gap-3 p-4
          bg-[rgba(139,92,246,0.08)]
          border border-[rgba(139,92,246,0.2)]
          rounded-[var(--radius-xl)]
        "
      >
        <div class="w-8 h-8 rounded-full bg-[rgba(139,92,246,0.15)] flex items-center justify-center flex-shrink-0">
          <Server class="w-4 h-4 text-[#8B5CF6]" />
        </div>
        <div class="text-sm text-[var(--text-secondary)] leading-relaxed">
          <p>
            MCP servers extend Estela's capabilities with additional tools, data sources, and
            integrations.
          </p>
          <a
            href="https://modelcontextprotocol.io"
            target="_blank"
            rel="noopener noreferrer"
            class="
              inline-flex items-center gap-1 mt-2
              text-[#8B5CF6] hover:text-[#A78BFA]
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

  return (
    <div
      class={`
        relative overflow-hidden
        rounded-[var(--radius-xl)]
        border transition-all duration-[var(--duration-fast)]
        ${
          props.server.status === 'connected'
            ? 'border-[rgba(34,197,94,0.3)] bg-[rgba(34,197,94,0.05)]'
            : props.server.status === 'error'
              ? 'border-[rgba(239,68,68,0.3)] bg-[rgba(239,68,68,0.05)]'
              : 'border-[rgba(255,255,255,0.05)] bg-[rgba(24,24,27,0.6)] hover:border-[rgba(255,255,255,0.1)]'
        }
      `}
    >
      {/* Main Row */}
      <div class="flex items-center gap-3 p-3">
        {/* Status Icon */}
        <div
          class="p-2.5 rounded-[var(--radius-lg)] transition-all duration-[var(--duration-fast)]"
          style={{ background: status().bgColor }}
        >
          <Server class="w-4 h-4" style={{ color: status().color }} />
        </div>

        {/* Info */}
        <div class="flex-1 min-w-0">
          <div class="flex items-center gap-2">
            <span class="text-sm font-medium text-[var(--text-primary)]">{props.server.name}</span>
            {/* Status dot */}
            <div class={`w-2 h-2 rounded-full ${status().dotClass}`} />
            <span class="text-xs text-[var(--text-muted)]">{status().label}</span>
          </div>
          <div class="text-xs text-[var(--text-muted)] truncate mt-0.5 font-mono">
            {props.server.url}
          </div>
        </div>

        {/* Actions */}
        <div class="flex items-center gap-1">
          <Show when={props.server.status === 'connected' && props.onDisconnect}>
            <button
              type="button"
              onClick={props.onDisconnect}
              class="
                p-1.5 rounded-[var(--radius-md)]
                text-[var(--text-muted)]
                hover:text-[#EF4444]
                hover:bg-[rgba(239,68,68,0.15)]
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
              onClick={props.onConnect}
              class="
                p-1.5 rounded-[var(--radius-md)]
                text-[var(--text-muted)]
                hover:text-[#22C55E]
                hover:bg-[rgba(34,197,94,0.15)]
                transition-colors duration-[var(--duration-fast)]
              "
              title="Connect"
            >
              <Play class="w-4 h-4" />
            </button>
          </Show>
          <Show when={props.server.status === 'connecting'}>
            <div class="p-1.5">
              <Activity class="w-4 h-4 text-[#EAB308] animate-pulse" />
            </div>
          </Show>
          <Show when={props.onEdit}>
            <button
              type="button"
              onClick={props.onEdit}
              class="
                p-1.5 rounded-[var(--radius-md)]
                text-[var(--text-muted)]
                hover:text-[var(--text-primary)]
                hover:bg-[rgba(255,255,255,0.08)]
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
              onClick={props.onRemove}
              class="
                p-1.5 rounded-[var(--radius-md)]
                text-[var(--text-muted)]
                hover:text-[#EF4444]
                hover:bg-[rgba(239,68,68,0.15)]
                transition-colors duration-[var(--duration-fast)]
              "
              title="Remove"
            >
              <Trash2 class="w-4 h-4" />
            </button>
          </Show>
          <ChevronRight class="w-4 h-4 text-[var(--text-muted)] ml-1" />
        </div>
      </div>

      {/* Error Message */}
      <Show when={props.server.status === 'error' && props.server.error}>
        <div class="px-4 py-2 bg-[rgba(239,68,68,0.1)] border-t border-[rgba(239,68,68,0.2)]">
          <div class="flex items-center gap-2">
            <AlertTriangle class="w-3.5 h-3.5 text-[#EF4444] flex-shrink-0" />
            <p class="text-xs text-[#EF4444]">{props.server.error}</p>
          </div>
        </div>
      </Show>

      {/* Capabilities (expandable) */}
      <Show when={props.server.capabilities && props.server.capabilities.length > 0}>
        <button
          type="button"
          onClick={() => setExpanded(!expanded())}
          class="
            w-full px-4 py-2 text-left text-xs
            text-[var(--text-muted)]
            hover:bg-[rgba(255,255,255,0.03)]
            border-t border-[rgba(255,255,255,0.05)]
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
          <div class="px-4 py-3 border-t border-[rgba(255,255,255,0.05)] bg-[rgba(255,255,255,0.02)]">
            <div class="flex flex-wrap gap-1.5">
              <For each={props.server.capabilities}>
                {(cap) => (
                  <span
                    class="
                      px-2 py-1 text-xs
                      bg-[rgba(255,255,255,0.05)]
                      text-[var(--text-secondary)]
                      rounded-full
                      border border-[rgba(255,255,255,0.05)]
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
