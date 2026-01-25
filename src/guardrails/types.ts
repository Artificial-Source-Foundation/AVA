/**
 * Delta9 Guardrails Types
 *
 * Type definitions for the guardrails system:
 * - Commander discipline (no-code rule)
 * - Three-strike error escalation
 * - Output validation
 */

import { z } from 'zod'

// =============================================================================
// Commander Discipline
// =============================================================================

/**
 * Code writing indicators to detect in Commander's responses
 */
export const CODE_PATTERNS = [
  // File operations
  /(?:^|\s)Write\s*\(/i,
  /(?:^|\s)Edit\s*\(/i,
  /(?:^|\s)file_path\s*:/i,
  // Direct code blocks
  /```(?:typescript|javascript|python|tsx?|jsx?|sh|bash)/i,
  // Code-related tool names
  /(?:^|\s)(?:create_file|write_file|edit_file|patch_file)\s*\(/i,
] as const

/**
 * Allowed Commander tools (read-only, planning)
 */
export const COMMANDER_ALLOWED_TOOLS = [
  // Read operations
  'Read',
  'Glob',
  'Grep',
  // Planning tools
  'mission_create',
  'mission_status',
  'mission_update',
  'mission_add_objective',
  'mission_add_task',
  // Delegation tools
  'delegate_task',
  'dispatch_task',
  'task_complete',
  'request_validation',
  // Council tools
  'consult_council',
  'quick_consult',
  'should_consult_council',
  'council_status',
  // Routing tools
  'analyze_complexity',
  'recommend_agent',
  'route_task',
  // Background tools
  'background_output',
  'background_list',
  // Knowledge tools
  'knowledge_list',
  'knowledge_get',
  // Diagnostics
  'delta9_health',
] as const

/**
 * Prohibited Commander tools (code-writing)
 */
export const COMMANDER_PROHIBITED_TOOLS = [
  'Write',
  'Edit',
  'Bash', // Could run code
  'run_tests', // Could modify state
] as const

export type CommanderAllowedTool = (typeof COMMANDER_ALLOWED_TOOLS)[number]
export type CommanderProhibitedTool = (typeof COMMANDER_PROHIBITED_TOOLS)[number]

/**
 * Commander discipline violation
 */
export interface CommanderViolation {
  type: 'tool_use' | 'code_block' | 'file_operation'
  tool?: string
  pattern?: string
  message: string
  timestamp: Date
}

// =============================================================================
// Three-Strike System
// =============================================================================

export const StrikeReasonSchema = z.enum([
  'validation_failed',
  'task_failed',
  'timeout',
  'budget_exceeded',
  'quality_rejected',
  'repeated_error',
  'other',
])

export type StrikeReason = z.infer<typeof StrikeReasonSchema>

export interface Strike {
  id: string
  agentId: string
  taskId?: string
  reason: StrikeReason
  message: string
  timestamp: Date
  context?: Record<string, unknown>
}

export const EscalationLevelSchema = z.enum([
  'none', // 0 strikes
  'warning', // 1 strike
  'retry_with_guidance', // 2 strikes
  'escalate_to_human', // 3 strikes
])

export type EscalationLevel = z.infer<typeof EscalationLevelSchema>

export interface StrikeStatus {
  agentId: string
  strikes: Strike[]
  level: EscalationLevel
  isEscalated: boolean
  canRetry: boolean
  lastStrike?: Strike
}

// =============================================================================
// Guardrails Configuration
// =============================================================================

export interface GuardrailsConfig {
  /** Enable Commander discipline enforcement */
  commanderDiscipline: boolean
  /** Enable three-strike system */
  threeStrikeEnabled: boolean
  /** Maximum strikes before escalation */
  maxStrikes: number
  /** Strike decay time (ms) - strikes expire after this duration */
  strikeDecayMs: number
  /** Enable output validation */
  outputValidation: boolean
}

export const DEFAULT_GUARDRAILS_CONFIG: GuardrailsConfig = {
  commanderDiscipline: true,
  threeStrikeEnabled: true,
  maxStrikes: 3,
  strikeDecayMs: 30 * 60 * 1000, // 30 minutes
  outputValidation: true,
}
