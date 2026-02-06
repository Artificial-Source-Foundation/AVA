/**
 * Message Bus Types
 * Type definitions for event-driven tool/UI communication
 */

import type { RiskLevel } from '../permissions/types.js'

// ============================================================================
// Message Types
// ============================================================================

export enum BusMessageType {
  // Tool confirmation flow
  TOOL_CONFIRMATION_REQUEST = 'tool-confirmation-request',
  TOOL_CONFIRMATION_RESPONSE = 'tool-confirmation-response',
  TOOL_POLICY_REJECTION = 'tool-policy-rejection',

  // Tool execution lifecycle
  TOOL_EXECUTION_START = 'tool-execution-start',
  TOOL_EXECUTION_SUCCESS = 'tool-execution-success',
  TOOL_EXECUTION_FAILURE = 'tool-execution-failure',

  // Policy updates
  UPDATE_POLICY = 'update-policy',

  // User interaction
  ASK_USER_REQUEST = 'ask-user-request',
  ASK_USER_RESPONSE = 'ask-user-response',

  // Tool call batch updates
  TOOL_CALLS_UPDATE = 'tool-calls-update',
}

// ============================================================================
// Base Message
// ============================================================================

export interface BusMessage {
  type: BusMessageType
  /** UUID for matching request/response pairs */
  correlationId: string
  /** Unix timestamp when message was created */
  timestamp: number
}

// ============================================================================
// Tool Confirmation Messages
// ============================================================================

export interface ToolConfirmationRequest extends BusMessage {
  type: BusMessageType.TOOL_CONFIRMATION_REQUEST
  toolName: string
  toolArgs: Record<string, unknown>
  riskLevel: RiskLevel
  /** Human-readable description of what the tool will do */
  description?: string
  /** MCP server name if applicable */
  serverName?: string
}

export interface ToolConfirmationResponse extends BusMessage {
  type: BusMessageType.TOOL_CONFIRMATION_RESPONSE
  confirmed: boolean
  /** Whether to remember this choice */
  rememberChoice?: 'session' | 'persistent' | false
  /** User's reason for denial */
  denyReason?: string
}

export interface ToolPolicyRejection extends BusMessage {
  type: BusMessageType.TOOL_POLICY_REJECTION
  toolName: string
  toolArgs: Record<string, unknown>
  reason: string
  denyMessage?: string
}

// ============================================================================
// Tool Execution Messages
// ============================================================================

export interface ToolExecutionStart extends BusMessage {
  type: BusMessageType.TOOL_EXECUTION_START
  toolName: string
  toolArgs: Record<string, unknown>
}

export interface ToolExecutionSuccess extends BusMessage {
  type: BusMessageType.TOOL_EXECUTION_SUCCESS
  toolName: string
  durationMs: number
  outputPreview?: string
}

export interface ToolExecutionFailure extends BusMessage {
  type: BusMessageType.TOOL_EXECUTION_FAILURE
  toolName: string
  error: string
  durationMs: number
}

// ============================================================================
// Policy Update Messages
// ============================================================================

export interface PolicyUpdate extends BusMessage {
  type: BusMessageType.UPDATE_POLICY
  action: 'add_rule' | 'remove_rule' | 'set_mode'
  payload: Record<string, unknown>
}

// ============================================================================
// User Interaction Messages
// ============================================================================

export interface AskUserRequest extends BusMessage {
  type: BusMessageType.ASK_USER_REQUEST
  question: string
  options?: string[]
}

export interface AskUserResponse extends BusMessage {
  type: BusMessageType.ASK_USER_RESPONSE
  answer: string
}

// ============================================================================
// Tool Calls Update
// ============================================================================

export interface ToolCallsUpdate extends BusMessage {
  type: BusMessageType.TOOL_CALLS_UPDATE
  activeCalls: number
  completedCalls: number
}

// ============================================================================
// Union Type
// ============================================================================

export type AnyBusMessage =
  | ToolConfirmationRequest
  | ToolConfirmationResponse
  | ToolPolicyRejection
  | ToolExecutionStart
  | ToolExecutionSuccess
  | ToolExecutionFailure
  | PolicyUpdate
  | AskUserRequest
  | AskUserResponse
  | ToolCallsUpdate
