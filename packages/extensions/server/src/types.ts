/**
 * Server extension types — API request/response shapes, run state.
 */

export interface RunRequest {
  goal: string
  context?: string
  tools?: string[]
  provider?: string
  model?: string
}

export interface RunResponse {
  runId: string
  status: 'started'
}

export interface RunStatus {
  runId: string
  status: 'running' | 'completed' | 'error' | 'aborted'
  startedAt: number
  completedAt?: number
  result?: string
  error?: string
}

export interface SteerRequest {
  message: string
}

export interface SteerResponse {
  accepted: boolean
}

export interface ServerEvent {
  type: 'text' | 'tool_use' | 'tool_result' | 'error' | 'done' | 'status'
  data: Record<string, unknown>
  timestamp: number
}

export interface ServerConfig {
  port: number
  host: string
  tokenFile: string
}

export const DEFAULT_SERVER_CONFIG: ServerConfig = {
  port: 3100,
  host: '127.0.0.1',
  tokenFile: '~/.ava/server-tokens.json',
}
