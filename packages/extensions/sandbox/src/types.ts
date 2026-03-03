/**
 * Sandbox types.
 */

export interface SandboxConfig {
  image: string
  timeout: number
  maxMemoryMB: number
  networkEnabled: boolean
  mountPaths: string[]
}

export const DEFAULT_SANDBOX_CONFIG: SandboxConfig = {
  image: 'node:22-slim',
  timeout: 60_000,
  maxMemoryMB: 512,
  networkEnabled: false,
  mountPaths: [],
}

export interface SandboxResult {
  stdout: string
  stderr: string
  exitCode: number
  durationMs: number
  timedOut: boolean
}

export interface Sandbox {
  name: 'native' | 'docker' | 'noop'
  run(config: SandboxConfig, code: string): Promise<SandboxResult>
}
