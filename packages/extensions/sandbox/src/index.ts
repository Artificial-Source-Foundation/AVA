/**
 * Sandbox extension — Docker sandboxed execution.
 *
 * Checks for Docker availability and registers a sandbox_run tool.
 * Graceful no-op if Docker is not installed.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { isDockerAvailable, runInSandbox } from './runner.js'
import type { SandboxConfig } from './types.js'
import { DEFAULT_SANDBOX_CONFIG } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const config = {
    ...DEFAULT_SANDBOX_CONFIG,
    ...api.getSettings<Partial<SandboxConfig>>('sandbox'),
  }
  const disposables: Disposable[] = []

  void isDockerAvailable(api.platform.shell).then((available) => {
    if (!available) {
      api.log.debug('Sandbox extension: Docker not available, tool not registered')
      api.emit('sandbox:ready', { available: false })
      return
    }

    // Register sandbox_run tool
    disposables.push(
      api.registerTool({
        definition: {
          name: 'sandbox_run',
          description: 'Execute code in an isolated Docker container',
          input_schema: {
            type: 'object',
            properties: {
              code: { type: 'string', description: 'Code to execute in the sandbox' },
              image: { type: 'string', description: 'Docker image to use (optional)' },
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

          const result = await runInSandbox(
            api.platform.shell,
            sandboxConfig,
            params.code as string
          )

          return {
            success: result.exitCode === 0,
            output: result.stdout || result.stderr,
            metadata: {
              exitCode: result.exitCode,
              durationMs: result.durationMs,
              timedOut: result.timedOut,
            },
          }
        },
      })
    )

    api.log.debug('Sandbox extension: Docker available, sandbox_run tool registered')
    api.emit('sandbox:ready', { available: true })
  })

  api.log.debug('Sandbox extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
