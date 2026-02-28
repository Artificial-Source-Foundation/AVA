/**
 * AVA CLI Agent V2 Command
 *
 * Uses core-v2 agent loop + extension system.
 * This is the new stack that will eventually replace the original agent command.
 */

import * as os from 'node:os'
import * as path from 'node:path'
import * as readline from 'node:readline'
import { fileURLToPath } from 'node:url'
import { type AgentEvent, type AgentEventCallback, AgentExecutor } from '@ava/core-v2/agent'
import type { BusMessage } from '@ava/core-v2/bus'
import { MessageBus } from '@ava/core-v2/bus'
import type { ExtensionModule } from '@ava/core-v2/extensions'
import { ExtensionManager, loadAllBuiltInExtensions } from '@ava/core-v2/extensions'
import type { LLMProvider } from '@ava/core-v2/llm'
import { registerProvider } from '@ava/core-v2/llm'
import { setPlatform } from '@ava/core-v2/platform'
import { createSessionManager } from '@ava/core-v2/session'
import { registerCoreTools } from '@ava/core-v2/tools'
import { createNodePlatform } from '@ava/platform-node/v2'

interface AgentV2Options {
  goal: string
  provider: string
  model: string
  maxTurns: number
  timeout: number
  cwd: string
  verbose: boolean
  yolo: boolean
  json: boolean
}

function parseArgs(args: string[]): AgentV2Options | null {
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
    } else if (arg === '--verbose') {
      verbose = true
    } else if (arg === '--yolo') {
      yolo = true
    } else if (arg === '--json') {
      json = true
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

  return { goal, provider, model, maxTurns, timeout, cwd, verbose, yolo, json }
}

export async function runAgentV2Command(args: string[]): Promise<void> {
  const options = parseArgs(args)
  if (!options) return

  // Initialize core-v2 platform
  const dbPath = path.join(os.homedir(), '.ava', 'data.db')
  const platform = createNodePlatform(dbPath)
  setPlatform(platform)

  // Register the 6 core tools (read, write, edit, bash, glob, grep)
  registerCoreTools()

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

  // Build system prompt from prompts extension sections
  let systemPrompt: string | undefined
  try {
    const promptsModule = (await importWithDistFallback(
      path.resolve(extensionsDir, 'prompts/src/builder.ts'),
      path.resolve(extensionsDir, 'dist/prompts/src/builder.js')
    )) as {
      addPromptSection: (s: { name: string; priority: number; content: string }) => void
      buildSystemPrompt: (model?: string) => string
    }
    promptsModule.addPromptSection({
      name: 'cwd',
      priority: 100,
      content: `Working directory: ${options.cwd}`,
    })
    systemPrompt = promptsModule.buildSystemPrompt(options.model || undefined)
  } catch {
    // Prompts extension not available — use default
  }

  // Set up tool approval via bus (Phase 2)
  const alwaysApproved = new Set<string>()
  let rlInterface: readline.Interface | undefined

  if (!options.yolo && !options.json) {
    bus.subscribe('permission:request', async (msg: BusMessage) => {
      const data = msg as BusMessage & {
        toolName: string
        args: Record<string, unknown>
        risk: string
      }

      // Check "always" approved
      if (alwaysApproved.has(data.toolName)) {
        bus.publish({
          type: 'permission:response',
          correlationId: msg.correlationId,
          timestamp: Date.now(),
          approved: true,
        } as BusMessage & { approved: boolean })
        return
      }

      const argsPreview = JSON.stringify(data.args).slice(0, 120)
      const riskLabel =
        data.risk === 'high'
          ? '\x1b[31m[HIGH]\x1b[0m'
          : data.risk === 'medium'
            ? '\x1b[33m[MED]\x1b[0m'
            : ''

      if (!rlInterface) {
        rlInterface = readline.createInterface({ input: process.stdin, output: process.stderr })
      }

      const answer = await new Promise<string>((resolve) => {
        rlInterface!.question(
          `${riskLabel} Allow \x1b[1m${data.toolName}\x1b[0m(${argsPreview})? [y/N/a(lways)] `,
          (ans) => resolve(ans.trim().toLowerCase())
        )
      })

      const approved = answer === 'y' || answer === 'yes' || answer === 'a' || answer === 'always'
      if (answer === 'a' || answer === 'always') {
        alwaysApproved.add(data.toolName)
      }

      bus.publish({
        type: 'permission:response',
        correlationId: msg.correlationId,
        timestamp: Date.now(),
        approved,
        reason: approved ? undefined : 'Denied by user',
      } as BusMessage & { approved: boolean; reason?: string })
    })
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

  try {
    // Build the event handler based on output mode
    const onEvent = createEventHandler(options)

    if (options.verbose) {
      process.stderr.write(`[agent-v2] Running with goal: ${options.goal}\n`)
      process.stderr.write(
        `[agent-v2] Provider: ${options.provider}, Model: ${options.model || 'default'}, Max turns: ${options.maxTurns}\n`
      )
      process.stderr.write(`[agent-v2] Extensions loaded: ${extensionCount}\n`)
      process.stderr.write(`[agent-v2] Yolo: ${options.yolo}\n\n`)
    }

    const agent = new AgentExecutor(
      {
        provider: options.provider as LLMProvider,
        model: options.model || undefined,
        maxTurns: options.maxTurns,
        maxTimeMinutes: options.timeout,
        systemPrompt,
      },
      onEvent
    )

    const result = await agent.run({ goal: options.goal, cwd: options.cwd }, abortController.signal)

    if (options.json) {
      // NDJSON summary line
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
    if (options.json) {
      console.log(JSON.stringify({ type: 'error', error: message }))
    } else {
      process.stderr.write(`\nAgent V2 error: ${message}\n`)
    }
    process.exitCode = 1
  } finally {
    process.removeListener('SIGINT', onSignal)
    process.removeListener('SIGTERM', onSignal)
    rlInterface?.close()
    await manager.dispose()
  }
}

// ─── Event Handler Factory ───────────────────────────────────────────────────

function createEventHandler(options: AgentV2Options): AgentEventCallback | undefined {
  if (options.json) {
    return (event: AgentEvent) => {
      console.log(JSON.stringify(event))
    }
  }

  if (options.verbose) {
    return (event: AgentEvent) => {
      switch (event.type) {
        case 'turn:start':
          process.stderr.write(`\n\x1b[36m── Turn ${event.turn} ──\x1b[0m\n`)
          break
        case 'thought':
          process.stderr.write(`\x1b[2m${event.content.slice(0, 300)}\x1b[0m\n`)
          break
        case 'tool:start':
          process.stderr.write(formatToolStart(event.toolName, event.args))
          break
        case 'tool:finish':
          process.stderr.write(formatToolFinish(event.toolName, event.success, event.durationMs))
          break
        case 'retry':
          process.stderr.write(
            `\x1b[33m[retry] Attempt ${(event as RetryEvent).attempt}/${(event as RetryEvent).maxRetries} — waiting ${((event as RetryEvent).delayMs / 1000).toFixed(1)}s\x1b[0m\n`
          )
          break
        case 'doom-loop':
          process.stderr.write(
            `\x1b[31m[doom-loop] ${(event as DoomLoopEvent).tool} called ${(event as DoomLoopEvent).count}x with same args\x1b[0m\n`
          )
          break
        case 'error':
          process.stderr.write(`\x1b[31m[error] ${event.error}\x1b[0m\n`)
          break
      }
    }
  }

  // Default mode — minimal spinner-like output
  return (event: AgentEvent) => {
    switch (event.type) {
      case 'tool:start':
        process.stderr.write(`  ${toolIcon(event.toolName)} ${event.toolName}\n`)
        break
      case 'error':
        process.stderr.write(`\x1b[31m  Error: ${event.error}\x1b[0m\n`)
        break
    }
  }
}

// ─── Tool Output Formatters ─────────────────────────────────────────────────

function toolIcon(name: string): string {
  switch (name) {
    case 'bash':
      return '$'
    case 'read_file':
      return '>'
    case 'write_file':
    case 'create_file':
      return '+'
    case 'edit':
    case 'multiedit':
      return '~'
    case 'delete_file':
      return '-'
    case 'glob':
    case 'grep':
      return '?'
    case 'task':
      return '|'
    default:
      return '*'
  }
}

function formatToolStart(toolName: string, args: Record<string, unknown>): string {
  const lines: string[] = []
  lines.push(`\x1b[1m  ${toolIcon(toolName)} ${toolName}\x1b[0m`)

  switch (toolName) {
    case 'bash': {
      const cmd = (args.command ?? '') as string
      lines.push(`    \x1b[2m$ ${cmd.slice(0, 200)}\x1b[0m`)
      break
    }
    case 'read_file': {
      const p = (args.path ?? args.filePath ?? '') as string
      lines.push(`    \x1b[2m${p}\x1b[0m`)
      break
    }
    case 'edit': {
      const p = (args.filePath ?? args.file_path ?? args.path ?? '') as string
      lines.push(`    \x1b[2m${p}\x1b[0m`)
      break
    }
    case 'write_file':
    case 'create_file': {
      const p = (args.path ?? args.filePath ?? '') as string
      lines.push(`    \x1b[2m${p}\x1b[0m`)
      break
    }
    case 'glob': {
      const pattern = (args.pattern ?? '') as string
      lines.push(`    \x1b[2m${pattern}\x1b[0m`)
      break
    }
    case 'grep': {
      const pattern = (args.pattern ?? '') as string
      lines.push(`    \x1b[2m/${pattern}/\x1b[0m`)
      break
    }
    case 'task': {
      const desc = (args.description ?? '') as string
      lines.push(`    \x1b[2m└ ${desc}\x1b[0m`)
      break
    }
    default: {
      const preview = JSON.stringify(args).slice(0, 120)
      lines.push(`    \x1b[2m${preview}\x1b[0m`)
    }
  }

  return lines.join('\n') + '\n'
}

function formatToolFinish(_toolName: string, success: boolean, durationMs: number): string {
  const status = success ? '\x1b[32mOK\x1b[0m' : '\x1b[31mFAIL\x1b[0m'
  return `    ${status} \x1b[2m(${durationMs}ms)\x1b[0m\n`
}

// Extended event type narrowing helpers
type RetryEvent = Extract<AgentEvent, { type: 'retry' }>
type DoomLoopEvent = Extract<AgentEvent, { type: 'doom-loop' }>

/** Import a module with fallback from .ts source to compiled .js in dist. */
async function importWithDistFallback(
  sourcePath: string,
  distPath: string
): Promise<Record<string, unknown>> {
  try {
    return (await import(sourcePath)) as Record<string, unknown>
  } catch {
    return (await import(distPath)) as Record<string, unknown>
  }
}

function printHelp(): void {
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
  --verbose             Verbose output with tool details
  --yolo                Auto-approve all tool calls (skip confirmation)
  --json                NDJSON output for scripting

EXAMPLES:
  ava agent-v2 run "list files" --verbose
  ava agent-v2 run "create hello.txt" --provider anthropic
  ava agent-v2 run "find TODOs" --provider anthropic --model claude-sonnet-4-20250514 --yolo
  ava agent-v2 run "refactor utils" --provider anthropic --json
`)
}
