// Core types for Estela

// Message error for retry functionality
export interface MessageError {
  type: 'rate_limit' | 'auth' | 'server' | 'network' | 'unknown'
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

// Project types
export * from './project'
