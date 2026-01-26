/**
 * Delta9 CLI Types
 *
 * Type definitions for CLI commands and options.
 */

// =============================================================================
// Command Options
// =============================================================================

export interface StatusOptions {
  /** Show verbose task details */
  verbose?: boolean
  /** Output format */
  format?: 'table' | 'json' | 'summary'
  /** Custom project directory */
  cwd?: string
}

export interface HistoryOptions {
  /** Number of events to show */
  limit?: number
  /** Filter by event type */
  type?: string
  /** Filter by category (mission, task, council, etc.) */
  category?: string
  /** Filter by session ID */
  session?: string
  /** Output format */
  format?: 'table' | 'json' | 'timeline'
  /** Custom project directory */
  cwd?: string
}

export interface HealthOptions {
  /** Show verbose diagnostics */
  verbose?: boolean
  /** Output format */
  format?: 'summary' | 'json'
  /** Custom project directory */
  cwd?: string
}

export interface AbortOptions {
  /** Reason for aborting */
  reason?: string
  /** Force abort even if already aborted */
  force?: boolean
  /** Create checkpoint for recovery (default: true) */
  checkpoint?: boolean
  /** Output format */
  format?: 'summary' | 'json'
  /** Custom project directory */
  cwd?: string
}

export interface ResumeOptions {
  /** Checkpoint ID to resume from */
  checkpoint?: string
  /** Reset failed tasks to pending (default: true) */
  resetFailed?: boolean
  /** Output format */
  format?: 'summary' | 'json'
  /** Custom project directory */
  cwd?: string
}

export interface QueryOptions {
  /** Filter by event type */
  type?: string
  /** Filter by category (mission, task, council, etc.) */
  category?: string
  /** Filter events since time (e.g., '1h', '30m', '2d', ISO date) */
  since?: string
  /** Filter events until time */
  until?: string
  /** Full-text search */
  search?: string
  /** Maximum events to return */
  limit?: number
  /** Show verbose event data */
  verbose?: boolean
  /** Output format */
  format?: 'table' | 'json'
  /** Custom project directory */
  cwd?: string
}

export interface StatsOptions {
  /** Time period: 1h, 24h, 7d, 30d, all */
  period?: string
  /** Show verbose breakdowns */
  verbose?: boolean
  /** Output format */
  format?: 'summary' | 'json'
  /** Custom project directory */
  cwd?: string
}

// =============================================================================
// Output Formatting
// =============================================================================

export interface TableColumn {
  header: string
  key: string
  width?: number
  align?: 'left' | 'right' | 'center'
}

export interface StatusReport {
  mission: {
    active: boolean
    id?: string
    status?: string
    title?: string
    objective?: string
    progress: {
      completed: number
      total: number
      percentage: number
    }
  }
  tasks: {
    pending: number
    inProgress: number
    completed: number
    failed: number
  }
  background: {
    active: number
    pending: number
    capacity: number
    utilization: string
  }
  uptime: string
  timestamp: string
}

export interface HistoryReport {
  events: Array<{
    id: string
    type: string
    timestamp: string
    summary: string
  }>
  stats: {
    total: number
    filtered: number
    categories: Record<string, number>
  }
}

export interface HealthReport {
  status: 'healthy' | 'degraded' | 'unhealthy'
  checks: {
    config: { ok: boolean; message: string }
    mission: { ok: boolean; message: string }
    background: { ok: boolean; message: string }
    sdk: { ok: boolean; message: string }
  }
  uptime: string
  timestamp: string
}

export interface AbortResult {
  success: boolean
  error?: string
  missionId?: string
  missionTitle?: string
  previousStatus?: string
  tasksAborted?: number
  tasksCompleted?: number
  cancelledTasks?: string[]
  checkpointId?: string
  reason?: string
  timestamp: string
}

export interface ResumeResult {
  success: boolean
  error?: string
  missionId?: string
  missionTitle?: string
  checkpointId?: string
  checkpointType?: string
  previousStatus?: string
  newStatus?: string
  tasksReset?: number
  resetTasks?: string[]
  taskSummary?: {
    pending: number
    inProgress: number
    completed: number
  }
  availableCheckpoints?: string[]
  timestamp: string
}

export interface QueryResult {
  query: {
    type?: string
    category?: string
    since?: string
    until?: string
    search?: string
    limit: number
  }
  events: Array<{
    id: string
    type: string
    timestamp: string
    category: string
    summary: string
    data?: Record<string, unknown>
  }>
  stats: {
    total: number
    matched: number
    categories: Record<string, number>
  }
  timestamp: string
}

export interface StatsReport {
  period: string
  missions: {
    total: number
    completed: number
    failed: number
    aborted: number
    successRate: number
  }
  tasks: {
    total: number
    completed: number
    failed: number
    avgAttempts: number
    avgDuration: number
  }
  agents: Record<
    string,
    {
      tasksCompleted: number
      tasksFailed: number
      tokensUsed: number
      avgDuration: number
    }
  >
  budget: {
    totalSpent: number
    byCategory: {
      council: number
      operators: number
      validators: number
      support: number
    }
  }
  decompositions: {
    total: number
    byStrategy: Record<string, number>
    avgSubtaskCount: number
    successRate: number
  }
  epics: {
    total: number
    completed: number
    inProgress: number
  }
  timestamp: string
}

// =============================================================================
// Colors (for terminal output)
// =============================================================================

export const colors = {
  reset: '\x1b[0m',
  bold: '\x1b[1m',
  dim: '\x1b[2m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  magenta: '\x1b[35m',
  cyan: '\x1b[36m',
  white: '\x1b[37m',
  gray: '\x1b[90m',
} as const

export function colorize(text: string, color: keyof typeof colors): string {
  return `${colors[color]}${text}${colors.reset}`
}

// =============================================================================
// Status Emoji/Symbols
// =============================================================================

export const symbols = {
  check: '✓',
  cross: '✗',
  warning: '⚠',
  bullet: '•',
  arrow: '→',
  dash: '─',
  pending: '○',
  inProgress: '◐',
  completed: '●',
  success: '✓',
  error: '✗',
} as const
