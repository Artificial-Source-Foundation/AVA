/**
 * Delta9 Delegation Tools
 *
 * Tools for multi-agent coordination with background execution.
 * Pattern from oh-my-opencode's delegate_task.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import { getBackgroundManager, type OpenCodeClient } from '../lib/background-manager.js'
import { trackBackgroundTask } from '../hooks/session.js'
import { formatErrorResponse } from '../lib/errors.js'
import { hints } from '../lib/hints.js'
import { buildOperatorHandoff, formatHandoffForPrompt } from '../dispatch/handoff.js'
import { loadSkillsForAgent } from './skills.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Types
// =============================================================================

export type AgentType =
  | 'operator'
  | 'operator_complex'
  | 'validator'
  | 'validator_strict'
  | 'explorer'
  | 'scout'
  | 'intel'
  | 'ui_ops'
  | 'scribe'
  | 'qa'

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create delegation tools
 *
 * @param state - MissionState instance
 * @param cwd - Project root directory
 * @param client - Optional OpenCode SDK client for real agent execution
 */
export function createDelegationTools(
  state: MissionState,
  cwd: string,
  client?: OpenCodeClient
): Record<string, ToolDefinition> {
  const manager = getBackgroundManager(state, cwd, client)

  /**
   * Delegate a task to a specialized agent
   */
  const delegate_task = tool({
    description: `Spawn a specialized agent for task execution.

**Purpose:** Offload work to background agents for parallel exploration or synchronous execution.

**Agent Types:**
- operator: General implementation tasks (default)
- operator_complex: Multi-file changes, complex refactoring
- validator: Verify work against acceptance criteria
- validator_strict: Rigorous validation with strict checks
- explorer: Deep codebase exploration and analysis
- scout: Quick reconnaissance and file discovery
- intel: Research and information gathering
- ui_ops: UI/UX focused tasks
- scribe: Documentation and writing tasks
- qa: Quality assurance and testing

**Execution Modes:**
- Background (run_in_background=true): Returns immediately with task ID. Use background_output to check results.
- Synchronous (default): Waits for agent to complete before returning.

**Examples:**
- Background exploration: delegate_task(prompt="Find all auth handlers", agent="explorer", run_in_background=true)
- Sync implementation: delegate_task(prompt="Add error handling to login", agent="operator")
- With mission link: delegate_task(prompt="Implement feature", taskId="task_123")
- With context: delegate_task(prompt="Fix bug", context="User reported login fails on Safari")
- With skills: delegate_task(prompt="Build UI component", agent="ui_ops", loadSkills=["typescript-patterns", "frontend-ui-ux"])

**Related:** background_output, background_list, background_cancel, mission_status`,

    args: {
      prompt: s.string().describe('Task prompt describing what the agent should do'),
      agent: s
        .enum([
          'operator',
          'operator_complex',
          'validator',
          'validator_strict',
          'explorer',
          'scout',
          'intel',
          'ui_ops',
          'scribe',
          'qa',
        ])
        .optional()
        .describe('Agent type (default: operator)'),
      run_in_background: s
        .boolean()
        .optional()
        .describe('Run asynchronously in background (default: false)'),
      resume: s.string().optional().describe('Resume a previous session by ID'),
      taskId: s.string().optional().describe('Link to Delta9 mission task for context'),
      context: s.string().optional().describe('Additional context to prepend to prompt'),
      loadSkills: s
        .array(s.string())
        .optional()
        .describe(
          'Skill names to inject into agent context (e.g., ["typescript-patterns", "testing-patterns"])'
        ),
    },

    async execute(args, ctx) {
      const agentType = args.agent || 'operator'
      const mission = state.getMission()

      // Build prompt with context
      let fullPrompt = args.prompt
      if (args.context) {
        fullPrompt = `Context:\n${args.context}\n\nTask:\n${args.prompt}`
      }

      // Load and inject skills if specified
      if (args.loadSkills && args.loadSkills.length > 0) {
        const skillContents = await loadSkillsForAgent(cwd, args.loadSkills)
        if (skillContents.length > 0) {
          const skillsBlock = skillContents
            .map((content, i) => `<skill name="${args.loadSkills![i]}">\n${content}\n</skill>`)
            .join('\n\n')
          fullPrompt = `<skills>\n${skillsBlock}\n</skills>\n\n${fullPrompt}`
        }
      }

      // Add handoff contract if mission task is linked
      if (mission && args.taskId) {
        const task = state.getTask(args.taskId)
        if (task) {
          // Get all tasks for handoff context
          const allTasks = mission.objectives.flatMap((o) => o.tasks)

          // Build structured handoff contract
          const handoff = buildOperatorHandoff({
            task,
            mission,
            allTasks,
            additionalContext: args.context,
          })

          // Format and prepend to prompt
          const handoffPrompt = formatHandoffForPrompt(handoff)
          fullPrompt = `${handoffPrompt}\n\n---\n\nADDITIONAL INSTRUCTIONS:\n${args.prompt}`
        }
      }

      // Check if SDK is available
      const sdkAvailable = !!client

      // Background execution
      if (args.run_in_background) {
        // Extract session ID for Ctrl+X navigation
        const parentSessionId = extractSessionId(ctx)

        const bgTaskId = await manager.launch({
          prompt: fullPrompt,
          agent: agentType,
          missionTaskId: args.taskId,
          parentSessionId: parentSessionId ?? undefined,
          missionContext: mission
            ? {
                id: mission.id,
                description: mission.description,
                status: mission.status,
              }
            : undefined,
        })

        // Track in session state
        if (parentSessionId) {
          trackBackgroundTask(parentSessionId, bgTaskId)
        }

        return JSON.stringify({
          success: true,
          backgroundTaskId: bgTaskId,
          agent: agentType,
          status: '\u23F3 queued',
          mode: sdkAvailable ? 'live' : 'simulation',
          message: `Task queued for ${agentType} agent. Use background_output(taskId="${bgTaskId}") to check progress.`,
          hint: !sdkAvailable ? hints.simulationMode : undefined,
        })
      }

      // Resume previous session
      if (args.resume) {
        return JSON.stringify({
          success: true,
          resumed: true,
          sessionId: args.resume,
          message: `Resuming session ${args.resume}`,
          hint: 'Resume requires OpenCode SDK integration.',
        })
      }

      // Synchronous execution
      try {
        const result = await manager.executeSync({
          prompt: fullPrompt,
          agent: agentType,
          missionTaskId: args.taskId,
        })

        return JSON.stringify({
          success: true,
          agent: agentType,
          status: '\u2705 completed',
          mode: sdkAvailable ? 'live' : 'simulation',
          result: JSON.parse(result),
          message: `Task completed by ${agentType} agent`,
        })
      } catch (error) {
        return formatErrorResponse(error)
      }
    },
  })

  return {
    delegate_task,
  }
}

// =============================================================================
// Helpers
// =============================================================================

/**
 * Extract session ID from context
 */
function extractSessionId(ctx: unknown): string | null {
  const context = ctx as { sessionID?: string } | undefined
  return context?.sessionID ?? null
}

// =============================================================================
// Type Export
// =============================================================================

export type DelegationTools = ReturnType<typeof createDelegationTools>
