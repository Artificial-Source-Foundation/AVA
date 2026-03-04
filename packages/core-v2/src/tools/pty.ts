/**
 * PTY tool — interactive shell via platform PTY interface.
 *
 * Spawns a PTY process, collects output for a configurable timeout,
 * then returns the result. Conditional on platform PTY support.
 */

import * as z from 'zod'
import { getPlatform } from '../platform.js'
import { defineTool } from './define.js'
import { ToolError, ToolErrorType } from './errors.js'
import { LIMITS, resolvePath, truncateOutput } from './utils.js'

const DEFAULT_TIMEOUT = 30_000
const DEFAULT_COLS = 120
const DEFAULT_ROWS = 40

const schema = z.object({
  command: z.string().describe('Shell command to execute in a PTY'),
  timeout: z
    .number()
    .int()
    .min(1000)
    .max(300_000)
    .optional()
    .describe('Timeout in ms (default: 30000, max: 300000)'),
  workdir: z.string().optional().describe('Working directory (defaults to session cwd)'),
})

export const ptyTool = defineTool({
  name: 'pty',
  description: 'Execute a command in a PTY. Use for interactive/TTY commands.',
  schema,
  permissions: ['execute'],
  locations: (input) => [{ path: input.workdir ?? '.', type: 'exec' as const }],

  async execute(input, ctx) {
    if (ctx.signal.aborted) {
      throw new ToolError('Aborted', ToolErrorType.EXECUTION_ABORTED, 'pty')
    }

    const platform = getPlatform()
    const pty = platform.pty

    if (!pty || !pty.isSupported()) {
      return {
        success: false,
        output:
          'PTY is not supported on this platform. Use the bash tool instead for non-interactive commands.',
        error: 'PTY not supported',
      }
    }

    const cwd = input.workdir
      ? resolvePath(input.workdir, ctx.workingDirectory)
      : ctx.workingDirectory
    const timeout = input.timeout ?? DEFAULT_TIMEOUT

    ctx.metadata?.({
      title: `PTY: ${input.command}`,
      metadata: { command: input.command, cwd },
    })

    const proc = pty.spawn('bash', ['-c', input.command], {
      cols: DEFAULT_COLS,
      rows: DEFAULT_ROWS,
      cwd,
    })

    let output = ''
    let killed = false

    // Collect output
    proc.onData((data) => {
      output += data
      ctx.onProgress?.({ chunk: data })
    })

    // Abort handler
    const abortHandler = () => {
      killed = true
      proc.kill()
    }
    ctx.signal.addEventListener('abort', abortHandler, { once: true })

    // Timeout handler
    const timer = setTimeout(() => {
      killed = true
      proc.kill()
    }, timeout)

    try {
      const { exitCode } = await proc.wait()

      if (ctx.signal.aborted) {
        throw new ToolError('Aborted', ToolErrorType.EXECUTION_ABORTED, 'pty')
      }

      if (killed && !ctx.signal.aborted) {
        throw new ToolError(
          `PTY command timed out after ${timeout}ms`,
          ToolErrorType.EXECUTION_TIMEOUT,
          'pty'
        )
      }

      // Strip ANSI escape sequences for cleaner output
      const cleanOutput = stripAnsi(output)
      const success = exitCode === 0

      const truncated = truncateOutput(cleanOutput, LIMITS.MAX_LINES, LIMITS.MAX_BYTES)
      let formattedOutput: string
      if (success) {
        formattedOutput = `<output>\n${truncated.content}\n</output>`
      } else {
        formattedOutput = `<output>\n${truncated.content}\n</output>\nExit code: ${exitCode}`
      }

      if (truncated.truncated) {
        formattedOutput += `\n(Output truncated: ${truncated.removedLines} lines removed)`
      }

      return {
        success,
        output: formattedOutput,
        metadata: {
          command: input.command,
          cwd,
          exitCode,
          outputLength: cleanOutput.length,
          pid: proc.pid,
        },
        locations: [{ path: cwd, type: 'exec' }],
      }
    } finally {
      clearTimeout(timer)
      ctx.signal.removeEventListener('abort', abortHandler)
    }
  },
})

/** Strip ANSI escape sequences from PTY output. */
function stripAnsi(str: string): string {
  // biome-ignore lint/suspicious/noControlCharactersInRegex: ANSI escape stripping requires control characters
  return str.replace(/\x1B\[[0-9;]*[A-Za-z]|\x1B\].*?(?:\x07|\x1B\\)|\x1B[()][AB012]|\x1B[=>]/g, '')
}
