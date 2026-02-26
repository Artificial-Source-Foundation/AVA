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
import { ExtensionManager, loadAllBuiltInExtensions } from '@ava/core-v2/extensions'
import type { LLMProvider } from '@ava/core-v2/llm'
import { registerProvider } from '@ava/core-v2/llm'
import { setPlatform } from '@ava/core-v2/platform'
import { createSessionManager } from '@ava/core-v2/session'
import { createNodePlatform } from '@ava/platform-node/v2'

interface AgentV2Options {
  goal: string
  provider: string
  maxTurns: number
  timeout: number
  cwd: string
  verbose: boolean
}

function parseArgs(args: string[]): AgentV2Options | null {
  if (args[0] !== 'run' || args.length < 2) {
    printHelp()
    return null
  }

  let goal = ''
  let provider = 'mock'
  let maxTurns = 20
  let timeout = 10
  let cwd = process.cwd()
  let verbose = false

  let i = 1
  while (i < args.length) {
    const arg = args[i]

    if (arg === '--provider' && i + 1 < args.length) {
      provider = args[++i]!
    } else if (arg === '--max-turns' && i + 1 < args.length) {
      maxTurns = parseInt(args[++i]!, 10)
    } else if (arg === '--timeout' && i + 1 < args.length) {
      timeout = parseInt(args[++i]!, 10)
    } else if (arg === '--cwd' && i + 1 < args.length) {
      cwd = args[++i]!
    } else if (arg === '--verbose') {
      verbose = true
    } else if (!arg!.startsWith('--') && !goal) {
      goal = arg!
    }

    i++
  }

  if (!goal) {
    console.error('Error: No goal provided.')
    console.error('Usage: ava agent-v2 run "your goal here"')
    return null
  }

  return { goal, provider, maxTurns, timeout, cwd, verbose }
}

export async function runAgentV2Command(args: string[]): Promise<void> {
  const options = parseArgs(args)
  if (!options) return

  // Initialize core-v2 platform
  const dbPath = path.join(os.homedir(), '.ava', 'data.db')
  const platform = createNodePlatform(dbPath)
  setPlatform(platform)

  // Always register the mock provider as a fallback
  registerProvider('mock', () => ({
    async *stream() {
      yield { content: 'Mock response — core-v2 agent loop is working!' }
      yield { done: true }
    },
  }))

  // Load built-in extensions (providers, tools, permissions, etc.)
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
    process.stderr.write(`[agent-v2] Warning: Failed to load extensions: ${message}\n`)
  }

  // Set up abort controller
  const abortController = new AbortController()
  let aborted = false

  const onSignal = () => {
    if (aborted) process.exit(1)
    aborted = true
    process.stderr.write('\nAborting agent... (press Ctrl+C again to force)\n')
    abortController.abort()
  }

  process.on('SIGINT', onSignal)
  process.on('SIGTERM', onSignal)

  const startTime = Date.now()

  try {
    if (options.verbose) {
      process.stderr.write(`[agent-v2] Running with goal: ${options.goal}\n`)
      process.stderr.write(
        `[agent-v2] Provider: ${options.provider}, Max turns: ${options.maxTurns}\n`
      )
      process.stderr.write(`[agent-v2] Extensions loaded: ${extensionCount}\n\n`)
    }

    const agent = new AgentExecutor({
      provider: options.provider as LLMProvider,
      maxTurns: options.maxTurns,
      maxTimeMinutes: options.timeout,
    })

    const result = await agent.run({ goal: options.goal, cwd: options.cwd }, abortController.signal)

    const durationMs = Date.now() - startTime
    const { input, output } = result.tokensUsed

    console.log(`\n--- Agent V2 Summary ---`)
    console.log(`Status:   ${result.success ? 'SUCCESS' : 'FAILED'} (${result.terminateMode})`)
    console.log(`Turns:    ${result.turns}`)
    console.log(`Tokens:   ${input} in / ${output} out`)
    console.log(`Duration: ${(durationMs / 1000).toFixed(1)}s`)

    if (result.output) {
      console.log(`Output:   ${result.output.slice(0, 500)}`)
    }

    process.exitCode = result.success ? 0 : 1
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    process.stderr.write(`\nAgent V2 error: ${message}\n`)
    process.exitCode = 1
  } finally {
    process.removeListener('SIGINT', onSignal)
    process.removeListener('SIGTERM', onSignal)
    await manager.dispose()
  }
}

function printHelp(): void {
  console.log(`
AVA Agent V2 - Core-v2 agent loop with extension system

USAGE:
  ava agent-v2 run "your goal here" [OPTIONS]

OPTIONS:
  --provider <name>     LLM provider (default: mock)
  --max-turns <n>       Maximum turns (default: 20)
  --timeout <minutes>   Timeout in minutes (default: 10)
  --cwd <path>          Working directory (default: current)
  --verbose             Verbose output to stderr

EXAMPLES:
  ava agent-v2 run "list files" --verbose
  ava agent-v2 run "create hello.txt" --provider anthropic
`)
}
