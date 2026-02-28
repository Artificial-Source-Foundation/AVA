/**
 * Context Tracking & Memory
 * Handles token tracking sync, auto-compaction, memory recall, and API message building.
 */

import { getToolDefinitions } from '@ava/core-v2/tools'
import { getCoreBudget } from '../../services/core-bridge'
import { deleteMessageFromDb } from '../../services/database'
import { logInfo, logWarn } from '../../services/logger'
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
// Auto-Compaction
// ============================================================================

/**
 * Auto-compact conversation when context exceeds 80%.
 * Uses sliding window to trim to ~50%, syncs state + DB.
 */
export async function maybeCompact(deps: ChatDeps): Promise<void> {
  const budget = getCoreBudget()
  if (!budget || !budget.needsCompaction(80)) return

  const currentMsgs = deps.session.messages()
  if (currentMsgs.length <= 4) return

  const coreMessages = currentMsgs.map((m) => ({
    id: m.id,
    role: m.role as 'user' | 'assistant' | 'system',
    content: m.content,
  }))

  try {
    const result = await budget.compact(coreMessages)
    if (result.tokensSaved === 0) return

    const keptIds = new Set(result.messages.map((m) => m.id))
    const removedMsgs = currentMsgs.filter((m) => !keptIds.has(m.id))

    deps.session.setMessages(currentMsgs.filter((m) => keptIds.has(m.id)))
    await Promise.all(removedMsgs.map((m) => deleteMessageFromDb(m.id)))

    budget.clear()
    for (const m of result.messages) {
      budget.addMessage(m.id, m.content)
    }
    syncTrackerStats(deps)

    const removed = result.originalCount - result.compactedCount
    logInfo(deps.LOG_SRC, 'Compaction complete', {
      removed,
      tokensSaved: result.tokensSaved,
      strategy: result.strategyUsed,
    })

    // Notify the UI so a toast can be shown
    window.dispatchEvent(
      new CustomEvent('ava:compacted', {
        detail: { removed, tokensSaved: result.tokensSaved },
      })
    )
  } catch (err) {
    logWarn(deps.LOG_SRC, 'Compaction failed', err)
  }
}

// ============================================================================
// Chat System Prompt
// ============================================================================

/** Build a system prompt for chat mode that tells the LLM about its tools */
function buildChatSystemPrompt(cwd: string): string {
  const toolNames = getToolDefinitions().map((t) => t.name)
  const os = navigator.platform?.includes('Win')
    ? 'Windows'
    : navigator.platform?.includes('Mac')
      ? 'macOS'
      : 'Linux'
  const date = new Date().toLocaleDateString()

  return `You are AVA, an AI coding assistant. You have direct access to the user's project files and tools.

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
}

// ============================================================================
// API Message Building
// ============================================================================

/** Build the messages array to send to the LLM API */
export async function buildApiMessages(
  deps: ChatDeps,
  excludeId?: string
): Promise<Array<{ role: 'user' | 'assistant' | 'system'; content: string | unknown[] }>> {
  const msgs = deps.session
    .messages()
    .filter((m) => m.id !== excludeId)
    .map((m) => {
      // Build multimodal content if message has images
      const imgs = (m.metadata?.images ?? []) as Array<{
        data: string
        mimeType: string
      }>
      if (imgs.length > 0) {
        return {
          role: m.role as 'user' | 'assistant' | 'system',
          content: [
            ...imgs.map((img) => ({
              type: 'image' as const,
              source: { type: 'base64' as const, media_type: img.mimeType, data: img.data },
            })),
            { type: 'text' as const, text: m.content },
          ] as unknown as string,
        }
      }
      return {
        role: m.role as 'user' | 'assistant' | 'system',
        content: m.content,
      }
    })

  // Prepend custom instructions as system message
  const instructions = deps.settings.settings().generation.customInstructions.trim()
  if (instructions) {
    msgs.unshift({ role: 'system', content: instructions })
  }

  // Prepend chat system prompt (before custom instructions)
  const cwd = deps.currentProject()?.directory || '.'
  msgs.unshift({ role: 'system', content: buildChatSystemPrompt(cwd) })

  return msgs
}
