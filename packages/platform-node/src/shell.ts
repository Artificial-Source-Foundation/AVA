/**
 * Node.js Shell Implementation
 */

import { exec, spawn } from 'node:child_process'
import { promisify } from 'node:util'
import type { ChildProcess, ExecOptions, ExecResult, IShell, SpawnOptions } from '@estela/core'

const execAsync = promisify(exec)

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

    // Kill function with process group support
    const killFn = (signal: NodeJS.Signals = 'SIGTERM') => {
      if (options?.killProcessGroup && process.platform !== 'win32' && child.pid) {
        // Kill entire process group: kill -- -PGID
        try {
          process.kill(-child.pid, signal)
        } catch {
          // Process might already be dead
          child.kill(signal)
        }
      } else {
        child.kill(signal)
      }
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
            resolve({ stdout, stderr, exitCode: code ?? 0 })
          })
        }),
    }
  }
}
