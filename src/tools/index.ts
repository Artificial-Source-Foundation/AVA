/**
 * Delta9 Tools Module
 *
 * Exports all tool factories and types.
 */

import type { MissionState } from '../mission/state.js'
import { createMissionTools, type MissionTools } from './mission.js'
import { createDispatchTools, type DispatchTools } from './dispatch.js'
import { createValidationTools, type ValidationTools } from './validation.js'

// =============================================================================
// Tool Factory Exports
// =============================================================================

export { createMissionTools, type MissionTools } from './mission.js'
export { createDispatchTools, type DispatchTools } from './dispatch.js'
export { createValidationTools, type ValidationTools } from './validation.js'

// =============================================================================
// Combined Tools
// =============================================================================

export interface Delta9Tools extends MissionTools, DispatchTools, ValidationTools {}

/**
 * Create all Delta9 tools
 */
export function createDelta9Tools(state: MissionState): Delta9Tools {
  return {
    ...createMissionTools(state),
    ...createDispatchTools(state),
    ...createValidationTools(state),
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

export const ALL_TOOL_NAMES = [
  ...MISSION_TOOL_NAMES,
  ...DISPATCH_TOOL_NAMES,
  ...VALIDATION_TOOL_NAMES,
] as const

export type Delta9ToolName = typeof ALL_TOOL_NAMES[number]
