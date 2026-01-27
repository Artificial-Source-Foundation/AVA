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
import { createKnowledgeTools } from './knowledge.js'
import { createCheckpointTools, type CheckpointTools } from './checkpoint.js'
import { createBudgetTools, type BudgetTools } from './budget.js'
import { createSkillTools } from './skills.js'
import { createLockTools } from './locks.js'
import { createMessagingTools } from './messaging.js'
import { createDecompositionTools } from './decomposition.js'
import { createEpicTools } from './epic.js'
import { createTraceTools, TRACE_TOOL_NAMES } from './traces.js'
import { createSubagentTools, SUBAGENT_TOOL_NAMES } from './subagents.js'
import { createSessionStateTools, SESSION_STATE_TOOL_NAMES } from './session-state.js'
import { createSquadronTools } from './squadrons.js'

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
export { createKnowledgeTools } from './knowledge.js'
export { createCheckpointTools, type CheckpointTools } from './checkpoint.js'
export { createBudgetTools, type BudgetTools } from './budget.js'
export { createSkillTools } from './skills.js'
export type { SkillToolsConfig } from './skills.js'
export { createLockTools } from './locks.js'
export type { LockToolsConfig } from './locks.js'
export { createMessagingTools } from './messaging.js'
export type { MessagingToolsConfig } from './messaging.js'
export { createDecompositionTools } from './decomposition.js'
export type { DecompositionToolsConfig } from './decomposition.js'
export { createEpicTools } from './epic.js'
export type { EpicToolsConfig } from './epic.js'
export { createSubagentTools, SUBAGENT_TOOL_NAMES } from './subagents.js'
export { createSessionStateTools, SESSION_STATE_TOOL_NAMES } from './session-state.js'
export { createSquadronTools } from './squadrons.js'
export type { SquadronTools } from './squadrons.js'

// =============================================================================
// Knowledge Tools Type
// =============================================================================

export type KnowledgeTools = {
  knowledge_list: unknown
  knowledge_get: unknown
  knowledge_set: unknown
  knowledge_append: unknown
  knowledge_replace: unknown
}

export type SkillTools = {
  list_skills: unknown
  use_skill: unknown
  read_skill_file: unknown
  run_skill_script: unknown
  get_skill: unknown
}

export type LockTools = {
  lock_file: unknown
  unlock_file: unknown
  check_lock: unknown
  list_locks: unknown
  lock_files: unknown
  unlock_all: unknown
}

export type MessagingTools = {
  send_message: unknown
  check_inbox: unknown
  read_message: unknown
  reply_message: unknown
  get_thread: unknown
  message_stats: unknown
}

export type DecompositionTools = {
  decompose_task: unknown
  validate_decomposition: unknown
  search_similar_tasks: unknown
  redecompose: unknown
  list_strategies: unknown
  record_decomposition_outcome: unknown
}

export type EpicTools = {
  create_epic: unknown
  link_tasks_to_epic: unknown
  epic_status: unknown
  epic_breakdown: unknown
  sync_to_git: unknown
  list_epics: unknown
  update_epic: unknown
}

export type TraceTools = {
  trace_decision: unknown
  query_traces: unknown
  get_trace: unknown
  find_similar_decisions: unknown
  trace_stats: unknown
}

export type SubagentTools = {
  spawn_subagent: unknown
  subagent_status: unknown
  get_subagent_output: unknown
  wait_for_subagent: unknown
  list_pending_outputs: unknown
}

export type SessionStateTools = {
  register_session: unknown
  set_session_state: unknown
  get_session_state: unknown
  list_sessions: unknown
  trigger_resume: unknown
  check_pending_resumes: unknown
}

export type SquadronToolsType = {
  spawn_squadron: unknown
  squadron_status: unknown
  wait_for_squadron: unknown
  list_squadrons: unknown
  cancel_squadron: unknown
}

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
  RoutingTools &
  KnowledgeTools &
  CheckpointTools &
  BudgetTools &
  SkillTools &
  LockTools &
  MessagingTools &
  DecompositionTools &
  EpicTools &
  TraceTools &
  SubagentTools &
  SessionStateTools &
  SquadronToolsType

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
    ...createCouncilTools(state, projectCwd, client),
    ...createMemoryTools(projectCwd),
    ...createDiagnosticsTools(state, projectCwd, client),
    ...createRoutingTools(),
    ...createKnowledgeTools(),
    ...createCheckpointTools(projectCwd),
    ...createBudgetTools(projectCwd),
    ...createSkillTools({ cwd: projectCwd }),
    ...createLockTools(),
    ...createMessagingTools(),
    ...createDecompositionTools(),
    ...createEpicTools(state, { cwd: projectCwd }),
    ...createTraceTools(),
    ...createSubagentTools(state, projectCwd, client),
    ...createSessionStateTools(),
    ...createSquadronTools(state, projectCwd, client),
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

export const ROUTING_TOOL_NAMES = ['analyze_complexity', 'recommend_agent'] as const

export const KNOWLEDGE_TOOL_NAMES = [
  'knowledge_list',
  'knowledge_get',
  'knowledge_set',
  'knowledge_append',
  'knowledge_replace',
] as const

export const CHECKPOINT_TOOL_NAMES = [
  'checkpoint_create',
  'checkpoint_list',
  'checkpoint_restore',
  'checkpoint_delete',
  'checkpoint_get',
] as const

export const BUDGET_TOOL_NAMES = [
  'budget_status',
  'budget_set_limit',
  'budget_check',
  'budget_breakdown',
] as const

export const SKILL_TOOL_NAMES = [
  'list_skills',
  'use_skill',
  'read_skill_file',
  'run_skill_script',
  'get_skill',
] as const

export const LOCK_TOOL_NAMES = [
  'lock_file',
  'unlock_file',
  'check_lock',
  'list_locks',
  'lock_files',
  'unlock_all',
] as const

export const MESSAGING_TOOL_NAMES = [
  'send_message',
  'check_inbox',
  'read_message',
  'reply_message',
  'get_thread',
  'message_stats',
] as const

export const DECOMPOSITION_TOOL_NAMES = [
  'decompose_task',
  'validate_decomposition',
  'search_similar_tasks',
  'redecompose',
  'list_strategies',
  'record_decomposition_outcome',
] as const

export const EPIC_TOOL_NAMES = [
  'create_epic',
  'link_tasks_to_epic',
  'epic_status',
  'epic_breakdown',
  'sync_to_git',
  'list_epics',
  'update_epic',
] as const

export const SQUADRON_TOOL_NAMES = [
  'spawn_squadron',
  'squadron_status',
  'wait_for_squadron',
  'list_squadrons',
  'cancel_squadron',
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
  ...KNOWLEDGE_TOOL_NAMES,
  ...CHECKPOINT_TOOL_NAMES,
  ...BUDGET_TOOL_NAMES,
  ...SKILL_TOOL_NAMES,
  ...LOCK_TOOL_NAMES,
  ...MESSAGING_TOOL_NAMES,
  ...DECOMPOSITION_TOOL_NAMES,
  ...EPIC_TOOL_NAMES,
  ...TRACE_TOOL_NAMES,
  ...SUBAGENT_TOOL_NAMES,
  ...SESSION_STATE_TOOL_NAMES,
  ...SQUADRON_TOOL_NAMES,
] as const

export type Delta9ToolName = (typeof ALL_TOOL_NAMES)[number]
