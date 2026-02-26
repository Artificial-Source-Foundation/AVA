/**
 * Tool error types.
 */

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
  BINARY_OUTPUT = 'BINARY_OUTPUT',
  INACTIVITY_TIMEOUT = 'INACTIVITY_TIMEOUT',
  NOT_SUPPORTED = 'NOT_SUPPORTED',
  UNKNOWN = 'UNKNOWN',
}

export class ToolError extends Error {
  readonly type: ToolErrorType
  readonly toolName?: string

  constructor(message: string, type: ToolErrorType, toolName?: string) {
    super(message)
    this.name = 'ToolError'
    this.type = type
    this.toolName = toolName
  }

  static from(err: unknown, toolName?: string): ToolError {
    if (err instanceof ToolError) return err

    const message = err instanceof Error ? err.message : String(err)
    const type = inferErrorType(message)
    return new ToolError(message, type, toolName)
  }
}

function inferErrorType(message: string): ToolErrorType {
  const lower = message.toLowerCase()
  if (lower.includes('enoent') || lower.includes('not found') || lower.includes('no such file')) {
    return ToolErrorType.FILE_NOT_FOUND
  }
  if (lower.includes('eexist') || lower.includes('already exists')) {
    return ToolErrorType.FILE_ALREADY_EXISTS
  }
  if (lower.includes('eperm') || lower.includes('eacces') || lower.includes('permission')) {
    return ToolErrorType.PERMISSION_DENIED
  }
  if (lower.includes('binary')) {
    return ToolErrorType.BINARY_FILE
  }
  if (lower.includes('is a directory') || lower.includes('eisdir')) {
    return ToolErrorType.PATH_IS_DIRECTORY
  }
  if (lower.includes('timeout')) {
    return ToolErrorType.EXECUTION_TIMEOUT
  }
  if (lower.includes('abort')) {
    return ToolErrorType.EXECUTION_ABORTED
  }
  return ToolErrorType.UNKNOWN
}
