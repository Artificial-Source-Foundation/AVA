/**
 * Node.js Shell Implementation
 */

import type { ChildProcess as NodeChildProcess } from 'node:child_process'
import { exec, spawn } from 'node:child_process'
import { promisify } from 'node:util'
import type { ChildProcess, ExecOptions, ExecResult, IShell, SpawnOptions } from '@ava/core-v2'

const execAsync = promisify(exec)

/** Default grace period before SIGKILL escalation (ms) */
const DEFAULT_SIGKILL_GRACE = 5000

export class NodeShell implements IShell {
  async exec(command: string, options?: ExecOptions): Promise<ExecResult> {
    try {
      const { stdout, stderr } = await execAsync(command, {
        cwd: options?.cwd,
        env: options?.env ? { ...process.env, ...options.env } : undefined,
        timeout: options?.timeout,
        maxBuffer: 10 * 1024 * 1024, // 10MB
      })
      return { stdout, stderr, exitCode: 0 }
    } catch (error) {
      const err = error as {
        stdout?: string
        stderr?: string
        code?: number
      }
      return {
        stdout: err.stdout ?? '',
        stderr: err.stderr ?? '',
        exitCode: err.code ?? 1,
      }
    }
  }

  spawn(command: string, args: string[], options?: SpawnOptions): ChildProcess {
    const child = spawn(command, args, {
      cwd: options?.cwd,
      env: options?.env ? { ...process.env, ...options.env } : undefined,
      stdio: ['pipe', 'pipe', 'pipe'],
      // Create new process group on Unix for proper cleanup
      detached: process.platform !== 'win32' && options?.killProcessGroup,
    })

    // Track if process is still running
    let isRunning = true
    child.on('exit', () => {
      isRunning = false
    })

    // Inactivity timeout handling
    let inactivityTimer: NodeJS.Timeout | null = null

    const resetInactivityTimer = () => {
      if (inactivityTimer) {
        clearTimeout(inactivityTimer)
      }
      if (options?.inactivityTimeout && isRunning) {
        inactivityTimer = setTimeout(() => {
          // Kill with SIGTERM, escalate to SIGKILL
          killWithEscalation('SIGTERM', 2000)
        }, options.inactivityTimeout)
      }
    }

    // Attach inactivity listeners to output streams
    if (options?.inactivityTimeout) {
      child.stdout?.on('data', resetInactivityTimer)
      child.stderr?.on('data', resetInactivityTimer)
      resetInactivityTimer() // Start initial timer
    }

    // Kill with SIGKILL escalation
    const killWithEscalation = async (
      signal: NodeJS.Signals = 'SIGTERM',
      graceMs: number = DEFAULT_SIGKILL_GRACE
    ) => {
      // Clear inactivity timer
      if (inactivityTimer) {
        clearTimeout(inactivityTimer)
        inactivityTimer = null
      }

      if (!isRunning || !child.pid) return

      // Send initial signal
      sendSignal(child, child.pid, signal, options?.killProcessGroup)

      // Wait for graceful exit
      const exited = await Promise.race([
        new Promise<boolean>((resolve) => {
          child.once('exit', () => resolve(true))
        }),
        new Promise<boolean>((resolve) => {
          setTimeout(() => resolve(false), graceMs)
        }),
      ])

      // Force kill if still running
      if (!exited && isRunning && child.pid) {
        sendSignal(child, child.pid, 'SIGKILL', options?.killProcessGroup)
      }
    }

    // Simple kill (backwards compatible)
    const killFn = () => {
      // Clear inactivity timer
      if (inactivityTimer) {
        clearTimeout(inactivityTimer)
        inactivityTimer = null
      }

      if (!child.pid) return
      sendSignal(child, child.pid, 'SIGTERM', options?.killProcessGroup)
    }

    // Convert Node streams to Web Streams
    const stdoutStream = child.stdout
      ? new ReadableStream<Uint8Array>({
          start(controller) {
            child.stdout!.on('data', (chunk) => controller.enqueue(chunk))
            child.stdout!.on('end', () => controller.close())
            child.stdout!.on('error', (err) => controller.error(err))
          },
        })
      : null

    const stderrStream = child.stderr
      ? new ReadableStream<Uint8Array>({
          start(controller) {
            child.stderr!.on('data', (chunk) => controller.enqueue(chunk))
            child.stderr!.on('end', () => controller.close())
            child.stderr!.on('error', (err) => controller.error(err))
          },
        })
      : null

    const stdinStream = child.stdin
      ? new WritableStream<Uint8Array>({
          write(chunk) {
            child.stdin!.write(chunk)
          },
          close() {
            child.stdin!.end()
          },
        })
      : null

    return {
      pid: child.pid,
      stdin: stdinStream,
      stdout: stdoutStream,
      stderr: stderrStream,
      kill: killFn,
      wait: () =>
        new Promise((resolve) => {
          let stdout = ''
          let stderr = ''

          child.stdout?.on('data', (chunk) => {
            stdout += chunk.toString()
          })
          child.stderr?.on('data', (chunk) => {
            stderr += chunk.toString()
          })

          child.on('close', (code) => {
            // Clear inactivity timer on close
            if (inactivityTimer) {
              clearTimeout(inactivityTimer)
              inactivityTimer = null
            }
            resolve({ stdout, stderr, exitCode: code ?? 0 })
          })
        }),
    }
  }
}

/**
 * Send a signal to a process, optionally killing the entire process group
 */
function sendSignal(
  child: NodeChildProcess,
  pid: number,
  signal: NodeJS.Signals,
  killProcessGroup?: boolean
): void {
  if (killProcessGroup && process.platform !== 'win32') {
    // Kill entire process group: kill -- -PGID
    try {
      process.kill(-pid, signal)
    } catch {
      // Process might already be dead
      try {
        child.kill(signal)
      } catch {
        // Ignore
      }
    }
  } else {
    try {
      child.kill(signal)
    } catch {
      // Process might already be dead
    }
  }
}
