/**
 * @estela/core Session Module
 * Session state management, persistence, and checkpoints
 */

// Doom Loop Detection
export {
  checkDoomLoop,
  clearDoomLoopHistory,
  type DoomLoopCheckResult,
  type DoomLoopConfig,
  DoomLoopDetector,
  getDoomLoopDetector,
  type RecordedToolCall,
  resetDoomLoopDetector,
} from './doom-loop.js'
// Manager
export { createSessionManager, SessionManager } from './manager.js'
// Types
export type {
  Checkpoint,
  CheckpointMeta,
  FileState,
  ForkInfo,
  ForkOptions,
  SerializedSessionState,
  SessionEvent,
  SessionEventListener,
  SessionManagerConfig,
  SessionMeta,
  SessionState,
  SessionStorage,
  TodoItem,
} from './types.js'
