// Core types for Estela

export interface Session {
  id: string;
  name: string;
  createdAt: number;
  updatedAt: number;
  status: 'active' | 'completed' | 'archived';
  metadata?: Record<string, unknown>;
}

export interface Message {
  id: string;
  sessionId: string;
  role: 'user' | 'assistant' | 'system';
  content: string;
  agentId?: string;
  createdAt: number;
  tokensUsed?: number;
  metadata?: Record<string, unknown>;
}

export interface Agent {
  id: string;
  sessionId: string;
  type: 'commander' | 'operator' | 'validator';
  status: 'idle' | 'thinking' | 'executing' | 'waiting' | 'completed' | 'error';
  model: string;
  createdAt: number;
  completedAt?: number;
  assignedFiles?: string[];
  taskDescription?: string;
  result?: TaskResult;
}

export interface TaskResult {
  success: boolean;
  summary: string;
  filesModified: string[];
  errors?: string[];
  tokensUsed: number;
}

export interface FileChange {
  id: string;
  sessionId: string;
  agentId: string;
  filePath: string;
  changeType: 'create' | 'edit' | 'delete';
  oldContent?: string;
  newContent?: string;
  createdAt: number;
  reverted: boolean;
}
