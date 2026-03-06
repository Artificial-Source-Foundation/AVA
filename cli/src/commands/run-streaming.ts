import type { AgentEvent, AgentEventCallback } from '@ava/core-v2'
import type { DiffRenderer } from '../rendering/diff-renderer.js'

export function createStreamingCallback(
  verbose: boolean,
  renderer?: DiffRenderer
): AgentEventCallback {
  if (!renderer) {
    return createConsoleStreamingCallback(verbose)
  }

  const lines: string[] = []
  return (event: AgentEvent) => {
    switch (event.type) {
      case 'agent:start':
        lines.push(`[Agent] Starting: ${event.goal}`)
        break
      case 'turn:start':
        lines.push('')
        lines.push(`[Turn ${event.turn}] ---`)
        break
      case 'turn:end': {
        const u = event.usage
        if (u) {
          const parts = [`  tokens: ${u.inputTokens} in / ${u.outputTokens} out`]
          if (u.cacheReadTokens) parts.push(`(${u.cacheReadTokens} cached)`)
          lines.push(parts.join(' '))
        }
        break
      }
      case 'tool:start':
        lines.push(`[Tool] ${event.toolName}(${JSON.stringify(event.args)})`)
        break
      case 'tool:finish':
        lines.push(
          `[Tool] ${event.toolName}: ${event.success ? 'OK' : 'FAIL'} (${event.durationMs}ms)`
        )
        break
      case 'thought':
        if (verbose) {
          lines.push(`[Thought] ${event.content}`)
        }
        break
      case 'error':
        lines.push(`[Error] ${event.error}`)
        break
      default:
        if (verbose) {
          lines.push(`[Event] ${event.type}`)
        }
    }
    renderer.render(lines)
  }
}

function createConsoleStreamingCallback(verbose: boolean): AgentEventCallback {
  return (event: AgentEvent) => {
    switch (event.type) {
      case 'agent:start':
        console.log(`[Agent] Starting: ${event.goal}`)
        break
      case 'turn:start':
        console.log(`\n[Turn ${event.turn}] ---`)
        break
      case 'turn:end': {
        const u = event.usage
        if (u) {
          const parts = [`  tokens: ${u.inputTokens} in / ${u.outputTokens} out`]
          if (u.cacheReadTokens) parts.push(`(${u.cacheReadTokens} cached)`)
          console.log(parts.join(' '))
        }
        break
      }
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
