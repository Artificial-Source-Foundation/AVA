/**
 * Docker Sandbox
 * Execute commands in a Docker container for isolation
 *
 * Features:
 * - Volume mount of working directory
 * - Network isolation (optional)
 * - Memory and CPU limits
 * - Timeout enforcement
 * - Graceful abort via container kill
 */

import { getPlatform } from '../../platform.js'
import type { Sandbox, SandboxConfig, SandboxExecResult } from './types.js'
import { DEFAULT_SANDBOX_CONFIG } from './types.js'

// ============================================================================
// DockerSandbox
// ============================================================================

/**
 * Docker-based sandbox for isolated command execution
 */
export class DockerSandbox implements Sandbox {
  readonly type = 'docker'
  private config: SandboxConfig

  constructor(config: Partial<SandboxConfig> = {}) {
    this.config = { ...DEFAULT_SANDBOX_CONFIG, ...config, mode: 'docker' }
  }

  /**
   * Execute a command in a Docker container
   */
  async exec(
    command: string,
    workingDirectory: string,
    signal?: AbortSignal
  ): Promise<SandboxExecResult> {
    const args = this.buildDockerArgs(command, workingDirectory)
    const shell = getPlatform().shell

    const child = shell.spawn('docker', args, {
      cwd: workingDirectory,
      killProcessGroup: true,
    })

    let stdout = ''
    let stderr = ''
    let timedOut = false

    // Abort handling
    const abortHandler = () => {
      child.kill()
    }
    if (signal) {
      signal.addEventListener('abort', abortHandler, { once: true })
    }

    // Timeout enforcement
    const timeoutMs = this.config.timeoutSeconds * 1000
    const timeoutId = setTimeout(() => {
      timedOut = true
      child.kill()
    }, timeoutMs)

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
        // Stream closed
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
        // Stream closed
      } finally {
        reader.releaseLock()
      }
    }

    const result = await child.wait()

    clearTimeout(timeoutId)
    if (signal) {
      signal.removeEventListener('abort', abortHandler)
    }

    return {
      exitCode: result.exitCode,
      stdout,
      stderr,
      timedOut,
    }
  }

  /**
   * Check if Docker is available
   */
  async isAvailable(): Promise<boolean> {
    try {
      const shell = getPlatform().shell
      const child = shell.spawn('docker', ['version', '--format', '{{.Server.Version}}'], {})

      const result = await child.wait()
      return result.exitCode === 0
    } catch {
      return false
    }
  }

  /**
   * Clean up (no persistent containers)
   */
  async cleanup(): Promise<void> {
    // --rm flag ensures containers are cleaned up automatically
  }

  /**
   * Build docker run arguments
   */
  buildDockerArgs(command: string, workingDirectory: string): string[] {
    const args: string[] = [
      'run',
      '--rm', // Auto-remove container
      '-v',
      `${workingDirectory}:/workspace`, // Mount working directory
      '-w',
      '/workspace', // Set working directory
      '--memory',
      this.config.memoryLimit, // Memory limit
      '--cpus',
      this.config.cpuLimit, // CPU limit
    ]

    // Network isolation
    if (!this.config.networkAccess) {
      args.push('--network', 'none')
    }

    // Image
    args.push(this.config.image)

    // Command
    args.push('sh', '-c', command)

    return args
  }

  /**
   * Get current configuration
   */
  getConfig(): SandboxConfig {
    return { ...this.config }
  }
}
