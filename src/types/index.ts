// Core types for Estela

// Message error for retry functionality
export interface MessageError {
  type: 'rate_limit' | 'auth' | 'server' | 'network' | 'api' | 'unknown'
  message: string
  retryAfter?: number
  timestamp: number
}

// Session token statistics
export interface SessionTokenStats {
  total: number
  count: number
}

export interface Session {
  id: string
  /** Project this session belongs to */
  projectId?: string
  name: string
  createdAt: number
  updatedAt: number
  status: 'active' | 'completed' | 'archived'
  metadata?: Record<string, unknown>
}

// Extended session with computed stats for sidebar display
export interface SessionWithStats extends Session {
  messageCount: number
  totalTokens: number
  lastPreview?: string
}

export interface Message {
  id: string
  sessionId: string
  role: 'user' | 'assistant' | 'system'
  content: string
  agentId?: string
  createdAt: number
  tokensUsed?: number
  metadata?: Record<string, unknown>
  // Sprint 1.2: Error and edit tracking
  error?: MessageError
  editedAt?: number
  regeneratedFrom?: string
}

export interface Agent {
  id: string
  sessionId: string
  type: 'commander' | 'operator' | 'validator'
  status: 'idle' | 'thinking' | 'executing' | 'waiting' | 'completed' | 'error'
  model: string
  createdAt: number
  completedAt?: number
  assignedFiles?: string[]
  taskDescription?: string
  result?: TaskResult
}

export interface TaskResult {
  success: boolean
  summary: string
  filesModified: string[]
  errors?: string[]
  tokensUsed: number
}

// ============================================================================
// File Operations
// ============================================================================

export type FileOperationType = 'read' | 'write' | 'edit' | 'delete'

export interface FileOperation {
  id: string
  sessionId: string
  agentId?: string
  agentName?: string
  type: FileOperationType
  filePath: string
  timestamp: number
  /** Number of lines read/written */
  lines?: number
  /** Lines added (for edit operations) */
  linesAdded?: number
  /** Lines removed (for edit operations) */
  linesRemoved?: number
  /** Whether this is a new file (for write operations) */
  isNew?: boolean
  /** Original content (for diff display) */
  originalContent?: string
  /** New content (for diff display) */
  newContent?: string
}

// ============================================================================
// Terminal Executions
// ============================================================================

export type ExecutionStatus = 'running' | 'success' | 'error'

export interface TerminalExecution {
  id: string
  sessionId: string
  agentId?: string
  agentName?: string
  command: string
  output: string
  status: ExecutionStatus
  exitCode?: number
  startedAt: number
  completedAt?: number
  /** Working directory */
  cwd?: string
}

// ============================================================================
// Memory/Context Items
// ============================================================================

export type MemoryItemType = 'conversation' | 'file' | 'code' | 'knowledge'

export interface MemoryItem {
  id: string
  sessionId: string
  type: MemoryItemType
  title: string
  preview: string
  tokens: number
  createdAt: number
  /** Source file path for file/code items */
  source?: string
}

// Project types
export * from './project'

// Team types
export * from './team'
