import type { SimpleLogger } from '@ava/core-v2/logger'
import type { AgentV2Options } from './types.js'

export function parseArgs(args: string[], log: SimpleLogger): AgentV2Options | null {
  if (args[0] !== 'run' || args.length < 2) {
    printHelp()
    return null
  }

  let goal = ''
  let provider = 'mock'
  let model = ''
  let maxTurns = 20
  let timeout = 10
  let cwd = process.cwd()
  let verbose = false
  let yolo = false
  let json = false
  let resume: string | null = null
  let praxis = false

  let i = 1
  while (i < args.length) {
    const arg = args[i]

    if (arg === '--provider' && i + 1 < args.length) {
      provider = args[++i]!
    } else if (arg === '--model' && i + 1 < args.length) {
      model = args[++i]!
    } else if (arg === '--max-turns' && i + 1 < args.length) {
      maxTurns = parseInt(args[++i]!, 10)
    } else if (arg === '--timeout' && i + 1 < args.length) {
      timeout = parseInt(args[++i]!, 10)
    } else if (arg === '--cwd' && i + 1 < args.length) {
      cwd = args[++i]!
    } else if (arg === '--resume' && i + 1 < args.length) {
      resume = args[++i]!
    } else if (arg === '--resume') {
      resume = 'latest'
    } else if (arg === '--verbose') {
      verbose = true
    } else if (arg === '--yolo') {
      yolo = true
    } else if (arg === '--json') {
      json = true
    } else if (arg === '--praxis') {
      praxis = true
    } else if (!arg!.startsWith('--') && !goal) {
      goal = arg!
    }

    i++
  }

  if (!goal && !resume) {
    log.warn('Agent-v2 command missing goal and resume target')
    console.error('Error: No goal provided.')
    console.error('Usage: ava agent-v2 run "your goal here"')
    return null
  }

  return { goal, provider, model, maxTurns, timeout, cwd, verbose, yolo, json, resume, praxis }
}

export function printHelp(): void {
  console.log(`
AVA Agent V2 - Core-v2 agent loop with extension system

USAGE:
  ava agent-v2 run "your goal here" [OPTIONS]

OPTIONS:
  --provider <name>     LLM provider (default: mock)
  --model <id>          Model ID (default: provider default)
  --max-turns <n>       Maximum turns (default: 20)
  --timeout <minutes>   Timeout in minutes (default: 10)
  --cwd <path>          Working directory (default: current)
  --resume [id]         Resume a previous session (latest if no ID given)
  --praxis              Force Praxis 3-tier delegation (auto-detected by default)
  --verbose             Verbose output with tool details
  --yolo                Auto-approve all tool calls (skip confirmation)
  --json                NDJSON output for scripting

EXAMPLES:
  ava agent-v2 run "list files" --verbose
  ava agent-v2 run "create hello.txt" --provider anthropic
  ava agent-v2 run "find TODOs" --provider anthropic --model claude-sonnet-4-20250514 --yolo
  ava agent-v2 run "refactor utils" --provider anthropic --json
  ava agent-v2 run "continue working" --resume
`)
}
