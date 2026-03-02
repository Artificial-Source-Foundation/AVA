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
import type { AgentConfig, AgentEvent, AgentEventCallback, LLMProvider } from '@ava/core-v2'
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
import { MockLLMClient, setupMockEnvironment } from './mock-client.js'

type AgentBackend = 'core' | 'core-v2'

interface RunOptions {
  goal: string
  provider?: LLMProvider
  model?: string
  maxTurns: number
  maxTimeMinutes: number
  cwd: string
  json: boolean
  verbose: boolean
  mock: boolean
  validation: boolean
  backend: AgentBackend
}

export async function runRunCommand(args: string[]): Promise<void> {
  const options = parseRunOptions(args)
  if (!options) {
    printRunHelp()
    return
  }

  // Route to legacy agent command for 'core' backend
  if (options.backend === 'core') {
    if (options.verbose) {
      process.stderr.write('[run] Using legacy core backend\n')
    }
    const { runAgentCommand } = await import('./agent.js')
    // Re-pack args for the legacy agent command: agent run "goal" [flags]
    const legacyArgs = ['run', options.goal]
    if (options.provider) legacyArgs.push('--provider', options.provider)
    if (options.model) legacyArgs.push('--model', options.model)
    legacyArgs.push('--max-turns', String(options.maxTurns))
    legacyArgs.push('--timeout', String(options.maxTimeMinutes))
    legacyArgs.push('--cwd', options.cwd)
    if (options.json) legacyArgs.push('--json')
    if (options.verbose) legacyArgs.push('--verbose')
    await runAgentCommand(legacyArgs)
    return
  }

  // Initialize platform + core tools
  const dbPath = path.join(os.homedir(), '.ava', 'data.db')
  setPlatform(createNodePlatform(dbPath))
  registerCoreTools()

  // Set up mock if requested
  if (options.mock) {
    setupMockEnvironment()
    registerProvider('mock', () => new MockLLMClient())
    registerProvider('anthropic', () => new MockLLMClient())
    if (!options.provider) {
      options.provider = 'mock' as LLMProvider
    }
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
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
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
  const eventHandler: AgentEventCallback = options.json
    ? (event) => console.log(JSON.stringify(event))
    : createStreamingCallback(options.verbose)

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
      process.stderr.write(`[run] Extensions loaded: ${extensionCount}\n\n`)
    }

    // Create and run agent
    const executor = new AgentExecutor({ ...config, systemPrompt }, eventHandler)

    const result = await executor.run({ goal: options.goal, cwd: options.cwd }, ac.signal)

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
        `  Turns: ${result.turns}, Tokens: ${input} in / ${output} out, Duration: ${(result.durationMs / 1000).toFixed(1)}s`
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
    if (options.json) {
      console.log(JSON.stringify({ type: 'error', error: message }))
    } else {
      console.error(`[Error] ${message}`)
    }
    process.exitCode = 1
  } finally {
    process.removeListener('SIGINT', onSignal)
    process.removeListener('SIGTERM', onSignal)
    await manager.dispose()
  }
}

/** Try importing from source (tsx), fall back to compiled dist. */
async function importWithFallback(srcPath: string, distPath: string): Promise<unknown> {
  try {
    return await import(srcPath)
  } catch {
    return await import(distPath)
  }
}

function createStreamingCallback(verbose: boolean): AgentEventCallback {
  return (event: AgentEvent) => {
    switch (event.type) {
      case 'agent:start':
        console.log(`[Agent] Starting: ${event.goal}`)
        break

      case 'turn:start':
        console.log(`[Turn ${event.turn}] ---`)
        break

      case 'tool:start':
        console.log(`[Tool] ${event.toolName}(${JSON.stringify(event.args)})`)
        break

      case 'tool:finish':
        console.log(
          `[Tool] ${event.toolName}: ${event.success ? 'OK' : 'FAIL'} (${event.durationMs}ms)`
        )
        break

      case 'thought':
        if (verbose) {
          console.log(`[Thought] ${event.content}`)
        }
        break

      case 'error':
        console.error(`[Error] ${event.error}`)
        break

      default:
        if (verbose) {
          console.log(`[Event] ${event.type}`)
        }
    }
  }
}

function parseRunOptions(args: string[]): RunOptions | null {
  // First non-flag argument is the goal
  let goal: string | undefined
  let provider: LLMProvider | undefined
  let model: string | undefined
  let maxTurns = 20
  let maxTimeMinutes = 10
  let cwd = process.cwd()
  let json = false
  let verbose = false
  let mock = false
  let validation = false
  let backend: AgentBackend = 'core-v2'

  for (let i = 0; i < args.length; i += 1) {
    const arg = args[i]

    if (arg === '--provider') {
      provider = args[++i] as LLMProvider
      continue
    }
    if (arg === '--model') {
      model = args[++i]
      continue
    }
    if (arg === '--max-turns') {
      maxTurns = parseInt(args[++i], 10)
      continue
    }
    if (arg === '--max-time') {
      maxTimeMinutes = parseInt(args[++i], 10)
      continue
    }
    if (arg === '--cwd') {
      cwd = args[++i]
      continue
    }
    if (arg === '--backend') {
      const val = args[++i]
      if (val === 'core' || val === 'core-v2') {
        backend = val
      } else {
        console.error(`Error: Invalid backend "${val}". Must be "core" or "core-v2".`)
        return null
      }
      continue
    }
    if (arg === '--json') {
      json = true
      continue
    }
    if (arg === '--verbose') {
      verbose = true
      continue
    }
    if (arg === '--mock') {
      mock = true
      continue
    }
    if (arg === '--validation') {
      validation = true
      continue
    }

    // First non-flag argument is the goal
    if (!goal && !arg.startsWith('--')) {
      goal = arg
    }
  }

  if (!goal) {
    return null
  }

  return {
    goal,
    provider,
    model,
    maxTurns,
    maxTimeMinutes,
    cwd,
    json,
    verbose,
    mock,
    validation,
    backend,
  }
}

function printRunHelp(): void {
  console.log(`
AVA Run - Unified agent entry point with dual-stack support

USAGE:
  ava run "<goal>" [options]

OPTIONS:
  --backend <stack>     Agent backend: "core" (legacy) or "core-v2" (default)
  --provider <name>     LLM provider (anthropic, openai, openrouter, etc.)
  --model <name>        Specific model name
  --max-turns <n>       Maximum turns (default: 20)
  --max-time <n>        Time limit in minutes (default: 10)
  --cwd <path>          Working directory (default: current)
  --json                Machine-readable JSON output
  --verbose             Show debug-level output (thoughts, tool output)
  --mock                Use mock LLM (no API key needed)
  --validation          Enable QA validation gate

EXAMPLES:
  ava run "List all TypeScript files in src/"
  ava run "Fix the bug in auth.ts" --provider anthropic --max-turns 10
  ava run "Read the README" --mock --max-turns 3
  ava run "Refactor the database module" --backend core --verbose
  ava run "Refactor the database module" --json --verbose
`)
}
