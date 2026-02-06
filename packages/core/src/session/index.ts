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
// File Storage
export {
  createFileSessionStorage,
  FileSessionStorage,
  type SessionMetaFile,
} from './file-storage.js'
// Manager
export { createSessionManager, SessionManager } from './manager.js'
// Resume / Session Selector
export {
  createSessionSelector,
  formatSessionTimestamp,
  RESUME_LATEST,
  type SessionDisplayInfo,
  type SessionResolveResult,
  SessionSelector,
} from './resume.js'
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
