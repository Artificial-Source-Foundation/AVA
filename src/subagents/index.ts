/**
 * Delta9 Subagent Module
 *
 * Async subagent system with aliasing and output piping.
 */

// Types
export {
  SubagentStateSchema,
  SubagentSchema,
  type SubagentState,
  type Subagent,
  type SpawnSubagentInput,
  type SubagentOutput,
  type SubagentQuery,
  type SubagentStats,
} from './types.js'

// Manager
export { SubagentManager, getSubagentManager, resetSubagentManager } from './manager.js'
