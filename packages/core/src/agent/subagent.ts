/**
 * Subagent System
 * Spawn and manage specialized subagents for complex tasks
 *
 * Based on OpenCode's subagent pattern
 */

import type { AgentConfig, AgentEvent } from './types.js'

// ============================================================================
// Types
// ============================================================================

/**
 * Built-in subagent types
 */
export type SubagentType = 'explore' | 'plan' | 'execute' | 'custom'

/**
 * Configuration for a subagent
 */
export interface SubagentConfig {
  /** Unique identifier for the subagent */
  id: string
  /** Human-readable name */
  name: string
  /** Description of what this subagent does */
  description: string
  /** Subagent type */
  type: SubagentType
  /** Tools this subagent can use (whitelist) */
  allowedTools?: string[]
  /** Tools this subagent cannot use (blacklist) */
  blockedTools?: string[]
  /** Maximum turns before termination */
  maxTurns?: number
  /** Custom system prompt additions */
  systemPrompt?: string
  /** Parent session ID (for context inheritance) */
  parentSessionId?: string
}

/**
 * Task for a subagent to execute
 */
export interface SubagentTask {
  /** Short description (3-5 words) */
  description: string
  /** Full task prompt */
  prompt: string
  /** Optional working directory override */
  workingDirectory?: string
}

/**
 * Result from a subagent execution
 */
export interface SubagentResult {
  /** Subagent ID */
  subagentId: string
  /** Whether the task was completed successfully */
  success: boolean
  /** Final output from the subagent */
  output: string
  /** Total turns taken */
  turns: number
  /** Reason for termination */
  terminationReason: 'completed' | 'max_turns' | 'error' | 'cancelled'
  /** Error message if failed */
  error?: string
  /** Session ID for resumption */
  sessionId: string
}

/**
 * Events emitted by subagents
 */
export type SubagentEvent =
  | { type: 'subagent_started'; subagentId: string; task: SubagentTask }
  | { type: 'subagent_progress'; subagentId: string; turn: number; event: AgentEvent }
  | { type: 'subagent_completed'; subagentId: string; result: SubagentResult }
  | { type: 'subagent_error'; subagentId: string; error: string }

/**
 * Listener for subagent events
 */
export type SubagentEventListener = (event: SubagentEvent) => void

// ============================================================================
// Subagent Presets
// ============================================================================

/**
 * Tool configurations for built-in subagent types
 */
export const SUBAGENT_PRESETS: Record<Exclude<SubagentType, 'custom'>, Partial<SubagentConfig>> = {
  /**
   * Explore: Read-only codebase exploration
   * Tools: glob, grep, read, ls
   */
  explore: {
    name: 'Explorer',
    description: 'Explore and understand the codebase',
    allowedTools: ['glob', 'grep', 'read', 'ls'],
    maxTurns: 20,
    systemPrompt: `You are an exploration agent. Your goal is to understand the codebase.
- Use glob to find files
- Use grep to search for patterns
- Use read to examine file contents
- Use ls to explore directory structure

Provide a summary of your findings when done.`,
  },

  /**
   * Plan: Planning without code execution
   * Tools: glob, grep, read, ls, write (for plan files only)
   */
  plan: {
    name: 'Planner',
    description: 'Plan implementation without executing code',
    allowedTools: ['glob', 'grep', 'read', 'ls', 'write'],
    maxTurns: 15,
    systemPrompt: `You are a planning agent. Your goal is to create implementation plans.
- Explore the codebase to understand existing patterns
- Identify files that need to be modified
- Create a detailed step-by-step plan
- Do NOT execute the plan, only document it

Output your plan as a structured document.`,
  },

  /**
   * Execute: Full code execution capabilities
   * Tools: All tools available
   */
  execute: {
    name: 'Executor',
    description: 'Execute code changes and tasks',
    maxTurns: 50,
    systemPrompt: `You are an execution agent. Your goal is to complete the task.
- Follow the plan if provided
- Make changes carefully
- Verify changes work
- Report completion when done`,
  },
}

// ============================================================================
// Subagent Manager
// ============================================================================

/**
 * Manages subagent lifecycle and communication
 */
export class SubagentManager {
  /** Active subagents */
  private subagents = new Map<string, SubagentConfig>()

  /** Event listener */
  private onEvent?: SubagentEventListener

  constructor(onEvent?: SubagentEventListener) {
    this.onEvent = onEvent
  }

  /**
   * Create a subagent configuration
   */
  createConfig(type: SubagentType, overrides?: Partial<SubagentConfig>): SubagentConfig {
    const id = `subagent-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`

    // Start with preset if not custom
    const preset = type !== 'custom' ? SUBAGENT_PRESETS[type] : {}

    return {
      id,
      type,
      name: overrides?.name ?? preset.name ?? 'Subagent',
      description: overrides?.description ?? preset.description ?? 'Custom subagent',
      allowedTools: overrides?.allowedTools ?? preset.allowedTools,
      blockedTools: overrides?.blockedTools ?? preset.blockedTools,
      maxTurns: overrides?.maxTurns ?? preset.maxTurns ?? 30,
      systemPrompt: overrides?.systemPrompt ?? preset.systemPrompt,
      parentSessionId: overrides?.parentSessionId,
    }
  }

  /**
   * Get agent configuration for a subagent
   * This transforms SubagentConfig into AgentConfig
   */
  toAgentConfig(config: SubagentConfig): Partial<AgentConfig> {
    return {
      maxTurns: config.maxTurns,
      // Note: systemPrompt is handled separately by SubagentManager
      // Tool filtering is done in the tool registry, not here
    }
  }

  /**
   * Filter tools based on subagent configuration
   */
  filterTools<T extends { name: string }>(tools: T[], config: SubagentConfig): T[] {
    // If allowedTools is set, only allow those
    if (config.allowedTools && config.allowedTools.length > 0) {
      return tools.filter((t) => config.allowedTools!.includes(t.name))
    }

    // If blockedTools is set, remove those
    if (config.blockedTools && config.blockedTools.length > 0) {
      return tools.filter((t) => !config.blockedTools!.includes(t.name))
    }

    return tools
  }

  /**
   * Register an active subagent
   */
  register(config: SubagentConfig): void {
    this.subagents.set(config.id, config)
  }

  /**
   * Unregister a subagent
   */
  unregister(id: string): void {
    this.subagents.delete(id)
  }

  /**
   * Get a subagent configuration
   */
  get(id: string): SubagentConfig | undefined {
    return this.subagents.get(id)
  }

  /**
   * Get all active subagents
   */
  getAll(): SubagentConfig[] {
    return Array.from(this.subagents.values())
  }

  /**
   * Emit a subagent event
   */
  emit(event: SubagentEvent): void {
    this.onEvent?.(event)
  }

  /**
   * Format result for parent agent consumption
   */
  formatResult(result: SubagentResult): string {
    const lines: string[] = [
      `=== Subagent Result (${result.subagentId}) ===`,
      '',
      `Status: ${result.success ? 'Completed' : 'Failed'}`,
      `Turns: ${result.turns}`,
      `Termination: ${result.terminationReason}`,
    ]

    if (result.error) {
      lines.push(`Error: ${result.error}`)
    }

    lines.push('')
    lines.push('Output:')
    lines.push(result.output)

    return lines.join('\n')
  }

  /**
   * Clear all subagents
   */
  clear(): void {
    this.subagents.clear()
  }
}

// ============================================================================
// Factory Functions
// ============================================================================

/**
 * Create a new SubagentManager
 */
export function createSubagentManager(onEvent?: SubagentEventListener): SubagentManager {
  return new SubagentManager(onEvent)
}

/**
 * Generate a unique session ID for a subagent
 */
export function generateSubagentSessionId(parentId: string, subagentId: string): string {
  return `${parentId}:${subagentId}`
}

/**
 * Check if a session ID is for a subagent
 */
export function isSubagentSession(sessionId: string): boolean {
  return sessionId.includes(':subagent-')
}

/**
 * Extract parent session ID from a subagent session ID
 */
export function getParentSessionId(subagentSessionId: string): string | undefined {
  if (!isSubagentSession(subagentSessionId)) {
    return undefined
  }
  return subagentSessionId.split(':subagent-')[0]
}
