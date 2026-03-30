/**
 * Database Service — Barrel Re-export
 *
 * All database operations are split into domain modules for maintainability.
 * Import from this file for backward compatibility.
 */

// Agent operations
export { getAgents, saveAgent, updateAgentInDb } from './db-agents'
// Checkpoint operations
export { getCheckpoints } from './db-checkpoints'
// Initialization
export { getDb, initDatabase } from './db-init'
// Message operations
export {
  deleteMessageFromDb,
  deleteMessagesFromTimestamp,
  deleteSessionMessages,
  duplicateSessionMessages,
  getMessages,
  insertMessages,
  saveMessage,
  updateMessage,
} from './db-messages'
// Resource operations (files, terminal, memory)
export {
  clearFileOperations,
  clearMemoryItems,
  clearTerminalExecutions,
  deleteMemoryItem,
  getAllMemoryItems,
  getFileOperations,
  getMemoryItems,
  getTerminalExecutions,
  saveFileOperation,
  saveMemoryItem,
  saveTerminalExecution,
  updateTerminalExecution,
} from './db-resources'
// Session operations
export {
  archiveSession,
  createSession,
  deleteSession,
  getArchivedSessions,
  getSessionsWithStats,
  updateSession,
} from './db-sessions'
