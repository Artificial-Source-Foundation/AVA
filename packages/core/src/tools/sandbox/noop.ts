/**
 * Noop Sandbox
 * Passes through to host execution (no isolation)
 * Used when sandbox mode is 'none' (default)
 */

import { getPlatform } from '../../platform.js'
import type { Sandbox, SandboxExecResult } from './types.js'

// ============================================================================
// NoopSandbox
// ============================================================================

/**
 * No-op sandbox — executes on the host directly
 * This is the default behavior (backward compatible)
 */
export class NoopSandbox implements Sandbox {
  readonly type = 'none'

  async exec(
    command: string,
    workingDirectory: string,
    signal?: AbortSignal
  ): Promise<SandboxExecResult> {
    const shell = getPlatform().shell

    const child = shell.spawn('bash', ['-c', command], {
      cwd: workingDirectory,
      killProcessGroup: true,
    })

    let stdout = ''
    let stderr = ''
    let aborted = false
    const timedOut = false

    // Abort handling
    const abortHandler = () => {
      aborted = true
      child.kill()
    }
    if (signal) {
      signal.addEventListener('abort', abortHandler, { once: true })
    }

    // Read stdout
    if (child.stdout) {
      const reader = child.stdout.getReader()
      const decoder = new TextDecoder()
      try {
        while (true) {
          const { done, value } = await reader.read()
          if (done) break
          stdout += decoder.decode(value, { stream: true })
        }
      } catch {
        // Stream may be closed
      } finally {
        reader.releaseLock()
      }
    }

    // Read stderr
    if (child.stderr) {
      const reader = child.stderr.getReader()
      const decoder = new TextDecoder()
      try {
        while (true) {
          const { done, value } = await reader.read()
          if (done) break
          stderr += decoder.decode(value, { stream: true })
        }
      } catch {
        // Stream may be closed
      } finally {
        reader.releaseLock()
      }
    }

    const result = await child.wait()

    if (signal) {
      signal.removeEventListener('abort', abortHandler)
    }

    return {
      exitCode: aborted ? 130 : result.exitCode,
      stdout,
      stderr,
      timedOut,
    }
  }

  async isAvailable(): Promise<boolean> {
    return true // Host is always available
  }

  async cleanup(): Promise<void> {
    // Nothing to clean up
  }
}
