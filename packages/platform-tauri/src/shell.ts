/**
 * Tauri Shell Implementation with Process Management
 *
 * Implements IShell interface for Tauri platform with:
 * - Inactivity timeout support (via JS tracking)
 * - Graceful kill with fallback
 * - Output buffering for tools that need it
 *
 * Limitations:
 * - killProcessGroup: Not supported (Tauri doesn't expose process groups)
 * - PID: Not exposed by Tauri shell API
 * - Streams: stdin/stdout/stderr are null (use wait() for output)
 */

import type { ChildProcess, ExecOptions, ExecResult, IShell, SpawnOptions } from '@ava/core-v2'
import { Command } from '@tauri-apps/plugin-shell'

/** Default inactivity timeout (30 seconds) */
const DEFAULT_INACTIVITY_TIMEOUT = 30000

export class TauriShell implements IShell {
  async exec(command: string, options?: ExecOptions): Promise<ExecResult> {
    const cmd = Command.create('sh', ['-c', command], {
      cwd: options?.cwd,
      env: options?.env,
    })

    const execPromise = cmd.execute()
    const result = options?.timeout
      ? await Promise.race([
          execPromise,
          new Promise<never>((_, reject) =>
            setTimeout(
              () => reject(new Error(`Command timed out after ${options.timeout}ms`)),
              options.timeout
            )
          ),
        ])
      : await execPromise

    return {
      stdout: result.stdout,
      stderr: result.stderr,
      exitCode: result.code ?? 0,
    }
  }

  spawn(command: string, args: string[], options?: SpawnOptions): ChildProcess {
    const cmd = Command.create(command, args, {
      cwd: options?.cwd,
      env: options?.env,
    })

    // Track output for wait()
    let stdout = ''
    let stderr = ''
    let exitCode = 0
    let finished = false
    let childInstance: { kill: () => Promise<void> } | null = null

    // Inactivity tracking
    let lastActivityTime = Date.now()
    let inactivityTimer: ReturnType<typeof setTimeout> | null = null
    const inactivityTimeout = options?.inactivityTimeout ?? DEFAULT_INACTIVITY_TIMEOUT

    const resetInactivityTimer = () => {
      lastActivityTime = Date.now()
      if (inactivityTimer) {
        clearTimeout(inactivityTimer)
        inactivityTimer = null
      }
      if (!finished) {
        inactivityTimer = setTimeout(() => {
          console.warn(`Process killed due to inactivity (${inactivityTimeout}ms)`)
          killFn()
        }, inactivityTimeout)
      }
    }

    // Set up output handlers before spawning
    cmd.stdout.on('data', (line: string) => {
      stdout += `${line}\n`
      resetInactivityTimer()
    })
    cmd.stderr.on('data', (line: string) => {
      stderr += `${line}\n`
      resetInactivityTimer()
    })
    cmd.on('close', (data: { code: number | null }) => {
      exitCode = data.code ?? 0
      finished = true
      if (inactivityTimer) {
        clearTimeout(inactivityTimer)
        inactivityTimer = null
      }
    })

    // Spawn the process
    const childPromise = cmd.spawn().then((child) => {
      childInstance = child
      return child
    })

    // Kill function with graceful escalation
    const killFn = () => {
      // Note: killProcessGroup is not supported by Tauri
      // Warn regardless of process state so callers know the flag was ignored
      if (options?.killProcessGroup) {
        console.warn('killProcessGroup is not supported on Tauri platform')
      }

      if (finished) return

      // Clear inactivity timer
      if (inactivityTimer) {
        clearTimeout(inactivityTimer)
        inactivityTimer = null
      }

      // Kill the process
      if (childInstance) {
        void childInstance.kill()
      } else {
        // If child hasn't spawned yet, kill after it does
        childPromise.then((child) => child.kill())
      }
    }

    return {
      pid: undefined, // Tauri doesn't expose PID directly
      stdin: null, // Tauri's shell API doesn't expose streams
      stdout: null, // Use wait() to get output
      stderr: null, // Use wait() to get output
      kill: killFn,
      wait: async (): Promise<ExecResult> => {
        // Wait for process to complete
        await childPromise

        // Start inactivity timer if configured
        if (inactivityTimeout > 0) {
          resetInactivityTimer()
        }

        // Poll until finished
        while (!finished) {
          await new Promise((r) => setTimeout(r, 50))

          // Check inactivity
          if (inactivityTimeout > 0 && Date.now() - lastActivityTime > inactivityTimeout) {
            killFn()
            throw new Error(`Process killed due to inactivity (${inactivityTimeout}ms)`)
          }
        }

        return { stdout, stderr, exitCode }
      },
    }
  }
}
