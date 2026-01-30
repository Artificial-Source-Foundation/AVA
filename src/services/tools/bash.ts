/**
 * Bash Tool
 * Execute shell commands with timeout and output truncation
 */

import { Command } from '@tauri-apps/plugin-shell'
import { ToolError, ToolErrorType } from './errors'
import type { Tool, ToolContext, ToolResult } from './types'
import { LIMITS, resolvePath, truncateOutput } from './utils'

// ============================================================================
// Types
// ============================================================================

interface BashParams {
  command: string
  description: string
  workdir?: string
  timeout?: number
}

// ============================================================================
// Constants
// ============================================================================

const DEFAULT_TIMEOUT = 2 * 60 * 1000 // 2 minutes

// ============================================================================
// Implementation
// ============================================================================

export const bashTool: Tool<BashParams> = {
  definition: {
    name: 'bash',
    description: `Execute shell commands. Use the workdir parameter instead of 'cd' commands. Default timeout is 2 minutes. Output is truncated at ${LIMITS.MAX_LINES} lines or ${LIMITS.MAX_BYTES / 1024}KB.`,
    input_schema: {
      type: 'object',
      properties: {
        command: {
          type: 'string',
          description: 'Shell command to execute',
        },
        description: {
          type: 'string',
          description:
            'Brief description of what this command does (5-10 words). Examples: "List files in directory", "Install dependencies", "Run tests"',
        },
        workdir: {
          type: 'string',
          description:
            'Working directory to run the command in (optional, defaults to project root). Use this instead of cd commands.',
        },
        timeout: {
          type: 'number',
          description: 'Timeout in milliseconds (optional, default 120000 = 2 minutes)',
        },
      },
      required: ['command', 'description'],
    },
  },

  validate(params: unknown): BashParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError('Invalid params: expected object', ToolErrorType.INVALID_PARAMS, 'bash')
    }

    const { command, description, workdir, timeout } = params as Record<string, unknown>

    if (typeof command !== 'string' || !command.trim()) {
      throw new ToolError(
        'Invalid command: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'bash'
      )
    }

    if (typeof description !== 'string' || !description.trim()) {
      throw new ToolError(
        'Invalid description: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'bash'
      )
    }

    if (workdir !== undefined && typeof workdir !== 'string') {
      throw new ToolError('Invalid workdir: must be string', ToolErrorType.INVALID_PARAMS, 'bash')
    }

    if (timeout !== undefined) {
      if (typeof timeout !== 'number' || timeout <= 0) {
        throw new ToolError(
          'Invalid timeout: must be positive number',
          ToolErrorType.INVALID_PARAMS,
          'bash'
        )
      }
    }

    return {
      command: command.trim(),
      description: description.trim(),
      workdir: workdir?.trim(),
      timeout: timeout as number | undefined,
    }
  },

  async execute(params: BashParams, ctx: ToolContext): Promise<ToolResult> {
    const cwd = params.workdir
      ? resolvePath(params.workdir, ctx.workingDirectory)
      : ctx.workingDirectory
    const timeout = params.timeout ?? DEFAULT_TIMEOUT

    // Check abort signal before execution
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Command was cancelled before execution',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    try {
      // Create command using Tauri shell plugin
      const cmd = Command.create('bash', ['-c', params.command], {
        cwd,
        encoding: 'utf-8',
      })

      // Collect output from events
      let stdout = ''
      let stderr = ''

      cmd.on('close', () => {
        // Process closed - handled by promise below
      })

      cmd.stdout.on('data', (data: string) => {
        stdout += data
      })

      cmd.stderr.on('data', (data: string) => {
        stderr += data
      })

      // Spawn the process to get Child with kill() method
      const child = await cmd.spawn()

      // Set up abort handling
      let aborted = false
      const abortHandler = () => {
        aborted = true
        child.kill()
      }
      ctx.signal.addEventListener('abort', abortHandler, { once: true })

      // Create promise that resolves when process closes
      const processPromise = new Promise<{ code: number | null }>((resolve) => {
        cmd.on('close', (data) => {
          resolve(data)
        })
      })

      // Execute with timeout
      const timeoutPromise = new Promise<null>((resolve) => {
        setTimeout(() => resolve(null), timeout)
      })

      const result = await Promise.race([processPromise, timeoutPromise])

      // Clean up abort listener
      ctx.signal.removeEventListener('abort', abortHandler)

      // Handle timeout
      if (result === null) {
        child.kill()
        return {
          success: false,
          output: `Command timed out after ${timeout}ms.\n\nTo increase timeout, use the timeout parameter.`,
          error: ToolErrorType.EXECUTION_TIMEOUT,
          metadata: {
            command: params.command,
            description: params.description,
            cwd,
            timeout,
            timedOut: true,
          },
        }
      }

      // Handle abort
      if (aborted) {
        return {
          success: false,
          output: 'Command was cancelled by user',
          error: ToolErrorType.EXECUTION_ABORTED,
        }
      }

      // Handle null exit code (killed/signaled process)
      const exitCode = result.code ?? 1

      // Build output
      let output = ''

      if (exitCode === 0) {
        // Success case
        const combined = stdout + (stderr ? `\n${stderr}` : '')
        const truncated = truncateOutput(combined)

        if (truncated.truncated) {
          output = `Exit code: 0\n\n<output>\n${truncated.content}\n\n(Output truncated: ${truncated.removedLines} lines removed. Use read_file with offset to see full output if saved to file.)\n</output>`
        } else {
          output = `Exit code: 0\n\n<output>\n${combined || '(no output)'}\n</output>`
        }
      } else {
        // Error case
        const parts: string[] = [`Exit code: ${exitCode}`]

        if (stderr.trim()) {
          const truncatedErr = truncateOutput(stderr)
          parts.push(`\n<stderr>\n${truncatedErr.content}\n</stderr>`)
        }

        if (stdout.trim()) {
          const truncatedOut = truncateOutput(stdout)
          parts.push(`\n<stdout>\n${truncatedOut.content}\n</stdout>`)
        }

        if (!stderr.trim() && !stdout.trim()) {
          parts.push('\n(no output)')
        }

        output = parts.join('')
      }

      return {
        success: exitCode === 0,
        output,
        metadata: {
          command: params.command,
          description: params.description,
          cwd,
          exitCode,
          stdoutLength: stdout.length,
          stderrLength: stderr.length,
        },
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      return {
        success: false,
        output: `Error executing command: ${message}`,
        error: ToolErrorType.UNKNOWN,
      }
    }
  },
}
