/**
 * Agent activity configuration
 *
 * Status config, icon mappings, and helper functions for agent activity display.
 */

import {
  AlertCircle,
  Bot,
  CheckCircle2,
  Clock,
  Code2,
  FileText,
  Loader2,
  Play,
  Search,
  Terminal,
} from 'lucide-solid'
import type { Agent } from '../../../types'

// ============================================================================
// Status Configuration
// ============================================================================

export type DisplayStatus = 'pending' | 'running' | 'completed' | 'error'

export const statusConfig: Record<
  DisplayStatus,
  { color: string; bg: string; icon: typeof CheckCircle2 }
> = {
  pending: { color: 'var(--text-muted)', bg: 'var(--surface-raised)', icon: Clock },
  running: { color: 'var(--accent)', bg: 'var(--accent-subtle)', icon: Loader2 },
  completed: { color: 'var(--success)', bg: 'var(--success-subtle)', icon: CheckCircle2 },
  error: { color: 'var(--error)', bg: 'var(--error-subtle)', icon: AlertCircle },
}

// Map agent type to icon
export const agentTypeIcons = {
  commander: Bot,
  operator: Code2,
  validator: Search,
} as const

// Tool status icons
export const toolStatusIcons = {
  pending: Clock,
  running: Loader2,
  success: CheckCircle2,
  error: AlertCircle,
} as const

// Tool name to icon mapping
export const toolIcons: Record<string, typeof Terminal> = {
  bash: Terminal,
  read_file: FileText,
  write_file: FileText,
  create_file: FileText,
  edit: Code2,
  glob: Search,
  grep: Search,
  ls: FileText,
  websearch: Search,
  webfetch: Search,
  browser: Play,
}

// ============================================================================
// Helper Functions
// ============================================================================

/** Map internal agent status to display status */
export const mapStatus = (status: Agent['status']): DisplayStatus => {
  switch (status) {
    case 'idle':
    case 'waiting':
      return 'pending'
    case 'thinking':
    case 'executing':
      return 'running'
    case 'completed':
      return 'completed'
    case 'error':
      return 'error'
    default:
      return 'pending'
  }
}

export const formatDuration = (ms: number): string => {
  const seconds = Math.floor(ms / 1000)
  if (seconds < 60) return `${seconds}s`
  const minutes = Math.floor(seconds / 60)
  return `${minutes}m ${seconds % 60}s`
}

export const getAgentDuration = (agent: Agent): number => {
  if (agent.completedAt) return agent.completedAt - agent.createdAt
  return Date.now() - agent.createdAt
}

/** Estimate progress based on agent status */
export const getProgress = (agent: Agent): number => {
  switch (agent.status) {
    case 'idle':
      return 0
    case 'waiting':
      return 10
    case 'thinking':
      return 40
    case 'executing':
      return 70
    case 'completed':
      return 100
    case 'error':
      return agent.result ? 50 : 20
    default:
      return 0
  }
}
