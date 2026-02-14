/**
 * Tauri Shell Implementation
 */

import type { ChildProcess, ExecOptions, ExecResult, IShell, SpawnOptions } from '@ava/core'
import { Command } from '@tauri-apps/plugin-shell'

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

    // Set up output handlers before spawning
    cmd.stdout.on('data', (line: string) => {
      stdout += `${line}\n`
    })
    cmd.stderr.on('data', (line: string) => {
      stderr += `${line}\n`
    })
    cmd.on('close', (data: { code: number | null }) => {
      exitCode = data.code ?? 0
      finished = true
    })

    // Spawn the process
    const childPromise = cmd.spawn()

    return {
      pid: undefined, // Tauri doesn't expose PID directly
      stdin: null, // Would need to adapt Tauri's stdin
      stdout: null, // Would need to adapt Tauri's stdout stream
      stderr: null, // Would need to adapt Tauri's stderr stream
      kill: () => {
        childPromise.then((c) => c.kill())
      },
      wait: async (): Promise<ExecResult> => {
        // Wait for process to complete
        await childPromise
        // Poll until finished
        while (!finished) {
          await new Promise((r) => setTimeout(r, 50))
        }
        return { stdout, stderr, exitCode }
      },
    }
  }
}
