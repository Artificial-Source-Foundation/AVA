/**
 * Context Tracking
 * Token tracking sync, conversation context building, and chat system prompt.
 * Compaction is now handled by AgentExecutor internally.
 */

import { getToolDefinitions } from '@ava/core-v2/tools'
import { getCoreBudget } from '../../services/core-bridge'
import type { ChatDeps } from './types'

// ============================================================================
// Tracker Stats Sync
// ============================================================================

/** Sync budget stats → contextStats signal */
export function syncTrackerStats(deps: ChatDeps): void {
  const budget = getCoreBudget()
  if (!budget) return
  const s = budget.getStats()
  deps.setContextStats({
    total: s.total,
    limit: s.limit,
    remaining: s.remaining,
    percentUsed: s.percentUsed,
  })
}

// ============================================================================
// Conversation Context Builder
// ============================================================================

/**
 * Build a formatted conversation context string from previous messages.
 * This is passed as `context` to AgentExecutor so it has chat history.
 */
export function buildConversationContext(deps: ChatDeps, excludeId?: string): string {
  const msgs = deps.session.messages().filter((m) => m.id !== excludeId)

  if (msgs.length === 0) return ''

  const lines: string[] = ['## Previous conversation:']
  for (const m of msgs) {
    const role = m.role === 'user' ? 'User' : m.role === 'assistant' ? 'Assistant' : 'System'
    // Truncate very long messages to keep context manageable
    const content =
      m.content.length > 2000 ? `${m.content.slice(0, 2000)}... [truncated]` : m.content
    lines.push(`\n**${role}:** ${content}`)
  }

  return lines.join('\n')
}

// ============================================================================
// Chat System Prompt
// ============================================================================

/** Build a system prompt for chat mode that tells the LLM about its tools */
export function buildChatSystemPrompt(cwd: string, deps?: ChatDeps): string {
  const toolNames = getToolDefinitions().map((t) => t.name)
  const os = navigator.platform?.includes('Win')
    ? 'Windows'
    : navigator.platform?.includes('Mac')
      ? 'macOS'
      : 'Linux'
  const date = new Date().toLocaleDateString()

  let prompt = `You are AVA, an AI coding assistant. You have direct access to the user's project files and tools.

## ENVIRONMENT
- **Working Directory**: ${cwd}
- **Operating System**: ${os}
- **Date**: ${date}

## CAPABILITIES

You have access to the following tools: ${toolNames.join(', ')}

### File Operations
- **glob** — Find files by pattern (e.g., "**/*.ts", "src/**/*.js")
- **read** — Read file contents with optional line range
- **grep** — Search file contents with regex patterns
- **create** — Create new files
- **write** — Write/overwrite file contents
- **edit** — Modify specific parts of a file (preferred for changes)
- **delete** — Remove files
- **ls** — List directory contents

### Command Execution
- **bash** — Execute shell commands

### Search
- **codesearch** — Search codebase with context
- **websearch** — Search the web
- **webfetch** — Fetch and parse web pages

## TOOL USAGE
- Use tools to answer questions — read files, search code, run commands.
- Read files before modifying them. Use search tools to find relevant code.
- If a tool fails, analyze the error and try an alternative approach.
- Be direct and technical. Prefer using tools over asking the user to paste content.
- Keep changes minimal and focused. Only modify what's necessary.`

  // Append custom instructions
  if (deps) {
    const instructions = deps.settings.settings().generation.customInstructions.trim()
    if (instructions) {
      prompt += `\n\n## CUSTOM INSTRUCTIONS\n${instructions}`
    }
  }

  return prompt
}
