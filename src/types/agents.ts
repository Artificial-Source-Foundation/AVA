/**
 * Delta9 Agent Types
 *
 * Defines the structure of agents in the Delta9 system.
 */

// =============================================================================
// Agent Roles
// =============================================================================

export type AgentRole =
  | 'commander'
  | 'oracle'
  | 'operator'
  | 'validator'
  | 'patcher'
  | 'scout'
  | 'intel'
  | 'strategist'
  | 'ui-ops'
  | 'scribe'
  | 'optics'
  | 'qa'

// =============================================================================
// Operator Specialties
// =============================================================================

export type OperatorSpecialty =
  | 'general'
  | 'ui-ops'
  | 'qa'
  | 'scribe'
  | 'patcher'

// =============================================================================
// Oracle Specialties
// =============================================================================

export type OracleSpecialty =
  | 'architecture'
  | 'logic'
  | 'ui'
  | 'performance'
  | 'general'

// =============================================================================
// Agent Definition
// =============================================================================

export interface AgentDefinition {
  /** Agent name */
  name: string
  /** Agent role */
  role: AgentRole
  /** Model to use */
  model: string
  /** Temperature for generation */
  temperature: number
  /** System prompt */
  systemPrompt: string
  /** Available tools */
  tools: string[]
  /** Optional specialty */
  specialty?: OperatorSpecialty | OracleSpecialty
  /** Maximum tokens for response */
  maxTokens?: number
  /** Agent description */
  description?: string
}

// =============================================================================
// Agent Context
// =============================================================================

export interface AgentContext {
  /** Current working directory */
  cwd: string
  /** Project name */
  projectName?: string
  /** Current mission ID */
  missionId?: string
  /** Current objective ID */
  objectiveId?: string
  /** Current task ID */
  taskId?: string
  /** Additional context */
  metadata?: Record<string, unknown>
}

// =============================================================================
// Agent Invocation
// =============================================================================

export interface AgentInvocation {
  /** Agent to invoke */
  agent: AgentRole
  /** Input/prompt for the agent */
  input: string
  /** Context for the agent */
  context: AgentContext
  /** Parent session ID */
  parentSession?: string
  /** Timeout in milliseconds */
  timeout?: number
}

// =============================================================================
// Agent Response
// =============================================================================

export interface AgentResponse {
  /** Agent that responded */
  agent: AgentRole
  /** Response content */
  content: string
  /** Whether execution was successful */
  success: boolean
  /** Error message if failed */
  error?: string
  /** Tokens used */
  tokensUsed?: number
  /** Cost in dollars */
  cost?: number
  /** Duration in milliseconds */
  duration?: number
  /** Files changed (for operators) */
  filesChanged?: string[]
  /** Metadata */
  metadata?: Record<string, unknown>
}

// =============================================================================
// Dispatch Request
// =============================================================================

export interface DispatchRequest {
  /** Task ID being dispatched */
  taskId: string
  /** Task description */
  description: string
  /** Acceptance criteria */
  acceptanceCriteria: string[]
  /** Mission context */
  missionContext: string
  /** Objective context */
  objectiveContext: string
  /** Suggested routing (if any) */
  routing?: OperatorSpecialty
  /** Previous attempts (for retries) */
  previousAttempts?: {
    attempt: number
    error?: string
    feedback?: string
  }[]
}

// =============================================================================
// Validation Request
// =============================================================================

export interface ValidationRequest {
  /** Task ID being validated */
  taskId: string
  /** Task description */
  description: string
  /** Acceptance criteria to check */
  acceptanceCriteria: string[]
  /** Files that were changed */
  filesChanged: string[]
  /** Git diff of changes */
  diff?: string
  /** Operator's completion summary */
  completionSummary: string
}

// =============================================================================
// Agent Registry Entry
// =============================================================================

export interface AgentRegistryEntry {
  /** Agent definition */
  definition: AgentDefinition
  /** Whether agent is enabled */
  enabled: boolean
  /** Priority (for routing) */
  priority: number
  /** Cost per 1K tokens (approximate) */
  costPer1kTokens: number
}

// =============================================================================
// Agent Metrics
// =============================================================================

export interface AgentMetrics {
  /** Agent name */
  agent: string
  /** Total invocations */
  invocations: number
  /** Successful invocations */
  successes: number
  /** Failed invocations */
  failures: number
  /** Total tokens used */
  tokensUsed: number
  /** Total cost */
  cost: number
  /** Average duration in ms */
  avgDuration: number
}
