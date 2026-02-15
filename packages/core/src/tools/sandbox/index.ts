/**
 * Sandbox System
 * Container-based isolation for command execution
 */

import { DockerSandbox } from './docker.js'
import { NoopSandbox } from './noop.js'
import type { Sandbox, SandboxConfig } from './types.js'
import { DEFAULT_SANDBOX_CONFIG } from './types.js'

export { DockerSandbox } from './docker.js'
export { NoopSandbox } from './noop.js'
export type { Sandbox, SandboxConfig, SandboxExecResult } from './types.js'
export { DEFAULT_SANDBOX_CONFIG } from './types.js'

/**
 * Create a sandbox based on configuration
 */
export function createSandbox(config: Partial<SandboxConfig> = {}): Sandbox {
  const mode = config.mode ?? DEFAULT_SANDBOX_CONFIG.mode

  if (mode === 'docker') {
    return new DockerSandbox(config)
  }

  return new NoopSandbox()
}
