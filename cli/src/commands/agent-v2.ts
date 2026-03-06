/**
 * AVA CLI Agent V2 Command
 *
 * Uses core-v2 agent loop + extension system.
 * This is the new stack that will eventually replace the original agent command.
 */

import * as os from 'node:os'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'
import { AgentExecutor } from '@ava/core-v2/agent'
import { MessageBus } from '@ava/core-v2/bus'
import type { ExtensionModule } from '@ava/core-v2/extensions'
import {
  ExtensionManager,
  emitEvent,
  getAgentModes,
  loadAllBuiltInExtensions,
} from '@ava/core-v2/extensions'
import type { LLMProvider } from '@ava/core-v2/llm'
import { registerProvider } from '@ava/core-v2/llm'
import { setPlatform } from '@ava/core-v2/platform'
import { createSessionManager } from '@ava/core-v2/session'
import { registerCoreTools } from '@ava/core-v2/tools'
import { createNodePlatform } from '@ava/platform-node/v2'
import { migrateOAuthCredentials } from '../auth/manager.js'
import { getCliLogger } from '../logger.js'
import { parseArgs } from './agent-v2/args.js'
import { createEventHandler } from './agent-v2/events.js'
import { setupPermissionPrompts } from './agent-v2/permissions.js'
import { createInstructionsReadyPromise, loadPromptsModule } from './agent-v2/prompts.js'
import { loadAgentModeSelector, resolveAgentMode } from './agent-v2/runtime.js'
import { expandAtMentions } from './at-mentions.js'

const log = getCliLogger('cli:agent-v2')

function registerMockProvider(): void {
  registerProvider('mock', () => ({
    async *stream() {
      yield { content: 'Mock response — core-v2 agent loop is working!' }
      yield { done: true }
    },
  }))
}

async function resolveResume(
  options: { resume: string | null; verbose: boolean },
  sessionManager: ReturnType<typeof createSessionManager>
): Promise<void> {
  if (!options.resume) return

  await sessionManager.loadFromStorage()
  if (options.resume === 'latest') {
    const sessions = sessionManager.list()
    const latest = sessions.sort((a, b) => b.updatedAt - a.updatedAt)[0]
    if (latest) {
      log.info('Resuming latest session', { session: latest.id })
      if (options.verbose) {
        process.stderr.write(
          `[agent-v2] Resuming session: ${latest.id} (${latest.name ?? 'unnamed'})\n`
        )
      }
    } else {
      log.warn('Resume requested but no prior sessions found')
      process.stderr.write('[agent-v2] No previous sessions found to resume. Starting fresh.\n')
    }
    return
  }

  const session = await sessionManager.loadSession(options.resume)
  if (session) {
    log.info('Resuming specific session', { session: session.id })
    if (options.verbose) {
      process.stderr.write(`[agent-v2] Resuming session: ${session.id}\n`)
    }
  } else {
    log.warn('Requested session not found', { session: options.resume })
    process.stderr.write(`[agent-v2] Session ${options.resume} not found. Starting fresh.\n`)
  }
}

async function activateExtensions(
  manager: ExtensionManager,
  extensionsDir: string
): Promise<number> {
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
    log.info('Agent-v2 extensions activated', { count: extensionCount })
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    log.warn('Agent-v2 extension activation failed', { error: message })
    process.stderr.write(`[agent-v2] Warning: Failed to load extensions: ${message}\n`)
  }
  return extensionCount
}

export async function runAgentV2Command(args: string[]): Promise<void> {
  const options = parseArgs(args, log)
  if (!options) return

  log.info('Agent-v2 command started', {
    provider: options.provider,
    model: options.model || 'default',
    max_turns: options.maxTurns,
    timeout_min: options.timeout,
    yolo: options.yolo,
    json: options.json,
    resume: options.resume ?? 'none',
    goal_length: options.goal.length,
  })

  if (options.goal) {
    const originalGoal = options.goal
    options.goal = await expandAtMentions(options.goal, options.cwd)
    if (options.goal !== originalGoal) {
      log.info('Expanded @mentions in goal', { goal_length: options.goal.length })
    }
  }

  const dbPath = path.join(os.homedir(), '.ava', 'data.db')
  const platform = createNodePlatform(dbPath)
  setPlatform(platform)

  await migrateOAuthCredentials()
  registerCoreTools()
  registerMockProvider()

  const bus = new MessageBus()
  const sessionManager = createSessionManager()
  await resolveResume(options, sessionManager)

  const manager = new ExtensionManager(bus, sessionManager)
  const currentDir = path.dirname(fileURLToPath(import.meta.url))
  const extensionsDir = path.resolve(currentDir, '../../../packages/extensions')
  const extensionCount = await activateExtensions(manager, extensionsDir)

  await loadAgentModeSelector(extensionsDir)
  const promptsModule = await loadPromptsModule(extensionsDir, options.cwd)
  const instructionsReady = createInstructionsReadyPromise(promptsModule, options)
  const permissionBridge = setupPermissionPrompts(bus, options)

  const abortController = new AbortController()
  let aborted = false
  const onSignal = () => {
    if (aborted) process.exit(1)
    aborted = true
    process.stderr.write('\nAborting agent... (press Ctrl+C again to force)\n')
    abortController.abort()
    try {
      log.warn('Agent-v2 abort requested')
    } catch {
      // Ignore logger errors in signal handler.
    }
  }

  process.on('SIGINT', onSignal)
  process.on('SIGTERM', onSignal)

  try {
    const onAgentEvent = createEventHandler(options)

    if (options.verbose) {
      process.stderr.write(`[agent-v2] Running with goal: ${options.goal}\n`)
      process.stderr.write(
        `[agent-v2] Provider: ${options.provider}, Model: ${options.model || 'default'}, Max turns: ${options.maxTurns}\n`
      )
      process.stderr.write(`[agent-v2] Extensions loaded: ${extensionCount}\n`)
      process.stderr.write(`[agent-v2] Yolo: ${options.yolo}\n\n`)
    }

    const session = sessionManager.create(options.goal.slice(0, 50), options.cwd)
    emitEvent('session:opened', {
      sessionId: session.id,
      workingDirectory: options.cwd,
    })
    await instructionsReady

    const systemPrompt = promptsModule
      ? promptsModule.buildSystemPrompt(options.model || undefined)
      : undefined

    const agent = new AgentExecutor(
      {
        provider: options.provider as LLMProvider,
        model: options.model || undefined,
        maxTurns: options.maxTurns,
        maxTimeMinutes: options.timeout,
        systemPrompt,
        toolMode: options.praxis ? 'praxis' : resolveAgentMode(options.goal, getAgentModes()),
      },
      onAgentEvent
    )

    const result = await agent.run({ goal: options.goal, cwd: options.cwd }, abortController.signal)
    log.info('Agent-v2 command completed', {
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
      console.log(`\n--- Agent V2 Summary ---`)
      console.log(`Status:   ${result.success ? 'SUCCESS' : 'FAILED'} (${result.terminateMode})`)
      console.log(`Turns:    ${result.turns}`)
      console.log(`Tokens:   ${input} in / ${output} out`)
      console.log(`Duration: ${(result.durationMs / 1000).toFixed(1)}s`)
      if (result.output) {
        console.log(`Output:   ${result.output.slice(0, 500)}`)
      }
    }

    process.exitCode = result.success ? 0 : 1
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    log.error('Agent-v2 command failed', { error: message })
    if (options.json) {
      console.log(JSON.stringify({ type: 'error', error: message }))
    } else {
      process.stderr.write(`\nAgent V2 error: ${message}\n`)
    }
    process.exitCode = 1
  } finally {
    process.removeListener('SIGINT', onSignal)
    process.removeListener('SIGTERM', onSignal)
    permissionBridge.unsubscribe()
    permissionBridge.close()
    await manager.dispose()
  }
}
