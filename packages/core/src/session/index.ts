/**
 * @estela/core Session Module
 * Session state management, persistence, and checkpoints
 */

// Manager
export { createSessionManager, SessionManager } from './manager.js'
// Types
export type {
  Checkpoint,
  CheckpointMeta,
  FileState,
  SerializedSessionState,
  SessionEvent,
  SessionEventListener,
  SessionManagerConfig,
  SessionMeta,
  SessionState,
  SessionStorage,
} from './types.js'
