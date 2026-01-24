/**
 * Delta9 Tools Module
 *
 * Exports all tool factories and types.
 */

import type { ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import type { OpenCodeClient } from '../lib/background-manager.js'
import { createMissionTools, type MissionTools } from './mission.js'
import { createDispatchTools, type DispatchTools } from './dispatch.js'
import { createValidationTools, type ValidationTools } from './validation.js'
import { createDelegationTools, type DelegationTools } from './delegation.js'
import { createBackgroundTools, type BackgroundTools } from './background.js'
import { createCouncilTools, type CouncilTools } from './council.js'
import { createMemoryTools, type MemoryTools } from './memory.js'
import { createDiagnosticsTools, type DiagnosticsTools, setPluginStartTime } from './diagnostics.js'
import { createRoutingTools, type RoutingTools } from './routing.js'

export type { OpenCodeClient } from '../lib/background-manager.js'

// =============================================================================
// Tool Factory Exports
// =============================================================================

export { createMissionTools, type MissionTools } from './mission.js'
export { createDispatchTools, type DispatchTools } from './dispatch.js'
export { createValidationTools, type ValidationTools } from './validation.js'
export { createDelegationTools, type DelegationTools } from './delegation.js'
export { createBackgroundTools, type BackgroundTools } from './background.js'
export { createCouncilTools, type CouncilTools } from './council.js'
export { createMemoryTools, type MemoryTools } from './memory.js'
export { createDiagnosticsTools, type DiagnosticsTools, setPluginStartTime } from './diagnostics.js'
export { createRoutingTools, type RoutingTools } from './routing.js'

// =============================================================================
// Combined Tools
// =============================================================================

export type Delta9Tools = MissionTools &
  DispatchTools &
  ValidationTools &
  DelegationTools &
  BackgroundTools &
  CouncilTools &
  MemoryTools &
  DiagnosticsTools &
  RoutingTools

/**
 * Create all Delta9 tools
 *
 * @param state - MissionState instance
 * @param cwd - Project root directory (needed for background manager and council)
 * @param client - Optional OpenCode SDK client for real agent execution
 */
export function createDelta9Tools(
  state: MissionState,
  cwd?: string,
  client?: OpenCodeClient
): Record<string, ToolDefinition> {
  // Use state's internal cwd if not provided
  const projectCwd = cwd ?? process.cwd()

  // Set plugin start time for uptime tracking
  setPluginStartTime(Date.now())

  return {
    ...createMissionTools(state),
    ...createDispatchTools(state, projectCwd),
    ...createValidationTools(state, projectCwd),
    ...createDelegationTools(state, projectCwd, client),
    ...createBackgroundTools(state, projectCwd, client),
    ...createCouncilTools(state, projectCwd),
    ...createMemoryTools(projectCwd),
    ...createDiagnosticsTools(state, projectCwd, client),
    ...createRoutingTools(),
  }
}

// =============================================================================
// Tool Names
// =============================================================================

export const MISSION_TOOL_NAMES = [
  'mission_create',
  'mission_status',
  'mission_update',
  'mission_add_objective',
  'mission_add_task',
] as const

export const DISPATCH_TOOL_NAMES = [
  'dispatch_task',
  'task_complete',
  'request_validation',
  'retry_task',
] as const

export const VALIDATION_TOOL_NAMES = [
  'validation_result',
  'run_tests',
  'check_lint',
  'check_types',
] as const

export const DELEGATION_TOOL_NAMES = ['delegate_task'] as const

export const BACKGROUND_TOOL_NAMES = [
  'background_output',
  'background_cancel',
  'background_list',
  'background_cleanup',
] as const

export const COUNCIL_TOOL_NAMES = [
  'consult_council',
  'quick_consult',
  'should_consult_council',
  'council_status',
] as const

export const MEMORY_TOOL_NAMES = [
  'memory_list',
  'memory_get',
  'memory_set',
  'memory_replace',
  'memory_append',
  'memory_delete',
] as const

export const DIAGNOSTICS_TOOL_NAMES = ['delta9_health'] as const

export const ROUTING_TOOL_NAMES = [
  'analyze_complexity',
  'recommend_agent',
] as const

export const ALL_TOOL_NAMES = [
  ...MISSION_TOOL_NAMES,
  ...DISPATCH_TOOL_NAMES,
  ...VALIDATION_TOOL_NAMES,
  ...DELEGATION_TOOL_NAMES,
  ...BACKGROUND_TOOL_NAMES,
  ...COUNCIL_TOOL_NAMES,
  ...MEMORY_TOOL_NAMES,
  ...DIAGNOSTICS_TOOL_NAMES,
  ...ROUTING_TOOL_NAMES,
] as const

export type Delta9ToolName = (typeof ALL_TOOL_NAMES)[number]
