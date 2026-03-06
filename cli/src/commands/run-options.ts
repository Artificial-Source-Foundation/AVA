import type { LLMProvider } from '@ava/core-v2'

export type AgentBackend = 'core' | 'core-v2'

export interface RunOptions {
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
  yolo: boolean
}

export function parseRunOptions(args: string[]): RunOptions | null {
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
  let yolo = false
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
      maxTurns = parseInt(args[++i] ?? '', 10)
      continue
    }
    if (arg === '--max-time') {
      maxTimeMinutes = parseInt(args[++i] ?? '', 10)
      continue
    }
    if (arg === '--cwd') {
      cwd = args[++i] ?? cwd
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
    if (arg === '--yolo') {
      yolo = true
      continue
    }

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
    yolo,
    backend,
  }
}

export function printRunHelp(): void {
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
  --yolo                Bypass all permission checks (auto-approve everything)
  --validation          Enable QA validation gate

EXAMPLES:
  ava run "List all TypeScript files in src/"
  ava run "Fix the bug in auth.ts" --provider anthropic --max-turns 10
  ava run "Read the README" --mock --max-turns 3
  ava run "Read the codebase" --provider openai --model gpt-4o --yolo
  ava run "Refactor the database module" --backend core --verbose
  ava run "Refactor the database module" --json --verbose
`)
}
