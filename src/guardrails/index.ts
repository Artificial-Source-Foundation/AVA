/**
 * Delta9 Guardrails System
 *
 * Safety guardrails for agent operations:
 * - Output truncation (per-tool limits)
 * - Commander discipline (no-code rule)
 * - Three-strike error escalation
 */

// Types
export {
  CODE_PATTERNS,
  COMMANDER_ALLOWED_TOOLS,
  COMMANDER_PROHIBITED_TOOLS,
  type CommanderAllowedTool,
  type CommanderProhibitedTool,
  type CommanderViolation,
  type Strike,
  type StrikeReason,
  type StrikeStatus,
  type EscalationLevel,
  type GuardrailsConfig,
  DEFAULT_GUARDRAILS_CONFIG,
  StrikeReasonSchema,
  EscalationLevelSchema,
} from './types.js'

// Commander Discipline
export {
  CommanderDisciplineEnforcer,
  getDisciplineEnforcer,
  resetDisciplineEnforcer,
  isToolAllowed,
  checkToolUse,
  checkResponseForCode,
  getViolations,
  getRecentViolations,
  clearViolations,
  type DisciplineCheckResult,
  type CommanderDisciplineConfig,
} from './commander-discipline.js'

// Three-Strike System
export {
  StrikeManager,
  getStrikeManager,
  resetStrikeManager,
  addStrike,
  getAgentStatus,
  clearAgentStrikes,
  getAgentRetryGuidance,
  type StrikeManagerConfig,
  type AddStrikeOptions,
  type RetryGuidance,
} from './three-strike.js'
