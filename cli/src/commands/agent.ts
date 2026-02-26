/**
 * AVA CLI Agent Command
 * Invokes the agent loop from the command line
 */

import type { LLMProvider } from '@ava/core'
import {
  type AgentEvent,
  type AgentResult,
  AvaLogger,
  getSettingsManager,
  runAgent,
  setLogger,
} from '@ava/core'

// ============================================================================
// Types
// ============================================================================

interface AgentCommandOptions {
  goal: string
  provider?: LLMProvider
  model?: string
  maxTurns: number
  timeout: number
  cwd: string
  verbose: boolean
  json: boolean
  logFile?: string
}

// ============================================================================
// Argument Parsing
// ============================================================================

function parseAgentArgs(args: string[]): AgentCommandOptions | null {
  const subcommand = args[0]

  if (subcommand !== 'run' || args.length < 2) {
    printHelp()
    return null
  }

  // Find the goal (first non-flag argument after 'run')
  let goal = ''
  let provider: LLMProvider | undefined
  let model: string | undefined
  let maxTurns = 20
  let timeout = 10
  let cwd = process.cwd()
  let verbose = false
  let json = false
  let logFile: string | undefined

  let i = 1
  while (i < args.length) {
    const arg = args[i]

    if (arg === '--provider' && i + 1 < args.length) {
      provider = args[++i] as LLMProvider
    } else if (arg === '--model' && i + 1 < args.length) {
      model = args[++i]
    } else if (arg === '--max-turns' && i + 1 < args.length) {
      maxTurns = parseInt(args[++i], 10)
    } else if (arg === '--timeout' && i + 1 < args.length) {
      timeout = parseInt(args[++i], 10)
    } else if (arg === '--cwd' && i + 1 < args.length) {
      cwd = args[++i]
    } else if (arg === '--verbose') {
      verbose = true
    } else if (arg === '--json') {
      json = true
    } else if (arg === '--log-file' && i + 1 < args.length) {
      logFile = args[++i]
    } else if (!arg.startsWith('--') && !goal) {
      goal = arg
    }

    i++
  }

  if (!goal) {
    console.error('Error: No goal provided.')
    console.error('Usage: ava agent run "your goal here"')
    return null
  }

  return { goal, provider, model, maxTurns, timeout, cwd, verbose, json, logFile }
}

// ============================================================================
// Event Formatting
// ============================================================================

function formatEventVerbose(event: AgentEvent): string {
  const time = new Date(event.timestamp).toLocaleTimeString()

  switch (event.type) {
    case 'agent:start':
      return `[${time}] Agent started — goal: ${event.goal}`
    case 'agent:finish':
      return `[${time}] Agent finished — ${event.result.success ? 'SUCCESS' : 'FAILED'} (${event.result.terminateMode}, ${event.result.turns} turns, ${event.result.tokensUsed} tokens, ${event.result.durationMs}ms)`
    case 'turn:start':
      return `[${time}] Turn ${event.turn} started`
    case 'turn:finish':
      return `[${time}] Turn ${event.turn} finished (${event.toolCalls.length} tool calls)`
    case 'tool:start':
      return `[${time}] Tool ${event.toolName} started`
    case 'tool:finish':
      return `[${time}] Tool ${event.toolName} ${event.success ? 'OK' : 'FAIL'} (${event.durationMs}ms)`
    case 'tool:error':
      return `[${time}] Tool ${event.toolName} ERROR: ${event.error}`
    case 'tool:metadata':
      return `[${time}] Tool ${event.toolName} metadata${event.title ? `: ${event.title}` : ''}`
    case 'thought':
      return `[${time}] Thought: ${event.text.slice(0, 120)}${event.text.length > 120 ? '...' : ''}`
    case 'recovery:start':
      return `[${time}] Recovery started: ${event.reason}`
    case 'recovery:finish':
      return `[${time}] Recovery ${event.success ? 'succeeded' : 'failed'} (${event.durationMs}ms)`
    case 'validation:start':
      return `[${time}] Validation started for ${event.files.length} files`
    case 'validation:result':
      return `[${time}] Validation ${event.passed ? 'PASSED' : 'FAILED'}: ${event.summary}`
    case 'validation:finish':
      return `[${time}] Validation finished: ${event.passed ? 'passed' : 'failed'} (${event.durationMs}ms)`
    case 'provider:switch':
      return `[${time}] Provider switched to ${event.provider}/${event.model}`
    case 'error':
      return `[${time}] ERROR: ${event.error}`
  }
}

function formatSummary(result: AgentResult, durationMs: number): string {
  const lines = [
    '',
    `--- Agent Summary ---`,
    `Status:   ${result.success ? 'SUCCESS' : 'FAILED'} (${result.terminateMode})`,
    `Turns:    ${result.turns}`,
    `Tokens:   ${result.tokensUsed}`,
    `Duration: ${(durationMs / 1000).toFixed(1)}s`,
  ]

  if (result.output) {
    lines.push(`Output:   ${result.output.slice(0, 500)}`)
  }

  if (result.error) {
    lines.push(`Error:    ${result.error}`)
  }

  return lines.join('\n')
}

// ============================================================================
// Agent Command
// ============================================================================

export async function runAgentCommand(args: string[]): Promise<void> {
  const options = parseAgentArgs(args)
  if (!options) return

  // Load settings
  try {
    await getSettingsManager().load()
  } catch {
    // Settings not loaded — continue with defaults
  }

  // Set up logger
  const logger = new AvaLogger({
    file: !!options.logFile,
    filePath: options.logFile,
    stderr: false,
    level: 'debug',
  })
  setLogger(logger)

  // Set up abort controller for Ctrl+C
  const abortController = new AbortController()
  let aborted = false

  const onSignal = () => {
    if (aborted) {
      // Second Ctrl+C — force exit
      process.exit(1)
    }
    aborted = true
    process.stderr.write('\nAborting agent... (press Ctrl+C again to force)\n')
    abortController.abort()
  }

  process.on('SIGINT', onSignal)
  process.on('SIGTERM', onSignal)

  const startTime = Date.now()

  // Event handler
  const onEvent = (event: AgentEvent) => {
    // NDJSON to stdout
    if (options.json) {
      process.stdout.write(`${JSON.stringify(event)}\n`)
    }

    // Verbose to stderr
    if (options.verbose) {
      process.stderr.write(`${formatEventVerbose(event)}\n`)
    }

    // Log to file via logger
    logger.fromAgentEvent(event)
  }

  try {
    if (options.verbose) {
      process.stderr.write(`Running agent with goal: ${options.goal}\n`)
      process.stderr.write(
        `Provider: ${options.provider ?? 'anthropic'}, Max turns: ${options.maxTurns}\n\n`
      )
    }

    const result = await runAgent(
      {
        goal: options.goal,
        cwd: options.cwd,
      },
      {
        provider: options.provider ?? 'anthropic',
        model: options.model,
        maxTurns: options.maxTurns,
        maxTimeMinutes: options.timeout,
      },
      abortController.signal,
      onEvent
    )

    const durationMs = Date.now() - startTime

    // Print summary
    if (options.verbose || !options.json) {
      process.stderr.write(`${formatSummary(result, durationMs)}\n`)
    }

    // Exit code
    process.exitCode = result.success ? 0 : 1
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    process.stderr.write(`\nAgent error: ${message}\n`)
    process.exitCode = 1
  } finally {
    process.removeListener('SIGINT', onSignal)
    process.removeListener('SIGTERM', onSignal)
  }
}

// ============================================================================
// Help
// ============================================================================

function printHelp(): void {
  console.log(`
AVA Agent - Run the AI agent from the command line

USAGE:
  ava agent run "your goal here" [OPTIONS]

OPTIONS:
  --provider <name>     LLM provider (default: anthropic)
  --model <name>        Model name (default: provider default)
  --max-turns <n>       Maximum turns (default: 20)
  --timeout <minutes>   Timeout in minutes (default: 10)
  --cwd <path>          Working directory (default: current)
  --verbose             Human-readable event stream to stderr
  --json                NDJSON event stream to stdout
  --log-file <path>     Write all events to a custom log file

EXAMPLES:
  # Run agent with verbose output
  ava agent run "list files in current directory" --verbose

  # Pipe NDJSON events to jq
  ava agent run "create hello.txt" --json | jq '.type'

  # Use a specific provider and model
  ava agent run "refactor auth module" --provider openai --model gpt-4

  # Log to file
  ava agent run "fix bug" --log-file ./agent.ndjson --verbose

ENVIRONMENT VARIABLES:
  AVA_ANTHROPIC_API_KEY    Anthropic API key
  AVA_OPENAI_API_KEY       OpenAI API key
  AVA_OPENROUTER_API_KEY   OpenRouter API key
`)
}
