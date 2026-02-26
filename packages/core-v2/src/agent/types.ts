/**
 * Agent types — config, events, results.
 */

import type { LLMProvider } from '../llm/types.js'

// ─── Termination ─────────────────────────────────────────────────────────────

export enum AgentTerminateMode {
  ERROR = 'ERROR',
  TIMEOUT = 'TIMEOUT',
  GOAL = 'GOAL',
  MAX_TURNS = 'MAX_TURNS',
  ABORTED = 'ABORTED',
}

// ─── Config ──────────────────────────────────────────────────────────────────

export interface AgentConfig {
  id?: string
  name?: string
  maxTimeMinutes: number
  maxTurns: number
  provider?: LLMProvider
  model?: string
  toolMode?: string
  systemPrompt?: string
}

export const DEFAULT_AGENT_CONFIG: Omit<AgentConfig, 'maxTimeMinutes' | 'maxTurns'> = {
  provider: 'anthropic',
  model: 'claude-sonnet-4-20250514',
}

// ─── Tool Call Info ──────────────────────────────────────────────────────────

export interface ToolCallInfo {
  name: string
  args: Record<string, unknown>
  result?: string
  success: boolean
  durationMs?: number
}

// ─── Result ──────────────────────────────────────────────────────────────────

export interface AgentResult {
  success: boolean
  terminateMode: AgentTerminateMode
  output: string
  turns: number
  tokensUsed: { input: number; output: number }
  durationMs: number
  error?: string
}

// ─── Events ──────────────────────────────────────────────────────────────────

export type AgentEvent =
  | { type: 'agent:start'; agentId: string; goal: string }
  | { type: 'agent:finish'; agentId: string; result: AgentResult }
  | { type: 'turn:start'; agentId: string; turn: number }
  | { type: 'turn:end'; agentId: string; turn: number; toolCalls: ToolCallInfo[] }
  | { type: 'tool:start'; agentId: string; toolName: string; args: Record<string, unknown> }
  | { type: 'tool:finish'; agentId: string; toolName: string; success: boolean; durationMs: number }
  | { type: 'thought'; agentId: string; content: string }
  | { type: 'error'; agentId: string; error: string }

export type AgentEventCallback = (event: AgentEvent) => void

// ─── Inputs ──────────────────────────────────────────────────────────────────

export interface AgentInputs {
  goal: string
  context?: string
  cwd: string
}

// ─── Turn Result ─────────────────────────────────────────────────────────────

export interface TurnUsage {
  inputTokens: number
  outputTokens: number
}

export type AgentTurnResult =
  | { status: 'continue'; toolCalls: ToolCallInfo[]; usage?: TurnUsage }
  | { status: 'stop'; terminateMode: AgentTerminateMode; result: string | null; usage?: TurnUsage }

export const COMPLETE_TASK_TOOL = 'attempt_completion'
