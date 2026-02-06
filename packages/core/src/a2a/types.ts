/**
 * A2A Protocol Types
 *
 * Types for the Agent-to-Agent protocol (v0.3.0).
 * Allows Estela to be discovered and used by other AI agents over HTTP.
 *
 * Reference: https://google.github.io/A2A/
 */

// ============================================================================
// Task States
// ============================================================================

/**
 * A2A task lifecycle states.
 *
 * submitted → working → completed | failed | canceled
 *                    → input_required → working (resume)
 */
export type A2ATaskState =
  | 'submitted'
  | 'working'
  | 'input_required'
  | 'completed'
  | 'failed'
  | 'canceled'
  | 'rejected'

// ============================================================================
// Message Parts
// ============================================================================

/** Text content */
export interface TextPart {
  type: 'text'
  text: string
}

/** File/data content */
export interface FilePart {
  type: 'file'
  file: {
    name?: string
    mimeType?: string
    /** Base64-encoded data */
    bytes?: string
    /** URI reference */
    uri?: string
  }
}

/** Structured data */
export interface DataPart {
  type: 'data'
  data: Record<string, unknown>
}

/** Union of all message parts */
export type Part = TextPart | FilePart | DataPart

// ============================================================================
// Messages
// ============================================================================

export interface A2AMessage {
  role: 'user' | 'agent'
  parts: Part[]
  metadata?: Record<string, unknown>
}

// ============================================================================
// Artifacts
// ============================================================================

/** Output artifact from task execution */
export interface Artifact {
  artifactId: string
  name?: string
  description?: string
  parts: Part[]
  metadata?: Record<string, unknown>
}

// ============================================================================
// Task
// ============================================================================

export interface TaskStatus {
  state: A2ATaskState
  message?: A2AMessage
  timestamp: string
}

export interface A2ATask {
  id: string
  contextId: string
  status: TaskStatus
  messages: A2AMessage[]
  artifacts: Artifact[]
  history: TaskStatus[]
  metadata?: Record<string, unknown>
}

// ============================================================================
// Agent Card
// ============================================================================

export interface AgentSkill {
  id: string
  name: string
  description: string
  tags?: string[]
  examples?: string[]
}

export interface AgentProvider {
  organization: string
  url?: string
}

export interface AgentCapabilities {
  streaming: boolean
  pushNotifications: boolean
  stateTransitionHistory: boolean
}

export interface AuthenticationScheme {
  scheme: 'bearer' | 'basic' | 'apiKey'
  description?: string
}

export interface AgentAuthentication {
  schemes: AuthenticationScheme[]
}

export interface AgentCard {
  name: string
  description: string
  url: string
  version: string
  protocolVersion: string
  provider?: AgentProvider
  capabilities: AgentCapabilities
  skills: AgentSkill[]
  defaultInputModes: string[]
  defaultOutputModes: string[]
  authentication?: AgentAuthentication
}

// ============================================================================
// SSE Events
// ============================================================================

export interface TaskStatusUpdateEvent {
  kind: 'status-update'
  taskId: string
  contextId: string
  final: boolean
  status: TaskStatus
  metadata?: Record<string, unknown>
}

export interface TaskArtifactUpdateEvent {
  kind: 'artifact-update'
  taskId: string
  contextId: string
  artifact: Artifact
  append: boolean
  lastChunk: boolean
}

export type A2AEvent = TaskStatusUpdateEvent | TaskArtifactUpdateEvent

// ============================================================================
// Request/Response
// ============================================================================

export interface SendMessageRequest {
  message: A2AMessage
  contextId?: string
  taskId?: string
}

export interface SendMessageResponse {
  task: A2ATask
}

export interface GetTaskResponse {
  task: A2ATask
}

export interface CancelTaskResponse {
  task: A2ATask
}

// ============================================================================
// Server Config
// ============================================================================

export interface A2AServerConfig {
  /** Port to listen on (default: 41242) */
  port?: number
  /** Host to bind to (default: 'localhost') */
  host?: string
  /** Bearer token for authentication (optional) */
  authToken?: string
  /** Working directory for agent execution */
  workingDirectory?: string
  /** Agent version string */
  agentVersion?: string
}

/** Default A2A server port */
export const DEFAULT_A2A_PORT = 41242

/** Default A2A protocol version */
export const A2A_PROTOCOL_VERSION = '0.3.0'

/** Default Estela agent version */
export const DEFAULT_AGENT_VERSION = '1.0.0'
