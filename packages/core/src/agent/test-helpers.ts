/**
 * Agent Test Helpers
 * Mock factories for agent types
 */

import type {
  AgentConfig,
  AgentEvent,
  AgentEventType,
  AgentFinishEvent,
  AgentResult,
  AgentStartEvent,
  AgentStep,
  ErrorEvent,
  RecoveryFinishEvent,
  RecoveryStartEvent,
  ThoughtEvent,
  ToolCallInfo,
  ToolErrorEvent,
  ToolFinishEvent,
  ToolMetadataEvent,
  ToolStartEvent,
  TurnFinishEvent,
  TurnStartEvent,
} from './types.js'
import { AgentTerminateMode } from './types.js'

// ============================================================================
// Mock Factories
// ============================================================================

/**
 * Create a mock AgentStep with defaults
 */
export function createMockStep(overrides?: Partial<AgentStep>): AgentStep {
  return {
    id: `step-${Math.random().toString(36).slice(2, 9)}`,
    turn: 0,
    description: 'Test step',
    toolsCalled: [],
    status: 'success',
    retryCount: 0,
    startedAt: Date.now() - 1000,
    completedAt: Date.now(),
    ...overrides,
  }
}

/**
 * Create a mock ToolCallInfo with defaults
 */
export function createMockToolCallInfo(overrides?: Partial<ToolCallInfo>): ToolCallInfo {
  return {
    name: 'test_tool',
    args: {},
    success: true,
    durationMs: 100,
    ...overrides,
  }
}

/**
 * Create a mock AgentResult with defaults
 */
export function createMockResult(overrides?: Partial<AgentResult>): AgentResult {
  return {
    success: true,
    terminateMode: AgentTerminateMode.GOAL,
    output: 'Test output',
    steps: [createMockStep()],
    tokensUsed: 1000,
    durationMs: 5000,
    turns: 1,
    ...overrides,
  }
}

/**
 * Create a mock AgentConfig with defaults
 */
export function createMockConfig(overrides?: Partial<AgentConfig>): AgentConfig {
  return {
    maxTimeMinutes: 5,
    maxTurns: 10,
    maxRetries: 3,
    gracePeriodMs: 60000,
    provider: 'anthropic',
    ...overrides,
  }
}

/**
 * Create a mock AgentEvent with type-specific defaults
 */
export function createMockEvent(
  type: AgentEventType,
  overrides?: Record<string, unknown>
): AgentEvent {
  const base = {
    agentId: 'test-agent',
    timestamp: Date.now(),
  }

  switch (type) {
    case 'agent:start':
      return {
        ...base,
        type: 'agent:start',
        goal: 'test goal',
        config: createMockConfig(),
        ...overrides,
      } as AgentStartEvent

    case 'agent:finish':
      return {
        ...base,
        type: 'agent:finish',
        result: createMockResult(),
        ...overrides,
      } as AgentFinishEvent

    case 'turn:start':
      return {
        ...base,
        type: 'turn:start',
        turn: 0,
        ...overrides,
      } as TurnStartEvent

    case 'turn:finish':
      return {
        ...base,
        type: 'turn:finish',
        turn: 0,
        toolCalls: [],
        ...overrides,
      } as TurnFinishEvent

    case 'tool:start':
      return {
        ...base,
        type: 'tool:start',
        toolName: 'test_tool',
        args: {},
        ...overrides,
      } as ToolStartEvent

    case 'tool:finish':
      return {
        ...base,
        type: 'tool:finish',
        toolName: 'test_tool',
        success: true,
        output: '',
        durationMs: 100,
        ...overrides,
      } as ToolFinishEvent

    case 'tool:error':
      return {
        ...base,
        type: 'tool:error',
        toolName: 'test_tool',
        error: 'test error',
        ...overrides,
      } as ToolErrorEvent

    case 'tool:metadata':
      return {
        ...base,
        type: 'tool:metadata',
        toolName: 'test_tool',
        metadata: {},
        ...overrides,
      } as ToolMetadataEvent

    case 'thought':
      return {
        ...base,
        type: 'thought',
        text: 'thinking...',
        ...overrides,
      } as ThoughtEvent

    case 'error':
      return {
        ...base,
        type: 'error',
        error: 'test error',
        ...overrides,
      } as ErrorEvent

    case 'recovery:start':
      return {
        ...base,
        type: 'recovery:start',
        reason: AgentTerminateMode.ERROR,
        turn: 0,
        ...overrides,
      } as RecoveryStartEvent

    case 'recovery:finish':
      return {
        ...base,
        type: 'recovery:finish',
        success: true,
        durationMs: 100,
        ...overrides,
      } as RecoveryFinishEvent

    default: {
      // Exhaustive check
      const _exhaustive: never = type
      throw new Error(`Unknown event type: ${_exhaustive}`)
    }
  }
}
