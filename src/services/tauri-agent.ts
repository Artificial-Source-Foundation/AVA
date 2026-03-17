import { isTauri, invoke as tauriInvoke } from '@tauri-apps/api/core'
import { listen, type UnlistenFn } from '@tauri-apps/api/event'
import { apiInvoke, createEventSocket } from '../lib/api-client'

/** Invoke the backend — Tauri IPC or HTTP API depending on runtime. */
function invoke<T>(cmd: string, args?: Record<string, unknown>): Promise<T> {
  if (isTauri()) {
    return args ? tauriInvoke<T>(cmd, args) : tauriInvoke<T>(cmd)
  }
  return apiInvoke<T>(cmd, args)
}

export interface ToolExecutionResult {
  content: string
  is_error: boolean
}

export interface AgentSession {
  id: string
  goal?: string
  messages: unknown[]
  completed: boolean
}

export interface ToolInfo {
  name: string
  description: string
}

export type AgentEvent =
  | { type: 'token'; content: string }
  | { type: 'tool_call'; name: string; args: Record<string, unknown> }
  | { type: 'tool_result'; content: string; is_error: boolean }
  | { type: 'progress'; message: string }
  | { type: 'complete'; session: AgentSession }
  | { type: 'error'; message: string }

export async function executeTool(
  tool: string,
  args: Record<string, unknown>
): Promise<ToolExecutionResult> {
  return invoke<ToolExecutionResult>('execute_tool', { tool, args })
}

export async function agentRun(goal: string): Promise<AgentSession> {
  return invoke<AgentSession>('agent_run', { goal })
}

export async function agentStream(goal: string): Promise<void> {
  await invoke<void>('agent_stream', { goal })
}

export async function listTools(): Promise<ToolInfo[]> {
  return invoke<ToolInfo[]>('list_tools')
}

export async function listenToAgentEvents(
  onEvent: (event: AgentEvent) => void
): Promise<UnlistenFn> {
  if (isTauri()) {
    return listen<AgentEvent>('agent-event', (event) => {
      onEvent(event.payload)
    })
  }

  // Browser mode — connect via WebSocket
  const ws = createEventSocket()
  ws.onmessage = (evt) => {
    try {
      const event = JSON.parse(evt.data as string) as AgentEvent
      onEvent(event)
    } catch {
      // Ignore malformed messages
    }
  }
  // Return an unlisten function that closes the socket
  return () => {
    ws.close()
  }
}
