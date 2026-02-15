/**
 * Agent Types
 * Type definitions for the useAgent hook
 */

import type { ApprovalRequest } from '../../lib/tool-approval'

// Re-export for consumers that import from useAgent
export type { ApprovalRequest }

/** Tool activity for UI display */
export interface ToolActivity {
  id: string
  name: string
  args: Record<string, unknown>
  status: 'pending' | 'running' | 'success' | 'error'
  output?: string
  error?: string
  startedAt: number
  completedAt?: number
  durationMs?: number
}

/** Agent state */
export interface AgentState {
  isRunning: boolean
  isPlanMode: boolean
  currentTurn: number
  tokensUsed: number
  currentThought: string
  toolActivity: ToolActivity[]
  pendingApproval: ApprovalRequest | null
  doomLoopDetected: boolean
  lastError: string | null
}
