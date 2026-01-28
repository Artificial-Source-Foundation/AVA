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

/**
 * Agent types accepted by delegate_task.
 * Includes both registered names and common aliases.
 */
export type AgentType =
  // Core agents (3-tier Marine system)
  | 'operator' // Alias for operator-tier2 (backward compat)
  | 'operator_complex' // Alias for operator-tier3 (backward compat)
  | 'operator_tier1' // Marine Private: Simple tasks
  | 'operator_tier2' // Marine Sergeant: Moderate tasks
  | 'operator_tier3' // Delta Force: Critical/complex tasks
  | 'validator'
  // Support agents - registered names (config keys)
  | 'scout'
  | 'intel'
  | 'strategist'
  | 'patcher'
  | 'qa'
  | 'scribe'
  | 'uiOps'
  // Strategic Advisors (6 members)
  | 'cipher'
  | 'vector'
  | 'apex'
  | 'aegis'
  | 'razor'
  | 'oracle'
  // Aliases (resolved to registered names)
  | 'validator_strict' // → validator
  | 'explorer' // → scout
  | 'ui_ops' // → uiOps
  | 'marine_private' // → operator_tier1
  | 'marine_sergeant' // → operator_tier2
  | 'delta_force' // → operator_tier3
  | 'marine' // → operator_tier2 (default marine)

/**
 * Map aliases to registered agent names (BUG-12 fix, BUG-18 enhancement)
 *
 * Allows Commander to use intuitive names while ensuring
 * delegate_task routes to properly registered agents.
 *
 * Supports:
 * - Snake_case variants (ui_ops -> uiOps)
 * - Hyphen variants (ui-ops -> uiOps)
 * - Config name mappings (frontend-ui-ux-engineer -> uiOps)
 * - Semantic aliases (explorer -> scout, frontend -> uiOps)
 * - Marine tier aliases (marine_private -> operator_tier1)
 */
const AGENT_ALIASES: Record<string, string> = {
  // 3-tier Marine system aliases
  marine_private: 'operator_tier1', // Simple tasks
  'marine-private': 'operator_tier1',
  marine_sergeant: 'operator_tier2', // Moderate tasks
  'marine-sergeant': 'operator_tier2',
  delta_force: 'operator_tier3', // Critical/complex tasks
  'delta-force': 'operator_tier3',
  marine: 'operator_tier2', // Default marine = sergeant tier
  // Backward compatibility
  operator_complex: 'operator_tier3', // Complex → tier3 (Delta Force)
  'operator-complex': 'operator_tier3',
  operator: 'operator_tier2', // Default operator → tier2 (Marine Sergeant)
  // Snake_case to camelCase
  ui_ops: 'uiOps',
  validator_strict: 'validator', // Strict mode handled by validator config
  // Hyphen variants (BUG-18)
  'ui-ops': 'uiOps',
  // Config name mappings (BUG-18)
  'frontend-ui-ux-engineer': 'uiOps',
  // Semantic aliases
  explorer: 'scout', // Explorer is really scout/RECON
  frontend: 'uiOps', // Frontend work goes to FACADE
  research: 'intel', // Research goes to SIGINT
  fix: 'patcher', // Quick fixes go to SURGEON
  test: 'qa', // Testing goes to SENTINEL
  docs: 'scribe', // Documentation goes to SCRIBE
  visual: 'uiOps', // Visual tasks go to FACADE (SPECTRE removed)
}

/**
 * Resolve agent type to registered name.
 * Uses case-insensitive matching with normalization.
 *
 * @param agentType - Agent type string (any case/format)
 * @returns Resolved agent name or original if no alias exists
 */
export function resolveAgentType(agentType: string): string {
  // Direct alias match (fast path)
  if (AGENT_ALIASES[agentType]) {
    return AGENT_ALIASES[agentType]
  }

  // Case-insensitive match with normalization (BUG-18)
  // Normalize: lowercase and convert hyphens/underscores to consistent format
  const normalized = agentType.toLowerCase().replace(/-/g, '_')
  for (const [alias, target] of Object.entries(AGENT_ALIASES)) {
    const normalizedAlias = alias.toLowerCase().replace(/-/g, '_')
    if (normalizedAlias === normalized) {
      return target
    }
  }

  return agentType
}

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

**Agent Types (3-Tier Marine System):**
- operator_tier1 / marine_private: Simple tasks (typos, formatting, minor fixes)
- operator_tier2 / marine_sergeant: Moderate tasks (features, components, general implementation) [DEFAULT]
- operator_tier3 / delta_force: Critical/complex tasks (refactoring, architecture, migrations)
- operator: Alias for operator_tier2 (backward compat)
- operator_complex: Alias for operator_tier3 (backward compat)
- validator: Verify work against acceptance criteria

**Support Agents:**
- scout: Quick reconnaissance and file discovery
- intel: Research and information gathering
- strategist: Guidance and alternative approaches
- patcher: Quick fixes and patches
- qa: Quality assurance and testing
- scribe: Documentation and writing tasks
- ui_ops: UI/UX focused tasks

**Strategic Advisors:**
- cipher, vector, apex, aegis, razor, oracle

**Execution Modes:**
- Background (run_in_background=true): Returns immediately with task ID. Use background_output to check results.
- Synchronous (default): Waits for agent to complete before returning.

**Examples:**
- Simple fix: delegate_task(prompt="Fix typo in header", agent="operator_tier1")
- Feature implementation: delegate_task(prompt="Add error handling to login", agent="operator_tier2")
- Complex refactor: delegate_task(prompt="Refactor auth system", agent="operator_tier3")
- Background exploration: delegate_task(prompt="Find all auth handlers", agent="scout", run_in_background=true)

**Related:** background_output, background_list, background_cancel, mission_status`,

    args: {
      prompt: s.string().describe('Task prompt describing what the agent should do'),
      agent: s
        .enum([
          // 3-tier Marine system
          'operator_tier1', // Marine Private: Simple tasks
          'operator_tier2', // Marine Sergeant: Moderate tasks
          'operator_tier3', // Delta Force: Critical/complex tasks
          // Backward compat aliases
          'operator', // → operator_tier2
          'operator_complex', // → operator_tier3
          'validator',
          // Support agents (registered names)
          'scout',
          'intel',
          'strategist',
          'patcher',
          'qa',
          'scribe',
          'uiOps',
          // Strategic Advisors (6 members)
          'cipher',
          'vector',
          'apex',
          'aegis',
          'razor',
          'oracle',
          // Marine aliases
          'marine_private', // → operator_tier1
          'marine_sergeant', // → operator_tier2
          'delta_force', // → operator_tier3
          'marine', // → operator_tier2 (default)
          // Other aliases
          'validator_strict',
          'explorer',
          'ui_ops',
        ])
        .optional()
        .describe(
          'Agent type (default: operator_tier2). Marine tiers: tier1=simple, tier2=moderate, tier3=critical'
        ),
      run_in_background: s
        .boolean()
        .optional()
        .describe('Run asynchronously in background (default: false)'),
      wait: s
        .boolean()
        .optional()
        .describe(
          'BUG-35 FIX: Wait for completion when run_in_background=true (default: false). Returns result when done.'
        ),
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
      // Resolve agent aliases to registered names (BUG-12 fix)
      const requestedAgent = args.agent || 'operator'
      const agentType = resolveAgentType(requestedAgent)
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

      // BUG-28 FIX: Auto-create task when taskId not provided but mission exists
      let missionTaskId = args.taskId
      if (!missionTaskId && mission) {
        const currentObjective = state.getCurrentObjective()
        if (currentObjective) {
          const autoTask = state.addTask(currentObjective.id, {
            description: `[Delegated] ${args.prompt.substring(0, 100)}${args.prompt.length > 100 ? '...' : ''}`,
            acceptanceCriteria: ['Task delegated to agent and completed'],
            routedTo: agentType,
          })
          missionTaskId = autoTask.id
        }
      }

      // Background execution
      if (args.run_in_background) {
        // Extract session ID for Ctrl+X navigation
        const parentSessionId = extractSessionId(ctx)

        // Mark mission task as in_progress (BUG-14 fix)
        if (missionTaskId && mission) {
          state.startTask(missionTaskId, agentType)
        }

        const bgTaskId = await manager.launch({
          prompt: fullPrompt,
          agent: agentType,
          missionTaskId: missionTaskId,
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

        // BUG-35 FIX: If wait=true, block until completion
        if (args.wait) {
          try {
            const output = await manager.getOutput(bgTaskId) // Blocks until completion
            return JSON.stringify({
              success: true,
              backgroundTaskId: bgTaskId,
              agent: agentType,
              status: '\u2705 completed',
              mode: sdkAvailable ? 'live' : 'simulation',
              output: output ? JSON.parse(output) : null,
              message: `Task completed by ${agentType} agent`,
            })
          } catch (error) {
            return JSON.stringify({
              success: false,
              backgroundTaskId: bgTaskId,
              agent: agentType,
              status: '\u274C failed',
              error: error instanceof Error ? error.message : String(error),
              message: 'Task execution failed while waiting',
            })
          }
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
        // Mark mission task as in_progress (BUG-14 fix)
        if (missionTaskId && mission) {
          state.startTask(missionTaskId, agentType)
        }

        const result = await manager.executeSync({
          prompt: fullPrompt,
          agent: agentType,
          missionTaskId: missionTaskId,
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
 * BUG-29 FIX: Try multiple paths to find session ID
 */
function extractSessionId(ctx: unknown): string | null {
  if (typeof ctx !== 'object' || ctx === null) {
    return null
  }

  const context = ctx as {
    sessionID?: string
    sessionId?: string
    session?: { id?: string }
    info?: { sessionId?: string }
  }

  // Try multiple paths
  return (
    context.sessionID ?? context.sessionId ?? context.session?.id ?? context.info?.sessionId ?? null
  )
}

// =============================================================================
// Type Export
// =============================================================================

export type DelegationTools = ReturnType<typeof createDelegationTools>
