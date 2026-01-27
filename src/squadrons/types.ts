/**
 * Delta9 Squadron Types
 *
 * Wave-based batch agent execution.
 * Squadrons execute multiple agents in parallel waves,
 * with automatic advancement between waves.
 *
 * Pattern:
 * Wave 1: Scout + Intel → parallel reconnaissance
 *    ↓ [wait for all]
 * Wave 2: Operators → parallel implementation
 *    ↓ [wait for all]
 * Wave 3: Validator → verify work
 */

import { z } from 'zod'

// =============================================================================
// Configuration
// =============================================================================

export interface SquadronConfig {
  /** Maximum agents per wave (default: 4) */
  maxConcurrentPerWave: number
  /** Timeout per wave in milliseconds (default: 10 minutes) */
  waveTimeout: number
  /** Automatically advance to next wave when current completes (default: true) */
  autoAdvance: boolean
}

export const DEFAULT_SQUADRON_CONFIG: SquadronConfig = {
  maxConcurrentPerWave: 4,
  waveTimeout: 10 * 60 * 1000, // 10 minutes
  autoAdvance: true,
}

// =============================================================================
// Schemas
// =============================================================================

export const WaveStatusSchema = z.enum(['pending', 'running', 'completed', 'failed'])
export type WaveStatus = z.infer<typeof WaveStatusSchema>

export const SquadronStatusSchema = z.enum([
  'pending',
  'running',
  'completed',
  'failed',
  'cancelled',
])
export type SquadronStatus = z.infer<typeof SquadronStatusSchema>

export const WaveAgentSchema = z.object({
  /** Unique ID for this agent within the squadron */
  id: z.string(),

  /** Agent type (scout, intel, operator, validator) */
  agentType: z.string(),

  /** Task prompt for this agent */
  prompt: z.string(),

  /** Optional context to inject */
  context: z.string().optional(),

  /** Skills to load */
  skills: z.array(z.string()).optional(),

  /** Subagent ID (assigned after spawn) */
  subagentId: z.string().optional(),

  /** Human-readable alias (assigned after spawn) */
  alias: z.string().optional(),

  /** Current state */
  state: z
    .enum(['pending', 'spawning', 'active', 'idle', 'completed', 'failed'])
    .default('pending'),

  /** Output (populated when completed) */
  output: z.string().optional(),

  /** Error (populated when failed) */
  error: z.string().optional(),

  /** When this agent was spawned */
  spawnedAt: z.string().optional(),

  /** When this agent completed */
  completedAt: z.string().optional(),
})

export type WaveAgent = z.infer<typeof WaveAgentSchema>

export const WaveSchema = z.object({
  /** Unique wave ID */
  id: z.string(),

  /** Wave number (1-indexed) */
  number: z.number().min(1),

  /** Agents in this wave */
  agents: z.array(WaveAgentSchema),

  /** Current status */
  status: WaveStatusSchema.default('pending'),

  /** When wave started */
  startedAt: z.string().optional(),

  /** When wave completed */
  completedAt: z.string().optional(),

  /** Wave timeout timestamp */
  timeoutAt: z.string().optional(),
})

export type Wave = z.infer<typeof WaveSchema>

export const SquadronSchema = z.object({
  /** Unique squadron ID (e.g., "sqd_abc123") */
  id: z.string(),

  /** Human-readable alias (e.g., "alpha-strike") */
  alias: z.string(),

  /** Description of what this squadron is doing */
  description: z.string(),

  /** All waves in execution order */
  waves: z.array(WaveSchema),

  /** Current wave number (1-indexed, 0 = not started) */
  currentWave: z.number().default(0),

  /** Squadron status */
  status: SquadronStatusSchema.default('pending'),

  /** Parent session ID (the session that spawned this squadron) */
  parentSessionId: z.string().optional(),

  /** Configuration overrides */
  config: z
    .object({
      maxConcurrentPerWave: z.number().optional(),
      waveTimeout: z.number().optional(),
      autoAdvance: z.boolean().optional(),
    })
    .optional(),

  /** When squadron was created */
  createdAt: z.string(),

  /** When squadron completed */
  completedAt: z.string().optional(),
})

export type Squadron = z.infer<typeof SquadronSchema>

// =============================================================================
// Input Types
// =============================================================================

export interface WaveAgentInput {
  /** Agent type (scout, intel, operator, validator) */
  type: string
  /** Task prompt */
  prompt: string
  /** Optional context */
  context?: string
  /** Skills to load */
  skills?: string[]
}

export interface WaveInput {
  /** Agents to run in this wave */
  agents: WaveAgentInput[]
}

export interface SpawnSquadronInput {
  /** Description of what this squadron is doing */
  description: string
  /** Waves to execute in order */
  waves: WaveInput[]
  /** Optional alias (auto-generated if not provided) */
  alias?: string
  /** Parent session ID */
  parentSessionId?: string
  /** Configuration overrides */
  config?: Partial<SquadronConfig>
}

// =============================================================================
// Result Types
// =============================================================================

export interface WaveResult {
  /** Wave number */
  number: number
  /** Final status */
  status: WaveStatus
  /** Agent results */
  agents: Array<{
    agentType: string
    alias?: string
    state: string
    output?: string
    error?: string
    duration?: number
  }>
  /** Total wave duration in ms */
  duration?: number
}

export interface SquadronResult {
  /** Squadron ID */
  id: string
  /** Squadron alias */
  alias: string
  /** Final status */
  status: SquadronStatus
  /** All wave results */
  waves: WaveResult[]
  /** Total duration in ms */
  duration?: number
  /** Error if failed */
  error?: string
}

// =============================================================================
// Query Types
// =============================================================================

export interface SquadronQuery {
  /** Filter by status */
  status?: SquadronStatus
  /** Filter by parent session */
  parentSessionId?: string
}

export interface SquadronStats {
  total: number
  byStatus: Record<SquadronStatus, number>
  activeWaves: number
  totalAgents: number
}

// =============================================================================
// Event Types (for notifications)
// =============================================================================

export type SquadronEventType =
  | 'squadron_started'
  | 'squadron_completed'
  | 'squadron_failed'
  | 'squadron_cancelled'
  | 'wave_started'
  | 'wave_completed'
  | 'wave_failed'
  | 'agent_started'
  | 'agent_completed'
  | 'agent_failed'

export interface SquadronEvent {
  type: SquadronEventType
  squadronId: string
  squadronAlias: string
  waveNumber?: number
  agentId?: string
  agentAlias?: string
  timestamp: string
  data?: Record<string, unknown>
}
