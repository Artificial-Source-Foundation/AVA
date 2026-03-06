/**
 * Run Command — Unified agent entry point with dual-stack support
 *
 * Usage:
 *   ava run "<goal>"                                 Runs with core-v2 (default)
 *   ava run "<goal>" --backend core                  Runs with legacy core
 *   ava run "<goal>" --backend core-v2               Runs with core-v2 + extensions
 *   ava run "<goal>" --mock --max-turns 3
 *   ava run "<goal>" --provider anthropic --model claude-sonnet-4-20250514
 */

import * as os from 'node:os'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'
import type { AgentConfig, AgentEventCallback } from '@ava/core-v2'
import { AgentExecutor, registerProvider } from '@ava/core-v2'
import { MessageBus } from '@ava/core-v2/bus'
import type { ExtensionModule } from '@ava/core-v2/extensions'
import {
  ExtensionManager,
  emitEvent,
  loadAllBuiltInExtensions,
  onEvent,
} from '@ava/core-v2/extensions'
import { setPlatform } from '@ava/core-v2/platform'
import { createSessionManager } from '@ava/core-v2/session'
import { registerCoreTools } from '@ava/core-v2/tools'
import { createNodePlatform } from '@ava/platform-node/v2'
import { getCliLogger } from '../logger.js'
import { DiffRenderer } from '../rendering/diff-renderer.js'
import { MockLLMClient, setupMockEnvironment } from './mock-client.js'
import {
  applyCliToolFilter,
  applyMockProviderDefaults,
  buildLegacyArgs,
  importWithFallback,
} from './run-helpers.js'
import { parseRunOptions, printRunHelp } from './run-options.js'
import { createStreamingCallback } from './run-streaming.js'

export async function runRunCommand(args: string[]): Promise<void> {
  const log = getCliLogger('cli:run')
  const parsed = parseRunOptions(args)
  if (!parsed) {
    log.warn('Invalid run options', { args: args.join(' ') })
    printRunHelp()
    return
  }
  let options = parsed

  log.info('Run command started', {
    backend: options.backend,
    provider: options.provider ?? 'default',
    model: options.model ?? 'default',
    max_turns: options.maxTurns,
    yolo: options.yolo,
  })

  // Route to legacy agent command for 'core' backend
  if (options.backend === 'core') {
    log.info('Routing to legacy core backend')
    if (options.verbose) {
      process.stderr.write('[run] Using legacy core backend\n')
    }
    const { runAgentCommand } = await import('./agent.js')
    await runAgentCommand(buildLegacyArgs(options))
    return
  }

  // Initialize platform + core tools
  const dbPath = path.join(os.homedir(), '.ava', 'data.db')
  setPlatform(createNodePlatform(dbPath))
  registerCoreTools()

  // Set up mock if requested
  if (options.mock) {
    setupMockEnvironment()
    options = applyMockProviderDefaults(
      options,
      (provider, factory) => registerProvider(provider, () => factory() as MockLLMClient),
      () => new MockLLMClient()
    )
  }

  // Load built-in extensions
  const bus = new MessageBus()
  const sessionManager = createSessionManager()
  const manager = new ExtensionManager(bus, sessionManager)

  const currentDir = path.dirname(fileURLToPath(import.meta.url))
  const extensionsDir = path.resolve(currentDir, '../../../packages/extensions')

  let extensionCount = 0
  try {
    const loaded = await loadAllBuiltInExtensions(extensionsDir)
    const modules = new Map<string, ExtensionModule>()
    for (const ext of loaded) {
      manager.register(ext.manifest, ext.path)
      modules.set(ext.manifest.name, ext.module)
    }
    await manager.activateAll(modules)
    extensionCount = manager.getActiveExtensions().length
    log.info('Extensions activated', { count: extensionCount })
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    log.warn('Failed to load extensions', { error: message })
    if (options.verbose) {
      process.stderr.write(`[run] Warning: Failed to load extensions: ${message}\n`)
    }
  }

  // Load system prompt from prompts extension
  interface PromptsModule {
    addPromptSection: (s: { name: string; priority: number; content: string }) => () => void
    buildSystemPrompt: (model?: string) => string
  }
  let promptsModule: PromptsModule | null = null
  try {
    const srcPath = path.resolve(extensionsDir, 'prompts/src/builder.ts')
    const distPath = path.resolve(extensionsDir, 'dist/prompts/src/builder.js')
    promptsModule = (await importWithFallback(srcPath, distPath)) as unknown as PromptsModule
    promptsModule.addPromptSection({
      name: 'cwd',
      priority: 100,
      content: `Working directory: ${options.cwd}`,
    })
  } catch {
    // Prompts extension not available
  }

  // Wait for instructions to load
  let instructionsReady: Promise<void> = Promise.resolve()
  if (promptsModule) {
    const pm = promptsModule
    instructionsReady = new Promise<void>((resolve) => {
      const timeout = setTimeout(resolve, 1000)
      onEvent('instructions:loaded', (data) => {
        clearTimeout(timeout)
        const { merged, count } = data as { merged: string; count: number }
        if (merged) {
          pm.addPromptSection({
            name: 'project-instructions',
            content: `# Project Instructions\n\n${merged}`,
            priority: 5,
          })
          if (options.verbose) {
            process.stderr.write(`[run] Loaded ${count} instruction file(s) into system prompt\n`)
          }
        }
        resolve()
      })
    })
  }

  // Set yolo mode — bypass all permission checks
  if (options.yolo) {
    try {
      const permissionsPath = path.resolve(extensionsDir, 'permissions/src/middleware.ts')
      const permissionsDistPath = path.resolve(extensionsDir, 'dist/permissions/src/middleware.js')
      const perms = (await importWithFallback(permissionsPath, permissionsDistPath)) as {
        updateSettings: (s: Record<string, unknown>) => void
      }
      perms.updateSettings({ permissionMode: 'yolo' })
    } catch {
      // Permissions extension not available — no-op
    }
  }

  // Build agent config
  const config: Partial<AgentConfig> = {
    maxTurns: options.maxTurns,
    maxTimeMinutes: options.maxTimeMinutes,
  }

  if (options.provider) {
    config.provider = options.provider
  }
  if (options.model) {
    config.model = options.model
  }

  const { getToolDefinitions } = await import('@ava/core-v2/tools')
  applyCliToolFilter(
    config,
    getToolDefinitions().map((tool) => tool.name)
  )

  // Set up abort controller
  const ac = new AbortController()
  let aborted = false

  const onSignal = () => {
    if (aborted) process.exit(1)
    aborted = true
    process.stderr.write('\n[run] Aborting... (press Ctrl+C again to force)\n')
    ac.abort()
  }

  process.on('SIGINT', onSignal)
  process.on('SIGTERM', onSignal)

  // Create event callback
  const diffRenderer = options.json ? undefined : new DiffRenderer()
  const eventHandler: AgentEventCallback = options.json
    ? (event) => console.log(JSON.stringify(event))
    : createStreamingCallback(options.verbose, diffRenderer)

  try {
    // Create session and emit session:opened for extensions
    const session = sessionManager.create(options.goal.slice(0, 50), options.cwd)
    emitEvent('session:opened', {
      sessionId: session.id,
      workingDirectory: options.cwd,
    })
    await instructionsReady

    // Build system prompt after instructions are loaded
    let systemPrompt: string | undefined
    if (promptsModule) {
      systemPrompt = promptsModule.buildSystemPrompt(options.model || undefined)
    }

    if (options.verbose) {
      process.stderr.write(`[run] Goal: ${options.goal}\n`)
      process.stderr.write(
        `[run] Provider: ${options.provider ?? 'default'}, Model: ${options.model ?? 'default'}, Max turns: ${options.maxTurns}\n`
      )
      process.stderr.write(`[run] Extensions loaded: ${extensionCount}\n`)
      if (systemPrompt) {
        const promptTokens = Math.ceil(systemPrompt.length / 4)
        process.stderr.write(
          `[run] System prompt: ${systemPrompt.length} chars (~${promptTokens} tokens)\n`
        )
      }
      process.stderr.write('\n')
    }

    // Create and run agent
    const executor = new AgentExecutor({ ...config, systemPrompt }, eventHandler)

    const result = await executor.run({ goal: options.goal, cwd: options.cwd }, ac.signal)
    log.info('Run command completed', {
      success: result.success,
      terminate_mode: result.terminateMode,
      turns: result.turns,
      duration_ms: result.durationMs,
      tokens_in: result.tokensUsed.input,
      tokens_out: result.tokensUsed.output,
    })

    if (options.json) {
      console.log(
        JSON.stringify({
          type: 'summary',
          success: result.success,
          terminateMode: result.terminateMode,
          turns: result.turns,
          tokens: result.tokensUsed,
          durationMs: result.durationMs,
          output: result.output,
        })
      )
    } else {
      const { input, output } = result.tokensUsed
      console.log(`\n[Done] ${result.success ? 'SUCCESS' : `FAILED (${result.terminateMode})`}`)
      console.log(
        `  Turns: ${result.turns}, Tokens: ${input} in / ${output} out (total: ${input + output}), Duration: ${(result.durationMs / 1000).toFixed(1)}s`
      )
      if (result.output) {
        console.log('')
        console.log(result.output)
      }
      if (result.error) {
        console.error('')
        console.error(`Error: ${result.error}`)
      }
    }

    process.exitCode = result.success ? 0 : 1
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    log.error('Run command failed', { error: message })
    if (options.json) {
      console.log(JSON.stringify({ type: 'error', error: message }))
    } else {
      console.error(`[Error] ${message}`)
    }
    process.exitCode = 1
  } finally {
    process.removeListener('SIGINT', onSignal)
    process.removeListener('SIGTERM', onSignal)
    diffRenderer?.dispose()
    await manager.dispose()
  }
}
