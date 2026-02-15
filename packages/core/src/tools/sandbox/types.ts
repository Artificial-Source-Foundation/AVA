/**
 * Sandbox Types
 * Abstraction for sandboxed command execution
 */

// ============================================================================
// Sandbox Interface
// ============================================================================

/** Result of a sandboxed command execution */
export interface SandboxExecResult {
  /** Exit code from the command */
  exitCode: number
  /** Standard output */
  stdout: string
  /** Standard error */
  stderr: string
  /** Whether the command timed out */
  timedOut: boolean
}

/** Configuration for sandbox mode */
export interface SandboxConfig {
  /** Sandbox mode: 'none' (host) or 'docker' */
  mode: 'none' | 'docker'
  /** Docker image to use (default: 'node:20-slim') */
  image: string
  /** Timeout in seconds (default: 120) */
  timeoutSeconds: number
  /** Whether to allow network access (default: false) */
  networkAccess: boolean
  /** Memory limit (default: '512m') */
  memoryLimit: string
  /** CPU limit (default: '1') */
  cpuLimit: string
}

/** Default sandbox configuration */
export const DEFAULT_SANDBOX_CONFIG: SandboxConfig = {
  mode: 'none',
  image: 'node:20-slim',
  timeoutSeconds: 120,
  networkAccess: false,
  memoryLimit: '512m',
  cpuLimit: '1',
}

/**
 * Sandbox interface for executing commands in an isolated environment
 */
export interface Sandbox {
  /** Execute a command in the sandbox */
  exec(command: string, workingDirectory: string, signal?: AbortSignal): Promise<SandboxExecResult>

  /** Check if the sandbox runtime is available */
  isAvailable(): Promise<boolean>

  /** Clean up sandbox resources */
  cleanup(): Promise<void>

  /** Get the sandbox type name */
  readonly type: string
}
