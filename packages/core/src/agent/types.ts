/**
 * Agent Types
 * Core types for the autonomous agent loop
 *
 * Based on Gemini CLI's agents/types.ts pattern
 */

import type { LLMProvider } from '../types/llm.js'

// ============================================================================
// Termination Modes
// ============================================================================

/**
 * Describes the possible termination modes for an agent
 */
export enum AgentTerminateMode {
  /** Agent encountered an error */
  ERROR = 'ERROR',
  /** Agent exceeded time limit */
  TIMEOUT = 'TIMEOUT',
  /** Agent completed its goal successfully */
  GOAL = 'GOAL',
  /** Agent exceeded maximum turn limit */
  MAX_TURNS = 'MAX_TURNS',
  /** Agent was aborted by user/signal */
  ABORTED = 'ABORTED',
  /** Agent stopped without calling attempt_completion */
  NO_COMPLETE_TASK = 'NO_COMPLETE_TASK',
  /** Agent detected repeating the same action (doom loop) */
  DOOM_LOOP = 'DOOM_LOOP',
}

// ============================================================================
// Agent Configuration
// ============================================================================

/**
 * Configuration for an agent execution
 */
export interface AgentConfig {
  /** Unique identifier for the agent */
  id?: string
  /** Display name for the agent */
  name?: string
  /** Maximum execution time in minutes */
  maxTimeMinutes: number
  /** Maximum number of conversation turns */
  maxTurns: number
  /** Maximum retries on recoverable errors */
  maxRetries: number
  /** List of tools the agent can use (empty = all available) */
  tools?: string[]
  /** Grace period in ms for final completion attempt */
  gracePeriodMs: number
  /** LLM provider to use */
  provider?: LLMProvider
  /** Model to use (provider-specific) */
  model?: string
  /** Enable validation pipeline before task completion */
  validationEnabled?: boolean
  /** Max retries for agent to fix validation failures (default: 2) */
  maxValidationRetries?: number
  /** Tool mode: full (all tools), minimal (core only), plan (read-only) */
  toolMode?: 'full' | 'minimal' | 'plan'
}

/**
 * Default agent configuration
 */
export const DEFAULT_AGENT_CONFIG: Omit<AgentConfig, 'maxTimeMinutes' | 'maxTurns'> = {
  maxRetries: 3,
  gracePeriodMs: 60 * 1000, // 1 minute
  provider: 'anthropic',
}

// ============================================================================
// Agent Step
// ============================================================================

/**
 * Status of an agent step
 */
export type AgentStepStatus = 'pending' | 'running' | 'success' | 'failed' | 'skipped'

/**
 * Represents a single step in the agent's execution
 */
export interface AgentStep {
  /** Unique step identifier */
  id: string
  /** Turn number when this step was executed */
  turn: number
  /** Description of what this step does */
  description: string
  /** Tools called during this step */
  toolsCalled: ToolCallInfo[]
  /** Current status of the step */
  status: AgentStepStatus
  /** Output/result from the step */
  output?: string
  /** Error message if failed */
  error?: string
  /** How many times this step has been retried */
  retryCount: number
  /** Timestamp when step started */
  startedAt?: number
  /** Timestamp when step completed */
  completedAt?: number
}

/**
 * Information about a tool call within a step
 */
export interface ToolCallInfo {
  /** Name of the tool */
  name: string
  /** Arguments passed to the tool */
  args: Record<string, unknown>
  /** Result from the tool */
  result?: string
  /** Whether the tool call succeeded */
  success: boolean
  /** Duration in ms */
  durationMs?: number
}

// ============================================================================
// Agent Result
// ============================================================================

/**
 * Result of an agent execution
 */
export interface AgentResult {
  /** Whether the agent completed its goal */
  success: boolean
  /** How the agent terminated */
  terminateMode: AgentTerminateMode
  /** Final output/result from the agent */
  output: string
  /** All steps executed by the agent */
  steps: AgentStep[]
  /** Total tokens used (input + output) */
  tokensUsed: number
  /** Total duration in milliseconds */
  durationMs: number
  /** Number of turns taken */
  turns: number
  /** Error message if failed */
  error?: string
}

// ============================================================================
// Agent Events
// ============================================================================

/**
 * Event types emitted during agent execution
 */
export type AgentEventType =
  | 'agent:start'
  | 'agent:finish'
  | 'turn:start'
  | 'turn:finish'
  | 'tool:start'
  | 'tool:finish'
  | 'tool:error'
  | 'tool:metadata'
  | 'thought'
  | 'recovery:start'
  | 'recovery:finish'
  | 'validation:start'
  | 'validation:result'
  | 'validation:finish'
  | 'provider:switch'
  | 'error'

/**
 * Base agent event
 */
export interface AgentEventBase {
  /** Event type */
  type: AgentEventType
  /** Agent ID */
  agentId: string
  /** Timestamp */
  timestamp: number
}

/**
 * Agent start event
 */
export interface AgentStartEvent extends AgentEventBase {
  type: 'agent:start'
  /** Goal/task for the agent */
  goal: string
  /** Agent configuration */
  config: AgentConfig
}

/**
 * Agent finish event
 */
export interface AgentFinishEvent extends AgentEventBase {
  type: 'agent:finish'
  /** Final result */
  result: AgentResult
}

/**
 * Turn start event
 */
export interface TurnStartEvent extends AgentEventBase {
  type: 'turn:start'
  /** Turn number (0-indexed) */
  turn: number
}

/**
 * Turn finish event
 */
export interface TurnFinishEvent extends AgentEventBase {
  type: 'turn:finish'
  /** Turn number */
  turn: number
  /** Tool calls made in this turn */
  toolCalls: ToolCallInfo[]
  /** Input tokens used in this turn */
  tokensIn?: number
  /** Output tokens used in this turn */
  tokensOut?: number
}

/**
 * Tool start event
 */
export interface ToolStartEvent extends AgentEventBase {
  type: 'tool:start'
  /** Tool name */
  toolName: string
  /** Tool arguments */
  args: Record<string, unknown>
}

/**
 * Tool finish event
 */
export interface ToolFinishEvent extends AgentEventBase {
  type: 'tool:finish'
  /** Tool name */
  toolName: string
  /** Whether tool succeeded */
  success: boolean
  /** Tool output */
  output: string
  /** Duration in ms */
  durationMs: number
}

/**
 * Tool error event
 */
export interface ToolErrorEvent extends AgentEventBase {
  type: 'tool:error'
  /** Tool name */
  toolName: string
  /** Error message */
  error: string
}

/**
 * Tool metadata update event
 * Emitted when a tool streams progressive updates during execution
 */
export interface ToolMetadataEvent extends AgentEventBase {
  type: 'tool:metadata'
  /** Tool name */
  toolName: string
  /** Metadata title (optional) */
  title?: string
  /** Metadata payload */
  metadata: Record<string, unknown>
}

/**
 * Thought/reasoning event
 */
export interface ThoughtEvent extends AgentEventBase {
  type: 'thought'
  /** Thought text */
  text: string
}

/**
 * Recovery start event
 */
export interface RecoveryStartEvent extends AgentEventBase {
  type: 'recovery:start'
  /** Reason for recovery */
  reason: AgentTerminateMode
  /** Turn number */
  turn: number
}

/**
 * Recovery finish event
 */
export interface RecoveryFinishEvent extends AgentEventBase {
  type: 'recovery:finish'
  /** Whether recovery succeeded */
  success: boolean
  /** Duration in ms */
  durationMs: number
}

/**
 * Error event
 */
export interface ErrorEvent extends AgentEventBase {
  type: 'error'
  /** Error message */
  error: string
  /** Error context */
  context?: string
}

/**
 * Validation start event
 */
export interface ValidationStartEvent extends AgentEventBase {
  type: 'validation:start'
  /** Files being validated */
  files: string[]
}

/**
 * Validation result event
 */
export interface ValidationResultEvent extends AgentEventBase {
  type: 'validation:result'
  /** Whether validation passed */
  passed: boolean
  /** Summary of validation results */
  summary: string
}

/**
 * Validation finish event
 */
export interface ValidationFinishEvent extends AgentEventBase {
  type: 'validation:finish'
  /** Whether validation passed */
  passed: boolean
  /** Duration in ms */
  durationMs: number
}

/**
 * Provider switch event
 */
export interface ProviderSwitchEvent extends AgentEventBase {
  type: 'provider:switch'
  /** New provider */
  provider: string
  /** New model */
  model: string
}

/**
 * Union of all agent events
 */
export type AgentEvent =
  | AgentStartEvent
  | AgentFinishEvent
  | TurnStartEvent
  | TurnFinishEvent
  | ToolStartEvent
  | ToolFinishEvent
  | ToolErrorEvent
  | ToolMetadataEvent
  | ThoughtEvent
  | RecoveryStartEvent
  | RecoveryFinishEvent
  | ValidationStartEvent
  | ValidationResultEvent
  | ValidationFinishEvent
  | ProviderSwitchEvent
  | ErrorEvent

/**
 * Callback for agent events
 */
export type AgentEventCallback = (event: AgentEvent) => void

// ============================================================================
// Agent Inputs
// ============================================================================

/**
 * Inputs passed to an agent
 */
export interface AgentInputs {
  /** The goal/task for the agent to accomplish */
  goal: string
  /** Optional context information */
  context?: string
  /** Working directory */
  cwd: string
  /** Additional parameters */
  [key: string]: unknown
}

// ============================================================================
// Internal Types
// ============================================================================

/**
 * Result of a single agent turn
 */
export type AgentTurnResult =
  | {
      status: 'continue'
      toolCalls: ToolCallInfo[]
    }
  | {
      status: 'stop'
      terminateMode: AgentTerminateMode
      result: string | null
    }

/**
 * The attempt_completion tool name constant
 */
export const COMPLETE_TASK_TOOL = 'attempt_completion'
