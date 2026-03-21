/**
 * Session Store
 * Public API — re-exports useSession() preserving the same shape.
 */

import { STORAGE_KEYS } from '../../config/constants'
import { getLastSessionForProject } from '../session-persistence'
import * as data from './session-data'
import * as lifecycle from './session-lifecycle'
import * as msg from './session-messages'
import {
  agentStats,
  agents,
  archivedSessions,
  backgroundPlanActive,
  backgroundPlanProgress,
  busySessionIds,
  checkpoints,
  compactionIndex,
  contextUsage,
  currentSession,
  editingMessageId,
  fileOperations,
  isLoadingMessages,
  isLoadingSessions,
  memoryItems,
  messages,
  readOnlyFiles,
  retryingMessageId,
  selectedModel,
  selectedProvider,
  sessions,
  sessionTokenStats,
  setAgents,
  setBackgroundPlanActive,
  setBackgroundPlanProgress,
  setCompactionIndex,
  setCurrentSession,
  setEditingMessageId,
  setFileOperations,
  setMemoryItems,
  setMessages,
  setRetryingMessageId,
  setSelectedModel,
  setTerminalExecutions,
  terminalExecutions,
} from './session-state'

export function useSession() {
  return {
    // State Accessors
    currentSession,
    setCurrentSession,
    sessions,
    isLoadingSessions,
    messages,
    setMessages,
    isLoadingMessages,
    agents,
    setAgents,
    fileOperations,
    setFileOperations,
    terminalExecutions,
    setTerminalExecutions,
    memoryItems,
    setMemoryItems,
    selectedModel,
    selectedProvider,
    setSelectedModel,
    retryingMessageId,
    editingMessageId,
    sessionTokenStats,
    contextUsage,
    agentStats,

    // Session Tree
    getSessionTree: lifecycle.getSessionTree,

    // Session List Management
    loadAllSessions: async () => {
      await lifecycle.loadSessionsForCurrentProject()
    },
    loadSessionsForCurrentProject: lifecycle.loadSessionsForCurrentProject,
    restoreForCurrentProject: lifecycle.restoreForCurrentProject,
    createNewSession: lifecycle.createNewSession,
    switchSession: lifecycle.switchSession,
    renameSession: lifecycle.renameSession,
    archiveSession: lifecycle.archiveSession,
    unarchiveSession: lifecycle.unarchiveSession,
    loadArchivedSessions: lifecycle.loadArchivedSessions,
    archivedSessions,
    busySessionIds,
    isSessionBusy: (id: string): boolean => busySessionIds().has(id),
    updateSessionSlug: lifecycle.updateSessionSlug,
    deleteSessionPermanently: lifecycle.deleteSessionPermanently,
    duplicateSession: lifecycle.duplicateSession,
    forkSession: lifecycle.forkSession,
    branchAtMessage: lifecycle.branchAtMessage,
    updateSessionStats: lifecycle.updateSessionStats,

    // Message Management
    loadSessionMessages: msg.loadSessionMessages,
    addMessage: msg.addMessage,
    updateMessageContent: msg.updateMessageContent,
    updateMessage: msg.updateMessage,
    setMessageError: msg.setMessageError,
    deleteMessage: msg.deleteMessage,
    deleteMessagesAfter: msg.deleteMessagesAfter,
    rollbackToMessage: msg.rollbackToMessage,
    replaceMessagesFromBackend: msg.replaceMessagesFromBackend,

    // Agent Management
    addAgent: data.addAgent,
    updateAgent: data.updateAgent,
    removeAgent: data.removeAgent,

    // File Operations Management
    addFileOperation: data.addFileOperation,
    clearFileOperations: data.clearFileOperations,
    undoFileChange: data.undoFileChange,
    redoFileChange: data.redoFileChange,
    getVersionCounts: data.getVersionCounts,

    // Terminal Executions Management
    addTerminalExecution: data.addTerminalExecution,
    updateTerminalExecution: data.updateTerminalExecution,
    clearTerminalExecutions: data.clearTerminalExecutions,

    // Memory Items Management
    addMemoryItem: data.addMemoryItem,
    removeMemoryItem: data.removeMemoryItem,
    clearMemoryItems: data.clearMemoryItems,
    queryMemoriesAcrossSessions: data.queryMemoriesAcrossSessions,

    // Compaction divider
    compactionIndex,

    // Checkpoints
    checkpoints,
    createCheckpoint: msg.createCheckpoint,
    rollbackToCheckpoint: msg.rollbackToCheckpoint,

    // Revert files
    revertFilesAfter: data.revertFilesAfter,

    // Read-Only File Context
    readOnlyFiles,
    toggleReadOnly: data.toggleReadOnly,
    isReadOnly: data.isReadOnly,

    // Background Plan Execution
    backgroundPlanActive,
    backgroundPlanProgress,
    startBackgroundPlan: data.startBackgroundPlan,
    updateBackgroundPlanProgress: data.updateBackgroundPlanProgress,
    stopBackgroundPlan: data.stopBackgroundPlan,

    // UI State Management
    startEditing: (id: string) => setEditingMessageId(id),
    stopEditing: () => setEditingMessageId(null),
    setRetryingMessageId,

    // Session State Management
    clearSession: () => {
      setCurrentSession(null)
      setMessages([])
      setAgents([])
      setFileOperations([])
      setTerminalExecutions([])
      setMemoryItems([])
      setEditingMessageId(null)
      setRetryingMessageId(null)
      setBackgroundPlanActive(false)
      setBackgroundPlanProgress('')
      setCompactionIndex(-1)
    },
    getLastSessionId: (): string | null => {
      return localStorage.getItem(STORAGE_KEYS.LAST_SESSION)
    },
    getLastSessionForProject: (projectId: string | null | undefined): string | null => {
      return getLastSessionForProject(projectId)
    },
  }
}
