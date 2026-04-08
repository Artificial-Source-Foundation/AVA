/**
 * Agent Hook Modules
 * Barrel export for extracted agent hook logic
 */

export { type AgentEventSignals, createAgentEventHandler } from './agent-events'
export {
  addToolActivity,
  updateToolActivity,
  updateToolActivityBatch,
} from './agent-tool-activity'
export type { AgentState, ApprovalRequest, ToolActivity } from './agent-types'
