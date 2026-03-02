// Core types for AVA

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
  totalCost: number
}

export interface Session {
  id: string
  /** Project this session belongs to */
  projectId?: string
  /** Parent session this was forked/branched from */
  parentSessionId?: string
  /** Human-readable slug generated from session goal */
  slug?: string
  /** Timestamp when agent started executing (null = idle) */
  busySince?: number | null
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
  costUSD?: number
  model?: string
  metadata?: Record<string, unknown>
  // Sprint 1.2: Error and edit tracking
  error?: MessageError
  editedAt?: number
  regeneratedFrom?: string
  // Vision support
  images?: Array<{ data: string; mimeType: string; name?: string }>
  // Tool execution visibility
  toolCalls?: ToolCall[]
}

// ============================================================================
// Tool Calls (Sprint 1: Tool Execution Visibility)
// ============================================================================

export type ToolCallStatus = 'pending' | 'running' | 'success' | 'error'

export interface ToolCall {
  id: string
  name: string
  args: Record<string, unknown>
  status: ToolCallStatus
  output?: string
  /** Live streaming output while tool is running (bash commands) */
  streamingOutput?: string
  error?: string
  startedAt: number
  completedAt?: number
  filePath?: string
  diff?: { oldContent: string; newContent: string }
  /** MCP UI resource for rich rendering (table, form, chart, image, markdown) */
  uiResource?: { type: 'table' | 'form' | 'chart' | 'image' | 'markdown'; data: unknown }
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

export type MemoryItemType = 'conversation' | 'file' | 'code' | 'knowledge' | 'checkpoint'

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

// Workflows
export interface Workflow {
  id: string
  projectId?: string
  name: string
  description: string
  tags: string[]
  prompt: string
  createdAt: number
  updatedAt: number
  usageCount: number
  sourceSessionId?: string
  /** Cron expression for scheduled execution (e.g. "0 9 * * *") */
  schedule?: string
  /** Timestamp of last scheduled run */
  lastRun?: number
}

// Plugin types
export * from './plugin'
// Project types
export * from './project'
// Team types
export * from './team'
