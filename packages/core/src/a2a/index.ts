/**
 * A2A (Agent-to-Agent) Protocol
 *
 * Exposes AVA as an A2A-compatible agent over HTTP.
 *
 * @module a2a
 */

// Agent Card
export { createAgentCard } from './agent-card.js'
export type { AuthResult } from './auth.js'
// Authentication
export {
  checkAuth,
  extractBearerToken,
  validateBearerToken,
} from './auth.js'
// Server
export {
  A2AServer,
  getA2AServer,
  resetA2AServer,
  setA2AServer,
} from './server.js'
export type { SSEWritable } from './streaming.js'

// SSE Streaming
export {
  formatJsonRpcSSE,
  formatSSE,
  SSE_HEADERS,
  SSEWriter,
  startKeepalive,
  statusEvent,
} from './streaming.js'
export type { TaskEventListener, TaskExecutor } from './task.js'
// Task Management
export {
  agentMessage,
  createArtifactEvent,
  createStatusEvent,
  dataPart,
  TaskManager,
  textPart,
  userMessage,
} from './task.js'
// Types
export type {
  A2AEvent,
  A2AMessage,
  A2AServerConfig,
  A2ATask,
  A2ATaskState,
  AgentAuthentication,
  AgentCapabilities,
  AgentCard,
  AgentProvider,
  AgentSkill,
  Artifact,
  AuthenticationScheme,
  CancelTaskResponse,
  DataPart,
  FilePart,
  GetTaskResponse,
  Part,
  SendMessageRequest,
  SendMessageResponse,
  TaskArtifactUpdateEvent,
  TaskStatus,
  TaskStatusUpdateEvent,
  TextPart,
} from './types.js'
export {
  A2A_PROTOCOL_VERSION,
  DEFAULT_A2A_PORT,
  DEFAULT_AGENT_VERSION,
} from './types.js'
