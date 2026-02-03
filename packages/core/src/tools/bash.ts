/**
 * Bash Tool
 * Execute shell commands with timeout, inactivity detection, and output truncation
 */

import { getPlatform } from '../platform.js'
import { ToolError, ToolErrorType } from './errors.js'
import type { Tool, ToolContext, ToolResult } from './types.js'
import { isBinaryOutput, LIMITS, resolvePath, truncateOutput } from './utils.js'

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
const DEFAULT_INACTIVITY_TIMEOUT = 30 * 1000 // 30 seconds

// ============================================================================
// Implementation
// ============================================================================

export const bashTool: Tool<BashParams> = {
  definition: {
    name: 'bash',
    description: `Execute shell commands. Use the workdir parameter instead of 'cd' commands. Default timeout is 2 minutes. Output is truncated at ${LIMITS.MAX_LINES} lines or ${LIMITS.MAX_BYTES / 1024}KB. Commands are killed if no output for 30 seconds.`,
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
    const shell = getPlatform().shell
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
      // Spawn command using platform shell
      const child = shell.spawn('bash', ['-c', params.command], {
        cwd,
        inactivityTimeout: DEFAULT_INACTIVITY_TIMEOUT,
        killProcessGroup: true,
      })

      // Collect output
      let stdout = ''
      let stderr = ''
      let binaryDetected = false

      // Set up abort handling
      let aborted = false
      const abortHandler = () => {
        aborted = true
        child.kill()
      }
      ctx.signal.addEventListener('abort', abortHandler, { once: true })

      // Set up timeout
      let timedOut = false
      const timeoutId = setTimeout(() => {
        timedOut = true
        child.kill()
      }, timeout)

      // Read stdout stream
      if (child.stdout) {
        const reader = child.stdout.getReader()
        const decoder = new TextDecoder()

        try {
          while (true) {
            const { done, value } = await reader.read()
            if (done) break

            // Check for binary output
            if (isBinaryOutput(value)) {
              binaryDetected = true
              stdout += '\n[Binary output detected. Halting stream...]'
              child.kill()
              break
            }

            stdout += decoder.decode(value, { stream: true })
          }
        } catch {
          // Stream error - process may have been killed
        } finally {
          reader.releaseLock()
        }
      }

      // Read stderr stream
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
          // Stream error - process may have been killed
        } finally {
          reader.releaseLock()
        }
      }

      // Wait for process to complete
      const result = await child.wait()

      // Clean up
      clearTimeout(timeoutId)
      ctx.signal.removeEventListener('abort', abortHandler)

      // Handle timeout
      if (timedOut) {
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
          locations: [{ path: cwd, type: 'exec' }],
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

      // Handle binary output detection
      if (binaryDetected) {
        return {
          success: false,
          output: `Binary output detected in command output.\n\nPartial output before binary:\n${stdout}`,
          error: ToolErrorType.BINARY_OUTPUT,
          metadata: {
            command: params.command,
            description: params.description,
            cwd,
            binaryDetected: true,
          },
          locations: [{ path: cwd, type: 'exec' }],
        }
      }

      const exitCode = result.exitCode

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
        locations: [{ path: cwd, type: 'exec' }],
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
