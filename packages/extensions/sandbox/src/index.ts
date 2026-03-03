/**
 * Sandbox extension — runtime-selected sandboxed execution.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'
import { createSandboxRuntime } from './runner.js'
import type { SandboxConfig } from './types.js'
import { DEFAULT_SANDBOX_CONFIG } from './types.js'

const SandboxRunSchema = z.object({
  code: z.string().describe('Code to execute in the sandbox'),
  image: z.string().optional().describe('Container image for docker runtime'),
  timeout: z.number().optional().describe('Timeout in ms'),
})

export function activate(api: ExtensionAPI): Disposable {
  const config = {
    ...DEFAULT_SANDBOX_CONFIG,
    ...api.getSettings<Partial<SandboxConfig>>('sandbox'),
  }
  const disposables: Disposable[] = []

  void createSandboxRuntime(api.platform.shell).then((runtime) => {
    const sandboxRunTool = defineTool({
      name: 'sandbox_run',
      description: 'Execute code in an isolated sandbox runtime',
      schema: SandboxRunSchema,
      async execute(params) {
        const sandboxConfig = {
          ...config,
          ...(params.image ? { image: params.image } : {}),
          ...(params.timeout ? { timeout: params.timeout } : {}),
        }

        const result = await runtime.run(sandboxConfig, params.code)

        return {
          success: result.exitCode === 0,
          output: result.stdout || result.stderr,
          metadata: {
            exitCode: result.exitCode,
            durationMs: result.durationMs,
            timedOut: result.timedOut,
            runtime: runtime.name,
          },
        }
      },
    })

    disposables.push(api.registerTool(sandboxRunTool))

    api.log.debug(
      `Sandbox extension: ${runtime.name} runtime selected, sandbox_run tool registered`
    )
    api.emit('sandbox:ready', { available: runtime.name !== 'noop', runtime: runtime.name })
  })

  api.log.debug('Sandbox extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
