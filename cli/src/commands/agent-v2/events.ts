import type { AgentEvent, AgentEventCallback } from '@ava/core-v2/agent'
import type { AgentV2Options, CompactingEvent, DoomLoopEvent, RetryEvent } from './types.js'

export function createEventHandler(options: AgentV2Options): AgentEventCallback | undefined {
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
        case 'context:compacting': {
          const ce = event as CompactingEvent
          process.stderr.write(
            `\x1b[33m[compaction] ${ce.messagesBefore} → ${ce.messagesAfter} messages (${ce.estimatedTokens} tokens / ${ce.contextLimit} limit)\x1b[0m\n`
          )
          break
        }
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

  return `${lines.join('\n')}\n`
}

function formatToolFinish(_toolName: string, success: boolean, durationMs: number): string {
  const status = success ? '\x1b[32mOK\x1b[0m' : '\x1b[31mFAIL\x1b[0m'
  return `    ${status} \x1b[2m(${durationMs}ms)\x1b[0m\n`
}
