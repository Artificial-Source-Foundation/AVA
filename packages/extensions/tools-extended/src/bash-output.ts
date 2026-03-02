/**
 * bash_output tool — read stdout/stderr from a background process.
 */

import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'
import { getProcess } from './process-registry.js'

const DEFAULT_LINES = 50

export const bashOutputTool = defineTool({
  name: 'bash_output',
  description:
    'Read the latest stdout and stderr from a background process started with bash_background. ' +
    'Also reports whether the process is still running or has exited.',
  schema: z.object({
    pid: z.number().describe('Process ID returned by bash_background'),
    lines: z
      .number()
      .optional()
      .describe(`Number of recent lines to return (default ${DEFAULT_LINES})`),
  }),
  permissions: ['read'],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const proc = getProcess(input.pid)
    if (!proc) {
      return {
        success: false,
        output: '',
        error: `No background process found with PID ${input.pid}`,
      }
    }

    const lines = input.lines ?? DEFAULT_LINES
    const recentStdout = proc.stdout.slice(-lines)
    const recentStderr = proc.stderr.slice(-lines)

    const isRunning = proc.exitCode === null
    const status = isRunning ? 'running' : `exited with code ${proc.exitCode}`
    const elapsed = Math.round((Date.now() - proc.startTime) / 1000)

    const parts: string[] = []
    parts.push(`PID: ${proc.pid}`)
    parts.push(`Command: ${proc.command}`)
    parts.push(`Status: ${status}`)
    parts.push(`Uptime: ${elapsed}s`)

    if (recentStdout.length > 0) {
      parts.push('')
      parts.push('--- stdout ---')
      parts.push(...recentStdout)
    }

    if (recentStderr.length > 0) {
      parts.push('')
      parts.push('--- stderr ---')
      parts.push(...recentStderr)
    }

    if (recentStdout.length === 0 && recentStderr.length === 0) {
      parts.push('')
      parts.push('(no output yet)')
    }

    return {
      success: true,
      output: parts.join('\n'),
      metadata: {
        pid: proc.pid,
        running: isRunning,
        exitCode: proc.exitCode,
        stdoutLines: proc.stdout.length,
        stderrLines: proc.stderr.length,
      },
    }
  },
})
