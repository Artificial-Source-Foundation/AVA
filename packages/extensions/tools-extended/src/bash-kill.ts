/**
 * bash_kill tool — terminate a background process.
 */

import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'
import { getProcess, removeProcess } from './process-registry.js'

export const bashKillTool = defineTool({
  name: 'bash_kill',
  description:
    'Send a signal to a background process and remove it from the registry. ' +
    'Defaults to SIGTERM.',
  schema: z.object({
    pid: z.number().describe('Process ID to kill'),
    signal: z
      .string()
      .optional()
      .describe('Signal name (default SIGTERM). Examples: SIGTERM, SIGKILL, SIGINT'),
  }),
  permissions: ['execute'],
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

    const sig = (input.signal ?? 'SIGTERM') as NodeJS.Signals

    try {
      proc.process.kill(sig)
    } catch (err) {
      return {
        success: false,
        output: '',
        error: `Failed to kill PID ${input.pid}: ${err instanceof Error ? err.message : String(err)}`,
      }
    }

    removeProcess(input.pid)

    return {
      success: true,
      output: `Sent ${sig} to PID ${input.pid} (${proc.command})`,
      metadata: { pid: input.pid, signal: sig },
    }
  },
})
