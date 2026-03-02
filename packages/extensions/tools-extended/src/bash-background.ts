/**
 * bash_background tool — spawn a shell command as a background process.
 */

import { spawn } from 'node:child_process'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'
import type { BackgroundProcess } from './process-registry.js'
import { MAX_BUFFER_LINES, registerProcess } from './process-registry.js'

function appendLine(buffer: string[], line: string): void {
  buffer.push(line)
  if (buffer.length > MAX_BUFFER_LINES) {
    buffer.shift()
  }
}

export const bashBackgroundTool = defineTool({
  name: 'bash_background',
  description:
    'Start a shell command as a background process. Returns the PID immediately. ' +
    'Use bash_output to read stdout/stderr and bash_kill to terminate.',
  schema: z.object({
    command: z.string().describe('Shell command to run in the background'),
    cwd: z
      .string()
      .optional()
      .describe('Working directory (defaults to session working directory)'),
  }),
  permissions: ['execute'],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const cwd = input.cwd ?? ctx.workingDirectory

    const child = spawn(input.command, {
      shell: true,
      cwd,
      stdio: ['ignore', 'pipe', 'pipe'],
    })

    if (!child.pid) {
      return { success: false, output: '', error: 'Failed to spawn process' }
    }

    const proc: BackgroundProcess = {
      pid: child.pid,
      command: input.command,
      stdout: [],
      stderr: [],
      startTime: Date.now(),
      exitCode: null,
      process: child,
    }

    if (child.stdout) {
      let stdoutPartial = ''
      child.stdout.on('data', (chunk: Buffer) => {
        const text = stdoutPartial + chunk.toString()
        const lines = text.split('\n')
        stdoutPartial = lines.pop() ?? ''
        for (const line of lines) {
          appendLine(proc.stdout, line)
        }
      })
      child.stdout.on('end', () => {
        if (stdoutPartial) {
          appendLine(proc.stdout, stdoutPartial)
          stdoutPartial = ''
        }
      })
    }

    if (child.stderr) {
      let stderrPartial = ''
      child.stderr.on('data', (chunk: Buffer) => {
        const text = stderrPartial + chunk.toString()
        const lines = text.split('\n')
        stderrPartial = lines.pop() ?? ''
        for (const line of lines) {
          appendLine(proc.stderr, line)
        }
      })
      child.stderr.on('end', () => {
        if (stderrPartial) {
          appendLine(proc.stderr, stderrPartial)
          stderrPartial = ''
        }
      })
    }

    child.on('exit', (code) => {
      proc.exitCode = code
    })

    registerProcess(proc)

    return {
      success: true,
      output: `Process started with PID ${child.pid}`,
      metadata: { pid: child.pid },
    }
  },
})
