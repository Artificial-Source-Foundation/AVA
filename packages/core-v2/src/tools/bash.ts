/**
 * bash tool — execute shell commands.
 *
 * Includes optional sandbox routing for install-class commands.
 * Security validation and PTY concerns remain extension-level.
 */

import * as z from 'zod'
import { type ExecResult, getPlatform } from '../platform.js'
import { dispatchCompute } from '../platform-dispatch.js'
import { defineTool } from './define.js'
import { ToolError, ToolErrorType } from './errors.js'
import { isBinaryOutput, LIMITS, resolvePath, truncateOutput } from './utils.js'

const schema = z.object({
  command: z.string().describe('Shell command to execute'),
  description: z.string().describe('Brief description of what this command does'),
  workdir: z.string().optional().describe('Working directory (defaults to session cwd)'),
  timeout: z.number().int().min(1000).optional().describe('Timeout in ms (default: 120000)'),
  requires_approval: z.boolean().optional().describe('Whether this command needs user approval'),
  _sandboxed: z.boolean().optional(),
  _sandboxPolicy: z
    .object({
      writableRoots: z.array(z.string()).optional(),
      networkAccess: z.boolean().optional(),
    })
    .optional(),
})

export const bashTool = defineTool({
  name: 'bash',
  description: 'Execute a shell command. Use for system commands, builds, tests, git operations.',
  schema,
  permissions: ['execute'],
  locations: (input) => [{ path: input.workdir ?? '.', type: 'exec' as const }],

  async execute(input, ctx) {
    if (ctx.signal.aborted) {
      throw new ToolError('Aborted', ToolErrorType.EXECUTION_ABORTED, 'bash')
    }

    const shell = getPlatform().shell
    const cwd = input.workdir
      ? resolvePath(input.workdir, ctx.workingDirectory)
      : ctx.workingDirectory
    const timeout = input.timeout ?? 120_000

    if (input._sandboxed) {
      const runUnsandboxed = async (): Promise<ExecResult> => shell.exec(input.command, { cwd })
      let result: ExecResult
      try {
        result = await dispatchCompute<ExecResult>(
          'sandbox_run',
          {
            command: input.command,
            cwd,
            timeout,
            policy: input._sandboxPolicy ?? { writableRoots: [cwd, '/tmp'], networkAccess: false },
          },
          runUnsandboxed
        )
      } catch {
        // TODO(sprint-6): Remove fallback once rust sandbox_run command is available in all runtimes.
        result = await runUnsandboxed()
      }

      const success = result.exitCode === 0
      let output: string
      if (success) {
        const combined = result.stdout + (result.stderr ? `\n${result.stderr}` : '')
        const truncated = truncateOutput(combined, LIMITS.MAX_LINES, LIMITS.MAX_BYTES)
        output = `<output>\n${truncated.content}\n</output>`
      } else {
        output = ''
        if (result.stderr) output += `<stderr>\n${result.stderr}\n</stderr>\n`
        if (result.stdout) output += `<stdout>\n${result.stdout}\n</stdout>`
        output += `\nExit code: ${result.exitCode}`
      }

      return {
        success,
        output,
        metadata: {
          command: input.command,
          description: input.description,
          cwd,
          exitCode: result.exitCode,
          stdoutLength: result.stdout.length,
          stderrLength: result.stderr.length,
          requiresApproval: input.requires_approval,
          sandboxed: true,
        },
        locations: [{ path: cwd, type: 'exec' }],
      }
    }

    // Stream metadata to UI
    ctx.metadata?.({
      title: input.description,
      metadata: { command: input.command, cwd },
    })

    const child = shell.spawn('bash', ['-c', input.command], {
      cwd,
      inactivityTimeout: 30_000,
      killProcessGroup: true,
    })

    let stdout = ''
    let stderr = ''
    let killed = false

    // Abort handler
    const abortHandler = () => {
      killed = true
      child.kill()
    }
    ctx.signal.addEventListener('abort', abortHandler, { once: true })

    // Timeout handler
    const timer = setTimeout(() => {
      killed = true
      child.kill()
    }, timeout)

    // Collect output via Web Streams
    const decoder = new TextDecoder()

    const readStream = async (
      stream: ReadableStream<Uint8Array> | null,
      onChunk: (text: string) => void
    ) => {
      if (!stream) return
      const reader = stream.getReader()
      try {
        while (true) {
          const { done, value } = await reader.read()
          if (done) break
          onChunk(decoder.decode(value, { stream: true }))
        }
      } catch {
        // Stream may be cancelled on kill
      } finally {
        reader.releaseLock()
      }
    }

    const stdoutPromise = readStream(child.stdout, (data) => {
      if (isBinaryOutput(new TextEncoder().encode(data))) {
        killed = true
        child.kill()
        return
      }
      stdout += data
      // Stream incremental output to UI
      ctx.metadata?.({
        title: input.description,
        metadata: { streamOutput: stdout.slice(-4096) },
      })
      // Stream progress via onProgress callback
      ctx.onProgress?.({ chunk: data })
    })

    const stderrPromise = readStream(child.stderr, (data) => {
      stderr += data
    })

    let result: ExecResult
    try {
      await Promise.all([stdoutPromise, stderrPromise])
      result = await child.wait()
    } finally {
      clearTimeout(timer)
      ctx.signal.removeEventListener('abort', abortHandler)
    }

    if (ctx.signal.aborted) {
      throw new ToolError('Aborted', ToolErrorType.EXECUTION_ABORTED, 'bash')
    }

    if (killed && !ctx.signal.aborted) {
      throw new ToolError(
        `Command timed out after ${timeout}ms`,
        ToolErrorType.EXECUTION_TIMEOUT,
        'bash'
      )
    }

    const exitCode = result.exitCode
    const success = exitCode === 0

    // Format output
    let output: string
    if (success) {
      const combined = stdout + (stderr ? `\n${stderr}` : '')
      const truncated = truncateOutput(combined, LIMITS.MAX_LINES, LIMITS.MAX_BYTES)
      output = `<output>\n${truncated.content}\n</output>`
      if (truncated.truncated) {
        output += `\n(Output truncated: ${truncated.removedLines} lines removed)`
      }
    } else {
      output = ''
      if (stderr) output += `<stderr>\n${stderr}\n</stderr>\n`
      if (stdout) output += `<stdout>\n${stdout}\n</stdout>`
      output += `\nExit code: ${exitCode}`
    }

    return {
      success,
      output,
      metadata: {
        command: input.command,
        description: input.description,
        cwd,
        exitCode,
        stdoutLength: stdout.length,
        stderrLength: stderr.length,
        requiresApproval: input.requires_approval,
      },
      locations: [{ path: cwd, type: 'exec' }],
    }
  },
})
