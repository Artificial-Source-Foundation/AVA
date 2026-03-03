/**
 * Sandbox extension — runtime-selected sandboxed execution.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { createSandboxRuntime } from './runner.js'
import type { SandboxConfig } from './types.js'
import { DEFAULT_SANDBOX_CONFIG } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const config = {
    ...DEFAULT_SANDBOX_CONFIG,
    ...api.getSettings<Partial<SandboxConfig>>('sandbox'),
  }
  const disposables: Disposable[] = []

  void createSandboxRuntime(api.platform.shell).then((runtime) => {
    disposables.push(
      api.registerTool({
        definition: {
          name: 'sandbox_run',
          description: 'Execute code in an isolated sandbox runtime',
          input_schema: {
            type: 'object',
            properties: {
              code: { type: 'string', description: 'Code to execute in the sandbox' },
              image: {
                type: 'string',
                description: 'Container image for docker runtime (optional)',
              },
              timeout: { type: 'number', description: 'Timeout in ms (optional)' },
            },
            required: ['code'],
          },
        },
        async execute(params) {
          const sandboxConfig = {
            ...config,
            ...(params.image ? { image: params.image as string } : {}),
            ...(params.timeout ? { timeout: params.timeout as number } : {}),
          }

          const result = await runtime.run(sandboxConfig, params.code as string)

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
    )

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
