/**
 * Sandbox runner — executes code in Docker containers.
 */

import type { IShell } from '@ava/core-v2/platform'
import type { SandboxConfig, SandboxResult } from './types.js'
import { DEFAULT_SANDBOX_CONFIG } from './types.js'

/**
 * Build a docker run command from config and code.
 */
export function buildDockerCommand(config: SandboxConfig, code: string): string {
  const flags: string[] = ['--rm']

  // Memory limit
  flags.push(`--memory=${config.maxMemoryMB}m`)

  // Network
  if (!config.networkEnabled) flags.push('--network=none')

  // Mount paths
  for (const mount of config.mountPaths) {
    flags.push(`-v "${mount}:${mount}:ro"`)
  }

  // Escape code for shell
  const escapedCode = code.replace(/'/g, "'\\''")

  return `docker run ${flags.join(' ')} ${config.image} sh -c '${escapedCode}'`
}

/**
 * Run code in a sandboxed Docker container.
 */
export async function runInSandbox(
  shell: IShell,
  config: SandboxConfig = DEFAULT_SANDBOX_CONFIG,
  code: string
): Promise<SandboxResult> {
  const command = buildDockerCommand(config, code)
  const startTime = Date.now()

  try {
    // Use a timeout wrapper
    const timeoutPromise = new Promise<never>((_, reject) => {
      setTimeout(() => reject(new Error('TIMEOUT')), config.timeout)
    })

    const result = await Promise.race([shell.exec(command), timeoutPromise])

    return {
      stdout: result.stdout,
      stderr: result.stderr,
      exitCode: result.exitCode,
      durationMs: Date.now() - startTime,
      timedOut: false,
    }
  } catch (err) {
    const isTimeout = err instanceof Error && err.message === 'TIMEOUT'
    return {
      stdout: '',
      stderr: isTimeout ? 'Execution timed out' : String(err),
      exitCode: isTimeout ? 124 : 1,
      durationMs: Date.now() - startTime,
      timedOut: isTimeout,
    }
  }
}

/**
 * Check if Docker is available.
 */
export async function isDockerAvailable(shell: IShell): Promise<boolean> {
  try {
    const result = await shell.exec('docker --version')
    return result.exitCode === 0
  } catch {
    return false
  }
}
