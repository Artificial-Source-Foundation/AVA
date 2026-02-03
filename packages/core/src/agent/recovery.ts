/**
 * Agent Recovery
 * Implements self-correction strategies for agent failures
 *
 * Based on Gemini CLI's retry.ts and fallback/handler.ts patterns
 */

import type { RecoveryPlan, RecoveryStrategy } from './planner.js'
import type { AgentStep, ToolCallInfo } from './types.js'

// ============================================================================
// Types
// ============================================================================

/**
 * Error categories for classification
 */
export type ErrorCategory =
  | 'permission' // Access denied, EACCES
  | 'not_found' // File/resource not found
  | 'timeout' // Operation timed out
  | 'network' // Connection errors
  | 'validation' // Invalid input/format
  | 'syntax' // Parse/syntax errors
  | 'resource' // Out of memory, disk full
  | 'rate_limit' // Too many requests
  | 'unknown' // Unclassified errors

/**
 * Retry options for operations
 */
export interface RetryOptions {
  /** Maximum number of attempts */
  maxAttempts: number
  /** Initial delay in ms before first retry */
  initialDelayMs: number
  /** Maximum delay between retries */
  maxDelayMs: number
  /** Jitter factor (0-1) for randomizing delays */
  jitterFactor: number
  /** AbortSignal for cancellation */
  signal?: AbortSignal
  /** Callback on each retry attempt */
  onRetry?: (attempt: number, error: unknown, delayMs: number) => void
}

const DEFAULT_RETRY_OPTIONS: RetryOptions = {
  maxAttempts: 3,
  initialDelayMs: 1000,
  maxDelayMs: 30000,
  jitterFactor: 0.3,
}

/**
 * Rollback state tracking
 */
export interface RollbackState {
  /** Steps that have been executed */
  executedSteps: AgentStep[]
  /** Steps that have been rolled back */
  rolledBackSteps: AgentStep[]
  /** Files that were modified */
  modifiedFiles: string[]
  /** Whether a git snapshot was created */
  hasSnapshot: boolean
  /** Snapshot ID if created */
  snapshotId?: string
}

/**
 * Recovery action result
 */
export interface RecoveryActionResult {
  /** Whether recovery succeeded */
  success: boolean
  /** What action was taken */
  action: 'retried' | 'skipped' | 'rolled_back' | 'aborted' | 'alternated'
  /** New steps to execute if alternated/decomposed */
  newSteps?: AgentStep[]
  /** Error message if failed */
  error?: string
}

// ============================================================================
// Error Classification
// ============================================================================

/**
 * Network error codes that indicate transient failures
 */
const NETWORK_ERROR_CODES = new Set([
  'ECONNRESET',
  'ETIMEDOUT',
  'EPIPE',
  'ENOTFOUND',
  'EAI_AGAIN',
  'ECONNREFUSED',
  'ENETUNREACH',
  'EHOSTUNREACH',
])

/**
 * Permission error codes
 */
const PERMISSION_ERROR_CODES = new Set(['EACCES', 'EPERM'])

/**
 * Not found error codes
 */
const NOT_FOUND_ERROR_CODES = new Set(['ENOENT', 'ENOTDIR'])

/**
 * Classify an error into a category
 */
export function classifyError(error: string | Error | unknown): ErrorCategory {
  const errorStr =
    typeof error === 'string'
      ? error.toLowerCase()
      : error instanceof Error
        ? error.message.toLowerCase()
        : String(error).toLowerCase()

  // Check for error codes
  const errorCode = extractErrorCode(error)
  if (errorCode) {
    if (NETWORK_ERROR_CODES.has(errorCode)) return 'network'
    if (PERMISSION_ERROR_CODES.has(errorCode)) return 'permission'
    if (NOT_FOUND_ERROR_CODES.has(errorCode)) return 'not_found'
  }

  // Pattern matching on error message
  if (errorStr.includes('permission denied') || errorStr.includes('access denied')) {
    return 'permission'
  }

  if (
    errorStr.includes('not found') ||
    errorStr.includes('no such file') ||
    errorStr.includes('does not exist')
  ) {
    return 'not_found'
  }

  if (
    errorStr.includes('timeout') ||
    errorStr.includes('timed out') ||
    errorStr.includes('deadline')
  ) {
    return 'timeout'
  }

  if (
    errorStr.includes('connection') ||
    errorStr.includes('network') ||
    errorStr.includes('socket') ||
    errorStr.includes('fetch failed')
  ) {
    return 'network'
  }

  if (
    errorStr.includes('invalid') ||
    errorStr.includes('validation') ||
    errorStr.includes('expected')
  ) {
    return 'validation'
  }

  if (
    errorStr.includes('syntax') ||
    errorStr.includes('parse') ||
    errorStr.includes('unexpected token')
  ) {
    return 'syntax'
  }

  if (
    errorStr.includes('out of memory') ||
    errorStr.includes('disk full') ||
    errorStr.includes('no space')
  ) {
    return 'resource'
  }

  if (
    errorStr.includes('rate limit') ||
    errorStr.includes('too many requests') ||
    errorStr.includes('429')
  ) {
    return 'rate_limit'
  }

  return 'unknown'
}

/**
 * Extract error code from various error formats
 */
function extractErrorCode(error: unknown): string | null {
  if (typeof error !== 'object' || error === null) {
    return null
  }

  // Direct code property
  if ('code' in error && typeof (error as { code: unknown }).code === 'string') {
    return (error as { code: string }).code
  }

  // Nested cause
  if ('cause' in error && typeof (error as { cause: unknown }).cause === 'object') {
    const cause = (error as { cause: Record<string, unknown> }).cause
    if (cause && 'code' in cause && typeof cause.code === 'string') {
      return cause.code
    }
  }

  return null
}

/**
 * Determine if an error category is retryable
 */
export function isRetryableCategory(category: ErrorCategory): boolean {
  return category === 'network' || category === 'timeout' || category === 'rate_limit'
}

/**
 * Get recommended strategy for an error category
 */
export function getStrategyForCategory(category: ErrorCategory): RecoveryStrategy {
  switch (category) {
    case 'network':
    case 'timeout':
    case 'rate_limit':
      return 'retry'
    case 'permission':
      return 'alternate'
    case 'not_found':
      return 'decompose'
    case 'validation':
      return 'alternate'
    case 'syntax':
      return 'abort'
    case 'resource':
      return 'abort'
    case 'unknown':
      return 'retry'
  }
}

// ============================================================================
// Retry Logic
// ============================================================================

/**
 * Sleep for a duration with optional abort support
 */
async function sleep(ms: number, signal?: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(resolve, ms)

    if (signal) {
      if (signal.aborted) {
        clearTimeout(timeout)
        reject(new DOMException('Aborted', 'AbortError'))
        return
      }

      const abortHandler = () => {
        clearTimeout(timeout)
        reject(new DOMException('Aborted', 'AbortError'))
      }
      signal.addEventListener('abort', abortHandler, { once: true })
    }
  })
}

/**
 * Calculate delay with exponential backoff and jitter
 */
export function calculateBackoffDelay(attempt: number, options: RetryOptions): number {
  // Exponential backoff: delay = initialDelay * 2^attempt
  const exponentialDelay = options.initialDelayMs * 2 ** (attempt - 1)
  const boundedDelay = Math.min(exponentialDelay, options.maxDelayMs)

  // Add jitter
  const jitter = boundedDelay * options.jitterFactor * (Math.random() * 2 - 1)
  return Math.max(0, boundedDelay + jitter)
}

/**
 * Retry a function with exponential backoff
 */
export async function retryWithBackoff<T>(
  fn: () => Promise<T>,
  options: Partial<RetryOptions> = {}
): Promise<T> {
  const opts = { ...DEFAULT_RETRY_OPTIONS, ...options }

  if (opts.signal?.aborted) {
    throw new DOMException('Aborted', 'AbortError')
  }

  let lastError: unknown

  for (let attempt = 1; attempt <= opts.maxAttempts; attempt++) {
    if (opts.signal?.aborted) {
      throw new DOMException('Aborted', 'AbortError')
    }

    try {
      return await fn()
    } catch (error) {
      lastError = error

      // Don't retry on abort
      if (error instanceof Error && error.name === 'AbortError') {
        throw error
      }

      // Check if error is retryable
      const category = classifyError(error)
      if (!isRetryableCategory(category)) {
        throw error
      }

      // Last attempt - throw
      if (attempt >= opts.maxAttempts) {
        throw error
      }

      // Calculate delay and wait
      const delayMs = calculateBackoffDelay(attempt, opts)

      if (opts.onRetry) {
        opts.onRetry(attempt, error, delayMs)
      }

      await sleep(delayMs, opts.signal)
    }
  }

  throw lastError
}

// ============================================================================
// Recovery Manager
// ============================================================================

/**
 * Manages recovery state and actions for agent execution
 */
export class RecoveryManager {
  private state: RollbackState
  private retryCounters = new Map<string, number>()
  private maxRetries: number

  constructor(maxRetries = 3) {
    this.maxRetries = maxRetries
    this.state = {
      executedSteps: [],
      rolledBackSteps: [],
      modifiedFiles: [],
      hasSnapshot: false,
    }
  }

  /**
   * Record a step as executed
   */
  recordStep(step: AgentStep): void {
    this.state.executedSteps.push(step)

    // Track modified files from tool calls
    for (const toolCall of step.toolsCalled) {
      const files = this.extractModifiedFiles(toolCall)
      this.state.modifiedFiles.push(...files)
    }
  }

  /**
   * Get the current rollback state
   */
  getState(): Readonly<RollbackState> {
    return this.state
  }

  /**
   * Check if a step can be retried
   */
  canRetry(stepId: string): boolean {
    const count = this.retryCounters.get(stepId) ?? 0
    return count < this.maxRetries
  }

  /**
   * Increment retry counter for a step
   */
  incrementRetry(stepId: string): number {
    const count = (this.retryCounters.get(stepId) ?? 0) + 1
    this.retryCounters.set(stepId, count)
    return count
  }

  /**
   * Execute a recovery action based on a plan
   */
  async executeRecovery(plan: RecoveryPlan, signal?: AbortSignal): Promise<RecoveryActionResult> {
    const { failedStep, strategy } = plan

    switch (strategy) {
      case 'retry':
        return this.handleRetry(failedStep, signal)

      case 'skip':
        return this.handleSkip(failedStep)

      case 'rollback':
        return this.handleRollback(failedStep)

      case 'alternate':
      case 'decompose':
        return this.handleAlternate(plan)

      case 'abort':
        return {
          success: false,
          action: 'aborted',
          error: 'Recovery not possible - aborting execution',
        }

      default:
        return {
          success: false,
          action: 'aborted',
          error: `Unknown recovery strategy: ${strategy}`,
        }
    }
  }

  /**
   * Handle retry strategy
   */
  private async handleRetry(step: AgentStep, signal?: AbortSignal): Promise<RecoveryActionResult> {
    if (!this.canRetry(step.id)) {
      return {
        success: false,
        action: 'aborted',
        error: `Maximum retries (${this.maxRetries}) exceeded for step ${step.id}`,
      }
    }

    const retryCount = this.incrementRetry(step.id)

    // Wait with exponential backoff before retry
    const delayMs = calculateBackoffDelay(retryCount, DEFAULT_RETRY_OPTIONS)
    await sleep(delayMs, signal)

    return {
      success: true,
      action: 'retried',
    }
  }

  /**
   * Handle skip strategy
   */
  private handleSkip(step: AgentStep): RecoveryActionResult {
    // Mark step as skipped in state
    step.status = 'skipped'

    return {
      success: true,
      action: 'skipped',
    }
  }

  /**
   * Handle rollback strategy
   */
  private handleRollback(step: AgentStep): RecoveryActionResult {
    // Add to rolled back list
    this.state.rolledBackSteps.push(step)

    // In a real implementation, this would:
    // 1. Revert file changes using git or snapshots
    // 2. Clean up any created resources
    // 3. Reset state to before the step

    return {
      success: true,
      action: 'rolled_back',
    }
  }

  /**
   * Handle alternate/decompose strategy
   */
  private handleAlternate(plan: RecoveryPlan): RecoveryActionResult {
    if (!plan.alternativeSteps || plan.alternativeSteps.length === 0) {
      return {
        success: false,
        action: 'aborted',
        error: 'No alternative steps provided',
      }
    }

    // Convert PlannedStep to AgentStep format
    const newSteps: AgentStep[] = plan.alternativeSteps.map((planned, index) => ({
      id: `alt-${plan.failedStep.id}-${index}`,
      turn: plan.failedStep.turn,
      description: planned.description,
      toolsCalled: [],
      status: 'pending' as const,
      retryCount: 0,
    }))

    return {
      success: true,
      action: 'alternated',
      newSteps,
    }
  }

  /**
   * Extract modified file paths from a tool call
   */
  private extractModifiedFiles(toolCall: ToolCallInfo): string[] {
    const files: string[] = []

    // Check for file path in common argument names
    const pathArgs = ['path', 'file_path', 'filePath', 'filename', 'target']
    for (const arg of pathArgs) {
      const value = toolCall.args[arg]
      if (typeof value === 'string' && value.startsWith('/')) {
        files.push(value)
      }
    }

    // Check for write/edit tool names
    const writingTools = ['write', 'edit', 'create', 'append', 'mkdir']
    if (writingTools.some((t) => toolCall.name.toLowerCase().includes(t))) {
      // Already captured above, but ensure we're tracking
    }

    return files
  }

  /**
   * Set snapshot information
   */
  setSnapshot(snapshotId: string): void {
    this.state.hasSnapshot = true
    this.state.snapshotId = snapshotId
  }

  /**
   * Reset the recovery manager
   */
  reset(): void {
    this.state = {
      executedSteps: [],
      rolledBackSteps: [],
      modifiedFiles: [],
      hasSnapshot: false,
    }
    this.retryCounters.clear()
  }
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create a new recovery manager
 */
export function createRecoveryManager(maxRetries?: number): RecoveryManager {
  return new RecoveryManager(maxRetries)
}
