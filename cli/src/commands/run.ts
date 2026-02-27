/**
 * Run Command
 * Execute the agent loop from the CLI
 *
 * Usage:
 *   ava run "<goal>"
 *   ava run "<goal>" --mock --max-turns 3
 *   ava run "<goal>" --provider anthropic --model claude-sonnet-4-20250514
 */

import type { AgentConfig, AgentEvent, AgentEventCallback, LLMProvider } from '@ava/core-v2'
import { AgentExecutor, registerProvider } from '@ava/core-v2'
import { MockLLMClient, setupMockEnvironment } from './mock-client.js'

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
}

export async function runRunCommand(args: string[]): Promise<void> {
  const options = parseRunOptions(args)
  if (!options) {
    printRunHelp()
    return
  }

  // Set up mock if requested
  if (options.mock) {
    setupMockEnvironment()
    registerProvider('anthropic', () => new MockLLMClient())
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

  process.on('SIGINT', () => {
    if (!ac.signal.aborted) {
      console.log('\n[Agent] Aborting...')
      ac.abort()
    }
  })

  process.on('SIGTERM', () => {
    if (!ac.signal.aborted) {
      ac.abort()
    }
  })

  // Create event callback
  const onEvent: AgentEventCallback = options.json
    ? (event) => console.log(JSON.stringify(event))
    : createStreamingCallback(options.verbose)

  // Create and run agent
  const executor = new AgentExecutor(config, onEvent)

  const startTime = Date.now()
  try {
    const result = await executor.run(
      {
        goal: options.goal,
        cwd: options.cwd,
      },
      ac.signal
    )

    const durationMs = Date.now() - startTime

    if (options.json) {
      console.log(JSON.stringify({ type: 'summary', result, durationMs }))
    } else {
      console.log('')
      const status = result.success ? 'SUCCESS' : `FAILED (${result.terminateMode})`
      const tokens = result.tokensUsed.input + result.tokensUsed.output
      console.log(`[Done] ${status} (${result.turns} turns, ${durationMs}ms, ${tokens} tokens)`)
      if (result.output) {
        console.log('')
        console.log(result.output)
      }
      if (result.error) {
        console.error('')
        console.error(`Error: ${result.error}`)
      }
    }

    process.exit(result.success ? 0 : 1)
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    if (options.json) {
      console.log(JSON.stringify({ type: 'error', error: message }))
    } else {
      console.error(`[Error] ${message}`)
    }
    process.exit(1)
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

  return { goal, provider, model, maxTurns, maxTimeMinutes, cwd, json, verbose, mock, validation }
}

function printRunHelp(): void {
  console.log(`
AVA Run - Execute the agent loop

USAGE:
  ava run "<goal>" [options]

OPTIONS:
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
  ava run "Refactor the database module" --json --verbose
`)
}
