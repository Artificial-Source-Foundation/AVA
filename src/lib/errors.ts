/**
 * Delta9 Rich Error Handling
 *
 * Provides error types with recovery suggestions for better DX:
 * - Delta9Error class with code, message, suggestions, and context
 * - Predefined errors with actionable recovery hints
 * - Formatted error output for tool responses
 */

// =============================================================================
// Error Types
// =============================================================================

export interface Delta9ErrorOptions {
  code: string
  message: string
  suggestions?: string[]
  context?: Record<string, unknown>
  cause?: Error
}

/**
 * Base error class for Delta9 with recovery suggestions.
 *
 * @example
 * throw new Delta9Error({
 *   code: 'TASK_NOT_FOUND',
 *   message: 'Task bg_123 not found',
 *   suggestions: ['Use background_list to see available tasks']
 * })
 */
export class Delta9Error extends Error {
  code: string
  suggestions: string[]
  context?: Record<string, unknown>

  constructor(opts: Delta9ErrorOptions) {
    super(opts.message)
    this.name = 'Delta9Error'
    this.code = opts.code
    this.suggestions = opts.suggestions ?? []
    this.context = opts.context
    if (opts.cause) {
      this.cause = opts.cause
    }
  }

  /**
   * Format error for tool response (JSON-serializable)
   */
  toJSON(): Record<string, unknown> {
    return {
      success: false,
      error: {
        code: this.code,
        message: this.message,
        suggestions: this.suggestions.length > 0 ? this.suggestions : undefined,
        context: this.context,
      },
    }
  }

  /**
   * Format error for tool response as JSON string
   */
  toToolResponse(): string {
    return JSON.stringify(this.toJSON())
  }
}

// =============================================================================
// Predefined Error Factories
// =============================================================================

export const errors = {
  // -------------------------------------------------------------------------
  // Task Errors
  // -------------------------------------------------------------------------

  taskNotFound: (taskId: string) =>
    new Delta9Error({
      code: 'TASK_NOT_FOUND',
      message: `Background task '${taskId}' not found`,
      suggestions: [
        'Use background_list to see available tasks',
        'Task may have been cleaned up (30 minute TTL)',
        'Check if task ID is correct (format: bg_xxxxx)',
      ],
      context: { taskId },
    }),

  taskAlreadyComplete: (taskId: string, status: string) =>
    new Delta9Error({
      code: 'TASK_ALREADY_COMPLETE',
      message: `Task '${taskId}' is already ${status}`,
      suggestions: [
        'Use background_output to retrieve the result',
        'Use background_cleanup to remove old tasks',
      ],
      context: { taskId, status },
    }),

  taskCancelFailed: (taskId: string, status: string) =>
    new Delta9Error({
      code: 'TASK_CANCEL_FAILED',
      message: `Cannot cancel task '${taskId}' in status: ${status}`,
      suggestions: [
        'Only pending or running tasks can be cancelled',
        'Task may have already completed',
      ],
      context: { taskId, status },
    }),

  taskTimeout: (taskId: string, timeoutMs: number) =>
    new Delta9Error({
      code: 'TASK_TIMEOUT',
      message: `Task '${taskId}' timed out after ${Math.round(timeoutMs / 1000)}s`,
      suggestions: [
        'Check background_list for task status',
        'Task may still be running - use background_output to check',
        'Consider increasing timeout or simplifying the task',
      ],
      context: { taskId, timeoutMs },
    }),

  // -------------------------------------------------------------------------
  // Mission Errors
  // -------------------------------------------------------------------------

  noActiveMission: () =>
    new Delta9Error({
      code: 'NO_ACTIVE_MISSION',
      message: 'No active mission',
      suggestions: [
        'Create a mission first with mission_create',
        'Check mission status with mission_status',
      ],
    }),

  missionAlreadyExists: (missionId: string) =>
    new Delta9Error({
      code: 'MISSION_ALREADY_EXISTS',
      message: `A mission is already active: ${missionId}`,
      suggestions: [
        'Complete or abort the current mission first',
        'Use mission_status to see current mission details',
      ],
      context: { missionId },
    }),

  missionTaskNotFound: (taskId: string) =>
    new Delta9Error({
      code: 'MISSION_TASK_NOT_FOUND',
      message: `Mission task '${taskId}' not found`,
      suggestions: ['Use mission_status to see all tasks', 'Check if task ID is correct'],
      context: { taskId },
    }),

  invalidMissionStatus: (from: string, to: string) =>
    new Delta9Error({
      code: 'INVALID_MISSION_STATUS',
      message: `Cannot transition mission from '${from}' to '${to}'`,
      suggestions: [
        'Check valid status transitions in documentation',
        'Use mission_status to see current state',
      ],
      context: { from, to },
    }),

  // -------------------------------------------------------------------------
  // SDK/Client Errors
  // -------------------------------------------------------------------------

  sdkUnavailable: () =>
    new Delta9Error({
      code: 'SDK_UNAVAILABLE',
      message: 'OpenCode SDK client not available',
      suggestions: [
        'Running in simulation mode - background tasks will be simulated',
        'Ensure Delta9 is loaded as an OpenCode plugin',
        'Check that OpenCode is properly initialized',
      ],
    }),

  sessionCreateFailed: (error: Error) =>
    new Delta9Error({
      code: 'SESSION_CREATE_FAILED',
      message: `Failed to create agent session: ${error.message}`,
      suggestions: [
        'Check OpenCode service status',
        'Verify API credentials and permissions',
        'Try again - may be a transient error',
      ],
      cause: error,
    }),

  sessionAborted: (sessionId: string) =>
    new Delta9Error({
      code: 'SESSION_ABORTED',
      message: `Session '${sessionId}' was aborted`,
      suggestions: [
        'Session may have been cancelled externally',
        'Check background_list for task status',
        'Retry the task if needed',
      ],
      context: { sessionId },
    }),

  // -------------------------------------------------------------------------
  // Council Errors
  // -------------------------------------------------------------------------

  councilNotConfigured: () =>
    new Delta9Error({
      code: 'COUNCIL_NOT_CONFIGURED',
      message: 'Council has no Strategic Advisors configured',
      suggestions: [
        'Add Strategic Advisor models to delta9.json configuration',
        'Example: {"council": {"members": [{"name": "Cipher", "model": "openai/gpt-5.2-codex"}]}}',
        'Use consult_council with specific models to override',
      ],
    }),

  councilConsultFailed: (reason: string) =>
    new Delta9Error({
      code: 'COUNCIL_CONSULT_FAILED',
      message: `Council consultation failed: ${reason}`,
      suggestions: [
        'Check Strategic Advisor model availability',
        'Try quick_consult for a single advisor',
        'Verify API credentials for all configured providers',
      ],
    }),

  // -------------------------------------------------------------------------
  // Config Errors
  // -------------------------------------------------------------------------

  configNotFound: (path: string) =>
    new Delta9Error({
      code: 'CONFIG_NOT_FOUND',
      message: `Configuration file not found at '${path}'`,
      suggestions: [
        'Create a delta9.json in the project root',
        'Or create ~/.delta9/config.json for global config',
        'Default configuration will be used',
      ],
      context: { path },
    }),

  configInvalid: (path: string, validationErrors: string[]) =>
    new Delta9Error({
      code: 'CONFIG_INVALID',
      message: `Invalid configuration in '${path}'`,
      suggestions: [
        'Check configuration schema in documentation',
        ...validationErrors.map((e) => `Fix: ${e}`),
      ],
      context: { path, validationErrors },
    }),

  // -------------------------------------------------------------------------
  // Validation Errors
  // -------------------------------------------------------------------------

  validationFailed: (taskId: string, failures: string[]) =>
    new Delta9Error({
      code: 'VALIDATION_FAILED',
      message: `Task '${taskId}' failed validation`,
      suggestions: [
        'Review the acceptance criteria',
        ...failures.map((f) => `Address: ${f}`),
        'Use retry_task to attempt again after fixes',
      ],
      context: { taskId, failures },
    }),

  // -------------------------------------------------------------------------
  // Memory Errors
  // -------------------------------------------------------------------------

  memoryKeyNotFound: (key: string) =>
    new Delta9Error({
      code: 'MEMORY_KEY_NOT_FOUND',
      message: `Memory key '${key}' not found`,
      suggestions: [
        'Use memory_list to see available keys',
        'Check if key was previously set',
        'Keys are case-sensitive',
      ],
      context: { key },
    }),
}

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * Check if an error is a Delta9Error
 */
export function isDelta9Error(error: unknown): error is Delta9Error {
  return error instanceof Delta9Error
}

/**
 * Format any error for tool response
 */
export function formatErrorResponse(error: unknown): string {
  if (isDelta9Error(error)) {
    return error.toToolResponse()
  }

  if (error instanceof Error) {
    return JSON.stringify({
      success: false,
      error: {
        code: 'UNKNOWN_ERROR',
        message: error.message,
        suggestions: ['Check logs for more details', 'Report bug if issue persists'],
      },
    })
  }

  return JSON.stringify({
    success: false,
    error: {
      code: 'UNKNOWN_ERROR',
      message: String(error),
      suggestions: ['Check logs for more details'],
    },
  })
}
