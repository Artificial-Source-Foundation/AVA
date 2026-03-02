/**
 * Context Tracking
 * Token tracking sync, conversation context building, and chat system prompt.
 * Compaction is now handled by AgentExecutor internally.
 */

import { getToolDefinitions } from '@ava/core-v2/tools'
import {
  addPromptSection,
  buildSystemPrompt,
} from '../../../packages/extensions/prompts/src/builder.js'
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

/**
 * Build a system prompt using the shared prompts extension pipeline.
 * Same builder the CLI uses — ensures identity, tool guidelines, model-family
 * adjustments, and project instructions (CLAUDE.md) are all included.
 *
 * Dynamic per-request context (cwd, OS, tools, custom instructions) is added
 * as temporary sections, built, then cleaned up.
 */
export function buildChatSystemPrompt(cwd: string, model?: string, deps?: ChatDeps): string {
  const toolNames = getToolDefinitions().map((t) => t.name)
  const os = navigator.platform?.includes('Win')
    ? 'Windows'
    : navigator.platform?.includes('Mac')
      ? 'macOS'
      : 'Linux'
  const date = new Date().toLocaleDateString()

  // Add temporary sections for per-request dynamic context
  const removers: Array<() => void> = []

  removers.push(
    addPromptSection({
      name: 'environment',
      priority: 50,
      content: `## Environment\n- Working directory: ${cwd}\n- OS: ${os}\n- Date: ${date}`,
    })
  )

  removers.push(
    addPromptSection({
      name: 'behavior',
      priority: 20,
      content:
        'Do the work without asking questions. Infer missing details from the codebase.\n' +
        'If a task is ambiguous, pick the most reasonable interpretation and execute.',
    })
  )

  removers.push(
    addPromptSection({
      name: 'tool-list',
      priority: 60,
      content: `Available tools: ${toolNames.join(', ')}`,
    })
  )

  if (deps) {
    const instructions = deps.settings.settings().generation.customInstructions.trim()
    if (instructions) {
      removers.push(
        addPromptSection({
          name: 'custom-instructions',
          priority: 200,
          content: `## Custom Instructions\n${instructions}`,
        })
      )
    }
  }

  // Build using the shared pipeline (includes identity, tool-guidelines,
  // project instructions from session:opened, and model-family adjustments)
  const prompt = buildSystemPrompt(model)

  // Clean up temporary sections
  for (const remove of removers) remove()

  return prompt
}
