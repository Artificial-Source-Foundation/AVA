/**
 * Delta9 Mission Module Exports
 */

// State manager
export { MissionState } from './state.js'

// Markdown generator
export { generateMissionMarkdown } from './markdown.js'

// History
export {
  appendHistory,
  logEvent,
  readHistory,
  readMissionHistory,
  readHistoryByType,
  readRecentHistory,
  getHistoryStats,
  searchHistory,
  type HistoryStats,
} from './history.js'

// Failure handling
export {
  handleTaskFailure,
  analyzeFailure,
  canAutoRecover,
  getFailureStats,
  type FailureContext,
  type FailureResponse,
  type RecoveryAttempt,
} from './failure-handler.js'

// Checkpoints
export {
  CheckpointManager,
  createCheckpointManager,
  generateObjectiveCheckpointName,
  describeCheckpoint,
  type Checkpoint,
  type CheckpointOptions,
  type RestoreResult,
} from './checkpoints.js'

// Recovery
export {
  RecoveryManager,
  createRecoveryManager,
  describeFailureAnalysis,
  describeRecoveryResult,
  type RecoveryStrategy,
  type RecoveryConfig,
  type FailureAnalysis,
  type FailureType,
  type RecoveryExecutionAttempt,
  type RecoveryResult,
} from './recovery.js'
