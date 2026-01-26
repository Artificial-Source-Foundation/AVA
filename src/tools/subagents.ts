/**
 * Delta9 Subagent Tools
 *
 * Tools for spawning and managing async subagents.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import type { OpenCodeClient } from '../lib/background-manager.js'
import { getSubagentManager } from '../subagents/index.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// Agent types
const AGENT_TYPES = [
  'operator',
  'operator_complex',
  'validator',
  'explorer',
  'scout',
  'intel',
  'ui_ops',
  'scribe',
  'qa',
] as const

// =============================================================================
// Tool Factory
// =============================================================================

export function createSubagentTools(
  state: MissionState,
  cwd: string,
  client?: OpenCodeClient
): Record<string, ToolDefinition> {
  const manager = getSubagentManager(state, cwd, client)

  /**
   * Spawn a new subagent
   */
  const spawn_subagent = tool({
    description: `Spawn an async subagent with a human-readable alias.

**Purpose:** Fire-and-forget agent creation for parallel work.

**Key Features:**
- Human-readable aliases (e.g., "code_searcher", "test_writer")
- Automatic state tracking (spawning → active → completed/failed)
- Output automatically collected when complete

**Agent Types:**
- operator: General implementation (default)
- operator_complex: Multi-file changes
- explorer: Deep codebase exploration
- scout: Quick reconnaissance
- intel: Research and info gathering
- validator: Verification tasks
- ui_ops: UI/UX focused tasks
- scribe: Documentation tasks
- qa: Quality assurance

**Examples:**
- spawn_subagent(alias="auth_explorer", prompt="Find all authentication handlers")
- spawn_subagent(alias="test_gen", prompt="Generate tests for user.ts", agent="qa")

**Related:** subagent_status, get_subagent_output, wait_for_subagent`,

    args: {
      alias: s.string().describe('Human-readable alias (e.g., "code_searcher", "doc_writer")'),
      prompt: s.string().describe('Task prompt for the subagent'),
      agent: s.enum(AGENT_TYPES).optional().describe('Agent type (default: operator)'),
      context: s.string().optional().describe('Additional context to include'),
    },

    async execute(args, ctx) {
      // Validate alias format
      if (!/^[a-z][a-z0-9_]*$/.test(args.alias)) {
        return JSON.stringify({
          success: false,
          error:
            'Invalid alias format. Use lowercase letters, numbers, underscores (e.g., "code_searcher")',
        })
      }

      try {
        const subagent = await manager.spawn({
          alias: args.alias,
          prompt: args.prompt,
          agentType: args.agent,
          context: args.context,
          parentSessionId: ctx.sessionID,
        })

        return JSON.stringify({
          success: true,
          alias: subagent.alias,
          taskId: subagent.taskId,
          state: subagent.state,
          agent: subagent.agentType,
          message: `Subagent "${subagent.alias}" spawned. Use subagent_status or get_subagent_output to check progress.`,
        })
      } catch (error) {
        return JSON.stringify({
          success: false,
          error: error instanceof Error ? error.message : String(error),
        })
      }
    },
  })

  /**
   * Get subagent status
   */
  const subagent_status = tool({
    description: `Check status of one or all subagents.

**Purpose:** Monitor subagent progress and see which ones have completed.

**Examples:**
- Check specific: subagent_status(alias="code_searcher")
- Check all: subagent_status()

**States:**
- spawning: Task queued, waiting for slot
- active: Agent is working
- completed: Work done, output available
- failed: Error occurred`,

    args: {
      alias: s.string().optional().describe('Specific subagent alias (omit for all)'),
    },

    async execute(args, _ctx) {
      if (args.alias) {
        const subagent = manager.getByAlias(args.alias)
        if (!subagent) {
          return JSON.stringify({
            success: false,
            error: `Subagent not found: ${args.alias}`,
          })
        }

        return JSON.stringify({
          success: true,
          subagent: {
            alias: subagent.alias,
            taskId: subagent.taskId,
            state: subagent.state,
            agent: subagent.agentType,
            spawnedAt: subagent.spawnedAt,
            completedAt: subagent.completedAt,
            hasOutput: !!subagent.output,
            hasError: !!subagent.error,
          },
        })
      }

      // List all subagents
      const subagents = manager.list()
      const stats = manager.getStats()

      return JSON.stringify({
        success: true,
        stats: {
          total: stats.total,
          active: stats.byState.active + stats.byState.spawning,
          completed: stats.byState.completed,
          failed: stats.byState.failed,
          pendingDelivery: stats.pendingDelivery,
        },
        subagents: subagents.map((s) => ({
          alias: s.alias,
          state: s.state,
          agent: s.agentType,
          hasOutput: !!s.output,
        })),
      })
    },
  })

  /**
   * Get subagent output
   */
  const get_subagent_output = tool({
    description: `Retrieve output from a completed subagent.

**Purpose:** Get the results of a subagent's work.

**Examples:**
- get_subagent_output(alias="code_searcher")
- get_subagent_output(alias="test_gen", mark_delivered=true)`,

    args: {
      alias: s.string().describe('Subagent alias'),
      mark_delivered: s.boolean().optional().describe('Mark output as delivered (default: false)'),
    },

    async execute(args, _ctx) {
      const subagent = manager.getByAlias(args.alias)
      if (!subagent) {
        return JSON.stringify({
          success: false,
          error: `Subagent not found: ${args.alias}`,
        })
      }

      if (subagent.state === 'spawning' || subagent.state === 'active') {
        return JSON.stringify({
          success: false,
          state: subagent.state,
          error: `Subagent still ${subagent.state}. Wait for completion or use wait_for_subagent.`,
        })
      }

      if (subagent.state === 'failed') {
        return JSON.stringify({
          success: false,
          state: 'failed',
          error: subagent.error || 'Unknown error',
        })
      }

      // Mark as delivered if requested
      if (args.mark_delivered) {
        manager.markDelivered([args.alias])
      }

      return JSON.stringify({
        success: true,
        alias: subagent.alias,
        state: subagent.state,
        output: subagent.output,
        duration: subagent.completedAt
          ? new Date(subagent.completedAt).getTime() - new Date(subagent.spawnedAt).getTime()
          : undefined,
      })
    },
  })

  /**
   * Wait for subagent to complete
   */
  const wait_for_subagent = tool({
    description: `Wait for a subagent to complete and return its output.

**Purpose:** Block until subagent finishes, then get result.

**Examples:**
- wait_for_subagent(alias="code_searcher")
- wait_for_subagent(alias="test_gen", timeout_ms=60000)`,

    args: {
      alias: s.string().describe('Subagent alias'),
      timeout_ms: s.number().optional().describe('Timeout in milliseconds (default: 30 minutes)'),
    },

    async execute(args, _ctx) {
      try {
        const result = await manager.waitFor(args.alias, args.timeout_ms)

        return JSON.stringify({
          success: result.state === 'completed',
          alias: result.alias,
          state: result.state,
          output: result.output,
          error: result.error,
          duration_ms: result.duration,
        })
      } catch (error) {
        return JSON.stringify({
          success: false,
          error: error instanceof Error ? error.message : String(error),
        })
      }
    },
  })

  /**
   * List pending outputs
   */
  const list_pending_outputs = tool({
    description: `List all completed subagent outputs that haven't been retrieved yet.

**Purpose:** Find out which subagents have finished and have results waiting.`,

    args: {},

    async execute(_args, ctx) {
      const pending = manager.getPendingOutputs(ctx.sessionID || '')

      return JSON.stringify({
        success: true,
        count: pending.length,
        pending: pending.map((p) => ({
          alias: p.alias,
          state: p.state,
          duration_ms: p.duration,
          preview: p.output?.slice(0, 200) + (p.output && p.output.length > 200 ? '...' : ''),
        })),
        message:
          pending.length > 0
            ? `${pending.length} subagent(s) have pending outputs. Use get_subagent_output to retrieve.`
            : 'No pending outputs.',
      })
    },
  })

  return {
    spawn_subagent,
    subagent_status,
    get_subagent_output,
    wait_for_subagent,
    list_pending_outputs,
  }
}

export const SUBAGENT_TOOL_NAMES = [
  'spawn_subagent',
  'subagent_status',
  'get_subagent_output',
  'wait_for_subagent',
  'list_pending_outputs',
] as const
