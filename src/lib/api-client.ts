/**
 * HTTP API Client for browser mode (non-Tauri).
 *
 * When the SolidJS frontend runs in a regular browser (not inside a Tauri
 * webview), all Rust backend communication goes through the HTTP API served
 * by `ava serve --port 8080`.
 *
 * This module provides:
 * - `apiInvoke()` — drop-in replacement for Tauri's `invoke()`
 * - `createEventSocket()` — WebSocket factory for agent event streaming
 */

const API_BASE = import.meta.env.VITE_API_URL || ''

/**
 * Map from Tauri command names to HTTP API endpoints.
 * Commands not listed here fall through to a generic `/api/{command}` path.
 */
const COMMAND_TO_ENDPOINT: Record<string, { path: string; method: 'GET' | 'POST' }> = {
  // Agent
  submit_goal: { path: '/api/agent/submit', method: 'POST' },
  cancel_agent: { path: '/api/agent/cancel', method: 'POST' },
  get_agent_status: { path: '/api/agent/status', method: 'GET' },
  steer_agent: { path: '/api/agent/steer', method: 'POST' },
  follow_up_agent: { path: '/api/agent/follow-up', method: 'POST' },
  post_complete_agent: { path: '/api/agent/post-complete', method: 'POST' },
  get_message_queue: { path: '/api/agent/queue', method: 'GET' },
  clear_message_queue: { path: '/api/agent/queue/clear', method: 'POST' },
  resolve_approval: { path: '/api/agent/resolve-approval', method: 'POST' },
  resolve_question: { path: '/api/agent/resolve-question', method: 'POST' },
  retry_last_message: { path: '/api/agent/retry', method: 'POST' },
  edit_and_resend: { path: '/api/agent/edit-resend', method: 'POST' },
  regenerate_response: { path: '/api/agent/regenerate', method: 'POST' },
  undo_last_edit: { path: '/api/agent/undo', method: 'POST' },

  // Sessions
  list_sessions: { path: '/api/sessions', method: 'GET' },
  list_session_agents: { path: '/api/sessions/{id}/agents', method: 'GET' },
  list_session_files: { path: '/api/sessions/{id}/files', method: 'GET' },
  list_session_terminal: { path: '/api/sessions/{id}/terminal', method: 'GET' },
  list_session_memory: { path: '/api/sessions/{id}/memory', method: 'GET' },
  list_session_checkpoints: { path: '/api/sessions/{id}/checkpoints', method: 'GET' },
  get_session_messages: { path: '/api/sessions/{id}/messages', method: 'GET' },
  load_session: { path: '/api/sessions/load', method: 'POST' },
  create_session: { path: '/api/sessions/create', method: 'POST' },
  delete_session: { path: '/api/sessions/delete', method: 'POST' }, // handled specially below
  rename_session: { path: '/api/sessions/rename', method: 'POST' }, // handled specially below
  search_sessions: { path: '/api/sessions/search', method: 'POST' },

  // Models
  list_models: { path: '/api/models', method: 'GET' },
  get_current_model: { path: '/api/models/current', method: 'GET' },
  switch_model: { path: '/api/models/switch', method: 'POST' },

  // Providers
  list_providers: { path: '/api/providers', method: 'GET' },

  // Config
  get_config: { path: '/api/config', method: 'GET' },

  // Tools
  list_tools: { path: '/api/tools', method: 'GET' },
  list_agent_tools: { path: '/api/tools/agent', method: 'GET' },
  execute_tool: { path: '/api/tools/execute', method: 'POST' },

  // MCP
  list_mcp_servers: { path: '/api/mcp', method: 'GET' },
  reload_mcp_servers: { path: '/api/mcp/reload', method: 'POST' },
  enable_mcp_server: { path: '/api/mcp/servers/{name}/enable', method: 'POST' },
  disable_mcp_server: { path: '/api/mcp/servers/{name}/disable', method: 'POST' },

  // Plugins
  list_installed_plugins: { path: '/api/plugins', method: 'GET' },

  // Permissions
  get_permission_level: { path: '/api/permissions', method: 'GET' },
  set_permission_level: { path: '/api/permissions', method: 'POST' },
  toggle_permission_level: { path: '/api/permissions/toggle', method: 'POST' },

  // Context
  compact_context: { path: '/api/context/compact', method: 'POST' },

  // Health
  health: { path: '/api/health', method: 'GET' },
}

/**
 * Invoke a backend command over HTTP. This mirrors the signature of Tauri's
 * `invoke<T>(cmd, args?)` so call sites can switch transparently.
 */
export async function apiInvoke<T>(cmd: string, args?: Record<string, unknown>): Promise<T> {
  const mapping = COMMAND_TO_ENDPOINT[cmd]
  let path = mapping ? mapping.path : `/api/${cmd}`
  const method = mapping ? mapping.method : args ? 'POST' : 'GET'

  // Substitute path parameters like {id} from args
  if (args) {
    path = path.replace(/\{(\w+)\}/g, (_, key) => {
      const val = args[key]
      if (val !== undefined && val !== null) {
        return encodeURIComponent(String(val))
      }
      return `{${key}}`
    })
  }

  const url = `${API_BASE}${path}`
  const headers: Record<string, string> = {}
  let body: string | undefined

  if (method === 'POST' && args) {
    headers['Content-Type'] = 'application/json'
    // Tauri invoke wraps params in { args: { ... } } — unwrap for HTTP API.
    // Also convert camelCase keys to snake_case for the Rust backend.
    const payload =
      args.args && typeof args.args === 'object' ? (args.args as Record<string, unknown>) : args
    const snaked: Record<string, unknown> = {}
    for (const [k, v] of Object.entries(payload)) {
      snaked[k.replace(/[A-Z]/g, (c) => `_${c.toLowerCase()}`)] = v
    }
    body = JSON.stringify(snaked)
  } else if (method === 'GET' && args) {
    // For GET requests with args, append as query parameters
    const params = new URLSearchParams()
    for (const [key, value] of Object.entries(args)) {
      if (value !== null && value !== undefined) {
        params.set(key, String(value))
      }
    }
    const qs = params.toString()
    const separator = url.includes('?') ? '&' : '?'
    const fullUrl = qs ? `${url}${separator}${qs}` : url
    const res = await fetch(fullUrl, { method, headers })
    if (!res.ok) {
      if (res.status === 404 && !mapping) {
        return undefined as T
      }
      const text = await res.text().catch(() => '')
      throw new Error(`API error ${res.status}: ${text || res.statusText}`)
    }
    return res.json() as Promise<T>
  }

  const res = await fetch(url, { method, headers, body })
  if (!res.ok) {
    // For unmapped commands that 404, return undefined instead of throwing
    if (res.status === 404 && !mapping) {
      return undefined as T
    }
    const text = await res.text().catch(() => '')
    throw new Error(`API error ${res.status}: ${text || res.statusText}`)
  }
  // Some endpoints return empty 204 responses
  const contentType = res.headers.get('content-type')
  if (res.status === 204 || !contentType?.includes('application/json')) {
    return undefined as T
  }
  return res.json() as Promise<T>
}

/**
 * Create a WebSocket connection to the agent event stream.
 *
 * The WebSocket sends `AgentEvent` payloads as JSON text frames.
 */
export function createEventSocket(path = '/ws'): WebSocket {
  const base = API_BASE || window.location.origin
  const wsUrl = base.replace(/^http/, 'ws') + path
  return new WebSocket(wsUrl)
}

export interface HealthResponse {
  status: string
  version: string
  cwd: string
}

/**
 * Check if the HTTP backend is reachable.
 * Returns the health response on success, or null if unreachable.
 */
export async function checkApiHealth(): Promise<HealthResponse | null> {
  try {
    const res = await fetch(`${API_BASE}/api/health`, { method: 'GET' })
    if (!res.ok) return null
    return (await res.json()) as HealthResponse
  } catch {
    return null
  }
}
