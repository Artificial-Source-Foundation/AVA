/**
 * Tool Error Types
 * Error handling for tool execution
 */

/** Error types for tool operations */
export enum ToolErrorType {
  INVALID_PARAMS = 'INVALID_PARAMS',
  FILE_NOT_FOUND = 'FILE_NOT_FOUND',
  FILE_ALREADY_EXISTS = 'FILE_ALREADY_EXISTS',
  PERMISSION_DENIED = 'PERMISSION_DENIED',
  BINARY_FILE = 'BINARY_FILE',
  PATH_IS_DIRECTORY = 'PATH_IS_DIRECTORY',
  INVALID_PATTERN = 'INVALID_PATTERN',
  CONTENT_TOO_LARGE = 'CONTENT_TOO_LARGE',
  EXECUTION_TIMEOUT = 'EXECUTION_TIMEOUT',
  EXECUTION_ABORTED = 'EXECUTION_ABORTED',
  UNKNOWN = 'UNKNOWN',
}

/** Custom error class for tool operations */
export class ToolError extends Error {
  constructor(
    message: string,
    public type: ToolErrorType,
    public toolName?: string
  ) {
    super(message)
    this.name = 'ToolError'
  }

  /** Create from unknown error */
  static from(err: unknown, toolName?: string): ToolError {
    if (err instanceof ToolError) {
      return err
    }

    const message = err instanceof Error ? err.message : String(err)

    // Try to infer error type from message
    let type = ToolErrorType.UNKNOWN
    const lowerMsg = message.toLowerCase()

    if (lowerMsg.includes('not found') || lowerMsg.includes('no such file')) {
      type = ToolErrorType.FILE_NOT_FOUND
    } else if (lowerMsg.includes('already exists') || lowerMsg.includes('file exists')) {
      type = ToolErrorType.FILE_ALREADY_EXISTS
    } else if (lowerMsg.includes('permission') || lowerMsg.includes('access denied')) {
      type = ToolErrorType.PERMISSION_DENIED
    } else if (lowerMsg.includes('is a directory')) {
      type = ToolErrorType.PATH_IS_DIRECTORY
    } else if (lowerMsg.includes('too large') || lowerMsg.includes('exceeds')) {
      type = ToolErrorType.CONTENT_TOO_LARGE
    } else if (lowerMsg.includes('timeout')) {
      type = ToolErrorType.EXECUTION_TIMEOUT
    } else if (lowerMsg.includes('abort')) {
      type = ToolErrorType.EXECUTION_ABORTED
    }

    return new ToolError(message, type, toolName)
  }
}
